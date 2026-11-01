#include "UI.h"
#include "Hardware.h" 
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(LCD_ADDRESS, 16, 2);

void lcdPrintPadded(uint8_t col, uint8_t row, const char *text) {
  lcd.setCursor(col, row);
  uint8_t i = 0;
  while (text[i] && i < 16) { lcd.print(text[i]); i++; }
  while (i < 16) { lcd.print(' '); i++; }
}

void uiInit() {
  lcd.init();
  lcd.backlight();
  backlightOn = true;
  lcdPrintPadded(0, 0, "Shinonome-VORSA");
  lcdPrintPadded(0, 1, "system loading..");
}

void enterFault() {
  heaterOff();
  sensorOK = false;
  uiState = UI_FAULT;
  if (!backlightOn) { lcd.backlight(); backlightOn = true; }
  queueBeep(3);
  lcdNeedsRefresh = true;
}

void enterRunMode() {
  uiState = UI_RUN;
  sleeping = handleIsOnHolder();
  lastActivity = millis();
  lcdNeedsRefresh = true;
}

void onEncoderButton() {
  lastActivity = millis();
  
  if (!backlightOn) { 
    lcd.backlight(); 
    backlightOn = true; 
    lcdNeedsRefresh = true; 
    return; 
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
        targetTempC = savedTargetTempC; 
        fanPercent = savedFanPercent;
      }
      enterRunMode();
      break;
    case UI_BOOT: case UI_FAULT: break;
  }
  lcdNeedsRefresh = true;
}

void refreshLCD() {
  static bool currentLcdState = true;
  if (backlightOn != currentLcdState) {
    if (backlightOn) {
      lcd.backlight();
    } else {
      lcd.noBacklight();
    }
    currentLcdState = backlightOn;
  }

  char line1[17], line2[17];
  switch (uiState) {
    case UI_BOOT:
      lcdPrintPadded(0, 0, "Shinonome-VORSA");
      lcdPrintPadded(0, 1, "system loading..");
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
      lcdPrintPadded(0, 0, "APAKAH YAKIN?"); 
      lcdPrintPadded(0, 1, confirmYes ? ">YES       NO" : " YES      >NO");
      break;
    case UI_RUN:
      snprintf(line1, sizeof(line1), "T:%3dC SP:%3d", isnan(currentTempC) ? 0 : (int)currentTempC, targetTempC);
      const char *statusTxt;
      if (sleeping) {
        statusTxt = "SLEEP";
      } else {
        statusTxt = "HEAT "; 
        if (heaterDemand == 0 && currentTempC >= targetTempC - READY_BAND_C) statusTxt = "READY";
        if (heaterDemand == 0 && currentTempC < targetTempC - READY_BAND_C) statusTxt = "IDLE ";
      }
      snprintf(line2, sizeof(line2), "F:%3d%% S:%s", fanPercent, statusTxt);
      lcdPrintPadded(0, 0, line1);
      lcdPrintPadded(0, 1, line2);
      break;
  }
}