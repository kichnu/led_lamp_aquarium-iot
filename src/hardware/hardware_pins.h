#ifndef HARDWARE_PINS_H
#define HARDWARE_PINS_H

// ============================================================
// SEEED XIAO ESP32-C3 — RL90 Lamp Pin Mapping (docs/RL90_HANDOFF.md §11)
// ============================================================
//
//                       ┌──── USB-C ────┐
//  1-Wire DS18B20  D0  GPIO2   ┤        ├ 5V
//  PWM A           D1  GPIO3   ┤        ├ GND
//  PWM B           D2  GPIO4   ┤        ├ 3V3
//  PWM C           D3  GPIO5   ┤        ├ D10 GPIO10  przycisk PROV (do GND)
//  PWM D           D4  GPIO6   ┤        ├ D9  GPIO9   wolny (BOOT, strapping)
//  PWM FAN         D5  GPIO7   ┤        ├ D8  GPIO8   wolny (strapping)
//  I2C SDA         D6  GPIO21  ┤        ├ D7  GPIO20  I2C SCL
//                       └───────────────┘
//
// A–D i FAN: bezpośrednio z GPIO przez 1 kΩ do padów lampy (wejście PWM Hi7001).
// Wejście Hi7001 wiszące = 100 % (wewnętrzny pull-up) — ~1 s błysku przy starcie
// akceptowany, bez pull-downów (USTALENIA.md).
// GPIO2 = strapping (musi być 1 przy starcie) — tylko pull-up, nigdy pull-down.
// ============================================================

// ============== PWM LED (Hi7001) ==============
#define PWM_A_PIN           3
#define PWM_B_PIN           4
#define PWM_C_PIN           5
#define PWM_D_PIN           6

// ============== FAN ==============
#define FAN_PWM_PIN         7

// ============== I2C — DS3231 (0x68) + FRAM (0x50) ==============
#define I2C_SDA_PIN         21
#define I2C_SCL_PIN         20
#define I2C_CLOCK_HZ        400000
#define FRAM_I2C_ADDR       0x50
#define RTC_I2C_ADDR        0x68

// ============== REZERWA ==============
#define DS18B20_PIN         2    // czujnik radiatora (przyszły failsafe), nieużywany

// ============== SYSTEM ==============
#define RESET_PIN           10   // INPUT_PULLUP, aktywny LOW, 5 s przy starcie → provisioning

#endif // HARDWARE_PINS_H
