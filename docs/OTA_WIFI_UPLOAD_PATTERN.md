# Wzorzec: upload firmware przez WiFi (ArduinoOTA)

Krótki, przenośny wyciąg z wdrożenia w `thermo_control`. Cel: nie błądzić od
zera przy powtórce w **Dolewka**/**Dozownik** (ESP32, actuator-bearing —
pompa/dozownik, więc `onStart()`-emergency-off jest tu jeszcze ważniejszy niż
w termostacie). Pełna historia i pełne uzasadnienia decyzji:
`docs/DEV_UPLOAD_LOGGING_CLI_ARCHITECTURE.md` (sekcja 1) i pamięć projektu
(`ota_erase_abort_root_cause_2026-07-24`, `ota_upload_crash_investigation_paused_2026-07-23`).

## 1. platformio.ini

```ini
[env:xxx]
default_envs = xxx          ; BEZ tego `pio run` bez -e budowałoby WSZYSTKIE
                             ; environmenty, łącznie z doomed OTA-uploadem

[env:xxx_ota]
extends = env:xxx
upload_protocol = espota
upload_flags =
    --auth=${sysenv.OTA_PASSWORD_<ip_z_podkreślnikami>}
    --host_port=8266          ; STAŁY port zwrotny — patrz gotcha #3 niżej

build_flags =
    ...
    -DOTA_PASSWORD=\"${sysenv.OTA_PASSWORD_<ip_z_podkreślnikami>}\"   ; makro w firmware ZOSTAJE
                                                  ; "OTA_PASSWORD" (to go czyta
                                                  ; ArduinoOTA.setPassword() w main.cpp) — zmienia
                                                  ; się tylko nazwa zmiennej ŚRODOWISKOWEJ shella,
                                                  ; z której sysenv czyta wartość
```

Nazwa zmiennej środowiskowej **po lokalnym IP urządzenia**, nie po nazwie
projektu (np. `OTA_PASSWORD_192_168_10_4`, kropki → podkreślniki — shell/SCons
nie akceptują kropek w nazwach zmiennych). Powód: kilka projektów IoT
trzymanych w jednym `~/.secrets/iot.env` i sourcowanych do tego samego shella
— gdyby wszystkie używały tej samej nazwy `OTA_PASSWORD`, ostatni `export` w
pliku nadpisywałby poprzednie i łatwo o wgranie złego hasła do złego
urządzenia. `#define OTA_PASSWORD` w firmware (main.cpp) zostaje bez zmian —
to tylko nazwa zmiennej po stronie shella/`platformio.ini`.

## 2. main.cpp

```cpp
if (isWiFiConnected()) {
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([]() {
        // KRYTYCZNE dla pompy/dozownika: wyłączyć aktuator PRZED erase flash.
        // Erase blokuje CPU na dłużej niż jeden tick — aktuator zostawiony ON
        // zostanie ON przez cały czas trwania OTA (kilka-kilkanaście sekund).
        actuatorEmergencyOff();
        setSystemState(false);
    });
    ArduinoOTA.onError([](ota_error_t error) {
        // OTA może się nie udać z przyczyn niezwiązanych z crashem (zły hash,
        // sieć) — wtedy urządzenie NIE restartuje się samo, więc trzeba
        // ręcznie przywrócić stan, który onStart() wyłączył.
        setSystemState(true);   // albo jawne wznowienie tego, co wyłączono
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        // Jeśli loopTask jest gdziekolwiek subskrybowany do Task Watchdoga
        // (esp_task_wdt_add) — ArduinoOTA.handle() blokuje loop() na czas
        // CAŁEGO transferu, więc esp_task_wdt_reset() z loop() się nie
        // wykonuje. onProgress() woła się w tej samej (blokującej) pętli po
        // każdym zapisanym fragmencie — bezpieczne miejsce na reset. Wywołanie
        // jest nieszkodliwe też wtedy, gdy nic nie jest subskrybowane (zwraca
        // po prostu ESP_ERR_NOT_FOUND). Patrz gotcha niżej.
        esp_task_wdt_reset();
    });
    ArduinoOTA.begin();
}
// w loop(): ArduinoOTA.handle() — KAŻDĄ iterację, nie w wolniejszym ticku
```

**Pierwszy upload firmware zawierającego ten kod musi iść przez USB** — OTA nie
istnieje, dopóki firmware z `ArduinoOTA.begin()` nie jest już na urządzeniu.

## 3. Partycje — NAJWAŻNIEJSZY punkt, źródło realnego buga

OTA wymaga **dwóch prawdziwych slotów aplikacji** (`app0`+`app1`, boot flip
między nimi). Sprawdź to bezpośrednio w pliku CSV
(`~/.platformio/packages/framework-arduinoespressif32/tools/partitions/*.csv`)
— **nie ufaj samej nazwie pliku**. `huge_app.csv` brzmi bezpiecznie, a ma
tylko JEDEN slot (`app0`, self-overwrite) — w `thermo_control` to spowodowało
twardy `abort()` w `esp_flash_erase_region` przy każdej próbie OTA (host:
timeout/connection reset na 0%; serial: `abort() was called at PC ...` w
środku `esp_flash_erase_region`).

Checklist:
1. Sprawdź realny rozmiar flash z bootowego logu (`Flash: <bytes>` w
   `ESP.getFlashChipSize()`/log startowy) — **nie ufaj domyślnemu profilowi
   płytki w PlatformIO**. Board id (np. `seeed_xiao_esp32s3`) opisuje
   chip/toolchain, nie faktyczny sprzęt — ten sam wzorzec "board != realna
   płytka" jak w `thermo_control`/Dozowniku/ATO (Waveshare zamiast Seeed).
2. Wybierz gotową tabelę partycji dopasowaną do REALNEGO rozmiaru flash z
   dwoma slotami OTA (np. `app3M_fat9M_16MB.csv` dla 16MB, `default_8MB.csv`
   dla 8MB, `min_spiffs.csv` dla 4MB) — `board_build.partitions` w
   `platformio.ini`.
3. Zmiana partycji wymaga jednego reflashu po USB (nie przechodzi przez OTA).

## 4. Gotchas z sesji upload/debug

- **`OTA_PASSWORD_<ip>` musi być ustawione w środowisku, w którym działa
  `pio run`** — export w osobnym procesie (np. osobny krok w agencie/skrypcie,
  inna sesja shella) nie przetrwa do procesu `pio`. Objaw: `Authentication
  Failed` mimo poprawnego hasła — bo firmware zostało przeflashowane z
  pustym/innym hasłem w build_flags.
- **Ad-hoc `export` przed każdym uploadem powoduje pełny rebuild całego kodu
  przy KAŻDYM OTA, nie tylko incremental** — `-DOTA_PASSWORD` siedzi w
  globalnym `build_flags` (patrz sekcja 1), więc PlatformIO/SCons liczy
  sygnaturę do rebuildu KAŻDEGO `.o` na podstawie pełnej listy flag dla danego
  environmentu. Jeśli wartość zmiennej różni się między wywołaniami (nowy
  terminal bez eksportu = pusty string, inna wartość niż poprzednio), ten
  jeden flag zmienia sygnaturę dla wszystkich plików naraz → SCons
  przebudowuje wszystko od zera, nawet gdy realnie zmienił się jeden plik
  (np. sam CSS w embedded HTML). Fix: ustaw hasło RAZ, na stałe, poza repo —
  `~/.secrets/iot.env` (`chmod 700` katalog, `chmod 600` plik, jedna linia
  `export OTA_PASSWORD_<ip>='...'` per urządzenie/projekt — nazwa zmiennej po
  IP, patrz sekcja 1), sourcowany z `~/.zshrc`:
  ```bash
  [ -f ~/.secrets/iot.env ] && source ~/.secrets/iot.env
  ```
  Ze stałą wartością `pio run -e xxx_ota -t upload` wraca do normalnego
  przyrostowego budowania.
- **Hasło ze znakami specjalnymi shella (`#`, `$`, spacja) — zawsze w
  pojedynczym cudzysłowie**: `export OTA_PASSWORD_192_168_10_4='we6#...'`.
- **Różne podsieci/VLAN (izolacja IoT na routerze)**: espota po autentykacji
  wymaga połączenia TCP zainicjowanego PRZEZ urządzenie z powrotem do kompa
  (na `--host_port`) — typowa reguła firewalla "IoT może być odpytywane, nie
  może samo wchodzić do LAN" to blokuje. Potrzebna tymczasowa reguła forward
  (src=urządzenie, dst=komp, dst-port=host_port).
- **Log-socket (WiFi TCP log) NIE pokaże przyczyny crasha/paniki** — pisze się
  surowo na UART z pominięciem softwarowego loggera. Diagnoza crasha zawsze
  wymaga fizycznego USB monitora równolegle z próbą OTA.
- **Task Watchdog resetuje urządzenie W ŚRODKU transferu OTA, jeśli loopTask
  jest do niego subskrybowany** (`esp_task_wdt_add(NULL)`) — znaleziony i
  naprawiony w Dozowniku (2026-07-27). Domyślny timeout ESP-IDF to **5s**
  (`CONFIG_ESP_TASK_WDT_TIMEOUT_S`, sprawdź w
  `~/.platformio/packages/framework-arduinoespressif32-libs/<chip>/*/include/sdkconfig.h`)
  — łatwo przeoczyć własną stałą typu `WDT_TIMEOUT_SECONDS` w kodzie, jeśli
  nigdy nie trafia do faktycznego `esp_task_wdt_init()`. `ArduinoOTA.handle()`
  blokuje `loop()` na czas całego transferu (typowo >5s dla firmware rzędu
  1MB+), więc `esp_task_wdt_reset()` z `loop()` się nie wykonuje. Objaw:
  upload konsekwentnie "timed out" na hoście w tym samym % postępu, a na
  urządzeniu pełny reset — po nim wszystko działa normalnie, bo stary
  firmware nigdy nie został nadpisany (OTA nie commituje częściowych
  zapisów). Fix: `esp_task_wdt_reset()` w `ArduinoOTA.onProgress()` (patrz
  punkt 2 wyżej) — woła się w tej samej blokującej pętli po każdym zapisanym
  fragmencie.
- **Log-socket (WiFi TCP, np. port 8880) konkuruje o pulę gniazd LWIP z
  OTA** — jeśli projekt ma własny `WiFiServer` do zdalnych logów (nasłuchujący
  od boota), a `ArduinoOTA` podczas transferu otwiera własne połączenie TCP
  z powrotem do hosta (`client.connect()` w `ArduinoOTA.cpp`), oba mogą się
  wykłócać o limitowaną pulę BSD-socketów. Bezpieczny wzorzec: `stopLogServer()`
  w `onStart()`, `startLogServer()` w `onError()` (na sukces niepotrzebne —
  urządzenie się restartuje).
- **Dekodowanie backtrace'u precyzyjnie, nie na oko**: `xtensa-esp32sX-elf-addr2line
  -pfiaC -e .pio/build/<env>/firmware.elf <adresy z Backtrace:>` — ELF musi być
  z TEGO SAMEGO builda co crash (nie przebudowany później). Dodatkowo
  `objdump -d` wokół adresu abort(), jeśli trzeba zobaczyć co dokładnie
  sprawdza kod przed wywołaniem `abort()`.
