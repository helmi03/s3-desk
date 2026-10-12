#pragma once
#include "prayer.h"

typedef enum {
    THEME_GELAP,
    THEME_HIJAU,
    THEME_BIRU,
    THEME_UNGU,
    THEME_COUNT
} theme_t;

extern const char *const THEME_NAMES[THEME_COUNT];

typedef enum {
    SOURCE_ESOLAT,
    SOURCE_MAWAQIT,
} source_t;

typedef struct {
    char zone[8];
    uint8_t adhan_mask;
    uint8_t volume;     /* 0..100 */
    uint8_t brightness; /* 5..100 */
    bool night_dim;
    theme_t theme;
    uint8_t rotation; /* quarter turns clockwise, used when auto_rotate is off */
    source_t source;
    char mawaqit_uuid[48];
    char mawaqit_token[128];
    char pin[8]; /* web pairing PIN, generated on first boot */
    bool auto_rotate; /* follow the accelerometer; added after v1, keep last */
} settings_t;

settings_t settings_defaults(void);

/* Applies the fields present in a JSON object onto *s. Unknown or invalid
 * fields are rejected as a whole; *s is untouched on failure. `err` gets a
 * Malay message. */
bool settings_apply_json(settings_t *s, const char *json, size_t len, char *err, size_t err_cap);

/* JSON for the web UI. The Mawaqit token and the PIN are never included. */
char *settings_to_json(const settings_t *s);
