#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "fetch";

#define ESOLAT_API "https://www.e-solat.gov.my/index.php?r=esolatApi/TakwimSolat"
#define MAWAQIT_API "https://mawaqit.net/api/2.0"
#define BODY_MAX (256 * 1024)
#define BACKOFF_MAX_S (60 * 60)

static TaskHandle_t task;

typedef struct {
    char *buf;
    size_t len;
} body_t;

static esp_err_t on_data(esp_http_client_event_t *e)
{
    body_t *b = e->user_data;
    if (e->event_id == HTTP_EVENT_ON_DATA && b->len + e->data_len < BODY_MAX) {
        memcpy(b->buf + b->len, e->data, e->data_len);
        b->len += e->data_len;
        b->buf[b->len] = '\0';
    }
    return ESP_OK;
}

/* Returns the HTTP status, or -1 on a transport error. */
static int http_request(const char *url, const char *post, const char *token, body_t *b)
{
    b->len = 0;
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = on_data,
        .user_data = b,
        .timeout_ms = 20000,
        .buffer_size = 4096,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_http_client_set_header(c, "Accept", "application/json");
    if (token) {
        esp_http_client_set_header(c, "Api-Access-Token", token);
        char bearer[160];
        snprintf(bearer, sizeof(bearer), "Bearer %s", token);
        esp_http_client_set_header(c, "Authorization", bearer);
    }
    if (post) {
        esp_http_client_set_method(c, HTTP_METHOD_POST);
        esp_http_client_set_header(c, "Content-Type", "application/x-www-form-urlencoded");
        esp_http_client_set_post_field(c, post, (int)strlen(post));
    }
    esp_err_t err = esp_http_client_perform(c);
    int status = err == ESP_OK ? esp_http_client_get_status_code(c) : -1;
    esp_http_client_cleanup(c);
    ESP_LOGI(TAG, "%s %s -> %d (%u bytes)", post ? "POST" : "GET", url, status, (unsigned)b->len);
    return status;
}

/* Fetches one calendar year for the zone. Returns PARSE_OK, PARSE_NO_RECORD,
 * or PARSE_BAD_DATA (network or server error). */
static parse_status_t fetch_esolat_year(const char *zone, int16_t year, body_t *b, year_table_t *t)
{
    char url[160], post[64];
    snprintf(url, sizeof(url), ESOLAT_API "&period=duration&zone=%s", zone);
    snprintf(post, sizeof(post), "datestart=%d-01-01&dateend=%d-12-31", year, year);
    if (http_request(url, post, NULL, b) != 200) {
        return PARSE_BAD_DATA;
    }
    parse_status_t s = esolat_parse(b->buf, b->len, zone, year, t);
    ESP_LOGI(TAG, "e-solat %s %d: %s, %d days", zone, year,
             s == PARSE_OK ? "ok" : s == PARSE_NO_RECORD ? "not published" : "bad data", year_table_day_count(t));
    return s;
}

static bool refresh_esolat(const char *zone, wallclock_t now, body_t *b, year_table_t *t)
{
    int16_t year = now.date.year;
    parse_status_t s = fetch_esolat_year(zone, year, b, t);
    if (s == PARSE_BAD_DATA) {
        return false;
    }
    if (s == PARSE_OK) {
        storage_save_year(t);
        tables_put(t);
    }
    if (s == PARSE_NO_RECORD || !tables_has(year - 1)) {
        /* This year is not out yet, or we have no estimate base for early next year. */
        if (fetch_esolat_year(zone, year - 1, b, t) == PARSE_OK) {
            storage_save_year(t);
            tables_put(t);
        }
    }
    if (now.date.month == 12 && !tables_has(year + 1)) {
        if (fetch_esolat_year(zone, year + 1, b, t) == PARSE_OK) {
            storage_save_year(t);
            tables_put(t);
        }
    }
    return true;
}

static bool refresh_mawaqit(const settings_t *s, wallclock_t now, body_t *b, year_table_t *t)
{
    char url[192];
    snprintf(url, sizeof(url), MAWAQIT_API "/mosque/%s/times", s->mawaqit_uuid);
    int status = http_request(url, NULL, s->mawaqit_token, b);
    if (status == 401 || status == 403) {
        ESP_LOGW(TAG, "Mawaqit rejected the token (%d)", status);
        return false;
    }
    char name[64];
    if (status != 200 || mawaqit_parse(b->buf, b->len, now.date, t, name, sizeof(name)) != PARSE_OK) {
        return false;
    }
    snprintf(t->zone, sizeof(t->zone), "MAWAQIT");
    storage_save_year(t);
    tables_put(t);
    app_set_mawaqit_name(name);
    return true;
}

static void fetch_task(void *arg)
{
    body_t body = {.buf = heap_caps_malloc(BODY_MAX, MALLOC_CAP_SPIRAM)};
    year_table_t *t = heap_caps_malloc(sizeof(year_table_t), MALLOC_CAP_SPIRAM);
    date_t done_for = {0};
    char done_key[200] = "";
    int backoff_s = 60;

    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(60 * 1000));
        wallclock_t now = clock_now();
        if (!now.valid || net_state() != UI_NET_ONLINE) {
            continue;
        }
        settings_t s = app_settings();
        char key[200];
        snprintf(key, sizeof(key), "%d|%s|%s|%s", s.source, s.zone, s.mawaqit_uuid, s.mawaqit_token);
        bool settings_changed = strcmp(key, done_key) != 0;
        bool new_day = date_cmp(done_for, now.date) != 0 && now.second >= 5 * 60;
        if (!settings_changed && !new_day) {
            continue;
        }
        bool ok = s.source == SOURCE_MAWAQIT ? refresh_mawaqit(&s, now, &body, t) : refresh_esolat(s.zone, now, &body, t);
        if (!ok && s.source == SOURCE_MAWAQIT) {
            ESP_LOGW(TAG, "Mawaqit failed, using e-solat for %s", s.zone);
            ok = refresh_esolat(s.zone, now, &body, t);
        }
        app_set_stale(!ok);
        if (ok) {
            done_for = now.date;
            snprintf(done_key, sizeof(done_key), "%s", key);
            backoff_s = 60;
        } else {
            ESP_LOGW(TAG, "refresh failed, retrying in %d s", backoff_s);
            vTaskDelay(pdMS_TO_TICKS(backoff_s * 1000));
            backoff_s = backoff_s * 2 > BACKOFF_MAX_S ? BACKOFF_MAX_S : backoff_s * 2;
            xTaskNotifyGive(task);
        }
    }
}

void fetch_kick(void)
{
    if (task) {
        xTaskNotifyGive(task);
    }
}

void fetch_start(void)
{
    xTaskCreatePinnedToCore(fetch_task, "fetch", 12288, NULL, 3, &task, 0);
}
