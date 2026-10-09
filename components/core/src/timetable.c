#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "timetable.h"

static const char *const MS_MONTH_ABBR[12] = {"Jan", "Feb", "Mac", "Apr", "Mei", "Jun",
                                              "Jul", "Ogos", "Sep", "Okt", "Nov", "Dis"};

static int day_index(date_t d)
{
    return (int)(date_to_days(d) - date_to_days((date_t){d.year, 1, 1}));
}

static void year_table_init(year_table_t *t, const char *zone, int16_t year, origin_t origin)
{
    memset(t, 0, sizeof(*t));
    snprintf(t->zone, sizeof(t->zone), "%s", zone);
    t->year = year;
    t->origin = origin;
}

static bool parse_hhmm(const cJSON *item, uint16_t *out)
{
    if (!cJSON_IsString(item)) {
        return false;
    }
    unsigned h, m;
    if (sscanf(item->valuestring, "%2u:%2u", &h, &m) != 2 || h > 23 || m > 59) {
        return false;
    }
    *out = (uint16_t)(h * 60 + m);
    return true;
}

static bool parse_esolat_date(const char *s, date_t *out)
{
    char mon[8];
    int d, y;
    if (sscanf(s, "%d-%7[A-Za-z]-%d", &d, mon, &y) != 3) {
        return false;
    }
    for (int m = 0; m < 12; m++) {
        if (strcmp(mon, MS_MONTH_ABBR[m]) == 0) {
            *out = (date_t){(int16_t)y, (int8_t)(m + 1), (int8_t)d};
            return date_valid(*out);
        }
    }
    return false;
}

static bool parse_iso_date(const char *s, date_t *out)
{
    int y, m, d;
    if (sscanf(s, "%d-%d-%d", &y, &m, &d) != 3) {
        return false;
    }
    *out = (date_t){(int16_t)y, (int8_t)m, (int8_t)d};
    return y > 0 && m >= 1 && m <= 12 && d >= 1 && d <= 30;
}

static void store_day(year_table_t *t, const day_times_t *day)
{
    if (day->date.year != t->year || !day_times_ordered(day)) {
        return;
    }
    int i = day_index(day->date);
    t->days[i] = *day;
    t->present[i] = true;
}

parse_status_t esolat_parse(const char *json, size_t len, const char *zone, int16_t year, year_table_t *out)
{
    static const char *const KEYS[P_COUNT] = {"imsak", "fajr", "syuruk", "dhuhr", "asr", "maghrib", "isha"};

    year_table_init(out, zone, year, ORIGIN_OFFICIAL);
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        return PARSE_BAD_DATA;
    }
    parse_status_t status = PARSE_BAD_DATA;
    const cJSON *st = cJSON_GetObjectItem(root, "status");
    const cJSON *rows = cJSON_GetObjectItem(root, "prayerTime");
    if (cJSON_IsString(st) && strcmp(st->valuestring, "NO_RECORD!") == 0) {
        status = PARSE_NO_RECORD;
    } else if (cJSON_IsString(st) && strcmp(st->valuestring, "OK!") == 0 && cJSON_IsArray(rows)) {
        const cJSON *row;
        cJSON_ArrayForEach(row, rows) {
            day_times_t day = {.origin = ORIGIN_OFFICIAL};
            const cJSON *date = cJSON_GetObjectItem(row, "date");
            const cJSON *hijri = cJSON_GetObjectItem(row, "hijri");
            if (!cJSON_IsString(date) || !parse_esolat_date(date->valuestring, &day.date)) {
                continue;
            }
            if (!cJSON_IsString(hijri) || !parse_iso_date(hijri->valuestring, &day.hijri)) {
                day.hijri = (date_t){0};
            }
            bool ok = true;
            for (int p = 0; p < P_COUNT && ok; p++) {
                ok = parse_hhmm(cJSON_GetObjectItem(row, KEYS[p]), &day.minute[p]);
            }
            if (ok) {
                store_day(out, &day);
            }
        }
        status = year_table_day_count(out) > 0 ? PARSE_OK : PARSE_NO_RECORD;
    }
    cJSON_Delete(root);
    return status;
}

static bool mawaqit_row(const cJSON *arr, date_t date, day_times_t *day)
{
    /* Mawaqit rows are [fajr, shuruq, dhuhr, asr, maghrib, isha]. */
    static const prayer_t ORDER[6] = {P_SUBUH, P_SYURUK, P_ZOHOR, P_ASAR, P_MAGHRIB, P_ISYAK};
    if (cJSON_GetArraySize(arr) < 6) {
        return false;
    }
    *day = (day_times_t){.date = date, .origin = ORIGIN_MAWAQIT};
    for (int i = 0; i < 6; i++) {
        if (!parse_hhmm(cJSON_GetArrayItem(arr, i), &day->minute[ORDER[i]])) {
            return false;
        }
    }
    uint16_t subuh = day->minute[P_SUBUH];
    day->minute[P_IMSAK] = subuh >= 10 ? subuh - 10 : 0;
    return true;
}

parse_status_t mawaqit_parse(const char *json, size_t len, date_t today, year_table_t *out, char *name, size_t name_cap)
{
    year_table_init(out, "MAWAQIT", today.year, ORIGIN_MAWAQIT);
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        return PARSE_BAD_DATA;
    }
    const cJSON *n = cJSON_GetObjectItem(root, "name");
    if (name && name_cap) {
        snprintf(name, name_cap, "%s", cJSON_IsString(n) ? n->valuestring : "");
    }
    const cJSON *calendar = cJSON_GetObjectItem(root, "calendar");
    if (cJSON_IsArray(calendar)) {
        for (int m = 0; m < 12 && m < cJSON_GetArraySize(calendar); m++) {
            const cJSON *month = cJSON_GetArrayItem(calendar, m);
            const cJSON *entry;
            cJSON_ArrayForEach(entry, month) {
                date_t date = {today.year, (int8_t)(m + 1), (int8_t)atoi(entry->string ? entry->string : "0")};
                day_times_t day;
                if (date_valid(date) && mawaqit_row(entry, date, &day)) {
                    store_day(out, &day);
                }
            }
        }
    }
    day_times_t day;
    if (mawaqit_row(cJSON_GetObjectItem(root, "times"), today, &day)) {
        store_day(out, &day);
    }
    cJSON_Delete(root);
    return year_table_day_count(out) > 0 ? PARSE_OK : PARSE_BAD_DATA;
}

int year_table_day_count(const year_table_t *t)
{
    int n = 0;
    for (int i = 0; i < YEAR_MAX_DAYS; i++) {
        n += t->present[i];
    }
    return n;
}

static const day_times_t *find_day(const timetable_t *tt, date_t date)
{
    for (size_t i = 0; i < tt->count; i++) {
        const year_table_t *t = tt->tables[i];
        if (t && t->year == date.year && t->present[day_index(date)]) {
            return &t->days[day_index(date)];
        }
    }
    return NULL;
}

bool timetable_day(const timetable_t *tt, date_t date, day_times_t *out)
{
    const day_times_t *d = find_day(tt, date);
    if (d) {
        *out = *d;
        return true;
    }
    date_t last_year = {(int16_t)(date.year - 1), date.month, date.day};
    if (!date_valid(last_year)) {
        last_year.day = 28; /* Feb 29 */
    }
    d = find_day(tt, last_year);
    if (!d) {
        return false;
    }
    *out = *d;
    out->date = date;
    out->hijri = (date_t){0};
    out->origin = ORIGIN_ESTIMATED;
    return true;
}

static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) {
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
    }
    return ~c;
}

#define BLOB_MAGIC "PTT1"
#define BLOB_HEADER 16
#define BLOB_ROW 19

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

size_t year_table_serialize(const year_table_t *t, uint8_t *buf, size_t cap)
{
    size_t need = BLOB_HEADER + YEAR_MAX_DAYS * BLOB_ROW + 4;
    if (cap < need) {
        return 0;
    }
    memset(buf, 0, need);
    memcpy(buf, BLOB_MAGIC, 4);
    memcpy(buf + 4, t->zone, 8);
    put16(buf + 12, (uint16_t)t->year);
    buf[14] = (uint8_t)t->origin;
    uint8_t *row = buf + BLOB_HEADER;
    for (int i = 0; i < YEAR_MAX_DAYS; i++, row += BLOB_ROW) {
        const day_times_t *d = &t->days[i];
        row[0] = t->present[i];
        put16(row + 1, (uint16_t)d->hijri.year);
        row[3] = (uint8_t)d->hijri.month;
        row[4] = (uint8_t)d->hijri.day;
        for (int p = 0; p < P_COUNT; p++) {
            put16(row + 5 + 2 * p, d->minute[p]);
        }
    }
    uint32_t crc = crc32(buf, need - 4);
    put16(buf + need - 4, (uint16_t)crc);
    put16(buf + need - 2, (uint16_t)(crc >> 16));
    return need;
}

bool year_table_deserialize(const uint8_t *buf, size_t len, year_table_t *out)
{
    size_t need = BLOB_HEADER + YEAR_MAX_DAYS * BLOB_ROW + 4;
    if (len != need || memcmp(buf, BLOB_MAGIC, 4) != 0) {
        return false;
    }
    uint32_t crc = get16(buf + need - 4) | ((uint32_t)get16(buf + need - 2) << 16);
    if (crc != crc32(buf, need - 4)) {
        return false;
    }
    char zone[9] = {0};
    memcpy(zone, buf + 4, 8);
    year_table_init(out, zone, (int16_t)get16(buf + 12), (origin_t)buf[14]);
    date_t jan1 = {out->year, 1, 1};
    const uint8_t *row = buf + BLOB_HEADER;
    for (int i = 0; i < YEAR_MAX_DAYS; i++, row += BLOB_ROW) {
        if (!row[0]) {
            continue;
        }
        day_times_t *d = &out->days[i];
        d->date = date_add(jan1, i);
        d->hijri = (date_t){(int16_t)get16(row + 1), (int8_t)row[3], (int8_t)row[4]};
        d->origin = out->origin;
        for (int p = 0; p < P_COUNT; p++) {
            d->minute[p] = get16(row + 5 + 2 * p);
        }
        out->present[i] = d->date.year == out->year;
    }
    return true;
}
