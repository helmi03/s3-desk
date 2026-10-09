#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "bsp/esp-bsp.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "mbedtls/base64.h"

static int cmd_time(int argc, char **argv)
{
    int y, mo, d, h, mi, s;
    if (argc != 2 || sscanf(argv[1], "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6) {
        printf("usage: time YYYY-MM-DDTHH:MM:SS\n");
        return 1;
    }
    clock_set_manual((date_t){(int16_t)y, (int8_t)mo, (int8_t)d}, h * 3600 + mi * 60 + s);
    return 0;
}

static int cmd_fake(int argc, char **argv)
{
    app_fake_prayer(argc > 1 ? atoi(argv[1]) : 1);
    return 0;
}

static int cmd_status(int argc, char **argv)
{
    ui_model_t m;
    app_snapshot(&m);
    printf("STATUS valid=%d date=%04d-%02d-%02d sec=%ld zone=%s have_today=%d origin=%d net=%d ip=%s playing=%d "
           "next=%s in=%ld mode=%d dimmed=%d times=",
           m.now.valid, m.now.date.year, m.now.date.month, m.now.date.day, (long)m.now.second, m.settings.zone,
           m.have_today, m.today.origin, m.net, m.ip, m.adhan_playing, PRAYERS[m.sched.next].name,
           (long)m.sched.seconds_to_next, ui_current_mode(), m.dimmed);
    for (int p = 0; p < P_COUNT; p++) {
        printf("%s%02u:%02u", p ? "," : "", m.today.minute[p] / 60, m.today.minute[p] % 60);
    }
    adhan_mark_t mark = storage_load_mark();
    printf(" mark=%04d-%02d-%02d/%s\n", mark.date.year, mark.date.month, mark.date.day,
           mark.date.year ? PRAYERS[mark.prayer].name : "-");
    return 0;
}

static int cmd_wifi(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: wifi SSID [PASSWORD]\n");
        return 1;
    }
    if (net_set_wifi(argv[1], argc > 2 ? argv[2] : "") != ESP_OK) {
        return 1;
    }
    printf("saved, restarting\n");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return 0;
}

static int cmd_tone(int argc, char **argv)
{
    return audio_play(TONE_FILE) ? 0 : 1;
}

static int cmd_adhan(int argc, char **argv)
{
    app_test_adhan();
    return 0;
}

static int cmd_stop(int argc, char **argv)
{
    app_stop_adhan();
    return 0;
}

static int cmd_key(int argc, char **argv)
{
    app_button_press();
    return 0;
}

static int cmd_hold(int argc, char **argv)
{
    app_button_hold();
    return 0;
}

static int cmd_set(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: set {\"volume\":50}\n");
        return 1;
    }
    settings_t s = app_settings();
    char err[64];
    if (!settings_apply_json(&s, argv[1], strlen(argv[1]), err, sizeof(err))) {
        printf("ERROR %s\n", err);
        return 1;
    }
    app_apply_settings(&s, err, sizeof(err));
    printf("OK\n");
    return 0;
}

static int cmd_view(int argc, char **argv)
{
    if (display_lock(500)) {
        if (argc > 1 && strcmp(argv[1], "settings") == 0) {
            ui_open_settings(true);
        } else {
            ui_open_settings(false);
            ui_show_view((ui_view_t)(argc > 1 ? atoi(argv[1]) % UI_VIEW_COUNT : 0));
        }
        display_unlock();
    }
    return 0;
}

static int cmd_snap(int argc, char **argv)
{
    display_dump();
    return 0;
}

static int cmd_tasks(int argc, char **argv)
{
    static char buf[2048];
    vTaskList(buf);
    printf("name state prio stack num core\n%s", buf);
    vTaskGetRunTimeStats(buf);
    printf("name abs_time pct\n%s", buf);
    printf("MEM internal free %u largest %u, dma free %u largest %u, psram free %u\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return 0;
}

static int cmd_reboot(int argc, char **argv)
{
    esp_restart();
    return 0;
}

void display_dump(void)
{
    if (!display_lock(2000)) {
        printf("SNAP ERROR lock\n");
        return;
    }
    lv_draw_buf_t *snap = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    display_unlock();
    if (!snap) {
        printf("SNAP ERROR memory\n");
        return;
    }
    uint32_t w = snap->header.w, h = snap->header.h, stride = snap->header.stride;
    printf("SNAP %lu %lu %lu\n", (unsigned long)w, (unsigned long)h, (unsigned long)stride);
    const size_t chunk = 3 * 256;
    unsigned char out[4 * 256 + 4];
    size_t total = (size_t)stride * h;
    for (size_t off = 0; off < total; off += chunk) {
        size_t n = total - off < chunk ? total - off : chunk;
        size_t olen = 0;
        mbedtls_base64_encode(out, sizeof(out), &olen, snap->data + off, n);
        out[olen] = '\0';
        printf("%s\n", out);
    }
    printf("SNAP END\n");
    lv_draw_buf_destroy(snap);
}

void console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    cfg.prompt = "waktu>";
    cfg.max_cmdline_length = 256;
    cfg.task_stack_size = 8192;
    esp_console_dev_usb_serial_jtag_config_t dev = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&dev, &cfg, &repl));
    const esp_console_cmd_t cmds[] = {
        {.command = "time", .help = "set local time: time 2026-10-09T13:03:50", .func = cmd_time},
        {.command = "fake", .help = "move the next Adhan to N minutes from now (today only)", .func = cmd_fake},
        {.command = "status", .help = "print one STATUS line", .func = cmd_status},
        {.command = "wifi", .help = "save Wi-Fi: wifi SSID PASSWORD", .func = cmd_wifi},
        {.command = "tone", .help = "play the test tone", .func = cmd_tone},
        {.command = "adhan", .help = "play the Adhan now (test)", .func = cmd_adhan},
        {.command = "stop", .help = "stop audio", .func = cmd_stop},
        {.command = "key", .help = "simulate a GPIO18 key press", .func = cmd_key},
        {.command = "hold", .help = "simulate holding the GPIO18 key (toggles Tetapan)", .func = cmd_hold},
        {.command = "set", .help = "apply settings JSON", .func = cmd_set},
        {.command = "view", .help = "view 0|1|2|settings", .func = cmd_view},
        {.command = "snap", .help = "dump the screen as base64 RGB565", .func = cmd_snap},
        {.command = "tasks", .help = "list tasks with state and CPU time", .func = cmd_tasks},
        {.command = "reboot", .help = "restart", .func = cmd_reboot},
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
