#include "prayer.h"

const prayer_info_t PRAYERS[P_COUNT] = {
    [P_IMSAK] = {"Imsak", false},
    [P_SUBUH] = {"Subuh", true},
    [P_SYURUK] = {"Syuruk", false},
    [P_ZOHOR] = {"Zohor", true},
    [P_ASAR] = {"Asar", true},
    [P_MAGHRIB] = {"Maghrib", true},
    [P_ISYAK] = {"Isyak", true},
};

/* Howard Hinnant's days_from_civil: days since 1970-01-01. */
int32_t date_to_days(date_t d)
{
    int32_t y = d.year - (d.month <= 2);
    int32_t era = (y >= 0 ? y : y - 399) / 400;
    int32_t yoe = y - era * 400;
    int32_t mp = (d.month + 9) % 12;
    int32_t doy = (153 * mp + 2) / 5 + d.day - 1;
    int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

date_t date_from_days(int32_t z)
{
    z += 719468;
    int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    int32_t doe = z - era * 146097;
    int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int32_t mp = (5 * doy + 2) / 153;
    int32_t day = doy - (153 * mp + 2) / 5 + 1;
    int32_t month = mp < 10 ? mp + 3 : mp - 9;
    int32_t year = yoe + era * 400 + (month <= 2);
    return (date_t){(int16_t)year, (int8_t)month, (int8_t)day};
}

date_t date_add(date_t d, int32_t days)
{
    return date_from_days(date_to_days(d) + days);
}

int date_cmp(date_t a, date_t b)
{
    int32_t x = date_to_days(a), y = date_to_days(b);
    return (x > y) - (x < y);
}

bool date_valid(date_t d)
{
    if (d.year < 1970 || d.month < 1 || d.month > 12 || d.day < 1 || d.day > 31) {
        return false;
    }
    date_t round = date_from_days(date_to_days(d));
    return round.year == d.year && round.month == d.month && round.day == d.day;
}

int date_weekday(date_t d)
{
    int32_t days = date_to_days(d);
    return (int)(((days % 7) + 11) % 7); /* 1970-01-01 was a Thursday */
}

bool day_times_ordered(const day_times_t *d)
{
    for (int p = 0; p < P_COUNT; p++) {
        if (d->minute[p] >= 24 * 60) {
            return false;
        }
        if (p > 0 && d->minute[p] < d->minute[p - 1]) {
            return false;
        }
    }
    return d->minute[P_SUBUH] < d->minute[P_SYURUK];
}
