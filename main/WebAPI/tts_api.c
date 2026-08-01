/* Baidu TTS — text-to-speech via tsn.baidu.com */
#include <string.h>
#include "tts_api.h"
#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "app_audio.h"
#include "audio_player.h"
#include "esp_crt_bundle.h"
#include "inttypes.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "stt_api.h"

#define BAIDU_TTS_URL   "https://tsn.baidu.com/text2audio"
#define BAIDU_CUID      "BOX1"
#define BAIDU_TTS_VOICE 0       /* 0=普通女声, 1=普通男声, 3=度逍遥, 4=度丫丫 */
#define BAIDU_TTS_SPEED 5       /* 0-15 */
#define BAIDU_TTS_PITCH 5       /* 0-15 */
#define BAIDU_TTS_VOL   5       /* 0-15 */

static const char *TAG = "TTS-Baidu";

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_HEADER:
        file_total_len = 0;
        break;
    case HTTP_EVENT_ON_DATA:
        if ((file_total_len + evt->data_len) < MAX_FILE_SIZE) {
            memcpy(audio_rx_buffer + file_total_len, (char *)evt->data, evt->data_len);
            file_total_len += evt->data_len;
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "TTS download done: %"PRIu32" bytes", file_total_len);
        if (file_total_len > 0) {
            app_audio_playback_begin();
            audio_player_play(audio_rx_buffer, file_total_len);
        } else {
            ESP_LOGE(TAG, "TTS response empty");
        }
        break;
    case HTTP_EVENT_DISCONNECTED:
    case HTTP_EVENT_ON_CONNECTED:
    case HTTP_EVENT_HEADER_SENT:
    case HTTP_EVENT_REDIRECT:
        break;
    }
    return ESP_OK;
}

static char dec2hex(short int c)
{
    if (0 <= c && c <= 9) return c + '0';
    else if (10 <= c && c <= 15) return c + 'A' - 10;
    return -1;
}

void url_encode(const char *url, char *encode_out)
{
    int res_len = 0;
    int len = strlen(url);
    for (int i = 0; i < len; ++i) {
        char c = url[i];
        if (c == '\\' && (i + 1) < len && url[i + 1] == 'n') { i += 1; continue; }
        if (('0' <= c && c <= '9') || ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || c == '/' || c == '.') {
            encode_out[res_len++] = c;
        } else {
            int j = (short int)c; if (j < 0) j += 256;
            encode_out[res_len++] = '%';
            encode_out[res_len++] = dec2hex(j / 16);
            encode_out[res_len++] = dec2hex(j - (j / 16) * 16);
        }
    }
    encode_out[res_len] = '\0';
}

esp_err_t text_to_speech_request(const char *message, AUDIO_CODECS_FORMAT code_format)
{
    if (message == NULL || message[0] == '\0') {
        ESP_LOGE(TAG, "TTS message empty");
        return ESP_ERR_INVALID_ARG;
    }

    /* Get Baidu OAuth token (shared with STT) */
    char *token = getAccessToken();
    if (token == NULL) {
        ESP_LOGE(TAG, "Baidu token failed");
        return ESP_FAIL;
    }

    /* URL-encode the text */
    size_t msg_len = strlen(message);
    char *encoded = heap_caps_malloc(3 * msg_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!encoded) { free(token); return ESP_ERR_NO_MEM; }
    url_encode(message, encoded);

    /* Build Baidu TTS URL */
    const char *fmt = (code_format == AUDIO_CODECS_MP3) ? "3" : "6";   /* 3=mp3, 6=wav */
    int url_size = snprintf(NULL, 0,
        BAIDU_TTS_URL "?tex=%s&tok=%s&cuid=%s&ctp=1&lan=zh&spd=%d&pit=%d&vol=%d&per=%d&aue=%s",
        encoded, token, BAIDU_CUID, BAIDU_TTS_SPEED, BAIDU_TTS_PITCH, BAIDU_TTS_VOL, BAIDU_TTS_VOICE, fmt);
    char *url = heap_caps_malloc(url_size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!url) { heap_caps_free(encoded); free(token); return ESP_ERR_NO_MEM; }
    snprintf(url, url_size + 1,
        BAIDU_TTS_URL "?tex=%s&tok=%s&cuid=%s&ctp=1&lan=zh&spd=%d&pit=%d&vol=%d&per=%d&aue=%s",
        encoded, token, BAIDU_CUID, BAIDU_TTS_SPEED, BAIDU_TTS_PITCH, BAIDU_TTS_VOL, BAIDU_TTS_VOICE, fmt);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .buffer_size = 256000,
        .buffer_size_tx = 4000,
        .timeout_ms = 40000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    uint32_t starttime = esp_log_timestamp();
    ESP_LOGI(TAG, "[TTS] requesting...");
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = ESP_FAIL;
    if (client) {
        err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            int code = esp_http_client_get_status_code(client);
            ESP_LOGI(TAG, "[TTS] HTTP %d, %"PRIu32" bytes, %"PRIu32"ms",
                     code, file_total_len, esp_log_timestamp() - starttime);
        } else {
            ESP_LOGE(TAG, "[TTS] HTTP failed: %s", esp_err_to_name(err));
        }
    }
    esp_http_client_cleanup(client);
    heap_caps_free(url);
    heap_caps_free(encoded);
    free(token);
    return err;
}
