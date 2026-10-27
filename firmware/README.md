# 858D Hot-Air Station Modular Firmware

## Overview

This directory contains the modular firmware for the Arduino-Based Digital Hot-Air Station. It is specifically designed to control an 858D hot-air rework handle equipped with a tilt sensor (gravity switch) to determine its operational state.

The firmware has been refactored into a clean, modular C++ architecture to separate hardware control, user interface rendering, and core system logic. This ensures easier maintenance, high responsiveness, and strict adherence to hardware safety standards.

## Firmware Architecture

The codebase is divided into the following modules to prevent logic tangling:

* **main.ino**: The core application loop. It handles the high-level state machine, automated cooling logic, and the PID control loop for the heater.

* **Config.h**: The centralized configuration header. It contains all pin mappings, system constants (such as PID parameters, hardware limits, and timeouts), and global state variables.

* **Hardware.h & Hardware.cpp**: The hardware abstraction layer. This module encapsulates TRIAC firing via Timer2 interrupts, zero-crossing detection, custom MAX6675 thermocouple bit-banging, rotary encoder polling, and buzzer management.

* **UI.h & UI.cpp**: The User Interface module. It handles the 16x2 I2C LCD rendering, menu navigation (Set Temp, Set Fan, Confirmation dialogs), and UI state transitions.

## Key Features

1. **Tilt Sensor Integration**: Detects the handle orientation. Facing down (LOW) tells the system the tool is in use; facing up (HIGH) triggers the sleep sequence.

2. **Automated Cooldown**: When entering sleep mode, the TRIAC is immediately disabled to cut off the heater, but the blower fan is forced to maximum speed until the internal nozzle temperature drops below 100 degrees Celsius to prevent the plastic handle from melting.

3. **Proportional Heater Control**: Utilizes zero-crossing detection and phase-angle delay control via a TRIAC to smoothly regulate the 220V AC heater element.

4. **Dynamic Encoder Navigation**: The rotary encoder logic supports acceleration (fast turning automatically increases step sizes for quick adjustments) and features an auto-sleep timeout for the LCD backlight to save power.

5. **Strict Safety Fault Detection**: Constantly monitors the MAX6675 thermocouple and the AC zero-crossing signal. If the temperature sensor is disconnected or the AC synchronization pulse is lost, the system instantly halts all heating operations and throws a "MALFUNCTION" alarm.

## Pin Mapping Reference

* **Heater Control**: D7 (TRIAC Gate), D2 (Zero-Cross Input)

* **Fan Control**: D9 (PWM Output)

* **Sensors**: D8 (Tilt Switch Input), D10 (MAX6675 CS), D11 (MAX6675 SO), D13 (MAX6675 SCK)

* **User Input**: D3 (Encoder Push Button), D4 (Encoder DT), D5 (Encoder CLK)

* **Output**: D6 (Buzzer), I2C Pins A4/SDA & A5/SCL (LCD)

## Dependencies

* `Wire.h` (Arduino Built-in)

* `LiquidCrystal_I2C.h` (External library required for the 16x2 LCD display)

* Note: The MAX6675 is read using direct pin manipulation (bit-banging) to avoid blocking delays present in standard external libraries.


## Changelog / Recent Updates

### Bug Fixes & Improvements
* **Display Power Management (`UI.cpp`)**: Fixed a bug where the LCD backlight would remain on indefinitely. The `refreshLCD()` function now correctly issues hardware-level commands to turn off the I2C LCD backlight after the configured 60-second inactivity timeout.
* **Failsafe Disconnect Detection (`Hardware.cpp`)**: Improved the MAX6675 thermocouple reading logic. The `MAX_SO` pin now utilizes the internal pull-up resistor (`INPUT_PULLUP`), and a strict `< 5.0°C` threshold was added to `readTemperature()`. This ensures the system immediately enters the **MALFUNCTION** state and cuts off the heater if the 858D handle is completely detached or if the connection lines fail.
