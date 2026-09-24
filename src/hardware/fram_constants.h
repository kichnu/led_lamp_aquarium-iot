#ifndef FRAM_CONSTANTS_H
#define FRAM_CONSTANTS_H

// ============================================================
// FRAM CONSTANTS — RL90 Lamp (docs/FRAM_MAP.md)
// FM24W256 / MB85RC256V, I2C 0x50, 32 KB (0x0000–0x7FFF)
// ============================================================

#define FRAM_SIZE               0x8000

#define FRAM_MAGIC_NUMBER       0x30394C52  // "RL90" (little-endian) — nagłówek i poświadczenia
#define FRAM_LAYOUT_VERSION     0x0001      // tylko obszar systemowy 0x0000–0x1FFF

// ============================================================
// OBSZAR SYSTEMOWY (0x0000–0x1FFF)
// ============================================================
#define FRAM_ADDR_HEADER        0x0000      // 32 B
#define FRAM_CREDENTIALS_ADDR   0x0020      // 1024 B — AES-256 (moduł crypto bez zmian)
#define FRAM_CREDENTIALS_SIZE   1024
#define FRAM_ADDR_SYSTEM_STATE  0x0420      // 64 B
#define FRAM_ADDR_LAMP_CONFIG   0x0460      // 128 B
#define FRAM_ADDR_CHANNEL_CFG   0x04E0      // 4 × 32 B
#define FRAM_ADDR_LOCK_PIN      0x0560      // 16 B — rezerwacja
#define FRAM_ADDR_TOMBSTONES    0x0800      // 16 B meta + 126 × 8 B
#define FRAM_TOMBSTONE_CAPACITY 126
#define FRAM_SYSTEM_END         0x2000

// ============================================================
// SLOTY PROGRAMÓW (0x2000–0x7FFF)
// ============================================================
#define FRAM_ADDR_PROGRAMS      0x2000
#define FRAM_PROGRAM_SLOT_SIZE  0x0400      // 1 KB
#define FRAM_PROGRAM_SLOTS      24

// Magic sekcji (osobny od CRC — odróżnia pustą/zerową FRAM od poprawnego zapisu)
#define SYSTEM_STATE_MAGIC      0x53595331  // "SYS1"
#define LAMP_CONFIG_MAGIC       0x4C43464D  // "LCFM"
#define CHANNEL_CONFIG_MAGIC    0x43484346  // "CHCF"
#define TOMBSTONE_MAGIC         0x544F4D42  // "TOMB"
#define PROGRAM_MAGIC           0x50524731  // "PRG1"
#define PROGRAM_FORMAT_VERSION  1

// ============================================================
// ENCRYPTION (credentials — bez zmian z ATO poza ziarnem projektu)
// ============================================================
#define ENCRYPTION_SALT     "ESP32_FRAM_SALT_2024"
#define ENCRYPTION_SEED     "RL90_LAMP_SEED_V1"

// ============================================================
// FIELD SIZES (credentials — bez zmian z ATO)
// ============================================================
#define MAX_DEVICE_NAME_LEN     31
#define MAX_WIFI_SSID_LEN       63
#define MAX_WIFI_PASSWORD_LEN   127
#define MAX_VPS_TOKEN_LEN       255
#define MAX_VPS_URL_LEN         100

#endif // FRAM_CONSTANTS_H
