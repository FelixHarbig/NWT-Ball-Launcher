/**
 * @file config.h
 * @brief Pin definitions and constants for ESP32 Car Control System
 */

#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// WIFI CONFIGURATION (Access Point Mode)
// ============================================================================
#define AP_SSID "ESP32-Car"
#define AP_PASSWORD "12345678"
#define AP_CHANNEL 1
#define AP_MAX_CONNECTIONS 1
#define WEBSOCKET_PORT 80

// ============================================================================
// MOTOR PINS (Tank Drive)
// ============================================================================
// Placeholder for motor driver - designed to be easily swapped for L298N, L293D, BTS7960, etc.
#define LEFT_MOTOR_IN1   32  // Left motor direction pin 1
#define LEFT_MOTOR_IN2   33  // Left motor direction pin 2
#define LEFT_MOTOR_PWM   25  // Left motor speed (PWM)

#define RIGHT_MOTOR_IN1  26  // Right motor direction pin 1
#define RIGHT_MOTOR_IN2  27  // Right motor direction pin 2
#define RIGHT_MOTOR_PWM  14  // Right motor speed (PWM)

// ============================================================================
// ULTRASONIC SENSOR PINS
// ============================================================================
// HC-SR04 sensors: Middle, Slight Right, Slight Left
#define US_TRIG_MIDDLE   5
#define US_ECHO_MIDDLE   18

#define US_TRIG_RIGHT    16
#define US_ECHO_RIGHT    17

#define US_TRIG_LEFT     4
#define US_ECHO_LEFT     19

// ============================================================================
// STATUS LED
// ============================================================================
#define STATUS_LED       2   // Built-in LED

// ============================================================================
// MOTOR CONFIGURATION
// ============================================================================
#define PWM_FREQUENCY    1000    // PWM frequency in Hz
#define PWM_RESOLUTION   8       // PWM resolution (8-bit = 0-255)

// Motor speed limits (-100 to 100)
#define MAX_MOTOR_SPEED  100
#define MIN_MOTOR_SPEED  -100

// ============================================================================
// SPEED SETTINGS
// ============================================================================
#define TRACKING_SPEED   50      // 50% PWM for tracking/following
#define SEARCH_SPEED     30      // 30% PWM for search mode

// ============================================================================
// OBSTACLE AVOIDANCE THRESHOLDS (in centimeters)
// ============================================================================
#define OBSTACLE_CLOSE   30      // Distance to stop/back away
#define OBSTACLE_WARNING 50      // Distance to turn away

// ============================================================================
// TRACKING PARAMETERS
// ============================================================================
#define CENTER_TOLERANCE 30      // Pixel tolerance for centering (dx threshold)

// ============================================================================
// TIMEOUT SETTINGS (in milliseconds)
// ============================================================================
#define COMMAND_TIMEOUT  2000    // 2 second timeout before switching to search mode
#define STATUS_INTERVAL  500     // Send status every 500ms

// ============================================================================
// ULTRASONIC SENSOR CONFIGURATION
// ============================================================================
#define US_TIMEOUT       25000   // Microseconds timeout for ultrasonic (max ~4m)
#define US_SAMPLES       3       // Number of samples to average

// ============================================================================
// SEARCH BEHAVIOR
// ============================================================================
#define SEARCH_TURN_DURATION 1500  // Duration of turn in milliseconds
#define SEARCH_MOVE_DURATION 2000  // Duration of forward movement in milliseconds

#endif // CONFIG_H
