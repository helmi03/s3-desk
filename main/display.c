#include "app.h"
#include "bsp/esp-bsp.h"
#include "bsp/touch.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"

static const char *TAG = "display";

/* 20 lines x 480 px x 2 bytes = 19.2 KB per buffer, in internal DMA RAM.
 * The BSP default puts 48 KB buffers in PSRAM, and the SPI driver then needs
 * a 48 KB internal bounce buffer per flush, which fails once Wi-Fi is up. */
#define BUFFER_LINES 20

bool display_lock(int timeout_ms)
{
    return esp_lv_adapter_lock(timeout_ms) == ESP_OK;
}

void display_unlock(void)
{
    esp_lv_adapter_unlock();
}

/* The CO5300 needs flush areas that start on even and end on odd coordinates. */
static void round_area(lv_event_t *e)
{
    lv_area_t *a = lv_event_get_param(e);
    a->x1 &= ~1;
    a->y1 &= ~1;
    a->x2 |= 1;
    a->y2 |= 1;
}

void display_start(bool flipped)
{
    esp_lv_adapter_config_t adapter = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(esp_lv_adapter_init(&adapter));

    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_io_handle_t io;
    const bsp_display_config_t panel_cfg = {.max_transfer_sz = BSP_LCD_H_RES * BUFFER_LINES * 2};
    ESP_ERROR_CHECK(bsp_display_new(&panel_cfg, &panel, &io));

    esp_lv_adapter_display_config_t disp_cfg = {
        .panel = panel,
        .panel_io = io,
        .profile = {
            .interface = ESP_LV_ADAPTER_PANEL_IF_OTHER,
            .rotation = ESP_LV_ADAPTER_ROTATE_0,
            .hor_res = BSP_LCD_H_RES,
            .ver_res = BSP_LCD_V_RES,
            .buffer_height = BUFFER_LINES,
            .use_psram = false,
            .require_double_buffer = true,
        },
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
    };
    lv_display_t *disp = esp_lv_adapter_register_display(&disp_cfg);
    lv_display_add_event_cb(disp, round_area, LV_EVENT_INVALIDATE_AREA, NULL);

    /* Upright the touch panel needs swap_xy + mirror_y (the BSP default);
     * turned 180 degrees that becomes swap_xy + mirror_x. */
    bsp_display_cfg_t touch_cfg = {.touch_flags = {.swap_xy = 1, .mirror_x = flipped, .mirror_y = !flipped}};
    esp_lcd_touch_handle_t tp;
    ESP_ERROR_CHECK(bsp_touch_new(&touch_cfg, &tp));
    esp_lv_adapter_touch_config_t tcfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, tp);
    esp_lv_adapter_register_touch(&tcfg);

    ESP_ERROR_CHECK(bsp_display_brightness_init());
    if (flipped) {
        bsp_display_rotation_set(BSP_DISPLAY_ROTATE_180);
    }
    ESP_ERROR_CHECK(esp_lv_adapter_start());
    ESP_LOGI(TAG, "display started, %s", flipped ? "flipped 180" : "upright");
}
