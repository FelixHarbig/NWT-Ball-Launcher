// main.cpp - Camera ESP (The "Eye")
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "driver/ledc.h"

#include "app_camera.h"
#include "person_detection.h"
#include "sd_utils.h"
#include "api_client.h" // New helper for Main ESP comms

static const char *TAG = "CAM_MAIN";

// --- Configuration ---
constexpr int CROP_SIZE = 288;
constexpr char SD_PGM_PATH[] = "/sdcard/test.pgm";

// Servo Configuration (Pin 16)
#define SERVO_PIN 16
#define SERVO_CHANNEL LEDC_CHANNEL_1
#define SERVO_TIMER LEDC_TIMER_1
#define SERVO_MODE LEDC_HIGH_SPEED_MODE

// Servo Angles (Duty Cycles for 50Hz 15-bit)
// 0.5ms (~820) to 2.5ms (~4100)
// Adjust these based on your mechanism
#define SERVO_REST_DUTY 1638 // ~1.0ms
#define SERVO_FIRE_DUTY 3276 // ~2.0ms 

// --- Wi-Fi Station Setup ---
static void wifi_init_sta()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.sta.ssid, "ESP32-AP");
    strcpy((char*)wifi_config.sta.password, "12345678");
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
    
    // Wait for connection (simple delay for prototype, use event group in prod)
    ESP_LOGI(TAG, "Connecting to Main ESP Wi-Fi...");
    vTaskDelay(pdMS_TO_TICKS(5000));
}

// --- Servo Control ---
static void servo_init() {
    ledc_timer_config_t timer_conf = {
        .speed_mode = SERVO_MODE,
        .duty_resolution = LEDC_TIMER_15_BIT,
        .timer_num = SERVO_TIMER,
        .freq_hz = 50,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t channel_conf = {
        .gpio_num = SERVO_PIN,
        .speed_mode = SERVO_MODE,
        .channel = SERVO_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = SERVO_TIMER,
        .duty = SERVO_REST_DUTY,
        .hpoint = 0
    };
    ledc_channel_config(&channel_conf);
}

static void servo_fire() {
    ESP_LOGI(TAG, "FIRING_SERVO!");
    // Move to Fire position
    ledc_set_duty(SERVO_MODE, SERVO_CHANNEL, SERVO_FIRE_DUTY);
    ledc_update_duty(SERVO_MODE, SERVO_CHANNEL);
    vTaskDelay(pdMS_TO_TICKS(500)); 
    
    // Return to Rest
    ledc_set_duty(SERVO_MODE, SERVO_CHANNEL, SERVO_REST_DUTY);
    ledc_update_duty(SERVO_MODE, SERVO_CHANNEL);
    vTaskDelay(pdMS_TO_TICKS(500));
}

// --- Main App ---
extern "C" void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    
    // 1. Init Wi-Fi (Client)
    wifi_init_sta();

    // 2. Init Camera
    if (app_camera_init() != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed");
        return;
    }

    // 3. Init SD Card
    if (sd_mount_card() != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed");
        return;
    }

    // 4. Init Detection
    if (person_detection_init() != ESP_OK) {
        ESP_LOGE(TAG, "Person detection init failed");
        return;
    }

    // 5. Init Servo
    servo_init();

    ESP_LOGI(TAG, "System Initialized. Starting Detection Loop...");

    // Main Loop
    while (true) {
        // A. Capture Frame
        camera_fb_t* fb = app_camera_get_frame();
        if (!fb) {
            ESP_LOGW(TAG, "Capture failed");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // B. Save to SD (Required for current detection lib)
        if (sd_save_center_crop_pgm(fb, CROP_SIZE, SD_PGM_PATH) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to save PGM");
            app_camera_return_frame(fb);
            continue;
        }
        app_camera_return_frame(fb); // Free memory asap

        // C. Detect
        detection_result_t result;
        bool found = detect_person_from_sd(SD_PGM_PATH, &result);

        if (found) {
            // Person detected in global 3x3 grid
            // Grid layout:
            // 0,0 | 0,1 | 0,2
            // 1,0 | 1,1 | 1,2 (Center)
            // 2,0 | 2,1 | 2,2
            
            ESP_LOGI(TAG, "Target Found at [%d, %d]", result.row, result.col);

            if (result.row == 1 && result.col == 1) {
                // --- TARGET CENTERED ---
                ESP_LOGI(TAG, "Target Centered! Checking ball status...");
                
                if (api_check_ball_status()) {
                    ESP_LOGW(TAG, "Ball Ready -> SHOOTING!");
                    servo_fire();
                    // Cooldown
                    vTaskDelay(pdMS_TO_TICKS(2000));
                } else {
                    ESP_LOGI(TAG, "No Ball ready. Waiting...");
                    vTaskDelay(pdMS_TO_TICKS(500));
                }

            } else {
                // --- TARGET OFF-CENTER ---
                // Calculate move direction
                // If col < 1 (Left) -> Move Stepper Neg
                // If col > 1 (Right) -> Move Stepper Pos
                // (Row checks could drive tank forward/back if desired)
                
                int steps = 0;
                if (result.col < 1) steps = -50; // Move Left
                else if (result.col > 1) steps = 50;  // Move Right
                
                if (steps != 0) {
                    ESP_LOGI(TAG, "Adjusting Aim: %d steps", steps);
                    api_send_stepper_command(steps);
                    // Wait for move to likely complete
                    vTaskDelay(pdMS_TO_TICKS(500));
                }
            }
        } else {
            // --- NO TARGET ---
            // Optional: Roam or Scan
            // For now, just wait
            ESP_LOGI(TAG, "No target.");
            vTaskDelay(pdMS_TO_TICKS(100)); // Scan interval
        }
    }
}

