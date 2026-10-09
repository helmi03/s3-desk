#pragma once
#include <stddef.h>

#include "prayer.h"

/* Malay names for dates shown on screen and in the web UI. */
const char *ms_weekday(int weekday); /* 0 = Ahad */
const char *ms_month(int month);     /* 1..12 */
const char *ms_hijri_month(int month);

/* "Jumaat, 9 Oktober 2026" */
size_t ms_format_date(date_t d, char *buf, size_t cap);
/* "27 Rabiulakhir 1448H", or "" when the Hijri date is unknown */
size_t ms_format_hijri(date_t h, char *buf, size_t cap);
/* 12-hour clock: "1:04 PM", "5:52 AM", "12:30 AM" */
size_t ms_format_time(uint16_t minute, char *buf, size_t cap);
/* "2 jam 14 minit", "14 minit", "45 saat" */
size_t ms_format_countdown(int32_t seconds, char *buf, size_t cap);
