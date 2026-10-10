#ifndef PROGRAM_STORE_H
#define PROGRAM_STORE_H

#include "lamp_types.h"

// ============================================================
// Biblioteka programów: 24 sloty × 1 KB w FRAM, katalogiem są nagłówki slotów
// (docs/FRAM_MAP.md). Programy niezmienne: zapis = nowy id, kasowanie = magic 0
// + tombstone. Aktywny program trzymany w RAM w całości.
// ============================================================

struct CatalogEntry {
    uint64_t id;
    uint64_t parent_id;
    uint32_t created_ts;
    uint32_t payload_crc;
    uint8_t  slot;
    uint8_t  flags;
    char     name[PROGRAM_NAME_LEN];
};

enum ProgramError : uint8_t {
    PROG_OK = 0,
    PROG_ERR_INVALID,       // krzywa nie przechodzi walidacji
    PROG_ERR_FULL,          // brak wolnego slotu
    PROG_ERR_NOT_FOUND,
    PROG_ERR_ACTIVE,        // nie można skasować aktywnego programu
    PROG_ERR_FACTORY,       // nie można skasować programu fabrycznego
    PROG_ERR_STORAGE,       // błąd FRAM
};

void initProgramStore(bool blankFram);

uint8_t             catalogCount();
const CatalogEntry* catalogEntry(uint8_t i);
const CatalogEntry* findProgram(uint64_t id);

const Program& activeProgram();
ProgramError   activateProgram(uint64_t id);     // wczytuje z FRAM, zapisuje SYSTEM_STATE
bool           loadProgram(uint64_t id, Program& out);

// Zapisuje p jako NOWY program (nadaje id, created_ts i seq z licznika programs_created).
// Pusta p.name → nazwa „Program NNNN” (seq 1..9999 cyklicznie). Zwraca id w newId.
ProgramError saveNewProgram(Program& p, uint64_t& newId);
ProgramError deleteProgram(uint64_t id);
// Zmiana nazwy przy niezmiennych programach: kopia pod nowym id (parent_id = stary), przełączenie
// aktywnego, potem kasowanie starego (tombstone). Zanik zasilania w trakcie zostawia najwyżej oba.
ProgramError renameProgram(uint64_t id, const char* name, uint64_t& newId);

// t[0]=0, t[n−1]=1440, t ściśle rosnące (≥ 1 min = MERGE_MIN), v ≤ 10000, 2 ≤ n ≤ 48
bool validateCurve(const ChannelCurve& c);

// v (setne %) krzywej w chwili minuteOfDay (ułamkowe minuty), interpolacja liniowa
float evalCurve(const ChannelCurve& c, float minuteOfDay);

const char* programErrorStr(ProgramError e);

// ===============================
// Synchronizacja ESP-NOW (network/espnow_sync)
// ===============================

// Program osierocony: aktywny, ale skasowany na innej lampie (tombstone). Zostaje w slocie
// do przełączenia na inny program — restart dobowy nie zmienia programu.
bool activeIsOrphan();

// Id programów w bibliotece bez osieroconego (to, co lampa udostępnia innym)
uint8_t  liveProgramIds(uint64_t* out, uint8_t max);
// CRC32 posortowanych liveProgramIds() — skrót katalogu w heartbeacie
uint32_t catalogHash();

#define PROGRAM_BLOB_MAX 1024   // nagłówek 64 B + payload ≤ 772 B
// Surowy program do transferu: nagłówek slotu (z magic) + payload
bool readProgramBlob(uint64_t id, uint8_t* buf, uint16_t& len);
// Program z innej lampy: weryfikacja (magic, wersja, oba CRC, krzywe) i zapis do wolnego
// slotu z zachowaniem id, parent_id, created_ts, nazwy i seq. Już obecny → PROG_OK.
ProgramError importProgramBlob(const uint8_t* buf, uint16_t len);
// Tombstone z innej lampy: kasuje program z biblioteki, aktywny zostaje jako osierocony.
// Zwraca true, gdy coś się zmieniło.
bool applyRemoteTombstone(uint64_t id);
// Osierocony aktywny po zmianie nazwy na innej lampie: jeśli w bibliotece jest program
// z parent_id = aktywny i tym samym payloadem — przełączenie bez rampy (krzywe te same).
bool adoptRenamedOrphan();

#endif
