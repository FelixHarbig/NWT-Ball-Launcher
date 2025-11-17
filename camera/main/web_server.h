#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"

// --- Configuration ---
#define WIFI_AP_SSID "NWT-Ball-Launcher"
#define WIFI_AP_PASS "fireaway"

// --- Functions ---

/**
 * @brief Starts the Wi-Fi Access Point and the HTTP web server.
 * The server will stream the camera feed and provide status updates.
 */
esp_err_t start_web_server();

#endif // WEB_SERVER_H
