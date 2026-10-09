#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ms.h"
#include "schedule.h"
#include "settings.h"
#include "timetable.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static char *read_fixture(const char *name, size_t *len)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", FIXTURES, name);
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(*len + 1);
    TEST_ASSERT_EQUAL(*len, fread(buf, 1, *len, f));
    buf[*len] = '\0';
    fclose(f);
    return buf;
}

static year_table_t *load_esolat(const char *name, int16_t year, parse_status_t expect)
{
    size_t len;
    char *json = read_fixture(name, &len);
    year_table_t *t = calloc(1, sizeof(*t));
    TEST_ASSERT_EQUAL_MESSAGE(expect, esolat_parse(json, len, "SGR03", year, t), name);
    free(json);
    return t;
}

static const day_times_t *table_day(const year_table_t *t, date_t d)
{
    int i = (int)(date_to_days(d) - date_to_days((date_t){d.year, 1, 1}));
    return t->present[i] ? &t->days[i] : NULL;
}

static day_times_t day_of(date_t date, const uint16_t m[P_COUNT])
{
    day_times_t d = {.date = date, .origin = ORIGIN_OFFICIAL};
    memcpy(d.minute, m, sizeof(d.minute));
    return d;
}

#define HM(h, m) ((uint16_t)((h) * 60 + (m)))
static const uint16_t KLANG_9_OCT[P_COUNT] = {HM(5, 42), HM(5, 52), HM(6, 58), HM(13, 4),
                                              HM(16, 17), HM(19, 5), HM(20, 14)};
static const date_t OCT9 = {2026, 10, 9};

static wallclock_t at(date_t d, int h, int m, int s)
{
    return (wallclock_t){.valid = true, .date = d, .second = h * 3600 + m * 60 + s};
}

static void test_esolat_year_matches_official_klang_times(void)
{
    year_table_t *t = load_esolat("esolat/SGR03-year.json", 2026, PARSE_OK);
    TEST_ASSERT_EQUAL(365, year_table_day_count(t));
    const day_times_t *d = table_day(t, OCT9);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(KLANG_9_OCT, d->minute, P_COUNT);
    TEST_ASSERT_EQUAL(1448, d->hijri.year);
    TEST_ASSERT_EQUAL(4, d->hijri.month);
    TEST_ASSERT_EQUAL(27, d->hijri.day);
    TEST_ASSERT_EQUAL(ORIGIN_OFFICIAL, d->origin);
    free(t);
}

static void test_esolat_parses_every_malay_month_abbreviation(void)
{
    year_table_t *t = load_esolat("esolat/SGR03-year.json", 2026, PARSE_OK);
    for (int m = 1; m <= 12; m++) {
        TEST_ASSERT_NOT_NULL_MESSAGE(table_day(t, (date_t){2026, (int8_t)m, 1}), ms_month(m));
    }
    free(t);
}

static void test_esolat_no_record_object_shape(void)
{
    free(load_esolat("esolat/no-record.json", 2026, PARSE_NO_RECORD));
}

static void test_esolat_duration_crossing_year_end_is_truncated(void)
{
    year_table_t *t = load_esolat("esolat/SGR03-duration-yearend.json", 2026, PARSE_OK);
    TEST_ASSERT_EQUAL(7, year_table_day_count(t));
    TEST_ASSERT_NOT_NULL(table_day(t, (date_t){2026, 12, 31}));
    free(t);
    free(load_esolat("esolat/SGR03-duration-yearend.json", 2027, PARSE_NO_RECORD));
}

static void test_esolat_rejects_row_with_out_of_order_times(void)
{
    const char *json =
        "{\"prayerTime\":["
        "{\"hijri\":\"1446-09-01\",\"date\":\"01-Mac-2025\",\"imsak\":\"06:13:00\",\"fajr\":\"18:23:00\","
        "\"syuruk\":\"07:30:00\",\"dhuhr\":\"13:30:00\",\"asr\":\"16:40:00\",\"maghrib\":\"19:30:00\",\"isha\":\"20:40:00\"},"
        "{\"hijri\":\"1446-09-02\",\"date\":\"02-Mac-2025\",\"imsak\":\"06:13:00\",\"fajr\":\"06:23:00\","
        "\"syuruk\":\"07:30:00\",\"dhuhr\":\"13:30:00\",\"asr\":\"16:40:00\",\"maghrib\":\"19:30:00\",\"isha\":\"20:40:00\"}"
        "],\"status\":\"OK!\"}";
    year_table_t *t = calloc(1, sizeof(*t));
    TEST_ASSERT_EQUAL(PARSE_OK, esolat_parse(json, strlen(json), "PLS01", 2025, t));
    TEST_ASSERT_NULL(table_day(t, (date_t){2025, 3, 1}));
    TEST_ASSERT_NOT_NULL(table_day(t, (date_t){2025, 3, 2}));
    free(t);
}

static void test_esolat_garbage_is_bad_data(void)
{
    year_table_t *t = calloc(1, sizeof(*t));
    TEST_ASSERT_EQUAL(PARSE_BAD_DATA, esolat_parse("<html>", 6, "SGR03", 2026, t));
    free(t);
}

static void test_previous_year_estimate_is_within_one_minute_for_a_whole_year(void)
{
    year_table_t *y2025 = load_esolat("esolat/SGR03-2025.json", 2025, PARSE_OK);
    year_table_t *y2026 = load_esolat("esolat/SGR03-year.json", 2026, PARSE_OK);
    timetable_t only_2025 = {.tables = {y2025}, .count = 1};
    int worst = 0, days = 0;
    for (date_t d = {2026, 1, 1}; d.year == 2026; d = date_add(d, 1)) {
        day_times_t est;
        const day_times_t *real = table_day(y2026, d);
        TEST_ASSERT_TRUE(timetable_day(&only_2025, d, &est));
        TEST_ASSERT_EQUAL(ORIGIN_ESTIMATED, est.origin);
        TEST_ASSERT_EQUAL(0, est.hijri.year);
        for (int p = 0; p < P_COUNT; p++) {
            int dev = abs((int)est.minute[p] - (int)real->minute[p]);
            worst = dev > worst ? dev : worst;
        }
        days++;
    }
    printf("fallback max deviation over %d days: %d min\n", days, worst);
    TEST_ASSERT_EQUAL(365, days);
    TEST_ASSERT_LESS_OR_EQUAL(1, worst);
    free(y2025);
    free(y2026);
}

static void test_official_day_wins_over_estimate(void)
{
    year_table_t *y2025 = load_esolat("esolat/SGR03-2025.json", 2025, PARSE_OK);
    year_table_t *y2026 = load_esolat("esolat/SGR03-year.json", 2026, PARSE_OK);
    timetable_t both = {.tables = {y2025, y2026}, .count = 2};
    day_times_t d;
    TEST_ASSERT_TRUE(timetable_day(&both, OCT9, &d));
    TEST_ASSERT_EQUAL(ORIGIN_OFFICIAL, d.origin);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(KLANG_9_OCT, d.minute, P_COUNT);
    TEST_ASSERT_TRUE(timetable_day(&both, (date_t){2027, 1, 5}, &d));
    TEST_ASSERT_EQUAL(ORIGIN_ESTIMATED, d.origin);
    TEST_ASSERT_EQUAL(2027, d.date.year);
    TEST_ASSERT_FALSE(timetable_day(&both, (date_t){2028, 1, 5}, &d));
    free(y2025);
    free(y2026);
}

static void test_leap_day_estimate_uses_feb_28(void)
{
    year_table_t *y2024 = load_esolat("esolat/SGR03-2024.json", 2024, PARSE_OK);
    year_table_t *y2023 = load_esolat("esolat/SGR03-2023.json", 2023, PARSE_OK);
    timetable_t tt = {.tables = {y2023}, .count = 1};
    day_times_t d;
    TEST_ASSERT_TRUE(timetable_day(&tt, (date_t){2024, 2, 29}, &d));
    TEST_ASSERT_EQUAL_UINT16_ARRAY(table_day(y2023, (date_t){2023, 2, 28})->minute, d.minute, P_COUNT);
    TEST_ASSERT_NOT_NULL(table_day(y2024, (date_t){2024, 2, 29}));
    free(y2023);
    free(y2024);
}

static void test_cache_blob_round_trips_and_detects_corruption(void)
{
    year_table_t *t = load_esolat("esolat/SGR03-year.json", 2026, PARSE_OK);
    static uint8_t blob[YEAR_TABLE_BLOB_MAX];
    size_t n = year_table_serialize(t, blob, sizeof(blob));
    TEST_ASSERT_GREATER_THAN(0, n);
    year_table_t *back = calloc(1, sizeof(*back));
    TEST_ASSERT_TRUE(year_table_deserialize(blob, n, back));
    TEST_ASSERT_EQUAL_STRING("SGR03", back->zone);
    TEST_ASSERT_EQUAL(2026, back->year);
    TEST_ASSERT_EQUAL(365, year_table_day_count(back));
    for (int i = 0; i < YEAR_MAX_DAYS; i++) {
        if (t->present[i]) {
            TEST_ASSERT_EQUAL(0, date_cmp(t->days[i].date, back->days[i].date));
            TEST_ASSERT_EQUAL(0, memcmp(&t->days[i].hijri, &back->days[i].hijri, sizeof(date_t)));
            TEST_ASSERT_EQUAL_UINT16_ARRAY(t->days[i].minute, back->days[i].minute, P_COUNT);
        }
    }
    blob[100] ^= 1;
    TEST_ASSERT_FALSE(year_table_deserialize(blob, n, back));
    TEST_ASSERT_FALSE(year_table_deserialize(blob, n - 1, back));
    free(t);
    free(back);
}

static void test_schedule_before_subuh_is_still_isyak_period(void)
{
    day_times_t today = day_of(OCT9, KLANG_9_OCT);
    schedule_t s = schedule_at(&today, NULL, at(OCT9, 4, 0, 0));
    TEST_ASSERT_EQUAL(P_ISYAK, s.current);
    TEST_ASSERT_EQUAL(P_SUBUH, s.next);
    TEST_ASSERT_EQUAL((5 * 60 + 52 - 4 * 60) * 60, s.seconds_to_next);
}

static void test_schedule_after_syuruk_counts_down_to_zohor_not_syuruk(void)
{
    day_times_t today = day_of(OCT9, KLANG_9_OCT);
    schedule_t s = schedule_at(&today, NULL, at(OCT9, 6, 30, 0));
    TEST_ASSERT_EQUAL(P_SUBUH, s.current);
    TEST_ASSERT_EQUAL(P_ZOHOR, s.next);
    s = schedule_at(&today, NULL, at(OCT9, 7, 0, 0));
    TEST_ASSERT_EQUAL(P_SYURUK, s.current);
    TEST_ASSERT_EQUAL(P_ZOHOR, s.next);
    TEST_ASSERT_EQUAL((13 * 60 + 4 - 7 * 60) * 60, s.seconds_to_next);
}

static void test_schedule_after_isyak_points_to_tomorrows_subuh(void)
{
    day_times_t today = day_of(OCT9, KLANG_9_OCT);
    uint16_t m[P_COUNT];
    memcpy(m, KLANG_9_OCT, sizeof(m));
    m[P_SUBUH] = HM(5, 53);
    day_times_t tomorrow = day_of((date_t){2026, 10, 10}, m);
    schedule_t s = schedule_at(&today, &tomorrow, at(OCT9, 21, 0, 0));
    TEST_ASSERT_EQUAL(P_ISYAK, s.current);
    TEST_ASSERT_EQUAL(P_SUBUH, s.next);
    TEST_ASSERT_EQUAL(10, s.next_date.day);
    TEST_ASSERT_EQUAL(3 * 3600 + (5 * 60 + 53) * 60, s.seconds_to_next);
    s = schedule_at(&today, NULL, at(OCT9, 21, 0, 0));
    TEST_ASSERT_EQUAL(-1, s.seconds_to_next);
}

static void test_adhan_fires_once_and_not_again_after_reboot(void)
{
    day_times_t today = day_of(OCT9, KLANG_9_OCT);
    adhan_mark_t none = {0};
    prayer_t p;
    TEST_ASSERT_TRUE(adhan_due(&today, at(OCT9, 13, 4, 0), ADHAN_MASK_ALL, none, &p));
    TEST_ASSERT_EQUAL(P_ZOHOR, p);
    adhan_mark_t mark = {OCT9, P_ZOHOR};
    TEST_ASSERT_FALSE(adhan_due(&today, at(OCT9, 13, 4, 30), ADHAN_MASK_ALL, mark, &p));
    TEST_ASSERT_TRUE(adhan_due(&today, at(OCT9, 16, 17, 0), ADHAN_MASK_ALL, mark, &p));
    TEST_ASSERT_EQUAL(P_ASAR, p);
}

static void test_adhan_does_not_play_late_or_without_time(void)
{
    day_times_t today = day_of(OCT9, KLANG_9_OCT);
    adhan_mark_t none = {0};
    prayer_t p;
    TEST_ASSERT_FALSE(adhan_due(&today, at(OCT9, 13, 5, 0), ADHAN_MASK_ALL, none, &p));
    wallclock_t unsynced = at(OCT9, 13, 4, 0);
    unsynced.valid = false;
    TEST_ASSERT_FALSE(adhan_due(&today, unsynced, ADHAN_MASK_ALL, none, &p));
    TEST_ASSERT_FALSE(adhan_due(&today, at((date_t){2026, 10, 10}, 13, 4, 0), ADHAN_MASK_ALL, none, &p));
}

static void test_adhan_respects_mask_and_skips_imsak_and_syuruk(void)
{
    day_times_t today = day_of(OCT9, KLANG_9_OCT);
    adhan_mark_t none = {0};
    prayer_t p;
    uint8_t no_zohor = ADHAN_MASK_ALL & (uint8_t)~(1u << P_ZOHOR);
    TEST_ASSERT_FALSE(adhan_due(&today, at(OCT9, 13, 4, 0), no_zohor, none, &p));
    TEST_ASSERT_FALSE(adhan_due(&today, at(OCT9, 5, 42, 0), 0xFF, none, &p));
    TEST_ASSERT_FALSE(adhan_due(&today, at(OCT9, 6, 58, 0), 0xFF, none, &p));
}

static void test_adhan_mark_from_yesterday_does_not_block_today(void)
{
    day_times_t today = day_of(OCT9, KLANG_9_OCT);
    adhan_mark_t yesterday_isyak = {{2026, 10, 8}, P_ISYAK};
    prayer_t p;
    TEST_ASSERT_TRUE(adhan_due(&today, at(OCT9, 5, 52, 0), ADHAN_MASK_ALL, yesterday_isyak, &p));
    TEST_ASSERT_EQUAL(P_SUBUH, p);
}

static void test_night_dim_window(void)
{
    day_times_t today = day_of(OCT9, KLANG_9_OCT);
    TEST_ASSERT_FALSE(night_dim_at(&today, at(OCT9, 21, 13, 0)));
    TEST_ASSERT_TRUE(night_dim_at(&today, at(OCT9, 21, 14, 0)));
    TEST_ASSERT_TRUE(night_dim_at(&today, at(OCT9, 5, 21, 0)));
    TEST_ASSERT_FALSE(night_dim_at(&today, at(OCT9, 5, 22, 0)));
}

static void test_malay_formatting(void)
{
    char buf[64];
    ms_format_date(OCT9, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("Jumaat, 9 Oktober 2026", buf);
    ms_format_hijri((date_t){1448, 4, 27}, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("27 Rabiulakhir 1448H", buf);
    ms_format_hijri((date_t){0}, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("", buf);
    ms_format_time(HM(5, 2), buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("5:02 AM", buf);
    ms_format_time(HM(13, 4), buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("1:04 PM", buf);
    ms_format_time(HM(12, 0), buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("12:00 PM", buf);
    ms_format_time(HM(0, 30), buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("12:30 AM", buf);
    ms_format_countdown(2 * 3600 + 14 * 60, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("2 jam 14 minit", buf);
    ms_format_countdown(13 * 60 + 1, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("14 minit", buf);
    ms_format_countdown(45, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("45 saat", buf);
}

static void test_settings_apply_valid_and_reject_invalid_atomically(void)
{
    settings_t s = settings_defaults();
    char err[64];
    const char *ok = "{\"zone\":\"WLY01\",\"volume\":40,\"adhan\":{\"Subuh\":false},\"theme\":2}";
    TEST_ASSERT_TRUE(settings_apply_json(&s, ok, strlen(ok), err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("WLY01", s.zone);
    TEST_ASSERT_EQUAL(40, s.volume);
    TEST_ASSERT_EQUAL(THEME_BIRU, s.theme);
    TEST_ASSERT_FALSE(s.adhan_mask & (1u << P_SUBUH));
    TEST_ASSERT_TRUE(s.adhan_mask & (1u << P_ZOHOR));

    settings_t before = s;
    const char *bad = "{\"volume\":10,\"zone\":\"XXX99\"}";
    TEST_ASSERT_FALSE(settings_apply_json(&s, bad, strlen(bad), err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("Zon tidak dikenali", err);
    TEST_ASSERT_EQUAL(0, memcmp(&before, &s, sizeof(s)));

    const char *syuruk = "{\"adhan\":{\"Syuruk\":true}}";
    TEST_ASSERT_FALSE(settings_apply_json(&s, syuruk, strlen(syuruk), err, sizeof(err)));
    const char *mawaqit = "{\"source\":\"mawaqit\"}";
    TEST_ASSERT_FALSE(settings_apply_json(&s, mawaqit, strlen(mawaqit), err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("Mawaqit memerlukan ID masjid dan token", err);
}

static void test_settings_json_never_exposes_token_or_pin(void)
{
    settings_t s = settings_defaults();
    snprintf(s.mawaqit_token, sizeof(s.mawaqit_token), "secret-token");
    snprintf(s.pin, sizeof(s.pin), "123456");
    char *json = settings_to_json(&s);
    TEST_ASSERT_NULL(strstr(json, "secret-token"));
    TEST_ASSERT_NULL(strstr(json, "123456"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"mawaqit_token_set\":true"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"zone\":\"SGR03\""));
    free(json);
}

static void test_mawaqit_today_and_calendar(void)
{
    const char *json =
        "{\"name\":\"Masjid Contoh\",\"times\":[\"05:50\",\"07:00\",\"13:05\",\"16:20\",\"19:06\",\"20:16\"],"
        "\"calendar\":[{\"1\":[\"06:00\",\"07:20\",\"13:20\",\"16:40\",\"19:15\",\"20:30\"]}]}";
    year_table_t *t = calloc(1, sizeof(*t));
    char name[32];
    TEST_ASSERT_EQUAL(PARSE_OK, mawaqit_parse(json, strlen(json), OCT9, t, name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("Masjid Contoh", name);
    const day_times_t *d = table_day(t, OCT9);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_EQUAL(ORIGIN_MAWAQIT, d->origin);
    TEST_ASSERT_EQUAL(HM(5, 40), d->minute[P_IMSAK]);
    TEST_ASSERT_EQUAL(HM(13, 5), d->minute[P_ZOHOR]);
    TEST_ASSERT_NOT_NULL(table_day(t, (date_t){2026, 1, 1}));
    TEST_ASSERT_EQUAL(PARSE_BAD_DATA, mawaqit_parse("{}", 2, OCT9, t, name, sizeof(name)));
    free(t);
}

static void test_dates(void)
{
    TEST_ASSERT_EQUAL(5, date_weekday(OCT9));
    TEST_ASSERT_EQUAL(0, date_cmp(date_add((date_t){2026, 12, 31}, 1), (date_t){2027, 1, 1}));
    TEST_ASSERT_FALSE(date_valid((date_t){2026, 2, 29}));
    TEST_ASSERT_TRUE(date_valid((date_t){2028, 2, 29}));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_dates);
    RUN_TEST(test_esolat_year_matches_official_klang_times);
    RUN_TEST(test_esolat_parses_every_malay_month_abbreviation);
    RUN_TEST(test_esolat_no_record_object_shape);
    RUN_TEST(test_esolat_duration_crossing_year_end_is_truncated);
    RUN_TEST(test_esolat_rejects_row_with_out_of_order_times);
    RUN_TEST(test_esolat_garbage_is_bad_data);
    RUN_TEST(test_previous_year_estimate_is_within_one_minute_for_a_whole_year);
    RUN_TEST(test_official_day_wins_over_estimate);
    RUN_TEST(test_leap_day_estimate_uses_feb_28);
    RUN_TEST(test_cache_blob_round_trips_and_detects_corruption);
    RUN_TEST(test_schedule_before_subuh_is_still_isyak_period);
    RUN_TEST(test_schedule_after_syuruk_counts_down_to_zohor_not_syuruk);
    RUN_TEST(test_schedule_after_isyak_points_to_tomorrows_subuh);
    RUN_TEST(test_adhan_fires_once_and_not_again_after_reboot);
    RUN_TEST(test_adhan_does_not_play_late_or_without_time);
    RUN_TEST(test_adhan_respects_mask_and_skips_imsak_and_syuruk);
    RUN_TEST(test_adhan_mark_from_yesterday_does_not_block_today);
    RUN_TEST(test_night_dim_window);
    RUN_TEST(test_malay_formatting);
    RUN_TEST(test_settings_apply_valid_and_reject_invalid_atomically);
    RUN_TEST(test_settings_json_never_exposes_token_or_pin);
    RUN_TEST(test_mawaqit_today_and_calendar);
    return UNITY_END();
}
