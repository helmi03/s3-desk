#include "app.h"
#include "esp_log.h"
#include "esp_pm.h"

static const char *TAG = "power";

static void pm_configure(bool low)
{
    esp_pm_config_t cfg = {.max_freq_mhz = 240, .min_freq_mhz = low ? 80 : 240, .light_sleep_enable = low};
    esp_err_t err = esp_pm_configure(&cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_pm_configure: %s", esp_err_to_name(err));
    }
}

void power_init(void)
{
    pm_configure(false);
}

void power_set_low(bool low)
{
    if (low) {
        display_sleep(true);
        hw_set_low_power(true);
        net_set_low_power(true);
        pm_configure(true);
    } else {
        pm_configure(false);
        net_set_low_power(false);
        hw_set_low_power(false);
        display_sleep(false);
    }
    ESP_LOGI(TAG, "low power %s", low ? "on" : "off");
}
