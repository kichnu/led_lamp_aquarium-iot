#ifndef LAMP_STORAGE_H
#define LAMP_STORAGE_H

#include "lamp_types.h"

// ============================================================
// Obszar systemowy FRAM: nagłówek, SYSTEM_STATE, LAMP_CONFIG, CHANNEL_CONFIG,
// TOMBSTONES. Kopia w RAM, zapis przez save*(). Sekcja z błędnym magic/CRC
// dostaje wartości domyślne (lazy-init) — nie rusza pozostałych.
// ============================================================

// Zwraca true, gdy FRAM była pusta (brak nagłówka) — wtedy program_store
// instaluje program fabryczny. Zmiana wersji layoutu reinicjalizuje obszar
// systemowy poza CREDENTIALS, bez programu fabrycznego.
bool initLampStorage(bool& blankFram);

SystemState&   sysState();
LampConfig&    lampConfig();
ChannelConfig& channelConfig(uint8_t ch);

bool saveSystemState();
bool saveLampConfig();
bool saveChannelConfig(uint8_t ch);

void defaultLampConfig(LampConfig& c);
void defaultChannelConfig(uint8_t ch, ChannelConfig& c);

bool isTombstoned(uint64_t id);
bool addTombstone(uint64_t id);

#endif
