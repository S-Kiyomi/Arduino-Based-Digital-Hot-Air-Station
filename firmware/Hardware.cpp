#include "Hardware.h"
#include "UI.h"

// --- LOCAL VARIABLES ---
static uint8_t beepRemaining = 0;
static bool buzzerOn = false;
static uint32_t buzzerChangedAt = 0;
const uint16_t BEEP_ON_MS = 100;
const uint16_t BEEP_OFF_MS = 140;

volatile uint8_t triacDelayTicks = 90;
volatile uint8_t zcTicks = 0;
volatile uint8_t gateTicksRemaining = 0;
volatile bool zcSeen = false;
volatile bool triacAllowed = false;
volatile bool triacFiredThisHalfCycle = false;
volatile uint32_t lastZeroCrossMicros = 0;

volatile uint32_t systemMillis = 0;
volatile uint32_t timer0OverflowCount = 0;

static uint8_t previousEncoderAB = 0;
static uint32_t lastEncoderMoveMs = 0;
static uint32_t lastButtonChangeMs = 0;
static bool buttonStable = 1;
static bool buttonLastRead = 1;
static bool buttonPressedEvent = false;

// --- TIMER 0 (MILLIS / MICROS ENGINE) ---
void timer0_init() {
  TCCR0A = (1 << WGM01) | (1 << WGM00); // Fast PWM Mode
  TCCR0B = (1 << CS01) | (1 << CS00);   // Prescaler 64 (16MHz / 64 = 250kHz)
  TIMSK0 |= (1 << TOIE0);               // Overflow Interrupt Enable (~1.024ms per overflow)
}

ISR(TIMER0_OVF_vect) {
  timer0OverflowCount++;
  systemMillis++;
}

uint32_t millis() {
  uint32_t m;
  cli();
  m = systemMillis;
  sei();
  return m;
}

uint32_t micros() {
  uint32_t m;
  uint8_t t;
  cli();
  m = timer0OverflowCount;
  t = TCNT0;
  if ((TIFR0 & (1 << TOV0)) && (t < 255)) m++;
  sei();
  return ((m << 8) + t) * 4; // 1 tick = 4us
}

// --- BARE-METAL EEPROM HANDLER ---
void eeprom_write_byte_bare(uint16_t addr, uint8_t data) {
  while (EECR & (1 << EEPE)); // Wait for completion of previous write
  EEAR = addr;
  EEDR = data;
  EECR |= (1 << EEMPE);
  EECR |= (1 << EEPE);
}

uint8_t eeprom_read_byte_bare(uint16_t addr) {
  while (EECR & (1 << EEPE));
  EEAR = addr;
  EECR |= (1 << EERE);
  return EEDR;
}

void eeprom_write_word_bare(uint16_t addr, uint16_t data) {
  eeprom_write_byte_bare(addr, (uint8_t)(data & 0xFF));
  eeprom_write_byte_bare(addr + 1, (uint8_t)(data >> 8));
}

uint16_t eeprom_read_word_bare(uint16_t addr) {
  uint8_t low = eeprom_read_byte_bare(addr);
  uint8_t high = eeprom_read_byte_bare(addr + 1);
  return (high << 8) | low;
}

void saveSettingsToEEPROM() {
  eeprom_write_word_bare(EEPROM_ADDR_MAGIC, EEPROM_MAGIC_VALUE);
  eeprom_write_word_bare(EEPROM_ADDR_TARGET_C, (uint16_t)targetTempC);
  eeprom_write_word_bare(EEPROM_ADDR_FAN_PCT, (uint16_t)fanPercent);
}

void loadSettingsFromEEPROM() {
  if (eeprom_read_word_bare(EEPROM_ADDR_MAGIC) == EEPROM_MAGIC_VALUE) {
    targetTempC = (int)eeprom_read_word_bare(EEPROM_ADDR_TARGET_C);
    savedTargetTempC = targetTempC;
    fanPercent = (int)eeprom_read_word_bare(EEPROM_ADDR_FAN_PCT);
    savedFanPercent = fanPercent;
  }
}

// --- INTERRUPTS (ZERO CROSS & TRIAC TIMER2) ---
ISR(INT0_vect) { // Zero Cross Pin PD2
  PORTD &= ~(1 << PD7); // Pulse TRIAC LOW
  zcTicks = 0;
  gateTicksRemaining = 0;
  triacFiredThisHalfCycle = false;
  zcSeen = true;
  lastZeroCrossMicros = micros();
}

ISR(TIMER2_COMPA_vect) {
  if (!triacAllowed || !zcSeen) {
    PORTD &= ~(1 << PD7);
    gateTicksRemaining = 0;
    return;
  }
  if (gateTicksRemaining > 0) {
    PORTD &= ~(1 << PD7);
    gateTicksRemaining--;
    return;
  }
  if (zcTicks < 100) zcTicks++;
  if (!triacFiredThisHalfCycle && zcTicks >= triacDelayTicks) {
    PORTD |= (1 << PD7); // Pulse TRIAC HIGH
    gateTicksRemaining = 1;
    triacFiredThisHalfCycle = true;
  }
}

// --- HARDWARE INITIALIZATION ---
void hardwareInit() {
  cli();

  // GPIO Direction setup
  DDRD |= (1 << DDD6) | (1 << DDD7);                           // Buzzer (PD6), Triac (PD7) OUTPUT
  DDRD &= ~((1 << DDD2) | (1 << DDD3) | (1 << DDD4) | (1 << DDD5)); // Inputs: ZC, SW, DT, CLK
  PORTD |= (1 << PORTD2) | (1 << PORTD3) | (1 << PORTD4) | (1 << PORTD5); // Pull-ups

  DDRB |= (1 << DDB1) | (1 << DDB2) | (1 << DDB5);              // Fan PWM (PB1), MAX CS (PB2), SCK (PB5) OUTPUT
  DDRB &= ~(1 << DDB0) | (1 << DDB4);                           // Handle SW (PB0), MAX SO (PB4) INPUT
  PORTB |= (1 << PORTB0) | (1 << PORTB4) | (1 << PORTB2);       // Pull-ups + MAX CS High

  // Hardware PWM Timer1 (Fan Control on PB1 / Pin 9)
  TCCR1A = (1 << COM1A1) | (1 << WGM10); // Non-inverting Fast PWM 8-bit
  TCCR1B = (1 << WGM12) | (1 << CS11) | (1 << CS10); // Prescaler 64 (~976Hz)
  OCR1A = 0;

  // Hardware SPI Master Mode (MAX6675)
  SPCR = (1 << SPE) | (1 << MSTR) | (1 << SPR0); // Enable SPI, Master, F_CPU/16 (1MHz)

  // Hardware TWI/I2C (LCD 100kHz)
  TWSR = 0x00; // Prescaler = 1
  TWBR = 0x48; // SCL frequency = 100kHz @ 16MHz F_CPU

  // SysTick Timer0 Setup
  timer0_init();

  // External Interrupt INT0 (Zero-Cross Falling Edge on PD2)
  EICRA = (1 << ISC01); // Falling Edge
  EIMSK = (1 << INT0);  // Enable INT0

  // Timer2 Setup (Triac Phase Timing)
  TCCR2A = (1 << WGM21); // CTC Mode
  TCCR2B = (1 << CS21);  // Prescaler 8 (1 tick = 0.5us)
  TCNT2 = 0;
  OCR2A = 199;           // Interrupt every 100us
  TIMSK2 |= (1 << OCIE2A);

  sei();

  previousEncoderAB = ((PIND & (1 << PD5)) ? 2 : 0) | ((PIND & (1 << PD4)) ? 1 : 0);
  loadSettingsFromEEPROM();
}

void setFanPWM(uint8_t duty8bit) {
  OCR1A = duty8bit;
}

void heaterOff() {
  heaterDemand = 0;
  cli();
  triacAllowed = false;
  gateTicksRemaining = 0;
  PORTD &= ~(1 << PD7);
  sei();
}

void setHeaterDemand(int demandPercent) {
  demandPercent = (demandPercent < 0) ? 0 : ((demandPercent > 100) ? 100 : demandPercent);
  heaterDemand = demandPercent;
  cli();
  if (demandPercent <= 0) {
    triacAllowed = false;
    gateTicksRemaining = 0;
    PORTD &= ~(1 << PD7);
  } else {
    uint16_t delayUs = 9000 - ((uint32_t)(9000 - 500) * demandPercent / 100);
    uint8_t ticks = (uint8_t)(delayUs / 100);
    triacDelayTicks = (ticks < 1) ? 1 : ((ticks > 90) ? 90 : ticks);
    triacAllowed = true;
  }
  sei();
}

void queueBeep(uint8_t count) {
  if (count > beepRemaining) beepRemaining = count;
}

void updateBuzzer() {
  uint32_t now = millis();
  if (buzzerOn) {
    if (now - buzzerChangedAt >= BEEP_ON_MS) {
      PORTD &= ~(1 << PD6); // Buzzer LOW
      buzzerOn = false;
      buzzerChangedAt = now;
      if (beepRemaining > 0) beepRemaining--;
    }
  } else if (beepRemaining > 0 && now - buzzerChangedAt >= BEEP_OFF_MS) {
    PORTD |= (1 << PD6);  // Buzzer HIGH
    buzzerOn = true;
    buzzerChangedAt = now;
  }
}

bool handleIsOnHolder() {
  bool val = (PINB & (1 << PINB0)) ? 1 : 0;
  return val == HANDLE_ON_HOLDER_LEVEL;
}

// Bare-metal Hardware SPI Read for MAX6675
bool readTemperature() {
  PORTB &= ~(1 << PB2); // CS LOW
  _delay_us(2);

  SPDR = 0xFF; while (!(SPSR & (1 << SPIF))); uint8_t h = SPDR;
  SPDR = 0xFF; while (!(SPSR & (1 << SPIF))); uint8_t l = SPDR;

  PORTB |= (1 << PB2);  // CS HIGH

  uint16_t raw = (h << 8) | l;

  if (raw & 0x0004) { // Open Thermocouple Fault
    currentTempC = NAN; sensorOK = false; return false;
  }
  float t = (float)((raw >> 3) & 0x0FFF) * 0.25f;
  if (!isfinite(t) || t < 5.0f || t > 1024.0f) {
    currentTempC = NAN; sensorOK = false; return false;
  }
  currentTempC = t; sensorOK = true; return true;
}

void updateEncoder() {
  static const int8_t table[16] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };
  uint8_t ab = ((PIND & (1 << PD5)) ? 2 : 0) | ((PIND & (1 << PD4)) ? 1 : 0);
  static int8_t accum = 0;
  accum += table[(previousEncoderAB << 2) | ab];
  previousEncoderAB = ab;

  if (accum >= 4 || accum <= -4) {
    int8_t direction = (accum > 0) ? 1 : -1;
    accum = 0;

    uint32_t now = millis();
    uint32_t interval = now - lastEncoderMoveMs;
    lastEncoderMoveMs = now; lastActivity = now;

    int step = 1;
    if (interval < 50) step = 10;
    else if (interval < 100) step = 5;

    switch (uiState) {
      case UI_SET_TEMP:
        if (step > 1) {
          targetTempC = (direction > 0) ? ((targetTempC / step) + 1) * step : ((targetTempC / step) - 1) * step;
        } else {
          targetTempC += direction;
        }
        targetTempC = (targetTempC < MIN_TEMP_C) ? MIN_TEMP_C : ((targetTempC > MAX_TEMP_C) ? MAX_TEMP_C : targetTempC);
        lcdNeedsRefresh = true;
        break;
      case UI_SET_FAN:
        if (step > 1) {
          fanPercent = (direction > 0) ? ((fanPercent / step) + 1) * step : ((fanPercent / step) - 1) * step;
        } else {
          fanPercent += direction;
        }
        fanPercent = (fanPercent < MIN_FAN_PERCENT) ? MIN_FAN_PERCENT : ((fanPercent > MAX_FAN_PERCENT) ? MAX_FAN_PERCENT : fanPercent);
        lcdNeedsRefresh = true;
        break;
      case UI_CONFIRM:
        confirmYes = !confirmYes;
        lcdNeedsRefresh = true;
        break;
      default: break;
    }
  }
}

void updateButton() {
  bool reading = (PIND & (1 << PD3)) ? 1 : 0;
  uint32_t now = millis();
  if (reading != buttonLastRead) { buttonLastRead = reading; lastButtonChangeMs = now; }
  if ((now - lastButtonChangeMs) > 30 && reading != buttonStable) {
    buttonStable = reading;
    if (buttonStable == 0) buttonPressedEvent = true;
  }
  if (buttonPressedEvent) {
    buttonPressedEvent = false;
    onEncoderButton();
  }
}