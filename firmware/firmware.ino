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
uint32_t lastActivity = 0;

// Main Loop Variables
uint32_t bootStarted = 0;
uint32_t lastTempRead = 0;
uint32_t lastUiRefresh = 0;
uint32_t readySince = 0;
bool readyBeeped = false;

// PID Controller State Variables
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
  // 1. Backlight Timeout
  if (uiState != UI_FAULT && backlightOn && (millis() - lastActivity >= BACKLIGHT_TIMEOUT_MS)) {
    backlightOn = false;
    lcdNeedsRefresh = true;
  }

  if (uiState != UI_RUN) return;

  // 2. Sleep State (Tilt/Holder Switch)
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

  // 3. Fan Cooling System (Bare-Metal PWM)
  if (sleeping) {
    heaterOff();
    resetPID();
    if (sensorOK && currentTempC > 100.0f) setFanPWM(255); // 100% Cooling
    else setFanPWM(0);
  } else {
    uint8_t fanDuty = (uint8_t)((uint32_t)fanPercent * 255 / 100);
    setFanPWM(fanDuty);
  }

  // 4. Heater PID Control
  bool zcHealthy = (lastZeroCrossMicros != 0UL) && ((micros() - lastZeroCrossMicros) < 30000UL);
  if (!sensorOK || sleeping || !zcHealthy) {
    heaterOff();
    resetPID();
  } else {
    float error = (float)targetTempC - currentTempC;

    if (fabs(error) <= PID_ACTIVE_BAND_C) {
      pidIntegral += error * 0.25f;
      pidIntegral = (pidIntegral < 0.0f) ? 0.0f : ((pidIntegral > PID_INTEGRAL_MAX) ? PID_INTEGRAL_MAX : pidIntegral);
    } else {
      pidIntegral = 0.0f;
    }

    float derivative = (error - pidLastError) / 0.25f;
    pidLastError = error;

    float pTerm = HEATER_KP * error;
    float iTerm = HEATER_KI * pidIntegral;
    float dTerm = HEATER_KD * derivative;

    float output = pTerm + iTerm + dTerm;
    int demand = (output < 0.0f) ? 0 : ((output > 100.0f) ? 100 : (int)output);

    setHeaterDemand(demand);
  }

  // 5. Ready Status & Beep
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
    }
  }
}

void loop() {
  uint32_t now = millis();

  updateEncoder();
  updateButton();
  updateBuzzer();

  // 3-Second Boot Phase
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

  // Temperature Polling (250ms)
  if (uiState != UI_BOOT && uiState != UI_FAULT && (now - lastTempRead >= 250)) {
    lastTempRead = now;
    if (!readTemperature()) enterFault();
  }

  // System State Handler
  if (uiState == UI_FAULT) {
    heaterOff();
    resetPID();
    setFanPWM(0);
  } else {
    updateSystemLogic();
  }

  // UI Refresh Interval (200ms)
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