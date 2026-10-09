#include <stdio.h>

#include "app.h"
#include "audio_player.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "audio";
static esp_codec_dev_handle_t speaker;
static int volume = 70;
static volatile int64_t requested_at;
static volatile bool requested;

static esp_err_t write_fn(void *buf, size_t len, size_t *written, uint32_t timeout_ms)
{
    int err = esp_codec_dev_write(speaker, buf, (int)len);
    *written = err == ESP_CODEC_DEV_OK ? len : 0;
    return err == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

static esp_err_t clk_set_fn(uint32_t rate, uint32_t bits, i2s_slot_mode_t ch)
{
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = rate,
        .channel = ch == I2S_SLOT_MODE_STEREO ? 2 : 1,
        .bits_per_sample = (uint8_t)bits,
    };
    esp_codec_dev_close(speaker);
    int err = esp_codec_dev_open(speaker, &fs);
    esp_codec_dev_set_out_vol(speaker, volume);
    ESP_LOGI(TAG, "codec open %lu Hz %lu bit %d ch: %d", (unsigned long)rate, (unsigned long)bits, fs.channel, err);
    return err == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

static esp_err_t mute_fn(AUDIO_PLAYER_MUTE_SETTING setting)
{
    esp_codec_dev_set_out_mute(speaker, setting == AUDIO_PLAYER_MUTE);
    return ESP_OK;
}

esp_err_t audio_init(void)
{
    speaker = bsp_audio_codec_speaker_init();
    if (!speaker) {
        ESP_LOGE(TAG, "speaker codec init failed");
        return ESP_FAIL;
    }
    audio_player_config_t cfg = {
        .mute_fn = mute_fn,
        .clk_set_fn = clk_set_fn,
        .write_fn = write_fn,
        .priority = 5,
        .coreID = 1,
    };
    esp_err_t err = audio_player_new(cfg);
    ESP_LOGI(TAG, "audio player: %s", esp_err_to_name(err));
    return err;
}

bool audio_play(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "missing %s", path);
        return false;
    }
    if (audio_player_play(f) != ESP_OK) {
        fclose(f);
        ESP_LOGE(TAG, "play %s failed", path);
        return false;
    }
    requested = true;
    requested_at = esp_timer_get_time();
    ESP_LOGI(TAG, "playing %s at volume %d", path, volume);
    return true;
}

void audio_stop(void)
{
    requested = false;
    audio_player_stop();
    ESP_LOGI(TAG, "stopped");
}

bool audio_is_playing(void)
{
    if (!requested) {
        return false;
    }
    if (audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING) {
        return true;
    }
    /* The player starts asynchronously; give it a moment before calling it finished. */
    if (esp_timer_get_time() - requested_at < 1500 * 1000) {
        return true;
    }
    requested = false;
    return false;
}

void audio_set_volume(int v)
{
    volume = v;
    if (speaker) {
        esp_codec_dev_set_out_vol(speaker, volume);
    }
}
