#ifndef PERSON_DETECTION_H
#define PERSON_DETECTION_H

#include "esp_err.h"
#include "esp_camera.h"

// ------------------------------------------------------------
// Detection Configuration
// ------------------------------------------------------------

// Final resized model input size
#define MODEL_INPUT_WIDTH   96
#define MODEL_INPUT_HEIGHT  96

// ≥75% confidence is considered a positive region
#define DETECTION_CONFIDENCE_THRESHOLD 0.75f

// ------------------------------------------------------------
// Data Structures
// ------------------------------------------------------------

// Represents the location of a detection in a grid
typedef struct {
    int row;           // row in grid
    int col;           // column in grid
    float confidence;  // confidence score (0..1)
} detection_result_t;

// ------------------------------------------------------------
// Public API
// ------------------------------------------------------------

bool detect_person_from_sd(const char *path, detection_result_t *result);


/**
 * @brief Initialize TFLite Micro and allocate tensor arena.
 *
 * Must be called once at startup.
 */
esp_err_t person_detection_init(void);

/**
 * @brief Free tensors and deinitialize model.
 *
 * Should be called when shutting down detection.
 */
void person_detection_deinit(void);

/**
 * @brief Perform the multi-stage detection pipeline.
 *
 * The pipeline:
 *   1. Full-frame inference (1×1)
 *   2. 2×2 grid scan if full-frame inconclusive
 *   3. 3×3 scan for localization
 *   4. Return TRUE only if the middle cell (1,1) is confirmed
 *
 * The function does NOT take ownership of fb and does NOT free it.
 *
 * @param fb The camera frame buffer
 * @param[out] result Structure containing the result location + confidence
 * @return true if a confirmed person is centered, false otherwise
 */
bool detect_person_in_center(camera_fb_t *fb, detection_result_t *result);

/**
 * @brief Strategy V2:
 * 1. Scan Middle (1,1) of 3x3. If person -> result=CENTER (row=1,col=1).
 * 2. Scan other 8 cells. If person -> result={r,c}.
 * 3. Scan Full Image (1x1). If person -> result=CENTER (row=1,col=1).
 *
 * @param path Path to PGM file on SD card
 * @param[out] result Result structure
 * @return true if person found (check result.row/col to see where)
 */
bool detect_person_strategy_v2(const char *path, detection_result_t *result);

#endif // PERSON_DETECTION_H
