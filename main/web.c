#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "app.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ms.h"
#include "zones.h"

static const char *TAG = "web";

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

#define BODY_LIMIT 4096
#define MP3_LIMIT (3 * 1024 * 1024)

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, s);
    free(s);
    return err;
}

static esp_err_t send_error(httpd_req_t *req, const char *status, const char *msg)
{
    httpd_resp_set_status(req, status);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "error", msg);
    return send_json(req, o);
}

static esp_err_t send_ok(httpd_req_t *req)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    return send_json(req, o);
}

/* Mutating requests need the PIN shown on the device, except while the
 * device is in Wi-Fi setup (its access point is already password protected). */
static bool authorised(httpd_req_t *req)
{
    if (net_state() == UI_NET_SETUP) {
        return true;
    }
    char pin[16] = "";
    settings_t s = app_settings();
    return httpd_req_get_hdr_value_str(req, "X-PIN", pin, sizeof(pin)) == ESP_OK && strcmp(pin, s.pin) == 0;
}

static char *read_body(httpd_req_t *req)
{
    if (req->content_len > BODY_LIMIT) {
        return NULL;
    }
    char *buf = calloc(1, req->content_len + 1);
    size_t got = 0;
    while (buf && got < req->content_len) {
        int r = httpd_req_recv(req, buf + got, req->content_len - got);
        if (r <= 0) {
            free(buf);
            return NULL;
        }
        got += (size_t)r;
    }
    return buf;
}

static esp_err_t h_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start);
}

static void add_day(cJSON *arr, const day_times_t *d)
{
    cJSON *o = cJSON_CreateObject();
    char buf[64];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", d->date.year, d->date.month, d->date.day);
    cJSON_AddStringToObject(o, "date", buf);
    ms_format_date(d->date, buf, sizeof(buf));
    cJSON_AddStringToObject(o, "date_ms", buf);
    ms_format_hijri(d->hijri, buf, sizeof(buf));
    cJSON_AddStringToObject(o, "hijri", buf);
    cJSON_AddStringToObject(o, "origin", d->origin == ORIGIN_ESTIMATED ? "anggaran" : d->origin == ORIGIN_MAWAQIT ? "mawaqit" : "rasmi");
    cJSON *t = cJSON_AddObjectToObject(o, "times");
    for (int p = 0; p < P_COUNT; p++) {
        ms_format_time(d->minute[p], buf, sizeof(buf));
        cJSON_AddStringToObject(t, PRAYERS[p].name, buf);
    }
    cJSON_AddItemToArray(arr, o);
}

static esp_err_t h_status(httpd_req_t *req)
{
    ui_model_t m;
    app_snapshot(&m);
    cJSON *o = cJSON_CreateObject();
    char buf[64];
    cJSON_AddBoolToObject(o, "time_valid", m.now.valid);
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02ld:%02ld:%02ld", m.now.date.year, m.now.date.month, m.now.date.day,
             (long)(m.now.second / 3600), (long)(m.now.second / 60 % 60), (long)(m.now.second % 60));
    cJSON_AddStringToObject(o, "now", buf);
    cJSON_AddStringToObject(o, "zone", m.settings.zone);
    cJSON_AddStringToObject(o, "place", m.place);
    cJSON_AddStringToObject(o, "ip", m.ip);
    cJSON_AddStringToObject(o, "net", m.net == UI_NET_ONLINE ? "online" : m.net == UI_NET_SETUP ? "setup" : "offline");
    cJSON_AddStringToObject(o, "version", esp_app_get_description()->version);
    cJSON_AddBoolToObject(o, "stale", m.stale);
    cJSON_AddBoolToObject(o, "adhan_playing", m.adhan_playing);
    if (m.battery_present) {
        cJSON_AddNumberToObject(o, "battery", m.battery_pct);
    }
    if (m.have_today) {
        cJSON_AddStringToObject(o, "next", PRAYERS[m.sched.next].name);
        cJSON_AddNumberToObject(o, "seconds_to_next", m.sched.seconds_to_next);
        cJSON_AddStringToObject(o, "current", PRAYERS[m.sched.current].name);
    }
    return send_json(req, o);
}

typedef struct {
    date_t from;
    int days;
    cJSON *arr;
} times_ctx_t;

static void collect_times(const timetable_t *tt, void *c)
{
    times_ctx_t *ctx = c;
    for (int i = 0; i < ctx->days; i++) {
        day_times_t d;
        if (timetable_day(tt, date_add(ctx->from, i), &d)) {
            add_day(ctx->arr, &d);
        }
    }
}

static esp_err_t h_times(httpd_req_t *req)
{
    wallclock_t now = clock_now();
    if (!now.valid) {
        return send_error(req, "503 Service Unavailable", "Masa belum diselaraskan");
    }
    times_ctx_t ctx = {.from = now.date, .days = 7, .arr = cJSON_CreateArray()};
    tables_with(collect_times, &ctx);
    return send_json(req, ctx.arr);
}

static esp_err_t h_settings_get(httpd_req_t *req)
{
    settings_t s = app_settings();
    char *json = settings_to_json(&s);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, json);
    free(json);
    return err;
}

static esp_err_t apply_json(httpd_req_t *req, const char *json)
{
    settings_t s = app_settings();
    char err[64];
    if (!settings_apply_json(&s, json, strlen(json), err, sizeof(err))) {
        return send_error(req, "400 Bad Request", err);
    }
    app_apply_settings(&s, err, sizeof(err));
    return h_settings_get(req);
}

static esp_err_t h_settings_post(httpd_req_t *req)
{
    if (!authorised(req)) {
        return send_error(req, "401 Unauthorized", "PIN salah");
    }
    char *body = read_body(req);
    if (!body) {
        return send_error(req, "400 Bad Request", "Permintaan tidak sah");
    }
    esp_err_t err = apply_json(req, body);
    free(body);
    return err;
}

static esp_err_t h_zones(httpd_req_t *req)
{
    cJSON *arr = cJSON_CreateArray();
    for (size_t i = 0; i < ZONE_COUNT; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "code", ZONES[i].code);
        cJSON_AddStringToObject(o, "state", ZONES[i].state);
        cJSON_AddStringToObject(o, "name", ZONES[i].name);
        cJSON_AddItemToArray(arr, o);
    }
    return send_json(req, arr);
}

static esp_err_t h_scan(httpd_req_t *req)
{
    char *json = net_scan_json();
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, json);
    free(json);
    return err;
}

static void restart_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

static void restart_soon(void)
{
    xTaskCreate(restart_task, "restart", 2048, NULL, 5, NULL);
}

static esp_err_t h_wifi(httpd_req_t *req)
{
    if (!authorised(req)) {
        return send_error(req, "401 Unauthorized", "PIN salah");
    }
    char *body = read_body(req);
    cJSON *o = body ? cJSON_Parse(body) : NULL;
    free(body);
    const cJSON *ssid = cJSON_GetObjectItem(o, "ssid"), *pass = cJSON_GetObjectItem(o, "password");
    esp_err_t err = cJSON_IsString(ssid) ? net_set_wifi(ssid->valuestring, cJSON_IsString(pass) ? pass->valuestring : "")
                                         : ESP_ERR_INVALID_ARG;
    cJSON_Delete(o);
    if (err != ESP_OK) {
        return send_error(req, "400 Bad Request", "Nama atau kata laluan Wi-Fi tidak sah");
    }
    restart_soon();
    return send_ok(req);
}

static esp_err_t h_adhan_play(httpd_req_t *req)
{
    if (!authorised(req)) {
        return send_error(req, "401 Unauthorized", "PIN salah");
    }
    app_test_adhan();
    return send_ok(req);
}

static esp_err_t h_adhan_stop(httpd_req_t *req)
{
    app_stop_adhan();
    return send_ok(req);
}

static esp_err_t h_adhan_toggle(httpd_req_t *req)
{
    if (!authorised(req)) {
        return send_error(req, "401 Unauthorized", "PIN salah");
    }
    char *body = read_body(req);
    cJSON *o = body ? cJSON_Parse(body) : NULL;
    free(body);
    const cJSON *p = cJSON_GetObjectItem(o, "prayer"), *on = cJSON_GetObjectItem(o, "enabled");
    char json[96] = "";
    if (cJSON_IsString(p) && cJSON_IsBool(on)) {
        snprintf(json, sizeof(json), "{\"adhan\":{\"%.16s\":%s}}", p->valuestring, cJSON_IsTrue(on) ? "true" : "false");
    }
    cJSON_Delete(o);
    if (!json[0]) {
        return send_error(req, "400 Bad Request", "Permintaan tidak sah");
    }
    return apply_json(req, json);
}

static esp_err_t receive_to_file(httpd_req_t *req, const char *path, size_t limit)
{
    if (req->content_len == 0 || req->content_len > limit) {
        return ESP_ERR_INVALID_SIZE;
    }
    char tmp[48];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        return ESP_FAIL;
    }
    char *buf = malloc(4096);
    size_t got = 0;
    esp_err_t err = buf ? ESP_OK : ESP_ERR_NO_MEM;
    while (err == ESP_OK && got < req->content_len) {
        int r = httpd_req_recv(req, buf, 4096);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0 || fwrite(buf, 1, (size_t)r, f) != (size_t)r) {
            err = ESP_FAIL;
        }
        got += r > 0 ? (size_t)r : 0;
    }
    free(buf);
    fclose(f);
    if (err == ESP_OK) {
        unlink(path);
        err = rename(tmp, path) == 0 ? ESP_OK : ESP_FAIL;
    } else {
        unlink(tmp);
    }
    ESP_LOGI(TAG, "upload %s %u bytes: %s", path, (unsigned)got, esp_err_to_name(err));
    return err;
}

static esp_err_t h_upload(httpd_req_t *req)
{
    if (!authorised(req)) {
        return send_error(req, "401 Unauthorized", "PIN salah");
    }
    char q[64] = "", which[16] = "adhan";
    httpd_req_get_url_query_str(req, q, sizeof(q));
    httpd_query_key_value(q, "file", which, sizeof(which));
    const char *path = strcmp(which, "subuh") == 0 ? ADHAN_SUBUH_FILE : ADHAN_FILE;
    if (strcmp(which, "subuh-delete") == 0) {
        unlink(ADHAN_SUBUH_FILE);
        return send_ok(req);
    }
    app_stop_adhan();
    esp_err_t err = receive_to_file(req, path, MP3_LIMIT);
    if (err == ESP_ERR_INVALID_SIZE) {
        return send_error(req, "413 Payload Too Large", "Fail MP3 mesti 1 bait hingga 3 MB");
    }
    return err == ESP_OK ? send_ok(req) : send_error(req, "500 Internal Server Error", "Gagal menyimpan fail");
}

static esp_err_t h_ota(httpd_req_t *req)
{
    if (!authorised(req)) {
        return send_error(req, "401 Unauthorized", "PIN salah");
    }
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    esp_ota_handle_t ota;
    if (!part || esp_ota_begin(part, OTA_SIZE_UNKNOWN, &ota) != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "Tiada partition kemas kini");
    }
    char *buf = malloc(4096);
    size_t got = 0;
    esp_err_t err = buf ? ESP_OK : ESP_ERR_NO_MEM;
    while (err == ESP_OK && got < req->content_len) {
        int r = httpd_req_recv(req, buf, 4096);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        err = r > 0 ? esp_ota_write(ota, buf, (size_t)r) : ESP_FAIL;
        got += r > 0 ? (size_t)r : 0;
    }
    free(buf);
    if (err == ESP_OK) {
        err = esp_ota_end(ota);
    } else {
        esp_ota_abort(ota);
    }
    if (err == ESP_OK) {
        err = esp_ota_set_boot_partition(part);
    }
    ESP_LOGI(TAG, "OTA %u bytes to %s: %s", (unsigned)got, part->label, esp_err_to_name(err));
    if (err != ESP_OK) {
        return send_error(req, "400 Bad Request", "Fail perisian tidak sah");
    }
    restart_soon();
    return send_ok(req);
}

static esp_err_t h_reboot(httpd_req_t *req)
{
    if (!authorised(req)) {
        return send_error(req, "401 Unauthorized", "PIN salah");
    }
    restart_soon();
    return send_ok(req);
}

static esp_err_t h_rotate(httpd_req_t *req)
{
    if (!authorised(req)) {
        return send_error(req, "401 Unauthorized", "PIN salah");
    }
    settings_t s = app_settings();
    s.rotation = (uint8_t)((s.auto_rotate ? hw_orientation() : s.rotation) + 1) % 4;
    s.auto_rotate = false;
    app_apply_settings(&s, NULL, 0);
    return send_ok(req);
}

static esp_err_t h_mosque(httpd_req_t *req)
{
    return h_settings_post(req);
}

static esp_err_t h_mosque_search(httpd_req_t *req)
{
    settings_t s = app_settings();
    if (!s.mawaqit_token[0]) {
        return send_error(req, "400 Bad Request", "Masukkan token Mawaqit dahulu");
    }
    char q[96] = "", word[64] = "";
    httpd_req_get_url_query_str(req, q, sizeof(q));
    httpd_query_key_value(q, "q", word, sizeof(word));
    char url[192];
    snprintf(url, sizeof(url), "https://mawaqit.net/api/2.0/mosque/search?word=%s", word);
    esp_http_client_config_t cfg = {.url = url, .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 15000};
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_http_client_set_header(c, "Api-Access-Token", s.mawaqit_token);
    esp_http_client_set_header(c, "Accept", "application/json");
    esp_err_t err = esp_http_client_open(c, 0);
    int64_t len = err == ESP_OK ? esp_http_client_fetch_headers(c) : -1;
    int status = esp_http_client_get_status_code(c);
    if (err != ESP_OK || status != 200) {
        esp_http_client_cleanup(c);
        ESP_LOGW(TAG, "Mawaqit search -> %d", status);
        return send_error(req, "502 Bad Gateway", status == 401 ? "Token Mawaqit ditolak" : "Mawaqit tidak dapat dihubungi");
    }
    httpd_resp_set_type(req, "application/json");
    char *buf = malloc(2048);
    int r;
    while (buf && (r = esp_http_client_read(c, buf, 2048)) > 0) {
        httpd_resp_send_chunk(req, buf, r);
    }
    free(buf);
    esp_http_client_cleanup(c);
    (void)len;
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t h_not_found(httpd_req_t *req, httpd_err_code_t code)
{
    if (net_state() == UI_NET_SETUP) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
        return httpd_resp_send(req, NULL, 0);
    }
    return send_error(req, "404 Not Found", "Tiada");
}

void web_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 24;
    cfg.stack_size = 8192;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = 30;
    httpd_handle_t server;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server failed to start");
        return;
    }
    const httpd_uri_t routes[] = {
        {"/", HTTP_GET, h_index, NULL},
        {"/api/status", HTTP_GET, h_status, NULL},
        {"/api/times", HTTP_GET, h_times, NULL},
        {"/api/settings", HTTP_GET, h_settings_get, NULL},
        {"/api/settings", HTTP_POST, h_settings_post, NULL},
        {"/api/zones", HTTP_GET, h_zones, NULL},
        {"/api/scan", HTTP_GET, h_scan, NULL},
        {"/api/wifi", HTTP_POST, h_wifi, NULL},
        {"/api/adhan", HTTP_POST, h_adhan_toggle, NULL},
        {"/api/adhan/play", HTTP_POST, h_adhan_play, NULL},
        {"/api/adhan/stop", HTTP_POST, h_adhan_stop, NULL},
        {"/api/upload", HTTP_POST, h_upload, NULL},
        {"/api/ota", HTTP_POST, h_ota, NULL},
        {"/api/reboot", HTTP_POST, h_reboot, NULL},
        {"/api/rotate", HTTP_POST, h_rotate, NULL},
        {"/api/mosque", HTTP_POST, h_mosque, NULL},
        {"/api/mosque/search", HTTP_GET, h_mosque_search, NULL},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, h_not_found);
    ESP_LOGI(TAG, "HTTP server on port 80");
}
