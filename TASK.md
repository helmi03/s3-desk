# Task: Prayer Times Display (JAKIM e-solat) on Waveshare ESP32-S3-Touch-AMOLED-2.16

Source reference: https://github.com/r1di/mawaqit-prayer-display (commit `5475311`, 2026-01-28, MIT).
Target device: Waveshare ESP32-S3-Touch-AMOLED-2.16 (inspected 2026-10-09).

## Goal

A standalone prayer-times display on the 2.16" round-cornered 480×480 AMOLED: fetches official JAKIM times for
a Malaysian zone (default **SGR03**, Klang) from e-solat, shows them in a few touch-switchable views, and plays the
Adhan through the onboard speaker. Mawaqit is an optional, user-enabled source for mosque-specific times.
Port the *features*, not the code: the reference is tightly coupled to a different board and has
several unfinished parts (see "Reference gaps").

## Hardware delta

| | Reference (JC4827W543) | Target (ESP32-S3-Touch-AMOLED-2.16) |
|---|---|---|
| Display | 4.3" IPS 480×272, NV3041A, QSPI, PWM backlight | 2.16" AMOLED 480×480, CO5300, QSPI, no backlight (brightness via panel cmd) |
| Touch | GT911 @0x5D | CST9217 (I2C) |
| Flash / PSRAM | 4 MB / 8 MB | 16 MB / 8 MB octal |
| Audio | Placeholder (“DAC pin 25”, never implemented) | ES8311 codec + speaker amp, ES7210 2-mic ADC |
| Power | USB only | AXP2101 PMU, battery-capable, PWR key |
| Inputs | Touch only | Touch + BOOT (GPIO0) + user key (GPIO18) + PWR key (via PMU) |
| Extras | — | QMI8658 IMU, microSD. No RTC found (AXP2101 has none): time is lost on power-off until SNTP |

Pin source of truth: Waveshare BSP `waveshare/esp32_s3_touch_amoled_2_16` ^2.0.1 (includes the v1.0.1
fixes: LCD RST GPIO39, touch RST GPIO40, SD CLK GPIO2, ES7210 MCLK GPIO42). Do not copy pins from the
reference repo.

## Reference gaps (do not port as-is)

1. **Adhan never plays.** `audio_player.cpp` is a stub; `/api/adhan/play` only shows a screen. Nothing in
   `loop()` checks prayer times, so there is no automatic Adhan trigger at all.
2. **Times never refresh.** `fetchPrayerTimes()` runs only at boot / mosque change; `PRAYER_TIMES_REFRESH`
   is unused. After midnight the display shows yesterday's times.
3. **Wrong mosque lookup.** Times are fetched via `mosque/search?word=<first 10 chars of name>` and fall back
   to the *first* result if the UUID isn't matched, which can silently show another mosque's times.
4. **Hard-coded timezone.** `configTime(3600, 3600, ...)` = CET with fixed DST.
5. **Dead code.** `mawaqit_api.cpp`, `web_server.cpp` (AsyncWebServer), `prayer_scheduler.cpp` are not used
   by `main.cpp` (which uses the sync `WebServer` and inline logic).
6. **Insecure defaults.** TLS verification disabled (`setInsecure()`), fixed AP password `12345678`.
7. **Arabic not rendered.** Built-in 5×7 GFX font is ASCII only; “Arabic” language shows Latin text.
8. **Layout is pixel-hard-coded** for 480×272 (touch hit boxes in `loop()` use literal coordinates).
9. **Data source is gated.** On 2026-10-09 `GET https://mawaqit.net/api/2.0/mosque/search?word=paris` with
   `Accept: application/json` returned `401 {"message":"Unauthorized"}` (same for `?lat=&lon=`; `/api/3.0` is 404).
   The reference's only data source no longer works without a token.

## Primary data source: JAKIM e-solat (probed 2026-10-09)

`GET https://www.e-solat.gov.my/index.php?r=esolatApi/TakwimSolat&period=<today|week|month|year>&zone=<ZONE>`
(`period=duration` is a POST with `datestart`/`dateend`, `YYYY-MM-DD`). No auth, `Access-Control-Allow-Origin: *`,
TLS 1.3 via CloudFront, chain to Amazon Root CA 1 (in the ESP-IDF cert bundle). Klang = zone **SGR03**
(“Klang, Kuala Langat”); zone list is in the `<option>`s of `?siteId=24&pageId=24`.

```json
{"prayerTime":[{"hijri":"1448-04-27","date":"09-Okt-2026","day":"Friday","imsak":"05:42:00","fajr":"05:52:00",
"syuruk":"06:58:00","dhuha":"07:23:00","dhuhr":"13:04:00","asr":"16:17:00","maghrib":"19:05:00","isha":"20:14:00"}],
"status":"OK!","serverTime":"2026-10-09 21:50:13","periodType":"today","lang":"ms_my","zone":"SGR03", ...}
```

| period | size | latency |
|---|---|---|
| today | ~0.3 KB | 0.12 s |
| month | 6.7 KB, 31 rows | 0.25 s |
| year | 77 KB, 365 rows | 0.28 s |

Parsing rules (boundary):
- `date` uses Malay month abbreviations (`Okt`, `Dis`, ...); times are `HH:MM:SS` local (MYT, UTC+8, no DST).
- Errors return HTTP 200 with `status:"NO_RECORD!"` and `prayerTime` as an **object**, not an array. Check `status`.
- `hijri` is provided (official JAKIM calendar). This removes the Hijri computation need.
- Data is published per calendar year: as of 2026-10-09, any 2027 range returns `NO_RECORD!`, and a range crossing
  Dec 31 is silently truncated to Dec 31.

Device strategy: fetch `month` (or `year` into LittleFS) on boot and daily; keep ≥ today+tomorrow cached. Near year
end, if next year is not published, fall back to local calculation with JAKIM-like parameters (commonly cited as Fajr 20°,
Isha 18°, Shafi'i Asr, +ihtiyat; unverified, tune against the cached official year until minutes match) and mark times as “calculated”. Unofficial/undocumented endpoint: no published terms or SLA.

## Optional data source: Mawaqit

Off by default. Enabled in settings by entering a Mawaqit API token and mosque UUID (the public API returns 401
without a token, see gap 9). Fetch times by UUID only, never by name search. If the token is missing or rejected,
show the error in settings and fall back to e-solat for the configured zone. A mosque may be outside MYT,
so TZ becomes a setting only when this source is active.

Both sources parse into the same internal day record (date, Hijri date, Imsak, Fajr, Syuruk, Dhuhr, Asr, Maghrib,
Isha, origin = official | mawaqit | calculated). The scheduler and UI read only that record.

## Tech decisions

- **ESP-IDF v5.5.4 + Waveshare BSP ^2.0.1 + LVGL v9** (BSP pulls `esp_lcd_co5300`, `esp_lcd_touch_cst9217`,
  `esp_codec_dev` ~1.5 for ES8311/ES7210, `esp_lvgl_adapter` ~0.6). It does **not** include a PMU or IMU driver:
  vendor `XPowersLib` from Waveshare's `examples/esp-idf/01_AXP2101/components/` for the AXP2101.
  Arduino/PlatformIO is possible but drops the maintained BSP; avoid.
- UI language: Malay only, no language setting and no translation layer. Strings live in one `ui/strings_ms.h`.
  Hijri months use Malay names (Muharam, Safar, Rabiulawal, Rabiulakhir, Jamadilawal, Jamadilakhir, Rejab,
  Syaaban, Ramadan, Syawal, Zulkaedah, Zulhijah); Gregorian dates use Malay day and month names.
- LVGL fonts generated with `lv_font_conv`: a Latin subset (e.g., Noto Sans) at the few sizes the screens use.
  No Arabic glyphs, bidi, or shaping.
- Audio: `esp_audio_codec`/`esp-audio-player` MP3 decode → BSP codec device (ES8311).
- Storage: NVS for settings, LittleFS (or SPIFFS) partition for Adhan MP3 + web UI + cached times.
- Time: SNTP with fixed TZ `MYT-8` (Malaysia has no DST). A TZ setting appears only when Mawaqit is the source.

### Proposed partition table (16 MB)

| Name | Type | Size |
|---|---|---|
| nvs | data/nvs | 24K |
| otadata | data/ota | 8K |
| phy_init | data/phy | 4K |
| ota_0 | app | 4M |
| ota_1 | app | 4M |
| storage | data/littlefs | ~7.9M (Adhan MP3s, web UI, fonts if not embedded, cached times) |

Back up the factory image before first flash (restore image: Waveshare repo
`firmware/ESP32-S3-Touch-AMOLED-2.16-FactoryOnly-260318.bin`, verified identical to device on 2026-10-09).

## Code layout

- `components/core/` is plain C/C++ with no ESP-IDF or LVGL dependency. It holds the day record, the e-solat and
  Mawaqit parsers, the fallback calculation, the cache format, and the scheduler. It builds for the device and for
  the host.
- `components/ui/` holds the LVGL screens. It talks to the hardware only through a small `ui_model` struct
  (times, now, next prayer, Adhan state), so the same code runs on the device and in the simulator.
- `main/` wires the BSP, Wi-Fi, SNTP, storage, audio, and buttons to `core` and `ui`.
- `sim/` is the LVGL SDL desktop simulator on macOS. It renders `ui` with fixture data and writes PNG screenshots
  (`lv_snapshot`) for every view, which is how UI acceptance is checked.
- `test/` holds host unit tests for `core` (Unity), run against the Phase 0 fixtures.

## Adhan trigger contract

- An Adhan for (date, prayer) fires at most once. Before playback starts, write `last_adhan = <YYYY-MM-DD>/<prayer>`
  to NVS. On every tick and after every boot, skip any (date, prayer) at or before that marker.
- Fire only inside the prayer's minute. A device that boots or syncs time after that minute does not play late.
- Never fire before the first successful SNTP sync (no RTC on this board).

## Release 1 (MVP): one screen, correct times, Adhan on time

Goal: the device sits on a desk in Klang, shows today's official times, and plays the Adhan on time, for days
without attention. Everything else waits for Release 2.

### Phase 0 — Data and fallback
- Save real e-solat responses as test fixtures: SGR03 `year`, `month`, a `NO_RECORD!` response, and a `duration`
  crossing Dec 31.
- Pick the fallback calculation library and tune JAKIM parameters against the SGR03 year fixture.
- ✅ Done when: `tools/fetch_esolat.sh SGR03` saves the fixtures, and the fallback reproduces every SGR03 time
  in the fixture within ±1 minute (script prints the max deviation).

### Phase 1 — Skeleton and hardware smoke test
- New ESP-IDF project: BSP dependency, partition table above, sdkconfig (PSRAM octal 80 MHz, 16 MB flash,
  LVGL in PSRAM), XPowersLib for the AXP2101.
- Display + touch + LVGL text, PMU init, brightness control.
- Audio path: a short tone and a short MP3 from LittleFS through ES8311 to the speaker, at a set volume.
- Wi-Fi from `menuconfig` credentials (development only) and SNTP sync with `MYT-8`.
- GPIO18 key read.
- ✅ One boot shows text, logs touch coordinates across the full 480×480 area, plays the tone and the MP3
  audibly, logs the GPIO18 press, and logs the correct local time.

### Phase 2 — Core logic on the host
- `core`: day record, e-solat parser, LittleFS cache format, fallback, scheduler (current and next prayer,
  countdown), and the Adhan trigger contract above.
- ✅ Host unit tests on the fixtures pass: Malay months, `HH:MM:SS`, the `NO_RECORD!` object shape, year
  rollover with and without next-year data, Isyak→Subuh next day, Syuruk never treated as a prayer, the
  once-per-prayer marker across a simulated reboot inside the prayer minute, and no Adhan before time sync.

### Phase 3 — One screen and the Adhan on the device
- Fetch `period=year` for SGR03 into LittleFS with TLS verified against the ESP-IDF certificate bundle. Refresh
  daily after midnight, retry with backoff, and show a stale-data marker when offline.
- One list view in True-Dark, all text in Malay: Imsak, Subuh, Syuruk, Zohor, Asar, Maghrib,
  Isyak, current prayer highlighted, Gregorian and Hijri date, zone name, countdown to the next prayer, and a
  "calculated" badge when relevant. Build it in `sim/` first.
- Adhan from one built-in MP3 at every prayer except Syuruk; an Adhan screen while playing; stop by tap or GPIO18.
- Debug hook: a serial command that sets a fake prayer time N minutes ahead.
- ✅ Simulator screenshot of the view with fixture data.
- ✅ On the device: the fake prayer plays the Adhan once; a reboot inside that minute does not replay it; tap
  and GPIO18 each stop playback.
- ✅ A 48-hour desk run covers a midnight refresh and every real prayer, checked against the serial log.

## Release 2: setup, settings, and polish

Start only after Release 1 passes on the desk.

### Phase 4 — Setup and settings
- First boot: SoftAP + captive portal (random AP password shown on screen as a QR code) for Wi-Fi. Remove the
  `menuconfig` credentials.
- Settings in NVS: Wi-Fi, JAKIM zone (picker from the e-solat zone list, default SGR03), per-prayer Adhan
  toggles, volume, brightness, night dimming.
- Settings screen via long-press (≥1.5 s) using LVGL widgets (no pixel hit boxes).
- Web UI (esp_http_server): status, times, settings, zone list, Wi-Fi scan, MP3 upload (size-limited, optional
  separate Fajr Adhan), test and stop Adhan, reboot, rotate. Pairing PIN on mutating endpoints.
- ✅ A wiped device is configured from a phone and reaches Release 1 behavior without a serial cable.

### Phase 5 — More views and themes
- Clock view (analog clock with next prayer and countdown) and countdown view; swipe or tap to switch.
- Themes: Green, Blue, Purple, True-Dark; AMOLED burn-in mitigation (pixel shift every few minutes, dimmed
  static elements).
- ✅ Simulator screenshots of every view in every theme, with no clipped text.

### Phase 6 — OTA and Mawaqit
- OTA update via web upload into ota_1/ota_0 with rollback.
- Mawaqit adapter (token + mosque UUID) into the same day record, mosque search endpoints, TZ setting.
- ✅ An OTA update from the phone succeeds, and a deliberately broken image rolls back. With a valid token,
  Mawaqit times show with the mosque name; with a bad token, settings shows the error and e-solat stays active.

### Phase 7 — Hardware extras (optional)
- PWR short-press = stop Adhan / screen on-off (via AXP2101 IRQ). Avoid BOOT (GPIO0/CHIP_PU) for regular UI.
- Battery level icon via AXP2101; IMU auto-rotate (QMI8658).

## Out of scope
- Porting the reference `Arduino_GFX`/`TouchLib` code directly.
- Iqama times, announcements, Jumuah — candidates for a later task.
- Any UI language other than Malay (English, Arabic, others); Arabic script rendering.

## Open questions
1. ~~Data source~~ Decided 2026-10-09: e-solat primary (zone SGR03), Mawaqit optional, calculated fallback.
2. ~~Language~~ Decided 2026-10-09: Malay only, no other languages.
3. Battery operation needed (affects sleep/brightness strategy)?
