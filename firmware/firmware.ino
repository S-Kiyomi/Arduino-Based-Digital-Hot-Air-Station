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

// Main Loop Specific Variables
unsigned long bootStarted = 0;
unsigned long lastTempRead = 0;
unsigned long lastUiRefresh = 0;
unsigned long readySince = 0;
bool readyBeeped = false;

void setup() {
  hardwareInit();
  uiInit();
  
  bootStarted = millis();
  lastActivity = millis();
}

void updateSystemLogic() {
  // 1. Backlight Timeout
  if (uiState != UI_FAULT && backlightOn && (millis() - lastActivity >= BACKLIGHT_TIMEOUT_MS)) {
    // lcd.noBacklight() handled via bypass to state variable
    backlightOn = false; 
    lcdNeedsRefresh = true;
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

  // 3. Fan Cooling System
  if (sleeping) {
    heaterOff();
    if (sensorOK && currentTempC > 100.0f) analogWrite(FAN_PIN, 255); 
    else analogWrite(FAN_PIN, 0); 
  } else {
    analogWrite(FAN_PIN, map(fanPercent, 0, 100, 0, 255));
  }

  // 4. Heater PID Control
  if (!sensorOK || sleeping) {
    heaterOff();
  } else {
    float error = (float)targetTempC - currentTempC;
    if (error <= 0.5f) {
      setHeaterDemand(0);
    } else {
      setHeaterDemand(constrain((int)(error * 2.0f), 0, 100)); // KP Constant = 2.0
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
        lcdNeedsRefresh = true; 
      }
    } else {
      readySince = 0;
      if (readyBeeped) {
        readyBeeped = false; 
        lcdNeedsRefresh = true; 
      }
    }
  }
}

void loop() {
  unsigned long now = millis();

  updateEncoder();
  updateButton();
  updateBuzzer();

  // Temperature Polling Interval (250ms) - Used for Boot and Run
  if (uiState != UI_FAULT && (now - lastTempRead >= 250)) {
    lastTempRead = now;

    if (uiState == UI_BOOT) {
      // Start checking the temperature after 3 seconds
      if (now - bootStarted >= 3000) {
        if (readTemperature()) {
          queueBeep(1);
          enterRunMode();
        } else if (now - bootStarted >= 4500) {
          // If failures persist for up to 4.5 seconds, then execute FAULT.
          enterFault(); 
        }
      }
    } else {
      // During normal RUN mode, monitor continuously
      if (!readTemperature()) enterFault();
    }
  }

  // System State Handler
  if (uiState == UI_FAULT) {
    heaterOff();
    analogWrite(FAN_PIN, 0);
  } else {
    updateSystemLogic();
  }

  // UI Refresh Interval (200ms)
  if (now - lastUiRefresh >= 200) {
    lastUiRefresh = now;
    if (lcdNeedsRefresh) {
      refreshLCD();
      lcdNeedsRefresh = false;
    } else if (uiState == UI_RUN && backlightOn) {
      refreshLCD(); 
    }
  }
}