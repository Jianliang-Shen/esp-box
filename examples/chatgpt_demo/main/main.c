/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_check.h"
#include "nvs_flash.h"
#include "app_ui_ctrl.h"
#include "audio_player.h"
#include "app_sr.h"
#include "bsp/esp-bsp.h"
#include "bsp_board.h"
#include "app_audio.h"
#include "app_wifi.h"
#include "settings.h"

#include "esp_event.h"
#include "esp_http_client.h"

#define SCROLL_START_DELAY_S            (1.5)
#define LISTEN_SPEAK_PANEL_DELAY_MS     2000
#define SERVER_ERROR                    "server_error"
#define INVALID_REQUEST_ERROR           "invalid_request_error"
#define SORRY_CANNOT_UNDERSTAND         "Sorry, I can't understand."
#define API_KEY_NOT_VALID               "API Key is not valid"

static char *TAG = "app_main";
static sys_param_t *sys_param = NULL;

#define MAX_HTTP_OUTPUT_BUFFER (1024)
#define CHUNK_SIZE 10240 // 每次上传的音频块大小
static char http_response[MAX_HTTP_OUTPUT_BUFFER];

esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    static char buffer[MAX_HTTP_OUTPUT_BUFFER];
    static int buffer_pos = 0;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (!esp_http_client_is_chunked_response(evt->client)) {
            // 累积数据
            if (buffer_pos + evt->data_len < MAX_HTTP_OUTPUT_BUFFER) {
                memcpy(buffer + buffer_pos, evt->data, evt->data_len);
                buffer_pos += evt->data_len;
            } else {
                ESP_LOGE(TAG, "Buffer overflow");
            }
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        // 请求完成时处理接收到的所有数据
        buffer[buffer_pos] = '\0'; // 结束字符串
        ESP_LOGI(TAG, "Full response: %s", buffer);

        // 将完整的响应复制到全局变量
        strncpy(http_response, buffer, MAX_HTTP_OUTPUT_BUFFER - 1);
        http_response[MAX_HTTP_OUTPUT_BUFFER - 1] = '\0'; // 确保以null结尾

        buffer_pos = 0; // 重置 buffer
        break;
    default:
        break;
    }
    return ESP_OK;
}

// 验证 POST 数据是否正确设置
void validate_post_data(esp_http_client_handle_t client) {
    char *post_data = NULL;
    int post_len = esp_http_client_get_post_field(client, &post_data);

    if (post_data != NULL && post_len > 0) {
        ESP_LOGI(TAG, "POST data length: %d", post_len);
        ESP_LOGI(TAG, "POST data content: ");
        for (int i = 0; i < post_len; i++) {
            printf("%02X ", (unsigned char)post_data[i]); // 打印每个字节
        }
        printf("\n");
    } else {
        ESP_LOGE(TAG, "No POST data found or invalid length.");
    }
}

// 发送音频数据到服务器
void send_audio_data(uint8_t *audio, int audio_len) {
    esp_http_client_config_t config = {
        .url = "http://192.168.71.80:5000/upload",
        .event_handler = _http_event_handler,
        .timeout_ms = 10000 // 设置超时为 10 秒
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    ESP_LOGI(TAG, "Total length: %d", audio_len);

    size_t sent_len = 0;
    while (sent_len < audio_len) {
        size_t chunk_len = (audio_len - sent_len > CHUNK_SIZE) ? CHUNK_SIZE : (audio_len - sent_len);

        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
        esp_http_client_set_post_field(client, (const char *)(audio + sent_len), chunk_len);

        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Chunk uploaded successfully, length: %d", chunk_len);
            sent_len += chunk_len;
        } else {
            ESP_LOGE(TAG, "Error in uploading chunk: %s", esp_err_to_name(err));
            break;
        }
    }

    esp_http_client_cleanup(client);
}

// 等待服务器的响应，带有自定义超时
void wait_for_response(const char *url, int timeout_ms) {
    esp_http_client_config_t config = {.url = url, .event_handler = _http_event_handler, .timeout_ms = timeout_ms};
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // 执行请求
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

/* program flow. This function is called in app_audio.c */
esp_err_t start_answer(uint8_t *audio, int audio_len)
{
    esp_err_t ret = ESP_OK;

    // ui_ctrl_show_panel(UI_CTRL_PANEL_GET, 0);

    // // UI listen success
    // ui_ctrl_label_show_text(UI_CTRL_LABEL_REPLY_QUESTION, "介绍一下你自己");
    // // ui_ctrl_label_show_text(UI_CTRL_LABEL_LISTEN_SPEAK, "Answer");

    // ui_ctrl_label_show_text(UI_CTRL_LABEL_REPLY_CONTENT, "如果您不记得某个特定的国际规则、国际贸易实践或者是其他细节，通常可以通过以下步骤来获取信息：1. **回顾相关领域**：如果该信息与你的日常生活或者专业领域相关，你可能曾在学习或工作中接触过。2. **查找权威资料**：例如教科书、学术论文、官方文件等。这些地方通常会包含较为全面和准确的国际 规则信息。3. **咨询专业人士**：如果你是学生或者在职的专业人士，可以向你的老师、导师或者同行请教。请根据具体情境选择合适的方法获取所需信息。");
    // ui_ctrl_show_panel(UI_CTRL_PANEL_REPLY, 0);

    // 发送音频数据到服务器
    send_audio_data(audio, audio_len);

    wait_for_response("http://192.168.71.80:5000/get_response", 10000);
    ui_ctrl_label_show_text(UI_CTRL_LABEL_REPLY_QUESTION, http_response);

    wait_for_response("http://192.168.71.80:5000/get_response2", 20000);
    ui_ctrl_label_show_text(UI_CTRL_LABEL_REPLY_CONTENT, http_response);
    ui_ctrl_show_panel(UI_CTRL_PANEL_REPLY, 0);

    return ret;
}

/* play audio function */

static void audio_play_finish_cb(void)
{
    ESP_LOGI(TAG, "replay audio end");
    if (ui_ctrl_reply_get_audio_start_flag()) {
        ui_ctrl_reply_set_audio_end_flag(true);
    }
}

void app_main()
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(settings_read_parameter_from_nvs());
    sys_param = settings_get_parameter();

    bsp_spiffs_mount();
    bsp_i2c_init();

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * CONFIG_BSP_LCD_DRAW_BUF_HEIGHT,
        .double_buffer = 0,
        .flags = {
            .buff_dma = true,
        }
    };
    bsp_display_start_with_config(&cfg);
    bsp_board_init();

    ESP_LOGI(TAG, "Display LVGL demo");
    bsp_display_backlight_on();
    ui_ctrl_init();
    app_network_start();

    ESP_LOGI(TAG, "speech recognition start");
    app_sr_start(false);
    audio_register_play_finish_cb(audio_play_finish_cb);

    while (true) {

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
