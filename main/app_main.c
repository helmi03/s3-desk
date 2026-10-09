#include "app.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"

static const char *TAG = "main";

void app_main(void)
{
    storage_init();
    clock_init();
    bsp_i2c_init();
    hw_init_sensors();
    settings_t s = storage_load_settings();
    bool flipped = s.rotation == 2 || hw_upside_down();
    ESP_LOGI(TAG, "display %s", flipped ? "flipped 180" : "upright");
    display_start(flipped);
    tables_init();
    audio_init();
    app_start();
    net_start();
    web_start();
    fetch_start();
    hw_start();
    console_start();
}
