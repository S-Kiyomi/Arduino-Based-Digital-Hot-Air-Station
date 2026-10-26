/*
  Arduino Uno R3 - 858D Hot-Air Station Controller (TILT SENSOR VERSION)
  Modular Firmware Version 2.0
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <math.h>
#include <avr/interrupt.h>

// ==========================================
// MODUL 1: PIN MAPPING & CONFIGURATION
// ==========================================
const uint8_t BUZZER_PIN        = 6;
const uint8_t TRIAC_PIN         = 7;
const uint8_t HANDLE_SWITCH_PIN = 8;  // Tilt sensor pin
const uint8_t FAN_PIN           = 9;
const uint8_t ENC_SW            = 3;
const uint8_t ENC_DT            = 4;
const uint8_t ENC_CLK           = 5;
const uint8_t MAX_CS            = 10;
const uint8_t MAX_SO            = 11;
const uint8_t MAX_SCK           = 13;
const uint8_t ZERO_CROSS_PIN    = 2;

const uint8_t LCD_ADDRESS = 0x27; 
// [PENTING] HIGH = Gagang ke atas (Sleep/Open), LOW = Gagang ke bawah (Kerja/Closed)
const bool HANDLE_ON_HOLDER_LEVEL = HIGH; 

const int MIN_TEMP_C = 100;
const int MAX_TEMP_C = 500;
const int MIN_FAN_PERCENT = 20;         
const int MAX_FAN_PERCENT = 100;
const float READY_BAND_C = 5.0;
const unsigned long READY_STABLE_MS = 2000;
const unsigned long BACKLIGHT_TIMEOUT_MS = 60000; // 60 Detik mati otomatis

// Heater PID / Triac Control
const float HEATER_KP = 2.0f;
const uint16_t TRIAC_MIN_DELAY_US = 500;   
const uint16_t TRIAC_MAX_DELAY_US = 9000;  
const uint8_t TRIAC_TICK_US = 100;         
const uint8_t TRIAC_GATE_PULSE_TICKS = 1;  

// ==========================================
// MODUL 2: GLOBAL VARIABLES & STATES
// ==========================================
LiquidCrystal_I2C lcd(LCD_ADDRESS, 16, 2);

enum UiState : uint8_t { UI_BOOT, UI_FAULT, UI_SET_TEMP, UI_SET_FAN, UI_CONFIRM, UI_RUN };
UiState uiState = UI_BOOT;

// System Timers
unsigned long bootStarted = 0;
unsigned long lastTempRead = 0;
unsigned long lastUiRefresh = 0;
unsigned long lastActivity = 0;
unsigned long readySince = 0;

// Variables
float currentTempC = NAN;
int targetTempC = 350;
int savedTargetTempC = 350;
int fanPercent = 50;
int savedFanPercent = 50;
int heaterDemand = 0;

// Flags
bool sensorOK = false;
bool readyBeeped = false;
bool sleeping = false;
bool backlightOn = true;
bool confirmYes = true;
bool lcdNeedsRefresh = true;

// Encoder & Button
uint8_t previousEncoderAB = 0;
unsigned long lastEncoderMoveMs = 0;
unsigned long lastButtonChangeMs = 0;
bool buttonStable = HIGH;
bool buttonLastRead = HIGH;
bool buttonPressedEvent = false;

// Buzzer
uint8_t beepRemaining = 0;
bool buzzerOn = false;
unsigned long buzzerChangedAt = 0;
const unsigned long BEEP_ON_MS = 100;
const unsigned long BEEP_OFF_MS = 140;

// Triac / Zero Cross (Volatile for ISR)
volatile uint8_t triacDelayTicks = 90;
volatile uint8_t zcTicks = 0;
volatile uint8_t gateTicksRemaining = 0;
volatile bool zcSeen = false;
volatile bool triacAllowed = false;
volatile bool triacFiredThisHalfCycle = false;
volatile unsigned long lastZeroCrossMicros = 0;

// ==========================================
// MODUL 3: HARDWARE CONTROL (Heater & Triac)
// ==========================================
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
    uint16_t delayUs = TRIAC_MAX_DELAY_US - ((uint32_t)(TRIAC_MAX_DELAY_US - TRIAC_MIN_DELAY_US) * demandPercent / 100);
    uint8_t ticks = (uint8_t)(delayUs / TRIAC_TICK_US);
    triacDelayTicks = constrain(ticks, 1, 90);
    triacAllowed = true;
  }
  interrupts();
}

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
    gateTicksRemaining = TRIAC_GATE_PULSE_TICKS;
    triacFiredThisHalfCycle = true;
  }
}

// ==========================================
// MODUL 4: SENSORS & ALARMS
// ==========================================
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

  if (raw & 0x0004) { // Thermocouple tercabut
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

// ==========================================
// MODUL 5: UI & DISPLAY
// ==========================================
void lcdPrintPadded(uint8_t col, uint8_t row, const char *text) {
  lcd.setCursor(col, row);
  uint8_t i = 0;
  while (text[i] && i < 16) { lcd.print(text[i]); i++; }
  while (i < 16) { lcd.print(' '); i++; }
}

void enterFault() {
  heaterOff();
  sensorOK = false;
  uiState = UI_FAULT;
  if(!backlightOn) { lcd.backlight(); backlightOn = true; }
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

void refreshLCD() {
  if (!backlightOn) return;
  char line1[17], line2[17];

  switch (uiState) {
    case UI_BOOT:
      lcdPrintPadded(0, 0, "system loading");
      lcdPrintPadded(0, 1, "");
      break;

    case UI_FAULT:
      lcdPrintPadded(0, 0, "MALFUNCTION!!");
      lcdPrintPadded(0, 1, "TERCABUT/RUSAK");
      break;

    case UI_SET_TEMP: 
      lcdPrintPadded(0, 0, "SET TEMPERATURE");
      snprintf(line2, sizeof(line2), "Target: %d C", targetTempC);
      lcdPrintPadded(0, 1, line2);
      break;

    case UI_SET_FAN: 
      lcdPrintPadded(0, 0, "SET AIR FLOW");
      snprintf(line2, sizeof(line2), "Fan: %d%%", fanPercent);
      lcdPrintPadded(0, 1, line2);
      break;

    case UI_CONFIRM:
      lcdPrintPadded(0, 0, "APAKAH YAKIN?"); // Disingkat agar muat 16 char
      lcdPrintPadded(0, 1, confirmYes ? ">YES       NO" : " YES      >NO");
      break;

    case UI_RUN:
      snprintf(line1, sizeof(line1), "T:%3dC SP:%3d", isnan(currentTempC) ? 0 : (int)currentTempC, targetTempC);
      
      const char *statusTxt;
      if (sleeping) {
        statusTxt = "SLEEP";
      } else {
        statusTxt = "HEAT "; // Default
        if (heaterDemand == 0 && currentTempC >= targetTempC - READY_BAND_C) statusTxt = "READY";
        if (heaterDemand == 0 && currentTempC < targetTempC - READY_BAND_C) statusTxt = "IDLE ";
      }
      snprintf(line2, sizeof(line2), "F:%3d%% S:%s", fanPercent, statusTxt);
      
      lcdPrintPadded(0, 0, line1);
      lcdPrintPadded(0, 1, line2);
      break;
  }
}

// ==========================================
// MODUL 6: INPUT HANDLING (Knob & Encoder)
// ==========================================
void onEncoderButton() {
  lastActivity = millis();
  
  if (!backlightOn) {
    lcd.backlight();
    backlightOn = true;
    lcdNeedsRefresh = true;
    return; // Bangun dari sleep layar, menu tidak berganti
  }

  switch (uiState) {
    case UI_RUN:      
      savedTargetTempC = targetTempC;
      savedFanPercent = fanPercent;
      uiState = UI_SET_TEMP; 
      break;
    case UI_SET_TEMP: uiState = UI_SET_FAN; break;
    case UI_SET_FAN:  uiState = UI_CONFIRM; confirmYes = true; break;
    case UI_CONFIRM:
      if (confirmYes) {
        savedTargetTempC = targetTempC;
        savedFanPercent = fanPercent;
      } else {
        targetTempC = savedTargetTempC; // Batal, kembalikan ke sebelumnya
        fanPercent = savedFanPercent;
      }
      enterRunMode();
      break;
    case UI_BOOT:
    case UI_FAULT: break;
  }
  lcdNeedsRefresh = true;
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
    onEncoderButton();
  }
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
    lastEncoderMoveMs = now;
    lastActivity = now;

    int step = 1;
    if (interval < 50) step = 10;       // Putaran Cepat (melangkah per puluhan)
    else if (interval < 100) step = 5;  // Putaran Sedang

    switch (uiState) {
      case UI_SET_TEMP:
        if (step > 1) { 
          // Snap agar lompatan angkanya bulat (misal 32 -> 40 -> 50)
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

// ==========================================
// MODUL 7: MAIN SYSTEM LOGIC
// ==========================================
void updateSystemLogic() {
  // 1. Backlight Timeout
  if (uiState != UI_FAULT && backlightOn && (millis() - lastActivity >= BACKLIGHT_TIMEOUT_MS)) {
    lcd.noBacklight();
    backlightOn = false;
  }

  if (uiState != UI_RUN) return;

  // 2. Sleep State (Tilt Sensor)
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

  // 3. Pendinginan (Kipas harus menyala meski Sleep jika suhu > 100C)
  if (sleeping) {
    heaterOff();
    if (sensorOK && currentTempC > 100.0f) analogWrite(FAN_PIN, 255); 
    else analogWrite(FAN_PIN, 0); // Suhu aman, matikan kipas
  } else {
    analogWrite(FAN_PIN, map(fanPercent, 0, 100, 0, 255));
  }

  // 4. Heater PID Control
  bool zcHealthy = (lastZeroCrossMicros != 0UL) && ((micros() - lastZeroCrossMicros) < 30000UL);
  if (!sensorOK || sleeping || !zcHealthy) {
    heaterOff();
  } else {
    float error = (float)targetTempC - currentTempC;
    if (error <= 0.5f) {
      setHeaterDemand(0);
    } else {
      setHeaterDemand(constrain((int)(error * HEATER_KP), 0, 100));
    }
  }

  // 5. Ready Status & Beep
  if (!sleeping && sensorOK) {
    bool inBand = fabs(currentTempC - (float)targetTempC) <= READY_BAND_C;
    if (inBand) {
      if (readySince == 0) readySince = millis();
      if (!readyBeeped && (millis() - readySince >= READY_STABLE_MS)) {
        readyBeeped = true;
        queueBeep(1);
        lcdNeedsRefresh = true; // Update LCD untuk mengganti label menjadi READY
      }
    } else {
      readySince = 0;
      if (readyBeeped) {
        readyBeeped = false; 
        lcdNeedsRefresh = true; // Jika suhu drop, kembalikan teks menjadi HEAT
      }
    }
  }
}

void setupTimer2() {
  cli();
  TCCR2A = _BV(WGM21);
  TCCR2B = _BV(CS21);
  TCNT2 = 0;
  OCR2A = 199;
  TIMSK2 |= _BV(OCIE2A);
  sei();
}

void setup() {
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

  lcd.init();
  lcd.backlight();
  backlightOn = true;
  refreshLCD();

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

  // Fase 1: Booting system 3 Detik
  if (uiState == UI_BOOT) {
    if (now - bootStarted >= 3000) {
      if (readTemperature()) {
        queueBeep(1);
        enterRunMode();
      } else {
        enterFault(); // Langsung trigger MALFUNCTION + 3x Beep
      }
    }
  }

  // Rutin baca suhu interval
  if (uiState != UI_BOOT && uiState != UI_FAULT && (now - lastTempRead >= 250)) {
    lastTempRead = now;
    if (!readTemperature()) enterFault();
  }

  // Handle Logika Heater / Kipas
  if (uiState == UI_FAULT) {
    heaterOff();
    analogWrite(FAN_PIN, 0);
  } else {
    updateSystemLogic();
  }

  // Handle Refresh UI
  if (now - lastUiRefresh >= 200) {
    lastUiRefresh = now;
    if (lcdNeedsRefresh) {
      refreshLCD();
      lcdNeedsRefresh = false;
    } else if (uiState == UI_RUN && backlightOn) {
      refreshLCD(); // Auto-refresh suhu jika di RUN Screen
    }
  }
}