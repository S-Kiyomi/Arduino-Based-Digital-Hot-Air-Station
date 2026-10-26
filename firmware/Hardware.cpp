#include "Hardware.h"
#include "UI.h" 
#include <avr/interrupt.h>

// --- LOCAL HARDWARE VARIABLES ---
static uint8_t beepRemaining = 0;
static bool buzzerOn = false;
static unsigned long buzzerChangedAt = 0;
const unsigned long BEEP_ON_MS = 100;
const unsigned long BEEP_OFF_MS = 140;

volatile uint8_t triacDelayTicks = 90;
volatile uint8_t zcTicks = 0;
volatile uint8_t gateTicksRemaining = 0;
volatile bool zcSeen = false;
volatile bool triacAllowed = false;
volatile bool triacFiredThisHalfCycle = false;
volatile unsigned long lastZeroCrossMicros = 0;

static uint8_t previousEncoderAB = 0;
static unsigned long lastEncoderMoveMs = 0;
static unsigned long lastButtonChangeMs = 0;
static bool buttonStable = HIGH;
static bool buttonLastRead = HIGH;
static bool buttonPressedEvent = false;

// --- INTERRUPT SERVICE ROUTINES (ISR) ---
void zeroCrossISR() {
  PORTD &= ~_BV(PD7); 
  zcTicks = 0;
  gateTicksRemaining = 0;
  triacFiredThisHalfCycle = false;
  zcSeen = true;
  lastZeroCrossMicros = micros();
}

ISR(TIMER2_COMPA_vect) {
  if (!triacAllowed || !zcSeen) {
    PORTD &= ~_BV(PD7);
    gateTicksRemaining = 0;
    return;
  }
  if (gateTicksRemaining > 0) {
    PORTD &= ~_BV(PD7);
    gateTicksRemaining--;
    return;
  }
  if (zcTicks < 100) zcTicks++;
  if (!triacFiredThisHalfCycle && zcTicks >= triacDelayTicks) {
    PORTD |= _BV(PD7);
    gateTicksRemaining = 1; // Gate pulse
    triacFiredThisHalfCycle = true;
  }
}

// --- MAIN HARDWARE FUNCTIONS ---
void hardwareInit() {
  pinMode(BUZZER_PIN, OUTPUT); digitalWrite(BUZZER_PIN, LOW);
  pinMode(TRIAC_PIN, OUTPUT); digitalWrite(TRIAC_PIN, LOW);
  pinMode(FAN_PIN, OUTPUT); analogWrite(FAN_PIN, 0);

  pinMode(MAX_CS, OUTPUT); digitalWrite(MAX_CS, HIGH);
  pinMode(MAX_SCK, OUTPUT); digitalWrite(MAX_SCK, LOW);
  pinMode(MAX_SO, INPUT);

  pinMode(ZERO_CROSS_PIN, INPUT_PULLUP);
  pinMode(HANDLE_SWITCH_PIN, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(ENC_CLK, INPUT_PULLUP);

  previousEncoderAB = (digitalRead(ENC_CLK) << 1) | digitalRead(ENC_DT);
  
  // Setup Interrupt
  attachInterrupt(digitalPinToInterrupt(ZERO_CROSS_PIN), zeroCrossISR, FALLING);
  cli();
  TCCR2A = _BV(WGM21);
  TCCR2B = _BV(CS21);
  TCNT2 = 0;
  OCR2A = 199;
  TIMSK2 |= _BV(OCIE2A);
  sei();
}

void heaterOff() {
  heaterDemand = 0;
  noInterrupts();
  triacAllowed = false;
  gateTicksRemaining = 0;
  PORTD &= ~_BV(PD7);
  interrupts();
}

void setHeaterDemand(int demandPercent) {
  demandPercent = constrain(demandPercent, 0, 100);
  heaterDemand = demandPercent;
  noInterrupts();
  if (demandPercent <= 0) {
    triacAllowed = false;
    gateTicksRemaining = 0;
    PORTD &= ~_BV(PD7);
  } else {
    uint16_t delayUs = 9000 - ((uint32_t)(9000 - 500) * demandPercent / 100);
    uint8_t ticks = (uint8_t)(delayUs / 100);
    triacDelayTicks = constrain(ticks, 1, 90);
    triacAllowed = true;
  }
  interrupts();
}

void queueBeep(uint8_t count) {
  if (count > beepRemaining) beepRemaining = count;
}

void updateBuzzer() {
  unsigned long now = millis();
  if (buzzerOn) {
    if (now - buzzerChangedAt >= BEEP_ON_MS) {
      digitalWrite(BUZZER_PIN, LOW);
      buzzerOn = false;
      buzzerChangedAt = now;
      if (beepRemaining > 0) beepRemaining--;
    }
  } else if (beepRemaining > 0 && now - buzzerChangedAt >= BEEP_OFF_MS) {
    digitalWrite(BUZZER_PIN, HIGH);
    buzzerOn = true;
    buzzerChangedAt = now;
  }
}

bool handleIsOnHolder() {
  return digitalRead(HANDLE_SWITCH_PIN) == HANDLE_ON_HOLDER_LEVEL;
}

bool readTemperature() {
  digitalWrite(MAX_CS, LOW); delayMicroseconds(2);
  uint16_t raw = 0;
  for (uint8_t i = 0; i < 16; i++) {
    digitalWrite(MAX_SCK, HIGH); delayMicroseconds(1);
    raw <<= 1;
    if (digitalRead(MAX_SO)) raw |= 1;
    digitalWrite(MAX_SCK, LOW); delayMicroseconds(1);
  }
  digitalWrite(MAX_CS, HIGH);

  if (raw & 0x0004) { // Sensor disconnected
    currentTempC = NAN; sensorOK = false; return false;
  }
  float t = (float)((raw >> 3) & 0x0FFF) * 0.25f;
  if (!isfinite(t) || t < 0.0f || t > 1024.0f) {
    currentTempC = NAN; sensorOK = false; return false;
  }
  currentTempC = t; sensorOK = true; return true;
}

void updateEncoder() {
  static const int8_t table[16] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };
  uint8_t ab = (digitalRead(ENC_CLK) << 1) | digitalRead(ENC_DT);
  static int8_t accum = 0;
  accum += table[(previousEncoderAB << 2) | ab];
  previousEncoderAB = ab;

  if (accum >= 4 || accum <= -4) {
    int8_t direction = (accum > 0) ? 1 : -1;
    accum = 0;

    unsigned long now = millis();
    unsigned long interval = now - lastEncoderMoveMs;
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
        targetTempC = constrain(targetTempC, MIN_TEMP_C, MAX_TEMP_C);
        lcdNeedsRefresh = true;
        break;
      case UI_SET_FAN:
        if (step > 1) {
          fanPercent = (direction > 0) ? ((fanPercent / step) + 1) * step : ((fanPercent / step) - 1) * step;
        } else {
          fanPercent += direction;
        }
        fanPercent = constrain(fanPercent, MIN_FAN_PERCENT, MAX_FAN_PERCENT);
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
  bool reading = digitalRead(ENC_SW);
  unsigned long now = millis();
  if (reading != buttonLastRead) { buttonLastRead = reading; lastButtonChangeMs = now; }
  if ((now - lastButtonChangeMs) > 30 && reading != buttonStable) {
    buttonStable = reading;
    if (buttonStable == LOW) buttonPressedEvent = true;
  }
  if (buttonPressedEvent) {
    buttonPressedEvent = false;
    onEncoderButton(); // Call function in UI.cpp
  }
}