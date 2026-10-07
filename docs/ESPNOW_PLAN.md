# ESP-NOW — plan etapu 2 (kopiowanie programów między lampami)

Stan: plan, nic nie zaimplementowane. Podstawa: `RL90_HANDOFF.md` §9.3–9.4. W projektach w
`~/Dokumenty/My_apps/IOT/` ani w `~/dev-knowledge/` nie ma kodu ESP-NOW — moduł powstaje od zera.

## Identyfikacja lamp

Provisioning raczej bez zmian. To, czego ESP-NOW potrzebuje, już istnieje:

- MAC jest adresem w ESP-NOW i jest unikalny. Lampy znajdują się same po broadcaście heartbeatu,
  bez ręcznego parowania.
- Nazwa urządzenia (pole „Device Name” w provisioningu) jest zapisana w FRAM (`fram_encryption`) i zwracana w API
  jako `device` (`web_handlers.cpp`, `getDeviceID()`). Heartbeat ją przesyła, GUI pokazuje w widoku grupy
  (np. „rl90-lamp-1”). Do sprawdzenia: czy obie lampy (192.168.10.5 i .6) mają różne nazwy.

Rzeczy, które mogą coś zmienić:

1. Zabezpieczenie ramek. Bez niego dowolne urządzenie w zasięgu może wysłać lampie program albo zmienić jej czas.
   Propozycja: podpis HMAC z kluczem grupy wyliczonym z hasła Wi-Fi, które obie lampy już znają — provisioning
   bez zmian. Wariant bezpieczniejszy: osobny klucz grupy wpisywany w provisioningu (dodatkowe pole).
2. Kanał radiowy. ESP-NOW działa tylko na kanale, na którym lampa jest połączona z Wi-Fi. Przy jednym punkcie
   dostępowym to jest automatyczne. Przy kilku AP / mesh na różnych kanałach lampy mogą się nie słyszeć.
3. Druga grupa lamp (np. inne akwarium). Wtedy potrzebna nazwa grupy — jedyna realna zmiana w provisioningu.
   Teraz grupa jest jedna i wyznacza ją wspólna sieć Wi-Fi.

## Do zrobienia

1. Moduł `espnow`: inicjalizacja po połączeniu z Wi-Fi, ramki z magic, wersją formatu i HMAC. Callback odbioru
   działa w tasku Wi-Fi — tylko wrzuca ramkę do kolejki; przetwarza ją `loop()` pod `LampLock` (inaczej dostęp
   do FRAM/I2C koliduje z resztą).
2. Heartbeat co kilkanaście sekund: nazwa, IP, skrót katalogu (hash posortowanej listy id + tombstone), id
   aktywnego programu, jakość czasu (świeży NTP czy sam RTC), licznik `programs_created`
   (przy synchronizacji max(własny, cudzy)).
3. Synchronizacja katalogu: wymiana list id, pobieranie brakujących programów porcjami ≤ 200 B (numer sekwencji,
   retransmisja braków), weryfikacja CRC32 i wersji formatu, zapis do wolnego slotu FRAM z nagłówkiem zapisywanym
   na końcu. `program_store` jest przygotowany: programy niezmienne, usunięte zostawiają tombstone, stałe id
   programu fabrycznego (`FACTORY_PROGRAM_ID`) takie samo na każdej lampie.
4. Czas: lampy z samym RTC korygują zegar z heartbeatu lampy, która ma świeży NTP (czas przesyłany jako UTC).
5. GUI: widok grupy, przycisk „ustaw na wszystkich”, blokada usuwania programu aktywnego na innej lampie, status
   „osierocony” (program aktywny na lampie, choć został już usunięty).

## Do ustalenia przed kodowaniem

- Skąd klucz grupy: z hasła Wi-Fi czy osobne pole w provisioningu?
- Jeden punkt dostępowy czy kilka (kanał ESP-NOW)?
- Czy „ustaw na wszystkich” aktywuje program od razu, czy po potwierdzeniu na każdej lampie?
