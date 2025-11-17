#include "motor_control.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp32/rom/ets_sys.h"
#include "esp_log.h"

static const char *TAG = "motor_control";

// Servo constants
#define SERVO_TIMER              LEDC_TIMER_0
#define SERVO_CHANNEL            LEDC_CHANNEL_0
#define SERVO_MODE               LEDC_HIGH_SPEED_MODE
#define SERVO_RESOLUTION         LEDC_TIMER_15_BIT // 15-bit resolution for fine control
#define SERVO_FREQ_HZ            50                // 50 Hz PWM frequency
#define SERVO_MIN_PULSE_US       500               // Minimum pulse width in microseconds
#define SERVO_MAX_PULSE_US       2500              // Maximum pulse width in microseconds
#define SERVO_MAX_DEGREE         180               // Maximum angle of the servo

static uint32_t angle_to_duty(float angle) {
    return (uint32_t)((SERVO_MIN_PULSE_US + (angle / SERVO_MAX_DEGREE) * (SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US)) / (1000000.0 / SERVO_FREQ_HZ) * (1 << SERVO_RESOLUTION));
}

esp_err_t motor_control_init() {
    // --- Servo Init ---
    ledc_timer_config_t timer_conf = {
        .speed_mode = SERVO_MODE,
        .duty_resolution = SERVO_RESOLUTION,
        .timer_num = SERVO_TIMER,
        .freq_hz = SERVO_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    ledc_channel_config_t channel_conf = {
        .gpio_num = SERVO_GPIO,
        .speed_mode = SERVO_MODE,
        .channel = SERVO_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = SERVO_TIMER,
        .duty = 0,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_conf));
    ESP_LOGI(TAG, "Servo initialized on GPIO %d", SERVO_GPIO);

    // --- Stepper Init ---
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << STEPPER_IN1_GPIO) | (1ULL << STEPPER_IN2_GPIO) | (1ULL << STEPPER_IN3_GPIO) | (1ULL << STEPPER_IN4_GPIO);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
    ESP_LOGI(TAG, "Stepper initialized on GPIOs %d, %d, %d, %d", STEPPER_IN1_GPIO, STEPPER_IN2_GPIO, STEPPER_IN3_GPIO, STEPPER_IN4_GPIO);

    // --- Sensor Init ---
    io_conf.pin_bit_mask = (1ULL << READY_SENSOR_GPIO);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE; // Assuming sensor pulls low when active
    gpio_config(&io_conf);
    ESP_LOGI(TAG, "Ready sensor initialized on GPIO %d", READY_SENSOR_GPIO);

    // Set initial servo position
    servo_set_angle(SERVO_DEFAULT_ANGLE);

    return ESP_OK;
}

void servo_set_angle(float angle) {
    if (angle < 0) angle = 0;
    if (angle > SERVO_MAX_DEGREE) angle = SERVO_MAX_DEGREE;

    uint32_t duty = angle_to_duty(angle);
    ledc_set_duty(SERVO_MODE, SERVO_CHANNEL, duty);
    ledc_update_duty(SERVO_MODE, SERVO_CHANNEL);
}

void stepper_load(int rotations) {
    ESP_LOGI(TAG, "Loading mechanism: %d rotations", rotations);
    const int step_delay_us = 2000; // Delay between steps
    const int total_steps = rotations * STEPS_PER_ROTATION;

    for (int i = 0; i < total_steps; i++) {
        switch (i % 4) {
            case 0:
                gpio_set_level((gpio_num_t)STEPPER_IN1_GPIO, 1);
                gpio_set_level((gpio_num_t)STEPPER_IN2_GPIO, 0);
                gpio_set_level((gpio_num_t)STEPPER_IN3_GPIO, 0);
                gpio_set_level((gpio_num_t)STEPPER_IN4_GPIO, 0);
                break;
            case 1:
                gpio_set_level((gpio_num_t)STEPPER_IN1_GPIO, 0);
                gpio_set_level((gpio_num_t)STEPPER_IN2_GPIO, 1);
                gpio_set_level((gpio_num_t)STEPPER_IN3_GPIO, 0);
                gpio_set_level((gpio_num_t)STEPPER_IN4_GPIO, 0);
                break;
            case 2:
                gpio_set_level((gpio_num_t)STEPPER_IN1_GPIO, 0);
                gpio_set_level((gpio_num_t)STEPPER_IN2_GPIO, 0);
                gpio_set_level((gpio_num_t)STEPPER_IN3_GPIO, 1);
                gpio_set_level((gpio_num_t)STEPPER_IN4_GPIO, 0);
                break;
            case 3:
                gpio_set_level((gpio_num_t)STEPPER_IN1_GPIO, 0);
                gpio_set_level((gpio_num_t)STEPPER_IN2_GPIO, 0);
                gpio_set_level((gpio_num_t)STEPPER_IN3_GPIO, 0);
                gpio_set_level((gpio_num_t)STEPPER_IN4_GPIO, 1);
                break;
        }
        ets_delay_us(step_delay_us);
    }
    // Power off stepper to save energy and prevent overheating
    gpio_set_level((gpio_num_t)STEPPER_IN1_GPIO, 0);
    gpio_set_level((gpio_num_t)STEPPER_IN2_GPIO, 0);
    gpio_set_level((gpio_num_t)STEPPER_IN3_GPIO, 0);
    gpio_set_level((gpio_num_t)STEPPER_IN4_GPIO, 0);
    ESP_LOGI(TAG, "Loading complete");
}

bool is_ready_to_fire() {
    // Assuming the sensor is active low (pulls the pin to ground when ready)
    return gpio_get_level((gpio_num_t)READY_SENSOR_GPIO) == 0;
}
