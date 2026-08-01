#include "stt_api.h"

#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "stt_api";

static char response_data[2048];
static int received_len;

static esp_err_t http_client_event_handler(esp_http_client_event_t *evt)
{
    char *buf = (char *)evt->user_data;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_CONNECTED:
        received_len = 0;
        if (buf) {
            buf[0] = '\0';
        }
        break;
    case HTTP_EVENT_ON_DATA:
        if (buf) {
            int copy_len = evt->data_len;
            if (received_len + copy_len >= (int)sizeof(response_data)) {
                copy_len = sizeof(response_data) - received_len - 1;
            }
            if (copy_len > 0) {
                memcpy(buf + received_len, evt->data, copy_len);
                received_len += copy_len;
                buf[received_len] = '\0';
            }
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "request finished, bytes=%d", received_len);
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "disconnected");
        break;
    case HTTP_EVENT_ERROR:
        ESP_LOGE(TAG, "HTTP event error");
        break;
    default:
        break;
    }

    return ESP_OK;
}

char *getAccessToken(void)
{
    char *access_token = NULL;
    memset(response_data, 0, sizeof(response_data));

    esp_http_client_config_t config = {
        .url = TOKEN_URL,
        .event_handler = http_client_event_handler,
        .user_data = response_data,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Baidu token client init failed");
        return NULL;
    }

    char request_params[200];
    snprintf(request_params, sizeof(request_params),
             "grant_type=client_credentials&client_id=%s&client_secret=%s",
             API_KEY, SECRET_KEY);

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_post_field(client, request_params, strlen(request_params));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Baidu token HTTP status=%d, response=%s", status_code, response_data);

        cJSON *json = cJSON_Parse(response_data);
        if (json != NULL) {
            cJSON *token_json = cJSON_GetObjectItem(json, "access_token");
            if (token_json != NULL && cJSON_IsString(token_json)) {
                access_token = strdup(token_json->valuestring);
            } else {
                cJSON *err_json = cJSON_GetObjectItem(json, "error_description");
                ESP_LOGE(TAG, "Baidu token missing access_token: %s",
                         (err_json && cJSON_IsString(err_json)) ? err_json->valuestring : "unknown");
            }
            cJSON_Delete(json);
        } else {
            ESP_LOGE(TAG, "Baidu token JSON parse failed");
        }
    } else {
        ESP_LOGE(TAG, "Baidu token request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return access_token;
}

char *mfg_speech_to_text(uint8_t *audio_data, int audio_len)
{
    if (audio_data == NULL || audio_len <= 0) {
        ESP_LOGE(TAG, "invalid audio data, len=%d", audio_len);
        return NULL;
    }

    char *asr_data = NULL;
    char url[512];
    memset(response_data, 0, sizeof(response_data));

    char *token = getAccessToken();
    if (token == NULL) {
        ESP_LOGE(TAG, "Failed to get Baidu access token");
        return NULL;
    }
    ESP_LOGI(TAG, "Baidu access token obtained successfully");

    snprintf(url, sizeof(url), "%s?dev_pid=1537&cuid=BOX1&token=%s", BAIDU_ASR_URL, token);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_client_event_handler,
        .user_data = response_data,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 20000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Baidu ASR client init failed");
        free(token);
        return NULL;
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_post_field(client, (const char *)audio_data, audio_len);
    esp_http_client_set_header(client, "Content-Type", "audio/wav;rate=16000");

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Baidu ASR HTTP status=%d, response=%s", status_code, response_data);

        cJSON *json = cJSON_Parse(response_data);
        if (json != NULL) {
            cJSON *result_json = cJSON_GetObjectItem(json, "result");
            if (result_json != NULL && cJSON_IsArray(result_json)) {
                cJSON *result_array = cJSON_GetArrayItem(result_json, 0);
                if (result_array != NULL && cJSON_IsString(result_array)) {
                    asr_data = strdup(result_array->valuestring);
                }
            }
            if (asr_data == NULL) {
                cJSON *err_msg = cJSON_GetObjectItem(json, "err_msg");
                ESP_LOGE(TAG, "Baidu ASR missing result: %s",
                         (err_msg && cJSON_IsString(err_msg)) ? err_msg->valuestring : "unknown");
            }
            cJSON_Delete(json);
        } else {
            ESP_LOGE(TAG, "Baidu ASR JSON parse failed");
        }
    } else {
        ESP_LOGE(TAG, "Baidu ASR HTTP POST failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(token);
    ESP_LOGI(TAG, "ASR result: %s", asr_data ? asr_data : "(null)");
    return asr_data;
}
