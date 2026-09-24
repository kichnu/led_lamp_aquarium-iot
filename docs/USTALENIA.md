# RL90 — ustalone i do ustalenia przed kodowaniem

Stan na 2026-09-24. Etap 1 zaimplementowany w `src/` (build OK), nietestowany na sprzęcie. Zbiorczy przegląd; szczegóły i uzasadnienia w `RL90_HANDOFF.md` (sekcje w nawiasach)
i `CURVE_EDITOR_IMPLEMENTATION.md`.

---

## Ustalone

### Sprzęt / platforma
- Seeed XIAO ESP32-C3. A–D i FAN sterowane bezpośrednio z GPIO przez 1 kΩ, bez buforów. FAN ma 3,3 V, 2N7002
  niepotrzebny (§3, §4 #1).
- Pinout (§11): PWM A–D = GPIO3–6 (przetestowane), FAN = GPIO7, I2C SDA 21 / SCL 20, DS18B20 (rezerwa) = GPIO2,
  przycisk provisioning = GPIO10, GPIO8/9 wolne.
- FRAM I2C 32 KB (FM24W256 / MB85RC256V) pod 0x50–0x53 (nie 0x57), DS3231 0x68 na tej samej magistrali (§10).
- PWM LED: LEDC 14 bit, 100 % = 2^14, **500 Hz** jako `#define LED_PWM_FREQ_HZ 500` (stała kompilacji, nie w FRAM)
  (§6).
- Wentylator: PWM **500 Hz** (`#define FAN_PWM_FREQ_HZ 500`), PWM wentylatora = szacowana moc LED (P 45 % → 45 %),
  histereza: start przy P ≥ 20 %, stop przy P < 18 % (`fan_min_pct` = 20, `fan_off_pct` = 18, w FRAM).
- Udział mocy kanałów (`power_frac`, domyślne w FRAM do czasu pomiaru #4–#5): A 15 %, B 10 %, C 60 %, D 15 %.
  Nie są addytywne: część diod świeci w dwóch kanałach naraz, więc Σ mocy kanałów mierzonych osobno > moc przy
  wszystkich włączonych. Edycja w GUI w sekcji „settings” (domyślnie ukrytej).
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
- Bez nocnego błysku: A–D LOW + `gpio_hold_en` przed `ESP.restart()`, zwolnienie po inicjalizacji LEDC.
  Bez osobnego testu w `pwm_test/` — weryfikacja na finalnym sprzęcie i kodzie (OTA jest przed finalną
  implementacją, obudowy nie trzeba otwierać). Jeśli hold nie zadziała: nocny błysk do czasu poprawki przez OTA
  jest akceptowalny (lampa to nie dozownik).
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
  Max 48 punktów/kanał, `MERGE_MIN` = 1 min — walidacja API tak samo: t całkowite, ściśle rosnące (różnica ≥ 1 min).
- UI wg `curve_editor_linear.html`: krzyż ▲▲/▲/＋✕/▼/▼▼, przesuw krzywej ±10 min, widok 08:00–24:00
  (tylko GUI — dane obejmują całą dobę 0–1440).
- Format w FRAM: punkt = `uint16 t` + `uint16 v` (setne %), slot 1 KB, nagłówek zapisywany jako ostatni.
- Programy niezmienne: „zapisz” zawsze tworzy nowy program (nowe id 64 bit), „skasuj” zostawia tombstone.
  Tylko te dwa klawisze. Limit 24 programów, first-fit slot, numer slotu niewidoczny w GUI.
  Program fabryczny ze stałym id `0x524C393046414354` (ASCII „RL90FACT”) zaszytym w firmware. Punkty (t w min,
  v w setnych %; zaokrąglone do pełnych godzin i dziesiątek %):
  ```
  A: [0,0] [600,0] [660,4000]  [1260,4000]  [1320,0] [1440,0]   10:00–11:00 rampa, 11:00–21:00 40 %, 22:00 zero
  B: [0,0] [600,0] [660,7000]  [1260,7000]  [1320,0] [1440,0]   70 %
  C: [0,0] [600,0] [660,10000] [1260,10000] [1320,0] [1440,0]   100 %
  D: [0,0] [600,0] [660,9000]  [1260,9000]  [1320,0] [1440,0]   90 %
  ```
- Lista programów w GUI alfabetycznie (bez rozróżniania wielkości liter, `localeCompare(…, 'pl')`).
- v w programie = % mocy kanału (liniowo z duty), nie % „postrzegany”. Wykres ma obrazować moc traconą w kanałach
  (≈ PAR przy równej sprawności LED). γ nie służy już percepcji — zostaje (domyślnie 1) tylko jako ewentualna korekta
  nieliniowości drivera, razem z `min_duty`.
- Oś Y wykresu: każdy kanał 0–100 % własnej mocy (jak w prototypie). Osobno w GUI wyświetlana szacowana moc tracona
  na LED (Σ `power_frac` · duty) — ta sama wartość, której używa wentylator.
- PWM: `duty = max(min_duty, pow(v, gamma) · 16384)`, parametry per kanał w FRAM (do pomiaru: γ = 1, min_duty = 0).
- Wentylator feedforward, bez czujnika: `P = Σ power_frac · duty` (duty po gammie/`min_duty`), przeliczane co ~30 s
  i wprost na PWM = min(P, 100 %) (Σ może przekroczyć 1). Histereza: wyłączony → start przy P ≥ 20 %,
  włączony → stop przy P < 18 %. Bez kick-startu.
- Tryb testowy: 4 suwaki startujące od 50 %, bez timeoutu. Tryb nocny: ten sam mechanizm, preset z FRAM,
  krok 0,1 %, tylko ręcznie. Jedna wspólna rampa dla przełączeń trybów i programów: domyślnie 10 s, zakres
  3–30 s w GUI (`ramp_s` w FRAM).
- API: POST jako form-urlencoded (szkic w `CURVE_EDITOR_IMPLEMENTATION.md` §6).

---

## Do ustalenia przed kodowaniem szkieletu

Brak — wszystkie punkty blokujące szkielet są ustalone.

## Otwarte, nieblokujące szkieletu

- UX edytora: ◀▶ przesuwa krzywą (tak jest) czy karetkę; ▲▲▼▼ zmienia punkt (tak jest) czy całą krzywą w pionie;
  `SNAP_TOL` = 8 min (może za dużo); czy zostaje linia `#status`.
- Blokada PIN edycji GUI (jak w termostacie) — czy potrzebna; slot w FRAM zarezerwowany.
- Szczegóły synchronizacji ESP-NOW (§9.4) — kolejny etap.
- Sprzęt: zakup FRAM; pomiary #3–#9 (mapowanie A/B/D → G/W/M, prądy, 62 vs 90 W, próg PWM, wentylator, test
  termiczny); #2 (R_pu) już opcjonalny; weryfikacja przetwornicy; prąd wsteczny 5V↔USB w XIAO C3.
- Piny FAN, I2C, DS18B20, przycisk, `gpio_hold` — nietestowane; testy już tylko na finalnym sprzęcie i kodzie
  (`pwm_test/` nie będzie dalej rozwijany).
- Prototyp HTML edytora nieuruchomiony w przeglądarce po stronie Claude (tylko logika w Pythonie).

## Nieaktualne w dokumentach (poprawić przed kodem)

- `CURVE_EDITOR_IMPLEMENTATION.md` §1–§4, §7: opisuje wariant Akima (RDP, long-press, ε, `MERGE_MIN` = 2,
  „punkty po RDP”). §5 pkt 3/6 i §7: v jako % „postrzegany” z gammą ~2,2 — teraz v = % mocy, γ = 1.
- `RL90_HANDOFF.md` §6 (termika): zamknięta pętla temperatury i derating — zastąpione feedforwardem; §9.3 i §13:
  programy niezmienne „do potwierdzenia” — już potwierdzone; §14: wskazuje
  `curve_editor.html` (Akima) zamiast wariantu linear.
- `docs/c6_bringup/`: stary test na C6 (historia z §5), zachowany tylko jako archiwum.

## Etap 1 — implementacja (2026-09-24)

- Moduły: `src/lamp/` (typy FRAM, obszar systemowy, biblioteka programów, silnik światła), `hardware/pwm_output`,
  własny sterownik FRAM I2C, `rtc_controller` z SNTP w tle (bez blokowania loop()), `core/lamp_lock`.
- API według `web/web_handlers.h`; GUI: status, lista programów (alfabetycznie), edytor (port prototypu),
  tryb test/nocny (4 pionowe suwaki), ustawienia ukryte pod „⚙ Settings”.
- Decyzje implementacyjne podjęte bez pytania (do weryfikacji):
  - aktywnego programu nie można skasować (przycisk ✕ wyłączony);
  - start z zasilania / po crashu: LEDC na 0 jako pierwsza instrukcja `setup()`, potem rampa od 0;
    bez czasu (brak DS3231 i NTP) kanały = 0, rampa w momencie pojawienia się czasu;
  - restart po OTA też przez `gpio_hold` (bez błysku), po nim wartość programu bez rampy;
  - WiFi łączy się w tle (termostat blokował `setup()` do 25 s — lampa byłaby ciemna);
  - blokada PIN usunięta z provisioningu (decyzja „czy potrzebna” wciąż otwarta);
  - GUI po angielsku jak w termostacie; preset nocny domyślnie 0,5 % na kanał.
