#ifndef CONFIG_H
#define CONFIG_H

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <math.h>

// --- EEPROM ADDRESSES ---
#define EEPROM_ADDR_MAGIC     0x00
#define EEPROM_ADDR_TARGET_C  0x02
#define EEPROM_ADDR_FAN_PCT   0x04
#define EEPROM_MAGIC_VALUE    0x5A3C

// --- SYSTEM CONFIGURATION ---
#define LCD_ADDR 0x27

const bool HANDLE_ON_HOLDER_LEVEL = 0; // LOW = Handle in Holder (Sleep)
const int MIN_TEMP_C = 100;
const int MAX_TEMP_C = 500;
const int MIN_FAN_PERCENT = 20;
const int MAX_FAN_PERCENT = 100;
const float READY_BAND_C = 5.0f;
const uint32_t BACKLIGHT_TIMEOUT_MS = 60000;
const uint32_t READY_STABLE_MS = 2000;

// --- PID TUNING PARAMETERS ---
const float HEATER_KP = 2.5f;
const float HEATER_KI = 0.2f;
const float HEATER_KD = 1.0f;
const float PID_INTEGRAL_MAX = 200.0f;
const float PID_ACTIVE_BAND_C = 30.0f;

// --- MACHINE STATE ---
enum UiState : uint8_t { UI_BOOT, UI_FAULT, UI_SET_TEMP, UI_SET_FAN, UI_CONFIRM, UI_RUN };

// --- GLOBAL VARIABLES (EXTERN) ---
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
extern uint32_t lastActivity;
extern volatile uint32_t systemMillis;
extern volatile uint32_t lastZeroCrossMicros;

#endif
