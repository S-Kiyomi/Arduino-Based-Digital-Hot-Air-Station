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

uint32_t bootStarted = 0;
uint32_t lastTempRead = 0;
uint32_t lastUiRefresh = 0;
uint32_t readySince = 0;
bool readyBeeped = false;

static float pidIntegral = 0.0f;
static float pidLastError = 0.0f;

void resetPID() {
  pidIntegral = 0.0f;
  pidLastError = 0.0f;
}

void updateSystemLogic() {
  uint32_t now = millis();
  
  if (uiState != UI_FAULT && backlightOn && (now - lastActivity >= BACKLIGHT_TIMEOUT_MS)) {
    backlightOn = false;
    lcdNeedsRefresh = true;
  }

  if (uiState != UI_RUN) return;

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

  if (sleeping) {
    heaterOff();
    resetPID();
    if (sensorOK && currentTempC > 100.0f) setFanPWM(255);
    else setFanPWM(0);
  } else {
    uint8_t fanDuty = (uint8_t)((uint32_t)fanPercent * 255 / 100);
    setFanPWM(fanDuty);
  }

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

    float output = (HEATER_KP * error) + (HEATER_KI * pidIntegral) + (HEATER_KD * derivative);
    int demand = (output < 0.0f) ? 0 : ((output > 100.0f) ? 100 : (int)output);
    setHeaterDemand(demand);
  }

  if (!sleeping && sensorOK) {
    bool inBand = fabs(currentTempC - (float)targetTempC) <= READY_BAND_C;
    if (inBand) {
      if (readySince == 0) readySince = now;
      if (!readyBeeped && (now - readySince >= READY_STABLE_MS)) {
        readyBeeped = true;
        queueBeep(1);
        lcdNeedsRefresh = true;
      }
    } else {
      readySince = 0;
    }
  }
}

int main(void) {
  hardwareInit();
  uiInit();

  bootStarted = millis();
  lastActivity = millis();

  // Infinite Loop
  while (1) {
    uint32_t now = millis();

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

    if (uiState != UI_BOOT && uiState != UI_FAULT && (now - lastTempRead >= 250)) {
      lastTempRead = now;
      if (!readTemperature()) enterFault();
    }

    if (uiState == UI_FAULT) {
      heaterOff();
      resetPID();
      setFanPWM(0);
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
  return 0;
}