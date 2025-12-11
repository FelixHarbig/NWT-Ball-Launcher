#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "driver/ledc.h"
#include <stdbool.h>

/*
Servo sweep for MG90S using ESP32 LEDC PWM (15-bit resolution).

- Frequency: 50 Hz → period = 20 ms
- Pulse width range: 0.5 ms → 2.5 ms (full mechanical range ±90°)
- 15-bit resolution: 0–32767
  - duty_min = (0.5 / 20) * 32767 ≈ 819
  - duty_max = (2.5 / 20) * 32767 ≈ 4095

- Duty steps: 10 per iteration
- Iteration delay: 20 ms per step
  → Smooth back-and-forth sweep

Logic:
- Start at duty_min (full left)
- Increment duty by `step` until duty_max (full right)
- Reverse direction and decrement back to duty_min
- Repeat indefinitely

This configuration ensures proper pulse widths for MG90S without overshooting, stalling, or erratic movement.
*/


void servoRotate_task(void *args) {
    const int duty_min  =  1802;      // ~500 µs (full left)
    const int duty_max  = 3112;     // saturated ~2500 µs (full right)
    int       duty      = duty_min;
    const int step      =   10;      // adjust for smoothness
    const int iter_ms   =   10;      // delay per step
    bool      increasing = true;
    int stop_debug = 1;

    ledc_timer_config_t timer_conf = {
        .duty_resolution = LEDC_TIMER_15_BIT,
        .freq_hz         = 50,
        .speed_mode      = LEDC_HIGH_SPEED_MODE,
        .timer_num       = LEDC_TIMER_0,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t channel_conf = {
        .channel    = LEDC_CHANNEL_0,
        .duty       = duty,
        .gpio_num   = 12, // 25 works with esp32 12 16 13 15 14 2
        .intr_type  = LEDC_INTR_DISABLE,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .timer_sel  = LEDC_TIMER_0,
    };
    ledc_channel_config(&channel_conf);

    while (true) {

        for(int i = 0; stop_debug < (duty_max - duty_min)*0.1; i++) {
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, duty);
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
        printf("Duty = %d\n", duty);
        printf("i = %d\n", i);


        if (increasing) {
            duty += step;
            if (duty >= duty_max) {
                duty = duty_max;
                increasing = false;
            }
        } else {
            duty -= step;
            if (duty <= duty_min) {
                duty = duty_min;
                increasing = true;
            }
        }

        vTaskDelay(iter_ms / portTICK_PERIOD_MS);
    }
    for(int duty = duty_min; duty < 2200; duty += 10){
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, duty);
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
        vTaskDelay(iter_ms / portTICK_PERIOD_MS);
        printf("Duty = %d\n", duty);
    }


    

    while(true) {
    duty = 2600;
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
    vTaskDelay(iter_ms / portTICK_PERIOD_MS);
    printf("Duty = %d\n", duty);
    }

    }
}

void app_main() {
    xTaskCreate(servoRotate_task, "servoRotate_task", 2048, NULL, 5, NULL);
}
