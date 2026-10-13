#include <stdio.h>
#include <string.h>

#include "app.h"
#include "bsp/esp-bsp.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "app";

#define DIM_BRIGHTNESS 30

static SemaphoreHandle_t lock;

static struct {
    settings_t settings;
    adhan_mark_t mark;
    bool adhan_playing;
    prayer_t adhan_prayer;
    bool stale;
    char mawaqit_name[64];
    int16_t loaded_year;
    bool reload_tables;
    bool screen_off;
    /* serial "fake N": today's next Adhan prayer moved to this minute, for testing */
    bool fake_active;
    date_t fake_date;
    prayer_t fake_prayer;
    uint16_t fake_minute;
    ui_model_t model;
} st;

/* Set from the LVGL task, which must not take `lock`; the loop applies it. */
static volatile bool wake_requested;
static TaskHandle_t loop_handle;

/* The loop sleeps up to 10 s with the screen off; this makes it react at once. */
static void notify_loop(void)
{
    if (loop_handle) {
        xTaskNotifyGive(loop_handle);
    }
}

static void with_lock(void (*fn)(void *), void *ctx)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    fn(ctx);
    xSemaphoreGive(lock);
}

settings_t app_settings(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    settings_t s = st.settings;
    xSemaphoreGive(lock);
    return s;
}

bool app_apply_settings(const settings_t *s, char *err, size_t err_cap)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    settings_t old = st.settings;
    settings_t n = *s;
    snprintf(n.pin, sizeof(n.pin), "%s", old.pin);
    st.settings = n;
    bool data_changed = strcmp(old.zone, n.zone) != 0 || old.source != n.source ||
                        strcmp(old.mawaqit_uuid, n.mawaqit_uuid) != 0 || strcmp(old.mawaqit_token, n.mawaqit_token) != 0;
    if (data_changed) {
        st.reload_tables = true;
        st.stale = false;
        st.mawaqit_name[0] = '\0';
    }
    xSemaphoreGive(lock);
    storage_save_settings(&n);
    audio_set_volume(n.volume);
    ESP_LOGI(TAG, "settings saved (zone %s, volume %u, theme %d)", n.zone, n.volume, n.theme);
    if (data_changed) {
        fetch_kick();
    }
    return true;
}

static void stop_locked(void *ctx)
{
    st.adhan_playing = false;
}

void app_stop_adhan(void)
{
    audio_stop();
    with_lock(stop_locked, NULL);
    ESP_LOGI(TAG, "ADHAN stop");
}

void app_test_adhan(void)
{
    if (audio_play(ADHAN_FILE)) {
        xSemaphoreTake(lock, portMAX_DELAY);
        st.adhan_playing = true;
        st.adhan_prayer = st.model.sched.next;
        xSemaphoreGive(lock);
        ESP_LOGI(TAG, "ADHAN test");
    }
}

void app_button_press(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    bool playing = st.adhan_playing;
    st.screen_off = false;
    xSemaphoreGive(lock);
    notify_loop();
    if (playing) {
        app_stop_adhan();
        return;
    }
    if (display_lock(500)) {
        ui_step_view(1);
        display_unlock();
    }
}

void app_button_hold(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    bool playing = st.adhan_playing;
    st.screen_off = false;
    xSemaphoreGive(lock);
    notify_loop();
    if (playing) {
        app_stop_adhan();
        return;
    }
    if (display_lock(500)) {
        ui_open_settings(ui_current_mode() != UI_MODE_SETTINGS);
        display_unlock();
    }
}

void app_power_short_press(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    bool playing = st.adhan_playing;
    if (!playing) {
        st.screen_off = !st.screen_off;
    }
    xSemaphoreGive(lock);
    notify_loop();
    if (playing) {
        app_stop_adhan();
    }
}

void app_set_screen(bool on)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    st.screen_off = !on;
    xSemaphoreGive(lock);
    notify_loop();
}

void app_fake_prayer(int minutes_ahead)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    wallclock_t now = clock_now();
    st.fake_active = now.valid && st.model.have_today;
    st.fake_date = now.date;
    st.fake_prayer = st.model.sched.next;
    st.fake_minute = (uint16_t)((now.second / 60 + minutes_ahead) % (24 * 60));
    xSemaphoreGive(lock);
    ESP_LOGI(TAG, "FAKE %s at minute %u (%s)", PRAYERS[st.fake_prayer].name, st.fake_minute,
             st.fake_active ? "armed" : "ignored, no time or data");
}

void app_set_stale(bool stale)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    st.stale = stale;
    xSemaphoreGive(lock);
}

void app_set_mawaqit_name(const char *name)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    snprintf(st.mawaqit_name, sizeof(st.mawaqit_name), "%s", name);
    xSemaphoreGive(lock);
}

void app_snapshot(ui_model_t *out)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    *out = st.model;
    xSemaphoreGive(lock);
}

static bool apply_fake(ui_model_t *m)
{
    if (!st.fake_active || !m->have_today || date_cmp(st.fake_date, m->now.date) != 0) {
        return false;
    }
    m->today.minute[st.fake_prayer] = st.fake_minute;
    return true;
}

static void fill_times(const timetable_t *tt, void *ctx)
{
    ui_model_t *m = ctx;
    ui_model_fill_times(m, tt, st.mawaqit_name);
    if (apply_fake(m)) {
        day_times_t tomorrow;
        bool have = timetable_day(tt, date_add(m->now.date, 1), &tomorrow);
        m->sched = schedule_at(&m->today, have ? &tomorrow : NULL, m->now);
    }
}

static void build_model(ui_model_t *m, wallclock_t now)
{
    *m = (ui_model_t){0};
    m->now = now;
    m->settings = st.settings;
    m->stale = st.stale;
    m->adhan_playing = st.adhan_playing;
    m->adhan_prayer = st.adhan_prayer;
    m->net = net_state();
    net_ip(m->ip, sizeof(m->ip));
    net_ap_credentials(m->ap_ssid, sizeof(m->ap_ssid), m->ap_pass, sizeof(m->ap_pass));
    m->battery_present = hw_battery(&m->battery_pct, &m->charging);
    snprintf(m->version, sizeof(m->version), "%s", esp_app_get_description()->version);
    tables_with(fill_times, m);
    m->dimmed = m->settings.night_dim && m->have_today && night_dim_at(&m->today, now) && !m->adhan_playing;
}

static void check_adhan(ui_model_t *m)
{
    prayer_t p;
    if (st.adhan_playing || !m->have_today || !adhan_due(&m->today, m->now, st.settings.adhan_mask, st.mark, &p)) {
        return;
    }
    /* Persist the mark before playing, so a crash or reboot mid-Adhan never replays it. */
    st.mark = (adhan_mark_t){.date = m->now.date, .prayer = p};
    storage_save_mark(st.mark);
    const char *file = ADHAN_FILE;
    FILE *f = p == P_SUBUH ? fopen(ADHAN_SUBUH_FILE, "rb") : NULL;
    if (f) {
        fclose(f);
        file = ADHAN_SUBUH_FILE;
    }
    st.adhan_playing = audio_play(file);
    st.adhan_prayer = p;
    st.screen_off = false;
    m->adhan_playing = st.adhan_playing;
    m->adhan_prayer = p;
    ESP_LOGI(TAG, "ADHAN start %s %04d-%02d-%02d %s", PRAYERS[p].name, m->now.date.year, m->now.date.month,
             m->now.date.day, st.adhan_playing ? "playing" : "AUDIO FAILED");
}

static int brightness_for(const ui_model_t *m)
{
    int b = m->settings.brightness;
    return m->dimmed && b > DIM_BRIGHTNESS ? DIM_BRIGHTNESS : b;
}

/* Last value sent to the panel, -1 when unknown. */
static int shown_brightness = -1;

static void show_brightness(int b)
{
    if (b != shown_brightness) {
        hw_set_brightness(b);
        shown_brightness = b;
    }
}

static void mark_app_valid_after(int64_t ticks)
{
    static bool done;
    if (done || xTaskGetTickCount() < ticks) {
        return;
    }
    done = true;
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "new firmware marked valid");
    }
}

static void loop_task(void *arg)
{
    static ui_model_t m;
    power_mode_t applied = POWER_ACTIVE;
    for (;;) {
        wallclock_t now = clock_now();
        bool reload = false;
        char zone[8];
        xSemaphoreTake(lock, portMAX_DELAY);
        if (now.valid && (st.loaded_year != now.date.year || st.reload_tables)) {
            st.loaded_year = now.date.year;
            st.reload_tables = false;
            reload = true;
            snprintf(zone, sizeof(zone), "%s", st.settings.source == SOURCE_MAWAQIT ? "MAWAQIT" : st.settings.zone);
        }
        xSemaphoreGive(lock);
        if (reload) {
            tables_reload(zone);
            fetch_kick();
        }

        bool playing = audio_is_playing();
        xSemaphoreTake(lock, portMAX_DELAY);
        if (st.adhan_playing && !playing) {
            st.adhan_playing = false;
            ESP_LOGI(TAG, "ADHAN finished");
        }
        if (wake_requested) {
            wake_requested = false;
            st.screen_off = false;
        }
        build_model(&m, now);
        check_adhan(&m);
        power_mode_t want = st.screen_off ? POWER_LOW : POWER_ACTIVE;
        int brightness = brightness_for(&m);
        st.model = m;
        xSemaphoreGive(lock);

        if (want != applied) {
            if (want == POWER_LOW) {
                show_brightness(0);
            }
            power_set_low(want == POWER_LOW);
            /* Deep standby lost register 0x51, so the brightness below must be re-sent. */
            shown_brightness = want == POWER_LOW ? 0 : -1;
            applied = want;
        }

        TickType_t wait = pdMS_TO_TICKS(250);
        if (applied == POWER_ACTIVE) {
            display_set_rotation(m.settings.auto_rotate ? hw_orientation() : m.settings.rotation);
            if (display_lock(200)) {
                ui_update(&m);
                display_unlock();
            }
            show_brightness(brightness);
        } else {
            uint32_t pomo_ms = UINT32_MAX;
            if (display_lock(200)) {
                pomo_ms = ui_pomodoro_poll();
                display_unlock();
            }
            net_low_power_tick();
            wait = pdMS_TO_TICKS(pomo_ms < 10000 ? pomo_ms : 10000);
        }
        mark_app_valid_after(pdMS_TO_TICKS(30000));
        ulTaskNotifyTake(pdTRUE, wait);
    }
}

static void ui_stop(void)
{
    app_stop_adhan();
}

static void ui_apply(const settings_t *s)
{
    app_apply_settings(s, NULL, 0);
}

static void ui_reset_wifi(void)
{
    net_reset_wifi();
}

static void ui_pomodoro_done(void)
{
    wake_requested = true;
    notify_loop();
}

static const ui_actions_t ACTIONS = {
    .stop_adhan = ui_stop,
    .test_adhan = app_test_adhan,
    .apply_settings = ui_apply,
    .reset_wifi = ui_reset_wifi,
    .pomodoro_done = ui_pomodoro_done,
};

void app_start(void)
{
    lock = xSemaphoreCreateMutex();
    st.settings = storage_load_settings();
    st.mark = storage_load_mark();
    audio_set_volume(st.settings.volume);
    ESP_LOGI(TAG, "zone %s, last Adhan %04d-%02d-%02d %s", st.settings.zone, st.mark.date.year, st.mark.date.month,
             st.mark.date.day, st.mark.date.year ? PRAYERS[st.mark.prayer].name : "-");
    if (display_lock(1000)) {
        ui_init(&ACTIONS);
        display_unlock();
    }
    power_init();
    xTaskCreatePinnedToCore(loop_task, "app", 8192, NULL, 4, &loop_handle, 0);
}
