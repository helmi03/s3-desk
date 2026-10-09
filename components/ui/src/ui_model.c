#include <stdio.h>

#include "ui.h"
#include "zones.h"

void ui_model_fill_times(ui_model_t *m, const timetable_t *tt, const char *mawaqit_name)
{
    const zone_t *z = zone_find(m->settings.zone);
    if (m->settings.source == SOURCE_MAWAQIT && mawaqit_name && mawaqit_name[0]) {
        snprintf(m->place, sizeof(m->place), "%s", mawaqit_name);
    } else {
        snprintf(m->place, sizeof(m->place), "%s", z ? z->name : m->settings.zone);
    }
    m->have_today = m->now.valid && timetable_day(tt, m->now.date, &m->today);
    if (!m->have_today) {
        m->sched = (schedule_t){.current = P_ISYAK, .next = P_SUBUH, .seconds_to_next = -1};
        return;
    }
    day_times_t tomorrow;
    bool have_tomorrow = timetable_day(tt, date_add(m->now.date, 1), &tomorrow);
    m->sched = schedule_at(&m->today, have_tomorrow ? &tomorrow : NULL, m->now);
}
