#include <stdio.h>
#include <string.h>

#include "app.h"
#include "cJSON.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "lwip/sockets.h"
#include "mdns.h"

static const char *TAG = "net";

static volatile ui_net_t state = UI_NET_OFF;
static char ip[16];
static char ap_ssid[24];
static char ap_pass[16];
static TimerHandle_t reconnect_timer;

ui_net_t net_state(void)
{
    return state;
}

void net_ip(char *out, size_t cap)
{
    snprintf(out, cap, "%s", state == UI_NET_SETUP ? "192.168.4.1" : ip);
}

void net_ap_credentials(char *ssid, size_t ssid_cap, char *pass, size_t pass_cap)
{
    snprintf(ssid, ssid_cap, "%s", ap_ssid);
    snprintf(pass, pass_cap, "%s", ap_pass);
}

static void on_sntp(struct timeval *tv)
{
    ESP_LOGI(TAG, "SNTP synced");
    clock_mark_synced();
}

static void reconnect(TimerHandle_t t)
{
    esp_wifi_connect();
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (state != UI_NET_SETUP) {
            state = UI_NET_CONNECTING;
        }
        ip[0] = '\0';
        xTimerStart(reconnect_timer, 0);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&e->ip_info.ip));
        state = UI_NET_ONLINE;
        ESP_LOGI(TAG, "online, IP %s", ip);
        fetch_kick();
    }
}

/* Answers every DNS query with 192.168.4.1 so phones open the setup page. */
static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "DNS bind failed");
        vTaskDelete(NULL);
    }
    uint8_t buf[512];
    for (;;) {
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        int n = recvfrom(sock, buf, sizeof(buf) - 16, 0, (struct sockaddr *)&from, &fl);
        if (n < 12) {
            continue;
        }
        buf[2] = 0x84; /* response, authoritative */
        buf[3] = 0x00;
        buf[6] = 0x00;
        buf[7] = 0x01; /* one answer */
        buf[8] = buf[9] = buf[10] = buf[11] = 0;
        static const uint8_t answer[] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 192, 168, 4, 1};
        memcpy(buf + n, answer, sizeof(answer));
        sendto(sock, buf, n + (int)sizeof(answer), 0, (struct sockaddr *)&from, fl);
    }
}

static void start_ap(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(ap_ssid, sizeof(ap_ssid), "WaktuSolat-%02X%02X", mac[4], mac[5]);
    storage_get_ap_pass(ap_pass, sizeof(ap_pass));
    wifi_config_t ap = {
        .ap = {.max_connection = 2, .authmode = WIFI_AUTH_WPA2_PSK, .channel = 6},
    };
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "%s", ap_ssid);
    ap.ap.ssid_len = (uint8_t)strlen(ap_ssid);
    snprintf((char *)ap.ap.password, sizeof(ap.ap.password), "%s", ap_pass);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    state = UI_NET_SETUP;
    xTaskCreate(dns_task, "dns", 3072, NULL, 3, NULL);
    ESP_LOGI(TAG, "setup access point %s up", ap_ssid);
}

void net_start(void)
{
    reconnect_timer = xTimerCreate("reconnect", pdMS_TO_TICKS(5000), pdFALSE, NULL, reconnect);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));

    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2, ESP_SNTP_SERVER_LIST("my.pool.ntp.org", "pool.ntp.org"));
    sntp.sync_cb = on_sntp;
    sntp.start = true;
    esp_netif_sntp_init(&sntp);

    if (mdns_init() == ESP_OK) {
        mdns_hostname_set("waktusolat");
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    }

    wifi_config_t sta;
    if (esp_wifi_get_config(WIFI_IF_STA, &sta) == ESP_OK && sta.sta.ssid[0]) {
        ESP_LOGI(TAG, "connecting to %s", (char *)sta.sta.ssid);
        state = UI_NET_CONNECTING;
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
    } else {
        start_ap();
    }
}

esp_err_t net_set_wifi(const char *ssid, const char *pass)
{
    if (!ssid[0] || strlen(ssid) > 32 || strlen(pass) > 63) {
        return ESP_ERR_INVALID_ARG;
    }
    wifi_config_t sta = {0};
    snprintf((char *)sta.sta.ssid, sizeof(sta.sta.ssid), "%s", ssid);
    snprintf((char *)sta.sta.password, sizeof(sta.sta.password), "%s", pass);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta);
    ESP_LOGI(TAG, "saved Wi-Fi %s: %s", ssid, esp_err_to_name(err));
    return err;
}

void net_reset_wifi(void)
{
    ESP_LOGI(TAG, "forgetting Wi-Fi and restarting into setup");
    esp_wifi_restore();
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

char *net_scan_json(void)
{
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_AP) {
        esp_wifi_set_mode(WIFI_MODE_APSTA);
    }
    wifi_scan_config_t cfg = {.show_hidden = false};
    cJSON *arr = cJSON_CreateArray();
    if (esp_wifi_scan_start(&cfg, true) == ESP_OK) {
        uint16_t n = 20;
        wifi_ap_record_t recs[20];
        esp_wifi_scan_get_ap_records(&n, recs);
        for (int i = 0; i < n; i++) {
            if (!recs[i].ssid[0]) {
                continue;
            }
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "ssid", (const char *)recs[i].ssid);
            cJSON_AddNumberToObject(o, "rssi", recs[i].rssi);
            cJSON_AddBoolToObject(o, "open", recs[i].authmode == WIFI_AUTH_OPEN);
            cJSON_AddItemToArray(arr, o);
        }
    }
    if (mode == WIFI_MODE_AP) {
        esp_wifi_set_mode(WIFI_MODE_AP);
    }
    char *out = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return out;
}
