# Open-Source DIY Hot-Air Rework Station (858D)

Solder uap atau *hot air rework station* digital berbasis Arduino Uno R3 ini adalah proyek rakitan/DIY sumber terbuka (open-source) yang dirancang untuk memberikan kontrol suhu dan aliran udara yang presisi. Proyek ini menggunakan gagang blower 858D standar, sensor Thermocouple Type-K (MAX6675), serta *rotary encoder* untuk antarmuka pengguna yang intuitif.

### WARNING!!
**Proyek ini berurusan dengan tegangan listrik tinggi (220V/230V AC).**
Kesalahan dalam perakitan dapat menyebabkan kerusakan perangkat, kebakaran, atau **kematian akibat tersengat listrik**. Jangan pernah menyentuh area sirkuit TRIAC/Heater saat sistem terhubung ke sumber listrik PLN. Pastikan sirkuit tegangan tinggi diisolasi secara fisik dan logis dari mikrokontroler. **Lakukan risiko perakitan dengan tanggung jawab Anda sendiri.**

---

## Fitur Utama

* **Sistem PID & Triac Control:** Memanaskan elemen secara halus dan mempertahankan target suhu dengan presisi menggunakan deteksi *Zero-Crossing*.
* **Auto-Sleep & Pendinginan Otomatis (Cooldown):** Menggunakan *tilt / magnetic sensor* internal pada gagang 858D. Saat gagang diarahkan ke atas / disimpan ke holder, pemanas otomatis mati, namun kipas tetap berputar penuh hingga suhu turun ke level aman (< 100°C) untuk mencegah gagang meleleh.
* **Antarmuka Rotary Encoder Dinamis:** Putar pelan untuk menyesuaikan nilai secara presisi (1 digit), atau putar cepat untuk melompat dalam kelipatan besar (5 atau 10 digit).
* **Sistem Failsafe & Deteksi Malfungsi:** Otomatis memutus arus pemanas dan membunyikan alarm jika sensor termokopel tercabut, rusak, atau suhu membaca nilai tidak wajar.
* **Manajemen Daya Layar:** Lampu latar (*backlight*) LCD otomatis mati setelah 60 detik tanpa aktivitas, dan menyala kembali hanya dengan menekan *knob*.

---

## Component Requirements

Untuk merakit stasiun solder uap ini, Anda membutuhkan komponen berikut:
1. **Mikrokontroler:** Arduino Uno R3
2. **Gagang Blower:** 858D Hot Air Handle (Versi dengan *Tilt Sensor* / Bola Gravitasi, atau *Reed Switch* magnetik).
3. **Sensor Suhu:** Modul MAX6675 (Thermocouple Type-K Amplifier).
4. **Layar:** LCD 16x2 dengan Modul I2C.
5. **Input:** Modul Rotary Encoder (KY-040).
6. **Sirkuit Pemanas (AC):** TRIAC (misal: BTA16) & Optocoupler dengan deteksi *Zero-Cross* (misal: MOC3041/MOC3021).
7. **Sirkuit Kipas (DC):** MOSFET N-Channel (misal: IRLZ44N) dan *Power Supply* 24V DC untuk kipas internal gagang.
8. **Lainnya:** Active Buzzer (5V) dan modul *Zero-Crossing Detector* AC ke DC.

---

## Schematics & Pinout

Berikut adalah pemetaan pin (*pinout*) berdasarkan konfigurasi *firmware* bawaan:

| Komponen | Pin / Koneksi ke Arduino | Keterangan |
| :--- | :--- | :--- |
| **Buzzer** | `D6` | Output (Active Buzzer) |
| **TRIAC (Heater)** | `D7` | Output (via Optocoupler) |
| **Tilt / Magnetic Sensor** | `D8` | Input Pullup (Deteksi kemiringan gagang) |
| **Kipas Blower** | `D9` | Output PWM (via MOSFET ke kipas 24V) |
| **Encoder SW** | `D3` | Tombol (Input Pullup) |
| **Encoder DT** | `D4` | Input Pullup |
| **Encoder CLK** | `D5` | Input Pullup |
| **MAX6675 CS** | `D10` | Chip Select |
| **MAX6675 SO** | `D11` | MISO / Data Out |
| **MAX6675 SCK**| `D13` | Serial Clock |
| **Zero Cross** | `D2` | Interrupt Pin (Deteksi fase AC) |
| **LCD 16x2** | `SDA / SCL` | Alamat default `0x27` (I2C) |

*(Catatan: Skema visual sirkuit lengkap akan segera diunggah ke repositori ini).*

---

## Cara Kerja Rangkaian

1. **Pembacaan Suhu:** Modul MAX6675 membaca tegangan sangat kecil dari termokopel di dalam gagang 858D, mengubahnya menjadi data digital, dan mengirimkannya ke Arduino via protokol SPI.
2. **Kontrol Daya Pemanas:** Untuk mengatur panas elemen 220V, Arduino menunggu sinyal *Interrupt* dari pin *Zero-Cross* (saat gelombang AC berada di titik nol volt). Berdasarkan kalkulasi PID, Timer internal Arduino akan menunda tembakan pulsa ke gerbang TRIAC (*phase-angle control*). Semakin cepat TRIAC dipicu setelah titik nol, semakin panas elemen tersebut].
3. **Logika Orientasi Gagang:** Ketika gagang diletakkan mendongak ke atas, *tilt / magnetic sensor* internal memutus sirkuit (terbaca *HIGH* oleh pin *Pull-Up* Arduino). Sistem langsung memotong sinyal gerbang TRIAC ke pemanas, lalu memerintahkan MOSFET untuk menyalakan kipas 24V pada siklus kerja 100% (*PWM 255*) hingga MAX6675 membaca suhu aman di bawah 100°C.

---

## License

Proyek ini dirilis di bawah lisensi [MIT](https://choosealicense.com/licenses/mit/). 
Anda bebas menggunakan, memodifikasi, dan mendistribusikan proyek ini baik untuk tujuan komersial maupun non-komersial.

Copyright (c) 2026 Rul Chan & Averyandra. All Rights Reserved.
