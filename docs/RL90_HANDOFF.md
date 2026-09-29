# PopBloom RL90 – zamiana modułu Tuya CBU na ESP32 (handoff)

Cel: zastąpić wlutowany moduł Wi-Fi/BLE (Tuya CBU) w lampie akwariowej PopBloom RL90
(płyta sterownika `RL90_V1`, płyta LED `JK1-D0B`) własnym ESP32 z własnym firmware.
Platforma docelowa: **Seeed XIAO ESP32-C3** (PlatformIO, oficjalny `espressif32`). Wstępne testy w sekcjach 1–7
robiono na XIAO ESP32-C6 (sekcja 5 to historia). Test PWM na C3 zakończony pozytywnie (`pwm_test/`, sekcja 8).
Ustalenia architektoniczne po testach: sekcje 8–13.

---

## 1. Fakty potwierdzone (zdjęcia + pomiary użytkownika)

### 1.1 Oryginalny moduł
- Tuya **CBU**, SoC **Beken BK7231N**, P/N 2.01.01.103308.
- Wlutowany pionowo, tylko dolny rząd 7 padów. Na płycie sitodruk `A B C D FAN GND 3.3` (`控制模块`).
- Mapowanie (spód modułu vs sitodruk płyty):

| Pad płyty | Pin BK7231N | HW PWM |
|---|---|---|
| A | P8 | PWM2 |
| B | P7 | PWM1 |
| C | P6 | PWM0 |
| D | P26 | PWM5 |
| FAN | P24 | PWM4 |
| GND | GND | – |
| 3.3 | 3V3 | – |

- Moduł nie ma żadnego sprzężenia zwrotnego: ADC, UART i pozostałe GPIO nie są wyprowadzone. Nie było czujnika temperatury.

### 1.2 Kanały mocy
- Zasilanie lampy: **24 V DC** (pola `DC输入 24V / GND`).
- LED to **wspólna anoda +24 V**. Złącze do płyty LED (strona drivera):
  `24V, 24V, B5-, B4-, B3-, M-, W-, G-, B2-, B1-, 24V`.
- 8 stringów: B1, B2, B3, B4, B5, G, W, M.
- **5× Hi7001** (Hichips, buck CC, wbudowany MOSFET 200 mΩ/100 V, SOP-8). Każdy ma własną parę rezystorów pomiarowych RS i własny dławik.

| Linia | Obciążenie (pomiar) |
|---|---|
| C | 2× Hi7001, dławiki 330 (33 µH): grupy `B1&B2` oraz `B3B4B5` (cała grupa niebieska, 5 stringów) |
| A | 1× Hi7001, dławik 470 (47 µH): jeden z G / W / M |
| B | 1× Hi7001, dławik 470: jeden z G / W / M |
| D | 1× Hi7001, dławik 470: jeden z G / W / M |
| FAN | tranzystor low-side, wentylator 2-przewodowy (obroty regulowane PWM w oryginalnej aplikacji) |

- Linie A–D idą **0 Ω** bezpośrednio na pin 2 (PWM) Hi7001. Nie ma filtrów RC.

### 1.3 Zachowanie linii sterujących (pomiar)
- Moduł odłączony, linie wiszą: **wszystkie kanały LED 100%, wentylator włączony**.
- Linia zwarta do GND przez **1 kΩ**: kanał (lub wentylator) **wyłączony**.
- Napięcie na wiszących A–D: **ok. 5 V** względem GND (ok. 1,7–2 V względem 3,3 V).
- Wniosek: wewnętrzny pull-up pinu PWM Hi7001 do jego VDD. Z pomiaru 1 kΩ wynika **R_pu > 5,25 kΩ** (dokładnej wartości jeszcze nie znamy).

### 1.4 Zasilanie układów
- Każdy Hi7001 ma własne zasilanie: **24 V → R3 = 3,5 kΩ → pin 3 (VDD)** plus kondensator do GND. Wewnętrzny zacisk VDD wynosi ok. 5,0–5,2 V, prąd ok. (24 − 5,2)/3,5k ≈ **5,4 mA**. **Na płycie nie ma wspólnej szyny 5 V.**
- Przetwornica pomocnicza (dławik 470, D3, 220 µF/25 V) daje **12 V**, które zasilają AMS1117-3.3 (U5). Wentylator jest zapewne 12 V (niepotwierdzone).
- AMS1117-3.3 zasila tylko moduł CBU.
- Masy: GND modułu = GND AMS1117 = GND Hi7001 (wspólna).

### 1.5 Moc
- Lampa bez modułu (wszystko 100%): **2,6 A przy 24 V ≈ 62 W** (zasilacz warsztatowy). Producent deklaruje 90 W. Pomiar zgrubny
  (zasilacz bez ograniczenia prądu) — do kalibracji mocy patrz `POWER_CALIBRATION.md`.

---

## 2. Hi7001 – kluczowe parametry z noty (V2.9, 2022-07-11)

| Pin | Nazwa | Funkcja |
|---|---|---|
| 1 | LD | ściemnianie analogowe 0,2–1,2 V (< 0,2 V = OFF), wewnętrzny pull-down 20 µA; nieużywany → zwierać z VDD |
| 2 | PWM | ściemnianie PWM, **wiszący = HIGH (ON)**, LOW = OFF |
| 3 | VDD | zasilanie, zacisk ok. 5,0–5,2 V, zasilany przez R3 z VIN |
| 4 | CS | źródło wewnętrznego MOSFET-a, rezystor pomiarowy |
| 5, 6 | D | dren MOSFET-a (piny odprowadzające ciepło) |
| 7, 8 | GND | masa |

- Progi PWM: **V_H = 1,4 V (narastające), V_L = 0,8 V (opadające)**. Abs. max V_PWM = 7 V. Logika 3,3 V jest w pełni kompatybilna.
- Reakcja na impulsy **< 60 ns**, ściemnianie deklarowane 65536:1. W nocie przebieg 100 Hz / 0,1% (impuls 10 µs) działa.
- Charakterystyka PWM (Iout = 300 mA, 100 Hz): prawie płaska poniżej ok. 1%, nieliniowa do ok. 10%, powyżej liniowa. W firmware potrzebna **gamma + minimalne wypełnienie** wyznaczone pomiarem.
- **I_out = 0,2 V / R_CS** (V_REF = 200 mV). Zakres 60 mA – 1,5 A, więc R_CS ≥ 0,133 Ω.
- Częstotliwość przełączania 30 kHz – 1 MHz (zależy od L i napięć).
- OTP: redukcja prądu przy 120 °C (ochrona układu, nie diod LED).
- V_IN: 5–100 V.

---

## 3. Jak oryginalny moduł znosił 5 V (i dlaczego ESP32 może sterować bezpośrednio)
- Wyjście push-pull BK7231N w stanie HIGH trzyma pin na 3,3 V, a różnica 1,7 V odkłada się na słabym pull-upie Hi7001 (prąd rzędu µA). Pin MCU nigdy nie widzi 5 V.
- W stanie LOW płynie 5,2 V / R_pu, czyli pojedyncze µA.
- W high-Z (boot, reset) linia rośnie do ok. 3,3 V + V_diody ESD. Prąd przez ESD ≈ (5,2 − 3,9)/R_pu, rzędu 10 µA. Formalnie poza specyfikacją, ale zaniedbywalne. Producent robił dokładnie tak samo.
- **Decyzja:** ESP32 steruje A–D **bezpośrednio przez rezystor szeregowy 330 Ω – 1 kΩ**. Wcześniej rozważane bufory 74LVC07 / 74AHCT są **niepotrzebne** (wynikały z błędnego założenia o szynie 5 V).
- Skutek przy boocie: piny w high-Z dają lampę na 100% przez ok. 1 s. **Decyzja (2026-09-23): akceptujemy, bez
  pull-downów** — oryginalny sterownik zachowuje się tak samo przy włączeniu zasilania. (Gdyby kiedyś miało być
  ciemno od startu: pull-down R_pd < 0,18 · R_pu, warunek 5,2 · R_pd/(R_pd + R_pu) < 0,8 V — wymaga pomiaru R_pu.)
  Problem błysku przy **planowym restarcie dobowym** rozwiązywany osobno przez `gpio_hold` (§15).

---

## 4. Otwarte kwestie (do zrobienia)

| # | Kwestia | Metoda | Blokuje? |
|---|---|---|---|
| 1 | ~~Napięcie na wiszącej linii FAN~~ | **ZAMKNIĘTE:** wejście PWM wentylatora ma 3,3 V (jak A–D), sterowanie bezpośrednie przez 1 kΩ | – |
| 2 | R_pu linii PWM | amperomierz µA pad → GND: R_pu = 5,2 V / I | nie — pull-downy odrzucone (§3), pomiar opcjonalny |
| 3 | Mapowanie A/B/D → G/W/M | test firmware, kanał po kanale | nie |
| 4 | Prądy kanałów | oznaczenia RS (I = 0,2/R_CS) + spadek na RS przy 100% | nie |
| 5 | 62 W vs 90 W | Σ V_f · I z R_CS; sprawdzić, czy zasilacz nie ogranicza (napięcie pod obciążeniem) | nie |
| 6 | Dolny próg liniowości PWM | `ramp <ch>` + prąd z zasilacza, 1000 / 500 / 250 Hz | nie |
| 7 | Częstotliwość i minimalne wypełnienie wentylatora, kick-start | test po rozwiązaniu #1 | nie |
| 8 | Dioda gasząca równolegle do wentylatora? | oględziny | nie |
| 9 | Test termiczny przy 100% (radiator po 30–60 min, cel ≤ 60–65 °C) | termometr / termopara | przed pracą ciągłą |

### Linia FAN, reguła
- ≤ 5,2 V i słaby pull-up: sterowanie bezpośrednie przez 1 kΩ.
- ok. 12 V: **NIE** łączyć bezpośrednio. N-MOSFET 2N7002 (G ← GPIO przez 100 Ω, pull-down 10 kΩ na G, D → FAN, S → GND). Logika odwrócona, więc `output_invert` / odwrócenie duty.

---

## 5. Konfiguracja testowa (XIAO ESP32-C6) — HISTORIA, zastąpiona przez sekcję 8 (C3)

| XIAO | GPIO | Pad RL90 | Uwagi |
|---|---|---|---|
| GND | – | GND | podłączać pierwszy |
| D0 | 0 | A | 1 kΩ szeregowo |
| D1 | 1 | B | 1 kΩ |
| D2 | 2 | C | 1 kΩ |
| D3 | 21 | D | 1 kΩ |
| D4 | 22 | FAN | dopiero po #1 |

- Omijamy strapping C6: GPIO4, 5, 8, 9, 15, oraz UART D6/D7. Na XIAO C6: GPIO3 = zasilanie przełącznika RF, GPIO14 = wybór anteny, GPIO15 = LED użytkownika.
- XIAO zasilane **tylko z USB**. **Nie łączyć 3V3 XIAO z szyną 3,3 V lampy.**
- Lampa zasilana z zasilacza warsztatowego, limit prądu ok. 2,8 A.

### Firmware testowy (dziś `docs/c6_bringup/main.cpp`)
- LEDC: kanały LED 1 kHz / 14 bit, FAN 25 kHz / 10 bit, domyślnie zablokowany (pin w high-Z do komendy `fan on`).
- Komendy przez Serial 115200:
  - `a|b|c|d|f <pct>`
  - `all <pct>`
  - `ramp <ch>` (kroki 0 → 0,01 → … → 100%, 3 s/krok)
  - `freq <Hz>` (kanały LED)
  - `fan on`
- Core 3.x: `ledcWrite(pin, (1<<res)-1)` daje pełne 100% (core zamienia max na max+1).
- `platformio.ini`: platforma pioarduino, `board = seeed_xiao_esp32c6`, `-DARDUINO_USB_CDC_ON_BOOT=1`. Awaryjnie `esp32-c6-devkitc-1` i numery GPIO.

---

## 6. Wymagania / zalecenia dla docelowego firmware i sprzętu

- **PWM LED:** 250–1000 Hz, rozdzielczość ≥ 14 bit. Przy 1 kHz / 14 bit krok to 61 ns, na granicy reakcji Hi7001. Niższa częstotliwość daje lepszy dolny zakres („księżyc”). Gamma plus minimalne wypełnienie z pomiaru (#6).
- **Wentylator:** osobny timer LEDC, częstotliwość zależna od #7. Kick-start 100% przez 1–2 s, potem wartość docelowa. Minimalne wypełnienie z pomiaru.
- **Termika:** dodać czujnik (NTC albo DS18B20) na radiatorze. Zamknięta pętla wentylatora plus derating mocy LED (np. liniowo od 55 °C). Kanał C (5 stringów, 2× Hi7001) prawdopodobnie ma największą moc.
- **Zasilanie ESP32 w docelowej wersji:** NIE z AMS1117 (wejście 12 V). Straty = 8,7 V · I: przy średnim poborze ESP32 z Wi-Fi 120–150 mA to 1,0–1,3 W, a SOT-223 przy θ_JA 60–90 °C/W daje przyrost 70–110 °C. Użyć osobnej przetwornicy step-down 12 → 3,3 V lub 24 → 3,3 V (MP1584 / MP2359), dodać 100 µF + 100 nF przy ESP32.
- **Failsafe przy zawieszeniu lub resecie MCU — ZDECYDOWANE (2026-09-23): bez pull-downów.** Reset/boot = lampa
  100% + wentylator ON przez ok. 1 s (jak oryginał przy włączeniu zasilania). Zawieszenie bez resetu: LEDC dalej
  generuje ostatni PWM, lampa stoi na ostatniej wartości — watchdog zamienia to w reset.
- Po włączeniu zasilania: rampa od 0 do wartości programu. Po planowym restarcie dobowym: od razu wartość
  programu, bez rampy (§15).
- Watchdog sprzętowy plus zapamiętanie harmonogramu i czasu (RTC/NTP) po restarcie.

---

## 7. Historia korekt (żeby nie wracać do błędnych założeń)
- ~~Szyna 5 V z przetwornicy pomocniczej zasila pull-upy~~ → 5 V to zacisk VDD każdego Hi7001, zasilanego przez R3 = 3,5 kΩ z 24 V. Przetwornica pomocnicza daje 12 V.
- ~~Potrzebny bufor 74LVC07 / 74AHCT~~ → wystarczy bezpośrednie sterowanie przez rezystor szeregowy.
- ~~Mostek do 3,3 V, żeby uzyskać 100%~~ → niepotrzebny, wiszące linie = 100%.
- ~~Linia FAN może mieć ok. 12 V, potrzebny 2N7002~~ → zmierzono 3,3 V na wejściu PWM wentylatora, sterowanie
  bezpośrednie przez 1 kΩ, bez tranzystora.
- ~~Platforma C6~~ → docelowo XIAO ESP32-C3.

---

## 8. Test PWM na XIAO ESP32-C3 (zakończony)

Samodzielny projekt `pwm_test/` (własny `platformio.ini`, nie zależy od `src/`, **zostaje w repo jako moduł testowy**).
- AP `RL90-PWM-TEST` / `pwmtest123`, kanał 6, captive portal (DNS `*` → 192.168.4.1), bez logowania.
- GUI: przycisk ON/OFF (OFF = 0% na wszystkich, wartości zapamiętane), kanały A–D z −/+ (krok 1%) i polem
  numerycznym (0,01%), pole częstotliwości 100–2000 Hz (wspólny timer).
- LEDC przez sterownik ESP-IDF (`driver/ledc.h`), 14 bit, 100% = duty 2^14 (bez impulsu LOW), domyślnie 1 kHz.
- Wynik: testy przeszły, wentylator reaguje na PWM tak samo jak kanały LED. Wgrywanie: `cd pwm_test && pio run -t upload`.
- Piny testowe C3: A=GPIO3, B=GPIO4, C=GPIO5, D=GPIO6, każdy przez 1 kΩ.
- **Dodane 2026-09-23 — test `gpio_hold` (jeszcze NIE wykonany na sprzęcie):** przyciski „Restart z hold” /
  „Restart bez hold”. Oba gaszą A–D (`ledc_stop`, poziom 0), wariant z hold robi `gpio_hold_en` na A–D przed
  `ESP.restart()`; po boocie `gpio_hold_dis` dopiero po `initPwm()`. GUI pokazuje przyczynę ostatniego resetu i
  czy był z hold (znacznik `RTC_NOINIT_ATTR`). Oczekiwane: bez hold ~1 s błysku 100%, z hold lampa ciemna przez
  cały restart. Wynik rozstrzyga §15.

---

## 9. Architektura docelowa (ustalenia z burzy mózgów)

### 9.1 Zasady
- Lampa jest **autonomiczna**: własny RTC (DS3231) i FRAM, krzywa wykonywana lokalnie z własnego zegara. Sieć służy
  tylko do konfiguracji i synchronizacji. Padnięcie innej lampy nie wpływa na działanie.
- Liczba lamp: teraz 2, docelowo 3, wszystko lokalnie (bez chmury Tuya). Współpraca lamp przez **ESP-NOW**
  (VLAN IoT ma izolację klientów, więc ruch IP lampa↔lampa odpada; ESP-NOW działa też bez routera).
- Każda lampa ma **stały IP** (rezerwacja w routerze) i **własną ścieżkę w nginx/VPS** (np. `/device/lamp1/`, jak
  `/device/thermo/` w termostacie; ta sama zasada trusted-proxy i ścieżek względnych). Dashboard VPS zostanie
  zmodyfikowany dla lamp na etapie wdrożenia na VPS (później).
- **GUI serwuje każda lampa** (ten sam kod, symetrycznie, bez mastera). Widok grupy pokazywany z heartbeatów
  ESP-NOW, edycja wysyłana do pozostałych ESP-NOW. Można też wejść bezpośrednio na każdą lampę.
- OTA wchodzi jak najwcześniej w finalnej implementacji (wzorzec z termostatu: `ArduinoOTA` + log-socket).
- Reuse sprawdzonych modułów z tego repo (`src/`): RTC, web server, provisioning, network, credentials, security.
  `src/` nie ruszać, dopóki użytkownik wyraźnie nie zleci finalnej implementacji.

### 9.2 Czas
- RTC trzyma UTC, krzywe zapisane w czasie **lokalnym** (minuty od północy), przeliczenie `localtime_r` z
  `POLAND_TZ = "CET-1CEST,M3.5.0,M10.5.0/3"`. Stałe godziny zegarowe, **bez** offsetów sunrise/sunset.
- Zmiana czasu: wiosną (02:00→03:00) krzywa przeskakuje do wartości dla 03:00, jesienią (03:00→02:00) odcinek
  02:00–03:00 się powtarza. Program „przeskakuje razem z zegarem" (decyzja użytkownika).
- Źródło czasu w grupie: lampa z najlepszą jakością (świeży NTP > sam RTC), pozostałe korygują RTC z heartbeatu.
  Czas przekazywany jako UTC.

### 9.3 Biblioteka programów (zamiast trybów grupa/indywidualnie)
Ustalone (użytkownik chce jeszcze „przespać się" ze szczegółami, więc traktować jako kierunek, nie finał):
- Każdy program ma **unikalny identyfikator** (64 bit, losowy: `esp_random` + MAC) i nazwę (etykieta, mogą się
  powtarzać, GUI dopisuje końcówkę id). Lampa trzyma **wiele programów**, wybór aktywnego jest lokalny dla lampy.
- Nowa/pusta lampa po provisioningu i uruchomieniu ESP-NOW **kopiuje programy** z innych lamp tym samym
  mechanizmem, co normalna synchronizacja (brak przypadku specjalnego).
- Użytkownik świadomie ustawia ten sam program na wszystkich lampach albo różne. Przycisk „ustaw na wszystkich"
  wysyła polecenie ESP-NOW; lampa, która nie ma jeszcze programu, dociąga go i aktywuje po zatwierdzeniu.
- **Rekomendacja (do potwierdzenia):** programy **niezmienne**. Edycja = „zapisz jako nowy" (nowy id, opcjonalne
  `parent`). Synchronizacja to wtedy suma zbiorów, bez konfliktów edycji i rewizji; kompletność = CRC32 stałej treści.
- Usuwanie przez **tombstone** (sam id), żeby lampa ze starą kopią nie wprowadziła programu z powrotem. GUI blokuje
  usunięcie programu aktywnego na znanej lampie; po wyścigu lampa używa lokalnej kopii, status „osierocony".
- Wentylator **nie jest** w programie. Sterowanie **feedforward, bez czujnika temperatury jako feedback**:
  firmware zna % udziału każdego kanału A–D w całkowitej mocy/prądzie lampy (do zmierzenia, kanały mają różną
  moc — kwestie #2–#9), sumuje to ważone rzeczywistym wypełnieniem PWM (po gammie i `min_duty`, **nie** surowym
  v% z edytora) i mapuje na PWM wentylatora z progiem + histerezą (0% poniżej progu, skok do `fan_min_pct`
  powyżej — pełna formuła w `CURVE_EDITOR_IMPLEMENTATION.md` §5/§7). Pomiar z radiatora (prawdopodobnie DS18B20
  na GPIO2, §11) to świadomie odłożone zabezpieczenie na przyszłość, nie wejście do tej pętli. Program = 4 kanały
  × ~48 punktów × 4 B ≈ 770 B + nagłówek + nazwa, mieści się w slocie 1 KB.

### 9.4 Procedura kopiowania i gwarancja kompletności
1. Heartbeat (co kilkanaście sekund) niesie skrót katalogu (hash posortowanej listy id + tombstone) i id aktywnego
   programu; różny skrót = coś się różni.
2. Lampy wymieniają listy id (1–2 ramki), każda ustala, czego jej brakuje.
3. Lampa sama pobiera brakujące programy po jednym, porcjami ≤200 B (numer sekwencji, retransmisja braków).
4. Skład w RAM (~1 KB), po odebraniu całości weryfikacja CRC32 i wersji formatu.
5. Zapis do wolnego slotu FRAM, **nagłówek (magic, id, CRC, długość) zapisany jako ostatni**. Dopiero wtedy program
   jest zatwierdzony.
- GUI listuje **wyłącznie zatwierdzone** programy. Przerwany transfer albo zanik zasilania zostawia slot z
  nieważnym nagłówkiem, niewidoczny i nadpisywalny. Lampy z nieznaną wersją formatu programu odrzucają go.

### 9.5 Odrzucone wcześniej rozważane (żeby nie wracać)
- ~~Tryby „grupa/indywidualnie" z dwoma slotami~~ → zastąpione biblioteką programów (9.3).
- ~~Znacznik czasu jako jedyna miara „najnowszy wygrywa"~~ (RTC może mieć zły czas) → przy niezmiennych
  programach konflikt w ogóle nie występuje.

---

## 10. Pamięć: FRAM I2C (decyzja)

- Wybór: **FRAM na I2C, 32 KB (256 kbit)**, np. Infineon **FM24W256** albo Fujitsu/RAMXEED **MB85RC256V**, SO8.
  Zasilanie 2,7–5,5 V (pasuje do 3,3 V), 400 kHz OK. Parametry z pamięci, do weryfikacji w karcie katalogowej
  przed zakupem (nie udało się otworzyć stron TME ani Allegro).
- Powód I2C zamiast SPI: XIAO C3 ma tylko 11 GPIO. SPI-FRAM (4 piny) + 5× PWM + I2C RTC = 11, bez miejsca na
  przycisk i czujnik temperatury.
- Adres FRAM ustawić na **0x50–0x53** (A0–A2), **nie 0x57**: moduły DS3231 typu ZS-042 mają EEPROM 24C32 pod 0x57.
  DS3231 = 0x68.
- Dlaczego FRAM, a nie flash ESP32: do trwałości zapisów flash w zupełności wystarcza (zapisy rzadkie, wear
  leveling w LittleFS/NVS), więc **nie ze względu na zużycie**. Argumenty za FRAM: dane przeżywają `erase_flash` i
  zmianę tablicy partycji; istniejący sprawdzony kod (`fram_controller`, szyfrowanie AES poświadczeń,
  `credentials_manager`) zostaje z minimalną zmianą warstwy sterownika SPI→I2C; spójność z innymi urządzeniami;
  brak opóźnień zapisu. Wady: dodatkowy układ, wspólna magistrala I2C z RTC (zawieszenie magistrali = utrata obu;
  łagodzić procedurą odzyskiwania magistrali).
- Opcjonalnie cienka warstwa `storage` (odczyt/zapis slotu z CRC), żeby backend był wymienny.
- Układ FRAM (ustalony 2026-09-23): 8 KB obszaru systemowego (config, poświadczenia, tombstone) + 24 sloty
  programów po 1 KB — pełna mapa w `FRAM_MAP.md`.

---

## 11. Pinout XIAO ESP32-C3 (docelowy)

```
                      ┌──── USB-C ────┐
 1-Wire DS18B20  D0  GPIO2   ┤        ├ 5V
 PWM A           D1  GPIO3   ┤        ├ GND
 PWM B           D2  GPIO4   ┤        ├ 3V3
 PWM C           D3  GPIO5   ┤        ├ D10 GPIO10  Przycisk RESET/PROV (do GND)
 PWM D           D4  GPIO6   ┤        ├ D9  GPIO9   wolny (BOOT, strapping)
 PWM FAN         D5  GPIO7   ┤        ├ D8  GPIO8   wolny (strapping)
 I2C SDA         D6  GPIO21  ┤        ├ D7  GPIO20  I2C SCL
                      └───────────────┘
```




- I2C: DS3231 (0x68) + FRAM (0x50), 400 kHz, kable krótkie, pull-up 4,7 kΩ do 3V3 (DS3231 modułowy często ma własne).
- PWM A–D i FAN: przez 1 kΩ szeregowo do padów lampy.
- **DS18B20 na GPIO2** (osobny pin, bo kabel do radiatora może być długi; nie na wspólnej magistrali I2C): pull-up
  4,7 kΩ do 3V3. GPIO2 jest pinem strapping i musi być 1 przy starcie; pull-up to zapewnia (NTC z dzielnikiem
  nie, mógłby dać stan niski). Nie stosować pull-downów na GPIO2.
- Przycisk na GPIO10 (bez strappingu), INPUT_PULLUP, aktywny LOW, przytrzymanie 5 s → provisioning (jak w termostacie).
- GPIO8 i GPIO9 zostają wolne (strapping), używać ostrożnie. GPIO9 to przycisk BOOT na płytce.
- Piny A–D (GPIO3–6) są przetestowane w `pwm_test/`. Piny FAN, I2C, DS18B20, przycisk nie były jeszcze testowane.

---

## 12. Zasilanie 24 V → 5 V

- Nie liniowo: przy 24 V → 5 V i ~0,15 A strata ≈ 2,9 W (7805/AMS1117 się przegrzeje).
- Propozycja: **3-pinowy moduł przetwornicy step-down (zamiennik 7805)**, zasilany bezpośrednio z 24 V wejścia lampy
  (nie z szyny 12 V AMS1117): **Murata OKI-78SR-5/1.5-W36-C** (do 36 V, 1,5 A) albo **Recom R-78B5.0-1.0** (do 32 V, 1 A).
  R-78E5.0-0.5 (do 28 V) ma mały zapas przy nominalnych 24 V. Parametry z pamięci, zweryfikować w kartach katalogowych.
- Kondensatory: wejście 10–22 µF/50 V, wyjście 100 µF + 100 nF, plus 100 µF + 100 nF przy ESP32.
- GND modułu wspólny z GND lampy (warunek poprawnego sterowania PWM Hi7001).
- Do sprawdzenia: czy XIAO C3 blokuje prąd wsteczny między pinem 5V a USB. Do tego czasu wgrywać przez USB przy
  odłączonym 24 V albo dać szeregowo diodę Schottky w linii 5V.
- Układ scalony (MP2359/MP1584) zamiast modułu jest tańszy, ale wymaga dławika i diody na PCB (opcja późniejsza).

---

## 13. Otwarte decyzje / kolejne kroki

- Potwierdzić programy niezmienne (9.3) i szczegóły procedury synchronizacji (użytkownik ma to jeszcze przemyśleć).
- Kupić FRAM I2C 32 KB (sprawdzić w karcie katalogowej: zasilanie 3,3 V, 400 kHz, adresowanie A0–A2).
- Otwarte kwestie sprzętowe z sekcji 4 (poza #1, zamkniętym): #2–#9 nadal do pomiarów, w tym #6 (dolny próg
  liniowości PWM, częstotliwość 100–2000 Hz w `pwm_test/`) i #7 (częstotliwość/minimalne wypełnienie wentylatora).
- Następny etap (po wyraźnym poleceniu użytkownika): szkielet finalnej aplikacji w `src/` z OTA na starcie,
  provisioning, RTC, FRAM I2C, PWM z harmonogramem; ESP-NOW i biblioteka programów w kolejnym kroku.

---

## 14. Edytor krzywych (dodane)

Idea z `reef_light-iot` (Akima + karetka + ▲▼ + lokalny RDP) przeniesiona na 4 kanały. Prototyp: `docs/curve_editor.html`,
specyfikacja i notatka wdrożeniowa: `docs/CURVE_EDITOR_IMPLEMENTATION.md` (format zapisu w FRAM, wykonanie, API,
lista rzeczy do dopracowania). Kluczowa poprawka: czysta Akima przestrzeliwuje na długich odcinkach, więc tangenta = 0
w ekstremach i przy odcinkach płaskich.

### 14.1 Tryb testowy i tryb nocny (poza samą krzywą)

Dwa tryby GUI, obok edycji krzywej programu:
- **Tryb testowy**: 4 pionowe suwaki (A–D), ręczne ustawianie, domyślnie wszystkie na 50 %. Do kalibracji/pomiarów
  (kwestie #2–#9 wyżej: R_pu, power_frac, próg liniowości PWM). Efemeryczny, nic nie zapisuje jako program. Bez
  auto-timeoutu (świadomie inaczej niż Service Mode w `src/` termostatu) — tylko ręczne wyjście.
- **Tryb nocny**: **ten sam mechanizm co tryb testowy**, NIE automatyczna zamiana krzywej w oknie 00:00–08:00.
  Inny preset domyślny (przyciemnione, „bezpieczne dla ryb" wartości zamiast 50 %) i krok 0,1 % zamiast zwykłego.
  Cel: user na chwilę ręcznie włącza ten tryb, żeby zerknąć do akwarium w nocy bez płoszenia ryb jasnym światłem
  testowym. Automatycznie w nocy (bez ręcznej interwencji) lampa nic specjalnego nie robi — nie ma planu na
  automatyczną symulację światła księżyca. Preset (4×0,1 %) zapisany w FRAM jako osobny config (jak `gamma`/
  `min_duty`), nie część programu/punktów krzywej — to tylko startowe wartości suwaków przy wejściu w ten tryb.
- **Rampa przy przełączaniu**: wspólny, konfigurowalny w GUI mechanizm płynnego przejścia dla program↔test,
  program↔tryb nocny (oba ręczne) i zwykłej zmiany aktywnego programu — zastępuje sztywne „~2 s" z
  `CURVE_EDITOR_IMPLEMENTATION.md` §5. Szczególnie istotna przy trybie nocnym: to łagodne wejście/wyjście, nie
  tylko przyciemniony preset, chroni ryby przed przestrachem.

---

## 15. Restart dobowy (ustalenia 2026-09-23)

Dane z innych projektów:
- thermo_control, reef_power_supply, cam_monitoring, top_off_water_new, filtr_RODI, top_off_water_doser: restart o
  lokalnej północy (`g_restartAtTs` z RTC przy boocie, guard 30 min uptime).
- Starsze (reef_light, top_off_water, switch_timer, TDS_meter): `millis() > 24 h`, godzina zależna od bootu.
- dosing_system (**ESP32-C3**): okno 00:15–00:45 UTC, tylko gdy scheduler IDLE i pompy OFF, flaga
  `last_auto_restart_day` w FRAM przeciw pętli restartów. `docs/MEMORY_LEAK_FIXES_1.0.md`: wyciek 330 B/min →
  25 B/min po poprawkach (~36 KB/dobę przy ~170 KB wolnego heapu) — ten sam MCU i ten sam stos web, który
  przeniesiemy z `src/`. Źródło reszty wycieku (AsyncWebServer?) niezweryfikowane (reef_power_supply `02_...md`).

Decyzja kierunkowa: **restart dobowy zostaje**, ale nie może dawać nocnego błysku 100% (pull-upy Hi7001, §3):
- Okno ok. 00:00–01:00, wzorzec z dozownika: flaga dnia w FRAM, warunki — brak trybu testowego/nocnego, brak
  aktywnej rampy, wszystkie kanały programu = 0.
- Przed `ESP.restart()`: A–D na stałe LOW + `gpio_hold_en`; po boocie LEDC na wartość programu, potem
  `gpio_hold_dis`. **Warunek: pozytywny test w `pwm_test/` (§8).** Jeśli hold nie przetrwa resetu programowego —
  wrócić do tematu (opcje: restart w dzień przy wysokiej jasności albo restart warunkowy od heapu).
- Po restarcie planowym od razu wartość programu, bez rampy od 0.
- Free heap + największy wolny blok w logach i `/api/status` — po kilku tygodniach ocenić, czy restart w ogóle
  jest potrzebny.
- Reset z watchdoga/crash: hold nie zadziała (brak kiedy go ustawić), błysk akceptowany jako sytuacja awaryjna.
