#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "storage";
#define NS "waktu"
#define SETTINGS_VERSION 1

typedef struct {
    uint32_t version;
    settings_t s;
} settings_blob_t;

esp_err_t storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    err = bsp_spiffs_mount();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
    }
    return err;
}

static bool nvs_get(const char *key, void *out, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t got = len;
    bool ok = nvs_get_blob(h, key, out, &got) == ESP_OK && got == len;
    nvs_close(h);
    return ok;
}

static void nvs_put(const char *key, const void *data, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed");
        return;
    }
    if (nvs_set_blob(h, key, data, len) != ESP_OK || nvs_commit(h) != ESP_OK) {
        ESP_LOGE(TAG, "write %s failed", key);
    }
    nvs_close(h);
}

static void random_digits(char *out, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        out[i] = (char)('0' + esp_random() % 10);
    }
    out[n] = '\0';
}

settings_t storage_load_settings(void)
{
    settings_blob_t b;
    if (nvs_get("settings", &b, sizeof(b)) && b.version == SETTINGS_VERSION) {
        return b.s;
    }
    settings_t s = settings_defaults();
    random_digits(s.pin, 6);
    storage_save_settings(&s);
    ESP_LOGI(TAG, "settings initialised with defaults");
    return s;
}

void storage_save_settings(const settings_t *s)
{
    settings_blob_t b = {.version = SETTINGS_VERSION, .s = *s};
    nvs_put("settings", &b, sizeof(b));
}

adhan_mark_t storage_load_mark(void)
{
    adhan_mark_t m = {0};
    nvs_get("mark", &m, sizeof(m));
    return m;
}

void storage_save_mark(adhan_mark_t m)
{
    nvs_put("mark", &m, sizeof(m));
}

void storage_get_ap_pass(char *out, size_t cap)
{
    char pass[16] = {0};
    if (!nvs_get("ap_pass", pass, 9) || strlen(pass) != 8) {
        random_digits(pass, 8);
        nvs_put("ap_pass", pass, 9);
    }
    snprintf(out, cap, "%s", pass);
}

static void year_path(const char *zone, int16_t year, char *out, size_t cap)
{
    snprintf(out, cap, SPIFFS_ROOT "/t-%s-%d.bin", zone, year);
}

esp_err_t storage_write_file_atomic(const char *path, const void *data, size_t len)
{
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        return ESP_FAIL;
    }
    size_t wrote = fwrite(data, 1, len, f);
    if (fclose(f) != 0 || wrote != len) {
        unlink(tmp);
        return ESP_FAIL;
    }
    unlink(path);
    return rename(tmp, path) == 0 ? ESP_OK : ESP_FAIL;
}

bool storage_load_year(const char *zone, int16_t year, year_table_t *out)
{
    char path[64];
    year_path(zone, year, path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    uint8_t *buf = malloc(YEAR_TABLE_BLOB_MAX);
    size_t len = buf ? fread(buf, 1, YEAR_TABLE_BLOB_MAX, f) : 0;
    fclose(f);
    bool ok = buf && year_table_deserialize(buf, len, out) && out->year == year && strcmp(out->zone, zone) == 0;
    free(buf);
    if (!ok) {
        ESP_LOGW(TAG, "discarding bad cache %s", path);
        unlink(path);
    }
    return ok;
}

bool storage_save_year(const year_table_t *t)
{
    uint8_t *buf = malloc(YEAR_TABLE_BLOB_MAX);
    if (!buf) {
        return false;
    }
    size_t len = year_table_serialize(t, buf, YEAR_TABLE_BLOB_MAX);
    char path[64];
    year_path(t->zone, t->year, path, sizeof(path));
    bool ok = len && storage_write_file_atomic(path, buf, len) == ESP_OK;
    free(buf);
    ESP_LOGI(TAG, "saved %s: %s", path, ok ? "ok" : "FAILED");
    return ok;
}
