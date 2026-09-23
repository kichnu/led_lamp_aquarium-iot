#ifndef TEMP_ALGORITHM_H
#define TEMP_ALGORITHM_H

#include <Arduino.h>
#include "algorithm_config.h"

// Automat sterowania temperaturą:
//  - histereza symetryczna (jeden parametr/kanał, OFF przy target_temp) dla SSR grzałki i PWM wentylatora
//  - detekcja alarmu trendu: grzałka ON od >= trend_window_min minut bez wystarczającego wzrostu temp.
//  - SENSOR_FAULT to awaria KRYTYCZNA — latch, grzałka/fan zamrożone do ręcznego resetu
//    (resetSensorFaultLatch()), nawet gdy czujnik sam wróci do zdrowia
//  - logowanie: bufor godzinowy (RAM-owy accumulator → FRAM co godzinę),
//    bufor dobowy (średnia z 24 godzinowych), log faktów wystąpienia alarmu
//    (bez start/end) — patrz TempAvgRecord/AlarmEvent w algorithm_config.h

class TempAlgorithm {
public:
    void begin();
    void update();                          // wywołuj co TEMP_MEASURE_INTERVAL_S (sensor+sterowanie+akumulacja)
    void checkLogRollover();                // wywołuj co pętlę/~100ms — detekcja granicy godziny/doby (patrz .cpp)

    AlgorithmState getState() const         { return _state; }
    const char*    getStateString() const;
    uint8_t        getAlarmFlags() const    { return _alarm_flags; }
    bool           isAlarmActive() const    { return _alarm_flags != 0; }
    // Wycisza buzzer dla alarmów aktywnych W TEJ CHWILI (web API). _alarm_flags
    // zostaje (widoczne w GUI dopóki warunek trwa) — wyciszenie dotyczy tylko
    // dźwięku i tylko bieżącego epizodu: gdy dany warunek ustąpi i pojawi się
    // ponownie, znów zadźwięczy (patrz _setAlarmFlag()).
    void           muteActiveAlarms();

    // SENSOR_FAULT jako awaria krytyczna (2026-07-14, user): raz zatrzaśnięta,
    // grzałka/fan zostają wymuszone OFF w KAŻDYM update() dopóki ktoś ręcznie
    // nie potwierdzi — sam powrót czujnika do zdrowia NIE wystarcza (to
    // świadomie inne zachowanie niż ALARM_LOW/HIGH/TREND, które same wracają
    // do normy). Jeśli usterka wciąż trwa, kolejny update() i tak natychmiast
    // zatrzaśnie ponownie.
    bool           isSensorFaultLatched() const { return _sensorFaultLatched; }
    void           resetSensorFaultLatch();

    // Konfiguracja (ładowana z FRAM lub defaults)
    bool           loadConfig();
    bool           saveConfig();
    const ThermoConfig& getConfig() const  { return _config; }
    bool           setConfig(const ThermoConfig& cfg);

private:
    AlgorithmState   _state       = STATE_IDLE;
    uint8_t          _alarm_flags = 0;   // aktywne TERAZ (live, nie latchowane)
    uint8_t          _muted_flags = 0;   // wyciszone przez muteActiveAlarms(), per-epizod
    ThermoConfig       _config    = {};
    bool             _fanActive   = false;  // latch: ON przy temp>fan_on_point, OFF przy temp<=fan_off_point (patrz _updateFan)
    bool             _sensorFaultLatched = false;
    // Ustawiane w update() przy każdym cyklu, gdy system jest wstrzymany (Service
    // Mode) — sygnalizuje, że przy najbliższym wznowieniu trzeba zresetować
    // grzałkę/fan do znanego stanu (patrz update() w .cpp, dlaczego).
    bool             _resumeResetPending = false;

    // Akumulator godzinowy — TYLKO RAM, flushowany do FRAM przy zmianie godziny.
    float            _hourAccumSum   = 0.0f;
    uint16_t         _hourAccumCount = 0;
    // (rok*366 + dzień_roku)*24 + godzina, dla ostatnio widzianej godziny lokalnej.
    // -1 = jeszcze nie zainicjalizowany (pierwszy tick po boot, nic nie flushuj).
    int32_t          _lastHourKey    = -1;

    // Punkt odniesienia dla trendu: początek BIEŻĄCEGO ciągłego grzania
    // (resetowany na każdym zboczu OFF→ON w _updateHeater). 0 = brak punktu
    // odniesienia (grzałka aktualnie nie grzeje od dłuższego czasu).
    uint32_t         _heatStartTs   = 0;
    float             _heatStartTemp = 0.0f;

    void _applyDefaults();
    void _updateHeater(float temp);
    void _updateFan(float temp);
    void _updateAlarms(float temp);
    bool _checkTrendAlarm(float temp) const;
    void _accumulateHourlySample(float temp);
    void _flushHourlyAccumulator(uint32_t hourStartTs);
    void _rollupDailyAverage();
    void _logAlarmEvent(uint8_t flag, float temp);
    // Ustawia/czyści bit w _alarm_flags; przy zboczu 0→1 loguje AlarmEvent
    // (fakt wystąpienia, bez end/duration), a przy 1→0 kasuje odpowiadający
    // bit w _muted_flags, żeby kolejne wystąpienie tego samego typu alarmu
    // znów zadźwięczyło (mute nie jest permanentny per-typ, tylko per-epizod).
    void _setAlarmFlag(uint8_t flag, bool active, float temp);
};

extern TempAlgorithm thermoAlgorithm;

#endif // TEMP_ALGORITHM_H
