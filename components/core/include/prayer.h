#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    P_IMSAK,
    P_SUBUH,
    P_SYURUK,
    P_ZOHOR,
    P_ASAR,
    P_MAGHRIB,
    P_ISYAK,
    P_COUNT
} prayer_t;

typedef struct {
    const char *name;
    bool adhan;
} prayer_info_t;

extern const prayer_info_t PRAYERS[P_COUNT];

#define ADHAN_MASK_ALL ((uint8_t)((1u << P_SUBUH) | (1u << P_ZOHOR) | (1u << P_ASAR) | (1u << P_MAGHRIB) | (1u << P_ISYAK)))

typedef struct {
    int16_t year;
    int8_t month;
    int8_t day;
} date_t;

typedef enum {
    ORIGIN_OFFICIAL,
    ORIGIN_MAWAQIT,
    ORIGIN_ESTIMATED,
} origin_t;

typedef struct {
    date_t date;
    date_t hijri; /* year 0 when unknown */
    uint16_t minute[P_COUNT]; /* minutes after local midnight */
    origin_t origin;
} day_times_t;

int32_t date_to_days(date_t d);
date_t date_from_days(int32_t days);
date_t date_add(date_t d, int32_t days);
int date_cmp(date_t a, date_t b);
bool date_valid(date_t d);
int date_weekday(date_t d); /* 0 = Ahad (Sunday) */
bool day_times_ordered(const day_times_t *d);
