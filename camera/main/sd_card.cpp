#include "sd_card.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "sdcard";

// IMPORTANT: Adapt pins for AI-Thinker
// This uses SDMMC mode (recommended)
esp_err_t sdcard_init(void) {
    ESP_LOGI(TAG, "Mounting SD card...");

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    slot_config.width = 1; // AI Thinker camera board uses 1-bit mode

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = true,
        .use_one_fat = false
    };


    sdmmc_card_t *card;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host,
                                            &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SD card mounted successfully.");
    return ESP_OK;
}

esp_err_t sdcard_save_jpeg(const char *path, const camera_fb_t *fb) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file for writing.");
        return ESP_FAIL;
    }

    size_t written = fwrite(fb->buf, 1, fb->len, f);
    fclose(f);

    if (written != fb->len) {
        ESP_LOGE(TAG, "File write failed.");
        return ESP_FAIL;
    }

    return ESP_OK;
}

uint8_t *sdcard_read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *buf = (uint8_t*)malloc(size);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    fread(buf, 1, size, f);
    fclose(f);

    if (out_size) *out_size = size;
    return buf;
}
