#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include "esp_err.h"

// --- Configuration ---
// Note: Adjust these values to match your hardware setup.

// Servo configuration
#define SERVO_GPIO          2  // GPIO pin for the servo
#define SERVO_DEFAULT_ANGLE 10 // Angle in degrees when idle
#define SERVO_FIRE_ANGLE    170 // Angle in degrees when firing

// Stepper motor configuration (for a 4-wire stepper driver like ULN2003)
#define STEPPER_IN1_GPIO    12
#define STEPPER_IN2_GPIO    13
#define STEPPER_IN3_GPIO    14
#define STEPPER_IN4_GPIO    15
#define STEPS_PER_ROTATION  512 // Steps for one full rotation of the motor

// Grayscale sensor configuration
#define READY_SENSOR_GPIO   4 // GPIO for the digital "ready to fire" sensor

// --- Functions ---

/**
 * @brief Initialize motor control peripherals (servo, stepper, sensor)
 */
esp_err_t motor_control_init();

/**
 * @brief Set the servo to a specific angle
 * @param angle Angle in degrees (0-180)
 */
void servo_set_angle(float angle);

/**
 * @brief Load the firing mechanism by turning the stepper motor
 * @param rotations Number of full rotations to turn
 */
void stepper_load(int rotations);

/**
 * @brief Check if the firing mechanism is ready
 * @return true if the sensor indicates ready, false otherwise
 */
bool is_ready_to_fire();

#endif // MOTOR_CONTROL_H
