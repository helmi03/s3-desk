#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "settings.h"
#include "zones.h"

const char *const THEME_NAMES[THEME_COUNT] = {"Gelap", "Hijau", "Biru", "Ungu"};

settings_t settings_defaults(void)
{
    settings_t s = {
        .zone = "SGR03",
        .adhan_mask = ADHAN_MASK_ALL,
        .volume = 70,
        .brightness = 80,
        .night_dim = true,
        .theme = THEME_GELAP,
        .rotation = 0,
        .source = SOURCE_ESOLAT,
    };
    return s;
}

static bool fail(char *err, size_t cap, const char *msg)
{
    if (err && cap) {
        snprintf(err, cap, "%s", msg);
    }
    return false;
}

static bool get_int(const cJSON *root, const char *key, int lo, int hi, int *out, bool *present)
{
    const cJSON *v = cJSON_GetObjectItem(root, key);
    *present = v != NULL;
    if (!v) {
        return true;
    }
    if (!cJSON_IsNumber(v) || v->valuedouble < lo || v->valuedouble > hi) {
        return false;
    }
    *out = v->valueint;
    return true;
}

bool settings_apply_json(settings_t *s, const char *json, size_t len, char *err, size_t err_cap)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return fail(err, err_cap, "JSON tidak sah");
    }
    settings_t n = *s;
    bool ok = true;
    const char *msg = NULL;
    int v;
    bool present;

    const cJSON *zone = cJSON_GetObjectItem(root, "zone");
    if (zone) {
        if (!cJSON_IsString(zone) || !zone_find(zone->valuestring)) {
            ok = false;
            msg = "Zon tidak dikenali";
        } else {
            snprintf(n.zone, sizeof(n.zone), "%s", zone->valuestring);
        }
    }
    const cJSON *adhan = cJSON_GetObjectItem(root, "adhan");
    if (ok && adhan) {
        if (!cJSON_IsObject(adhan)) {
            ok = false;
            msg = "Tetapan azan tidak sah";
        }
        for (int p = 0; ok && p < P_COUNT; p++) {
            const cJSON *e = cJSON_GetObjectItem(adhan, PRAYERS[p].name);
            if (!e) {
                continue;
            }
            if (!PRAYERS[p].adhan || !cJSON_IsBool(e)) {
                ok = false;
                msg = "Tetapan azan tidak sah";
            } else if (cJSON_IsTrue(e)) {
                n.adhan_mask |= (uint8_t)(1u << p);
            } else {
                n.adhan_mask &= (uint8_t)~(1u << p);
            }
        }
    }
    if (ok && !(ok = get_int(root, "volume", 0, 100, &v, &present))) {
        msg = "Kelantangan mesti 0 hingga 100";
    } else if (ok && present) {
        n.volume = (uint8_t)v;
    }
    if (ok && !(ok = get_int(root, "brightness", 5, 100, &v, &present))) {
        msg = "Kecerahan mesti 5 hingga 100";
    } else if (ok && present) {
        n.brightness = (uint8_t)v;
    }
    if (ok && !(ok = get_int(root, "theme", 0, THEME_COUNT - 1, &v, &present))) {
        msg = "Tema tidak sah";
    } else if (ok && present) {
        n.theme = (theme_t)v;
    }
    if (ok && !(ok = get_int(root, "rotation", 0, 2, &v, &present))) {
        msg = "Putaran tidak sah";
    } else if (ok && present) {
        n.rotation = v ? 2 : 0;
    }
    const cJSON *dim = cJSON_GetObjectItem(root, "night_dim");
    if (ok && dim) {
        if (!cJSON_IsBool(dim)) {
            ok = false;
            msg = "Tetapan malap malam tidak sah";
        } else {
            n.night_dim = cJSON_IsTrue(dim);
        }
    }
    const cJSON *source = cJSON_GetObjectItem(root, "source");
    if (ok && source) {
        if (cJSON_IsString(source) && strcmp(source->valuestring, "esolat") == 0) {
            n.source = SOURCE_ESOLAT;
        } else if (cJSON_IsString(source) && strcmp(source->valuestring, "mawaqit") == 0) {
            n.source = SOURCE_MAWAQIT;
        } else {
            ok = false;
            msg = "Sumber tidak sah";
        }
    }
    const cJSON *uuid = cJSON_GetObjectItem(root, "mawaqit_uuid");
    if (ok && uuid) {
        if (!cJSON_IsString(uuid) || strlen(uuid->valuestring) >= sizeof(n.mawaqit_uuid)) {
            ok = false;
            msg = "ID masjid Mawaqit tidak sah";
        } else {
            snprintf(n.mawaqit_uuid, sizeof(n.mawaqit_uuid), "%s", uuid->valuestring);
        }
    }
    const cJSON *token = cJSON_GetObjectItem(root, "mawaqit_token");
    if (ok && token) {
        if (!cJSON_IsString(token) || strlen(token->valuestring) >= sizeof(n.mawaqit_token)) {
            ok = false;
            msg = "Token Mawaqit tidak sah";
        } else {
            snprintf(n.mawaqit_token, sizeof(n.mawaqit_token), "%s", token->valuestring);
        }
    }
    if (ok && n.source == SOURCE_MAWAQIT && (!n.mawaqit_uuid[0] || !n.mawaqit_token[0])) {
        ok = false;
        msg = "Mawaqit memerlukan ID masjid dan token";
    }
    cJSON_Delete(root);
    if (!ok) {
        return fail(err, err_cap, msg);
    }
    *s = n;
    return true;
}

char *settings_to_json(const settings_t *s)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "zone", s->zone);
    cJSON *adhan = cJSON_AddObjectToObject(root, "adhan");
    for (int p = 0; p < P_COUNT; p++) {
        if (PRAYERS[p].adhan) {
            cJSON_AddBoolToObject(adhan, PRAYERS[p].name, (s->adhan_mask >> p) & 1u);
        }
    }
    cJSON_AddNumberToObject(root, "volume", s->volume);
    cJSON_AddNumberToObject(root, "brightness", s->brightness);
    cJSON_AddBoolToObject(root, "night_dim", s->night_dim);
    cJSON_AddNumberToObject(root, "theme", s->theme);
    cJSON_AddNumberToObject(root, "rotation", s->rotation);
    cJSON_AddStringToObject(root, "source", s->source == SOURCE_MAWAQIT ? "mawaqit" : "esolat");
    cJSON_AddStringToObject(root, "mawaqit_uuid", s->mawaqit_uuid);
    cJSON_AddBoolToObject(root, "mawaqit_token_set", s->mawaqit_token[0] != '\0');
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}
