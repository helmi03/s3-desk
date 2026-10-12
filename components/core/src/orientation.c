#include <stdlib.h>

#include "orientation.h"

#define DOMINANT_MG 700

bool orientation_of(accel_mg_t a, quarter_t *out)
{
    if (abs(a.z) > DOMINANT_MG) {
        return false;
    }
    if (a.x < -DOMINANT_MG) {
        *out = 0;
    } else if (a.x > DOMINANT_MG) {
        *out = 2;
    } else if (a.y > DOMINANT_MG) {
        *out = 1;
    } else if (a.y < -DOMINANT_MG) {
        *out = 3;
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

/* The panel draws LVGL pixel L at physical position P = MY(MX(MV(L))) for
 * the MADCTL bits below; touch reports P, so LVGL needs the inverse. */
void orientation_map_touch(quarter_t q, int size, int ux, int uy, int *lx, int *ly)
{
    int m = size - 1;
    switch (q & 3) {
    case 1: /* MV|MX: P = (m - Ly, Lx) */
        *lx = uy;
        *ly = m - ux;
        break;
    case 2: /* MY|MX: P = (m - Lx, m - Ly) */
        *lx = m - ux;
        *ly = m - uy;
        break;
    case 3: /* MV|MY: P = (Ly, m - Lx) */
        *lx = m - uy;
        *ly = ux;
        break;
    default:
        *lx = ux;
        *ly = uy;
        break;
    }
}

uint8_t orientation_madctl(quarter_t q)
{
    static const uint8_t MADCTL[4] = {0x00, 0x60, 0xC0, 0xA0};
    return MADCTL[q & 3];
}
