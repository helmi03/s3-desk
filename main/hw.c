#include <stdlib.h>

#include "app.h"
#include "bsp/esp-bsp.h"
#include "button_gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "iot_button.h"

static const char *TAG = "hw";

#define USER_KEY_GPIO 18
#define AXP2101_ADDR 0x34
#define AXP_STATUS1 0x00
#define AXP_STATUS2 0x01
#define AXP_IC_TYPE 0x03
#define AXP_INTEN2 0x41
#define AXP_INTSTS2 0x49
#define AXP_PKEY_SHORT (1u << 3)
#define AXP_BAT_DET_CTRL 0x68
#define AXP_FUEL_GAUGE_CTRL 0xA2
#define AXP_BAT_PERCENT 0xA4
#define QMI8658_WHO_AM_I 0x00
#define QMI8658_CTRL1 0x02
#define QMI8658_CTRL2 0x03
#define QMI8658_CTRL3 0x04
#define QMI8658_CTRL7 0x08
#define QMI8658_AX_L 0x35

static i2c_master_dev_handle_t pmu, imu;
static volatile bool battery_present, battery_charging;
static volatile uint8_t battery_pct;

static esp_err_t reg_read(i2c_master_dev_handle_t d, uint8_t reg, uint8_t *out, size_t n)
{
    return i2c_master_transmit_receive(d, &reg, 1, out, n, 100);
}

static esp_err_t reg_write(i2c_master_dev_handle_t d, uint8_t reg, uint8_t v)
{
    uint8_t buf[2] = {reg, v};
    return i2c_master_transmit(d, buf, 2, 100);
}

static i2c_master_dev_handle_t add_device(uint8_t addr)
{
    i2c_device_config_t cfg = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr, .scl_speed_hz = 400000};
    i2c_master_dev_handle_t d = NULL;
    if (i2c_master_probe(bsp_i2c_get_handle(), addr, 50) != ESP_OK ||
        i2c_master_bus_add_device(bsp_i2c_get_handle(), &cfg, &d) != ESP_OK) {
        return NULL;
    }
    return d;
}

static void pmu_init(void)
{
    pmu = add_device(AXP2101_ADDR);
    uint8_t v;
    if (!pmu || reg_read(pmu, AXP_IC_TYPE, &v, 1) != ESP_OK) {
        ESP_LOGW(TAG, "AXP2101 not found");
        pmu = NULL;
        return;
    }
    reg_read(pmu, AXP_BAT_DET_CTRL, &v, 1);
    reg_write(pmu, AXP_BAT_DET_CTRL, v | 0x01);
    reg_read(pmu, AXP_FUEL_GAUGE_CTRL, &v, 1);
    reg_write(pmu, AXP_FUEL_GAUGE_CTRL, v | 0x01);
    reg_read(pmu, AXP_INTEN2, &v, 1);
    reg_write(pmu, AXP_INTEN2, v | AXP_PKEY_SHORT);
    reg_write(pmu, AXP_INTSTS2, AXP_PKEY_SHORT);
    ESP_LOGI(TAG, "AXP2101 ready");
}

static void pmu_poll(void)
{
    uint8_t s1, s2, pct, irq;
    if (!pmu) {
        return;
    }
    if (reg_read(pmu, AXP_INTSTS2, &irq, 1) == ESP_OK && (irq & AXP_PKEY_SHORT)) {
        reg_write(pmu, AXP_INTSTS2, AXP_PKEY_SHORT);
        ESP_LOGI(TAG, "PWR short press");
        app_power_short_press();
    }
    if (reg_read(pmu, AXP_STATUS1, &s1, 1) == ESP_OK && reg_read(pmu, AXP_STATUS2, &s2, 1) == ESP_OK &&
        reg_read(pmu, AXP_BAT_PERCENT, &pct, 1) == ESP_OK) {
        battery_present = (s1 >> 3) & 1u;
        battery_charging = (s2 >> 5) == 0x01;
        battery_pct = pct > 100 ? 100 : pct;
    }
}

bool hw_battery(uint8_t *pct, bool *charging)
{
    *pct = battery_pct;
    *charging = battery_charging;
    return battery_present;
}

static void imu_init(void)
{
    static const uint8_t ADDRS[] = {0x6B, 0x6A};
    uint8_t who = 0;
    for (size_t i = 0; i < sizeof(ADDRS) && !imu; i++) {
        imu = add_device(ADDRS[i]);
        if (imu && (reg_read(imu, QMI8658_WHO_AM_I, &who, 1) != ESP_OK || who != 0x05)) {
            i2c_master_bus_rm_device(imu);
            imu = NULL;
        }
    }
    if (!imu) {
        ESP_LOGW(TAG, "QMI8658 not found");
        return;
    }
    reg_write(imu, QMI8658_CTRL1, 0x40); /* register address auto-increment */
    reg_write(imu, QMI8658_CTRL2, 0x15); /* +-4 g, 117 Hz */
    reg_write(imu, QMI8658_CTRL3, 0x55); /* gyro +-1024 dps */
    reg_write(imu, QMI8658_CTRL7, 0x03); /* accelerometer and gyro on */
    ESP_LOGI(TAG, "QMI8658 ready");
}

bool hw_upside_down(void)
{
    if (!imu) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    uint8_t d[6];
    if (reg_read(imu, QMI8658_AX_L, d, sizeof(d)) != ESP_OK) {
        return false;
    }
    int16_t ax = (int16_t)(d[0] | (d[1] << 8));
    ESP_LOGI(TAG, "accel x %d", ax);
    /* Measured on the board: standing upright x reads -0.95 g (y, z ~0); turned 180 degrees it reads +1 g. */
    return ax > 4096;
}

bool hw_imu_read(int16_t acc[3], int16_t gyr[3])
{
    uint8_t d[12];
    if (!imu || reg_read(imu, QMI8658_AX_L, d, sizeof(d)) != ESP_OK) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        acc[i] = (int16_t)(d[2 * i] | (d[2 * i + 1] << 8));
        gyr[i] = (int16_t)(d[6 + 2 * i] | (d[6 + 2 * i + 1] << 8));
    }
    return true;
}

void hw_set_brightness(int percent)
{
    bsp_display_brightness_set(percent);
}

static void on_key(void *btn, void *ctx)
{
    ESP_LOGI(TAG, "key GPIO18 press");
    app_button_press();
}

static void on_key_hold(void *btn, void *ctx)
{
    ESP_LOGI(TAG, "key GPIO18 hold");
    app_button_hold();
}

static void poll_task(void *arg)
{
    for (;;) {
        pmu_poll();
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

void hw_init_sensors(void)
{
    pmu_init();
    imu_init();
}

void hw_start(void)
{
    button_config_t cfg = {.long_press_time = 1000};
    button_gpio_config_t gpio = {.gpio_num = USER_KEY_GPIO, .active_level = 0};
    button_handle_t btn = NULL;
    if (iot_button_new_gpio_device(&cfg, &gpio, &btn) == ESP_OK) {
        iot_button_register_cb(btn, BUTTON_SINGLE_CLICK, NULL, on_key, NULL);
        iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, NULL, on_key_hold, NULL);
    } else {
        ESP_LOGW(TAG, "GPIO18 key init failed");
    }
    xTaskCreatePinnedToCore(poll_task, "hw", 3072, NULL, 3, NULL, 0);
}
