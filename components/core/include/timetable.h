#pragma once
#include "prayer.h"

#define YEAR_MAX_DAYS 366

/* One calendar year of official times for one zone. A day whose source row
 * was missing or rejected keeps minute[] zeroed and present = false. */
typedef struct {
    char zone[8];
    int16_t year;
    origin_t origin;
    bool present[YEAR_MAX_DAYS];
    day_times_t days[YEAR_MAX_DAYS];
} year_table_t;

typedef enum {
    PARSE_OK,
    PARSE_NO_RECORD,
    PARSE_BAD_DATA,
} parse_status_t;

/* e-solat TakwimSolat response for period=year or period=duration. Rows from
 * years other than `year` are ignored. */
parse_status_t esolat_parse(const char *json, size_t len, const char *zone, int16_t year, year_table_t *out);

/* Mawaqit mosque response: {"name": ..., "times": ["HH:MM" x6], "calendar": [12 x {"<day>": [6]}]}.
 * Fills today's row from "times" and the whole year from "calendar" when present. */
parse_status_t mawaqit_parse(const char *json, size_t len, date_t today, year_table_t *out, char *name, size_t name_cap);

int year_table_day_count(const year_table_t *t);

/* Tables the device currently holds, newest first is not required. */
typedef struct {
    const year_table_t *tables[3];
    size_t count;
} timetable_t;

/* Official row for `date`, else the same month/day from the previous year
 * marked ORIGIN_ESTIMATED with the Hijri date cleared. */
bool timetable_day(const timetable_t *tt, date_t date, day_times_t *out);

#define YEAR_TABLE_BLOB_MAX (16 + YEAR_MAX_DAYS * 20)
size_t year_table_serialize(const year_table_t *t, uint8_t *buf, size_t cap);
bool year_table_deserialize(const uint8_t *buf, size_t len, year_table_t *out);
