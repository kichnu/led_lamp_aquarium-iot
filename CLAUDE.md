# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Custom firmware for the **PopBloom RL90** aquarium LED lamp: the original Tuya CBU (BK7231N) Wi-Fi module is
replaced by a **Seeed XIAO ESP32-C3** (PlatformIO, Arduino framework). 4 PWM LED channels (A–D, Hi7001 buck
drivers) + PWM fan, DS3231 RTC, I2C FRAM 32 KB, a per-channel daily light curve ("program") executed locally,
web GUI with a curve editor, later ESP-NOW sync between 2–3 lamps.

**Status: design phase.** Decisions are settled in docs; the final firmware is not written yet.

## Repository Layout

```
docs/
  USTALENIA.md                    what is settled / still open before coding — START HERE, keep it current
  RL90_HANDOFF.md                 hardware facts, measurements, architecture (§1–§15)
  FRAM_MAP.md                     FRAM layout (8 KB system area + 24 × 1 KB program slots)
  CURVE_EDITOR_IMPLEMENTATION.md  curve editor / program format / API sketch (partly stale: still Akima, see USTALENIA)
  curve_editor_linear.html        chosen editor prototype (polyline) — make editor changes HERE
  curve_editor.html               older Akima variant, kept for reference only
  c6_bringup/                     archived first bring-up test on XIAO ESP32-C6
  NETWORK_TOPOLOGY_ESP32.md, OTA_WIFI_UPLOAD_PATTERN.md   patterns reused from sibling projects
pwm_test/                         standalone PWM + gpio_hold test for XIAO C3 (own platformio.ini)
src/, platformio.ini              BASELINE COPIED FROM thermo_control-iot (ESP32-S3) — not lamp code yet
```

## Rules

- **Do not modify `src/` or the root `platformio.ini`** until the user explicitly starts the final
  implementation. They are the thermostat codebase kept as a source of reusable modules (RTC, web server,
  provisioning, network, credentials, security, logging/OTA).
- Before a design decision, check how sibling projects in `~/Dokumenty/My_apps/IOT/` solved it (dosing_system
  is the closest: same ESP32-C3 + I2C FRAM MB85RC256V) and reuse the proven pattern including its known fixes.
- When something is decided, update `docs/USTALENIA.md` right away: move it to "Ustalone", remove it from
  "Do ustalenia", renumber — without asking.
- `docs/*.md` code snippets are design sketches; once firmware exists, headers win.

## Build Commands

```bash
# PWM / gpio_hold test (XIAO ESP32-C3) — AP "RL90-PWM-TEST" / "pwmtest123", captive portal GUI
cd pwm_test && pio run -t upload
pio device monitor                 # 115200 baud
```

The root project (`pio run` in repo root) still builds the thermostat for ESP32-S3 — not relevant for the lamp.

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
