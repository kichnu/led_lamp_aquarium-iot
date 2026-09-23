#ifndef HARDWARE_PINS_H
#define HARDWARE_PINS_H

// ============================================================
// WAVESHARE ESP32-S3-PICO — Thermo Control Pin Mapping
// Źródło: ESP32-S3 Datasheet v1.6 + docs/ESP32-S3-Pico pinout.md
// ============================================================
//
// Zmiana płytki: Seeed XIAO ESP32-S3 → Waveshare ESP32-S3-Pico
// Powód: SPI FRAM (8KB, MB85RS64V) potrzebuje 4 linii (SCK/MISO/MOSI/CS)
//        obok już zajętych I2C0 (RTC) + I2C1 (STS35) + aktuatorów —
//        XIAO ma za mało wyprowadzonych GPIO, Pico ma znacznie więcej.
//
// I2C0 (Wire,  GPIO5/6,  400kHz): DS3231 RTC (0x68) — kable <5cm
// I2C1 (Wire1, GPIO7/8,  100kHz): STS35 temp (0x4B) — kabel ~1m
// SPI  (FSPI/SPI2, domyślne IO_MUX ESP32-S3): MB85RS64V FRAM (8KB)
//
// UWAGI z datasheetu ESP32-S3 v1.6 (przeniesione z XIAO, nadal aktualne):
//   GPIO3  = strapping pin (JTAG source). INPUT_PULLUP → HIGH = normalny boot.
//            Jeśli przycisk trzymany PRZY starcie → LOW = JTAG via pins (niekrytyczne).
//   GPIO1,GPIO2,GPIO3,GPIO4: LOW glitch ~60µs przy power-on (Table 2-2).
//            SSR: 60µs LOW = 60µs grzania → pomijalny.
//            Fan: 60µs LOW = 0% PWM → fine.
//            Buzzer: 60µs LOW = cisza → brak fałszywego dźwięku.
//   GPIO9  = brak glitch przy boot → idealne dla ISR (energy pulse).
//   GPIO33-37 = octal PSRAM (płytka Pico) — NIE używać.
//   GPIO38    = safety cutoff (opis w pinout płytki) — zarezerwowane, NIE używać.
//   GPIO39-42 = JTAG (MTCK/MTDO/MTDI/MTMS) — unikać w produkcji.
//   GPIO0     = BOOT strapping — unikać.
// ============================================================

// ============================================================
// Waveshare ESP32-S3-Pico — Pin Assignment v2.0
// ============================================================
//                                                      ┌───────────────────   ──┐
//                                                      │       USB-C            │
//                       FRAM_SPI_MOSI_PIN  GPIO 11 ────┤                        ├──── VBUS (5V)               
//                         FRAM_SPI_SCK_PIN GPIO 12 ────┤                        ├──── VSYS (5V)
//                                            GND   ────┤                        ├──── GND
//                        FRAM_SPI_MISO_PIN GPIO 13 ────┤                        ├──── EN   (3V3_EN)
//                                          GPIO 14 ────┤                        ├──── 3V3  (OUT)
//                                          GPIO 15 ────┤                        ├──── GPIO 10 FRAM_SPI_CS_PIN                      
//                                          GPIO 16 ────┤                        ├──── GPIO  9 ENERGY_PULSE_PIN
//                                            GND   ────┤                        ├──── GND
//                          HEATER_SSR_PIN  GPIO 17 ────┤                        ├──── GPIO  8 STS35_SCL_PIN
//                             RESERVE_PIN  GPIO 18 ────┤                        ├──── GPIO  7 STS35_SDA_PIN
//                                          GPIO 33 ────┤                        ├──── RUN  
//                                          GPIO 34 ────┤                        ├──── GPIO  6 I2C_SCL_PIN
//                                            GND   ────┤                        ├──── GND
//                                          GPIO 35 ────┤                        ├──── GPIO  5 I2C_SDA_PIN
//                                          GPIO 36 ────┤                        ├──── GPIO  4  
//                                RESET_PIN GPIO 37 ────┤                        ├──── GPIO  2 FAN_PWM_PIN
//                                          GPIO 38 ────┤                        ├──── GPIO  1 BUZZER_PIN
//                                            GND   ────┤                        ├──── GND  
//                              JTAG(MTCK)  GPIO 39 ────┤                        ├──── GPIO 41  JTAG(MTDI)
//                              JTAG(MTDO)  GPIO 40 ────┤                        ├──── GPIO 42  JTAG(MTMS)
//                                                      └───────────────   ──────┘

// ============== ACTUATORS ==============
#define HEATER_SSR_PIN      17   // SSR grzałki (active LOW: LOW = grzeje)
#define FAN_PWM_PIN         2   // Iduino ST1168 MOSFET PWM (HIGH = max)

// ============== RESERVE ==============
// Fizycznie sąsiaduje z HEATER_SSR_PIN (17) na złączu płytki — pozostawiony
// jako floating input, dublował zbocza SSR (przesłuch pojemnościowy między
// sąsiednimi pinami). INPUT_PULLUP wymusza stan 1 = nieaktywny.
#define RESERVE_PIN         18

// ============== I2C0 (Wire) — DS3231 RTC — kable krótkie ====================
#define I2C_SDA_PIN         5
#define I2C_SCL_PIN         6

// ============== I2C1 (Wire1) — STS35 temp — kabel ~1m =======================
#define STS35_SDA_PIN       7
#define STS35_SCL_PIN       8
#define STS35_I2C_ADDR      0x4B  // ADDR pin → VDD; alternatywa: 0x4A (→ GND)

// ============== SPI (FSPI/SPI2) — MB85RS64V FRAM, 8KB, SPI ==================
// Domyślne piny IO_MUX dla SPI2 na ESP32-S3 — najszybsza ścieżka, bez GPIO matrix.
#define FRAM_SPI_CS_PIN     10
#define FRAM_SPI_MOSI_PIN   11
#define FRAM_SPI_SCK_PIN    12
#define FRAM_SPI_MISO_PIN   13

// ============== ENERGY METER ==============
#define ENERGY_PULSE_PIN    9   // INPUT_PULLUP, ISR FALLING, brak boot glitch

// ============== SYSTEM ==============
#define BUZZER_PIN          1   // HIGH = ON
#define RESET_PIN          37   // INPUT_PULLUP, active LOW, hold 5s → provisioning
                                // GPIO3 = strapping JTAG source (niekrytyczne)

// ============== WOLNE (zarezerwowane na przyszłość) ==========================
// GPIO14-16, GPIO21 — wolne po przypisaniu SPI FRAM (GPIO10-13)

#endif // HARDWARE_PINS_H
