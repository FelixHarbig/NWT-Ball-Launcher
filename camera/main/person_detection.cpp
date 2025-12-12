// person_detection.cpp
#include "person_detection.h"
#include "sd_utils.h"               // for read_region_from_pgm
#include "person_detect_model_data.h"
#include "esp_log.h"
#include <cstring>
#include <cmath>
#include <algorithm>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

static const char *TAG = "person_det";

#define MODEL_INPUT_WIDTH 96
#define MODEL_INPUT_HEIGHT 96
#define MODEL_INPUT_CHANNELS 1

#define TENSOR_ARENA_SIZE (128 * 1024)
static uint8_t tensor_arena[TENSOR_ARENA_SIZE] __attribute__((aligned(16)));

static const tflite::Model* model = nullptr;
static tflite::MicroInterpreter* interpreter = nullptr;
static TfLiteTensor* input_tensor = nullptr;

esp_err_t person_detection_init() {
    model = tflite::GetModel(g_person_detect_model_data);
    if (!model) {
        ESP_LOGE(TAG, "Model pointer null");
        return ESP_FAIL;
    }
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Model schema mismatch");
        return ESP_FAIL;
    }

    static tflite::MicroMutableOpResolver<6> resolver;
    resolver.AddConv2D();
    resolver.AddDepthwiseConv2D();
    resolver.AddAveragePool2D();
    resolver.AddFullyConnected();
    resolver.AddReshape();
    resolver.AddSoftmax();

    static tflite::MicroInterpreter static_interpreter(model, resolver, tensor_arena, TENSOR_ARENA_SIZE);
    interpreter = &static_interpreter;

    if (interpreter->AllocateTensors() != kTfLiteOk) {
        ESP_LOGE(TAG, "AllocateTensors failed");
        return ESP_FAIL;
    }
    input_tensor = interpreter->input(0);
    ESP_LOGI(TAG, "TFLM model initialized");
    return ESP_OK;
}

// Bilinear resize from src (region_w x region_h) -> dst MODEL_INPUT_WIDTHxMODEL_INPUT_HEIGHT
static void resize_bilinear_to_model(const uint8_t* src, int region_w, int region_h, uint8_t* dst) {
    float x_ratio = (float)region_w / (float)MODEL_INPUT_WIDTH;
    float y_ratio = (float)region_h / (float)MODEL_INPUT_HEIGHT;

    for (int y = 0; y < MODEL_INPUT_HEIGHT; ++y) {
        for (int x = 0; x < MODEL_INPUT_WIDTH; ++x) {
            float src_x = (x + 0.5f) * x_ratio - 0.5f;
            float src_y = (y + 0.5f) * y_ratio - 0.5f;
            int x0 = (int)floorf(src_x);
            int y0 = (int)floorf(src_y);
            int x1 = x0 + 1;
            int y1 = y0 + 1;
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 >= region_w) x1 = region_w - 1;
            if (y1 >= region_h) y1 = region_h - 1;
            float dx = src_x - x0;
            float dy = src_y - y0;
            uint8_t p00 = src[y0 * region_w + x0];
            uint8_t p10 = src[y0 * region_w + x1];
            uint8_t p01 = src[y1 * region_w + x0];
            uint8_t p11 = src[y1 * region_w + x1];
            float top = p00 * (1.0f - dx) + p10 * dx;
            float bottom = p01 * (1.0f - dx) + p11 * dx;
            float val = top * (1.0f - dy) + bottom * dy;
            int v = (int)(val + 0.5f);
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            dst[y * MODEL_INPUT_WIDTH + x] = (uint8_t)v;
        }
    }
}

static float run_inference_on_region_buffer(uint8_t* region_buf, int region_w, int region_h) {
    if (!interpreter || !input_tensor) return 0.0f;

    // Resize into input tensor memory
    uint8_t* model_input = input_tensor->data.uint8;
    resize_bilinear_to_model(region_buf, region_w, region_h, model_input);

    if (interpreter->Invoke() != kTfLiteOk) {
        ESP_LOGE(TAG, "Interpreter invoke failed");
        return 0.0f;
    }

    TfLiteTensor* output = interpreter->output(0);
    // assuming quantized 2-class softmax with index 1 = person
    float score = (float)output->data.uint8[1] / 255.0f;
    return score;
}

// #define DETECTION_CONFIDENCE_THRESHOLD 0.60f

// Top-level detection function: implements the 1x1 -> 2x2 -> 3x3 flow reading from PGM on SD.
// Returns true if person confirmed in center (global center of 288x288).
bool detect_person_from_sd(const char* pgm_path, detection_result_t* out_result) {
    const int IMG_W = 288;
    const int IMG_H = 288;

    // --- Step 1: Full image 1×1 checks ---
    const int img_bytes = IMG_W * IMG_H;
    uint8_t* img_buf = (uint8_t*)heap_caps_malloc(img_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!img_buf) {
        ESP_LOGE(TAG, "malloc img_buf failed");
        return false;
    }

    if (!read_region_from_pgm(pgm_path, IMG_W, IMG_H, 0, 0, IMG_W, IMG_H, img_buf)) {
        ESP_LOGE(TAG, "read full pgm failed");
        free(img_buf);
        return false;
    }

    int positive = 0;
    for (int i = 0; i < 3; ++i) {
        float conf = run_inference_on_region_buffer(img_buf, IMG_W, IMG_H);
        if (conf > DETECTION_CONFIDENCE_THRESHOLD) positive++;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    free(img_buf);

    // ----------------------------------------------------------------------
    // CASE A: Full image positive → do full 3×3 scan
    // ----------------------------------------------------------------------
    if (positive >= 2) {
        ESP_LOGI(TAG, "Full-image positive -> 3x3 scan");

        const int cell_w = IMG_W / 3; // 96
        const int cell_h = IMG_H / 3; // 96;

        uint8_t* buf = (uint8_t*)malloc(cell_w * cell_h);
        if (!buf) return false;

        bool found = false;
        int sx = 0, sy = 0;

        for (int r = 0; r < 3 && !found; ++r) {
            for (int c = 0; c < 3 && !found; ++c) {
                sx = c * cell_w;
                sy = r * cell_h;

                if (!read_region_from_pgm(pgm_path, IMG_W, IMG_H, sx, sy, cell_w, cell_h, buf))
                    continue;

                float conf = run_inference_on_region_buffer(buf, cell_w, cell_h);
                if (conf > DETECTION_CONFIDENCE_THRESHOLD) {
                    found = true;
                    out_result->row = r;
                    out_result->col = c;
                    out_result->confidence = conf;
                }
            }
        }

        if (!found) {
            free(buf);
            return false;
        }

        // --- verification (buf is still valid!) ---
        int verify = 0;
        for (int k = 0; k < 2; ++k) {
            if (!read_region_from_pgm(pgm_path, IMG_W, IMG_H, sx, sy, cell_w, cell_h, buf))
                break;

            float vconf = run_inference_on_region_buffer(buf, cell_w, cell_h);
            if (vconf > DETECTION_CONFIDENCE_THRESHOLD) verify++;
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        free(buf);
        if (verify >= 1) {
            ESP_LOGI(TAG, "Verified 3x3 cell [%d,%d]", out_result->row, out_result->col);
            return (out_result->row == 1 && out_result->col == 1);
        }
        return false;
    }

    // ----------------------------------------------------------------------
    // CASE B: Full image negative → do 2×2 scan
    // ----------------------------------------------------------------------
    ESP_LOGI(TAG, "Full-image negative -> 2x2 scan");

    const int q_w = IMG_W / 2; // 144
    const int q_h = IMG_H / 2;

    uint8_t* qbuf = (uint8_t*)malloc(q_w * q_h);
    if (!qbuf) return false;

    bool found2x2 = false;
    int qr = -1, qc = -1;

    for (int r = 0; r < 2 && !found2x2; ++r) {
        for (int c = 0; c < 2 && !found2x2; ++c) {
            int sx = c * q_w;
            int sy = r * q_h;

            if (!read_region_from_pgm(pgm_path, IMG_W, IMG_H, sx, sy, q_w, q_h, qbuf))
                continue;

            float conf = run_inference_on_region_buffer(qbuf, q_w, q_h);
            if (conf > DETECTION_CONFIDENCE_THRESHOLD) {
                found2x2 = true;
                qr = r;
                qc = c;
                out_result->row = r;
                out_result->col = c;
                out_result->confidence = conf;
            }
        }
    }
    free(qbuf);

    if (!found2x2) return false;

    // --- Sub-scan inside that quadrant using a 3×3 subdivision ---
    ESP_LOGI(TAG, "Found in 2x2 at [%d,%d] -> scanning 3x3 inside that quadrant", qr, qc);

    int sub_x = qc * q_w;
    int sub_y = qr * q_h;
    int sub_w = q_w;
    int sub_h = q_h;

    int sc_w = sub_w / 3;
    int sc_h = sub_h / 3;

    uint8_t* sbuf = (uint8_t*)malloc(sc_w * sc_h);
    if (!sbuf) return false;

    bool sub_found = false;
    int global_r = -1, global_c = -1;
    int sx_final = 0, sy_final = 0;

    for (int rr = 0; rr < 3 && !sub_found; ++rr) {
        for (int cc = 0; cc < 3 && !sub_found; ++cc) {
            int sx = sub_x + cc * sc_w;
            int sy = sub_y + rr * sc_h;

            if (!read_region_from_pgm(pgm_path, IMG_W, IMG_H, sx, sy, sc_w, sc_h, sbuf))
                continue;

            float conf = run_inference_on_region_buffer(sbuf, sc_w, sc_h);
            if (conf > DETECTION_CONFIDENCE_THRESHOLD) {
                sub_found = true;

                global_c = (sx + sc_w / 2) * 3 / IMG_W;
                global_r = (sy + sc_h / 2) * 3 / IMG_H;

                out_result->row = global_r;
                out_result->col = global_c;
                out_result->confidence = conf;

                sx_final = global_c * (IMG_W / 3);
                sy_final = global_r * (IMG_H / 3);
            }
        }
    }
    free(sbuf);

    if (!sub_found) return false;

    // --- Verify global 3×3 cell ---
    int cell_w = IMG_W / 3;
    int cell_h = IMG_H / 3;

    uint8_t* vbuf = (uint8_t*)malloc(cell_w * cell_h);
    if (!vbuf) return false;

    int ver_ok = 0;
    for (int k = 0; k < 2; ++k) {
        if (!read_region_from_pgm(pgm_path, IMG_W, IMG_H, sx_final, sy_final, cell_w, cell_h, vbuf))
            break;

        float vconf = run_inference_on_region_buffer(vbuf, cell_w, cell_h);
        if (vconf > DETECTION_CONFIDENCE_THRESHOLD) ver_ok++;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    free(vbuf);

    if (ver_ok >= 1) {
        ESP_LOGI(TAG, "Verified global cell [%d,%d]", out_result->row, out_result->col);
        return (out_result->row == 1 && out_result->col == 1);
    }
    return false;
}

// ----------------------------------------------------------------------
// Strategy V2 Implementation
// ----------------------------------------------------------------------
bool detect_person_strategy_v2(const char* pgm_path, detection_result_t* out_result) {
    const int IMG_W = 288;
    const int IMG_H = 288;
    // 3x3 cells means each cell is 96x96
    const int cell_w = 96;
    const int cell_h = 96;

    uint8_t* buf = (uint8_t*)heap_caps_malloc(cell_w * cell_h, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGE(TAG, "malloc buf failed");
        return false;
    }

    // 1. Scan Middle (1,1)
    ESP_LOGI(TAG, "Strategy V2: Scanning Center (1,1)");
    if (read_region_from_pgm(pgm_path, IMG_W, IMG_H, cell_w, cell_h, cell_w, cell_h, buf)) {
        float conf = run_inference_on_region_buffer(buf, cell_w, cell_h);
        if (conf > DETECTION_CONFIDENCE_THRESHOLD) {
            ESP_LOGI(TAG, "Found in Center! Conf: %.2f", conf);
            out_result->row = 1;
            out_result->col = 1;
            out_result->confidence = conf;
            free(buf);
            return true;
        }
    }

    // 2. Scan Other 8 cells
    ESP_LOGI(TAG, "Strategy V2: Scanning Surroundings");
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            if (r == 1 && c == 1) continue; // Skip center

            int sx = c * cell_w;
            int sy = r * cell_h;
            if (read_region_from_pgm(pgm_path, IMG_W, IMG_H, sx, sy, cell_w, cell_h, buf)) {
                float conf = run_inference_on_region_buffer(buf, cell_w, cell_h);
                if (conf > DETECTION_CONFIDENCE_THRESHOLD) {
                    ESP_LOGI(TAG, "Found at [%d, %d] Conf: %.2f", r, c, conf);
                    out_result->row = r;
                    out_result->col = c;
                    out_result->confidence = conf;
                    free(buf);
                    return true;
                }
            }
        }
    }
    free(buf);

    // 3. Scan Full Image (1x1)
    ESP_LOGI(TAG, "Strategy V2: Scanning Full Frame (Very Near?)");
    // Reuse PGM buffer logic but need full size buffer or just resize directly?
    // Run inference helper resizes whatever buffer is passed.
    // Full image is 288x288.
    
    int full_bytes = IMG_W * IMG_H;
    uint8_t* fbuf = (uint8_t*)heap_caps_malloc(full_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!fbuf) {
        ESP_LOGE(TAG, "malloc fbuf failed");
        return false;
    }

    if (read_region_from_pgm(pgm_path, IMG_W, IMG_H, 0, 0, IMG_W, IMG_H, fbuf)) {
        float conf = run_inference_on_region_buffer(fbuf, IMG_W, IMG_H);
        if (conf > DETECTION_CONFIDENCE_THRESHOLD) {
             ESP_LOGI(TAG, "Found in Full Frame! Conf: %.2f", conf);
             // Treat as Center/Near
             out_result->row = 1;
             out_result->col = 1;
             out_result->confidence = conf;
             free(fbuf);
             return true;
        }
    }
    
    free(fbuf);
    ESP_LOGI(TAG, "Strategy V2: No person found.");
    return false;
}
