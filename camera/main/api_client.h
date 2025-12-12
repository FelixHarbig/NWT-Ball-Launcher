#ifndef API_CLIENT_H
#define API_CLIENT_H

#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h" // Assuming cJSON is available in IDF

static const char *API_TAG = "API_CLIENT";

#define MAIN_ESP_IP "192.168.4.1"
#define BASE_URL "http://" MAIN_ESP_IP

/**
 * @brief Check if ball is ready
 * @return true if ball is detected, false otherwise
 */
static bool api_check_ball_status() {
    bool ball_ready = false;
    esp_http_client_config_t config = {};
    config.url = BASE_URL "/sensor/ball";
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 2000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    if (esp_http_client_open(client, 0) == ESP_OK) {
        int content_length = esp_http_client_fetch_headers(client);
        if (content_length > 0) {
            char *buffer = (char*)malloc(content_length + 1);
            if (buffer) {
                int read_len = esp_http_client_read(client, buffer, content_length);
                if (read_len > 0) {
                    buffer[read_len] = 0;
                    // Parse JSON: {"ball_detected": true}
                    if (strstr(buffer, "true")) {
                        ball_ready = true;
                    }
                }
                free(buffer);
            }
        }
    } else {
        ESP_LOGE(API_TAG, "Failed to open connection for ball check");
    }
    esp_http_client_cleanup(client);
    return ball_ready;
}

/**
 * @brief Send stepper move command
 * @param steps Positive for one direction, negative for other
 */
static void api_send_stepper_command(int steps) {
    esp_http_client_config_t config = {};
    config.url = BASE_URL "/control/stepper";
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 2000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    char post_data[64];
    snprintf(post_data, sizeof(post_data), "{\"steps\": %d}", steps);
    
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_http_client_set_header(client, "Content-Type", "application/json"); // Optional but good practice

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(API_TAG, "Stepper command failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

/**
 * @brief Send tank move command (Placeholder)
 */
static void api_send_tank_command(int left, int right) {
    esp_http_client_config_t config = {};
    config.url = BASE_URL "/control/tank";
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 1000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    char post_data[64];
    snprintf(post_data, sizeof(post_data), "{\"left\": %d, \"right\": %d}", left, right);
    
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    
    esp_http_client_perform(client);
    esp_http_client_cleanup(client);
}

#endif // API_CLIENT_H
