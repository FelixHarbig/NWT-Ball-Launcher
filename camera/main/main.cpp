// main.cpp
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "app_camera.h"         // you already have this header
#include "person_detection.h"   // you already have this header
#include "sd_utils.h"           // we provide sd_utils.cpp (header assumed present)
#include "motor_control.h"  
#include "web_server.h"     // your existing firing functions

static const char *TAG = "main";

// target crop size (square) so 3x3 cells are 96x96
constexpr int CROP_SIZE = 288;
constexpr char SD_PGM_PATH[] = "/sdcard/test.pgm";

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting app");

    // 1) Init camera
    esp_err_t err = app_camera_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: 0x%X", err);
        return;
    }

    // 2) Mount SD
    if (sd_mount_card() != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed");
        return;
    }
    
    FILE* f = fopen("/sdcard/test.txt", "w");
    if (!f) ESP_LOGE(TAG, "Cannot write test file");
    else { fprintf(f,"ok\n"); fclose(f); }

    FILE* fa = fopen("/sdcard/bubba.pgm", "wb");
    if (!fa) ESP_LOGE(TAG, "Cannot write test file2");
    else { fprintf(fa,"ok\n"); fclose(fa); }


    // 3) Init detector
    if (person_detection_init() != ESP_OK) {
        ESP_LOGE(TAG, "Person detection init failed");
        return;
    }

    if (start_web_server() != ESP_OK) {
        ESP_LOGE("main", "Web server failed to start");
        return;
    }

    // 4) Init motor_control if you have one
    // motor_control_init(); // assume this exists and handles readiness checks

    // 5) Main detection task (single task here)
    xTaskCreate([](void*) {
        detection_result_t result;
        while (true) {
            // Capture frame
            camera_fb_t* fb = app_camera_get_frame();
            if (!fb) {
                ESP_LOGW(TAG, "No frame");
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }

            // Save centered 288x288 PGM to SD (reads from fb->buf which is grayscale)
            if (sd_save_center_crop_pgm(fb, CROP_SIZE, SD_PGM_PATH) != ESP_OK) {
                ESP_LOGW(TAG, "Failed saving cropped PGM");
                app_camera_return_frame(fb);
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }

            // Return FB asap to free PSRAM
            app_camera_return_frame(fb);

            // Run detection sequence based on on-disk PGM
            bool should_fire = detect_person_from_sd(SD_PGM_PATH, &result);

            if (should_fire) {
                ESP_LOGI(TAG, "Target in center and confirmed. Fire sequence.");

                if (is_ready_to_fire()) {
                    // example firing flow, adapt to your motor_control/servo API
                    stepper_load(2); // existing function
                    servo_set_angle(SERVO_FIRE_ANGLE);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    servo_set_angle(SERVO_DEFAULT_ANGLE);
                    vTaskDelay(pdMS_TO_TICKS(5000)); // cooldown
                } else {
                    ESP_LOGW(TAG, "Not ready to fire");
                }
            } else {
                ESP_LOGI(TAG, "No confirmed target in center this cycle");
            }

            // small delay between capture cycles
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }, "detection_task", 16 * 1024, nullptr, 5, nullptr);

    ESP_LOGI(TAG, "Main initialized");
}
