#include <string.h>

#include "zones.h"

const zone_t *zone_find(const char *code)
{
    for (size_t i = 0; i < ZONE_COUNT; i++) {
        if (strcmp(ZONES[i].code, code) == 0) {
            return &ZONES[i];
        }
    }
    return NULL;
}
