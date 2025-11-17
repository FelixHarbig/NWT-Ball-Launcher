#ifndef SD_UTILS_H
#define SD_UTILS_H

#include "esp_err.h"
#include "esp_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

// Mount SD card (/sdcard)
esp_err_t sd_mount_card();

// Save 288x288 grayscale crop from center of frame
esp_err_t sd_save_center_crop_pgm(camera_fb_t *fb, int crop_size, const char *path);

// Read sub-region from a PGM on SD into buffer
bool read_region_from_pgm(const char *path,
                          int img_w, int img_h,
                          int start_x, int start_y,
                          int region_w, int region_h,
                          uint8_t *out_buf);

#ifdef __cplusplus
}
#endif

#endif
