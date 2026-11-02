# Shinonome-VORSA 858D Hot-Air Station Bare-Metal Firmware (v2.0)

> *Read this in other languages: [Bahasa Indonesia](../docs/firmware/README_ID.md)*

## Overview

This directory contains the high-performance **Bare-Metal Firmware (v2.0)** for the ATmega328P-Based Digital Hot-Air Station (Shinonome-VORSA). It is specifically tailored for controlling an 858D hot-air rework handle equipped with a tilt/magnetic switch and MAX6675 thermocouple sensing.

The codebase operates completely independent of the Arduino IDE and core libraries (`Wire.h`, `EEPROM.h`, `LiquidCrystal_I2C.h`, `pinMode`, `digitalWrite`). By transitioning to a pure standalone C/C++ architecture compiled directly via `avr-gcc` and `make`, the firmware achieves near-zero memory overhead, ultra-fast interrupt latency, minimal flash consumption (under 6.5 KB), and persistent EEPROM storage.

---

## Architectural Highlights & Bare-Metal Enhancements

The firmware strictly bypasses hardware abstraction layers (HAL) to interact directly with ATmega328P registers:

- **Bare-Metal TWI/I2C Engine**: Replaces `Wire.h` and `LiquidCrystal_I2C.h`. Uses raw `TWCR`, `TWDR`, and `TWBR` registers for direct 100kHz I2C protocol communication with the PCF8574 LCD backpack.
- **Hardware SPI (MAX6675)**: SPI communication is handled via raw `SPDR` register transfers instead of software bit-banging, optimizing temperature polling efficiency.
- **PROGMEM & Memory Optimization**: Static UI strings are retained in Flash ROM using `PROGMEM` and `pgm_read_byte`. Custom lightweight integer formatters replace heavy `snprintf` routines to save over 2KB of flash and minimize SRAM usage (~128 bytes total).
- **Persistent EEPROM Storage**: Direct AVR register access (`EEAR`, `EEDR`, `EECR`) provides non-volatile saving for `targetTempC` and `fanPercent` settings upon UI confirmation.
- **Hardware Fast PWM & Timers**: Blower fan speed is controlled via Timer1 Fast PWM (`OCR1A` on PB1), SysTick time tracking via Timer0 overflow, and TRIAC phase-angle delay firing via Timer2 CTC interrupt.

---

## Directory Structure

- **`main.cpp`**: Core application entry point containing `int main(void)`, system loop, PID heat control logic, and sleep logic.
- **`Config.h`**: Centralized hardware pin mapping (AVR Port definitions), system parameters, PID gains, and EEPROM addresses.
- **`Hardware.h` & `Hardware.cpp`**: Hardware abstraction module operating directly on AVR registers. Handles INT0 Zero-Cross interrupts, Timer2 TRIAC gate pulse generation, Hardware SPI, PWM Timer1, SysTick Timer0, and bare-metal EEPROM read/write.
- **`UI.h` & `UI.cpp`**: Bare-metal TWI/I2C LCD driver, custom string formatting, PROGMEM text rendering, and state-machine menu navigation.
- **`Makefile`**: Build automation script targeting `avr-gcc`, `avr-objcopy`, and `avrdude` with auto-generated `build/` directory output.

---

## Build and Flashing Instructions

### Prerequisites

Install the AVR toolchain on your Linux system (e.g., Fedora):

```bash
sudo dnf install avr-gcc avr-gcc-c++ avr-libc avrdude make
```

### Commands

- **Build firmware**:
  ```bash
  make
  ```
  *Compiles C++ sources into `build/firmware.hex` and displays memory usage.*

- **Flash to ATmega328P (Arduino Board)**:
  ```bash
  make flash
  ```

- **To specify a custom serial port (e.g., `/dev/ttyACM0`)**:
  ```bash
  make flash PORT=/dev/ttyACM0
  ```

- **Clean build artifacts**:
  ```bash
  make clean
  ```

---

## Key Features

1. **Bare-Metal Performance**: Drastically reduced flash binary footprint (~19.6%) and SRAM usage (~6.2%) with instant response times.
2. **Handle State Detection**: Monitors the handle switch (PB0 / D8) to instantly transition between active heating (`HEAT`) and auto-cooldown (`SLEEP`).
3. **Automated Cooldown Safety**: Instantly cuts off TRIAC power upon sleep, forcing blower fan PWM to 100% until nozzle temperature drops below 100°C.
4. **Zero-Cross TRIAC Phase Control**: Synchronized AC zero-crossing via external interrupt INT0 (PD2) combined with Timer2 CTC for phase-angle delay heating control.
5. **Non-Volatile Configuration**: Automatic bare-metal EEPROM auto-save when confirming new temperature or fan settings.
6. **Strict Hardware Failsafe**: Continuous health checking of MAX6675 open-thermocouple status and zero-cross AC synchronization pulse presence. Enters `MALFUNCTION` mode immediately on fault.

---

## Hardware Pin Mapping (ATmega328P Register Reference)

- **Heater Control**: PD7 / D7 (TRIAC Gate), PD2 / D2 (Zero-Cross INT0)
- **Fan PWM Control**: PB1 / D9 (Timer1 OC1A PWM Output)
- **Thermocouple Sensor (MAX6675 Hardware SPI)**: PB2 / D10 (CS), PB4 / D12 (MISO/SO), PB5 / D13 (SCK)
- **User Interface Inputs**: PD3 / D3 (Encoder SW), PD4 / D4 (Encoder DT), PD5 / D5 (Encoder CLK), PB0 / D8 (Handle Sensor Input)
- **System Outputs**: PD6 / D6 (Buzzer)
- **Display I2C Interface**: PC4 / A4 (SDA), PC5 / A5 (SCL)

---

## External Dependencies

None. The firmware is 100% self-contained and operates purely on native `<avr/io.h>`, `<avr/interrupt.h>`, and `<avr/pgmspace.h>` headers included with the AVR-GCC toolchain.

---

## Changelog / Recent Updates (v2.0 Bare-Metal Refactor)

- **Standalone AVR-GCC Migration**: Removed Arduino core runtime and replaced `firmware.ino` with a standard `main.cpp` entry point and structured `Makefile`.
- **Direct Register Driver Implementation**: Implemented native bare-metal TWI driver, hardware SPI for MAX6675, Fast PWM Timer1 for fan speed, Timer0 SysTick, and register-level EEPROM handlers.
- **SRAM & Flash Optimization**: Migrated UI strings to `PROGMEM` and replaced `snprintf` with lightweight integer formatting, bringing memory consumption down to 6.4 KB Flash and 128 Bytes RAM.
- **EEPROM Auto-Save**: Integrated bare-metal EEPROM word reading/writing to preserve user setpoints (`targetTempC` and `fanPercent`) across reboots.