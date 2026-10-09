#include "schedule.h"

static const prayer_t PERIODS[] = {P_SUBUH, P_SYURUK, P_ZOHOR, P_ASAR, P_MAGHRIB, P_ISYAK};

schedule_t schedule_at(const day_times_t *today, const day_times_t *tomorrow, wallclock_t now)
{
    int32_t minute = now.second / 60;
    schedule_t s = {.current = P_ISYAK, .next = P_SUBUH, .seconds_to_next = -1};

    for (size_t i = 0; i < sizeof(PERIODS) / sizeof(PERIODS[0]); i++) {
        if (minute >= today->minute[PERIODS[i]]) {
            s.current = PERIODS[i];
        }
    }
    for (int p = 0; p < P_COUNT; p++) {
        if (PRAYERS[p].adhan && today->minute[p] * 60 > now.second) {
            s.next = (prayer_t)p;
            s.next_date = now.date;
            s.seconds_to_next = today->minute[p] * 60 - now.second;
            return s;
        }
    }
    if (tomorrow) {
        s.next = P_SUBUH;
        s.next_date = tomorrow->date;
        s.seconds_to_next = 24 * 3600 - now.second + tomorrow->minute[P_SUBUH] * 60;
    }
    return s;
}

static bool after_mark(date_t date, prayer_t p, adhan_mark_t last)
{
    if (last.date.year == 0) {
        return true;
    }
    int c = date_cmp(date, last.date);
    return c > 0 || (c == 0 && p > last.prayer);
}

bool adhan_due(const day_times_t *today, wallclock_t now, uint8_t mask, adhan_mark_t last, prayer_t *out)
{
    if (!now.valid || date_cmp(today->date, now.date) != 0) {
        return false;
    }
    int32_t minute = now.second / 60;
    for (int p = 0; p < P_COUNT; p++) {
        if (PRAYERS[p].adhan && (mask & (1u << p)) && today->minute[p] == minute &&
            after_mark(now.date, (prayer_t)p, last)) {
            *out = (prayer_t)p;
            return true;
        }
    }
    return false;
}

bool night_dim_at(const day_times_t *today, wallclock_t now)
{
    int32_t minute = now.second / 60;
    return minute >= today->minute[P_ISYAK] + 60 || minute < today->minute[P_SUBUH] - 30;
}
