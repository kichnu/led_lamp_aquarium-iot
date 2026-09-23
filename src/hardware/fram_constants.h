#ifndef FRAM_CONSTANTS_H
#define FRAM_CONSTANTS_H

// ============================================================
// SHARED FRAM CONSTANTS — Thermo Control
// MB85RS64V, SPI (CS = FRAM_SPI_CS_PIN), 8KB (0x0000–0x1FFF)
// ============================================================

#define FRAM_MAGIC_NUMBER       0x54484D4F  // "THMO" in hex
#define FRAM_DATA_VERSION       0x0001

// ============================================================
// MEMORY LAYOUT
// ============================================================

// 0x0000–0x0017: Header (24B)
#define FRAM_ADDR_MAGIC         0x0000      // 4B
#define FRAM_ADDR_VERSION       0x0004      // 2B
// 0x0006–0x0017: reserved

// 0x0018–0x0417: Encrypted credentials (1024B) — bez zmian ze struktury ATO
#define FRAM_CREDENTIALS_ADDR   0x0018
#define FRAM_CREDENTIALS_SIZE   1024

// 0x0500–0x05FF: System config + metadane 3 ringów
// Uwaga: ThermoConfig realnie 48B (10 float+2×u16+2×u8, padded), EnergyStore
// realnie 12B (checksum wbudowany w struct) — static_assert w algorithm_config.h.
// (2026-07-14: ThermoConfig zyskało trend_window_min, 40B→44B. 2026-07-24:
// zyskało thermal_buffer, 44B→48B — adresy poniżej przesunięte, stary zapis
// odrzuci się przez checksum mismatch — patrz [[fram_magic_byte_gotcha]]: to
// też kasuje EnergyStore/hourly/daily/alarm meta do zera, bo ich adresy też
// się przesuwają; ring bufferów DANE zostają nietknięte, tylko ich metadane
// count/wptr się resetują. Żeby NIE powtarzać tego przy każdej kolejnej zmianie
// ThermoConfig: od 2026-07-24 struct dostaje stały slot 0x40 (64B) zamiast być
// pakowany "na styk" — 48B+2B checksum = 50B użyte, 14B rezerwy (~28%) na
// przyszłe pola BEZ przesuwania EnergyStore/ringów. Analogicznie zostaje spory
// margines (162B, ~63% tego bloku) między metadanymi a FRAM_ADDR_HOURLY_BUFFER.)
#define FRAM_ADDR_THERMO_CONFIG     0x0500  // ThermoConfig (48B, w slocie 64B)
#define FRAM_ADDR_THERMO_CFG_CHKSUM 0x0530  // 2B checksum
// 0x0532–0x053F: rezerwa (14B) na przyszły wzrost ThermoConfig
#define FRAM_ADDR_ENERGY_STORE      0x0540  // EnergyStore (12B, checksum wbudowany)

// Metadane ring bufferów (count/wptr/checksum, 6B każdy — wzorzec jak w
// pierwotnym ring bufferze event-driven, patrz calculateChecksum(wptr+count))
#define FRAM_ADDR_HOURLY_COUNT      0x054C  // 2B
#define FRAM_ADDR_HOURLY_WPTR       0x054E  // 2B
#define FRAM_ADDR_HOURLY_META_CHKSUM 0x0550 // 2B
#define FRAM_ADDR_DAILY_COUNT       0x0552  // 2B
#define FRAM_ADDR_DAILY_WPTR        0x0554  // 2B
#define FRAM_ADDR_DAILY_META_CHKSUM 0x0556  // 2B
#define FRAM_ADDR_ALARM_COUNT       0x0558  // 2B
#define FRAM_ADDR_ALARM_WPTR        0x055A  // 2B
#define FRAM_ADDR_ALARM_META_CHKSUM 0x055C  // 2B
// 0x055E–0x05FF: gap (162B rezerwa, ~63% tego bloku) przed FRAM_ADDR_HOURLY_BUFFER

// 0x0600–0x06BF: Ring godzinowy — HOURLY_BUFFER_CAPACITY(24) × 8B = 192B
// rolling 24h, nadpisywany co godzinę (nawet niepełną — patrz pamięć projektu,
// restart o północy gwarantuje ≥1 niepełną godzinę dziennie). sizeof(TempAvgRecord)
// to logicznie 6B (4B+2B), ale kompilator dopełnia do 8B — przyjęte wprost.
#define FRAM_ADDR_HOURLY_BUFFER     0x0600
#define HOURLY_RECORD_SIZE          8       // sizeof(TempAvgRecord), padded

// 0x06C0–0x122F: Ring dobowy — DAILY_BUFFER_CAPACITY(366) × 8B = 2928B
// średnia z 24 rekordów godzinowych, zapisywana przy zmianie doby lokalnej
#define FRAM_ADDR_DAILY_BUFFER      0x06C0
#define DAILY_RECORD_SIZE           8       // sizeof(TempAvgRecord), padded

// 0x1230–0x1B8F: Ring zdarzeń alarmowych — ALARM_BUFFER_CAPACITY(300) × 8B = 2400B,
// jeden wpis na fakt wystąpienia alarmu (bez end/duration, patrz AlarmEvent)
#define FRAM_ADDR_ALARM_BUFFER      0x1230
#define ALARM_RECORD_SIZE           8       // sizeof(AlarmEvent)

// 0x1B90–0x1B9B: lock-PIN edycji GUI (2026-07-15) — patrz LockPin w
// algorithm_config.h. 0x1BA0–0x1CA1 zwolnione (2026-07-24, C-09 usunięte —
// zastąpione przez ThermoConfig.thermal_buffer), wraca do rezerwy.
#define FRAM_ADDR_LOCK_PIN           0x1B90  // LockPin (12B)

// 0x1CA2–0x1CA9: EnergyResetInfo (8B) — timestamp kasowania licznika total
// (2026-07-18), osobno od EnergyStore (0x052E) żeby nie przesuwać layoutu
#define FRAM_ADDR_ENERGY_RESET        0x1CA2  // EnergyResetInfo (8B)

// 0x1CAA–0x1FFF: wolne (~854B rezerwa)

// ============================================================
// ENCRYPTION (credentials — bez zmian z ATO)
// ============================================================
#define ENCRYPTION_SALT     "ESP32_FRAM_SALT_2024"
#define ENCRYPTION_SEED     "THERMO_SYSTEM_SEED_V1"

// ============================================================
// FIELD SIZES (credentials — bez zmian z ATO)
// ============================================================
#define MAX_DEVICE_NAME_LEN     31
#define MAX_WIFI_SSID_LEN       63
#define MAX_WIFI_PASSWORD_LEN   127
#define MAX_VPS_TOKEN_LEN       255
#define MAX_VPS_URL_LEN         100

// ============================================================
// THERMO CONFIG MAGIC
// ============================================================
#define THERMO_CONFIG_MAGIC     0xA9    // is_configured field value

#endif // FRAM_CONSTANTS_H
