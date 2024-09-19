#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>

#define WIFI_SSID "ChinaNet-sCmY"
#define WIFI_PASS "u3pqz3d2"
#define OLLAMA_URL "http://192.168.71.76:11434/api/chat"
#define MODEL_NAME "qwen:7b"
static const char *TAG = "HTTP_EXAMPLE";
static const char *context = "You are talking to a virtual assistant designed to help";

// WiFi event handler
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    }
}

void wifi_init_sta(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {
        .sta =
            {
                .ssid = WIFI_SSID,
                .password = WIFI_PASS,
            },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}

#define MAX_HTTP_OUTPUT_BUFFER 2048

// 模拟的音频数据
char audio[] = {0x12, 0x34, 0x56, 0x78, 0x90}; // 假设的音频数据
size_t audio_len = sizeof(audio);                 // 音频数据长度

// HTTP 事件处理函数
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
void send_audio_data() {
    esp_http_client_config_t config = {
        .url = "http://192.168.71.76:5001/upload",
        .event_handler = _http_event_handler,
        .timeout_ms = 10000 // 设置超时为 10 秒
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // 设置 POST 请求和发送数据
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream"); // 设置为二进制传输，否则上位机无法获取二进制数据
    esp_http_client_set_post_field(client, (const char *)audio, audio_len);

    // 验证 POST 数据是否正确
    // validate_post_data(client);

    // 执行请求
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
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

void task_http_request(void *pvParameters) {
    // 发送音频数据到服务器
    send_audio_data();

    // 等待服务器返回第一个字符串（超时10秒）
    wait_for_response("http://192.168.71.76:5001/get_response", 10000);

    // 等待服务器返回第二个长字符串（超时20秒）
    wait_for_response("http://192.168.71.76:5001/get_response2", 20000);

    // 等待服务器返回WAV文件（超时30秒）
    // wait_for_response("http://192.168.71.76:5001/get_wav", 30000);

    vTaskDelete(NULL); // 删除任务
}

void app_main(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize WiFi
    wifi_init_sta();

    // Wait for WiFi to connect (you can implement event-based notification for better handling)
    vTaskDelay(10000 / portTICK_PERIOD_MS); // Wait for WiFi connection

    // 创建一个HTTP请求的任务
    xTaskCreate(&task_http_request, "http_request_task", 8192, NULL, 5, NULL);
}
