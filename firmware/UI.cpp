#include "UI.h"
#include "Hardware.h"

// --- PROGMEM STRING STORAGE (SAVING RAM) ---
const char str_boot_1[] PROGMEM = "Shinonome-VORSA";
const char str_boot_2[] PROGMEM = "system loading..";
const char str_fault_1[] PROGMEM = "MALFUNCTION!!";
const char str_fault_2[] PROGMEM = "TERCABUT/RUSAK";
const char str_set_temp[] PROGMEM = "SET TEMPERATURE";
const char str_set_fan[] PROGMEM = "SET AIR FLOW";
const char str_confirm[] PROGMEM = "APAKAH YAKIN?";
const char str_yes_no[] PROGMEM = ">YES        NO";
const char str_no_yes[] PROGMEM = " YES       >NO";

// --- BARE-METAL TWI (I2C) DRIVER ---
void twi_start() {
  TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
  while (!(TWCR & (1 << TWINT)));
}

void twi_write(uint8_t data) {
  TWDR = data;
  TWCR = (1 << TWINT) | (1 << TWEN);
  while (!(TWCR & (1 << TWINT)));
}

void twi_stop() {
  TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
}

static uint8_t lcd_backlight_mask = 0x08;

void lcd_send_nibble(uint8_t nibble, uint8_t mode) {
  uint8_t data = (nibble & 0xF0) | mode | lcd_backlight_mask;
  twi_start();
  twi_write(LCD_ADDR << 1);
  twi_write(data | 0x04); // Enable HIGH
  twi_write(data & ~0x04); // Enable LOW
  twi_stop();
  _delay_us(50);
}

void lcd_send_byte(uint8_t value, uint8_t mode) {
  lcd_send_nibble(value & 0xF0, mode);
  lcd_send_nibble((value << 4) & 0xF0, mode);
}

void lcd_command(uint8_t cmd) { lcd_send_byte(cmd, 0); }
void lcd_data(uint8_t data) { lcd_send_byte(data, 0x01); }

void lcd_set_cursor(uint8_t col, uint8_t row) {
  uint8_t offsets[] = {0x00, 0x40};
  lcd_command(0x80 | (col + offsets[row]));
}

void lcd_set_backlight(bool on) {
  lcd_backlight_mask = on ? 0x08 : 0x00;
  twi_start();
  twi_write(LCD_ADDR << 1);
  twi_write(lcd_backlight_mask);
  twi_stop();
}

void lcdPrintPadded_P(uint8_t col, uint8_t row, const char *pgm_str) {
  lcd_set_cursor(col, row);
  uint8_t i = 0;
  char c;
  while ((c = pgm_read_byte(pgm_str + i)) && i < 16) {
    lcd_data(c);
    i++;
  }
  while (i < 16) { lcd_data(' '); i++; }
}

void lcdPrintPadded_RAM(uint8_t col, uint8_t row, const char *str) {
  lcd_set_cursor(col, row);
  uint8_t i = 0;
  while (str[i] && i < 16) {
    lcd_data(str[i]);
    i++;
  }
  while (i < 16) { lcd_data(' '); i++; }
}

void uiInit() {
  _delay_ms(50); // Power-on wait
  lcd_send_nibble(0x30, 0); _delay_us(4500);
  lcd_send_nibble(0x30, 0); _delay_us(4500);
  lcd_send_nibble(0x30, 0); _delay_us(150);
  lcd_send_nibble(0x20, 0); // 4-bit mode

  lcd_command(0x28); // 2 Lines, 5x8 Font
  lcd_command(0x0C); // Display ON, Cursor OFF
  lcd_command(0x01); // Clear Display
  _delay_ms(2);
  lcd_command(0x06); // Entry Mode Set

  lcd_set_backlight(true);
  backlightOn = true;
  lcdPrintPadded_P(0, 0, str_boot_1);
  lcdPrintPadded_P(0, 1, str_boot_2);
}

void enterFault() {
  heaterOff();
  sensorOK = false;
  uiState = UI_FAULT;
  if (!backlightOn) { lcd_set_backlight(true); backlightOn = true; }
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
    lcd_set_backlight(true); backlightOn = true; lcdNeedsRefresh = true; return;
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
        saveSettingsToEEPROM(); // EEPROM Auto-Save on Confirmation!
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

// Lightweight integer formatter without heavy snprintf
static void format_num3(char* buf, int num) {
  buf[0] = (num >= 100) ? ('0' + (num / 100)) : ' ';
  buf[1] = (num >= 10)  ? ('0' + ((num / 10) % 10)) : ' ';
  buf[2] = '0' + (num % 10);
}

void refreshLCD() {
  static bool currentLcdState = true;
  if (backlightOn != currentLcdState) {
    lcd_set_backlight(backlightOn);
    currentLcdState = backlightOn;
  }

  char lineBuf[17];
  switch (uiState) {
    case UI_BOOT:
      lcdPrintPadded_P(0, 0, str_boot_1);
      lcdPrintPadded_P(0, 1, str_boot_2);
      break;
    case UI_FAULT:
      lcdPrintPadded_P(0, 0, str_fault_1);
      lcdPrintPadded_P(0, 1, str_fault_2);
      break;
    case UI_SET_TEMP:
      lcdPrintPadded_P(0, 0, str_set_temp);
      lineBuf[0]='T'; lineBuf[1]='a'; lineBuf[2]='r'; lineBuf[3]='g'; lineBuf[4]='e'; lineBuf[5]='t'; lineBuf[6]=':'; lineBuf[7]=' ';
      format_num3(&lineBuf[8], targetTempC);
      lineBuf[11]=' '; lineBuf[12]='C'; lineBuf[13]='\0';
      lcdPrintPadded_RAM(0, 1, lineBuf);
      break;
    case UI_SET_FAN:
      lcdPrintPadded_P(0, 0, str_set_fan);
      lineBuf[0]='F'; lineBuf[1]='a'; lineBuf[2]='n'; lineBuf[3]=':'; lineBuf[4]=' ';
      format_num3(&lineBuf[5], fanPercent);
      lineBuf[8]='%'; lineBuf[9]='\0';
      lcdPrintPadded_RAM(0, 1, lineBuf);
      break;
    case UI_CONFIRM:
      lcdPrintPadded_P(0, 0, str_confirm);
      lcdPrintPadded_P(0, 1, confirmYes ? str_yes_no : str_no_yes);
      break;
    case UI_RUN:
      // Line 1: "T:350C SP:350"
      lineBuf[0]='T'; lineBuf[1]=':';
      format_num3(&lineBuf[2], isnan(currentTempC) ? 0 : (int)currentTempC);
      lineBuf[5]='C'; lineBuf[6]=' '; lineBuf[7]='S'; lineBuf[8]='P'; lineBuf[9]=':';
      format_num3(&lineBuf[10], targetTempC);
      lineBuf[13]='\0';
      lcdPrintPadded_RAM(0, 0, lineBuf);

      // Line 2: "F: 50% S:HEAT "
      lineBuf[0]='F'; lineBuf[1]=':';
      format_num3(&lineBuf[2], fanPercent);
      lineBuf[5]='%'; lineBuf[6]=' '; lineBuf[7]='S'; lineBuf[8]=':';

      const char *st = "IDLE ";
      if (sleeping) {
        st = "SLEEP";
      } else {
        st = "HEAT ";
        if (heaterDemand == 0 && currentTempC >= targetTempC - READY_BAND_C) st = "READY";
        if (heaterDemand == 0 && currentTempC < targetTempC - READY_BAND_C) st = "IDLE ";
      }
      for(uint8_t k=0; k<5; k++) lineBuf[9+k] = st[k];
      lineBuf[14]='\0';
      lcdPrintPadded_RAM(0, 1, lineBuf);
      break;
  }
}