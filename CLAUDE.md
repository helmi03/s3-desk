# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

ESP-IDF 5.5 firmware for the Waveshare ESP32-S3-Touch-AMOLED-2.16 (480×480 CO5300 AMOLED, CST9217 touch, QMI8658
IMU, ES8311 audio, AXP2101 PMU). A desk display of swipeable LVGL 9 screens: Malaysian prayer times from JAKIM
e-Solat with azan, an analog clock, a countdown and a Pomodoro timer. All user-facing text is Malay.

## Commands

```sh
tools/check.sh                 # everything: lint, host tests, simulator, firmware build (set -e, stops on first failure)
tools/check.sh --device        # also flashes and runs tools/smoke.py (PORT=/dev/cu.usbmodemXXXX)

# host unit tests (Unity, ASan+UBSan, -Werror)
cmake -S test -B test/build -G Ninja && ninja -C test/build && ./test/build/core_tests

# simulator: renders every screen to sim/shots/*.png and fails on clipped labels or broken swipe/scroll/alert checks
cmake -S sim -B sim/build -G Ninja && ninja -C sim/build && ./sim/build/sim sim/shots

# firmware
source ~/esp/esp-idf/export.sh
idf.py build
idf.py -p /dev/cu.usbmodem* app-flash   # app only; `flash` also writes the storage partition built from data/
```

- There is no single-test filter: `test/test_main.c` runs every `RUN_TEST` in `main()`. Comment out the others temporarily.
- Python tools need pyserial/pillow and are run with uvx: `uvx --with pyserial python tools/devcmd.py --port $P status "view 3"`.
- `idf.py build` fails on warnings like `-Wformat-truncation` that the host and simulator builds don't flag, so always run it before flashing.
- The USB port name changes between sockets (`ls /dev/cu.usbmodem*`).

## Architecture

Three layers, so most logic runs and is tested on the Mac:

- **`components/core/`**: pure C with no ESP-IDF or LVGL. It is built both as an IDF component and as a host
  library (`CMakeLists.txt` branches on `ESP_PLATFORM`; on the host it compiles IDF's cJSON source directly).
  It holds:
  - the e-Solat and Mawaqit parsers and the year-table blob format ("PTT1" with a CRC)
  - `timetable_day`, which falls back to the same date in the previous year when the current year is missing
    (within ±1 minute on the measured data)
  - schedule and azan-due logic
  - settings JSON
  - orientation and touch mapping
  - the Pomodoro state machine
  - the Wi-Fi schedule
  - the generated zone table
- **`components/ui/`**: all LVGL code (`ui.c`), shared by the firmware and the simulator.
  - It renders a `ui_model_t` snapshot pushed through `ui_update()`.
  - It talks back only through `ui_actions_t` callbacks, which run in the LVGL task, must not block, and must
    not take the app's `lock`.
  - `ui_mode_for()` chooses the mode: the azan overlay first, then settings, Wi-Fi setup, waiting for time, no
    data, then the normal view. Within the normal view, `ui.view` cycles through `UI_VIEW_*`.
- **`main/`**: the firmware.
  - `app.c` holds the shared state under a mutex. Its 250 ms loop builds the model, runs the azan check and
    brightness, applies rotation, then calls `ui_update()` under `display_lock()`.
  - The other modules are `display.c`, `hw.c` (button, PMU and IMU over raw I2C), `net.c` (STA/SoftAP/captive
    DNS/SNTP/mDNS), `fetch.c`, `web.c` (REST plus the embedded `index.html`, protected by an X-PIN header),
    `storage.c` (NVS and SPIFFS) and `console.c`.

### Hardware facts that bit before

- **Display lock:** `bsp_display_lock()` returns an `esp_err_t` cast to `bool`, so success reads as false. Use
  `display_lock()` and `display_unlock()` from `main/display.c`; `check.sh` lints for the BSP calls.
- **Rotation:** the CO5300 ignores the MADCTL row/column swap. 180° is done by panel mirroring; 90° and 270° are
  rotated in software in the adapter's draw-bitmap hook. Flush areas must start on an even pixel and end on an
  odd one.
- **Touch frame:** the CST9217 raw frame `(480 - y, x)` lines up with the buttons-on-top orientation (q=3), not
  the panel's native one. See `TOUCH_FRAME_QUARTER` in `orientation.c`.
- **Orientation mapping:** measured on the board, accel y+ → 3, x− → 2, y− → 1, x+ → 0. A reading must hold for 1 s.
- **Draw buffers:** they live in internal DMA RAM. PSRAM buffers caused `ESP_ERR_NO_MEM` on flush.
- **LVGL events:** a swipe still ends in `SHORT_CLICKED`. The gesture handler calls `lv_indev_wait_release()`
  so one swipe moves exactly one screen. `lv_obj_clean()` keeps the screen's own event callbacks, so they're
  registered once in `ui_init()`, never in `build_all()`, which reruns on every theme change. Use LVGL 9.6's dedicated setters and getters (`lv_obj_set_hidden`,
  `lv_obj_is_hidden`); the generic flag API is deprecated.
- **Low power:** it is active while the screen is off. `main/power.c` switches it, and only the app loop calls
  it. `lv_tick_set_cb` reads `esp_timer`, so LVGL time keeps running while the adapter is paused.
  - The panel uses plain sleep-in (0x10), not the driver's deep standby, whose wake reruns an init with
    1.2 s of delays. On wake the frame is drawn while the panel is still off, then 0x29 turns it on
    (about 220 ms in total).
  - The touch chip must be reset before LVGL resumes, because the driver's failed-read recovery blocks the
    LVGL task.
  - The BSP enables both I2S channels at init. `audio.c` opens and closes the mic, then the speaker, to
    release their PM locks.
  - Light sleep drops the USB serial link, so wake the screen before flashing.
- **Clock and Wi-Fi:**
  - Time comes from the PCF85063 RTC at I2C 0x51, which keeps time through resets. It is stored in UTC and
    written on every SNTP sync.
  - With the screen off, the system clock is trimmed back to the RTC on wake and every 10 minutes, because light
    sleep runs it off the RC oscillator.
  - The station radio follows `wifi_policy.c`, driven by `net_tick()` from the app loop:
    - It turns on for a sync every 6 hours, for a due fetch (weekly, a missing year, or changed settings), and
      while Tetapan or the web page is in use, plus 10 minutes after.
    - A failed attempt gives up after 120 s and waits 30 minutes before the next.
  - Board I2C addresses: 0x18 ES8311, 0x34 AXP2101, 0x40 ES7210, 0x51 PCF85063, 0x5A CST9217, 0x6B QMI8658,
    and an unidentified device at 0x56. `i2cscan` and `i2cget` are on the console.

### Data and assets

- **e-Solat API:** `esolatApi/TakwimSolat` only publishes the current year. Month names are Malay abbreviations,
  including "Ogos". `NO_RECORD!` comes back as HTTP 200 with an object instead of a list. Real responses are in
  `fixtures/esolat/` (refetch with `tools/fetch_esolat.sh`).
- **Preloaded year tables:** `data/t-<zone>-<year>.bin` are built with `test/build/mkcache <json> <zone> <year> <out>`.
- **Storage image:** `data/` becomes the SPIFFS storage image (azan MP3, tone, cached tables).
- **Azan marker:** saved to NVS before playback, so a reboot never replays a prayer's azan.
- **Fonts:**
  - Paper Mono is generated by `tools/gen_fonts.sh` into `components/ui/fonts/font_*.c`. Never edit those by hand.
  - Only the Font Awesome glyphs the UI uses are merged in: the dropdown arrow at 18 px, refresh and next at 22 px.
  - Montserrat 14 stays enabled only as LVGL's default font.
  - Adding an `LV_SYMBOL_*` means adding its code point in `gen_fonts.sh`.
- **New screens:** add a `UI_VIEW_*` entry, a build/update pair in `ui.c`, and an entry in the simulator's
  `VIEW_FILE` table.
