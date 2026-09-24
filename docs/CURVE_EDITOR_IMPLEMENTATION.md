# Edytor krzywych — notatka wdrożeniowa

Źródło idei: `~/Dokumenty/My_apps/IOT/reef_light-iot/docs/` (`Curve_editor_code.html`, `curve_editor_dock.md`,
`Podsumowanie techniczne_popbloom.md`). Prototyp po poprawkach i rozszerzeniu do 4 kanałów: `docs/curve_editor.html`
(samodzielny plik, otwierać w przeglądarce, do testów UX bez sprzętu).

Status: **kierunek ustalony, szczegóły do dopracowania przy pisaniu kodu.** `src/` nie ruszany.
Kontekst architektury (programy, ESP-NOW, FRAM, czas): `RL90_HANDOFF.md` sekcje 9–11.

---

## 1. Idea (bez zmian względem reef_light)

- Krzywa kanału = lista punktów kontrolnych `{t, v}`, t = minuty lokalnego dnia 0–1440, v = 0–100 %.
  Punkty krańcowe t=0 i t=1440 są zawsze obecne i nieusuwalne.
- Interpolacja: spline Akimy (lokalny: zmiana punktu prawie nie wpływa na krzywą 2+ węzły dalej).
- Edycja: karetka na osi czasu + ▲▼. Punkt powstaje dopiero przy ▲▼ (lazy), snap karetki do punktu ±8 min.
- Krótkie naciśnięcie ▲▼ = krok normalny, długie = krok duży. Przycisk ✕ kasuje snapnięty punkt.
- Lokalny RDP po każdej zmianie: w oknie ±90 min usuwa punkty, których brak zmienia krzywą o < ε. Edytowany
  punkt chroniony. Dzięki temu liczba punktów zostaje mała bez ręcznego sprzątania.
- Widok „cała doba / okno aktywne" (zakres między pierwszym a ostatnim punktem 0 %).

## 2. Poprawki naniesione w prototypie

| # | Problem w reef_light | Poprawka |
|---|---|---|
| 1 | `LONG_PRESS_MS` = 2000 w kodzie, 200 w dokumentacji | ustawione 200, **do potwierdzenia na telefonie** |
| 2 | `evalSpline` zdefiniowana dwa razy, druga (bez zabezpieczeń NaN/`h=0`) nadpisywała pierwszą | jedna wersja z zabezpieczeniami |
| 3 | `#eps-row` pusty i niedomknięty, ε na sztywno 3 | przyciski ε: 0,5 / 3 / 5 / 10 % |
| 4 | Odbicie na krańcach niestandardowe (`dExt[1] = d[0]`), n=2 → `d[1]` niezdefiniowane | standardowe odbicie `2·d0 − d1`, osobna gałąź dla n=2 (prosta) |
| 5 | **Przestrzelenie splajnu** (Akima na długich odcinkach), do tej pory ukryte przez `clamp 0–100` | zerowa tangenta w ekstremach lokalnych i przy odcinkach płaskich, na krańcach styczna = sieczna (patrz 3.2) |
| 6 | Krok 2 % zbyt gruby dla „księżyca" | tryb dokładny 0,1 % / 1 % (przycisk „krok"), v zaokrąglane do 0,01 % |
| 7 | Jeden kanał | 4 kanały A–D, zakładki, pozostałe kanały jako przyciemniony podgląd |
| 8 | Brak limitu punktów | `MAX_POINTS = 48` na kanał (z krańcowymi), ostrzeżenie w GUI |

Poprawka 5 wymaga komentarza: czysta Akima przy przykładzie z reef_light (0 % do 06:00, 10 % o 07:00, …) dawała
−8,5 % przed świtem i 92,4 % przy szczycie 90 %. Przy pojedynczym punkcie „księżyca" 0,5 % na dobę z zerami w
reszcie dawała **6,9 % w południe** (powinno być ≈ 0). Klamra 0–100 tylko maskowała minus. Po poprawce we
wszystkich testowanych przypadkach `min ≥ 0` i `max = maksymalny punkt` (test: 8-punktowy dzień, prosta, trójkąt,
punkt księżyca). Efekt uboczny, zamierzony: szczyty i doliny są zawsze płaskie (tangenta 0), więc dzień ma
łagodne „pagórki" zamiast ostrych wierzchołków.

## 3. Matematyka (definicja kanoniczna — JS i C++ muszą dawać ten sam wynik)

### 3.1 Przygotowanie
1. Punkty posortowane po t; scal punkty bliższe niż `MERGE_MIN = 2` min (zostaje pierwszy).
2. n = liczba punktów po scaleniu. n < 2 → v = 0. n = 2 → prosta.
3. Nachylenia segmentów `d[i] = (v[i+1] − v[i]) / (t[i+1] − t[i])`, i = 0..n−2.

### 3.2 Tangenty
Tablica pomocnicza `s[0..n+2]`: `s[k+2] = d[k]`; `s[1] = 2·s[2] − s[3]`; `s[0] = 2·s[1] − s[2]`;
`s[n+1] = 2·s[n] − s[n−1]`; `s[n+2] = 2·s[n+1] − s[n]`.

Dla każdego węzła i:
```
w1 = |s[i+3] − s[i+2]|,  w2 = |s[i+1] − s[i]|
m[i] = (w1 + w2 < 1e-10) ? (s[i+1] + s[i+2]) / 2 : (w1·s[i+1] + w2·s[i+2]) / (w1 + w2)
i == 0:            m = d[0]
i == n−1:          m = d[n−2]
d[i−1]·d[i] <= 0:  m = 0          // ekstremum lokalne lub odcinek płaski
brak skończoności: m = 0
```

### 3.3 Wartość w chwili t
Poza `[p0.t, p(n−1).t]` → wartość skrajna. Wyszukiwanie segmentu binarne, potem Hermite:
```
h = t[hi] − t[lo];  x = (t − t[lo]) / h
v = (2x³−3x²+1)·v[lo] + (x³−2x²+x)·h·m[lo] + (−2x³+3x²)·v[hi] + (x³−x²)·h·m[hi]
```
Wynik nieskończony → interpolacja liniowa. Na końcu klamra do 0–100.

### 3.4 Test zgodności JS ↔ C++
Przycisk „wektor testowy" w prototypie wypisuje punkty i próbki co 10 min (4 miejsca po przecinku). Test na
hoście (albo `pio test -e native`): te same punkty → C++ liczy próbki → różnica ≤ 0,01 %. Minimum 4 zestawy:
dzień typowy, prosta (n=2), trójkąt (n=3), punkt księżyca 0,5 % z zerami. Referencyjna implementacja w Pythonie
była użyta do sprawdzenia poprawki 5 (kod w historii sesji, w razie potrzeby odtworzyć z sekcji 3.1–3.3).

## 4. Zapis w programie (FRAM, slot 1 KB)

Punkt: `uint16 t` (minuty, 0–1440) + `uint16 v` (setne procenta, 0–10000) = 4 B. Kanały A–D po kolei:
`uint8 count` + `count × 4 B`, max 48 punktów.

```
Nagłówek (zapisywany OSTATNI, zatwierdza slot):
  uint32 magic
  uint8  format_version
  uint64 program_id          // losowy: esp_random + MAC (9.3 handoffu)
  uint32 crc32               // z treści (kanały), bez nagłówka
  uint16 payload_len
  char   name[24]
Treść: 4 × { uint8 count; { uint16 t; uint16 v; } × count }
```
Rozmiar: nagłówek ≈ 43 B + 4 × (1 + 48·4) = 815 B najwyżej, mieści się w 1 KB.
Punkty w FRAM zawsze w wersji **po RDP** (to, co widzi użytkownik w GUI), więc GUI po wczytaniu pokazuje dokładnie
ten sam zestaw punktów. Firmware nie modyfikuje punktów.

## 5. Wykonanie na urządzeniu

1. Co ~1 s (albo co minutę + interpolacja płynna co 1 s) `local_min = (godz·60 + min)` z `localtime_r` (RTC = UTC,
   `POLAND_TZ` — patrz 9.2 handoffu) + ułamek z sekund.
2. Dla każdego kanału `v_pct = akima(t)` (float, tangenty policzone raz po wczytaniu programu do RAM).
3. **Mapowanie na PWM (po stronie firmware, nie GUI):**
   ```
   v == 0            → duty = 0
   inaczej           → duty = max(min_duty[ch], round(pow(v/100, gamma[ch]) · 16384))   // 14 bit, 100 % = 16384
   ```
   `gamma[ch]` i `min_duty[ch]` to parametry kanału zapisane w FRAM (konfiguracja, nie program), wyznaczone
   pomiarem (kwestia #6 z handoffu: dolny próg liniowości Hi7001, prąd z zasilacza przy 1000/500/250 Hz).
   Do czasu pomiaru: γ = 1, min_duty = 0, ale struktury i przypisania gotowe.
4. Zmiana wartości wyjścia tylko gdy duty się zmieni. Płynność: aktualizacja co 1 s wystarcza (krzywe wolne),
   ale przy nagłym kroku (zmiana aktywnego programu, wejście/wyjście z trybu testowego lub nocnego) rampa —
   wspólny, konfigurowalny w GUI mechanizm dla wszystkich tych przejść, patrz §8 (czas rampy jeszcze do ustalenia,
   zastępuje wcześniejsze sztywne „~2 s").
5. Zmiana czasu (DST): program przeskakuje z zegarem (decyzja 9.2), dodatkowej logiki nie trzeba, bo domena
   krzywej to godziny zegarowe.
6. **Wentylator (feedforward, bez czujnika temperatury):**
   ```
   P = Σ (power_frac[ch] · duty[ch] / 16384)     // ch = A..D, duty z kroku 3 wyżej (po gammie i min_duty!)
   fan_on   = fan_on ? (P >= fan_off_pct) : (P >= fan_min_pct)   // co ~30 s, ON ≥ 20 %, OFF < 18 %
   fan_duty = fan_on ? min(P, 100%) : 0
   // (zaktualizowane 2026-09-24, reszta punktu nieaktualna — patrz USTALENIA.md)
   ```
   `power_frac[ch]` — udział kanału w całkowitej mocy/prądzie lampy (parametr FRAM jak `gamma`/`min_duty`,
   wyznaczony pomiarem, kwestie #2–#9 handoffu; Σ power_frac ≈ 1 przy pełnym obciążeniu wszystkich kanałów).
   Suma musi liczyć **rzeczywiste `duty` z kroku 3** (po gammie i `min_duty`), nie surowe `v_pct` z kroku 2 —
   przy typowej gammie ~2,2 różnica jest ogromna w dolnym zakresie (v=50 % → realnie ~22 % duty), więc liczenie
   z v_pct przeszacowałoby potrzebę chłodzenia. Histereza start/stop wokół `FAN_ON_THRESHOLD` (jak
   `STATE_COOLING`/`cool_start` w algorytmie termostatu), żeby nie migał przy oscylacji koło progu. `FAN_ON_THRESHOLD`
   i `fan_min_pct` — do wyznaczenia pomiarem/próbami, jak `gamma`/`min_duty`. Osobny, odłożony na przyszłość
   temat: zabezpieczenie z czujnika radiatora (prawdopodobnie DS18B20 na GPIO2, §11 handoffu) — nie wejście do
   tej pętli, tylko planowany dodatkowy failsafe.

## 6. API (szkic, do zamknięcia przy implementacji)

Formularze `application/x-www-form-urlencoded` (wzorzec z repo, nie JSON w POST). Odpowiedzi GET w JSON.
```
GET  /api/programs                 lista zatwierdzonych programów {id, name, active}
GET  /api/program?id=<hex>         punkty 4 kanałów: {"A":[[t,v100],...],...}
POST /api/program-save             name + ch_a..ch_d (kodowanie: "t:v,t:v,...") → nowy id (program niezmienny)
POST /api/program-activate         id
POST /api/program-delete           id (tombstone)
GET  /api/live                     bieżące v% i duty dla A–D (podgląd, karetka „teraz" w GUI)
POST /api/manual-enter             mode=test|night → startowe wartości suwaków (test: 50%; night: zapisany preset), z rampą
POST /api/manual-set                ch_a..ch_d (wartości suwaków, live)
POST /api/manual-exit               powrót do aktywnego programu, z rampą
GET|POST /api/night-preset          odczyt/zapis presetu trybu nocnego (4×0,1%) — szkic, forma do domknięcia (§8)
```
Walidacja po stronie serwera (nie ufać GUI): t rosnące, t[0] = 0, t[n−1] = 1440, różnice ≥ 1 min (= `MERGE_MIN`), v ≤ 10000,
count ≤ 48 na kanał, długość nazwy.

GUI serwowane z PROGMEM (`html_pages.*` w stylu termostatu), edytor jako jeden plik, gzip.

## 7. Do dopracowania na etapie kodu

- `LONG_PRESS_MS`: 200 ms może być za krótkie dla dotyku (zwykłe tapnięcie to często 100–150 ms). Sprawdzić na telefonie.
- Domyślne ε RDP przy trybie dokładnym: 3 % zjada punkty księżyca (0,5 % nie odróżnia się od 0). Możliwe, że ε
  powinno być zależne od trybu kroku albo względne.
- Wygładzanie w ekstremach (tangenta 0) — sprawdzić, czy użytkownikowi odpowiada; alternatywa to monotoniczna
  Akima z ograniczeniem tylko przy przestrzeleniu.
- Kolory i nazwy kanałów A/B/D po zmierzeniu mapowania na G/W/M (kwestia #3).
- Czy w GUI pokazywać krzywe w v% „postrzeganych" (jak teraz) czy w % mocy? Decyzja wpływa na gammę:
  teraz v to wartość wejściowa gammy, więc 50 % ≈ „wygląda na połowę".
- Kopiuj/wklej krzywej między kanałami, „skopiuj ze wschodu na zachód", przesunięcie całego programu o ±N min —
  wygodne, nieobowiązkowe.
- Wentylator poza edytorem — sterowanie feedforward, patrz §5 pkt 6. `power_frac[ch]` (pomiar mocy per kanał),
  `FAN_ON_THRESHOLD` i `fan_min_pct` jeszcze do wyznaczenia.
- Zaokrąglenie do setnych: 0,01 % przy 14 bit PWM to 1,6 kroku duty, więc granica sensowna, ale sprawdzić z gammą.
- Czas trwania rampy przy przełączaniu (§8) — do ustalenia/pomiaru, ma być konfigurowalny w GUI.

## 8. Tryb testowy i tryb nocny (poza edytorem krzywej)

Dwa tryby GUI, poza edycją krzywej programu (patrz też `RL90_HANDOFF.md` §14.1):
- **Tryb testowy**: 4 pionowe suwaki (A–D), ręczne ustawianie, domyślnie wszystkie na 50 %. Do kalibracji/pomiarów
  (kwestie #2–#9 handoffu). Efemeryczny — nic nie zapisuje jako program. Bez auto-timeoutu (świadomie inaczej niż
  Service Mode w `src/` termostatu) — tylko ręczne wyjście.
- **Tryb nocny**: **ten sam mechanizm co tryb testowy**, NIE automatyczna zamiana krzywej w oknie 00:00–08:00.
  Inny preset domyślny (przyciemnione, „bezpieczne dla ryb" wartości zamiast 50 %) i krok 0,1 % zamiast zwykłego
  (moduł tego samego komponentu suwaków co tryb testowy, tylko z innymi wartościami startowymi i krokiem). Cel:
  user na chwilę ręcznie włącza ten tryb, żeby zerknąć do akwarium w nocy bez płoszenia ryb jasnym światłem
  testowym. Automatycznie w nocy (bez ręcznej interwencji) lampa nic specjalnego nie robi — brak planu na
  automatyczną symulację światła księżyca, stąd też brak potrzeby precyzji 0,1 % w samym edytorze krzywej
  (§14 handoffu, `VIEW_START_DEFAULT`).
- Preset trybu nocnego (4×0,1 %) zapisany w FRAM jako osobny config, analogicznie do `gamma[ch]`/`min_duty[ch]`
  z §5 — NIE część programu/punktów krzywej. To tylko wartości startowe suwaków przy wejściu w tryb nocny.
- **Rampa**: jeden, wspólny mechanizm płynnego przejścia (czas konfigurowalny w GUI, do ustalenia) dla
  program↔test, program↔tryb nocny (oba wyzwalane ręcznie) oraz zwykłej zmiany aktywnego programu — patrz §5
  pkt 4. Przy trybie nocnym rampa jest szczególnie istotna: to łagodne wejście/wyjście, nie tylko przyciemniony
  preset, chroni ryby przed przestrachem.
