/**
 * @file esp32-car.ino
 * @brief ESP32 Car Control System - Main Arduino Sketch
 * 
 * Features:
 * - WiFi Access Point for Raspberry Pi connection
 * - WebSocket server for real-time communication
 * - Tank drive motor control
 * - 3x HC-SR04 ultrasonic sensors for obstacle detection
 * - State machine: tracking, searching, stopped, manual
 * - Obstacle avoidance behavior
 * - Search behavior (drive around)
 */

#include <WiFi.h>
#include <WebSocketServer.h>
#include <ArduinoJson.h>
#include "config.h"

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

// WebSocket server
WebSocketServer webSocketServer;
WiFiClient wsClient;

// Motor state
int8_t leftMotorSpeed = 0;
int8_t rightMotorSpeed = 0;

// Ultrasonic sensor distances (in cm)
int obstacleMiddle = 0;
int obstacleLeft = 0;
int obstacleRight = 0;

// Command tracking
unsigned long lastCommandTime = 0;
bool commandReceived = false;

// State machine
enum CarMode {
    MODE_STOPPED,
    MODE_TRACKING,
    MODE_SEARCHING,
    MODE_MANUAL,
    MODE_OBSTACLE_AVOIDANCE
};

CarMode currentMode = MODE_STOPPED;
CarMode previousMode = MODE_STOPPED;

// Search behavior
unsigned long lastSearchAction = 0;
bool searchTurn = false;  // false = forward, true = turn
int8_t searchDirection = 1;  // 1 = turn right, -1 = turn left

// Status LED blink
unsigned long lastLedBlink = 0;
bool ledState = false;

// ============================================================================
// MOTOR CONTROL
// ============================================================================

/**
 * @brief Initialize motor pins
 */
void initMotors() {
    // Left motor
    pinMode(LEFT_MOTOR_IN1, OUTPUT);
    pinMode(LEFT_MOTOR_IN2, OUTPUT);
    pinMode(LEFT_MOTOR_PWM, OUTPUT);
    
    // Right motor
    pinMode(RIGHT_MOTOR_IN1, OUTPUT);
    pinMode(RIGHT_MOTOR_IN2, OUTPUT);
    pinMode(RIGHT_MOTOR_PWM, OUTPUT);
    
    // Configure PWM
    ledcSetup(0, PWM_FREQUENCY, PWM_RESOLUTION);
    ledcSetup(1, PWM_FREQUENCY, PWM_RESOLUTION);
    ledcAttachPin(LEFT_MOTOR_PWM, 0);
    ledcAttachPin(RIGHT_MOTOR_PWM, 1);
    
    // Stop motors initially
    stopMotors();
}

/**
 * @brief Set left motor speed (-100 to 100)
 */
void setLeftMotor(int8_t speed) {
    speed = constrain(speed, MIN_MOTOR_SPEED, MAX_MOTOR_SPEED);
    
    if (speed > 0) {
        // Forward
        digitalWrite(LEFT_MOTOR_IN1, HIGH);
        digitalWrite(LEFT_MOTOR_IN2, LOW);
        ledcWrite(0, map(speed, 0, 100, 0, 255));
    } else if (speed < 0) {
        // Backward
        digitalWrite(LEFT_MOTOR_IN1, LOW);
        digitalWrite(LEFT_MOTOR_IN2, HIGH);
        ledcWrite(0, map(-speed, 0, 100, 0, 255));
    } else {
        // Stop
        digitalWrite(LEFT_MOTOR_IN1, LOW);
        digitalWrite(LEFT_MOTOR_IN2, LOW);
        ledcWrite(0, 0);
    }
}

/**
 * @brief Set right motor speed (-100 to 100)
 */
void setRightMotor(int8_t speed) {
    speed = constrain(speed, MIN_MOTOR_SPEED, MAX_MOTOR_SPEED);
    
    if (speed > 0) {
        // Forward
        digitalWrite(RIGHT_MOTOR_IN1, HIGH);
        digitalWrite(RIGHT_MOTOR_IN2, LOW);
        ledcWrite(1, map(speed, 0, 100, 0, 255));
    } else if (speed < 0) {
        // Backward
        digitalWrite(RIGHT_MOTOR_IN1, LOW);
        digitalWrite(RIGHT_MOTOR_IN2, HIGH);
        ledcWrite(1, map(-speed, 0, 100, 0, 255));
    } else {
        // Stop
        digitalWrite(RIGHT_MOTOR_IN1, LOW);
        digitalWrite(RIGHT_MOTOR_IN2, LOW);
        ledcWrite(1, 0);
    }
}

/**
 * @brief Set both motor speeds
 */
void setMotors(int8_t left, int8_t right) {
    leftMotorSpeed = left;
    rightMotorSpeed = right;
    setLeftMotor(left);
    setRightMotor(right);
}

/**
 * @brief Stop both motors
 */
void stopMotors() {
    setMotors(0, 0);
}

// ============================================================================
// ULTRASONIC SENSOR
// ============================================================================

/**
 * @brief Measure distance from HC-SR04 sensor
 * @param trigPin Trigger pin
 * @param echoPin Echo pin
 * @return Distance in cm, or 0 if timeout
 */
int measureDistance(uint8_t trigPin, uint8_t echoPin) {
    // Send trigger pulse
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    
    // Measure echo duration
    long duration = pulseIn(echoPin, HIGH, US_TIMEOUT);
    
    // Calculate distance (speed of sound: 343 m/s = 0.034 cm/us)
    // Distance = duration * 0.034 / 2 (round trip)
    int distance = duration * 0.034 / 2;
    
    return distance;
}

/**
 * @brief Read all three ultrasonic sensors (with averaging)
 */
void readSensors() {
    // Take multiple samples and average
    long midSum = 0, leftSum = 0, rightSum = 0;
    int validSamples = 0;
    
    for (int i = 0; i < US_SAMPLES; i++) {
        int mid = measureDistance(US_TRIG_MIDDLE, US_ECHO_MIDDLE);
        int left = measureDistance(US_TRIG_LEFT, US_ECHO_LEFT);
        int right = measureDistance(US_TRIG_RIGHT, US_ECHO_RIGHT);
        
        if (mid > 0 && mid < 400) {
            midSum += mid;
            validSamples++;
        }
        if (left > 0 && left < 400) {
            leftSum += left;
            validSamples++;
        }
        if (right > 0 && right < 400) {
            rightSum += right;
            validSamples++;
        }
        
        delay(10);  // Small delay between samples
    }
    
    obstacleMiddle = (midSum > 0) ? midSum / US_SAMPLES : 0;
    obstacleLeft = (leftSum > 0) ? leftSum / US_SAMPLES : 0;
    obstacleRight = (rightSum > 0) ? rightSum / US_SAMPLES : 0;
}

/**
 * @brief Initialize ultrasonic sensor pins
 */
void initSensors() {
    pinMode(US_TRIG_MIDDLE, OUTPUT);
    pinMode(US_ECHO_MIDDLE, INPUT);
    pinMode(US_TRIG_LEFT, OUTPUT);
    pinMode(US_ECHO_LEFT, INPUT);
    pinMode(US_TRIG_RIGHT, OUTPUT);
    pinMode(US_ECHO_RIGHT, INPUT);
}

// ============================================================================
// OBSTACLE AVOIDANCE
// ============================================================================

/**
 * @brief Check for obstacles and return true if action needed
 */
bool checkObstacles() {
    readSensors();
    
    // Check if any obstacle is too close
    bool obstacleAhead = (obstacleMiddle > 0 && obstacleMiddle < OBSTACLE_CLOSE);
    bool obstacleLeftClose = (obstacleLeft > 0 && obstacleLeft < OBSTACLE_CLOSE);
    bool obstacleRightClose = (obstacleRight > 0 && obstacleRight < OBSTACLE_CLOSE);
    
    if (obstacleAhead || obstacleLeftClose || obstacleRightClose) {
        return true;
    }
    
    return false;
}

/**
 * @brief Check for warning distance (turn away)
 */
bool checkWarningDistance() {
    readSensors();
    
    bool warningAhead = (obstacleMiddle > 0 && obstacleMiddle < OBSTACLE_WARNING);
    
    return warningAhead;
}

/**
 * @brief Perform obstacle avoidance action
 */
void performObstacleAvoidance() {
    readSensors();
    
    // Determine which way to go
    if (obstacleMiddle > 0 && obstacleMiddle < OBSTACLE_CLOSE) {
        // Too close - back away
        setMotors(-SEARCH_SPEED, -SEARCH_SPEED);
        delay(500);
    }
    
    // Turn away from obstacle
    if (obstacleLeft > obstacleRight) {
        // More space on left - turn right
        setMotors(SEARCH_SPEED, -SEARCH_SPEED);
    } else if (obstacleRight > obstacleLeft) {
        // More space on right - turn left
        setMotors(-SEARCH_SPEED, SEARCH_SPEED);
    } else {
        // Equal - turn right by default
        setMotors(SEARCH_SPEED, -SEARCH_SPEED);
    }
    
    delay(800);
    
    // Resume previous behavior
    if (currentMode == MODE_TRACKING) {
        currentMode = MODE_TRACKING;
    } else {
        currentMode = MODE_SEARCHING;
    }
}

// ============================================================================
// STATE MACHINE
// ============================================================================

/**
 * @brief Process track command from Raspberry Pi
 */
void processTrackCommand(int dx, int dy, bool found) {
    lastCommandTime = millis();
    commandReceived = true;
    
    if (!found) {
        // No person found - switch to search mode
        currentMode = MODE_SEARCHING;
        return;
    }
    
    currentMode = MODE_TRACKING;
    
    // Check obstacle first
    if (checkObstacles()) {
        performObstacleAvoidance();
        return;
    }
    
    // Check warning distance - turn away without stopping
    if (checkWarningDistance()) {
        if (obstacleLeft > obstacleRight) {
            setMotors(TRACKING_SPEED, -TRACKING_SPEED * 0.5);
        } else {
            setMotors(-TRACKING_SPEED * 0.5, TRACKING_SPEED);
        }
        return;
    }
    
    // Map horizontal offset to turning
    // dx > 0: person is to the right -> turn right
    // dx < 0: person is to the left -> turn left
    // dx ≈ 0: person is centered -> move forward
    
    if (abs(dx) < CENTER_TOLERANCE) {
        // Person centered - move forward
        setMotors(TRACKING_SPEED, TRACKING_SPEED);
    } else if (dx > 0) {
        // Person to right - turn right
        int8_t turnFactor = map(abs(dx), CENTER_TOLERANCE, 320, 20, 100);
        setMotors(TRACKING_SPEED, TRACKING_SPEED - (TRACKING_SPEED * turnFactor / 100));
    } else {
        // Person to left - turn left
        int8_t turnFactor = map(abs(dx), CENTER_TOLERANCE, 320, 20, 100);
        setMotors(TRACKING_SPEED - (TRACKING_SPEED * turnFactor / 100), TRACKING_SPEED);
    }
}

/**
 * @brief Process manual control command
 */
void processManualCommand(int8_t left, int8_t right) {
    lastCommandTime = millis();
    commandReceived = true;
    currentMode = MODE_MANUAL;
    
    setMotors(left, right);
}

/**
 * @brief Execute search behavior
 */
void executeSearch() {
    unsigned long currentTime = millis();
    
    // Check for obstacles first
    if (checkObstacles()) {
        previousMode = MODE_SEARCHING;
        currentMode = MODE_OBSTACLE_AVOIDANCE;
        performObstacleAvoidance();
        return;
    }
    
    // Check warning distance - turn away
    if (checkWarningDistance()) {
        if (obstacleLeft > obstacleRight) {
            setMotors(SEARCH_SPEED, -SEARCH_SPEED);
        } else {
            setMotors(-SEARCH_SPEED, SEARCH_SPEED);
        }
        delay(500);
        return;
    }
    
    // Execute search pattern: forward + periodic turns
    if (currentTime - lastSearchAction > (searchTurn ? SEARCH_TURN_DURATION : SEARCH_MOVE_DURATION)) {
        searchTurn = !searchTurn;
        lastSearchAction = currentTime;
        
        // Alternate turn direction
        if (searchTurn) {
            searchDirection = (searchDirection == 1) ? -1 : 1;
        }
    }
    
    if (searchTurn) {
        // Turn
        setMotors(SEARCH_SPEED * searchDirection, -SEARCH_SPEED * searchDirection);
    } else {
        // Move forward
        setMotors(SEARCH_SPEED, SEARCH_SPEED);
    }
}

/**
 * @brief Check for command timeout
 */
void checkTimeout() {
    if (commandReceived && (millis() - lastCommandTime > COMMAND_TIMEOUT)) {
        // No commands received - switch to search mode
        commandReceived = false;
        currentMode = MODE_SEARCHING;
    }
}

/**
 * @brief Update state machine
 */
void updateState() {
    checkTimeout();
    
    switch (currentMode) {
        case MODE_STOPPED:
            stopMotors();
            break;
            
        case MODE_TRACKING:
            // Tracking is handled in processTrackCommand
            break;
            
        case MODE_SEARCHING:
            executeSearch();
            break;
            
        case MODE_MANUAL:
            // Manual is handled in processManualCommand
            break;
            
        case MODE_OBSTACLE_AVOIDANCE:
            performObstacleAvoidance();
            break;
    }
}

// ============================================================================
// WEBSOCKET COMMUNICATION
// ============================================================================

/**
 * @brief Send status to Raspberry Pi
 */
void sendStatus() {
    if (!wsClient.connected()) return;
    
    StaticJsonDocument<256> doc;
    doc["type"] = "status";
    doc["left_speed"] = leftMotorSpeed;
    doc["right_speed"] = rightMotorSpeed;
    doc["obstacle_middle"] = obstacleMiddle;
    doc["obstacle_left"] = obstacleLeft;
    doc["obstacle_right"] = obstacleRight;
    
    // Convert mode to string
    switch (currentMode) {
        case MODE_STOPPED:
            doc["mode"] = "stopped";
            break;
        case MODE_TRACKING:
            doc["mode"] = "tracking";
            break;
        case MODE_SEARCHING:
            doc["mode"] = "searching";
            break;
        case MODE_MANUAL:
            doc["mode"] = "manual";
            break;
        case MODE_OBSTACLE_AVOIDANCE:
            doc["mode"] = "obstacle_avoidance";
            break;
    }
    
    doc["timestamp"] = millis();
    
    String jsonStr;
    serializeJson(doc, jsonStr);
    wsClient.println(jsonStr);
}

/**
 * @brief Process incoming WebSocket message
 */
void processWebSocketMessage(String message) {
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
        Serial.print("JSON parse error: ");
        Serial.println(error.c_str());
        return;
    }
    
    String type = doc["type"].as<String>();
    
    if (type == "track") {
        int dx = doc["dx"].as<int>();
        int dy = doc["dy"].as<int>();
        bool found = doc["found"].as<bool>();
        processTrackCommand(dx, dy, found);
    } 
    else if (type == "manual") {
        int8_t left = doc["left"].as<int8_t>();
        int8_t right = doc["right"].as<int8_t>();
        processManualCommand(left, right);
    }
    else if (type == "status_req") {
        sendStatus();
    }
}

// ============================================================================
// WIFI & WEBSOCKET SETUP
// ============================================================================

/**
 * @brief Initialize WiFi Access Point
 */
void initWiFi() {
    Serial.println("Starting WiFi Access Point...");
    
    // Configure and start AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, 0, AP_MAX_CONNECTIONS);
    
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);
    Serial.print("AP SSID: ");
    Serial.println(AP_SSID);
}

/**
 * @brief Initialize WebSocket server
 */
void initWebSocket() {
    webSocketServer.begin();
    Serial.println("WebSocket server started");
}

// ============================================================================
// STATUS LED
// ============================================================================

/**
 * @brief Blink status LED based on mode
 */
void updateStatusLed() {
    unsigned long currentTime = millis();
    unsigned long blinkInterval;
    
    // Different blink rates for different modes
    switch (currentMode) {
        case MODE_STOPPED:
            blinkInterval = 2000;  // Slow blink
            break;
        case MODE_TRACKING:
            blinkInterval = 500;   // Fast blink
            break;
        case MODE_SEARCHING:
            blinkInterval = 1000;  // Medium blink
            break;
        case MODE_MANUAL:
            blinkInterval = 200;   // Very fast blink
            break;
        case MODE_OBSTACLE_AVOIDANCE:
            blinkInterval = 100;   // Blink fast when avoiding
            break;
        default:
            blinkInterval = 1000;
    }
    
    if (currentTime - lastLedBlink > blinkInterval) {
        lastLedBlink = currentTime;
        ledState = !ledState;
        digitalWrite(STATUS_LED, ledState ? HIGH : LOW);
    }
}

// ============================================================================
// MAIN SETUP & LOOP
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("===========================================");
    Serial.println("ESP32 Car Control System");
    Serial.println("===========================================");
    
    // Initialize LED
    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, LOW);
    
    // Initialize motors
    initMotors();
    Serial.println("Motors initialized");
    
    // Initialize sensors
    initSensors();
    Serial.println("Ultrasonic sensors initialized");
    
    // Initialize WiFi
    initWiFi();
    
    // Initialize WebSocket
    initWebSocket();
    
    // Initial state
    currentMode = MODE_STOPPED;
    Serial.println("Setup complete!");
    Serial.println("Waiting for Raspberry Pi connection...");
}

void loop() {
    // Check for new WebSocket clients
    if (webSocketServer.getClient()) {
        if (!wsClient.connected()) {
            wsClient = webSocketServer.getClient();
            Serial.println("Client connected!");
        }
    }
    
    // Handle WebSocket communication
    if (wsClient.connected()) {
        // Check for incoming data
        if (wsClient.available()) {
            String message = wsClient.readStringUntil('\n');
            if (message.length() > 0) {
                Serial.print("Received: ");
                Serial.println(message);
                processWebSocketMessage(message);
            }
        }
    } else {
        // Client disconnected - stop motors
        currentMode = MODE_STOPPED;
    }
    
    // Update state machine
    updateState();
    
    // Update status LED
    updateStatusLed();
    
    // Small delay for stability
    delay(10);
}
