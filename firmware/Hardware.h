#ifndef HARDWARE_H
#define HARDWARE_H

#include "Config.h"

extern volatile unsigned long lastZeroCrossMicros;

void hardwareInit();
void heaterOff();
void setHeaterDemand(int demandPercent);
void queueBeep(uint8_t count);
void updateBuzzer();
bool handleIsOnHolder();
bool readTemperature();
void updateEncoder();
void updateButton();
bool isZeroCrossHealthy();

#endif