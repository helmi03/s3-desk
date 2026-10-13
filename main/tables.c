#include <assert.h>
#include <string.h>

#include "app.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "tables";

/* Up to three consecutive years: last year (for estimates), this year, next year. */
#define SLOTS 3

static SemaphoreHandle_t lock;
static year_table_t *slots[SLOTS];

void tables_init(void)
{
    lock = xSemaphoreCreateMutex();
    for (int i = 0; i < SLOTS; i++) {
        slots[i] = heap_caps_calloc(1, sizeof(year_table_t), MALLOC_CAP_SPIRAM);
        assert(slots[i]);
    }
}

void tables_reload(const char *zone)
{
    wallclock_t now = clock_now();
    int16_t year = now.valid ? now.date.year : 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    for (int i = 0; i < SLOTS; i++) {
        slots[i]->year = 0;
    }
    int n = 0;
    if (year) {
        for (int16_t y = year - 1; y <= year + 1; y++) {
            if (storage_load_year(zone, y, slots[n])) {
                ESP_LOGI(TAG, "loaded %s %d (%d days)", zone, y, year_table_day_count(slots[n]));
                n++;
            }
        }
    }
    xSemaphoreGive(lock);
}

void tables_put(const year_table_t *t)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    year_table_t *dst = NULL;
    for (int i = 0; i < SLOTS && !dst; i++) {
        if (slots[i]->year == t->year) {
            dst = slots[i];
        }
    }
    for (int i = 0; i < SLOTS && !dst; i++) {
        if (slots[i]->year == 0) {
            dst = slots[i];
        }
    }
    if (!dst) {
        dst = slots[0];
        for (int i = 1; i < SLOTS; i++) {
            if (slots[i]->year < dst->year) {
                dst = slots[i];
            }
        }
    }
    *dst = *t;
    xSemaphoreGive(lock);
}

bool tables_has(int16_t year)
{
    bool found = false;
    xSemaphoreTake(lock, portMAX_DELAY);
    for (int i = 0; i < SLOTS; i++) {
        found |= slots[i]->year == year && year_table_day_count(slots[i]) > 0;
    }
    xSemaphoreGive(lock);
    return found;
}

void tables_with(void (*fn)(const timetable_t *tt, void *ctx), void *ctx)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    timetable_t tt = {0};
    for (int i = 0; i < SLOTS; i++) {
        if (slots[i]->year) {
            tt.tables[tt.count++] = slots[i];
        }
    }
    fn(&tt, ctx);
    xSemaphoreGive(lock);
}
