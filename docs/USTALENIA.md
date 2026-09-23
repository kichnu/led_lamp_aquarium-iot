# RL90 — ustalone i do ustalenia przed kodowaniem

Stan na 2026-09-23. Zbiorczy przegląd; szczegóły i uzasadnienia w `RL90_HANDOFF.md` (sekcje w nawiasach)
i `CURVE_EDITOR_IMPLEMENTATION.md`.

---

## Ustalone

### Sprzęt / platforma
- Seeed XIAO ESP32-C3. A–D i FAN sterowane bezpośrednio z GPIO przez 1 kΩ, bez buforów. FAN ma 3,3 V, 2N7002
  niepotrzebny (§3, §4 #1).
- Pinout (§11): PWM A–D = GPIO3–6 (przetestowane), FAN = GPIO7, I2C SDA 21 / SCL 20, DS18B20 (rezerwa) = GPIO2,
  przycisk provisioning = GPIO10, GPIO8/9 wolne.
- FRAM I2C 32 KB (FM24W256 / MB85RC256V) pod 0x50–0x53 (nie 0x57), DS3231 0x68 na tej samej magistrali (§10).
- PWM LED: LEDC 14 bit, 100 % = 2^14, częstotliwość w przedziale 250–1000 Hz (§6).
- Zasilanie 5 V z 24 V przez moduł step-down (OKI-78SR / R-78B), nie liniowo (§12).
- Start bez pull-downów na A–D: przy włączeniu zasilania / resecie ~1 s błysku 100 %, jak oryginalny sterownik
  (§3, §6). Po włączeniu zasilania rampa od 0 do wartości programu.

### Architektura
- Lampa autonomiczna (własny RTC + FRAM, krzywa liczona lokalnie). ESP-NOW między lampami, bez mastera,
  każda lampa serwuje GUI (§9.1).
- Stały IP + własna ścieżka nginx `/device/lampN/`, trusted-proxy jak w termostacie.
- Czas: RTC w UTC, krzywe w czasie lokalnym (`POLAND_TZ`), program przeskakuje z zegarem przy DST (§9.2).
- OTA + log-socket od pierwszego etapu; reuse modułów z `src/` (RTC, web, provisioning, credentials, security).
- Etapy: szkielet (OTA, provisioning, RTC, FRAM I2C, PWM z harmonogramem) → ESP-NOW + biblioteka programów.

### Restart dobowy (§15)
- Zostaje (dane z dozownika na tym samym C3 i stosie web: wyciek ~36 KB/dobę po poprawkach).
- Okno ok. 00:00–01:00, flaga dnia w FRAM przeciw pętli restartów, warunki: brak trybu testowego/nocnego, brak
  rampy, wszystkie kanały programu = 0.
- Bez nocnego błysku: A–D LOW + `gpio_hold_en` przed `ESP.restart()`, zwolnienie po inicjalizacji LEDC —
  **pod warunkiem pozytywnego testu** (niżej).
- Po restarcie planowym od razu wartość programu, bez rampy.
- Free heap + największy blok w logach i `/api/status`; po kilku tygodniach ocena, czy restart jest potrzebny.

### FRAM (2026-09-23) — szczegóły w `FRAM_MAP.md`
- 8 KB obszaru systemowego (0x0000–0x1FFF) + **24 sloty programów** × 1 KB (0x2000–0x7FFF).
- Wzorce: nagłówek 32 B + poświadczenia na 0x0020 (dozownik), stałe sloty z rezerwą (termostat), magic + CRC w
  każdym structcie, lazy-init per sekcja, `static_assert`, `#pragma pack(1)`.
- Wersja layoutu dotyczy tylko obszaru systemowego — zmiana configu nigdy nie kasuje programów ani poświadczeń.
- Brak osobnego katalogu: katalogiem są nagłówki slotów. Tombstone: ring 126 id.
- Nagłówek programu 64 B z `created_ts` i `parent_id`; `magic` zapisywany ostatni, kasowanie = `magic = 0`.
- Program fabryczny instalowany tylko przy inicjalizacji pustej FRAM.

### Watchdog (2026-09-23)
- Domyślne zostają: Interrupt WDT 300 ms, Task WDT 5 s z resetem (sdkconfig C3 frameworku).
- `enableLoopWDT()` w `setup()` — podpina `loop()` pod TWDT (domyślnie Arduino go nie pilnuje, więc zawieszenie
  z oddawaniem CPU, np. pętla retry I2C z `delay()`, zostawiłoby PWM na ostatniej wartości bez resetu).
- `esp_task_wdt_reset()` w `ArduinoOTA.onProgress()` — sprawdzony fix z dozownika
  (`OTA_WIFI_UPLOAD_PATTERN.md`), zachować przy przenoszeniu wzorca z termostatu.
- Timeout 5 s bez zmian; wywołania blokujące `loop()` dłużej poprawiać, nie wydłużać timeoutu. Przy przenoszeniu
  modułów z `src/` przejrzeć: sync NTP (`rtc_controller`), łączenie Wi-Fi, zapis FRAM.
- `esp_reset_reason()` w `/api/status` i logu + licznik resetów WDT/panic w FRAM.
- Reset z watchdoga = błysk 100 % (bez `gpio_hold`), akceptowany jako sytuacja awaryjna.

### Edytor i program
- Krzywa łamana (interpolacja liniowa), bez Akimy i RDP; punkty nigdy nie są kasowane automatycznie.
  Max 48 punktów/kanał, `MERGE_MIN` = 1 min.
- UI wg `curve_editor_linear.html`: krzyż ▲▲/▲/＋✕/▼/▼▼, przesuw krzywej ±10 min, widok 08:00–24:00
  (tylko GUI — dane obejmują całą dobę 0–1440).
- Format w FRAM: punkt = `uint16 t` + `uint16 v` (setne %), slot 1 KB, nagłówek zapisywany jako ostatni.
- Programy niezmienne: „zapisz” zawsze tworzy nowy program (nowe id 64 bit), „skasuj” zostawia tombstone.
  Tylko te dwa klawisze. Limit 24 programów, first-fit slot, numer slotu niewidoczny w GUI.
  Program fabryczny ze stałym id zaszytym w firmware.
- PWM: `duty = max(min_duty, pow(v, gamma) · 16384)`, parametry per kanał w FRAM (do pomiaru: γ = 1, min_duty = 0).
- Wentylator feedforward, bez czujnika: `Σ power_frac · duty` (duty po gammie), próg + histereza.
- Tryb testowy: 4 suwaki startujące od 50 %, bez timeoutu. Tryb nocny: ten sam mechanizm, preset z FRAM,
  krok 0,1 %, tylko ręcznie. Jedna wspólna rampa dla przełączeń trybów i programów.
- API: POST jako form-urlencoded (szkic w `CURVE_EDITOR_IMPLEMENTATION.md` §6).

---

## Do ustalenia przed kodowaniem szkieletu

1. **Test `gpio_hold` w `pwm_test/`** (przyciski „Restart z hold” / „Restart bez hold”) — czy hold przetrwa reset
   programowy C3. Negatywny wynik → wrócić do sposobu restartu (restart w dzień przy wysokiej jasności albo
   warunkowy od heapu).
2. Czas rampy: wartość domyślna i zakres w GUI.
3. Częstotliwość PWM LED (250 / 500 / 1000 Hz) — zależy od pomiaru #6.
4. Wentylator: częstotliwość, minimalne wypełnienie, kick-start (#7). `power_frac`, `FAN_ON_THRESHOLD`,
   `fan_min_pct` z pomiarów #4–#5.
5. Treść programu fabrycznego (punkty) i jego stałe id.
6. Kolejność listy programów w GUI: alfabetycznie czy wg daty (pole `created_ts` w nagłówku już jest, `FRAM_MAP.md`).
7. Walidacja API vs `MERGE_MIN`: dokument wymaga różnicy ≥ 2 min, prototyp dopuszcza 1 min — ujednolicić.
8. Krzywe w GUI jako v% postrzegane (obecnie) czy % mocy.

## Otwarte, nieblokujące szkieletu

- UX edytora: ◀▶ przesuwa krzywą (tak jest) czy karetkę; ▲▲▼▼ zmienia punkt (tak jest) czy całą krzywą w pionie;
  `SNAP_TOL` = 8 min (może za dużo); czy zostaje linia `#status`.
- Blokada PIN edycji GUI (jak w termostacie) — czy potrzebna; slot w FRAM zarezerwowany.
- Szczegóły synchronizacji ESP-NOW (§9.4) — kolejny etap.
- Sprzęt: zakup FRAM; pomiary #3–#9 (mapowanie A/B/D → G/W/M, prądy, 62 vs 90 W, próg PWM, wentylator, test
  termiczny); #2 (R_pu) już opcjonalny; weryfikacja przetwornicy; prąd wsteczny 5V↔USB w XIAO C3.
- Piny FAN, I2C, DS18B20, przycisk — nietestowane na sprzęcie.
- Prototyp HTML edytora nieuruchomiony w przeglądarce po stronie Claude (tylko logika w Pythonie).

## Nieaktualne w dokumentach (poprawić przed kodem)

- `CURVE_EDITOR_IMPLEMENTATION.md` §1–§4, §7: opisuje wariant Akima (RDP, long-press, ε, `MERGE_MIN` = 2,
  „punkty po RDP”).
- `RL90_HANDOFF.md` §6 (termika): zamknięta pętla temperatury i derating — zastąpione feedforwardem; §9.3 i §13:
  programy niezmienne „do potwierdzenia” — już potwierdzone; §14: wskazuje
  `curve_editor.html` (Akima) zamiast wariantu linear.
- `docs/c6_bringup/`: stary test na C6 (historia z §5), zachowany tylko jako archiwum.
