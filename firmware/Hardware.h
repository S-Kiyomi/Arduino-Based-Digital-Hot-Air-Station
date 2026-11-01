#ifndef HARDWARE_H
#define HARDWARE_H

#include "Config.h"

// System Timing & Core Functions
void timer0_init();
uint32_t millis();
uint32_t micros();

// Peripheral Inits
void hardwareInit();
void eeprom_write_word_bare(uint16_t addr, uint16_t data);
uint16_t eeprom_read_word_bare(uint16_t addr);
void saveSettingsToEEPROM();
void loadSettingsFromEEPROM();

// Control Functions
void heaterOff();
void setHeaterDemand(int demandPercent);
void setFanPWM(uint8_t duty8bit);
void queueBeep(uint8_t count);
void updateBuzzer();
bool handleIsOnHolder();
bool readTemperature();
void updateEncoder();
void updateButton();

#endif