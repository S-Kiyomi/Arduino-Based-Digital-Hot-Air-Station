# Shinonome-VORSA 858D Hot-Air Station Bare-Metal Firmware (v2.0)

## Overview

This directory contains the high-performance **Bare-Metal Firmware (v2.0)** for the Arduino/ATmega328P-Based Digital Hot-Air Station (Shinonome-VORSA). It is specifically tailored for controlling an 858D hot-air rework handle equipped with a tilt/magnetic switch and MAX6675 thermocouple sensing.

The codebase has been refactored from higher-level Arduino abstractions into pure AVR bare-metal C/C++. By eliminating standard Arduino cores (`digitalRead`, `digitalWrite`, `analogWrite`, `Wire.h`, `EEPROM.h`, `LiquidCrystal_I2C.h`), the firmware achieves near-zero memory overhead, ultra-fast interrupt latency, extremely small flash consumption, and persistent configuration persistence.

## Architectural Highlights & Bare-Metal Enhancements

The firmware strictly bypasses hardware abstraction layers (HAL) to interact directly with ATmega328P registers:

- **Bare-Metal TWI/I2C Engine**: Replaces `Wire.h` and `LiquidCrystal_I2C.h`. Uses raw `TWCR`, `TWDR`, and `TWBR` registers for direct 100kHz I2C protocol communication with the PCF8574 LCD backpack.
- **Hardware SPI (MAX6675)**: SPI communication is handled via raw `SPDR` register transfers instead of software bit-banging, optimizing temperature polling efficiency.
- **PROGMEM & Memory Optimization**: All static UI strings are retained in Flash ROM using `PROGMEM` and `pgm_read_byte`. Custom lightweight integer formatters replace heavy `snprintf` routines to save over 2KB of flash and minimize SRAM usage.
- **Persistent EEPROM Storage**: Direct AVR register access (`EEAR`, `EEDR`, `EECR`) provides non-volatile saving for `targetTempC` and `fanPercent` settings upon UI confirmation.
- **Hardware Fast PWM & Timers**: Kipas dikendalikan langsung oleh Timer1 Fast PWM (`OCR1A` on PB1), SysTick waktu berbasis Timer0 register, serta kontrol fasa TRIAC presisi berbasis Timer2 CTC interrupt.

## Directory Structure

- **firmware.ino**: Main entry point containing initialization, PID heat control loop, sleep logic, and high-level polling timers.
- **Config.h**: Centralized hardware pin mapping (AVR Port definitions), system parameters, PID gains, and EEPROM addresses.
- **Hardware.h & Hardware.cpp**: Hardware abstraction module operating directly on AVR registers. Handles INT0 Zero-Cross interrupts, Timer2 TRIAC gate pulse generation, Hardware SPI, PWM Timer1, SysTick Timer0, and bare-metal EEPROM read/write.
- **UI.h & UI.cpp**: Bare-metal TWI/I2C LCD driver, custom string formatting, PROGMEM text rendering, and state-machine menu navigation.

## Key Features

1. **Bare-Metal Performance**: Drastically reduced flash binary footprint and SRAM usage with instant response times.
2. **Handle State Detection**: Monitors the handle switch (PB0 / D8) to instantly transition between active heating (`HEAT`) and auto-cooldown (`SLEEP`).
3. **Automated Cooldown Safety**: Instantly cuts off TRIAC power upon sleep, forcing blower fan PWM to 100% until nozzle temperature drops below 100°C.
4. **Zero-Cross TRIAC Phase Control**: Synchronized AC zero-crossing via external interrupt INT0 (PD2) combined with Timer2 CTC for phase-angle delay heating control.
5. **Non-Volatile Configuration**: Automatic bare-metal EEPROM auto-save when confirming new temperature or fan settings.
6. **Strict Hardware Failsafe**: Continuous health checking of MAX6675 open-thermocouple status and zero-cross AC synchronization pulse presence. Enters `MALFUNCTION` mode immediately on fault.

## Hardware Pin Mapping (ATmega328P Register Reference)

- **Heater Control**: `PD7` / D7 (TRIAC Gate), `PD2` / D2 (Zero-Cross INT0)
- **Fan PWM Control**: `PB1` / D9 (Timer1 OC1A PWM Output)
- **Thermocouple Sensor (MAX6675 Hardware SPI)**: `PB2` / D10 (CS), `PB4` / D12 (MISO/SO), `PB5` / D13 (SCK)
- **User Interface Inputs**: `PD3` / D3 (Encoder SW), `PD4` / D4 (Encoder DT), `PD5` / D5 (Encoder CLK), `PB0` / D8 (Handle Sensor Input)
- **System Outputs**: `PD6` / D6 (Buzzer)
- **Display I2C Interface**: `PC4` / A4 (SDA), `PC5` / A5 (SCL)

## External Dependencies

- **None**. The firmware is 100% self-contained and operates purely on native `<avr/io.h>`, `<avr/interrupt.h>`, and `<avr/pgmspace.h>` headers included with the AVR-GCC toolchain.

---

## Changelog / Recent Updates (v2.0 Refactor)

### Bare-Metal Migration & Performance Overhaul
- **Removed Arduino Core Dependencies**: Eliminated `Wire.h`, `LiquidCrystal_I2C.h`, `EEPROM.h`, `pinMode()`, `digitalRead()`, `digitalWrite()`, and `analogWrite()`.
- **Direct Register Driver Implementation**: Implemented bare-metal TWI driver, hardware SPI for MAX6675, Fast PWM Timer1 for fan speed, and register-level EEPROM handlers.
- **SRAM & Flash Savings**: Migrated all UI strings to `PROGMEM` and replaced `snprintf` with custom lightweight integer formatters, drastically reducing binary size.
- **EEPROM Auto-Save**: Integrated bare-metal EEPROM word reading/writing to preserve user setpoints (`targetTempC` and `fanPercent`) across reboots.