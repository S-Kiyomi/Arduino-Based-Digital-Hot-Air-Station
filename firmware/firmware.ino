#include "Config.h"
#include "Hardware.h"
#include "UI.h"

// --- GLOBAL VARIABLES INITIALIZATION ---
UiState uiState = UI_BOOT;
float currentTempC = NAN;
int targetTempC = 350;
int savedTargetTempC = 350;
int fanPercent = 50;
int savedFanPercent = 50;
int heaterDemand = 0;

bool sensorOK = false;
bool sleeping = false;
bool backlightOn = true;
bool confirmYes = true;
bool lcdNeedsRefresh = true;
unsigned long lastActivity = 0;

// Main Loop Timers
unsigned long bootStarted = 0;
unsigned long lastTempRead = 0;
unsigned long lastUiRefresh = 0;
unsigned long readySince = 0;
bool readyBeeped = false;

// PID Controller Variables
static float pidIntegral = 0.0f;
static float pidLastError = 0.0f;

void resetPID() {
  pidIntegral = 0.0f;
  pidLastError = 0.0f;
}

void setup() {
  hardwareInit();
  uiInit();
  bootStarted = millis();
  lastActivity = millis();
}

void updateSystemLogic() {
  // 1. Backlight Timeout (60s)
  if (uiState != UI_FAULT && backlightOn && (millis() - lastActivity >= BACKLIGHT_TIMEOUT_MS)) {
    backlightOn = false; 
    lcdNeedsRefresh = true;
  }

  if (uiState != UI_RUN) return;

  // 2. Sleep State (Tilt Sensor Switch)
  bool nowSleeping = handleIsOnHolder();
  if (nowSleeping && !sleeping) {
    sleeping = true;
    heaterOff();
    resetPID();
    queueBeep(1);
    lcdNeedsRefresh = true;
  } else if (!nowSleeping && sleeping) {
    sleeping = false;
    readySince = 0;
    readyBeeped = false;
    resetPID();
    lcdNeedsRefresh = true;
  }

  // 3. fan cooler
  if (sleeping) {
    heaterOff();
    resetPID();
    if (sensorOK && currentTempC > 100.0f) analogWrite(FAN_PIN, 255); 
    else analogWrite(FAN_PIN, 0); 
  } else {
    analogWrite(FAN_PIN, map(fanPercent, 0, 100, 0, 255));
  }

  // 4. Heater PID control
  bool zcHealthy = isZeroCrossHealthy();
  if (!sensorOK || sleeping || !zcHealthy) {
    heaterOff();
    resetPID();
  } else {
    float error = (float)targetTempC - currentTempC;

    if (fabs(error) <= PID_ACTIVE_BAND_C) {
      pidIntegral += error * 0.25f;
      pidIntegral = constrain(pidIntegral, 0.0f, PID_INTEGRAL_MAX);
    } else {
      pidIntegral = 0.0f;
    }

    float derivative = (error - pidLastError) / 0.25f;
    pidLastError = error;

    float pTerm = HEATER_KP * error;
    float iTerm = HEATER_KI * pidIntegral;
    float dTerm = HEATER_KD * derivative;

    float output = pTerm + iTerm + dTerm;
    int demand = constrain((int)output, 0, 100);

    setHeaterDemand(demand);
  }

  // 5. Ready Status & Beep (Stabilization Band)
  if (!sleeping && sensorOK) {
    bool inBand = fabs(currentTempC - (float)targetTempC) <= READY_BAND_C;
    if (inBand) {
      if (readySince == 0) readySince = millis();
      if (!readyBeeped && (millis() - readySince >= READY_STABLE_MS)) {
        readyBeeped = true;
        queueBeep(1);
        lcdNeedsRefresh = true; 
      }
    } else {
      readySince = 0;
      // beep
    }
  }
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
      } else {
        enterFault(); 
      }
    }
  } 
  
  else if (uiState != UI_FAULT && (now - lastTempRead >= 250)) {
    lastTempRead = now;
    if (!readTemperature()) enterFault();
  }

  
  if (uiState == UI_FAULT) {
    heaterOff();
    resetPID();
    analogWrite(FAN_PIN, 0);
  } else {
    updateSystemLogic();
  }

  
  if (now - lastUiRefresh >= 200) {
    lastUiRefresh = now;
    if (lcdNeedsRefresh) {
      refreshLCD();
      lcdNeedsRefresh = false;
    } else if (uiState == UI_RUN) {
      refreshLCD(); 
    }
  }
}