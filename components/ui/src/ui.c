#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "ms.h"
#include "ui.h"
#include "zones.h"

LV_FONT_DECLARE(font_digits_72);
LV_FONT_DECLARE(font_digits_120);

#define SCREEN 480
#define MARGIN 24
#define ROW_H 42
#define BURN_IN_PERIOD_S 180
#define ZONE_OPTS_MAX 4096

typedef struct {
    uint32_t bg, surface, text, muted, accent, on_accent;
} palette_t;

static const palette_t PALETTES[THEME_COUNT] = {
    [THEME_GELAP] = {0x000000, 0x161616, 0xF2F2F2, 0x8A8A8A, 0xF5B942, 0x000000},
    [THEME_HIJAU] = {0x000000, 0x0B1F17, 0xEAFBF3, 0x7FA595, 0x34D399, 0x00140C},
    [THEME_BIRU] = {0x000000, 0x0B1426, 0xEAF2FF, 0x7F93B5, 0x60A5FA, 0x00122B},
    [THEME_UNGU] = {0x000000, 0x170B24, 0xF6EEFF, 0x9F8AB5, 0xC084FC, 0x1A0030},
};

static const prayer_t LIST_ORDER[P_COUNT] = {P_IMSAK, P_SUBUH, P_SYURUK, P_ZOHOR, P_ASAR, P_MAGHRIB, P_ISYAK};

static struct {
    const ui_actions_t *actions;
    ui_model_t model;
    bool have_model;
    theme_t theme;
    ui_view_t view;
    bool settings_open;
    ui_mode_t mode;

    lv_obj_t *root;       /* shifted for burn-in protection */
    lv_obj_t *mode_obj[UI_MODE_NO_DATA + 1];
    lv_obj_t *view_obj[UI_VIEW_COUNT];

    /* list view */
    lv_obj_t *list_date, *list_hijri, *list_badge, *list_rows[P_COUNT], *list_names[P_COUNT], *list_times[P_COUNT];
    lv_obj_t *list_next, *list_footer;
    /* clock view */
    lv_obj_t *clock_scale, *clock_hour, *clock_min, *clock_sec, *clock_digital, *clock_date, *clock_next;
    /* countdown view */
    lv_obj_t *cd_label, *cd_name, *cd_time, *cd_remaining, *cd_footer;
    /* overlays */
    lv_obj_t *adhan_name, *adhan_time;
    lv_obj_t *setup_qr, *setup_text;
    lv_obj_t *waiting_text, *nodata_text;
    /* settings */
    lv_obj_t *set_switch[P_COUNT], *set_volume, *set_brightness, *set_dim, *set_theme, *set_zone, *set_info;
} ui;

static const palette_t *pal(void)
{
    return &PALETTES[ui.theme];
}

static lv_color_t col(uint32_t hex)
{
    return lv_color_hex(hex);
}

static void set_text(lv_obj_t *label, const char *text)
{
    const char *old = lv_label_get_text(label);
    if (!old || strcmp(old, text) != 0) {
        lv_label_set_text(label, text);
    }
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col(color), 0);
    lv_label_set_text(l, "");
    return l;
}

static lv_obj_t *panel(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, SCREEN, SCREEN);
    lv_obj_set_clickable(o, false);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_hidden(o, true);
    lv_obj_set_event_bubble(o, true);
    return o;
}

ui_mode_t ui_mode_for(const ui_model_t *m, bool settings_open)
{
    if (m->adhan_playing) {
        return UI_MODE_ADHAN;
    }
    if (settings_open) {
        return UI_MODE_SETTINGS;
    }
    if (m->net == UI_NET_SETUP) {
        return UI_MODE_SETUP;
    }
    if (!m->now.valid) {
        return UI_MODE_WAITING_TIME;
    }
    if (!m->have_today) {
        return UI_MODE_NO_DATA;
    }
    return UI_MODE_VIEW;
}

/* ---------- list view ---------- */

static void build_list(lv_obj_t *p)
{
    const palette_t *c = pal();
    ui.list_date = label(p, &lv_font_montserrat_22, c->text);
    lv_obj_align(ui.list_date, LV_ALIGN_TOP_MID, 0, 18);
    ui.list_hijri = label(p, &lv_font_montserrat_18, c->muted);
    lv_obj_align(ui.list_hijri, LV_ALIGN_TOP_MID, 0, 46);
    ui.list_badge = label(p, &lv_font_montserrat_14, c->on_accent);
    lv_obj_set_style_bg_color(ui.list_badge, col(c->accent), 0);
    lv_obj_set_style_bg_opa(ui.list_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ui.list_badge, 8, 0);
    lv_obj_set_style_pad_hor(ui.list_badge, 8, 0);
    lv_obj_set_style_pad_ver(ui.list_badge, 2, 0);
    lv_label_set_text(ui.list_badge, "Anggaran");
    lv_obj_align(ui.list_badge, LV_ALIGN_TOP_MID, 0, 48);

    for (int i = 0; i < P_COUNT; i++) {
        lv_obj_t *row = lv_obj_create(p);
        lv_obj_remove_style_all(row);
        lv_obj_set_clickable(row, false);
        lv_obj_set_scrollable(row, false);
        lv_obj_set_event_bubble(row, true);
        lv_obj_set_size(row, SCREEN - 2 * MARGIN, ROW_H - 4);
        lv_obj_set_pos(row, MARGIN, 82 + i * ROW_H);
        lv_obj_set_style_radius(row, 12, 0);
        lv_obj_set_style_bg_color(row, col(c->accent), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        ui.list_rows[i] = row;
        ui.list_names[i] = label(row, &lv_font_montserrat_28, c->text);
        lv_obj_align(ui.list_names[i], LV_ALIGN_LEFT_MID, 16, 0);
        lv_label_set_text(ui.list_names[i], PRAYERS[LIST_ORDER[i]].name);
        ui.list_times[i] = label(row, &lv_font_montserrat_28, c->text);
        lv_obj_align(ui.list_times[i], LV_ALIGN_RIGHT_MID, -16, 0);
    }
    ui.list_next = label(p, &lv_font_montserrat_22, c->accent);
    lv_obj_align(ui.list_next, LV_ALIGN_TOP_MID, 0, 82 + P_COUNT * ROW_H + 8);
    ui.list_footer = label(p, &lv_font_montserrat_14, c->muted);
    lv_obj_align(ui.list_footer, LV_ALIGN_BOTTOM_MID, 0, -14);
}

static void footer_text(const ui_model_t *m, char *buf, size_t cap)
{
    char hm[8];
    ms_format_hhmm((uint16_t)(m->now.second / 60), hm, sizeof(hm));
    const char *net = m->net == UI_NET_ONLINE ? "" : m->net == UI_NET_CONNECTING ? "  Wi-Fi..." : "  Luar talian";
    char bat[24] = "";
    if (m->battery_present) {
        snprintf(bat, sizeof(bat), "  Bateri %u%%%s", m->battery_pct, m->charging ? "+" : "");
    }
    snprintf(buf, cap, "%s  %s%s%s%s", hm, m->place, m->stale ? "  (data lama)" : "", net, bat);
}

static void next_text(const ui_model_t *m, char *buf, size_t cap)
{
    if (m->sched.seconds_to_next < 0) {
        snprintf(buf, cap, "%s", "");
        return;
    }
    char cd[32];
    ms_format_countdown(m->sched.seconds_to_next, cd, sizeof(cd));
    snprintf(buf, cap, "%s dalam %s", PRAYERS[m->sched.next].name, cd);
}

static void update_list(const ui_model_t *m)
{
    const palette_t *c = pal();
    char buf[96];
    ms_format_date(m->now.date, buf, sizeof(buf));
    set_text(ui.list_date, buf);
    ms_format_hijri(m->today.hijri, buf, sizeof(buf));
    set_text(ui.list_hijri, buf);
    bool estimated = m->today.origin == ORIGIN_ESTIMATED;
    if (estimated) {
        lv_obj_set_hidden(ui.list_badge, false);
    } else {
        lv_obj_set_hidden(ui.list_badge, true);
    }
    for (int i = 0; i < P_COUNT; i++) {
        prayer_t p = LIST_ORDER[i];
        bool current = p == m->sched.current;
        ms_format_hhmm(m->today.minute[p], buf, sizeof(buf));
        set_text(ui.list_times[i], buf);
        lv_obj_set_style_bg_opa(ui.list_rows[i], current ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        uint32_t fg = current ? c->on_accent : (PRAYERS[p].adhan ? c->text : c->muted);
        lv_obj_set_style_text_color(ui.list_names[i], col(fg), 0);
        lv_obj_set_style_text_color(ui.list_times[i], col(fg), 0);
    }
    next_text(m, buf, sizeof(buf));
    set_text(ui.list_next, buf);
    footer_text(m, buf, sizeof(buf));
    set_text(ui.list_footer, buf);
}

/* ---------- clock view ---------- */

static void build_clock(lv_obj_t *p)
{
    const palette_t *c = pal();
    static const char *HOURS[] = {"12", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", NULL};
    lv_obj_t *s = lv_scale_create(p);
    ui.clock_scale = s;
    lv_obj_set_size(s, 300, 300);
    lv_obj_align(s, LV_ALIGN_TOP_MID, 0, 20);
    lv_scale_set_mode(s, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_range(s, 0, 60);
    lv_scale_set_total_tick_count(s, 61);
    lv_scale_set_major_tick_every(s, 5);
    lv_scale_set_angle_range(s, 360);
    lv_scale_set_rotation(s, 270);
    lv_scale_set_label_show(s, true);
    lv_scale_set_text_src(s, HOURS);
    lv_obj_set_style_bg_opa(s, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s, 0, 0);
    lv_obj_set_style_text_font(s, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(s, col(c->text), 0);
    lv_obj_set_style_length(s, 14, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(s, 3, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(s, col(c->text), LV_PART_INDICATOR);
    lv_obj_set_style_length(s, 6, LV_PART_ITEMS);
    lv_obj_set_style_line_width(s, 2, LV_PART_ITEMS);
    lv_obj_set_style_line_color(s, col(c->muted), LV_PART_ITEMS);
    lv_obj_set_style_arc_width(s, 0, LV_PART_MAIN);
    lv_obj_set_style_radial_offset(s, 0, LV_PART_INDICATOR);
    lv_obj_set_style_pad_radial(s, 12, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(s, 6, 0);

    ui.clock_hour = lv_line_create(s);
    lv_obj_set_style_line_width(ui.clock_hour, 8, 0);
    lv_obj_set_style_line_rounded(ui.clock_hour, true, 0);
    lv_obj_set_style_line_color(ui.clock_hour, col(c->text), 0);
    ui.clock_min = lv_line_create(s);
    lv_obj_set_style_line_width(ui.clock_min, 5, 0);
    lv_obj_set_style_line_rounded(ui.clock_min, true, 0);
    lv_obj_set_style_line_color(ui.clock_min, col(c->text), 0);
    ui.clock_sec = lv_line_create(s);
    lv_obj_set_style_line_width(ui.clock_sec, 2, 0);
    lv_obj_set_style_line_color(ui.clock_sec, col(c->accent), 0);

    ui.clock_digital = label(p, &lv_font_montserrat_36, c->text);
    lv_obj_align(ui.clock_digital, LV_ALIGN_TOP_MID, 0, 330);
    ui.clock_date = label(p, &lv_font_montserrat_18, c->muted);
    lv_obj_align(ui.clock_date, LV_ALIGN_TOP_MID, 0, 378);
    ui.clock_next = label(p, &lv_font_montserrat_22, c->accent);
    lv_obj_align(ui.clock_next, LV_ALIGN_TOP_MID, 0, 412);
}

static void update_clock(const ui_model_t *m)
{
    int32_t sec = m->now.second;
    int32_t h = (sec / 3600) % 12, mi = (sec / 60) % 60, s = sec % 60;
    lv_scale_set_line_needle_value(ui.clock_scale, ui.clock_hour, 80, h * 5 + mi / 12);
    lv_scale_set_line_needle_value(ui.clock_scale, ui.clock_min, 120, mi);
    lv_scale_set_line_needle_value(ui.clock_scale, ui.clock_sec, 130, s);
    char buf[128];
    ms_format_hhmm((uint16_t)(sec / 60), buf, sizeof(buf));
    set_text(ui.clock_digital, buf);
    char date[64], hijri[48];
    ms_format_date(m->now.date, date, sizeof(date));
    ms_format_hijri(m->today.hijri, hijri, sizeof(hijri));
    snprintf(buf, sizeof(buf), "%s%s%s", date, hijri[0] ? "\n" : "", hijri);
    set_text(ui.clock_date, buf);
    lv_obj_set_style_text_align(ui.clock_date, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ui.clock_next, LV_ALIGN_TOP_MID, 0, hijri[0] ? 428 : 412);
    next_text(m, buf, sizeof(buf));
    set_text(ui.clock_next, buf);
}

/* ---------- countdown view ---------- */

static void build_countdown(lv_obj_t *p)
{
    const palette_t *c = pal();
    ui.cd_label = label(p, &lv_font_montserrat_22, c->muted);
    lv_label_set_text(ui.cd_label, "Waktu seterusnya");
    lv_obj_align(ui.cd_label, LV_ALIGN_TOP_MID, 0, 40);
    ui.cd_name = label(p, &lv_font_montserrat_48, c->accent);
    lv_obj_align(ui.cd_name, LV_ALIGN_TOP_MID, 0, 80);
    ui.cd_time = label(p, &font_digits_72, c->text);
    lv_obj_align(ui.cd_time, LV_ALIGN_TOP_MID, 0, 140);
    ui.cd_remaining = label(p, &font_digits_72, c->accent);
    lv_obj_align(ui.cd_remaining, LV_ALIGN_TOP_MID, 0, 270);
    lv_obj_t *lagi = label(p, &lv_font_montserrat_22, c->muted);
    lv_label_set_text(lagi, "lagi");
    lv_obj_align(lagi, LV_ALIGN_TOP_MID, 0, 240);
    ui.cd_footer = label(p, &lv_font_montserrat_14, c->muted);
    lv_obj_align(ui.cd_footer, LV_ALIGN_BOTTOM_MID, 0, -14);
}

static void update_countdown(const ui_model_t *m)
{
    char buf[96];
    set_text(ui.cd_name, PRAYERS[m->sched.next].name);
    if (m->sched.seconds_to_next >= 0) {
        int32_t t = m->sched.seconds_to_next;
        snprintf(buf, sizeof(buf), "%02ld:%02ld:%02ld", (long)(t / 3600), (long)(t / 60 % 60), (long)(t % 60));
        set_text(ui.cd_remaining, buf);
        uint16_t at = (uint16_t)(((m->now.second + t) / 60) % (24 * 60));
        ms_format_hhmm(at, buf, sizeof(buf));
        set_text(ui.cd_time, buf);
    } else {
        set_text(ui.cd_remaining, "--:--:--");
        set_text(ui.cd_time, "--:--");
    }
    footer_text(m, buf, sizeof(buf));
    set_text(ui.cd_footer, buf);
}

/* ---------- overlays ---------- */

static void on_adhan_tap(lv_event_t *e)
{
    if (ui.actions && ui.actions->stop_adhan) {
        ui.actions->stop_adhan();
    }
}

static void build_adhan(lv_obj_t *p)
{
    const palette_t *c = pal();
    lv_obj_set_clickable(p, true);
    lv_obj_set_event_bubble(p, false);
    lv_obj_add_event_cb(p, on_adhan_tap, LV_EVENT_CLICKED, NULL);
    lv_obj_t *t = label(p, &lv_font_montserrat_28, c->muted);
    lv_label_set_text(t, "Azan");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 90);
    ui.adhan_name = label(p, &lv_font_montserrat_48, c->accent);
    lv_obj_align(ui.adhan_name, LV_ALIGN_TOP_MID, 0, 150);
    ui.adhan_time = label(p, &font_digits_72, c->text);
    lv_obj_align(ui.adhan_time, LV_ALIGN_TOP_MID, 0, 220);
    lv_obj_t *hint = label(p, &lv_font_montserrat_18, c->muted);
    lv_label_set_text(hint, "Sentuh skrin untuk henti");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -40);
}

static void update_adhan(const ui_model_t *m)
{
    char buf[16];
    set_text(ui.adhan_name, PRAYERS[m->adhan_prayer].name);
    ms_format_hhmm(m->today.minute[m->adhan_prayer], buf, sizeof(buf));
    set_text(ui.adhan_time, m->have_today ? buf : "");
}

static void build_setup(lv_obj_t *p)
{
    const palette_t *c = pal();
    lv_obj_t *t = label(p, &lv_font_montserrat_28, c->text);
    lv_label_set_text(t, "Sambung ke Wi-Fi");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 22);
    ui.setup_qr = lv_qrcode_create(p);
    lv_qrcode_set_size(ui.setup_qr, 200);
    lv_qrcode_set_dark_color(ui.setup_qr, lv_color_black());
    lv_qrcode_set_light_color(ui.setup_qr, lv_color_white());
    lv_obj_set_style_border_color(ui.setup_qr, lv_color_white(), 0);
    lv_obj_set_style_border_width(ui.setup_qr, 8, 0);
    lv_obj_align(ui.setup_qr, LV_ALIGN_TOP_MID, 0, 66);
    ui.setup_text = label(p, &lv_font_montserrat_18, c->text);
    lv_obj_set_style_text_align(ui.setup_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ui.setup_text, LV_ALIGN_TOP_MID, 0, 292);
}

static void update_setup(const ui_model_t *m)
{
    static char last_qr[160];
    char qr[160];
    snprintf(qr, sizeof(qr), "WIFI:T:WPA;S:%s;P:%s;;", m->ap_ssid, m->ap_pass);
    if (strcmp(qr, last_qr) != 0) {
        lv_qrcode_update(ui.setup_qr, qr, strlen(qr));
        snprintf(last_qr, sizeof(last_qr), "%s", qr);
    }
    char buf[256];
    snprintf(buf, sizeof(buf),
             "Imbas kod ini dengan telefon, atau sambung ke\n%s  (kata laluan %s)\n\nKemudian buka http://192.168.4.1\nuntuk pilih rangkaian Wi-Fi rumah.",
             m->ap_ssid, m->ap_pass);
    set_text(ui.setup_text, buf);
}

static void build_message(lv_obj_t *p, lv_obj_t **text, const char *title, bool spinner)
{
    const palette_t *c = pal();
    lv_obj_t *t = label(p, &lv_font_montserrat_28, c->text);
    lv_label_set_text(t, title);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 150);
    if (spinner) {
        lv_obj_t *s = lv_spinner_create(p);
        lv_obj_set_size(s, 56, 56);
        lv_obj_set_style_arc_color(s, col(c->accent), LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(s, col(c->surface), LV_PART_MAIN);
        lv_obj_align(s, LV_ALIGN_TOP_MID, 0, 210);
    }
    *text = label(p, &lv_font_montserrat_18, c->muted);
    lv_obj_set_width(*text, SCREEN - 2 * MARGIN);
    lv_obj_set_style_text_align(*text, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(*text, LV_LABEL_LONG_WRAP);
    lv_obj_align(*text, LV_ALIGN_TOP_MID, 0, 290);
}

static void update_waiting(const ui_model_t *m)
{
    const char *why = m->net == UI_NET_ONLINE ? "Menyelaras masa dengan internet."
                      : m->net == UI_NET_CONNECTING ? "Menyambung ke Wi-Fi..."
                                                    : "Tiada Wi-Fi. Masa tidak dapat ditentukan.";
    set_text(ui.waiting_text, why);
}

static void update_nodata(const ui_model_t *m)
{
    char buf[160];
    snprintf(buf, sizeof(buf), "%s\n%s", m->place,
             m->net == UI_NET_ONLINE ? "Memuat turun dari e-Solat JAKIM..." : "Sambung ke Wi-Fi untuk muat turun.");
    set_text(ui.nodata_text, buf);
}

/* ---------- settings ---------- */

static void settings_changed(lv_event_t *e)
{
    settings_t s = ui.model.settings;
    for (int p = 0; p < P_COUNT; p++) {
        if (ui.set_switch[p]) {
            bool on = lv_obj_has_state(ui.set_switch[p], LV_STATE_CHECKED);
            s.adhan_mask = on ? (uint8_t)(s.adhan_mask | (1u << p)) : (uint8_t)(s.adhan_mask & ~(1u << p));
        }
    }
    s.volume = (uint8_t)lv_slider_get_value(ui.set_volume);
    s.brightness = (uint8_t)lv_slider_get_value(ui.set_brightness);
    s.night_dim = lv_obj_has_state(ui.set_dim, LV_STATE_CHECKED);
    s.theme = (theme_t)lv_dropdown_get_selected(ui.set_theme);
    uint32_t z = lv_dropdown_get_selected(ui.set_zone);
    if (z < ZONE_COUNT) {
        snprintf(s.zone, sizeof(s.zone), "%s", ZONES[z].code);
    }
    ui.model.settings = s;
    if (ui.actions && ui.actions->apply_settings) {
        ui.actions->apply_settings(&s);
    }
}

static void on_back(lv_event_t *e)
{
    ui_open_settings(false);
}

static void on_test(lv_event_t *e)
{
    if (ui.actions && ui.actions->test_adhan) {
        ui.actions->test_adhan();
    }
}

static void on_reset_wifi(lv_event_t *e)
{
    if (ui.actions && ui.actions->reset_wifi) {
        ui.actions->reset_wifi();
    }
}

static lv_obj_t *settings_row(lv_obj_t *list, const char *text)
{
    const palette_t *c = pal();
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 56);
    lv_obj_set_scrollable(row, false);
    lv_obj_t *l = label(row, &lv_font_montserrat_22, c->text);
    lv_label_set_text(l, text);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    return row;
}

static lv_obj_t *settings_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    const palette_t *c = pal();
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, LV_PCT(100), 52);
    lv_obj_set_style_bg_color(b, col(c->surface), 0);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = label(b, &lv_font_montserrat_22, c->text);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return b;
}

static void style_control(lv_obj_t *o)
{
    const palette_t *c = pal();
    lv_obj_set_style_bg_color(o, col(c->surface), LV_PART_MAIN);
    lv_obj_set_style_bg_color(o, col(c->accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(o, col(c->accent), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(o, col(c->text), LV_PART_KNOB);
}

static void build_settings(lv_obj_t *p)
{
    const palette_t *c = pal();
    lv_obj_t *list = lv_obj_create(p);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, SCREEN - 2 * MARGIN, SCREEN - 20);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_style_pad_bottom(list, 40, 0);
    lv_obj_set_scrollable(list, true);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *title = label(list, &lv_font_montserrat_28, c->accent);
    lv_label_set_text(title, "Tetapan");
    settings_button(list, "Kembali", on_back);

    lv_obj_t *h = label(list, &lv_font_montserrat_18, c->muted);
    lv_label_set_text(h, "Azan");
    for (int p = 0; p < P_COUNT; p++) {
        if (!PRAYERS[p].adhan) {
            continue;
        }
        lv_obj_t *row = settings_row(list, PRAYERS[p].name);
        lv_obj_t *sw = lv_switch_create(row);
        style_control(sw);
        lv_obj_set_size(sw, 64, 34);
        lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(sw, settings_changed, LV_EVENT_VALUE_CHANGED, NULL);
        ui.set_switch[p] = sw;
    }

    h = label(list, &lv_font_montserrat_18, c->muted);
    lv_label_set_text(h, "Kelantangan");
    ui.set_volume = lv_slider_create(list);
    style_control(ui.set_volume);
    lv_obj_set_width(ui.set_volume, LV_PCT(94));
    lv_obj_set_style_margin_hor(ui.set_volume, 12, 0);
    lv_slider_set_range(ui.set_volume, 0, 100);
    lv_obj_add_event_cb(ui.set_volume, settings_changed, LV_EVENT_RELEASED, NULL);

    h = label(list, &lv_font_montserrat_18, c->muted);
    lv_label_set_text(h, "Kecerahan");
    ui.set_brightness = lv_slider_create(list);
    style_control(ui.set_brightness);
    lv_obj_set_width(ui.set_brightness, LV_PCT(94));
    lv_slider_set_range(ui.set_brightness, 5, 100);
    lv_obj_add_event_cb(ui.set_brightness, settings_changed, LV_EVENT_RELEASED, NULL);

    lv_obj_t *row = settings_row(list, "Malap waktu malam");
    ui.set_dim = lv_switch_create(row);
    style_control(ui.set_dim);
    lv_obj_set_size(ui.set_dim, 64, 34);
    lv_obj_align(ui.set_dim, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(ui.set_dim, settings_changed, LV_EVENT_VALUE_CHANGED, NULL);

    h = label(list, &lv_font_montserrat_18, c->muted);
    lv_label_set_text(h, "Tema");
    ui.set_theme = lv_dropdown_create(list);
    lv_obj_set_width(ui.set_theme, LV_PCT(100));
    lv_dropdown_set_options(ui.set_theme, "Gelap\nHijau\nBiru\nUngu");
    lv_obj_add_event_cb(ui.set_theme, settings_changed, LV_EVENT_VALUE_CHANGED, NULL);

    h = label(list, &lv_font_montserrat_18, c->muted);
    lv_label_set_text(h, "Zon");
    ui.set_zone = lv_dropdown_create(list);
    lv_obj_set_width(ui.set_zone, LV_PCT(100));
    static char zone_opts[ZONE_OPTS_MAX];
    zone_opts[0] = '\0';
    for (size_t i = 0; i < ZONE_COUNT; i++) {
        char line[96];
        snprintf(line, sizeof(line), "%s%s %s", i ? "\n" : "", ZONES[i].code, ZONES[i].name);
        line[60] = '\0';
        strncat(zone_opts, line, sizeof(zone_opts) - strlen(zone_opts) - 1);
    }
    lv_dropdown_set_options(ui.set_zone, zone_opts);
    lv_obj_add_event_cb(ui.set_zone, settings_changed, LV_EVENT_VALUE_CHANGED, NULL);
    for (int i = 0; i < 2; i++) {
        lv_obj_t *dd = i ? ui.set_zone : ui.set_theme;
        lv_obj_set_style_bg_color(dd, col(c->surface), 0);
        lv_obj_set_style_text_color(dd, col(c->text), 0);
        lv_obj_set_style_text_font(dd, &lv_font_montserrat_18, 0);
        lv_obj_set_style_border_width(dd, 0, 0);
    }

    settings_button(list, "Uji azan", on_test);
    settings_button(list, "Tetapkan semula Wi-Fi", on_reset_wifi);
    ui.set_info = label(list, &lv_font_montserrat_14, c->muted);
    lv_obj_set_width(ui.set_info, LV_PCT(100));
    lv_label_set_long_mode(ui.set_info, LV_LABEL_LONG_WRAP);
}

static void sync_settings_controls(const settings_t *s)
{
    for (int p = 0; p < P_COUNT; p++) {
        if (ui.set_switch[p]) {
            if ((s->adhan_mask >> p) & 1u) {
                lv_obj_add_state(ui.set_switch[p], LV_STATE_CHECKED);
            } else {
                lv_obj_remove_state(ui.set_switch[p], LV_STATE_CHECKED);
            }
        }
    }
    lv_slider_set_value(ui.set_volume, s->volume, LV_ANIM_OFF);
    lv_slider_set_value(ui.set_brightness, s->brightness, LV_ANIM_OFF);
    if (s->night_dim) {
        lv_obj_add_state(ui.set_dim, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(ui.set_dim, LV_STATE_CHECKED);
    }
    lv_dropdown_set_selected(ui.set_theme, s->theme);
    for (size_t i = 0; i < ZONE_COUNT; i++) {
        if (strcmp(ZONES[i].code, s->zone) == 0) {
            lv_dropdown_set_selected(ui.set_zone, (uint32_t)i);
        }
    }
}

static void update_settings_info(const ui_model_t *m)
{
    char buf[160];
    snprintf(buf, sizeof(buf), "Alamat: %s\nPIN web: %s\nVersi: %s", m->ip[0] ? m->ip : "-", m->settings.pin,
             m->version);
    set_text(ui.set_info, buf);
}

/* ---------- screen, input, mode switching ---------- */

static void on_screen_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    bool settings_reachable = ui.mode == UI_MODE_VIEW || ui.mode == UI_MODE_SETTINGS || ui.mode == UI_MODE_NO_DATA ||
                              ui.mode == UI_MODE_WAITING_TIME;
    if (code == LV_EVENT_LONG_PRESSED && settings_reachable) {
        ui_open_settings(ui.mode != UI_MODE_SETTINGS);
        return;
    }
    if (ui.mode != UI_MODE_VIEW) {
        return;
    }
    if (code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        if (dir == LV_DIR_LEFT) {
            ui_step_view(1);
        } else if (dir == LV_DIR_RIGHT) {
            ui_step_view(-1);
        }
    } else if (code == LV_EVENT_SHORT_CLICKED) {
        ui_step_view(1);
    }
}

static void build_all(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    memset(ui.set_switch, 0, sizeof(ui.set_switch));
    lv_obj_set_style_bg_color(scr, col(pal()->bg), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(scr, false);
    lv_obj_add_event_cb(scr, on_screen_event, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(scr, on_screen_event, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(scr, on_screen_event, LV_EVENT_LONG_PRESSED, NULL);

    ui.root = panel(scr);
    lv_obj_set_hidden(ui.root, false);

    for (int m = 0; m <= UI_MODE_NO_DATA; m++) {
        ui.mode_obj[m] = panel(ui.root);
    }
    for (int v = 0; v < UI_VIEW_COUNT; v++) {
        ui.view_obj[v] = panel(ui.mode_obj[UI_MODE_VIEW]);
    }
    lv_obj_set_hidden(ui.mode_obj[UI_MODE_VIEW], false);
    build_list(ui.view_obj[UI_VIEW_LIST]);
    build_clock(ui.view_obj[UI_VIEW_CLOCK]);
    build_countdown(ui.view_obj[UI_VIEW_COUNTDOWN]);
    build_adhan(ui.mode_obj[UI_MODE_ADHAN]);
    build_setup(ui.mode_obj[UI_MODE_SETUP]);
    build_message(ui.mode_obj[UI_MODE_WAITING_TIME], &ui.waiting_text, "Menunggu masa", true);
    build_message(ui.mode_obj[UI_MODE_NO_DATA], &ui.nodata_text, "Tiada data waktu solat", true);
    build_settings(ui.mode_obj[UI_MODE_SETTINGS]);
    lv_obj_set_event_bubble(ui.mode_obj[UI_MODE_SETTINGS], false);
    lv_obj_set_clickable(ui.mode_obj[UI_MODE_SETTINGS], true);
    lv_obj_add_event_cb(ui.mode_obj[UI_MODE_SETTINGS], on_screen_event, LV_EVENT_LONG_PRESSED, NULL);
    ui.mode = (ui_mode_t)-1;
}

static void show_only(lv_obj_t **objs, int count, int active)
{
    for (int i = 0; i < count; i++) {
        if (i == active) {
            lv_obj_set_hidden(objs[i], false);
        } else {
            lv_obj_set_hidden(objs[i], true);
        }
    }
}

static void burn_in_shift(const ui_model_t *m)
{
    static const int8_t OFFSETS[][2] = {{0, 0}, {3, 2}, {-2, 3}, {-3, -2}, {2, -3}, {4, 0}, {0, 4}, {-4, 0}, {0, -4}};
    int n = (int)(sizeof(OFFSETS) / sizeof(OFFSETS[0]));
    int i = m->now.valid ? (int)((m->now.second / BURN_IN_PERIOD_S) % n) : 0;
    lv_obj_set_pos(ui.root, OFFSETS[i][0], OFFSETS[i][1]);
}

static void render(void)
{
    const ui_model_t *m = &ui.model;
    if (m->settings.theme != ui.theme) {
        ui.theme = m->settings.theme;
        build_all();
    }
    ui_mode_t mode = ui_mode_for(m, ui.settings_open);
    if (mode != ui.mode) {
        if (mode == UI_MODE_SETTINGS) {
            sync_settings_controls(&m->settings);
        }
        show_only(ui.mode_obj, UI_MODE_NO_DATA + 1, mode);
        ui.mode = mode;
    }
    show_only(ui.view_obj, UI_VIEW_COUNT, ui.view);
    burn_in_shift(m);
    switch (mode) {
    case UI_MODE_VIEW:
        if (ui.view == UI_VIEW_LIST) {
            update_list(m);
        } else if (ui.view == UI_VIEW_CLOCK) {
            update_clock(m);
        } else {
            update_countdown(m);
        }
        break;
    case UI_MODE_ADHAN:
        update_adhan(m);
        break;
    case UI_MODE_SETUP:
        update_setup(m);
        break;
    case UI_MODE_WAITING_TIME:
        update_waiting(m);
        break;
    case UI_MODE_NO_DATA:
        update_nodata(m);
        break;
    case UI_MODE_SETTINGS:
        update_settings_info(m);
        break;
    }
}

void ui_init(const ui_actions_t *actions)
{
    memset(&ui, 0, sizeof(ui));
    ui.actions = actions;
    ui.theme = THEME_GELAP;
    ui.model.settings = settings_defaults();
    lv_indev_t *indev = NULL;
    while ((indev = lv_indev_get_next(indev)) != NULL) {
        lv_indev_set_long_press_time(indev, 1500);
    }
    build_all();
}

void ui_update(const ui_model_t *m)
{
    ui.model = *m;
    ui.have_model = true;
    render();
}

void ui_show_view(ui_view_t view)
{
    ui.view = view;
    if (ui.have_model) {
        render();
    }
}

void ui_step_view(int dir)
{
    ui_show_view((ui_view_t)((ui.view + UI_VIEW_COUNT + dir) % UI_VIEW_COUNT));
}

void ui_open_settings(bool open)
{
    ui.settings_open = open;
    if (ui.have_model) {
        render();
    }
}

ui_mode_t ui_current_mode(void)
{
    return ui.mode;
}
