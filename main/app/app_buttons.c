/*
 * Button input for BOX1:
 *   K0 (GPIO 0) = Config button, single click plays intro voice
 *   K1+K2 (XL9555) = vol down / vol up / mute
 *
 * GPIO 0 uses iot_button. XL9555 keys are polled in a dedicated task.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "iot_button.h"
#include "xl9555.h"

#include "app_audio.h"
#include "app_ui_ctrl.h"
#include "app_buttons.h"
#include "tts_api.h"

#define BOX1_CONFIG_BUTTON_IO   (GPIO_NUM_0)
#define BUTTON_EVT_QUEUE_LEN    (8)
#define BUTTON_TASK_STACK       (4096)
#define BUTTON_TASK_PRIO        (4)
#define XL9555_SCAN_MS          (30)

static const char *TAG = "app_buttons";

typedef enum {
    BTN_EVT_VOLUME_DOWN = 0,
    BTN_EVT_VOLUME_UP,
    BTN_EVT_MUTE_TOGGLE,
    BTN_EVT_INTRO_PLAY,
} btn_evt_t;

static QueueHandle_t btn_evt_queue = NULL;

static void config_button_cb(void *handle, void *usr_data)
{
    btn_evt_t evt = BTN_EVT_INTRO_PLAY;
    xQueueSend(btn_evt_queue, &evt, 0);
}

static void xl9555_scan_task(void *arg)
{
    int k1_hold = 0, k2_hold = 0;
    while (true) {
        uint8_t key = xl9555_key_scan(0);

        if (key == KEY0_PRES) {
            k1_hold++;
            k2_hold = 0;
            if (k1_hold == 1) {
                vTaskDelay(pdMS_TO_TICKS(500));
                if (xl9555_key_scan(0) == KEY0_PRES) {
                    btn_evt_t evt = BTN_EVT_MUTE_TOGGLE;
                    xQueueSend(btn_evt_queue, &evt, 0);
                    while (xl9555_key_scan(0) != 0) {
                        vTaskDelay(pdMS_TO_TICKS(50));
                    }
                    k1_hold = 0;
                } else {
                    btn_evt_t evt = BTN_EVT_VOLUME_DOWN;
                    xQueueSend(btn_evt_queue, &evt, 0);
                    k1_hold = 0;
                }
            }
        } else if (key == KEY1_PRES) {
            k2_hold++;
            k1_hold = 0;
            if (k2_hold == 1) {
                btn_evt_t evt = BTN_EVT_VOLUME_UP;
                xQueueSend(btn_evt_queue, &evt, 0);
                vTaskDelay(pdMS_TO_TICKS(200));
                k2_hold = 0;
            }
        } else {
            k1_hold = 0;
            k2_hold = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(XL9555_SCAN_MS));
    }
}

static void button_handler_task(void *arg)
{
    btn_evt_t evt;
    while (true) {
        if (xQueueReceive(btn_evt_queue, &evt, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        ESP_LOGI(TAG, "button evt:%d", (int)evt);
        switch (evt) {
        case BTN_EVT_VOLUME_DOWN:
            app_audio_volume_down();
            ui_ctrl_volume_show(app_audio_volume_get(), app_audio_mute_get());
            break;
        case BTN_EVT_VOLUME_UP:
            app_audio_volume_up();
            ui_ctrl_volume_show(app_audio_volume_get(), app_audio_mute_get());
            break;
        case BTN_EVT_MUTE_TOGGLE:
            app_audio_mute_toggle();
            ui_ctrl_volume_show(app_audio_volume_get(), app_audio_mute_get());
            break;
        case BTN_EVT_INTRO_PLAY: {
            if (app_audio_is_playing()) {
                ESP_LOGI(TAG, "Config button ignored while audio is playing");
                break;
            }
            ESP_LOGI(TAG, "Config button: enter listen → wait → play");

            /* 1. Ding sound, then switch to LISTEN */
            audio_play_task("/spiffs/echo_en_wake.wav");
            ui_ctrl_show_panel(UI_CTRL_PANEL_LISTEN, 0);

            /* 2. Silent wait 15 seconds */
            vTaskDelay(pdMS_TO_TICKS(15000));

            /* 3. Show text + start scroll + play audio (audio_play_task blocks, scroll runs in LVGL task) */
            const char *intro_text =
                "大三看到室友都在努力，感到焦虑太正常啦。"
                "先深呼吸，别被别人的节奏带偏。考研只是人生的一条路，不是必选项。"
                "你可以静下心来问问自己，到底想要什么。是想继续深造，还是想早点工作？"
                "如果决定考研，那就定好计划，踏实复习。如果不考，就把精力放在找实习、学技能上。"
                "找到属于自己的方向，只要行动起来，焦虑自然就会消失。"
                "别怕，按自己的节奏走，你一定能做出最适合的选择！";
            ui_ctrl_label_show_text(UI_CTRL_LABEL_REPLY_CONTENT, intro_text);
            ui_ctrl_show_panel(UI_CTRL_PANEL_REPLY, 0);
            ui_ctrl_reply_set_audio_start_flag(true);
            esp_err_t status = audio_play_task("/spiffs/config_voice.wav");
            if (status != ESP_OK) {
                ESP_LOGE(TAG, "Intro playback failed: %s", esp_err_to_name(status));
                ui_ctrl_show_panel(UI_CTRL_PANEL_SLEEP, 1000);
            }
            break;
        }
        default:
            break;
        }
        ESP_LOGI(TAG, "evt:%d vol:%d mute:%d", (int)evt, app_audio_volume_get(), (int)app_audio_mute_get());
    }
}

esp_err_t app_buttons_init(void)
{
    btn_evt_queue = xQueueCreate(BUTTON_EVT_QUEUE_LEN, sizeof(btn_evt_t));
    if (btn_evt_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const button_config_t config_cfg = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = { .gpio_num = BOX1_CONFIG_BUTTON_IO, .active_level = 0 },
    };
    button_handle_t btn = iot_button_create(&config_cfg);
    if (btn) {
        iot_button_register_cb(btn, BUTTON_SINGLE_CLICK, config_button_cb, NULL);
        ESP_LOGI(TAG, "K0 (GPIO0) ready");
    } else {
        ESP_LOGE(TAG, "K0 (GPIO0) init failed");
    }

    xTaskCreate(button_handler_task, "Button Task", BUTTON_TASK_STACK, NULL, BUTTON_TASK_PRIO, NULL);
    xTaskCreate(xl9555_scan_task, "XL9555 Scan", 2048, NULL, BUTTON_TASK_PRIO - 1, NULL);

    ESP_LOGI(TAG, "buttons ready: K0=intro  K1=vol-/mute  K2=vol+");
    return ESP_OK;
}
