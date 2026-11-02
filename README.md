# Shinonome-VORSA Open-Source 858D Digital Hot-Air Rework Station (v2.0)

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://choosealicense.com/licenses/mit/)
[![Platform: ATmega328P](https://img.shields.io/badge/MCU-ATmega328P%20%2F%20Arduino%20Uno-blue.svg)](https://www.microchip.com/)
[![Architecture: AVR Bare-Metal](https://img.shields.io/badge/Architecture-Pure%20Bare--Metal-red.svg)]()

> *Read this in other languages: [Bahasa Indonesia](docs/README_ID.md)*

This **ATmega328P (Arduino Uno R3)** microcontroller-based digital hot-air rework station is an open-source DIY project designed to deliver high-precision temperature control and airflow management. It utilizes a standard 858D hot-air handle, a K-type thermocouple sensor (MAX6675), and a rotary encoder for an intuitive user interface.

In **v2.0**, the firmware has been completely refactored from the standard Arduino core environment to **Pure AVR Bare-Metal C/C++** to achieve overhead-free execution, lightning-fast response times, and an extremely low memory footprint.

> **HIGH VOLTAGE SAFETY WARNING (220V AC)**
> 
> This project deals directly with lethal 220V/230V AC mains electricity. Incorrect assembly can lead to hardware destruction, fire hazard, or fatal electric shock.
> - Never touch any part of the TRIAC/Heater circuitry while the system is connected to mains power.
> - Ensure high-voltage circuits are physically and optocoupled-isolated from microcontroller lines.
> - Assemble and operate entirely at your own risk.

---

## Key Features

- **Pure Bare-Metal Firmware (v2.0):** Runs without Arduino library dependencies (`Wire.h`, `LiquidCrystal_I2C.h`, `EEPROM.h`, `pinMode`, `digitalWrite`). Compiled natively via `avr-gcc` and `Makefile`.
- **Extreme Memory Footprint Optimization:** Extremely lightweight footprint (~6.4 KB Flash & 128 Bytes SRAM). UI text strings are stored in Flash ROM (`PROGMEM`) with custom lightweight integer formatting.
- **Non-Volatile EEPROM Storage:** Target temperature (`targetTempC`) and fan speed (`fanPercent`) settings are saved automatically to EEPROM upon UI confirmation.
- **PID Control & TRIAC Phase-Angle Timing:** Smoothly heats the element and precisely maintains temperature using AC Zero-Crossing detection (INT0) and Timer2 CTC interrupt timing.
- **Auto-Sleep & Automated Cooldown:** When the handle is placed in its cradle, heater power is instantly cut while the blower fan ramps up to 100% PWM until the nozzle drops below 100°C to prevent thermal damage.
- **Failsafe & Fault Detection:** Instantly cuts power and triggers a *MALFUNCTION* alarm if the thermocouple opens/fails or if the AC Zero-Cross sync pulse is lost.
- **Display Power Management:** LCD backlight automatically turns off after 60 seconds of inactivity.

---

## Component Requirements & Pin Mapping

| Component | Arduino Pin | AVR Port/Pin | Register Type / Function |
| :--- | :--- | :--- | :--- |
| Zero-Cross Signal | D2 | PD2 | External Interrupt 0 (INT0) |
| Encoder SW | D3 | PD3 | Digital Input Pull-up |
| Encoder DT | D4 | PD4 | Digital Input Pull-up |
| Encoder CLK | D5 | PD5 | Digital Input Pull-up |
| Buzzer | D6 | PD6 | Digital Output |
| TRIAC Gate | D7 | PD7 | Digital Output (Timer2 Phase Delay) |
| Handle Switch | D8 | PB0 | Digital Input Pull-up (Sleep/Work) |
| Blower Fan PWM | D9 | PB1 | Output Hardware Fast PWM Timer1 (OC1A) |
| MAX6675 CS | D10 | PB2 | SPI Slave Select (Output) |
| MAX6675 SO | D12 | PB4 | SPI MISO Input |
| MAX6675 SCK | D13 | PB5 | SPI Serial Clock Output |
| LCD 16x2 I2C | A4 / A5 | PC4 / PC5 | Hardware TWI (SDA / SCL) |

---

## Documentation & Firmware Build Guide

For detailed bare-metal architecture, ATmega328P register configuration, and compilation (`make`) and flashing (`avrdude`) instructions, please read the technical documentation:

[Firmware Documentation & Build Guide](firmware/README.md)

---

## License

This project is licensed under the MIT License. You are free to use, modify, and distribute this project for both commercial and non-commercial purposes.

Copyright (c) 2026 Rul Chan & Averyandra. All Rights Reserved.