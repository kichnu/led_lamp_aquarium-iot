#ifndef ALGORITHM_CONFIG_H
#define ALGORITHM_CONFIG_H

#include <Arduino.h>

// ============================================================
// POMIAR TEMPERATURY
// ============================================================
#define TEMP_MEASURE_INTERVAL_S     15      // co ile sekund odczyt STS35

// ============================================================
// SENSOR FAULT — dwie warstwy tolerancji na przejściowe zakłócenia I2C
// (SSR/PWM w tej samej obudowie = realne źródło EMI, nawet przy sprawnym
// kablu — patrz pamięć projektu). Warstwa 1: szybki retry w środku
// pojedynczego cyklu (temp_sensor.cpp). Warstwa 2: licznik kolejnych
// nieudanych CYKLI (nie prób) — dopiero po SENSOR_FAULT_THRESHOLD w pełni
// nieudanych cyklach z rzędu ogłaszamy fault. To jest AWARIA KRYTYCZNA:
// zatrzymuje grzałkę/fan na stałe (latch), do ręcznego resetu w GUI/API
// — nawet gdy czujnik sam wróci do zdrowia (patrz TempAlgorithm).
// ============================================================
#define SENSOR_FAULT_THRESHOLD      3       // kolejnych w pełni nieudanych CYKLI → STATE_SENSOR_FAULT (latch)
#define STS35_RETRY_COUNT           3        // prób odczytu w jednym cyklu, zanim cykl liczy się jako nieudany
#define STS35_RETRY_DELAY_MS        50       // odstęp między próbami w cyklu

// ============================================================
// DOMYŚLNA KONFIGURACJA
// ============================================================
#define DEFAULT_TARGET_TEMP         25.0f
#define DEFAULT_HEAT_HYST           0.3f    // SSR ON poniżej T - X, OFF przy T (patrz MIN_HYSTERESIS_OFFSET)
#define DEFAULT_COOL_START          0.3f    // histereza OFF: fan OFF przy fan_on_point - cool_start (patrz thermal_buffer)
#define DEFAULT_COOL_FULL           1.0f    // fan 100% powyżej fan_on_point + X (patrz thermal_buffer)
#define DEFAULT_FAN_MIN_PCT         30      // floor rampy fana (poniżej fan się nie rusza / jest nieefektywny)
// Bufor termiczny dzień/noc (2026-07-24, user): fan ON dopiero od
// target_temp + thermal_buffer (nie od samego target_temp) — pozwala
// wykorzystać nadmiar ciepła zaakumulowany w dzień zamiast od razu go
// zbijać, żeby grzałka miała mniej pracy w nocy. cool_start liczy się teraz
// OD tego punktu (OFF przy fan_on_point - cool_start), podobnie cool_full
// (100% przy fan_on_point + cool_full) i alarm_delta_high (ALARM_HIGH przy
// fan_on_point + alarm_delta_high) — patrz _updateFan()/_updateAlarms().
#define DEFAULT_THERMAL_BUFFER      1.0f

// Przycisk "Fan ON" w GUI (test manualny, karta System Control) — jaki % ma
// ustawić jednym kliknięciem. Mirror tej wartości jest w JS (html_pages.cpp,
// FAN_MANUAL_DEFAULT_PCT) — brak mechanizmu wstrzykiwania #define do PROGMEM.
#define FAN_MANUAL_DEFAULT_PCT      50
#define DEFAULT_ALARM_DELTA_LOW     2.0f    // ALARM poniżej T - X
#define DEFAULT_ALARM_DELTA_HIGH    1.5f    // ALARM powyżej (T + thermal_buffer) + X — patrz DEFAULT_THERMAL_BUFFER
// Trend: grzałka ON od co najmniej trend_window_min minut, a temperatura nie
// wzrosła o trend_alarm_delta — czyli grzałka "pracuje", ale nieskutecznie
// (przepalony element, brak zasilania mimo sterowania). 0.2°C, bo 0.1°C to już
// rozdzielczość pomiaru STS35 — mniejszy próg tonąłby w szumie.
#define DEFAULT_TREND_ALARM_DELTA   0.2f    // wymagany wzrost temp. w oknie [°C]
#define DEFAULT_TREND_WINDOW_MIN    60      // długość okna [min] — do przetestowania w terenie
#define DEFAULT_TEMP_OFFSET         0.0f    // kalibracja czujnika
#define DEFAULT_HEATER_WATT         0.0f    // moc grzałek [W] — user wpisuje
#define DEFAULT_PULSES_PER_KWH      1000    // miernik energii

// Minimalna dopuszczalna wartość heat_hyst/cool_start — poniżej tego próg ON/OFF
// praktycznie pokrywa się z szumem czujnika STS35 (±0.1°C) i grozi chatteringiem.
#define MIN_HYSTERESIS_OFFSET       0.1f

// ============================================================
// LOGOWANIE — trzy proste bufory FRAM: godzinowy (rolling 24h, średnia
// liczona w RAM), dobowy (~rok, średnia z 24 rekordów godzinowych) i
// zdarzeń alarmowych (start/end epizodu). Zastąpiło to wcześniejszy
// event-driven LogZone/LogZoneConfig (2026-07-08 → 2026-07-14, patrz
// pamięć projektu) — decyzja: prostota i czytelny wykres długookresowy
// ważniejsze niż granulacja stopni ALARM_LOW/HIGH 1/2/3 i flapping.
// ============================================================
#define HOURLY_BUFFER_CAPACITY   24    // rolling 24h — nadpisywany co godzinę
#define DAILY_BUFFER_CAPACITY    366   // ~rok (+dzień przestępny) średnich dobowych
#define ALARM_BUFFER_CAPACITY    300   // fakty wystąpienia alarmu (jeden wpis/wystąpienie, patrz AlarmEvent)

// ============================================================
// STANY AUTOMATU (sterowanie aktuatorami — bez zmian)
// ============================================================
enum AlgorithmState : uint8_t {
    STATE_IDLE          = 0,
    STATE_HEATING       = 1,
    STATE_COOLING       = 2,
    STATE_ALARM_LOW     = 3,
    STATE_ALARM_HIGH    = 4,
    STATE_SENSOR_FAULT  = 5,
    STATE_TREND_ALARM   = 6,
};

// ============================================================
// BITMASKA _alarm_flags (warstwa sterowania — TempAlgorithm::getAlarmFlags(),
// niezależna od kodowania ring buffera poniżej)
// ============================================================
#define ALARM_FLAG_TEMP_LOW     0x01  // bit 0
#define ALARM_FLAG_TEMP_HIGH    0x02  // bit 1
#define ALARM_FLAG_TREND        0x04  // bit 2
#define ALARM_FLAG_SENSOR       0x08  // bit 3

// ============================================================
// KONFIGURACJA ALGORYTMU (persystowana w FRAM)
// ============================================================
struct ThermoConfig {
    float    target_temp;       // temperatura zadana [°C]
    float    heat_hyst;         // SSR ON gdy temp < target - heat_hyst; OFF gdy temp >= target_temp
    // Fan: punkt odniesienia to fan_on_point = target_temp + thermal_buffer (NIE
    // sam target_temp, patrz thermal_buffer niżej) — cool_start/cool_full liczone
    // OD fan_on_point, nie od target_temp. Patrz TempAlgorithm::_updateFan().
    float    cool_start;        // fan OFF gdy temp <= fan_on_point - cool_start (histereza)
    float    cool_full;         // fan 100% gdy temp > fan_on_point + cool_full
    float    alarm_delta_low;   // ALARM gdy temp < target - X
    float    alarm_delta_high;  // ALARM gdy temp > fan_on_point + X (patrz thermal_buffer)
    float    trend_alarm_delta; // wymagany wzrost temp. [°C] w oknie trend_window_min, przy ciągłym grzaniu
    float    temp_offset;       // kalibracja czujnika STS35 [°C]
    float    heater_watt;       // nieużywane — zarezerwowane pole (GUI: "Reserved")
    float    thermal_buffer;    // bufor dzień/noc: fan ON dopiero od target_temp + thermal_buffer, patrz DEFAULT_THERMAL_BUFFER
    uint16_t pulses_per_kwh;    // konfiguracja miernika energii
    uint16_t trend_window_min;  // długość okna detekcji trendu [min], patrz trend_alarm_delta
    uint8_t  is_configured;     // THERMO_CONFIG_MAGIC = ważna konfiguracja
    uint8_t  fan_min_pct;       // floor rampy fana [%] (0-100), patrz DEFAULT_FAN_MIN_PCT
};
static_assert(sizeof(ThermoConfig) == 48, "ThermoConfig must be 48 bytes"); // 10 float + 2×u16 + 2×u8 = 48B (padded) — thermal_buffer dodane 2026-07-24, 44B→48B

// ============================================================
// REKORD ŚREDNIEJ TEMPERATURY — używany przez bufor godzinowy (24 rekordy,
// rolling) i dobowy (~rok). Ta sama struktura, różne pojemności/adresy FRAM.
// 4B+2B logicznie, ale kompilator dopełnia do 8B (wyrównanie uint32_t) —
// przyjęte wprost zamiast wymuszać __attribute__((packed)) na Xtensa.
// ============================================================
struct TempAvgRecord {
    uint32_t timestamp;   // 4B — Unix UTC początku okresu (godziny / doby lokalnej)
    int16_t  temp_x10;    // 2B — średnia arytmetyczna, zaokrąglona do 0.1°C
};
static_assert(sizeof(TempAvgRecord) == 8, "TempAvgRecord must be 8 bytes (padded)");

// ============================================================
// ZDARZENIA ALARMOWE — sam FAKT wystąpienia (zbocze 0→1 danego typu), żeby
// dało się je "wyłowić" z długookresowego wykresu temp (który sam w sobie nie
// pokazuje alarmów, bo to tylko uśrednione wartości). Świadomie BEZ end/duration
// (2026-07-14, user) — nie steruje aktuatorami, to robi _alarm_flags/_updateAlarms().
// ============================================================
enum AlarmEventType : uint8_t {
    ALARM_EVT_LOW    = 0,
    ALARM_EVT_HIGH   = 1,
    ALARM_EVT_TREND  = 2,
    ALARM_EVT_SENSOR = 3,
};

struct AlarmEvent {
    uint32_t timestamp;   // 4B — Unix UTC zdarzenia
    int16_t  temp_x10;    // 2B — temp w chwili zdarzenia (ostatnia znana przy SENSOR_*)
    uint8_t  type;        // 1B — AlarmEventType
    uint8_t  _pad;        // 1B
};
static_assert(sizeof(AlarmEvent) == 8, "AlarmEvent must be 8 bytes");

// ============================================================
// ENERGIA (persystowana osobno w FRAM)
// ============================================================
struct EnergyStore {
    uint32_t total_pulses;      // narastający licznik impulsów
    uint32_t day_start_pulses;  // wartość na początku bieżącej doby
    uint16_t pulses_per_kwh;    // kopia z ThermoConfig
    uint16_t checksum;
};
static_assert(sizeof(EnergyStore) == 12, "EnergyStore must be 12 bytes"); // 2×u32 + 2×u16 = 12B, checksum wbudowany (pre-existing assert błędnie mówił 16)

// Timestamp ostatniego skasowania licznika total (przycisk + PIN w GUI).
// Osobny struct/adres od EnergyStore, żeby nie przesuwać jego istniejącego
// layoutu w FRAM — patrz FRAM_ADDR_ENERGY_RESET (wolny blok za lock-PIN).
// Zero jest poprawnym defaultem ("nigdy nie kasowany"), więc — inaczej niż
// LockPin — magic byte nie jest tu potrzebny (patrz [[fram_magic_byte_gotcha]]).
struct EnergyResetInfo {
    uint32_t reset_ts;   // Unix UTC ostatniego resetu total_pulses (0 = nigdy)
    uint16_t _reserved;
    uint16_t checksum;
};
static_assert(sizeof(EnergyResetInfo) == 8, "EnergyResetInfo must be 8 bytes");

// C-09 (odstępstwo od temperatury zadanej, 2026-07-15 → usunięte 2026-07-24):
// zastąpione przez thermal_buffer (patrz ThermoConfig/DEFAULT_THERMAL_BUFFER
// wyżej) — nie kolidowały ze sobą strukturalnie, ale drugi mechanizm robiący
// podobną rzecz (przesunięcie efektywnej temp. wokół której działa algorytm)
// był zbędny. FRAM_ADDR_DEVIATION_STATE/REASONS w fram_constants.h też usunięte.

// ============================================================
// LOCK PIN — blokada edycji GUI (osobna od hasła logowania), 2026-07-15.
// Chroni zapisy (config/odstępstwo/lista powodów/czyszczenie historii), NIE
// kartę "System" ani ręczne sterowanie w Service Mode — te zostają zawsze
// dostępne. Lazy-init do "1234" przy pierwszym odczycie świeżego FRAM.
//
// magic (LOCK_PIN_MAGIC) jest konieczny OBOK checksumu: calculateChecksum()
// to zwykła suma bajtów — świeże/nigdy niezapisane FRAM czyta się jako same
// zera, a suma samych zer to też 0, więc sam checksum "poprawnie" waliduje
// pusty PIN jako rzekomo zapisany stan (nie do odblokowania, bo GUI blokuje
// wysłanie pustego PIN-u). magic odróżnia to jednoznacznie, tak jak
// is_configured/THERMO_CONFIG_MAGIC przy ThermoConfig.
// ============================================================
#define LOCK_PIN_MAGIC 0x7A

struct LockPin {
    char     pin[8];       // numeryczny PIN, null-terminated
    uint8_t  magic;        // LOCK_PIN_MAGIC = ważny zapis
    uint8_t  _reserved;
    uint16_t checksum;
};
static_assert(sizeof(LockPin) == 12, "LockPin must be 12 bytes");

#endif // ALGORITHM_CONFIG_H
