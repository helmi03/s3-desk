#pragma once
#include <stddef.h>

typedef struct {
    const char *code;
    const char *state;
    const char *name;
} zone_t;

extern const zone_t ZONES[];
extern const size_t ZONE_COUNT;

const zone_t *zone_find(const char *code);
