# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Custom firmware for the **PopBloom RL90** aquarium LED lamp: the original Tuya CBU (BK7231N) Wi-Fi module is
replaced by a **Seeed XIAO ESP32-C3** (PlatformIO, Arduino framework). 4 PWM LED channels (A–D, Hi7001 buck
drivers) + PWM fan, DS3231 RTC, I2C FRAM 32 KB, a per-channel daily light curve ("program") executed locally,
web GUI with a curve editor, later ESP-NOW sync between 2–3 lamps.

**Status: stage 1 implemented, not yet tested on hardware** (backend, GUI with editor, OTA). ESP-NOW = stage 2.

## Repository Layout

```
docs/
  USTALENIA.md                    what is settled / still open before coding — START HERE, keep it current
  RL90_HANDOFF.md                 hardware facts, measurements, architecture (§1–§15)
  FRAM_MAP.md                     FRAM layout (8 KB system area + 24 × 1 KB program slots)
  POWER_CALIBRATION.md            plan: GUI power in W (measurements, shared-LED model, calibration page, INA226)
  CURVE_EDITOR_IMPLEMENTATION.md  curve editor / program format / API sketch (partly stale: still Akima, see USTALENIA)
  curve_editor_linear.html        chosen editor prototype (polyline) — make editor changes HERE
  curve_editor.html               older Akima variant, kept for reference only
  NETWORK_TOPOLOGY_ESP32.md, OTA_WIFI_UPLOAD_PATTERN.md   patterns reused from sibling projects
pwm_test/                         archived PWM test (no further tests here — only on final hw/firmware)
src/                              lamp firmware (XIAO ESP32-C3)
  lamp/                           lamp_types.h (FRAM structs, #defines), lamp_storage (system area),
                                  program_store (24 slots, factory program), light_engine (curve, ramp, fan, restart)
  hardware/                       fram_controller (own I2C block driver), rtc_controller (DS3231 + background SNTP),
                                  pwm_output (LEDC 500 Hz/14 bit, gpio_hold restart), hardware_pins.h
  web/                            web_handlers (lamp API), html_pages (GUI: thermostat CSS + editor port)
  core/lamp_lock                  recursive mutex: AsyncTCP handlers vs loop() (FRAM/I2C + lamp state)
  provisioning/, security/, crypto/, config/, network/   reused from thermo_control-iot
```

## Rules

- Before a design decision, check how sibling projects in `~/Dokumenty/My_apps/IOT/` solved it and reuse the
  proven pattern including its known fixes. Closest on the same MCU: top_off_water_new-iot (XIAO ESP32-C3,
  FRAM I2C, OTA with min_spiffs.csv). dosing_system builds for seeed_xiao_esp32s3 (despite docs calling it C3).
- Serve HTML with `beginResponse(200, type, (const uint8_t*)html, strlen(html))` — never `send(200, type, const char*)`,
  which copies the ~55 KB page into a heap String on every request (C3 has no PSRAM).
- Web handlers run in the AsyncTCP task: anything touching FRAM/I2C or lamp state goes under `LampLock`.
- When something is decided, update `docs/USTALENIA.md` right away: move it to "Ustalone", remove it from
  "Do ustalenia", renumber — without asking.
- `docs/*.md` code snippets are design sketches; once firmware exists, headers win.

## Build Commands

```bash
pio run                                            # build (XIAO ESP32-C3)
pio run -t upload                                  # USB (first flash must be USB)
export OTA_PASSWORD_192_168_10_5=... && pio run -e seeed_xiao_esp32c3_ota -t upload   # OTA, same command
pio device monitor                                 # 115200 baud
pio device monitor --port socket://192.168.10.5:8880   # log-socket over WiFi
```
Provisioning: hold GPIO10 button 5 s at boot → AP "RL90-LAMP-SETUP" / "setup12345".

## Key Decisions (summary — details in USTALENIA.md)

- Pinout (XIAO C3): PWM A–D = GPIO3–6 (1 kΩ series, direct drive), FAN = GPIO7, I2C SDA 21 / SCL 20
  (DS3231 0x68 + FRAM 0x50), DS18B20 reserve = GPIO2, provisioning button = GPIO10.
- LEDC 14 bit, 100 % = 2^14. Floating Hi7001 PWM input = 100 % (internal pull-up) → ~1 s full-brightness flash at
  boot is accepted (same as the original controller); no pull-downs.
- Daily restart kept (~00:00–01:00, channels at 0), with `gpio_hold_en` LOW on A–D to avoid a night flash —
  verified on final hardware/firmware via OTA (no more `pwm_test/` testing).
- Watchdog: `enableLoopWDT()`, `esp_task_wdt_reset()` in `ArduinoOTA.onProgress()`, 5 s timeout.
- Programs: polyline curves, max 48 points/channel, immutable (save = new id, delete = tombstone), 24 FRAM slots.
- Program values v = % of channel power (linear with duty, γ = 1). Factory program id `0x524C393046414354`.
- Fan: 500 Hz, feedforward P = Σ power_frac × actual duty, every ~30 s fan PWM = P (45 % → 45 %),
  hysteresis ON ≥ 20 % / OFF < 18 % (fan_min_pct / fan_off_pct), no sensor. power_frac non-additive (shared LEDs), editable in hidden GUI settings.
- LED PWM 500 Hz (`#define`), common ramp default 10 s (3–30 s), program list sorted alphabetically.
- Time: RTC in UTC, curves in local time (`POLAND_TZ`), program follows the clock across DST.
- POST handlers use `application/x-www-form-urlencoded` (ESPAsyncWebServer `hasParam(name, true)`), never JSON.
