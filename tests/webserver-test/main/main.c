#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "driver/gpio.h"

static const char *TAG = "WIFI_SERVER";


// -----------------------------------------------------------
//  Helper: extract "<something>" from "/pin/<something>"
// -----------------------------------------------------------
static const char* get_subpath(const char *uri, const char *prefix) {
    size_t len = strlen(prefix);
    if (strncmp(uri, prefix, len) != 0) return NULL;

    if (uri[len] == 0) return NULL;  // nothing after /pin
    return uri + len;                // return the number part
}

/* ---------------- MOTOR QUEUE ---------------- */
static QueueHandle_t motor_queue;

/* Messages sent to motor task */
typedef struct {
    char action[32];
} motor_cmd_t;

/* Motor task (non-blocking HTTP) */
static void motor_task(void *arg)
{
    motor_cmd_t cmd;

    while (true) {
        if (xQueueReceive(motor_queue, &cmd, portMAX_DELAY)) {
            ESP_LOGI(TAG, "Motor action received: %s", cmd.action);

            // Replace this with your real motor control
            vTaskDelay(pdMS_TO_TICKS(200));

            ESP_LOGI(TAG, "Motor action done");
        }
    }
}

/* ---------------- GPIO INIT ---------------- */
static void configure_gpio()
{
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
        .pin_bit_mask = (1ULL << GPIO_NUM_4)
    };
    gpio_config(&io_conf);
}


// -----------------------------------------------------------
//  Handle GET /pin/<number>
// -----------------------------------------------------------
static esp_err_t get_pin_handler_1(httpd_req_t *req)
{
    

    int pin = 1;
    if (pin <= 0 || pin > 40) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid pin");
        return ESP_FAIL;
    }

    gpio_set_direction(pin, GPIO_MODE_INPUT);
    int level = gpio_get_level(pin);

    char resp[64];
    snprintf(resp, sizeof(resp), "{\"pin\":%d,\"value\":%d}", pin, level);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}


// -----------------------------------------------------------
//  Handle POST /action/<name>
// -----------------------------------------------------------
static esp_err_t post_action_handler(httpd_req_t *req)
{
    const char *sub = get_subpath(req->uri, "/action/");
    if (!sub) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Action name required");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Action triggered: %s", sub);

    // placeholder: simulate motor movement
    vTaskDelay(100 / portTICK_PERIOD_MS);

    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}


/* ---------------- WEB SERVER ---------------- */
static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {

        httpd_uri_t pin_route = {
            .uri       = "/pin_1",
            .method    = HTTP_GET,
            .handler   = get_pin_handler_1,
            .user_ctx  = NULL,
        };
        httpd_register_uri_handler(server, &pin_route);

        httpd_uri_t action_route = {
            .uri       = "/action/*",
            .method    = HTTP_POST,
            .handler   = post_action_handler,
        };
        httpd_register_uri_handler(server, &action_route);
    }

    return server;
}

/* ---------------- SOFT AP ---------------- */
static void wifi_init_softap()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "ESP32-AP",
            .password = "12345678",
            .ssid_len = 0,
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        }
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/* ---------------- MAIN ---------------- */
void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    configure_gpio();

    motor_queue = xQueueCreate(10, sizeof(motor_cmd_t));
    xTaskCreate(motor_task, "motor_task", 4096, NULL, 5, NULL);

    wifi_init_softap();
    start_webserver();

    ESP_LOGI(TAG, "Async-style server ready.");
}
