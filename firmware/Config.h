#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// --- PIN MAP ---
const uint8_t BUZZER_PIN        = 6;
const uint8_t TRIAC_PIN         = 7;
const uint8_t HANDLE_SWITCH_PIN = 8;
const uint8_t FAN_PIN           = 9;
const uint8_t ENC_SW            = 3;
const uint8_t ENC_DT            = 4;
const uint8_t ENC_CLK           = 5;
const uint8_t MAX_CS            = 10;
const uint8_t MAX_SO            = 11;
const uint8_t MAX_SCK           = 13;
const uint8_t ZERO_CROSS_PIN    = 2;

// --- SYSTEM CONFIGURATION ---
const uint8_t LCD_ADDRESS = 0x27;
// HIGH = Handle up (Sleep), LOW = Handle down (Work)
const bool HANDLE_ON_HOLDER_LEVEL = HIGH; 
const int MIN_TEMP_C = 100;
const int MAX_TEMP_C = 500;
const int MIN_FAN_PERCENT = 20;
const int MAX_FAN_PERCENT = 100;
const float READY_BAND_C = 5.0;
const unsigned long BACKLIGHT_TIMEOUT_MS = 60000;
const unsigned long READY_STABLE_MS = 2000;

// --- MACHINE STATE ---
enum UiState : uint8_t { UI_BOOT, UI_FAULT, UI_SET_TEMP, UI_SET_FAN, UI_CONFIRM, UI_RUN };

// --- GLOBAL VARIABLES (EXTERN) ---
// Only declared here, initialized in the main file (.ino)
extern UiState uiState;
extern float currentTempC;
extern int targetTempC;
extern int savedTargetTempC;
extern int fanPercent;
extern int savedFanPercent;
extern int heaterDemand;
extern bool sensorOK;
extern bool sleeping;
extern bool backlightOn;
extern bool confirmYes;
extern bool lcdNeedsRefresh;
extern unsigned long lastActivity;

#endif