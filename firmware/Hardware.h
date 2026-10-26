#ifndef HARDWARE_H
#define HARDWARE_H
#include "Config.h"

void hardwareInit();
void heaterOff();
void setHeaterDemand(int demandPercent);
void queueBeep(uint8_t count);
void updateBuzzer();
bool handleIsOnHolder();
bool readTemperature();
void updateEncoder();
void updateButton();

#endif