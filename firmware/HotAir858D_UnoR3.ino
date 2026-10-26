/*
  Arduino Uno R3 - 858D hot-air station controller
  Libraries:
    - LiquidCrystal_I2C
    - max6675

  IMPORTANT:
  - Bench-test with heater AC disconnected first.
  - D7 must drive the MOC3021/MOC3023 LED through a suitable resistor/driver.
  - D2 zero-cross signal must be logic-level isolated from mains.
  - D8 handle/holder switch polarity is configurable below.
  - Verify the handle switch really indicates the holder position before enabling heater.
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <math.h>
#include <avr/interrupt.h>

// ---------------- PIN MAP ----------------
const uint8_t BUZZER_PIN = 6;
const uint8_t TRIAC_PIN  = 7;
const uint8_t HANDLE_SWITCH_PIN = 8; // additional input; see HANDLE_ON_HOLDER_LEVEL
const uint8_t FAN_PIN    = 9;
const uint8_t ENC_SW     = 3;
const uint8_t ENC_DT     = 4;
const uint8_t ENC_CLK    = 5;
const uint8_t MAX_CS     = 10;
const uint8_t MAX_SO     = 11;
const uint8_t MAX_SCK    = 13;
const uint8_t ZERO_CROSS_PIN = 2;

// ---------------- USER CONFIG ----------------
const uint8_t LCD_ADDRESS = 0x27;       // common addresses: 0x27 or 0x3F
const bool HANDLE_ON_HOLDER_LEVEL = LOW; // INPUT_PULLUP: switch closed to GND = LOW
const bool USE_HANDLE_SWITCH_FOR_SLEEP = true;
const int MIN_TEMP_C = 100;
const int MAX_TEMP_C = 500;
const int TEMP_STEP_C = 1;
const int MIN_FAN_PERCENT = 20;         // adjust to your blower/driver's safe minimum
const int MAX_FAN_PERCENT = 100;
const float READY_BAND_C = 5.0;
const unsigned long READY_STABLE_MS = 2000;
const unsigned long BACKLIGHT_TIMEOUT_MS = 60000;
const unsigned long TEMP_READ_INTERVAL_MS = 250;
const unsigned long UI_REFRESH_MS = 200;
const unsigned long SENSOR_STARTUP_TIMEOUT_MS = 1500;

// Heater control: conservative proportional demand, then phase-angle delay.
// Tune only with suitable isolated instrumentation and heater disconnected initially.
const float HEATER_KP = 2.0f;
const uint16_t TRIAC_MIN_DELAY_US = 500;   // early firing at high demand
const uint16_t TRIAC_MAX_DELAY_US = 9000;  // late firing at low demand; suitable for 50 Hz
const uint8_t TRIAC_TICK_US = 100;         // Timer2 compare tick
const uint8_t TRIAC_GATE_PULSE_TICKS = 1;  // 100 us gate pulse

LiquidCrystal_I2C lcd(LCD_ADDRESS, 16, 2);

// ---------------- UI STATES ----------------
enum UiState : uint8_t {
  UI_BOOT,
  UI_FAULT,
  UI_SET_TEMP,
  UI_SET_FAN,
  UI_CONFIRM,
  UI_RUN
};

UiState uiState = UI_BOOT;
unsigned long bootStarted = 0;
unsigned long lastTempRead = 0;
unsigned long lastUiRefresh = 0;
unsigned long lastActivity = 0;
unsigned long readySince = 0;
unsigned long lastReadyBeep = 0;

float currentTempC = NAN;
int targetTempC = 350;
int savedTargetTempC = 350;
int fanPercent = 50;
int savedFanPercent = 50;
int heaterDemand = 0;
bool sensorOK = false;
bool readyBeeped = false;
bool sleeping = false;
bool backlightOn = true;
bool confirmYes = true;
bool lcdNeedsRefresh = true;

// Encoder state
int8_t encoderDelta = 0;
uint8_t previousEncoderAB = 0;
unsigned long lastEncoderMoveMs = 0;
unsigned long lastButtonChangeMs = 0;
bool buttonStable = HIGH;
bool buttonLastRead = HIGH;
bool buttonPressedEvent = false;

// Beeper queue (active buzzer assumed; HIGH = sound)
uint8_t beepRemaining = 0;
bool buzzerOn = false;
unsigned long buzzerChangedAt = 0;
const unsigned long BEEP_ON_MS = 100;
const unsigned long BEEP_OFF_MS = 140;

// Heater/zero-cross shared state
volatile uint8_t triacDelayTicks = 90;
volatile uint8_t zcTicks = 0;
volatile uint8_t gateTicksRemaining = 0;
volatile bool zcSeen = false;
volatile bool triacAllowed = false;
volatile bool triacFiredThisHalfCycle = false;
volatile unsigned long lastZeroCrossMicros = 0;

// ---------------- SMALL HELPERS ----------------
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
    uint16_t delayUs = TRIAC_MAX_DELAY_US -
      ((uint32_t)(TRIAC_MAX_DELAY_US - TRIAC_MIN_DELAY_US) * demandPercent / 100);
    uint8_t ticks = (uint8_t)(delayUs / TRIAC_TICK_US);
    if (ticks < 1) ticks = 1;
    if (ticks > 90) ticks = 90;
    triacDelayTicks = ticks;
    triacAllowed = true;
  }
  interrupts();
}

void zeroCrossISR() {
  PORTD &= ~_BV(PD7); // D7 LOW at each crossing
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
    gateTicksRemaining = TRIAC_GATE_PULSE_TICKS;
    triacFiredThisHalfCycle = true;
  }
}

bool handleIsOnHolder() {
  if (!USE_HANDLE_SWITCH_FOR_SLEEP) return false;
  return digitalRead(HANDLE_SWITCH_PIN) == HANDLE_ON_HOLDER_LEVEL;
}

bool readTemperature() {
  // Read MAX6675 directly without a blocking 220 ms library delay.
  digitalWrite(MAX_CS, LOW);
  delayMicroseconds(2);

  uint16_t raw = 0;
  for (uint8_t i = 0; i < 16; i++) {
    digitalWrite(MAX_SCK, HIGH);
    delayMicroseconds(1);
    raw <<= 1;
    if (digitalRead(MAX_SO)) raw |= 1;
    digitalWrite(MAX_SCK, LOW);
    delayMicroseconds(1);
  }
  digitalWrite(MAX_CS, HIGH);

  // D2 (bit 2) = thermocouple open circuit.
  if (raw & 0x0004) {
    currentTempC = NAN;
    sensorOK = false;
    return false;
  }

  float t = (float)((raw >> 3) & 0x0FFF) * 0.25f;
  if (!isfinite(t) || t < 0.0f || t > 1024.0f) {
    currentTempC = NAN;
    sensorOK = false;
    return false;
  }

  currentTempC = t;
  sensorOK = true;
  return true;
}

void lcdPrintPadded(uint8_t col, uint8_t row, const char *text) {
  lcd.setCursor(col, row);
  uint8_t i = 0;
  while (text[i] && i < 16) {
    lcd.print(text[i]);
    i++;
  }
  while (i < 16) {
    lcd.print(' ');
    i++;
  }
}

void showBootScreen() {
  lcdPrintPadded(0, 0, "SYSTEM LOADING");
  lcdPrintPadded(0, 1, "Please wait...");
}

void showFaultScreen() {
  lcdPrintPadded(0, 0, "MALFUNCTION!!");
  lcdPrintPadded(0, 1, "TERCABUT/RUSAK");
}

void showRunScreen() {
  char line1[17];
  char line2[17];

  if (sleeping) {
    snprintf(line1, sizeof(line1), "T:%3dC SP:%3d", isnan(currentTempC) ? 0 : (int)currentTempC, targetTempC);
    snprintf(line2, sizeof(line2), "F:%3d%% S:SLEEP", fanPercent);
  } else {
    snprintf(line1, sizeof(line1), "T:%3dC SP:%3d", isnan(currentTempC) ? 0 : (int)currentTempC, targetTempC);
    const char *status = "HEATING";
    if (heaterDemand == 0 && currentTempC >= targetTempC - READY_BAND_C) status = "READY";
    if (heaterDemand == 0 && currentTempC < targetTempC - READY_BAND_C) status = "IDLE";
    snprintf(line2, sizeof(line2), "F:%3d%% %s", fanPercent, status);
  }

  lcdPrintPadded(0, 0, line1);
  lcdPrintPadded(0, 1, line2);
}

void refreshLCD() {
  if (!backlightOn) return;

  switch (uiState) {
    case UI_BOOT:
      showBootScreen();
      break;
    case UI_FAULT:
      showFaultScreen();
      break;
    case UI_SET_TEMP: {
      char a[17], b[17];
      snprintf(a, sizeof(a), "SET TEMPERATURE");
      snprintf(b, sizeof(b), "Target: %d C", targetTempC);
      lcdPrintPadded(0, 0, a);
      lcdPrintPadded(0, 1, b);
      break;
    }
    case UI_SET_FAN: {
      char a[17], b[17];
      snprintf(a, sizeof(a), "SET AIR FLOW");
      snprintf(b, sizeof(b), "Fan: %d%%", fanPercent);
      lcdPrintPadded(0, 0, a);
      lcdPrintPadded(0, 1, b);
      break;
    }
    case UI_CONFIRM:
      lcdPrintPadded(0, 0, "CONFIRM SETTINGS");
      lcdPrintPadded(0, 1, confirmYes ? ">YES       NO" : " YES      >NO");
      break;
    case UI_RUN:
      showRunScreen();
      break;
  }
}

void enterFault() {
  heaterOff();
  sensorOK = false;
  uiState = UI_FAULT;
  lcd.backlight();
  backlightOn = true;
  queueBeep(3);
  lcdNeedsRefresh = true;
}

void enterRunMode() {
  uiState = UI_RUN;
  sleeping = handleIsOnHolder();
  readySince = 0;
  readyBeeped = false;
  lastActivity = millis();
  lcdNeedsRefresh = true;
}

void onEncoderButton() {
  lastActivity = millis();

  if (!backlightOn) {
    lcd.backlight();
    backlightOn = true;
    lcdNeedsRefresh = true;
    return; // wake only; don't also select a menu item
  }

  switch (uiState) {
    case UI_SET_TEMP:
      uiState = UI_SET_FAN;
      break;

    case UI_SET_FAN:
      uiState = UI_CONFIRM;
      confirmYes = true;
      break;

    case UI_CONFIRM:
      if (confirmYes) {
        savedTargetTempC = targetTempC;
        savedFanPercent = fanPercent;
        enterRunMode();
      } else {
        targetTempC = savedTargetTempC;
        fanPercent = savedFanPercent;
        enterRunMode();
      }
      break;

    case UI_RUN:
      savedTargetTempC = targetTempC;
      savedFanPercent = fanPercent;
      uiState = UI_SET_TEMP;
      break;

    case UI_BOOT:
    case UI_FAULT:
      break;
  }
  lcdNeedsRefresh = true;
}

void updateEncoder() {
  // Quadrature lookup table; handles one detent as a small signed movement.
  static const int8_t table[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
  };

  uint8_t a = digitalRead(ENC_CLK);
  uint8_t b = digitalRead(ENC_DT);
  uint8_t ab = (a << 1) | b;
  static int8_t accum = 0;
  uint8_t idx = (previousEncoderAB << 2) | ab;
  accum += table[idx];
  previousEncoderAB = ab;

  if (accum >= 4 || accum <= -4) {
    int8_t direction = (accum > 0) ? 1 : -1;
    accum = 0;

    unsigned long now = millis();
    unsigned long interval = now - lastEncoderMoveMs;
    lastEncoderMoveMs = now;
    lastActivity = now;

    int step = 1;
    if (interval < 45) step = 15;
    else if (interval < 90) step = 5;
    else if (interval < 160) step = 2;

    switch (uiState) {
      case UI_SET_TEMP:
        targetTempC = constrain(targetTempC + direction * step, MIN_TEMP_C, MAX_TEMP_C);
        lcdNeedsRefresh = true;
        break;
      case UI_SET_FAN:
        fanPercent = constrain(fanPercent + direction * step, MIN_FAN_PERCENT, MAX_FAN_PERCENT);
        lcdNeedsRefresh = true;
        break;
      case UI_CONFIRM:
        confirmYes = !confirmYes;
        lcdNeedsRefresh = true;
        break;
      default:
        break;
    }
  }
}

void updateButton() {
  bool reading = digitalRead(ENC_SW);
  unsigned long now = millis();

  if (reading != buttonLastRead) {
    buttonLastRead = reading;
    lastButtonChangeMs = now;
  }

  if ((now - lastButtonChangeMs) > 30 && reading != buttonStable) {
    buttonStable = reading;
    if (buttonStable == LOW) buttonPressedEvent = true;
  }

  if (buttonPressedEvent) {
    buttonPressedEvent = false;
    onEncoderButton();
  }
}

void updateHeaterControl() {
  // If zero-cross pulses disappear, fail safe: disable heater firing.
  bool zeroCrossHealthy = (lastZeroCrossMicros != 0UL) &&
                          ((micros() - lastZeroCrossMicros) < 30000UL);
  if (uiState != UI_RUN || !sensorOK || sleeping || !zeroCrossHealthy) {
    heaterOff();
    return;
  }

  float error = (float)targetTempC - currentTempC;
  if (error <= 0.5f) {
    setHeaterDemand(0);
    return;
  }

  int demand = (int)(error * HEATER_KP);
  demand = constrain(demand, 0, 100);
  setHeaterDemand(demand);
}

void updateReadyState() {
  if (uiState != UI_RUN || sleeping || !sensorOK) {
    readySince = 0;
    return;
  }

  bool inBand = fabs(currentTempC - (float)targetTempC) <= READY_BAND_C;
  if (inBand) {
    if (readySince == 0) readySince = millis();
    if (!readyBeeped && millis() - readySince >= READY_STABLE_MS) {
      readyBeeped = true;
      queueBeep(1);
    }
  } else {
    readySince = 0;
    readyBeeped = false;
  }
}

void updateSleepState() {
  if (uiState != UI_RUN || !USE_HANDLE_SWITCH_FOR_SLEEP) return;

  bool nowSleeping = handleIsOnHolder();
  if (nowSleeping && !sleeping) {
    sleeping = true;
    heaterOff();
    queueBeep(1);
    lcdNeedsRefresh = true;
  } else if (!nowSleeping && sleeping) {
    sleeping = false;
    readySince = 0;
    readyBeeped = false;
    lcdNeedsRefresh = true;
  }

  // In sleep, use the blower for cooldown until the nozzle is below 100 C.
  if (sleeping) {
    heaterOff();
    if (sensorOK && currentTempC > 100.0f) {
      analogWrite(FAN_PIN, 255);
    } else {
      analogWrite(FAN_PIN, 0);
    }
  } else {
    analogWrite(FAN_PIN, map(fanPercent, 0, 100, 0, 255));
  }
}

void updateBacklight() {
  if (uiState == UI_FAULT) return; // keep fault visible
  if (backlightOn && millis() - lastActivity >= BACKLIGHT_TIMEOUT_MS) {
    lcd.noBacklight();
    backlightOn = false;
  }
}

void setupTimer2() {
  // Timer2 CTC, prescaler 8: 16 MHz / 8 = 2 MHz.
  // OCR2A=199 gives 100 us per interrupt.
  cli();
  TCCR2A = _BV(WGM21);
  TCCR2B = _BV(CS21);
  TCNT2 = 0;
  OCR2A = 199;
  TIMSK2 |= _BV(OCIE2A);
  sei();
}

void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  pinMode(TRIAC_PIN, OUTPUT);
  digitalWrite(TRIAC_PIN, LOW);

  pinMode(FAN_PIN, OUTPUT);
  analogWrite(FAN_PIN, 0);

  pinMode(MAX_CS, OUTPUT);
  digitalWrite(MAX_CS, HIGH);
  pinMode(MAX_SCK, OUTPUT);
  digitalWrite(MAX_SCK, LOW);
  pinMode(MAX_SO, INPUT);

  pinMode(ZERO_CROSS_PIN, INPUT_PULLUP);
  pinMode(HANDLE_SWITCH_PIN, INPUT_PULLUP);

  pinMode(ENC_SW, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(ENC_CLK, INPUT_PULLUP);

  lcd.init();
  lcd.noBacklight();
  lcd.backlight();
  backlightOn = true;
  showBootScreen();

  previousEncoderAB = (digitalRead(ENC_CLK) << 1) | digitalRead(ENC_DT);
  lastActivity = millis();
  bootStarted = millis();

  heaterOff();
  attachInterrupt(digitalPinToInterrupt(ZERO_CROSS_PIN), zeroCrossISR, FALLING);
  setupTimer2();
}

void loop() {
  unsigned long now = millis();

  updateEncoder();
  updateButton();
  updateBuzzer();

  if (uiState == UI_BOOT) {
    if (now - bootStarted >= 3000) {
      if (readTemperature()) {
        queueBeep(1);
        enterRunMode();
        lcdNeedsRefresh = true;
      } else if (now - bootStarted >= 3000 + SENSOR_STARTUP_TIMEOUT_MS) {
        enterFault();
      }
    }
  }

  if (uiState != UI_BOOT && uiState != UI_FAULT &&
      now - lastTempRead >= TEMP_READ_INTERVAL_MS) {
    lastTempRead = now;
    if (!readTemperature()) {
      enterFault();
    }
  }

  if (uiState == UI_FAULT) {
    heaterOff();
    analogWrite(FAN_PIN, 0);
  } else {
    updateSleepState();
    updateHeaterControl();
    updateReadyState();
  }

  if (now - lastUiRefresh >= UI_REFRESH_MS) {
    lastUiRefresh = now;
    if (lcdNeedsRefresh) {
      refreshLCD();
      lcdNeedsRefresh = false;
    } else if (uiState == UI_RUN && backlightOn) {
      refreshLCD();
    }
  }

  updateBacklight();
}
