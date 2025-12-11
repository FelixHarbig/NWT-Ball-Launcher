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
#include "esp32/rom/ets_sys.h" // for microsecond delays if needed

static const char *TAG = "MAIN_ESP";

// --- Hardware Pins Configuration ---
// Stepper Motor (ULN2003 Driver assumed)
#define STEPPER_PIN_1 12
#define STEPPER_PIN_2 13
#define STEPPER_PIN_3 14
#define STEPPER_PIN_4 16 // Validated from tests/stepper-motor

// Ball Sensor
#define BALL_SENSOR_PIN 4 // Reused from pin_1 test

// DC Motor Placeholders
#define MOTOR_LEFT_PIN_A  26 
#define MOTOR_LEFT_PIN_B  27
#define MOTOR_RIGHT_PIN_A 25
#define MOTOR_RIGHT_PIN_B 33


// --- Global State ---
static QueueHandle_t stepper_queue;
static QueueHandle_t tank_queue;

typedef struct {
    int steps;    // positive = cw, negative = ccw
} stepper_cmd_t;

typedef struct {
    int left_speed;
    int right_speed;
} tank_cmd_t;


/* -----------------------------------------------------------
   Stepper Motor Task
   Based on tests/stepper-motor/main/main.c logic
   but adapted to be non-blocking with a queue.
----------------------------------------------------------- */
static void stepper_task(void *arg)
{
    // Initialize Pins
    gpio_set_direction((gpio_num_t)STEPPER_PIN_1, GPIO_MODE_OUTPUT);
    gpio_set_direction((gpio_num_t)STEPPER_PIN_2, GPIO_MODE_OUTPUT);
    gpio_set_direction((gpio_num_t)STEPPER_PIN_3, GPIO_MODE_OUTPUT);
    gpio_set_direction((gpio_num_t)STEPPER_PIN_4, GPIO_MODE_OUTPUT);

    stepper_cmd_t cmd;
    
    // Simple 4-step sequence (adapt if 8-step is needed)
    // 1-2-3-4
    // Note: Half-stepping is smoother, but full stepping is simpler.
    // Let's use a simple full step sequence for now as per common libraries.
    // bitmask pattern for coils: 1, 2, 4, 8
    
    /* 
       The test code used:
         gpio_set_level(GPIO_NUM_16,0); ets_delay...
         This implies simple toggling. 
         But a stepper needs a sequence. 
         I will implement a standard 4-phase sequence.
    */
    
    int step_idx = 0;
    // 4-step sequence (AB-BC-CD-DA double coil excitation for torque)
    const uint8_t steps_lookup[4] = {0b1100, 0b0110, 0b0011, 0b1001}; 
    // const uint8_t steps_lookup[4] = {0b1000, 0b0100, 0b0010, 0b0001}; // Wave drive

    while (true) {
        if (xQueueReceive(stepper_queue, &cmd, portMAX_DELAY)) {
            ESP_LOGI(TAG, "Stepper move: %d", cmd.steps);
            
            int count = abs(cmd.steps);
            int dir = (cmd.steps > 0) ? 1 : -1;
            
            for (int i = 0; i < count; i++) {
                step_idx += dir;
                if (step_idx < 0) step_idx = 3;
                if (step_idx > 3) step_idx = 0;
                
                uint8_t mask = steps_lookup[step_idx];
                
                gpio_set_level((gpio_num_t)STEPPER_PIN_1, (mask & 0b1000) ? 1 : 0);
                gpio_set_level((gpio_num_t)STEPPER_PIN_2, (mask & 0b0100) ? 1 : 0);
                gpio_set_level((gpio_num_t)STEPPER_PIN_3, (mask & 0b0010) ? 1 : 0);
                gpio_set_level((gpio_num_t)STEPPER_PIN_4, (mask & 0b0001) ? 1 : 0);
                
                vTaskDelay(pdMS_TO_TICKS(5)); // Speed control
            }
            
            // Turn off coils to save power/reduce heat
            gpio_set_level((gpio_num_t)STEPPER_PIN_1, 0);
            gpio_set_level((gpio_num_t)STEPPER_PIN_2, 0);
            gpio_set_level((gpio_num_t)STEPPER_PIN_3, 0);
            gpio_set_level((gpio_num_t)STEPPER_PIN_4, 0);
            
            ESP_LOGI(TAG, "Stepper done");
        }
    }
}

/* -----------------------------------------------------------
   DC Motor / Tank Task (Placeholder)
----------------------------------------------------------- */
static void tank_task(void *arg)
{
    // Pins Setup (Placeholder)
    gpio_set_direction((gpio_num_t)MOTOR_LEFT_PIN_A, GPIO_MODE_OUTPUT);
    gpio_set_direction((gpio_num_t)MOTOR_LEFT_PIN_B, GPIO_MODE_OUTPUT);
    // ... others

    tank_cmd_t cmd;
    while(true) {
        if (xQueueReceive(tank_queue, &cmd, portMAX_DELAY)) {
            ESP_LOGI(TAG, "Tank Move: L=%d R=%d", cmd.left_speed, cmd.right_speed);
            // Implement PWM or simple High/Low here
            vTaskDelay(pdMS_TO_TICKS(100)); 
        }
    }
}


/* -----------------------------------------------------------
   Web Server Handlers
----------------------------------------------------------- */

// GET /sensor/ball
// Returns {"ball_ready": true/false}
static esp_err_t get_ball_sensor_handler(httpd_req_t *req)
{
    // Assuming Active LOW or HIGH? Let's assume Active LOW (ball present = 0) 
    // or HIGH depending on sensor. Let's send raw value first.
    int level = gpio_get_level((gpio_num_t)BALL_SENSOR_PIN);
    // If standard IR sensor, often LOW means detection.
    
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"ball_detected\": %s}", level == 0 ? "true" : "false");
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

// POST /control/stepper
// Body: {"steps": 100} or {"steps": -50}
static esp_err_t post_stepper_handler(httpd_req_t *req)
{
    char content[100];
    int len = req->content_len;
    if (len >= sizeof(content)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Content too long");
        return ESP_FAIL;
    }
    
    int ret = httpd_req_recv(req, content, len);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Recv fail");
        return ESP_FAIL;
    }
    content[len] = '\0';

    // Very simple "JSON" parsing (manual scan to avoid large libraries dependencies if not needed)
    // Looking for "steps": <int>
    int steps = 0;
    char *p = strstr(content, "\"steps\"");
    if (p) {
        // finding the number
        while (*p && (*p < '0' || *p > '9') && *p != '-') p++;
        if (*p) steps = atoi(p);
    } else {
         httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing steps params");
         return ESP_FAIL;       
    }

    stepper_cmd_t cmd = {.steps = steps};
    xQueueSend(stepper_queue, &cmd, 0);

    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

// POST /control/tank
// Body: {"left": 100, "right": 100}
static esp_err_t post_tank_handler(httpd_req_t *req)
{
    // Placeholder parsing
    tank_cmd_t cmd = {50, 50}; // dummy defaults
    xQueueSend(tank_queue, &cmd, 0);
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

// API Routes (inspired by pin_1)
static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // Increase stack size if needed needed
    config.stack_size = 8192; 

    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        
        httpd_uri_t ball_route = {
            .uri       = "/sensor/ball",
            .method    = HTTP_GET,
            .handler   = get_ball_sensor_handler,
            .user_ctx  = NULL,
        };
        httpd_register_uri_handler(server, &ball_route);

        httpd_uri_t stepper_route = {
            .uri       = "/control/stepper",
            .method    = HTTP_POST,
            .handler   = post_stepper_handler,
            .user_ctx  = NULL,
        };
        httpd_register_uri_handler(server, &stepper_route);

        httpd_uri_t tank_route = {
            .uri       = "/control/tank",
            .method    = HTTP_POST,
            .handler   = post_tank_handler,
            .user_ctx  = NULL,
        };
        httpd_register_uri_handler(server, &tank_route);
    }
    return server;
}

/* ---------------- SoftAP & Main ---------------- */
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
    if (strlen((char *)wifi_config.ap.password) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    
    // Init Ball Sensor
    gpio_set_direction((gpio_num_t)BALL_SENSOR_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)BALL_SENSOR_PIN, GPIO_PULLUP_ONLY);

    stepper_queue = xQueueCreate(10, sizeof(stepper_cmd_t));
    tank_queue = xQueueCreate(10, sizeof(tank_cmd_t));

    xTaskCreate(stepper_task, "stepper_task", 2048, NULL, 5, NULL);
    xTaskCreate(tank_task, "tank_task", 2048, NULL, 5, NULL);

    wifi_init_softap();
    start_webserver();

    ESP_LOGI(TAG, "Main ESP32 Ready. AP: ESP32-AP");
}

