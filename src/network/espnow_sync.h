#ifndef ESPNOW_SYNC_H
#define ESPNOW_SYNC_H

#include <Arduino.h>

// ============================================================
// ESP-NOW między lampami (docs/ESPNOW_PLAN.md, RL90_HANDOFF.md §9.3–9.4).
// Bez mastera: heartbeat broadcastem co 15 s (i zaraz po zmianie katalogu/aktywnego),
// różny skrót katalogu → wymiana list id + tombstone → każda lampa sama pobiera brakujące
// programy porcjami. Czas: lampa bez świeżego NTP bierze UTC od lampy, która go ma.
// Ramki: HMAC-SHA256 (16 B) z kluczem z hasła Wi-Fi + licznik przeciw powtórkom.
// Kanał = kanał połączenia Wi-Fi (jeden AP), modem sleep wyłączony (inaczej gubi ramki).
// Callback odbioru tylko kolejkuje; przetwarzanie w updateEspNow() z loop() pod LampLock.
// ============================================================

#define ESPNOW_MAX_PEERS 6

struct PeerInfo {
    uint8_t  mac[6];
    char     name[32];
    uint32_t ip;
    bool     online;            // heartbeat w ostatnich 45 s
    uint32_t last_seen_s;
    uint64_t active_id;
    char     active_name[24];
    bool     active_orphan;
    uint8_t  mode;              // LampMode
    uint8_t  time_quality;      // 2 NTP, 1 RTC, 0 brak
    bool     in_sync;           // ten sam skrót katalogu
    uint8_t  prog_count;
    char     fw[12];
};

struct EspNowStatus {
    bool     enabled;           // klucz grupy wyliczony, esp_now_init OK
    uint8_t  mac[6];
    uint8_t  channel;
    uint32_t catalog_hash;      // skrót własnego katalogu — GUI przeładowuje listę po zmianie
    uint8_t  fetch_queue;       // programy do pobrania
    uint64_t fetching_id;       // 0 = brak transferu
    uint64_t pending_activate;  // „ustaw na wszystkich” czeka na pobranie programu
    uint32_t rx_ok, rx_bad;     // ramki przyjęte / odrzucone (HMAC, powtórka, format)
    uint32_t tx_fail;
};

void initEspNow();              // po initWiFi() (WiFi.mode(STA))
void updateEspNow();            // w loop()

uint8_t espnowPeerCount();
bool    espnowPeer(uint8_t i, PeerInfo& out);
void    espnowStatus(EspNowStatus& out);

// Lampa online z tym programem jako aktywnym (name może być nullptr)
bool espnowActiveOnPeer(uint64_t id, char* name, size_t nameLen);

// „Ustaw na wszystkich”: polecenie do lamp online (z potwierdzeniem i ponowieniem).
// Lokalną aktywację robi wywołujący. Zwraca liczbę lamp, do których poszło polecenie.
uint8_t espnowActivateAll(uint64_t id);

#endif
