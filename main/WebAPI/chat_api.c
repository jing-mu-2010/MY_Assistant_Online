#include "chat_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "chat_api";

static const char *url = "https://ws-w456lmthuttpug1v.cn-beijing.maas.aliyuncs.com/compatible-mode/v1/chat/completions";
static const char *model = "qwen3.6-flash";

#define CHAT_API_KEY "sk-ws-H.REHEYHX.ZLzg.MEUCIQDlCoqtuTDXTjdRc4LBFQdavzziswvx9cZ6sYhZAcsHGgIgLZjliaCzNhHra5rWja_Rd9Tid0Hdz5pgnn3S2v_Kja8"

#define CHAT_RESPONSE_SIZE (4096 * 2)
#define MAX_CHAT_HISTORY 20

static char response_data[CHAT_RESPONSE_SIZE];
static int received_len;
static char *chat_history[MAX_CHAT_HISTORY];
static int chat_history_length;

static esp_err_t http_client_event_handler1(esp_http_client_event_t *evt)
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
            if (received_len + copy_len >= CHAT_RESPONSE_SIZE) {
                copy_len = CHAT_RESPONSE_SIZE - received_len - 1;
            }
            if (copy_len > 0) {
                memcpy(buf + received_len, evt->data, copy_len);
                received_len += copy_len;
                buf[received_len] = '\0';
            }
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "chat request finished, bytes=%d", received_len);
        break;
    case HTTP_EVENT_ERROR:
        ESP_LOGE(TAG, "chat HTTP event error");
        break;
    default:
        break;
    }

    return ESP_OK;
}

static void chat_history_add(const char *prompt)
{
    if (chat_history_length >= MAX_CHAT_HISTORY) {
        free(chat_history[0]);
        for (int i = 0; i < chat_history_length - 1; i++) {
            chat_history[i] = chat_history[i + 1];
        }
        chat_history_length--;
    }

    chat_history[chat_history_length] = strdup(prompt);
    if (chat_history[chat_history_length]) {
        chat_history_length++;
    }
}

char *mfg_agent_chat(char *prompt, const char *api_key)
{
    const char *effective_api_key = CHAT_API_KEY;

    if (prompt == NULL || prompt[0] == '\0') {
        ESP_LOGE(TAG, "empty chat prompt");
        return NULL;
    }
    if (strcmp(effective_api_key, "你的真实key") == 0) {
        effective_api_key = api_key;
    }
    if (effective_api_key == NULL || effective_api_key[0] == '\0' || strcmp(effective_api_key, "sk-xxxxxxxx") == 0) {
        ESP_LOGE(TAG, "chat api key is empty or not configured");
        return NULL;
    }

    char *answer = NULL;
    memset(response_data, 0, sizeof(response_data));

    cJSON *root = cJSON_CreateObject();
    cJSON *messages_array = cJSON_CreateArray();
    if (root == NULL || messages_array == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(messages_array);
        return NULL;
    }

    cJSON *system_message = cJSON_CreateObject();
    cJSON_AddStringToObject(system_message, "role", "system");
    cJSON_AddStringToObject(system_message, "content",
                            "You are a voice assistant running on embedded hardware. "
                            "Reply in concise spoken Chinese plain text. "
                            "Do not use Markdown, code blocks, URLs, or bullet formatting.");
    cJSON_AddItemToArray(messages_array, system_message);

    for (int i = 0; i < chat_history_length; i++) {
        cJSON *message_item = cJSON_CreateObject();
        cJSON_AddStringToObject(message_item, "role", "user");
        cJSON_AddStringToObject(message_item, "content", chat_history[i]);
        cJSON_AddItemToArray(messages_array, message_item);
    }

    cJSON *user_message = cJSON_CreateObject();
    cJSON_AddStringToObject(user_message, "role", "user");
    cJSON_AddStringToObject(user_message, "content", prompt);
    cJSON_AddItemToArray(messages_array, user_message);

    cJSON_AddStringToObject(root, "model", model);
    cJSON_AddItemToObject(root, "messages", messages_array);
    cJSON_AddNumberToObject(root, "temperature", 0.7);

    char *request_params = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (request_params == NULL) {
        ESP_LOGE(TAG, "failed to build chat request JSON");
        return NULL;
    }

    ESP_LOGI(TAG, "Chat request model=%s, prompt=%s", model, prompt);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_client_event_handler1,
        .user_data = response_data,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 30000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "chat client init failed");
        free(request_params);
        return NULL;
    }

    char auth_header[320];
    if (strstr(effective_api_key, "Bearer ") == effective_api_key) {
        strlcpy(auth_header, effective_api_key, sizeof(auth_header));
    } else {
        snprintf(auth_header, sizeof(auth_header), "Bearer %s", effective_api_key);
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, request_params, strlen(request_params));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Chat HTTP status=%d, response=%s", status_code, response_data);

        cJSON *json = cJSON_Parse(response_data);
        if (json != NULL) {
            cJSON *choices_array = cJSON_GetObjectItem(json, "choices");
            if (choices_array != NULL && cJSON_IsArray(choices_array) && cJSON_GetArraySize(choices_array) > 0) {
                cJSON *message_obj = cJSON_GetObjectItem(cJSON_GetArrayItem(choices_array, 0), "message");
                cJSON *content_obj = message_obj ? cJSON_GetObjectItem(message_obj, "content") : NULL;
                if (content_obj != NULL && cJSON_IsString(content_obj)) {
                    answer = strdup(content_obj->valuestring);
                    chat_history_add(prompt);
                }
            }
            if (answer == NULL) {
                cJSON *error_obj = cJSON_GetObjectItem(json, "error");
                ESP_LOGE(TAG, "Chat response missing answer: %s",
                         error_obj ? cJSON_PrintUnformatted(error_obj) : "unknown");
            }
            cJSON_Delete(json);
        } else {
            ESP_LOGE(TAG, "Chat JSON parse failed");
        }
    } else {
        ESP_LOGE(TAG, "Chat HTTP POST failed: %s", esp_err_to_name(err));
    }

    free(request_params);
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "Chat answer: %s", answer ? answer : "(null)");
    return answer;
}
