# RL90 — mapa FRAM

Stan: 2026-09-24, zaimplementowane (`src/lamp/lamp_types.h`, `lamp_storage.cpp`, `program_store.cpp` — nagłówki wygrywają z tym szkicem). Układ: FRAM I2C 32 KB (FM24W256 / MB85RC256V), adres 0x50,
adresowanie 2-bajtowe, 0x0000–0x7FFF. Kontekst: `RL90_HANDOFF.md` §9.3–9.4, §10; `CURVE_EDITOR_IMPLEMENTATION.md` §4.

---

## Wzorce przejęte z innych projektów

| Wzorzec | Źródło | Po co |
|---|---|---|
| Nagłówek 32 B (magic + wersja layoutu) na 0x0000, poświadczenia AES 1024 B zaraz za nim na 0x0020 | dosing_system (C3 + MB85RC256V) | ten sam układ i ta sama magistrala co tu; moduł `fram_encryption` bierze adres ze stałej |
| Stałe sloty z rezerwą zamiast pakowania „na styk” | thermo_control (`fram_constants.h`, 2026-07-24) | rozrost structa nie przesuwa sekcji za nim, nie kasuje ich przez błędny checksum |
| Osobny bajt/słowo `magic` w każdym structcie + CRC | thermo_control (pułapka `LockPin`: zera = „poprawny” checksum) | odróżnia pustą/nigdy niezapisaną FRAM od poprawnego zapisu |
| Lazy-init per sekcja (zły magic/CRC → wartości domyślne i zapis) | dosing_system (CRC-lazy-init sekcji) | nowa sekcja nie wymaga bumpu wersji layoutu ani factory resetu |
| `static_assert` rozmiarów structów i braku nakładania sekcji | dosing_system (`fram_layout.h`) | błąd layoutu wychodzi przy kompilacji |
| `#pragma pack(push, 1)` dla structów zapisywanych w FRAM | dosing_system | rozmiar niezależny od paddingu kompilatora (termostat miał 6 B → 8 B) |
| Flaga dnia ostatniego restartu w FRAM | dosing_system (`last_auto_restart_day`) | ochrona przed pętlą restartów (§15 handoffu) |

Świadoma różnica względem dozownika: tam niezgodność wersji layoutu = automatyczny factory reset całej FRAM.
Tu wersja layoutu dotyczy **tylko obszaru systemowego** (0x0000–0x1FFF); sloty programów mają własne
`format_version` w nagłówku każdego programu. Zmiana configu nigdy nie kasuje biblioteki programów.

---

## Podział

```
0x0000 ┌──────────────────────────────┐
       │ obszar systemowy 8 KB         │  wersja layoutu, config, poświadczenia, tombstone
0x2000 ├──────────────────────────────┤
       │ 24 sloty programów × 1 KB     │  0x2000 + n × 0x400, n = 0..23
0x8000 └──────────────────────────────┘
```

24 × 1024 = 24 576 B dokładnie do końca układu, adres slotu to proste przesunięcie.

## Obszar systemowy (0x0000–0x1FFF)

| Sekcja | Adres | Rozmiar | Użyte | Zawartość |
|---|---|---|---|---|
| HEADER | 0x0000 | 32 B | 32 | magic `"RL90"`, wersja layoutu, znacznik czasu inicjalizacji, CRC |
| CREDENTIALS | 0x0020 | 1024 B | 1024 | AES-256: Wi-Fi, hash hasła admina, device ID (moduł z `src/` bez zmian poza adresem) |
| SYSTEM_STATE | 0x0420 | 64 B | ~40 | id aktywnego programu, dzień ostatniego restartu, liczniki resetów |
| LAMP_CONFIG | 0x0460 | 128 B | ~40 | PWM, rampa, wentylator, preset nocny |
| CHANNEL_CONFIG | 0x04E0 | 4 × 32 B | 4 × ~16 | gamma, min_duty, power_frac, etykieta per kanał A–D |
| LOCK_PIN | 0x0560 | 16 B | 12 | rezerwacja — tylko jeśli przeniesiemy blokadę PIN z termostatu |
| (rezerwa) | 0x0570 | 656 B | — | wzrost structów config bez przesuwania TOMBSTONES |
| TOMBSTONES | 0x0800 | 1024 B | 1024 | 16 B meta + 126 × id 64 bit |
| (rezerwa) | 0x0C00 | 5 KB | — | przyszłe sekcje (np. log zdarzeń, jeśli kiedyś potrzebny) |

Liczby „użyte” to szkic; dokładne wartości ustalą `static_assert` przy kodzie.

### SYSTEM_STATE (64 B)
```c
struct SystemState {            // packed
    uint32_t magic;
    uint64_t active_program_id;   // 0 = brak → program fabryczny albo pierwszy zatwierdzony
    uint32_t last_restart_day;    // dzień lokalny ostatniego restartu planowego (§15)
    uint16_t cnt_wdt;             // liczniki resetów wg esp_reset_reason()
    uint16_t cnt_panic;
    uint16_t cnt_brownout;
    uint16_t cnt_other;
    uint8_t  last_reset_reason;
    uint32_t programs_created;    // zapisy z edytora, tylko rośnie; przy starcie ≥ max seq ze slotów
    uint8_t  _reserved[31];
    uint32_t crc32;
};
```
Nie ma tu trybu testowego ani nocnego: oba są efemeryczne, po restarcie lampa wraca do programu.

### LAMP_CONFIG (128 B)
```c
struct LampConfig {             // packed
    uint32_t magic;
    uint16_t ramp_s;              // wspólna rampa, domyślnie 10, zakres 3–30
    uint8_t  fan_on_pct;          // próg startu (P ≥), domyślnie 20; PWM fana = P
    uint8_t  fan_off_pct;         // próg stopu (P <), domyślnie 18 — histereza
                                  // (częstotliwości PWM LED/FAN = #define 500 Hz, nie w FRAM)
    uint16_t night_preset[4];     // setne % (0,1 % = 10), start suwaków trybu nocnego
    uint8_t  _reserved[...];      // do 124 B
    uint32_t crc32;
};
```

### CHANNEL_CONFIG (4 × 32 B)
```c
struct ChannelConfig {          // packed, 32 B
    uint32_t magic;
    float    gamma;               // 1.0 (v = % mocy; γ tylko korekta nieliniowości drivera)
    uint16_t min_duty;            // 0–16384
    uint16_t power_frac;          // ‱ mocy lampy przy kanale solo, domyślnie A 1500, B 1000, C 6000, D 1500
                                  // (nieaddytywne — wspólne diody; edycja w GUI „settings”)
    char     label[12];           // np. "Blue", "White" po ustaleniu mapowania (#3)
    uint8_t  _reserved[4];
    uint32_t crc32;
};
```
Każdy kanał ma własny magic i CRC, więc zapis jednego kanału nie dotyka pozostałych.

### TOMBSTONES (1024 B)
```c
struct TombstoneMeta {          // 16 B
    uint32_t magic;
    uint16_t count;               // 0..126
    uint16_t wptr;                // ring: po zapełnieniu nadpisywany najstarszy
    uint8_t  _reserved[4];
    uint32_t crc32;               // z count+wptr
};
// za meta: uint64_t ids[126]
```
126 skasowanych programów zanim ring nadpisze najstarszy wpis. Ryzyko wskrzeszenia dotyczy tylko programu
skasowanego ponad 126 kasowań temu, i to wyłącznie gdy jakaś lampa przez cały ten czas była odłączona.

### Katalog
Brak osobnej sekcji. Katalogiem są nagłówki 24 slotów: przy starcie odczyt 24 × 64 B (~1,5 KB, ~40 ms przy
400 kHz) i budowa listy w RAM. Skrót katalogu do heartbeatu ESP-NOW (§9.4) liczony w RAM z posortowanych id
programów + tombstone. Nie ma czego rozspójnić.

## Slot programu (0x2000 + n × 0x400)

```c
struct ProgramHeader {          // packed, 64 B, na początku slotu
    uint32_t magic;               // zapisywany OSTATNI; 0 = slot wolny/skasowany
    uint8_t  format_version;
    uint8_t  flags;               // bit0: program fabryczny
    uint16_t payload_len;
    uint64_t program_id;          // losowy (esp_random + MAC) albo stały dla fabrycznego
    uint64_t parent_id;           // program, z którego powstał przez edycję (0 = brak), §9.3
    uint32_t created_ts;          // UTC z RTC — tylko do sortowania listy w GUI, nie do rozstrzygania sync
    uint32_t payload_crc32;
    char     name[24];
    uint32_t seq;                 // numer z programs_created (0 = fabryczny / sprzed licznika); zmiana nazwy go zachowuje
    uint32_t header_crc32;        // CRC pól nagłówka poza magic
};
// payload od offsetu 64: 4 × { uint8 count; { uint16 t; uint16 v; } × count }
// max 4 × (1 + 48 × 4) = 772 B → 64 + 772 = 836 B, rezerwa 188 B
```

Zapis (zgodnie z §9.4):
1. Wybór pierwszego slotu z `magic` ≠ `PROGRAM_MAGIC` (first-fit).
2. Zapis payloadu, potem nagłówka bez `magic`.
3. Odczyt kontrolny (CRC payloadu i nagłówka).
4. Zapis `magic` (4 B), dopiero teraz program jest zatwierdzony.

Kasowanie: `magic = 0` + dopisanie id do TOMBSTONES. Zanik zasilania w dowolnym momencie zostawia slot
niezatwierdzony (niewidoczny, wolny do nadpisania) albo w pełni poprawny.

Odczyt przy starcie: slot jest ważny, gdy `magic` się zgadza, `format_version` jest znana i oba CRC się
zgadzają. W przeciwnym razie traktowany jak wolny.

Przy okazji: `created_ts` w nagłówku umożliwia sortowanie listy wg daty (pytanie nr 7 z `USTALENIA.md`).

## Inicjalizacja

- Pusta FRAM (zły magic nagłówka): zapis HEADER, domyślne SYSTEM_STATE / LAMP_CONFIG / CHANNEL_CONFIG,
  pusta lista TOMBSTONES, instalacja programu fabrycznego do slotu 0 i ustawienie go jako aktywnego.
- Program fabryczny jest nieusuwalny (2026-09-26): `deleteProgram()` zwraca `PROG_ERR_FACTORY`, `addTombstone()`
  pomija jego id. Przy każdym starcie, jeśli brak go w bibliotece, `initProgramStore()` zdejmuje go z TOMBSTONES
  (`removeTombstone()`) i instaluje w wolnym slocie; jako aktywny tylko przy pustej FRAM.
- Pojedyncza sekcja z błędnym magic/CRC: tylko ta sekcja dostaje wartości domyślne (lazy-init).
- Zmiana wersji layoutu obszaru systemowego: reinicjalizacja 0x0000–0x1FFF oprócz CREDENTIALS (nie trzeba
  ponownie provisioningu). Sloty 0x2000–0x7FFF nietknięte.

## Uwagi implementacyjne

- Sterownik: własny, blokowy na `Wire` (porcje 64 B + odczyt kontrolny). Adafruit_FRAM_I2C odrzucony — czyta
  i pisze bajt po bajcie (5 B transakcji na 1 B danych).
- CRC: CRC32 (IEEE) w każdej sekcji; poświadczenia zostają przy 16-bitowej sumie z modułu crypto.
- Wspólna magistrala z DS3231: procedura odzyskiwania magistrali (§10) i timeout I2C krótszy niż 5 s TWDT.
- Częstotliwość zapisów jest mała (config, program, raz na dobę SYSTEM_STATE), więc FRAM nie wymaga żadnego
  rozkładania zapisów.
