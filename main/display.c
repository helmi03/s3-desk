#include "app.h"
#include "bsp/esp-bsp.h"
#include "bsp/touch.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "orientation.h"

static const char *TAG = "display";

/* 20 lines x 480 px x 2 bytes = 19.2 KB per buffer, in internal DMA RAM.
 * The BSP default puts 48 KB buffers in PSRAM, and the SPI driver then needs
 * a 48 KB internal bounce buffer per flush, which fails once Wi-Fi is up. */
#define BUFFER_LINES 20

static esp_lcd_panel_io_handle_t panel_io;
static volatile quarter_t quarter;

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

/* Touch flags stay off; this hook gets raw CST9217 points and produces LVGL
 * coordinates for the current rotation. Upright, the panel needs the
 * driver's swap_xy + mirror_y: (x, y) -> (480 - y, x). */
static void map_touch(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *n, uint8_t max)
{
    for (int i = 0; i < *n; i++) {
        int ux = BSP_LCD_V_RES - y[i], uy = x[i], lx, ly;
        ux = ux < 0 ? 0 : ux >= BSP_LCD_H_RES ? BSP_LCD_H_RES - 1 : ux;
        uy = uy < 0 ? 0 : uy >= BSP_LCD_V_RES ? BSP_LCD_V_RES - 1 : uy;
        orientation_map_touch(quarter, BSP_LCD_H_RES, ux, uy, &lx, &ly);
        x[i] = (uint16_t)lx;
        y[i] = (uint16_t)ly;
    }
}

static void write_madctl(quarter_t q)
{
    uint8_t madctl = orientation_madctl(q);
    esp_lcd_panel_io_tx_param(panel_io, (0x02 << 24) | (0x36 << 8), &madctl, 1);
}

void display_set_rotation(quarter_t q)
{
    if (q == quarter || !display_lock(1000)) {
        return;
    }
    write_madctl(q);
    quarter = q;
    lv_obj_invalidate(lv_screen_active());
    display_unlock();
    ESP_LOGI(TAG, "rotated to %d degrees", q * 90);
}

void display_start(quarter_t initial)
{
    esp_lv_adapter_config_t adapter = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(esp_lv_adapter_init(&adapter));

    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_io_handle_t io;
    const bsp_display_config_t panel_cfg = {.max_transfer_sz = BSP_LCD_H_RES * BUFFER_LINES * 2};
    ESP_ERROR_CHECK(bsp_display_new(&panel_cfg, &panel, &io));
    panel_io = io;

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

    bsp_display_cfg_t touch_cfg = {0};
    esp_lcd_touch_handle_t tp;
    ESP_ERROR_CHECK(bsp_touch_new(&touch_cfg, &tp));
    tp->config.process_coordinates = map_touch;
    esp_lv_adapter_touch_config_t tcfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, tp);
    esp_lv_adapter_register_touch(&tcfg);

    ESP_ERROR_CHECK(bsp_display_brightness_init());
    quarter = initial & 3;
    write_madctl(quarter);
    ESP_ERROR_CHECK(esp_lv_adapter_start());
    ESP_LOGI(TAG, "display started at %d degrees", quarter * 90);
}
