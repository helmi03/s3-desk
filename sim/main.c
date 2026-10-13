#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "pomodoro.h"
#include "ui.h"

#define W 480
#define H 480

static uint8_t draw_buf[W * H * 2];
static int clipped;

static lv_point_t finger;
static bool finger_down;

static void finger_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    data->point = finger;
    data->state = finger_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void run_ms(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 16) {
        lv_tick_inc(16);
        lv_timer_handler();
    }
}

/* Drags a virtual finger from (x0, y0) to (x1, y1) in small steps, as a swipe. */
static void drag(int x0, int y0, int x1, int y1)
{
    finger = (lv_point_t){x0, y0};
    finger_down = true;
    for (int i = 0; i <= 20; i++) {
        finger.x = x0 + (x1 - x0) * i / 20;
        finger.y = y0 + (y1 - y0) * i / 20;
        lv_tick_inc(16);
        lv_timer_handler();
    }
    finger_down = false;
    run_ms(960);
}

static void tap(int x, int y)
{
    finger = (lv_point_t){x, y};
    finger_down = true;
    run_ms(96);
    finger_down = false;
    run_ms(96);
}

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL %s\n", msg);
    return 1;
}

static int32_t max_scroll_y(lv_obj_t *obj)
{
    int32_t best = lv_obj_get_scroll_y(obj);
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
        int32_t v = max_scroll_y(lv_obj_get_child(obj, (int32_t)i));
        best = v > best ? v : best;
    }
    return best;
}

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    lv_display_flush_ready(d);
}

static char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "missing %s\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc(*len);
    if (fread(b, 1, *len, f) != *len) {
        exit(1);
    }
    fclose(f);
    return b;
}

static year_table_t *load_year(const char *name, int16_t year)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/esolat/%s", FIXTURES, name);
    size_t len;
    char *json = read_file(path, &len);
    year_table_t *t = calloc(1, sizeof(*t));
    if (esolat_parse(json, len, "SGR03", year, t) != PARSE_OK) {
        fprintf(stderr, "parse failed %s\n", name);
        exit(1);
    }
    free(json);
    return t;
}

static void write_bmp(const char *path, lv_draw_buf_t *snap)
{
    FILE *f = fopen(path, "wb");
    uint32_t row = W * 3, size = 54 + row * H;
    uint8_t hdr[54] = {'B', 'M'};
    memcpy(hdr + 2, &size, 4);
    hdr[10] = 54;
    hdr[14] = 40;
    int32_t w = W, h = -H;
    memcpy(hdr + 18, &w, 4);
    memcpy(hdr + 22, &h, 4);
    hdr[26] = 1;
    hdr[28] = 24;
    fwrite(hdr, 1, 54, f);
    for (int y = 0; y < H; y++) {
        const uint8_t *src = snap->data + y * snap->header.stride;
        fwrite(src, 1, row, f); /* RGB888 in LVGL is stored B,G,R */
    }
    fclose(f);
}

static void check_clipping(lv_obj_t *obj, const char *shot, bool in_scroll)
{
    if (lv_obj_is_hidden(obj)) {
        return;
    }
    if (lv_obj_check_type(obj, &lv_label_class)) {
        lv_area_t a;
        lv_obj_get_coords(obj, &a);
        const char *text = lv_label_get_text(obj);
        if (text[0] && (a.x1 < 0 || a.x2 >= W || (!in_scroll && (a.y1 < 0 || a.y2 >= H)))) {
            fprintf(stderr, "CLIPPED in %s: \"%s\" at (%d,%d)-(%d,%d)\n", shot, text, (int)a.x1, (int)a.y1,
                    (int)a.x2, (int)a.y2);
            clipped++;
        }
        lv_point_t size;
        lv_text_get_size(&size, text, lv_obj_get_style_text_font(obj, 0), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (lv_label_get_long_mode(obj) != LV_LABEL_LONG_WRAP && size.x > lv_area_get_width(&a) + 1) {
            fprintf(stderr, "TRUNCATED in %s: \"%s\"\n", shot, text);
            clipped++;
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
        check_clipping(lv_obj_get_child(obj, (int32_t)i), shot, in_scroll || lv_obj_is_scrollable(obj));
    }
}

static void shot(const char *dir, const char *name)
{
    lv_refr_now(NULL);
    lv_obj_t *scr = lv_screen_active();
    check_clipping(scr, name, false);
    lv_draw_buf_t *snap = lv_snapshot_take(scr, LV_COLOR_FORMAT_RGB888);
    char bmp[512], cmd[1200];
    snprintf(bmp, sizeof(bmp), "%s/%s.bmp", dir, name);
    write_bmp(bmp, snap);
    lv_draw_buf_destroy(snap);
    snprintf(cmd, sizeof(cmd), "sips -s format png '%s' --out '%s/%s.png' >/dev/null && rm '%s'", bmp, dir, name, bmp);
    if (system(cmd) != 0) {
        exit(1);
    }
    printf("%s/%s.png\n", dir, name);
}

static ui_model_t model_at(const timetable_t *tt, date_t d, int h, int m, int s, theme_t theme)
{
    ui_model_t mo = {0};
    mo.settings = settings_defaults();
    mo.settings.theme = theme;
    snprintf(mo.settings.pin, sizeof(mo.settings.pin), "482913");
    mo.now = (wallclock_t){.valid = true, .date = d, .second = h * 3600 + m * 60 + s};
    mo.net = UI_NET_ONLINE;
    snprintf(mo.ip, sizeof(mo.ip), "192.168.1.42");
    snprintf(mo.version, sizeof(mo.version), "sim");
    ui_model_fill_times(&mo, tt, NULL);
    return mo;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "sim/shots";
    lv_init();
    lv_display_t *disp = lv_display_create(W, H);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_FULL);

    year_table_t *y2026 = load_year("SGR03-year.json", 2026);
    timetable_t tt = {.tables = {y2026}, .count = 1};
    date_t oct9 = {2026, 10, 9};
    static const char *const THEME_FILE[THEME_COUNT] = {"gelap", "hijau", "biru", "ungu"};
    static const char *const VIEW_FILE[UI_VIEW_COUNT] = {"senarai", "jam", "kiraan", "pomodoro"};

    lv_indev_t *pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer, finger_read);
    ui_init(NULL);
    char name[64];
    for (int t = 0; t < THEME_COUNT; t++) {
        ui_model_t m = model_at(&tt, oct9, 15, 30, 20, (theme_t)t);
        for (int v = 0; v < UI_VIEW_COUNT; v++) {
            ui_update(&m);
            ui_show_view((ui_view_t)v);
            snprintf(name, sizeof(name), "%s-%s", VIEW_FILE[v], THEME_FILE[t]);
            shot(dir, name);
        }
    }
    ui_show_view(UI_VIEW_LIST);

    ui_model_t m = model_at(&tt, (date_t){2027, 1, 5}, 21, 40, 0, THEME_GELAP);
    m.battery_present = true;
    m.battery_pct = 76;
    m.charging = true;
    m.net = UI_NET_CONNECTING;
    ui_update(&m);
    shot(dir, "senarai-anggaran");

    m = model_at(&tt, oct9, 16, 17, 5, THEME_GELAP);
    m.adhan_playing = true;
    m.adhan_prayer = P_ASAR;
    ui_update(&m);
    shot(dir, "azan");

    m = model_at(&tt, oct9, 0, 0, 0, THEME_GELAP);
    m.now.valid = false;
    m.net = UI_NET_SETUP;
    snprintf(m.ap_ssid, sizeof(m.ap_ssid), "WaktuSolat-D3E8");
    snprintf(m.ap_pass, sizeof(m.ap_pass), "solat4821");
    ui_update(&m);
    shot(dir, "persediaan");

    m.net = UI_NET_CONNECTING;
    ui_update(&m);
    shot(dir, "menunggu-masa");

    m = model_at(&tt, (date_t){2029, 3, 1}, 9, 0, 0, THEME_GELAP);
    ui_update(&m);
    shot(dir, "tiada-data");

    m = model_at(&tt, oct9, 15, 30, 20, THEME_GELAP);
    ui_update(&m);
    ui_open_settings(true);
    shot(dir, "tetapan");
    drag(240, 400, 240, 120);
    int32_t scrolled = max_scroll_y(lv_screen_active());
    shot(dir, "tetapan-bawah");
    ui_open_settings(false);
    if (scrolled < 100) {
        fprintf(stderr, "FAIL settings did not scroll on swipe up (scroll_y %d)\n", (int)scrolled);
        return 1;
    }
    printf("settings scrolls on swipe up (scroll_y %d)\n", (int)scrolled);

    /* One swipe moves exactly one screen, through all four and back. */
    ui_update(&m);
    ui_show_view(UI_VIEW_LIST);
    for (int i = 1; i <= UI_VIEW_COUNT; i++) {
        drag(400, 240, 80, 240);
        if (ui_current_view() != (ui_view_t)(i % UI_VIEW_COUNT)) {
            return fail("swipe left did not move one screen forward");
        }
    }
    drag(80, 240, 400, 240);
    if (ui_current_view() != (ui_view_t)(UI_VIEW_COUNT - 1)) {
        return fail("swipe right did not move one screen back");
    }
    printf("swipes step one screen at a time\n");

    /* Pomodoro: start, let focus run out, expect the silent alert. */
    ui_show_view(UI_VIEW_POMODORO);
    tap(240, 426); /* Mula */
    run_ms(POMO_FOCUS_S * 1000u + 1000);
    if (!ui_pomodoro_alert_shown()) {
        return fail("no alert when focus ran out");
    }
    run_ms(300); /* mid-pulse */
    shot(dir, "pomodoro-tamat");
    tap(240, 240);
    if (ui_pomodoro_alert_shown() || ui_current_view() != UI_VIEW_POMODORO) {
        return fail("tapping the alert did not close it");
    }
    shot(dir, "pomodoro-rehat");
    printf("pomodoro alert shows and closes\n");

    /* With the screen off LVGL's timers are paused and the firmware polls instead. */
    tap(112, 426); /* reset: stops the break */
    tap(368, 426); /* skip to focus */
    if (ui_pomodoro_poll() != UINT32_MAX) {
        return fail("poll reports time left on a stopped pomodoro");
    }
    tap(240, 426); /* Mula */
    uint32_t left = ui_pomodoro_poll();
    if (left > POMO_FOCUS_S * 1000u || left < POMO_FOCUS_S * 1000u - 1000) {
        fprintf(stderr, "FAIL poll after start returned %u ms, want about 25 min\n", (unsigned)left);
        return 1;
    }
    lv_tick_inc(left + 1000);
    ui_pomodoro_poll();
    if (!ui_pomodoro_alert_shown()) {
        return fail("poll past the end of focus did not show the alert");
    }
    tap(240, 240);
    printf("pomodoro poll counts down and alerts without LVGL timers\n");

    if (clipped) {
        fprintf(stderr, "%d clipped or truncated labels\n", clipped);
        return 1;
    }
    printf("no clipped text\n");
    return 0;
}
