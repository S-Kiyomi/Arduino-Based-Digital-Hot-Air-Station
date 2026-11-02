# Shinonome-VORSA Open-Source 858D Digital Hot-Air Rework Station (v2.0)

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://choosealicense.com/licenses/mit/)
[![Platform: ATmega328P](https://img.shields.io/badge/MCU-ATmega328P%20%2F%20Arduino%20Uno-blue.svg)](https://www.microchip.com/)
[![Architecture: AVR Bare-Metal](https://img.shields.io/badge/Architecture-Pure%20Bare--Metal-red.svg)]()

> *Baca dalam bahasa lain: [English](../../README.md)*

Solder uap atau *hot air rework station* digital berbasis mikrokontroler **ATmega328P (Arduino Uno R3)** ini adalah proyek rakitan/DIY sumber terbuka (*open-source*) yang dirancang untuk memberikan kontrol suhu dan aliran udara presisi tinggi. Proyek ini menggunakan gagang blower 858D standar, sensor Thermocouple Type-K (MAX6675), serta *rotary encoder* untuk antarmuka pengguna yang intuitif.

Pada **v2.0**, *firmware* telah dirombak total dari ekosistem bawaan Arduino core ke **Pure AVR Bare-Metal C/C++** untuk menghasilkan eksekusi bebas *overhead*, waktu respon kilat, dan penggunaan memori yang sangat hemat.

> **PERINGATAN KESELAMATAN TEGANGAN TINGGI (220V AC)**
> 
> Proyek ini berurusan langsung dengan tegangan listrik AC 220V/230V PLN yang mematikan. Kesalahan perakitan dapat menyebabkan kerusakan perangkat keras, kebakaran, atau kematian akibat tersengat listrik.
> - Jangan pernah menyentuh area sirkuit TRIAC/Heater saat sistem terhubung ke sumber listrik.
> - Pastikan sirkuit tegangan tinggi terisolasi secara fisik dan optis (menggunakan optocoupler) dari jalur mikrokontroler.
> - Lakukan perakitan dengan risiko dan tanggung jawab Anda sendiri.

---

## Fitur Utama

* **Pure Bare-Metal Firmware (v2.0):** Berjalan tanpa dependensi pustaka Arduino (`Wire.h`, `LiquidCrystal_I2C.h`, `EEPROM.h`, `pinMode`, `digitalWrite`). Menggunakan pengompilasi `avr-gcc` murni via `Makefile`.
* **Penghematan Memori Ekstrem:** Memakan memori sangat minim (~6.4 KB Flash & 128 Byte SRAM). Teks UI disimpan di Flash ROM (`PROGMEM`) dan menggunakan pemformat angka kustom.
* **Penyimpanan EEPROM Non-Volatile:** Pengaturan target suhu (`targetTempC`) dan kecepatan kipas (`fanPercent`) disimpan secara otomatis ke EEPROM saat dikonfirmasi via menu UI.
* **Sistem PID & Kontrol Phase-Angle TRIAC:** Memanaskan elemen secara halus dan mempertahankan suhu dengan presisi menggunakan deteksi *Zero-Crossing* (INT0) dan pewaktuan interupsi Timer2 CTC.
* **Auto-Sleep & Pendinginan Otomatis (Cooldown):** Saat gagang diletakkan di holder, pemanas langsung diputus, namun kipas ditiupkan penuh (PWM 100%) hingga suhu nozzle di bawah 100°C untuk mencegah gagang meleleh.
* **Sistem Failsafe & Deteksi Malfungsi:** Menghentikan pemanasan dan membunyikan alaram *MALFUNCTION* secara instan jika termokopel tercabut/rusak (open-circuit) atau sinyal sinkronisasi AC *Zero-Cross* hilang.
* **Manajemen Daya Layar:** Lampu latar (*backlight*) LCD otomatis mati setelah 60 detik tanpa aktivitas.

---

## Component Requirements & Pin Mapping

| Komponen | Pin Arduino | Port/Pin AVR | Tipe / Fungsi Register |
| :--- | :--- | :--- | :--- |
| Zero-Cross Signal | D2 | PD2 | External Interrupt 0 (INT0) |
| Encoder SW | D3 | PD3 | Digital Input Pull-up |
| Encoder DT | D4 | PD4 | Digital Input Pull-up |
| Encoder CLK | D5 | PD5 | Digital Input Pull-up |
| Buzzer | D6 | PD6 | Digital Output |
| TRIAC Gate | D7 | PD7 | Digital Output (Timer2 Phase Delay) |
| Handle Switch | D8 | PB0 | Digital Input Pull-up (Sleep/Work) |
| Kipas Blower PWM | D9 | PB1 | Output Hardware Fast PWM Timer1 (OC1A) |
| MAX6675 CS | D10 | PB2 | SPI Slave Select (Output) |
| MAX6675 SO | D12 | PB4 | SPI MISO Input |
| MAX6675 SCK | D13 | PB5 | SPI Serial Clock Output |
| LCD 16x2 I2C | A4 / A5 | PC4 / PC5 | Hardware TWI (SDA / SCL) |

---

## Documentation & Firmware Build Guide

Untuk detail arsitektur bare-metal, konfigurasi register ATmega328P, serta panduan pengompilan (`make`) dan flashing (`avrdude`), silakan baca dokumentasi teknis di:

[Dokumentasi Firmware & Panduan Kompilasi](firmware/README.md)

---

## Lisensi

Proyek ini dirilis di bawah lisensi MIT. Anda bebas menggunakan, memodifikasi, dan mendistribusikan proyek ini baik untuk tujuan non-komersial maupun komersial.

Copyright (c) 2026 Rul Chan & Averyandra. All Rights Reserved.