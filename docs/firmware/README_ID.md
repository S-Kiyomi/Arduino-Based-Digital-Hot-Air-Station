# Shinonome-VORSA 858D Hot-Air Station Bare-Metal Firmware (v2.0)

> *Baca dalam bahasa lain: [English](/firmware/README.md)*

## Overview

Direktori ini berisi **Bare-Metal Firmware (v2.0)** berkinerja tinggi untuk ATmega328P-Based Digital Hot-Air Station (Shinonome-VORSA). Firmware ini dirancang khusus untuk mengontrol *rework handle* udara panas 858D yang dilengkapi dengan *tilt/magnetic switch* dan pembacaan termokopel MAX6675.

Codebase ini beroperasi sepenuhnya independen dari Arduino IDE dan *core libraries* (`Wire.h`, `EEPROM.h`, `LiquidCrystal_I2C.h`, `pinMode`, `digitalWrite`). Dengan beralih ke arsitektur C/C++ *standalone* murni yang dikompilasi langsung via `avr-gcc` dan `make`, firmware ini mencapai *memory overhead* yang mendekati nol, *interrupt latency* ultra-cepat, konsumsi *flash memory* minimal (di bawah 6.5 KB), dan penyimpanan EEPROM persisten.

---

## Architectural Highlights & Bare-Metal Enhancements

Firmware ini secara ketat memintas *Hardware Abstraction Layer* (HAL) untuk berinteraksi langsung dengan register ATmega328P:

- **Bare-Metal TWI/I2C Engine**: Menggantikan `Wire.h` dan `LiquidCrystal_I2C.h`. Menggunakan *raw registers* `TWCR`, `TWDR`, dan `TWBR` untuk komunikasi protokol I2C 100kHz secara langsung dengan PCF8574 LCD *backpack*.
- **Hardware SPI (MAX6675)**: Komunikasi SPI ditangani melalui transfer *raw register* `SPDR` alih-alih *software bit-banging*, mengoptimalkan efisiensi *temperature polling*.
- **PROGMEM & Memory Optimization**: *UI strings* statis disimpan di Flash ROM menggunakan `PROGMEM` dan `pgm_read_byte`. Custom *lightweight integer formatter* menggantikan rutinitas `snprintf` yang berat untuk menghemat lebih dari 2KB *flash* serta meminimalkan penggunaan SRAM (~128 byte total).
- **Persistent EEPROM Storage**: Akses register AVR langsung (`EEAR`, `EEDR`, `EECR`) menyediakan penyimpanan *non-volatile* untuk nilai `targetTempC` dan `fanPercent` saat konfirmasi UI.
- **Hardware Fast PWM & Timers**: Kecepatan *blower fan* dikontrol melalui Timer1 Fast PWM (`OCR1A` pada PB1), *time tracking* SysTick via Timer0 *overflow*, dan pemicuan *phase-angle delay* TRIAC via Timer2 CTC *interrupt*.

---

## Directory Structure

- **`main.cpp`**: *Entry point* aplikasi utama yang berisi `int main(void)`, *system loop*, logika PID *heat control*, dan logika *sleep*.
- **`Config.h`**: Pemetaan *hardware pin* terpusat (definisi AVR Port), parameter sistem, *PID gains*, dan alamat EEPROM.
- **`Hardware.h` & `Hardware.cpp`**: Modul *hardware abstraction* yang beroperasi langsung pada register AVR. Menangani INT0 *Zero-Cross interrupt*, pemrosesan *gate pulse* TRIAC Timer2, Hardware SPI, PWM Timer1, SysTick Timer0, serta *bare-metal EEPROM read/write*.
- **`UI.h` & `UI.cpp`**: Bare-metal TWI/I2C LCD *driver*, *custom string formatting*, *PROGMEM text rendering*, dan navigasi menu berbasis *state-machine*.
- **`Makefile`**: Skrip *build automation* yang menargetkan `avr-gcc`, `avr-objcopy`, dan `avrdude` dengan output direktori `build/` yang di-generate otomatis.

---

## Build and Flashing Instructions

### Prerequisites

Install AVR *toolchain* di sistem Linux Anda (misalnya, Fedora):

```bash
sudo dnf install avr-gcc avr-gcc-c++ avr-libc avrdude make
```

### Commands

- **Build firmware**:
  ```bash
  make
  ```
  *Mengompilasi source C++ menjadi `build/firmware.hex` dan menampilkan penggunaan memori.*

- **Flash ke ATmega328P (Arduino Board)**:
  ```bash
  make flash
  ```

- **Menentukan custom serial port (misalnya, `/dev/ttyACM0`)**:
  ```bash
  make flash PORT=/dev/ttyACM0
  ```

- **Clean build artifacts**:
  ```bash
  make clean
  ```

---

## Key Features

1. **Bare-Metal Performance**: Drastis mengurangi *flash binary footprint* (~19.6%) dan penggunaan SRAM (~6.2%) dengan *response time* yang instan.
2. **Handle State Detection**: Memantau *handle switch* (PB0 / D8) untuk bertransisi secara instan antara pemanasan aktif (`HEAT`) dan *auto-cooldown* (`SLEEP`).
3. **Automated Cooldown Safety**: Instan memutus daya TRIAC saat *sleep*, memaksa PWM *blower fan* ke 100% hingga suhu *nozzle* turun di bawah 100°C.
4. **Zero-Cross TRIAC Phase Control**: AC *zero-crossing* yang tersinkronisasi via *external interrupt* INT0 (PD2) digabungkan dengan Timer2 CTC untuk kontrol pemanasan *phase-angle delay*.
5. **Non-Volatile Configuration**: Automatic *bare-metal EEPROM auto-save* saat mengonfirmasi pengaturan suhu atau *fan* baru.
6. **Strict Hardware Failsafe**: *Health check* berkelanjutan pada status *open-thermocouple* MAX6675 dan presensi pulsa sinkronisasi AC *zero-cross*. Langsung masuk ke mode `MALFUNCTION` jika terjadi *fault*.

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

Tidak ada. Firmware ini 100% *self-contained* dan beroperasi secara murni pada *header* bawaan `<avr/io.h>`, `<avr/interrupt.h>`, dan `<avr/pgmspace.h>` yang disertakan dalam AVR-GCC *toolchain*.

---

## Changelog / Recent Updates (v2.0 Bare-Metal Refactor)

- **Standalone AVR-GCC Migration**: Menghapus *Arduino core runtime* dan mengganti `firmware.ino` dengan *entry point* standar `main.cpp` serta `Makefile` yang terstruktur.
- **Direct Register Driver Implementation**: Mengimplementasikan *native bare-metal TWI driver*, Hardware SPI untuk MAX6675, Fast PWM Timer1 untuk *fan speed*, Timer0 SysTick, dan *register-level EEPROM handler*.
- **SRAM & Flash Optimization**: Memindahkan *UI strings* ke `PROGMEM` dan mengganti `snprintf` dengan *lightweight integer formatting*, menekan konsumsi memori hingga 6.4 KB Flash dan 128 Bytes RAM.
- **EEPROM Auto-Save**: Mengintegrasikan *bare-metal EEPROM word reading/writing* untuk mempertahankan *user setpoints* (`targetTempC` dan `fanPercent`) saat *reboot*.
