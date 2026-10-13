#pragma once
#include "schedule.h"
#include "settings.h"
#include "timetable.h"

typedef enum {
    UI_NET_OFF,
    UI_NET_CONNECTING,
    UI_NET_ONLINE,
    UI_NET_SETUP, /* SoftAP captive portal is up, device not configured */
} ui_net_t;

typedef struct {
    wallclock_t now;
    bool have_today;
    day_times_t today;
    schedule_t sched;
    char place[64]; /* zone name, or mosque name under Mawaqit */
    bool stale;     /* cached data could not be refreshed */
    bool adhan_playing;
    prayer_t adhan_prayer;
    ui_net_t net;
    char ap_ssid[33];
    char ap_pass[65];
    char ip[16];
    bool battery_present;
    uint8_t battery_pct;
    bool charging;
    bool dimmed;
    char version[32];
    settings_t settings;
} ui_model_t;

/* Called from the LVGL task. Implementations must not block. */
typedef struct {
    void (*stop_adhan)(void);
    void (*test_adhan)(void);
    void (*apply_settings)(const settings_t *s);
    void (*reset_wifi)(void);
    void (*pomodoro_done)(void); /* a focus or break phase ran out */
} ui_actions_t;

typedef enum {
    UI_VIEW_LIST,
    UI_VIEW_CLOCK,
    UI_VIEW_COUNTDOWN,
    UI_VIEW_POMODORO,
    UI_VIEW_COUNT
} ui_view_t;

typedef enum {
    UI_MODE_VIEW,
    UI_MODE_SETTINGS,
    UI_MODE_ADHAN,
    UI_MODE_SETUP,
    UI_MODE_WAITING_TIME,
    UI_MODE_NO_DATA,
} ui_mode_t;

/* Which screen the model calls for. Pure; exposed for tests and the simulator. */
ui_mode_t ui_mode_for(const ui_model_t *m, bool settings_open);

/* Fills the time-derived fields (today, schedule, place) from the tables.
 * Pure; shared by the firmware and the simulator. */
void ui_model_fill_times(ui_model_t *m, const timetable_t *tt, const char *mawaqit_name);

void ui_init(const ui_actions_t *actions);
void ui_update(const ui_model_t *m);
void ui_show_view(ui_view_t view);
void ui_step_view(int dir);
void ui_open_settings(bool open);
ui_mode_t ui_current_mode(void);
