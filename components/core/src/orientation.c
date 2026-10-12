#include <stdlib.h>

#include "orientation.h"

#define DOMINANT_MG 700

bool orientation_of(accel_mg_t a, quarter_t *out)
{
    if (abs(a.z) > DOMINANT_MG) {
        return false;
    }
    /* Measured on the board: buttons on top reads y = +1 g and needs a 270
     * turn; buttons on the right reads x = -1 g and showed the unturned
     * picture upside down. The panel's native "up" is the buttons-left side. */
    if (a.y > DOMINANT_MG) {
        *out = 3;
    } else if (a.x < -DOMINANT_MG) {
        *out = 2;
    } else if (a.y < -DOMINANT_MG) {
        *out = 1;
    } else if (a.x > DOMINANT_MG) {
        *out = 0;
    } else {
        return false;
    }
    return true;
}

bool orientation_update(orientation_t *o, accel_mg_t a, uint16_t dt_ms)
{
    quarter_t q;
    if (!orientation_of(a, &q)) {
        o->candidate = o->stable;
        o->held_ms = 0;
        return false;
    }
    if (q != o->candidate) {
        o->candidate = q;
        o->held_ms = 0;
    }
    if (q == o->stable) {
        return false;
    }
    o->held_ms = (uint16_t)(o->held_ms + dt_ms);
    if (o->held_ms < ORIENT_HOLD_MS) {
        return false;
    }
    o->stable = q;
    o->held_ms = 0;
    return true;
}

/* Physical position P of LVGL pixel L: software 90 degrees for odd quarters,
 * then the panel's mirror (0xC0 = both axes). Touch reports P; LVGL needs L. */
#define TOUCH_FRAME_QUARTER 3

static void unrotate(quarter_t q, int size, int ux, int uy, int *lx, int *ly)
{
    int m = size - 1;
    switch (q & 3) {
    case 1: /* software 90: P = (m - Ly, Lx) */
        *lx = uy;
        *ly = m - ux;
        break;
    case 2: /* panel mirror both: P = (m - Lx, m - Ly) */
        *lx = m - ux;
        *ly = m - uy;
        break;
    case 3: /* software 90 then panel mirror both: P = (Ly, m - Lx) */
        *lx = m - uy;
        *ly = ux;
        break;
    default:
        *lx = ux;
        *ly = uy;
        break;
    }
}

void orientation_map_touch(quarter_t q, int size, int ux, int uy, int *lx, int *ly)
{
    unrotate((quarter_t)(q - TOUCH_FRAME_QUARTER), size, ux, uy, lx, ly);
}

uint8_t orientation_madctl(quarter_t q)
{
    static const uint8_t MADCTL[4] = {0x00, 0x00, 0xC0, 0xC0};
    return MADCTL[q & 3];
}

bool orientation_software_rotate(quarter_t q)
{
    return q & 1;
}
