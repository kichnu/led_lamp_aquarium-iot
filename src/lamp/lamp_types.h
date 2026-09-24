#ifndef LAMP_TYPES_H
#define LAMP_TYPES_H

#include <Arduino.h>

// ============================================================
// Stałe i struktury lampy — zapisywane w FRAM (docs/FRAM_MAP.md).
// Wszystkie structy FRAM: #pragma pack(1), magic + CRC32, stały rozmiar
// sprawdzany static_assert (fram_controller.cpp).
// ============================================================

#define NUM_CHANNELS        4           // A–D
#define DAY_MIN             1440
#define MAX_POINTS          48          // na kanał, z punktami krańcowymi t=0 i t=1440
#define V_MAX               10000       // v w setnych % (100,00 %)
#define PROGRAM_NAME_LEN    24          // z '\0'

#define LED_PWM_FREQ_HZ     500
#define FAN_PWM_FREQ_HZ     500
#define PWM_RES_BITS        14
#define PWM_MAX_DUTY        (1u << PWM_RES_BITS)   // 16384 = 100 %

#define RAMP_S_DEFAULT      10
#define RAMP_S_MIN          3
#define RAMP_S_MAX          30
#define FAN_ON_PCT_DEFAULT  20
#define FAN_OFF_PCT_DEFAULT 18
#define FAN_UPDATE_MS       30000

// Program fabryczny — stały id, identyczny na każdej lampie (ESP-NOW, tombstone)
#define FACTORY_PROGRAM_ID  0x524C393046414354ULL  // ASCII "RL90FACT"

#pragma pack(push, 1)

struct FramHeader {             // 32 B
    uint32_t magic;
    uint16_t layout_version;
    uint16_t _pad;
    uint32_t init_ts;
    uint8_t  _reserved[16];
    uint32_t crc32;
};

struct SystemState {            // 64 B
    uint32_t magic;
    uint64_t active_program_id;   // 0 = brak
    uint32_t last_restart_day;    // dzień lokalny (dni od epoki) ostatniego restartu planowego
    uint16_t cnt_wdt;             // liczniki resetów wg esp_reset_reason()
    uint16_t cnt_panic;
    uint16_t cnt_brownout;
    uint16_t cnt_other;
    uint8_t  last_reset_reason;
    uint8_t  _reserved[35];
    uint32_t crc32;
};

struct LampConfig {             // 128 B
    uint32_t magic;
    uint16_t ramp_s;              // wspólna rampa, 3–30 s
    uint8_t  fan_on_pct;          // start wentylatora przy P ≥ (domyślnie 20)
    uint8_t  fan_off_pct;         // stop przy P < (domyślnie 18)
    uint16_t night_preset[NUM_CHANNELS];  // setne %, start suwaków trybu nocnego
    uint8_t  _reserved[108];
    uint32_t crc32;
};

struct ChannelConfig {          // 32 B
    uint32_t magic;
    float    gamma;               // 1.0 — v = % mocy; γ tylko korekta nieliniowości drivera
    uint16_t min_duty;            // 0–16384
    uint16_t power_frac;          // ‱ mocy lampy przy kanale solo (nieaddytywne)
    char     label[12];
    uint8_t  _reserved[4];
    uint32_t crc32;
};

struct TombstoneMeta {          // 16 B, za nim uint64_t ids[126]
    uint32_t magic;
    uint16_t count;
    uint16_t wptr;
    uint8_t  _reserved[4];
    uint32_t crc32;
};

struct ProgramHeader {          // 64 B, początek slotu
    uint32_t magic;               // zapisywany OSTATNI; 0 = slot wolny/skasowany
    uint8_t  format_version;
    uint8_t  flags;               // bit0: program fabryczny
    uint16_t payload_len;
    uint64_t program_id;
    uint64_t parent_id;
    uint32_t created_ts;          // UTC
    uint32_t payload_crc32;
    char     name[PROGRAM_NAME_LEN];
    uint8_t  _reserved[4];
    uint32_t header_crc32;        // CRC pól po magic, bez header_crc32
};

#pragma pack(pop)

#define PROGRAM_FLAG_FACTORY 0x01

// Punkt krzywej: t w minutach (0–1440), v w setnych %
struct CurvePoint {
    uint16_t t;
    uint16_t v;
};

struct ChannelCurve {
    uint8_t    count;
    CurvePoint pts[MAX_POINTS];
};

// Program w RAM (pełny — 4 krzywe)
struct Program {
    uint64_t     id;
    uint64_t     parent_id;
    uint32_t     created_ts;
    uint8_t      flags;
    char         name[PROGRAM_NAME_LEN];
    ChannelCurve ch[NUM_CHANNELS];
};

extern const char CHANNEL_NAMES[NUM_CHANNELS];

#endif // LAMP_TYPES_H
