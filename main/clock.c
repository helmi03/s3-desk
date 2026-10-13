#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include "app.h"
#include "esp_log.h"

static const char *TAG = "clock";
static volatile bool synced;

void clock_init(void)
{
    setenv("TZ", "MYT-8", 1);
    tzset();
}

static void mark_valid(const char *source)
{
    if (!synced) {
        ESP_LOGI(TAG, "time valid (%s)", source);
    }
    synced = true;
}

void clock_load_rtc(void)
{
    time_t t;
    if (!hw_rtc_get(&t)) {
        ESP_LOGI(TAG, "RTC has no valid time, waiting for SNTP");
        return;
    }
    settimeofday(&(struct timeval){.tv_sec = t}, NULL);
    mark_valid("RTC");
}

void clock_sntp_synced(void)
{
    time_t now = time(NULL), rtc;
    if (hw_rtc_get(&rtc)) {
        ESP_LOGI(TAG, "RTC was %+lld s off SNTP", (long long)(rtc - now));
    }
    hw_rtc_set(now);
    mark_valid("SNTP");
}

void clock_trim_from_rtc(void)
{
    time_t rtc;
    if (!synced || !hw_rtc_get(&rtc)) {
        return;
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);
    /* The RTC counts whole seconds, so ignore differences it cannot resolve. */
    long long off = (long long)tv.tv_sec - rtc;
    if (off >= 2 || off <= -2) {
        ESP_LOGI(TAG, "system clock was %+lld s off the RTC, corrected", off);
        settimeofday(&(struct timeval){.tv_sec = rtc}, NULL);
    }
}

wallclock_t clock_now(void)
{
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    return (wallclock_t){
        .valid = synced && tm.tm_year + 1900 >= 2025,
        .date = {(int16_t)(tm.tm_year + 1900), (int8_t)(tm.tm_mon + 1), (int8_t)tm.tm_mday},
        .second = tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec,
    };
}

void clock_set_manual(date_t d, int32_t second_of_day)
{
    struct tm tm = {
        .tm_year = d.year - 1900,
        .tm_mon = d.month - 1,
        .tm_mday = d.day,
        .tm_hour = second_of_day / 3600,
        .tm_min = second_of_day / 60 % 60,
        .tm_sec = second_of_day % 60,
        .tm_isdst = 0,
    };
    struct timeval tv = {.tv_sec = mktime(&tm)};
    settimeofday(&tv, NULL);
    ESP_LOGI(TAG, "time set by hand to %04d-%02d-%02d %02ld:%02ld:%02ld", d.year, d.month, d.day,
             (long)(second_of_day / 3600), (long)(second_of_day / 60 % 60), (long)(second_of_day % 60));
    hw_rtc_set(time(NULL));
    mark_valid("set by hand");
}
