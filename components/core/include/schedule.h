#pragma once
#include "prayer.h"

/* Local wall-clock time. `valid` is false until the clock has been set by
 * SNTP or by hand; the board has no RTC. */
typedef struct {
    bool valid;
    date_t date;
    int32_t second; /* seconds after local midnight */
} wallclock_t;

typedef struct {
    prayer_t current; /* period we are in: Subuh, Syuruk, Zohor, Asar, Maghrib or Isyak */
    prayer_t next;    /* next prayer with an Adhan */
    date_t next_date;
    int32_t seconds_to_next;
} schedule_t;

/* `tomorrow` may be NULL; then a next prayer after Isyak is unknown and
 * seconds_to_next is -1. */
schedule_t schedule_at(const day_times_t *today, const day_times_t *tomorrow, wallclock_t now);

/* The last Adhan started, persisted so a reboot never replays it. */
typedef struct {
    date_t date; /* year 0 when none */
    prayer_t prayer;
} adhan_mark_t;

/* Returns true and sets *out when an Adhan should start now: the clock is
 * valid, the current minute is a prayer's minute, the prayer is enabled in
 * `mask`, and (date, prayer) is after `last`. */
bool adhan_due(const day_times_t *today, wallclock_t now, uint8_t mask, adhan_mark_t last, prayer_t *out);

/* Night dimming window: from 60 minutes after Isyak to 30 minutes before Subuh. */
bool night_dim_at(const day_times_t *today, wallclock_t now);
