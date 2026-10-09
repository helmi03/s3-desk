#include <stdio.h>

#include "ms.h"

static const char *const WEEKDAYS[7] = {"Ahad", "Isnin", "Selasa", "Rabu", "Khamis", "Jumaat", "Sabtu"};

static const char *const MONTHS[12] = {"Januari", "Februari", "Mac",   "April",   "Mei",      "Jun",
                                       "Julai",   "Ogos",     "September", "Oktober", "November", "Disember"};

static const char *const HIJRI_MONTHS[12] = {"Muharam", "Safar",  "Rabiulawal", "Rabiulakhir",
                                             "Jamadilawal", "Jamadilakhir", "Rejab", "Syaaban",
                                             "Ramadan", "Syawal", "Zulkaedah", "Zulhijah"};

const char *ms_weekday(int weekday)
{
    return (weekday >= 0 && weekday < 7) ? WEEKDAYS[weekday] : "";
}

const char *ms_month(int month)
{
    return (month >= 1 && month <= 12) ? MONTHS[month - 1] : "";
}

const char *ms_hijri_month(int month)
{
    return (month >= 1 && month <= 12) ? HIJRI_MONTHS[month - 1] : "";
}

static size_t clamp(int n, size_t cap)
{
    if (n < 0) {
        return 0;
    }
    return (size_t)n < cap ? (size_t)n : (cap ? cap - 1 : 0);
}

size_t ms_format_date(date_t d, char *buf, size_t cap)
{
    return clamp(snprintf(buf, cap, "%s, %d %s %d", ms_weekday(date_weekday(d)), d.day, ms_month(d.month), d.year), cap);
}

size_t ms_format_hijri(date_t h, char *buf, size_t cap)
{
    if (h.year == 0) {
        return clamp(snprintf(buf, cap, "%s", ""), cap);
    }
    return clamp(snprintf(buf, cap, "%d %s %dH", h.day, ms_hijri_month(h.month), h.year), cap);
}

size_t ms_format_hhmm(uint16_t minute, char *buf, size_t cap)
{
    return clamp(snprintf(buf, cap, "%02u:%02u", minute / 60u, minute % 60u), cap);
}

size_t ms_format_countdown(int32_t seconds, char *buf, size_t cap)
{
    if (seconds < 0) {
        return clamp(snprintf(buf, cap, "%s", "-"), cap);
    }
    if (seconds < 60) {
        return clamp(snprintf(buf, cap, "%ld saat", (long)seconds), cap);
    }
    int32_t minutes = (seconds + 59) / 60;
    if (minutes < 60) {
        return clamp(snprintf(buf, cap, "%ld minit", (long)minutes), cap);
    }
    return clamp(snprintf(buf, cap, "%ld jam %ld minit", (long)(minutes / 60), (long)(minutes % 60)), cap);
}
