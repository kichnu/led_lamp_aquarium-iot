# Moc w watach zamiast % — plan pomiarów i kalibracji

Stan: 2026-09-26, planowanie (nic nie zaimplementowane). Cel: `id="power"` w GUI pokazuje moc LED w W zamiast
obecnego `P = Σ power_frac · duty` w %. Ten sam model ma zastąpić `power_frac` w sterowaniu wentylatorem.

## Ustalenia z rozmowy

- Zasilacz warsztatowy **nie ograniczał prądu** — nieaddytywność mocy kanałów nie jest artefaktem limitu.
  Wcześniej podawane wartości prądu (np. „2,6 A przy 24 V ≈ 62 W”, `RL90_HANDOFF.md` §1) są **zgrubne** —
  nie używać ich jako danych kalibracyjnych.
- Zasilacz **nie ma komunikacji z PC** — odczyty wpisywane ręcznie z wyświetlacza.
- Interesuje moc **samych LED**. Pomiar tylko po stronie wejścia 24 V (bez grzebania w PCB przetwornic),
  więc zawiera też wentylator, elektronikę (moduł, VDD Hi7001 ≈ 5 × 5,4 mA · 24 V) i straty driverów.
  Moc LED = P_wej − P0 − P_fan(duty), gdzie P0 i P_fan wyznaczone osobnym pomiarem.
  Uwaga: tak liczona „moc LED” zawiera straty przetwornic buck (sprawność ~90 %) — prawdziwa moc diod
  wymagałaby pomiaru po stronie LED, czego nie robimy.
- Część diod jest wspólna między kanałami → moce kanałów nie sumują się (stąd „power_frac nieaddytywne”
  w `USTALENIA.md`).

## Czujnik prądu w lampie (opcja, decyzja otwarta)

INA226 nadaje się, limit „800 mA” wynika z bocznika na typowym module, nie z układu:

| Układ | Zakres napięcia bocznika | Bocznik | Zakres prądu | Rozdzielczość |
|---|---|---|---|---|
| INA226 (moduł z R100) | ±81,92 mV | 0,1 Ω | 0,82 A | 25 µA |
| INA226 z bocznikiem R010 | ±81,92 mV | 10 mΩ | 8,2 A | 0,25 mA |
| INA219 (moduł z R100) | ±320 mV | 0,1 Ω | 3,2 A | ~0,8 mA (12 bit) |

- Całość lampy (kanał C > 1,5 A, suma kilka A) → INA226 z bocznikiem 10 mΩ (moduł z R010 albo
  przelutowany bocznik; moc na boczniku przy 3 A ≈ 90 mW). Magistrala I2C już jest (SDA 21 / SCL 20, obok DS3231
  0x68 i FRAM 0x50), INA226 domyślnie 0x40 — bez konfliktu. Szyna VBUS INA226 do 36 V — 24 V OK.
- Zalety: prawdziwe waty na bieżąco, odporne na temperaturę i starzenie diod; ten sam moduł = miernik dla
  kalibracji (automatycznej, bez ręcznego wpisywania).
- Model z kalibracji i tak potrzebny: przewidywanie mocy programu w edytorze, feedforward wentylatora.

## Model

Wypełnienie d_i = faktyczny duty LEDC (po gammie i `min_duty`) — zmiana gammy nie unieważnia kalibracji.

```
P_LED = Σ f_i(d_i)  −  Σ_{i<j} c_ij · min(d_i, d_j)
P_wej = P0 + P_fan(d_fan) + P_LED
```

- f_i(d): krzywa kanału solo, tabela ~14 punktów, interpolacja liniowa, gęściej na dole (Hi7001 nieliniowy
  poniżej ~10 %): 0, 0,5, 1, 2, 3, 5, 7, 10, 15, 20, 30, 50, 70, 100 %.
- c_ij: poprawka na wspólne diody pary kanałów. `min(d_i, d_j)` to hipoteza („wspólny odcinek liczy się raz”) —
  dane rozstrzygną, czy lepiej pasuje `d_i · d_j`. Pary bez interakcji: c_ij ≈ 0, pomijane.
- P_fan(d): kilka punktów przy zerowych LED; P0: wszystko 0.

## Plan pomiarów

Warunki każdego punktu: wentylator na stałej wartości (tryb kalibracji — feedforward zaburzałby pomiar),
odczekanie 30–60 s na ustalenie termiki, zapis U i I. Przebieg w górę i w dół (histereza termiczna); temperatura
radiatora, jeśli będzie DS18B20 (GPIO2).

1. **Diagnoza addytywności — 16 pomiarów:** wszystkie kombinacje kanałów wł./wył. przy 100 %, wentylator stały.
   Inkluzja–ekskluzja pokazuje, które pary/trójki współdzielą diody i ile. Brak interakcji → etap 3 odpada.
2. **Krzywe solo — 4 × 14 ≈ 56 punktów** + P0 + P_fan (np. 0/30/50/70/100 %).
3. **Siatki par z interakcją** (tylko wskazane w etapie 1): 5 × 5 (0/25/50/75/100 %) na parę; kilka punktów
   kontrolnych z 4 kanałami naraz (np. program fabryczny w kilku chwilach doby) → błąd całego modelu.

## Aplikacja kalibracyjna

Zasilacz bez interfejsu PC → **strona kalibracji w GUI lampy** (ukryta, jak Settings):
prowadzi krok po kroku (ustawia duty kanałów i wentylatora), użytkownik wpisuje odczyt prądu (i napięcia) z
zasilacza; dane w przeglądarce (localStorage na wypadek przerwania), dopasowanie (najmniejsze kwadraty) w JS,
podgląd błędów, zapis parametrów do FRAM. Ręcznie realne ~50–70 punktów. Z INA226 ta sama strona może czytać
prąd sama — wtedy cały przebieg automatyczny.

## Wymagania firmware

- Tryb kalibracji: duty kanałów wprost (bez rampy), wentylator na zadanej wartości, bez automatyki.
- Sekcja FRAM na parametry: 4 × 14 punktów f_i (uint16 mW) + 6 × c_ij + P0 + P_fan (~150 B, obszar systemowy
  ma zapas) + endpoint zapisu/odczytu.
- `/api/status`: moc LED w W (i P_wej / P_fan w diagnostyce); wentylator z modelu zamiast `power_frac`.
- Opcjonalnie sterownik INA226 (I2C 0x40) pod `LampLock`.

## Otwarte

- INA226 z bocznikiem 10 mΩ w lampie — tak/nie (wpływa na tryb aplikacji: ręczny vs automatyczny).
- Postać członu interakcji (`min` vs iloczyn) — po etapie 1–3.
- Czy w GUI pokazywać tylko moc LED, czy też P_wej (np. w diagnostyce).
