#ifndef SD_CARD_H
#define SD_CARD_H

#include "esp_err.h"
#include "esp_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t sdcard_init(void);

/**
 * Save the JPEG frame buffer to the SD card.
 * Filename must be a null-terminated string, e.g. "/sdcard/img.jpg"
 */
esp_err_t sdcard_save_jpeg(const char *path, const camera_fb_t *fb);

/**
 * Read a JPEG file fully into RAM. (For debugging only!)
 * In detection we will *not* read into RAM, we will decode in tiles.
 */
uint8_t *sdcard_read_file(const char *path, size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif // SD_CARD_H
