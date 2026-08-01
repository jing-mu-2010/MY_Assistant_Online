/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "app_ui_ctrl.h"
#include "app_buttons.h"
#include "tts_api.h"
#include "app_sr.h"
#include "bsp/esp-bsp.h"
#include "app_audio.h"
#include "app_wifi.h"
#include "settings.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "xl9555.h"
#include "box1_lcd.h"
#include "esp_lvgl_port.h"

#include "stt_api.h"
#include "chat_api.h"

#define SCROLL_START_DELAY_S (1.5)
static char *TAG = "app_main";
static sys_param_t *sys_param = NULL;


/* program flow. This function is called in app_audio.c */
esp_err_t start_openai(uint8_t *audio, int audio_len)
{
    ESP_LOGE(TAG, "start_openai audio_len=%d\n", audio_len);

    char *recognition_result = mfg_speech_to_text(audio, audio_len); // 百度语音转文字
    ESP_LOGE(TAG, "------------text: %s\n", recognition_result);

    if (recognition_result == NULL) {
        ESP_LOGE(TAG, "speech_to_text returned NULL");
        ui_ctrl_label_show_text(UI_CTRL_LABEL_LISTEN_SPEAK, "Speech recognition failed");
        ui_ctrl_show_panel(UI_CTRL_PANEL_SLEEP, 2000);
        return ESP_FAIL;
    }

    if (strlen(recognition_result) == 0)
    {
        ESP_LOGE(TAG, "speech_to_text returned empty text");
        ui_ctrl_label_show_text(UI_CTRL_LABEL_LISTEN_SPEAK, "No speech recognized");
        ui_ctrl_show_panel(UI_CTRL_PANEL_SLEEP, 2000);
        return ESP_FAIL;
    }
    ESP_LOGE(TAG, "user: %s\n", recognition_result);

    if (strcmp(recognition_result, "invalid_request_error") == 0)
    {
        ui_ctrl_label_show_text(UI_CTRL_LABEL_LISTEN_SPEAK, "Sorry, I can't understand.");
        ui_ctrl_show_panel(UI_CTRL_PANEL_SLEEP, 2000);
        return ESP_FAIL;
    }

    char *response = mfg_agent_chat(recognition_result, sys_param->key); // 获得agent的回答（API Key从NVS配置传入）
    ESP_LOGE(TAG, "------------response: %s\n", response);

    if (response == NULL || response[0] == '\0') {
        ESP_LOGE(TAG, "agent_chat returned empty response");
        ui_ctrl_label_show_text(UI_CTRL_LABEL_LISTEN_SPEAK, "Chat API failed");
        ui_ctrl_show_panel(UI_CTRL_PANEL_SLEEP, 2000);
        return ESP_FAIL;
    }

    ui_ctrl_show_panel(UI_CTRL_PANEL_GET, 0);
    // UI listen success
    ui_ctrl_label_show_text(UI_CTRL_LABEL_REPLY_QUESTION, recognition_result);
    ui_ctrl_label_show_text(UI_CTRL_LABEL_LISTEN_SPEAK, recognition_result);

    if (response != NULL && (strcmp(response, "invalid_request_error") == 0))
    {
        // UI listen fail
        ui_ctrl_label_show_text(UI_CTRL_LABEL_LISTEN_SPEAK, "Sorry, I can't understand.");
        ui_ctrl_show_panel(UI_CTRL_PANEL_SLEEP, 2000);
        return ESP_FAIL;
    }

    // UI listen success
    ui_ctrl_label_show_text(UI_CTRL_LABEL_REPLY_QUESTION, response);
    ui_ctrl_label_show_text(UI_CTRL_LABEL_LISTEN_SPEAK, response);

    if (strcmp(response, "invalid_request_error") == 0)
    {
        ui_ctrl_label_show_text(UI_CTRL_LABEL_LISTEN_SPEAK, "Sorry, I can't understand.");
        ui_ctrl_show_panel(UI_CTRL_PANEL_SLEEP, 2000);
        return ESP_FAIL;
    }

    ui_ctrl_label_show_text(UI_CTRL_LABEL_REPLY_CONTENT, response);
    ui_ctrl_show_panel(UI_CTRL_PANEL_REPLY, 0);

    ESP_LOGE(TAG, "start tts\n");
    esp_err_t status = text_to_speech_request(response, AUDIO_CODECS_MP3);

    if (status != ESP_OK)
    {
        ESP_LOGE(TAG, "Error creating ChatGPT request: %s\n", esp_err_to_name(status));
        // UI reply audio fail
        ui_ctrl_show_panel(UI_CTRL_PANEL_SLEEP, 0);
    }
    else
    {
        // Wait a moment before starting to scroll the reply content
        vTaskDelay(pdMS_TO_TICKS(SCROLL_START_DELAY_S * 1000));
        ui_ctrl_reply_set_audio_start_flag(true);
    }
    // Clearing resources
    // result->delete(result);
    // free(text);
    return ESP_OK;
}

/* play audio function */

static void audio_play_finish_cb(void)
{
    ESP_LOGI(TAG, "replay audio end");
    if (ui_ctrl_reply_get_audio_start_flag())
    {
        ui_ctrl_reply_set_audio_end_flag(true);
    }
}

static esp_err_t box1_board_init(void)
{
    const struct {
        gpio_num_t sda;
        gpio_num_t scl;
    } i2c_pins[] = {
        { GPIO_NUM_48, GPIO_NUM_45 },
        { GPIO_NUM_45, GPIO_NUM_48 },
        { GPIO_NUM_48, GPIO_NUM_47 },
        { GPIO_NUM_47, GPIO_NUM_48 },
    };

    uint8_t read_reg = XL9555_INPUT_PORT0_REG;
    uint8_t read_data[2] = { 0 };
    bool driver_installed = false;

    for (size_t i = 0; i < sizeof(i2c_pins) / sizeof(i2c_pins[0]); i++) {
        if (driver_installed) {
            i2c_driver_delete(I2C_NUM_0);
            driver_installed = false;
        }

        i2c_config_t i2c_cfg = {
            .mode = I2C_MODE_MASTER,
            .sda_io_num = i2c_pins[i].sda,
            .scl_io_num = i2c_pins[i].scl,
            .sda_pullup_en = GPIO_PULLUP_ENABLE,
            .scl_pullup_en = GPIO_PULLUP_ENABLE,
            .master.clk_speed = 100000,
        };
        ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c_cfg));
        ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));
        driver_installed = true;

        for (uint8_t addr = 0x20; addr <= 0x27; addr++) {
            esp_err_t ret = i2c_master_write_read_device(
                I2C_NUM_0,
                addr,
                &read_reg,
                sizeof(read_reg),
                read_data,
                sizeof(read_data),
                pdMS_TO_TICKS(100));

            if (ret == ESP_OK) {
                xl9555_set_addr(addr);
                ESP_LOGI(TAG, "XL9555 found at 0x%02X, SDA GPIO%d, SCL GPIO%d",
                         addr, i2c_pins[i].sda, i2c_pins[i].scl);
                return ESP_OK;
            }
        }
    }

    ESP_LOGE(TAG, "XL9555 not found on GPIO48/45, GPIO45/48, GPIO48/47 or GPIO47/48, addresses 0x20-0x27");
    return ESP_ERR_NOT_FOUND;
}

void app_main()
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(settings_read_parameter_from_nvs());
    sys_param = settings_get_parameter();

    bsp_spiffs_mount();
#if CONFIG_BSP_BOARD_ESP32S3_BOX1
    /* BOX1 hardware init: I2C → XL9555 → Codec → LCD (8080) → Buttons → LVGL */
    ESP_ERROR_CHECK(box1_board_init()); /* I2C + XL9555 probe */
    ESP_ERROR_CHECK(xl9555_init());     /* IO expander (backlight, buttons, speaker) */
    ESP_ERROR_CHECK(box1_codec_init()); /* ES8311 audio codec */
    app_buttons_init();             /* GPIO 0/1 fallback buttons */

    /* LVGL port init */
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    /* MCU 8080 parallel LCD init */
    esp_lcd_panel_io_handle_t lcd_io = NULL;
    esp_lcd_panel_handle_t lcd_panel = NULL;
    ESP_ERROR_CHECK(box1_lcd_init(&lcd_io, &lcd_panel));

    /* Register display with LVGL */
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = lcd_io,
        .panel_handle = lcd_panel,
        .buffer_size = 320 * CONFIG_BSP_LCD_DRAW_BUF_HEIGHT,
        .hres = 320,
        .vres = 240,
        .monochrome = false,
        .rotation = {
            .swap_xy = true,
            .mirror_x = true,
            .mirror_y = false,
        },
        .flags = { .buff_dma = true, .buff_spiram = false },
    };
    lvgl_port_add_disp(&disp_cfg);

    /* Turn on backlight via XL9555 */
    xl9555_pin_write(LCD_BL_IO, 1);
    /* Enable speaker amp via XL9555 */
    xl9555_pin_write(SPK_CTRL_IO, 1);
#else
    bsp_i2c_init();
    bsp_display_start();
#endif

    ESP_LOGI(TAG, "Display LVGL demo");
    ui_ctrl_init();
    app_network_start();

    ESP_LOGI(TAG, "speech recognition start");
    app_sr_start(false);
    audio_register_play_finish_cb(audio_play_finish_cb);

    while (true)
    {
        ESP_LOGD(TAG, "\tDescription\tInternal\tSPIRAM");
        ESP_LOGD(TAG, "Current Free Memory\t%d\t\t%d",
                 heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
                 heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        ESP_LOGD(TAG, "Min. Ever Free Size\t%d\t\t%d",
                 heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
                 heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
        vTaskDelay(pdMS_TO_TICKS(5 * 1000));
    }
}
