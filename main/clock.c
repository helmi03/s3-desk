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

void clock_mark_synced(void)
{
    if (!synced) {
        ESP_LOGI(TAG, "time valid");
    }
    synced = true;
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
    clock_mark_synced();
}
