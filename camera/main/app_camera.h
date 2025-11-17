#ifndef APP_CAMERA_H
#define APP_CAMERA_H

#include "esp_camera.h"

// Initialize the camera
esp_err_t app_camera_init();

// Get a camera frame
camera_fb_t* app_camera_get_frame();

// Release a camera frame
void app_camera_return_frame(camera_fb_t *fb);

#endif // APP_CAMERA_H
