#pragma once
#include "esp_err.h"
#include "orientation.h"
#include "schedule.h"
#include "settings.h"
#include "timetable.h"
#include "ui.h"

#define SPIFFS_ROOT "/spiffs"
#define ADHAN_FILE SPIFFS_ROOT "/adhan.mp3"
#define ADHAN_SUBUH_FILE SPIFFS_ROOT "/adhan_subuh.mp3"
#define TONE_FILE SPIFFS_ROOT "/tone.mp3"

/* app.c: shared state and the 4 Hz control loop */
void app_start(void);
settings_t app_settings(void);
bool app_apply_settings(const settings_t *s, char *err, size_t err_cap);
void app_stop_adhan(void);
void app_test_adhan(void);
void app_button_press(void);
void app_button_hold(void);
void app_power_short_press(void);
void app_fake_prayer(int minutes_ahead);
void app_set_stale(bool stale);
void app_set_mawaqit_name(const char *name);
/* Copies today's/tomorrow's times and status for the web API. */
void app_snapshot(ui_model_t *out);

/* clock.c */
void clock_init(void);
wallclock_t clock_now(void);
void clock_set_manual(date_t d, int32_t second_of_day);
void clock_mark_synced(void);

/* storage.c */
esp_err_t storage_init(void);
settings_t storage_load_settings(void);
void storage_save_settings(const settings_t *s);
adhan_mark_t storage_load_mark(void);
void storage_save_mark(adhan_mark_t m);
bool storage_load_year(const char *zone, int16_t year, year_table_t *out);
bool storage_save_year(const year_table_t *t);
void storage_get_ap_pass(char *out, size_t cap);
esp_err_t storage_write_file_atomic(const char *path, const void *data, size_t len);

/* tables.c: the year tables in RAM */
void tables_init(void);
void tables_reload(const char *zone);
void tables_put(const year_table_t *t);
bool tables_has(int16_t year);
/* Runs fn with the timetable while holding the tables lock. */
void tables_with(void (*fn)(const timetable_t *tt, void *ctx), void *ctx);

/* fetch.c: e-solat / Mawaqit download task */
void fetch_start(void);
void fetch_kick(void);

/* audio.c */
esp_err_t audio_init(void);
bool audio_play(const char *path);
void audio_stop(void);
bool audio_is_playing(void);
void audio_set_volume(int volume);

/* net.c */
void net_start(void);
ui_net_t net_state(void);
void net_ip(char *out, size_t cap);
void net_ap_credentials(char *ssid, size_t ssid_cap, char *pass, size_t pass_cap);
esp_err_t net_set_wifi(const char *ssid, const char *pass);
void net_reset_wifi(void);
char *net_scan_json(void);

/* web.c */
void web_start(void);

/* console.c */
void console_start(void);

/* hw.c: GPIO18 key, AXP2101 PMU, QMI8658 orientation, brightness */
void hw_init_sensors(void);
void hw_start(void);
bool hw_battery(uint8_t *pct, bool *charging);
quarter_t hw_orientation_now(void); /* one reading, 0 if flat or unclear */
quarter_t hw_orientation(void);     /* last orientation held for a second */
bool hw_imu_read(int16_t acc[3], int16_t gyr[3]); /* raw: 8192 LSB/g, 32 LSB/dps */
void hw_set_brightness(int percent);

/* The BSP's bsp_display_lock() returns esp_lv_adapter_lock()'s esp_err_t as a
 * bool, so it reports false when the lock was taken. Use these instead;
 * tools/check.sh rejects bsp_display_lock in main/. */
void display_start(quarter_t initial);
void display_set_rotation(quarter_t q);
extern volatile bool display_touch_log;
bool display_lock(int timeout_ms);
void display_unlock(void);

/* screenshot over the console, used by tools/smoke.py */
void display_dump(void);
