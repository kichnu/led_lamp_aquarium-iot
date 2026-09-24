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
    uint32_t created_ts;
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
    PROG_ERR_STORAGE,       // błąd FRAM
};

void initProgramStore(bool blankFram);

uint8_t             catalogCount();
const CatalogEntry* catalogEntry(uint8_t i);
const CatalogEntry* findProgram(uint64_t id);

const Program& activeProgram();
ProgramError   activateProgram(uint64_t id);     // wczytuje z FRAM, zapisuje SYSTEM_STATE
bool           loadProgram(uint64_t id, Program& out);

// Zapisuje p jako NOWY program (nadaje id i created_ts). Zwraca id w newId.
ProgramError saveNewProgram(Program& p, uint64_t& newId);
ProgramError deleteProgram(uint64_t id);

// t[0]=0, t[n−1]=1440, t ściśle rosnące (≥ 1 min = MERGE_MIN), v ≤ 10000, 2 ≤ n ≤ 48
bool validateCurve(const ChannelCurve& c);

// v (setne %) krzywej w chwili minuteOfDay (ułamkowe minuty), interpolacja liniowa
float evalCurve(const ChannelCurve& c, float minuteOfDay);

const char* programErrorStr(ProgramError e);

#endif
