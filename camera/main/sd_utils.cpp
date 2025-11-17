#include "sd_utils.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sd_utils";

#define MOUNT_POINT "/sdcard"

// Card handle must be preserved for VFS to work correctly across tasks
static sdmmc_card_t* s_card = NULL;


// -------------------------
//  MOUNT SD CARD
// -------------------------
esp_err_t sd_mount_card() {
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    // Force 1-bit mode for stability on ESP32-CAM boards
    host.flags = SDMMC_HOST_FLAG_1BIT;

    // Lower SD clock frequency for more stable communication
    host.max_freq_khz = 10000; // 10 MHz (default is often 20+ MHz)

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    // Set bus width to 1-bit
    slot.width = 1;
    // Set GPIO pins for SD card communication
    slot.clk = GPIO_NUM_14;
    slot.cmd = GPIO_NUM_15;
    slot.d0 = GPIO_NUM_2;
    slot.d1 = GPIO_NUM_4;
    slot.d2 = GPIO_NUM_12;
    slot.d3 = GPIO_NUM_13;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    ESP_LOGI("sd_utils", "Mounting SD card with max_freq_khz=%d", host.max_freq_khz);
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(
        MOUNT_POINT,
        &host,
        &slot,
        &mount_config,
        &s_card
    );

    if (ret != ESP_OK) {
        ESP_LOGE("sd_utils", "Failed to mount SD card: %s", esp_err_to_name(ret));
        if (ret == ESP_FAIL) {
            ESP_LOGE("sd_utils", "Failed to mount filesystem. "
                                "If you want the card to be formatted, set format_if_mount_failed = true in mount_config.");
        }
        return ret;
    }

    ESP_LOGI("sd_utils", "SD card mounted successfully");
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}


// -------------------------
// SAVE CENTER CROP AS PGM
// -------------------------
esp_err_t sd_save_center_crop_pgm(camera_fb_t *fb, int crop_size, const char *path) {
    if (!fb) {
        ESP_LOGE(TAG, "Frame is null");
        return ESP_FAIL;
    }

    int w = fb->width;   // 400
    int h = fb->height;  // 296

    if (crop_size > w || crop_size > h) {
        ESP_LOGE(TAG, "Crop too large: crop=%d frame=%dx%d", crop_size, w, h);
        return ESP_FAIL;
    }

    int sx = (w - crop_size) / 2;
    int sy = (h - crop_size) / 2;

    ESP_LOGI(TAG, "Cropping %dx%d at (%d,%d)", crop_size, crop_size, sx, sy);


    ESP_LOGI(TAG, "Frame size: %dx%d, format: %d", fb->width, fb->height, fb->format);

    ESP_LOGI(TAG, "Saving PGM to file: %s", path);
    FILE *f = fopen("/sdcard/test.pgm", "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file %s", path);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "File opened successfully");

    fprintf(f, "P5\n%d %d\n255\n", crop_size, crop_size);

    if (fb->format == PIXFORMAT_GRAYSCALE) {
        // direct copy
        for (int y = 0; y < crop_size; y++) {
            fwrite(&fb->buf[(sy + y) * w + sx], 1, crop_size, f);
        }
    } else if (fb->format == PIXFORMAT_RGB565) {
        // convert RGB565 -> grayscale
        for (int y = 0; y < crop_size; y++) {
            for (int x = 0; x < crop_size; x++) {
                uint16_t pixel = ((uint16_t*)fb->buf)[(sy + y) * w + (sx + x)];
                uint8_t r = ((pixel >> 11) & 0x1F) << 3;
                uint8_t g = ((pixel >> 5) & 0x3F) << 2;
                uint8_t b = (pixel & 0x1F) << 3;
                uint8_t gray = (uint8_t)((uint16_t)r*30 + g*59 + b*11)/100;
                fwrite(&gray, 1, 1, f);
            }
        }
    } else {
        ESP_LOGE(TAG, "Unsupported frame format: %d", fb->format);
        fclose(f);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "File written successfully");
    fclose(f);
    return ESP_OK;
}


// -------------------------
// READ REGION FROM PGM
// -------------------------
bool read_region_from_pgm(const char *path,
                          int img_w, int img_h,
                          int start_x, int start_y,
                          int region_w, int region_h,
                          uint8_t *out_buf)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    // Skip PGM header: "P5\n<width> <height>\n255\n"
    char header[32];
    fgets(header, sizeof(header), f); // P5
    fgets(header, sizeof(header), f); // dimensions
    fgets(header, sizeof(header), f); // maxval

    // Seek to first row
    long data_offset = ftell(f);
    for (int y = 0; y < region_h; y++) {
        long row_offset = data_offset + (start_y + y) * img_w + start_x;
        fseek(f, row_offset, SEEK_SET);
        fread(&out_buf[y * region_w], 1, region_w, f);
    }


    fclose(f);
    return true;
}
