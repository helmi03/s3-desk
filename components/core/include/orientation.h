#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Quarter turns clockwise from upright: 0, 1 (90), 2 (180), 3 (270). */
typedef uint8_t quarter_t;

/* Accelerometer readings in milli-g, in the QMI8658's axes as mounted on the
 * board: standing upright gravity reads about x = -1000. */
typedef struct {
    int16_t x, y, z;
} accel_mg_t;

typedef struct {
    quarter_t stable;    /* last orientation held long enough */
    quarter_t candidate; /* orientation currently being seen */
    uint16_t held_ms;
} orientation_t;

#define ORIENT_HOLD_MS 1000

/* Feed one reading taken `dt_ms` after the previous one. Returns true when
 * the stable orientation changed. Ignores readings while the board lies flat
 * or is tilted between two orientations. */
bool orientation_update(orientation_t *o, accel_mg_t a, uint16_t dt_ms);

/* The orientation a single reading points to, or false if it is ambiguous. */
bool orientation_of(accel_mg_t a, quarter_t *out);

/* Maps a touch point in upright panel coordinates to LVGL coordinates when
 * the panel is rotated by `q` (panel and touch share one transform). */
void orientation_map_touch(quarter_t q, int size, int ux, int uy, int *lx, int *ly);

/* MADCTL value the CO5300 needs for `q`. */
uint8_t orientation_madctl(quarter_t q);
