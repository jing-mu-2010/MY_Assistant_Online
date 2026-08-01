/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_spiffs.h"
#include "esp_vfs.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "app_sr.h"
#include "app_audio.h"
#include "bsp/esp-bsp.h"
#include "audio_player.h"
#include "file_iterator.h"
#include "app_ui_ctrl.h"
#include "tts_api.h"
#include "app_wifi.h"
#include "xl9555.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"

static const char *TAG = "app_audio";

/* ============================================================
 * BOX1 Audio Codec Configuration
 * VERIFY all GPIO pins against BOX1 schematic before use!
 * ============================================================ */
#define BOX1_I2S_MCLK   GPIO_NUM_NC
#define BOX1_I2S_SCLK   GPIO_NUM_21
#define BOX1_I2S_LCLK   GPIO_NUM_13
#define BOX1_I2S_DOUT   GPIO_NUM_14
#define BOX1_I2S_DSIN   GPIO_NUM_47

static i2s_chan_handle_t i2s_tx_handle = NULL;
static i2s_chan_handle_t i2s_rx_handle = NULL;
static esp_codec_dev_handle_t box1_codec_dev = NULL;
static volatile bool g_audio_playing = false;

/* I2S read wrapper — mic recording via ES7210 */
static esp_err_t box1_i2s_read(void *audio_buffer, size_t len, size_t *bytes_read, uint32_t timeout_ms)
{
    return i2s_channel_read(i2s_rx_handle, audio_buffer, len, bytes_read, timeout_ms);
}

/* I2S write wrapper — speaker playback via ES8311 */
static esp_err_t box1_i2s_write(void *audio_buffer, size_t len, size_t *bytes_written, uint32_t timeout_ms)
{
    esp_err_t ret = i2s_channel_write(i2s_tx_handle, audio_buffer, len, bytes_written, timeout_ms);
    if (ret != ESP_OK || (bytes_written && *bytes_written == 0)) {
        ESP_LOGE(TAG, "i2s write ret=%s len=%u written=%u", esp_err_to_name(ret),
                 (unsigned)len, (unsigned)(bytes_written ? *bytes_written : 0));
    }
    return ret;
}

/* Mute control — amp enable pin */
static esp_err_t box1_mute_set(bool mute)
{
    if (box1_codec_dev) {
        esp_codec_dev_set_out_mute(box1_codec_dev, mute);
    }
    xl9555_pin_write(SPK_CTRL_IO, mute ? 0 : 1);
    return ESP_OK;
}

/* ES8311 DAC volume / soft-mute over the BOX1 I2C bus (I2C_NUM_0) */
#define BOX1_ES8311_ADDR            0x18
#define BOX1_ES8311_REG_DAC_MUTE    0x31
#define BOX1_ES8311_REG_DAC_VOLUME  0x32

static esp_err_t box1_es8311_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_write_to_device(I2C_NUM_0, BOX1_ES8311_ADDR, buf, sizeof(buf), pdMS_TO_TICKS(100));
}

/* Volume control: 0-100 maps linearly to the ES8311 DAC volume register */
static esp_err_t box1_volume_set(int volume, void *aux)
{
    if (volume < 0) {
        volume = 0;
    } else if (volume > 100) {
        volume = 100;
    }
    if (box1_codec_dev) {
        return esp_codec_dev_set_out_vol(box1_codec_dev, volume);
    }
    return box1_es8311_write_reg(BOX1_ES8311_REG_DAC_VOLUME, (uint8_t)(volume * 255 / 100));
}

/* DAC soft-mute: REG31 bit5/bit6 (same as the esp_codec_dev es8311 driver) */
static esp_err_t box1_dac_mute_set(bool mute)
{
    if (box1_codec_dev) {
        return esp_codec_dev_set_out_mute(box1_codec_dev, mute);
    }
    uint8_t reg_addr = BOX1_ES8311_REG_DAC_MUTE;
    uint8_t reg_val  = 0;
    esp_err_t ret = i2c_master_write_read_device(I2C_NUM_0, BOX1_ES8311_ADDR,
                                                 &reg_addr, 1, &reg_val, 1, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        return ret;
    }
    reg_val &= 0x9F;
    if (mute) {
        reg_val |= 0x60;
    }
    return box1_es8311_write_reg(BOX1_ES8311_REG_DAC_MUTE, reg_val);
}

/* Public button-facing volume / mute API */
#define BOX1_VOLUME_STEP    (10)

static int  g_volume_level = CONFIG_VOLUME_LEVEL;
static bool g_dac_muted    = false;

int app_audio_volume_get(void)
{
    return g_volume_level;
}

void app_audio_volume_set(int volume)
{
    if (volume < 0) {
        volume = 0;
    } else if (volume > 100) {
        volume = 100;
    }
    g_volume_level = volume;
    /* adjusting the volume implicitly clears DAC soft-mute */
    if (g_dac_muted) {
        g_dac_muted = false;
        box1_dac_mute_set(false);
    }
    xl9555_pin_write(SPK_CTRL_IO, 1);
    box1_volume_set(g_volume_level, NULL);
}

void app_audio_volume_up(void)
{
    app_audio_volume_set(g_volume_level + BOX1_VOLUME_STEP);
}

void app_audio_volume_down(void)
{
    app_audio_volume_set(g_volume_level - BOX1_VOLUME_STEP);
}

bool app_audio_mute_get(void)
{
    return g_dac_muted;
}

void app_audio_playback_begin(void)
{
    g_audio_playing = true;
}

void app_audio_playback_end(void)
{
    g_audio_playing = false;
}

bool app_audio_is_playing(void)
{
    return g_audio_playing;
}

void app_audio_mute_toggle(void)
{
    g_dac_muted = !g_dac_muted;
    box1_dac_mute_set(g_dac_muted);
    if (!g_dac_muted) {
        xl9555_pin_write(SPK_CTRL_IO, 1);
        box1_volume_set(g_volume_level, NULL);
    }
}

/* I2S clock reconfig — called when sample rate changes (WAV vs MP3 playback) */
static esp_err_t box1_codec_reconfig_clk(uint32_t sample_rate, uint32_t bits_per_sample, i2s_slot_mode_t slot_mode)
{
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    clk_cfg.sample_rate_hz = sample_rate;

    esp_err_t ret = i2s_channel_disable(i2s_tx_handle);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "disable TX before clock reconfig failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2s_channel_reconfig_std_clock(i2s_tx_handle, &clk_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "reconfig TX clock failed: %s", esp_err_to_name(ret));
    }

    esp_err_t enable_ret = i2s_channel_enable(i2s_tx_handle);
    if (enable_ret != ESP_OK && enable_ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "enable TX after clock reconfig failed: %s", esp_err_to_name(enable_ret));
        return enable_ret;
    }

    return ret;
}

/* Full codec reconfig — placeholder */
static esp_err_t box1_codec_reconfig(void)
{
    return ESP_OK;
}

esp_err_t box1_codec_init(void)
{
    ESP_LOGI(TAG, "Initializing BOX1 audio codec (ES8311+ES7210)");

    xl9555_pin_write(SPK_CTRL_IO, 0); // Start muted

    /* I2S full-duplex: TX (playback) + RX (recording) on I2S_NUM_0 */
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 6,
        .dma_frame_num = 240,
        .auto_clear = true,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &i2s_tx_handle, &i2s_rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BOX1_I2S_MCLK,
            .bclk = BOX1_I2S_SCLK,
            .ws   = BOX1_I2S_LCLK,
            .dout = BOX1_I2S_DOUT,
            .din  = BOX1_I2S_DSIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    std_cfg.slot_cfg.left_align = true;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_tx_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_rx_handle));

    audio_codec_i2s_cfg_t i2s_cfg = {
        .rx_handle = i2s_rx_handle,
        .tx_handle = i2s_tx_handle,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    ESP_RETURN_ON_FALSE(data_if, ESP_FAIL, TAG, "audio codec data interface init failed");

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = I2C_NUM_0,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    ESP_RETURN_ON_FALSE(ctrl_if, ESP_FAIL, TAG, "ES8311 I2C control interface init failed");

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(gpio_if, ESP_FAIL, TAG, "audio codec GPIO interface init failed");

    esp_codec_dev_hw_gain_t gain = {
        .pa_voltage = 5.0,
        .codec_dac_voltage = 3.3,
    };
    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin = -1,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = false,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = gain,
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es8311_cfg);
    ESP_RETURN_ON_FALSE(codec_if, ESP_FAIL, TAG, "ES8311 codec interface init failed");

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    box1_codec_dev = esp_codec_dev_new(&dev_cfg);
    ESP_RETURN_ON_FALSE(box1_codec_dev, ESP_FAIL, TAG, "ES8311 codec device init failed");

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
    };
    ESP_ERROR_CHECK(esp_codec_dev_open(box1_codec_dev, &fs));
    ESP_ERROR_CHECK(esp_codec_dev_set_in_gain(box1_codec_dev, 42.0));
    ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(box1_codec_dev, CONFIG_VOLUME_LEVEL));
    ESP_ERROR_CHECK(esp_codec_dev_set_out_mute(box1_codec_dev, false));
    g_volume_level = CONFIG_VOLUME_LEVEL;
    g_dac_muted = false;
    xl9555_pin_write(SPK_CTRL_IO, 1);

    ESP_LOGI(TAG, "BOX1 audio codec initialized successfully");
    return ESP_OK;
}

bsp_codec_config_t *box1_codec_handle(void)
{
    static bsp_codec_config_t cfg = {
        .i2s_write_fn        = box1_i2s_write,
        .i2s_read_fn         = box1_i2s_read,
        .mute_set_fn         = box1_mute_set,
        .volume_set_fn       = box1_volume_set,
        .i2s_reconfig_clk_fn = box1_codec_reconfig_clk,
        .codec_reconfig_fn   = box1_codec_reconfig,
    };
    return &cfg;
}

/* ============================================================
 * End of BOX1 codec
 * ============================================================ */


#if CONFIG_BSP_BOARD_ESP32_S3_BOX
static bool mute_flag = true;
#endif
bool record_flag = false;
uint32_t record_total_len = 0;
uint32_t file_total_len = 0;
static uint8_t *record_audio_buffer = NULL;
uint8_t *audio_rx_buffer = NULL;
audio_play_finish_cb_t audio_play_finish_cb = NULL;

extern sr_data_t *g_sr_data;
extern esp_err_t start_openai(uint8_t *audio, int audio_len);
extern int Cache_WriteBack_Addr(uint32_t addr, uint32_t size);

/* main function */
void mute_btn_handler(void *handle, void *arg)
{
#if CONFIG_BSP_BOARD_ESP32_S3_BOX
    button_event_t event = (button_event_t)arg;

    if (BUTTON_PRESS_DOWN == event) {
        esp_rom_printf(DRAM_STR("Audio Mute On\r\n"));
        mute_flag = true;
    } else {
        esp_rom_printf(DRAM_STR("Audio Mute Off\r\n"));
        mute_flag = false;
    }
#endif
}

static esp_err_t audio_mute_function(AUDIO_PLAYER_MUTE_SETTING setting)
{
#if CONFIG_BSP_BOARD_ESP32S3_BOX1
    bsp_codec_config_t *codec_handle = box1_codec_handle();
#else
    bsp_codec_config_t *codec_handle = bsp_board_get_codec_handle();
#endif

    codec_handle->mute_set_fn(setting == AUDIO_PLAYER_MUTE ? true : false);
    // restore the voice volume upon unmuting
    if (setting == AUDIO_PLAYER_UNMUTE) {
        codec_handle->volume_set_fn(CONFIG_VOLUME_LEVEL, NULL);
    } else {
        app_audio_playback_end();
        if (audio_play_finish_cb) {
            audio_play_finish_cb();
        }
    }
    return ESP_OK;
}

void audio_record_init()
{
    /* Create file if record to SD card enabled*/
#if DEBUG_SAVE_PCM
    record_audio_buffer = heap_caps_calloc(1, FILE_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(record_audio_buffer);
    printf("successfully created record_audio_buffer with a size: %zu\n", FILE_SIZE);
    audio_rx_buffer = heap_caps_calloc(1, MAX_FILE_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(audio_rx_buffer);
    printf("audio_rx_buffer with a size: %zu\n", MAX_FILE_SIZE);
#endif

    if (record_audio_buffer == NULL || audio_rx_buffer == NULL)
    {
        printf("Error: Failed to allocate memory for buffers\n");
        return; // Return or handle the error condition appropriately
    }

    file_iterator_instance_t *file_iterator = file_iterator_new(BSP_SPIFFS_MOUNT_POINT);
    assert(file_iterator != NULL);

#if CONFIG_BSP_BOARD_ESP32S3_BOX1
    bsp_codec_config_t *codec_handle = box1_codec_handle();
#else
    bsp_codec_config_t *codec_handle = bsp_board_get_codec_handle();
#endif
    audio_player_config_t config = { .mute_fn = audio_mute_function,
                                     .write_fn = codec_handle->i2s_write_fn,
                                     .clk_set_fn = codec_handle->i2s_reconfig_clk_fn,
                                     .priority = 5
                                   };
    ESP_ERROR_CHECK(audio_player_new(config));
}

void audio_record_save(int16_t *audio_buffer, int audio_chunksize)
{
#if DEBUG_SAVE_PCM
    if (record_flag) {
        uint16_t *record_buff = (uint16_t *)(record_audio_buffer + sizeof(wav_header_t));
        record_buff += record_total_len;
        for (int i = 0; i < (audio_chunksize - 1); i++) {
            if (record_total_len < (MAX_FILE_SIZE - sizeof(wav_header_t)) / 2) {
#if PCM_ONE_CHANNEL
                record_buff[i] = audio_buffer[i];
                record_total_len += 1;
#else
                record_buff[i * 2 + 0] = audio_buffer[i * 2 + 0];
                record_buff[i * 2 + 1] = audio_buffer[i * 2 + 1];
                record_total_len += 2;
#endif
            }
        }
    }
#endif
}

void audio_register_play_finish_cb(audio_play_finish_cb_t cb)
{
    audio_play_finish_cb = cb;
}

static void audio_record_start()
{
#if DEBUG_SAVE_PCM
    if (app_audio_is_playing()) {
        ESP_LOGI(TAG, "skip record start while audio is playing");
        return;
    }
    ESP_LOGI(TAG, "### record Start");
    audio_player_stop();

    record_flag = true;
    record_total_len = 0;
    file_total_len = sizeof(wav_header_t);
#endif
}

static esp_err_t audio_record_stop()
{
    esp_err_t ret = ESP_OK;
#if DEBUG_SAVE_PCM
    record_flag = false;
#if PCM_ONE_CHANNEL
    record_total_len *= 2;
#else
    record_total_len *= 2;
#endif
    file_total_len += record_total_len;
    ESP_LOGI(TAG, "### record Stop, %" PRIu32 " %" PRIu32 "K", \
         record_total_len, \
         record_total_len / 1024);

    FILE *fp = fopen("/spiffs/echo_en_wake.wav", "r");
    ESP_GOTO_ON_FALSE(NULL != fp, ESP_FAIL, err, TAG, "Failed create record file");

    wav_header_t wav_head;
    int len = fread(&wav_head, 1, sizeof(wav_header_t), fp);
    ESP_GOTO_ON_FALSE(len > 0, ESP_FAIL, err, TAG, "Failed create record file");

    wav_head.SampleRate = 16000;
#if PCM_ONE_CHANNEL
    wav_head.NumChannels = 1;
#else
    wav_head.NumChannels = 2;
#endif
    wav_head.BitsPerSample = 16;
    wav_head.ChunkSize = file_total_len - 8;
    wav_head.ByteRate = wav_head.SampleRate * wav_head.BitsPerSample * wav_head.NumChannels / 8;
    wav_head.Subchunk2ID[0] = 'd';
    wav_head.Subchunk2ID[1] = 'a';
    wav_head.Subchunk2ID[2] = 't';
    wav_head.Subchunk2ID[3] = 'a';
    wav_head.Subchunk2Size = record_total_len;
    memcpy((void *)record_audio_buffer, &wav_head, sizeof(wav_header_t));
    Cache_WriteBack_Addr((uint32_t)record_audio_buffer, record_total_len);
    // audio_player_play(record_audio_buffer, file_total_len);

#endif
err:
    if (fp) {
        fclose(fp);
    }
    return ret;
}

esp_err_t audio_play_task(void *filepath)
{
    FILE *fp = NULL;
    struct stat file_stat;
    esp_err_t ret = ESP_OK;

    const size_t chunk_size = 4096;
    uint8_t *buffer = malloc(chunk_size);
    uint8_t *stereo_buffer = NULL;
    ESP_GOTO_ON_FALSE(NULL != buffer, ESP_FAIL, EXIT, TAG, "buffer malloc failed");
    stereo_buffer = malloc(chunk_size * 2);
    ESP_GOTO_ON_FALSE(NULL != stereo_buffer, ESP_FAIL, EXIT, TAG, "stereo buffer malloc failed");

    ESP_GOTO_ON_FALSE(-1 != stat(filepath, &file_stat), ESP_FAIL, EXIT, TAG, "Failed to stat file");

    fp = fopen(filepath, "r");
    ESP_GOTO_ON_FALSE(NULL != fp, ESP_FAIL, EXIT, TAG, "Failed create record file");

    wav_header_t wav_head;
    int len = fread(&wav_head, 1, sizeof(wav_header_t), fp);
    ESP_GOTO_ON_FALSE(len > 0, ESP_FAIL, EXIT, TAG, "Read wav header failed");

    if (NULL == strstr((char *)wav_head.Subchunk1ID, "fmt") &&
            NULL == strstr((char *)wav_head.Subchunk2ID, "data")) {
        ESP_LOGI(TAG, "PCM format");
        fseek(fp, 0, SEEK_SET);
        wav_head.SampleRate = 16000;
        wav_head.NumChannels = 2;
        wav_head.BitsPerSample = 16;
    }

#if CONFIG_BSP_BOARD_ESP32S3_BOX1
    bsp_codec_config_t *codec_handle = box1_codec_handle();
#else
    bsp_codec_config_t *codec_handle = bsp_board_get_codec_handle();
#endif

    ESP_LOGI(TAG, "frame_rate= %" PRIi32 ", ch=%d, width=%d", wav_head.SampleRate, wav_head.NumChannels, wav_head.BitsPerSample);
    ret = codec_handle->i2s_reconfig_clk_fn(wav_head.SampleRate, wav_head.BitsPerSample, I2S_SLOT_MODE_STEREO);
    ESP_GOTO_ON_FALSE(ret == ESP_OK, ret, EXIT, TAG, "I2S clock reconfig failed");

    app_audio_playback_begin();
    codec_handle->mute_set_fn(false);
    codec_handle->volume_set_fn(CONFIG_VOLUME_LEVEL,NULL);
    vTaskDelay(pdMS_TO_TICKS(500));

    size_t cnt, total_cnt = 0;
    do {
        /* Read file in chunks into the scratch buffer */
        len = fread(buffer, 1, chunk_size, fp);
        if (len <= 0) {
            break;
        } else if (len > 0) {
            if (wav_head.NumChannels == 1 && wav_head.BitsPerSample == 16) {
                int sample_count = len / sizeof(int16_t);
                int16_t *mono = (int16_t *)buffer;
                int16_t *stereo = (int16_t *)stereo_buffer;
                for (int i = 0; i < sample_count; i++) {
                    stereo[i * 2] = mono[i];
                    stereo[i * 2 + 1] = mono[i];
                }
                codec_handle->i2s_write_fn(stereo_buffer, sample_count * 2 * sizeof(int16_t), &cnt, portMAX_DELAY);
            } else {
                codec_handle->i2s_write_fn(buffer, len, &cnt, portMAX_DELAY);
            }
            total_cnt += cnt;
        }
    } while (1);
    printf("audio play end, %d, %d K\r\n", total_cnt, total_cnt / 1024);

EXIT:
    app_audio_playback_end();
    if (fp) {
        fclose(fp);
    }
    if (buffer) {
        free(buffer);
    }
    if (stereo_buffer) {
        free(stereo_buffer);
    }
    return ret;
}

esp_err_t audio_mp3_load(void *filepath, size_t *file_len)
{
    FILE *fp = NULL;
    struct stat file_stat;
    esp_err_t ret = ESP_OK;

    size_t len, total_cnt = 0;

    const size_t chunk_size = 4096;
    uint8_t *buffer = malloc(chunk_size);
    ESP_GOTO_ON_FALSE(NULL != buffer, ESP_FAIL, EXIT, TAG, "buffer malloc failed");

    ESP_GOTO_ON_FALSE(-1 != stat(filepath, &file_stat), ESP_FAIL, EXIT, TAG, "Failed to stat file");

    fp = fopen(filepath, "r");
    ESP_GOTO_ON_FALSE(NULL != fp, ESP_FAIL, EXIT, TAG, "Failed create record file");

    do {
        /* Read file in chunks into the scratch buffer */
        len = fread(buffer, 1, chunk_size, fp);
        if (len <= 0) {
            break;
        } else if (len > 0) {
            memcpy((void *)(audio_rx_buffer + total_cnt), buffer, len);
            total_cnt += len;
        }
    } while (1);
    printf("audio load end, %d, %d K\r\n", total_cnt, total_cnt / 1024);

EXIT:
    if (fp) {
        fclose(fp);
    }
    if (buffer) {
        free(buffer);
    }
    *file_len = total_cnt;
    return ret;
}

void sr_handler_task(void *pvParam)
{
    static bool mute_state = false;

#if CONFIG_BSP_BOARD_ESP32_S3_BOX
    mute_flag = bsp_button_get(BSP_BUTTON_MUTE);
    printf("sr handle task, mute:%d\n", mute_flag);
#endif

    while (true) {
        if (NEED_DELETE && xEventGroupGetBits(g_sr_data->event_group)) {
            xEventGroupSetBits(g_sr_data->event_group, HANDLE_DELETED);
            vTaskDelete(NULL);
        }

        sr_result_t result = {
            .wakenet_mode = WAKENET_NO_DETECT,
            .state = ESP_MN_STATE_DETECTING,
        };

        app_sr_get_result(&result, pdMS_TO_TICKS(1 * 1000));
        if (app_audio_is_playing()) {
            continue;
        }

#if CONFIG_BSP_BOARD_ESP32_S3_BOX
        if (mute_state != mute_flag) {
            mute_state = mute_flag;
            if (false == mute_state) {
                ESP_LOGI(TAG, "reset CODEC");
                bsp_codec_config_t *bsp_codec_config = bsp_board_get_codec_handle();
                bsp_codec_config->codec_reconfig_fn();
            }
        }
#elif CONFIG_BSP_BOARD_ESP32S3_BOX1
        // BOX1 mute: handled by box1_mute_set() via amp GPIO
#endif
        if (ESP_MN_STATE_TIMEOUT == result.state) {
            ESP_LOGI(TAG, "ESP_MN_STATE_TIMEOUT");
            audio_record_stop();
            // audio_play_task("/spiffs/echo_en_wake.wav");
            size_t len = 0;
            esp_err_t load_ret = audio_mp3_load("/spiffs/waitPlease_cn.mp3", &len);
            ESP_LOGI(TAG, "waitPlease load ret=%s len=%u record_total_len=%" PRIu32 ", file_total_len=%" PRIu32,
                     esp_err_to_name(load_ret), (unsigned)len, record_total_len, file_total_len);
            if (len && !app_audio_is_playing()) {
                esp_err_t play_ret = audio_player_play(audio_rx_buffer, len);
                ESP_LOGI(TAG, "waitPlease play ret=%s", esp_err_to_name(play_ret));
            }
            uint32_t starttime = esp_log_timestamp();
            WiFi_Connect_Status wifi_status = wifi_connected_already();
            ESP_LOGE(TAG, "[Start] start_openai, timestamp: %" PRIu32 ", wifi_status=%d", starttime, wifi_status);
            if (WIFI_STATUS_CONNECTED_OK == wifi_status) {
                esp_err_t openai_ret = start_openai((uint8_t *)record_audio_buffer, file_total_len);
                ESP_LOGI(TAG, "start_openai ret=%s", esp_err_to_name(openai_ret));
            } else {
                ESP_LOGE(TAG, "skip start_openai: Wi-Fi is not connected, status=%d", wifi_status);
                ui_ctrl_label_show_text(UI_CTRL_LABEL_LISTEN_SPEAK, "Wi-Fi disconnected");
                ui_ctrl_show_panel(UI_CTRL_PANEL_SLEEP, 2000);
            }
            ESP_LOGE(TAG, "[End] start_openai, +offset:%" PRIu32, esp_log_timestamp() - starttime);
            continue;
        }

        if (WAKENET_DETECTED == result.wakenet_mode) {
            audio_record_start();

            // UI show listen
            ui_ctrl_guide_jump();
            ui_ctrl_show_panel(UI_CTRL_PANEL_LISTEN, 0);

            audio_play_task("/spiffs/echo_en_wake.wav");
            continue;
        }

        if (ESP_MN_STATE_DETECTED & result.state) {
            ESP_LOGI(TAG, "STOP:%d", result.command_id);
            audio_record_stop();
            audio_play_task("/spiffs/echo_en_ok.wav");
            //How to stop the transmission, when start_openai begins.
            continue;
        }
    }
    vTaskDelete(NULL);
}
