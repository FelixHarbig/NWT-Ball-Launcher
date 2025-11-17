/*
  person_web/main.cpp  (C++ compatible)

  ESP-IDF app:
   - starts WiFi AP
   - serves a simple upload page on /
   - receives POST /infer with raw 96x96 grayscale uint8 bytes (9216 bytes)
   - runs tflite micro model (person_detect_model_data) and returns JSON
*/

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_http_server.h"

#include "person_detect_model_data.h" // generated model data C file

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

// ---- Config ----
static const char *TAG = "person_web";
#define AP_SSID "ESP32_PersonDetect"
#define AP_PASS "esp32pass"

extern const uint8_t index_html[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t style_css[] asm("_binary_style_css_start");
extern const uint8_t style_css_end[] asm("_binary_style_css_end");

#define GRID_SIZE 3  // 3x3 grid

#define MODEL_INPUT_WIDTH 96
#define MODEL_INPUT_HEIGHT 96
#define MODEL_INPUT_SIZE (MODEL_INPUT_WIDTH * MODEL_INPUT_HEIGHT) // grayscale

// Tensor arena size (adjust if AllocateTensors fails)
#define TENSOR_ARENA_SIZE (128 * 1024)  // 128 KB
#ifdef CONFIG_SPIRAM_SUPPORT
static uint8_t tensor_arena[TENSOR_ARENA_SIZE] __attribute__((section(".psram.bss"))) = {0};
#else
static uint8_t tensor_arena[TENSOR_ARENA_SIZE] = {0};
#endif

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

// TFLM globals
const tflite::Model *model = NULL;
tflite::MicroInterpreter *interpreter = NULL;
TfLiteTensor *input_tensor = NULL;
TfLiteTensor *output_tensor = NULL;

// ----------------- Serve HTML/JS which resizes image client-side -----------------
static const char index_html_old[] =
"<!doctype html>\n"
"<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width'>\n"
"<title>ESP32 Person Detection</title>\n"
"</head><body>\n"
"<h2>Upload an image (client will resize to 96x96)</h2>\n"
"<input type='file' id='file' accept='image/*' />\n"
"<button id='btn'>Upload & Infer</button>\n"
"<p id='status'></p>\n"
"<pre id='result'></pre>\n"
"\n"
"<script>\n"
"const W=96, H=96;\n"
"async function readFileAsImage(file){\n"
"  return new Promise((res,rej)=>{\n"
"    const img = new Image();\n"
"    img.onload = () => res(img);\n"
"    img.onerror = rej;\n"
"    const fr = new FileReader();\n"
"    fr.onload = e => img.src = e.target.result;\n"
"    fr.readAsDataURL(file);\n"
"  });\n"
"}\n"
"function toGrayData(img){\n"
"  const canvas = document.createElement('canvas'); canvas.width=W; canvas.height=H;\n"
"  const ctx = canvas.getContext('2d');\n"
"  // draw image scaled to 96x96\n"
"  ctx.drawImage(img,0,0,W,H);\n"
"  const imgd = ctx.getImageData(0,0,W,H);\n"
"  const data = new Uint8Array(W*H);\n"
"  for(let i=0,j=0;i<imgd.data.length;i+=4,j++){\n"
"    const r=imgd.data[i], g=imgd.data[i+1], b=imgd.data[i+2];\n"
"    // luma conversion\n"
"    data[j] = Math.round(0.299*r + 0.587*g + 0.114*b);\n"
"  }\n"
"  return data;\n"
"}\n"
"document.getElementById('btn').onclick = async ()=>{\n"
"  const f = document.getElementById('file').files[0];\n"
"  if(!f){ alert('choose file'); return; }\n"
"  document.getElementById('status').innerText = 'Preparing image...';\n"
"  try{\n"
"    const img = await readFileAsImage(f);\n"
"    const gray = toGrayData(img);\n"
"    document.getElementById('status').innerText = 'Uploading ('+gray.length+' bytes)...';\n"
"    const resp = await fetch('/infer',{ method:'POST', headers:{'Content-Type':'application/octet-stream'}, body:gray });\n"
"    if(!resp.ok){ throw new Error('HTTP '+resp.status); }\n"
"    const json = await resp.json();\n"
"    document.getElementById('status').innerText = 'Done';\n"
"    document.getElementById('result').innerText = JSON.stringify(json, null, 2);\n"
"  }catch(e){ document.getElementById('status').innerText = 'Error: '+e; }\n"
"};\n"
"</script>\n"
"</body></html>\n";

// ----------------- HTTP handlers -----------------
// static esp_err_t index_get_handler(httpd_req_t *req)
//{
 //   httpd_resp_set_type(req, "text/html");
//    httpd_resp_send(req, index_html, strlen(index_html));
//    return ESP_OK;
//}


static esp_err_t index_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)index_html, index_html_end - index_html);
    return ESP_OK;
}

static esp_err_t css_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, (const char *)style_css, style_css_end - style_css);
    return ESP_OK;
}


float infer_cell(uint8_t *cell_data) {
    // copy into input tensor
    if (input_tensor->type == kTfLiteUInt8) {
        memcpy(input_tensor->data.uint8, cell_data, MODEL_INPUT_SIZE);
    } else if (input_tensor->type == kTfLiteInt8) {
        int8_t *dst = input_tensor->data.int8;
        for (size_t i = 0; i < MODEL_INPUT_SIZE; ++i) dst[i] = (int8_t)((int)cell_data[i]-128);
    }
    TfLiteStatus invoke_status = interpreter->Invoke();
    if (invoke_status != kTfLiteOk) return 0.0f;
    TfLiteTensor *out = interpreter->output(0);
    float conf = 0.0f;
    if (out->type == kTfLiteUInt8) {
        conf = (out->data.uint8[1]-out->params.zero_point)*out->params.scale;
    } else if (out->type == kTfLiteInt8) {
        conf = (out->data.int8[1]-out->params.zero_point)*out->params.scale;
    } else if (out->type == kTfLiteFloat32) {
        conf = out->data.f[1];
    }
    if (conf < 0.0f) conf = 0.0f;
    if (conf > 1.0f) conf = 1.0f;
    return conf;
}

/*
 * POST /infer
 * Body: raw application/octet-stream of length MODEL_INPUT_SIZE (9216) bytes
 * Returns: JSON {"human": true/false, "confidence": 0.83}
 */
static esp_err_t infer_post_handler(httpd_req_t *req)
{
    int content_len = req->content_len; // can be 0 if not provided
    if (content_len != MODEL_INPUT_SIZE) {
        ESP_LOGW(TAG, "Content-Length %d != expected %d. We'll read up to expected size or what's available.", content_len, MODEL_INPUT_SIZE);
    }

    // allocate input buffer for expected size
    uint8_t *buf = (uint8_t *)malloc(MODEL_INPUT_SIZE);
    if (!buf) {
        ESP_LOGE(TAG, "No memory for input buffer");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
        return ESP_FAIL;
    }
    memset(buf, 0, MODEL_INPUT_SIZE);

    // Read body robustly (partial reads, timeouts)
    int total_received = 0;
    while (total_received < MODEL_INPUT_SIZE) {
        // request to read remaining up to MODEL_INPUT_SIZE - total_received
        int to_read = MODEL_INPUT_SIZE - total_received;
        int r = httpd_req_recv(req, (char *)buf + total_received, to_read);
        if (r > 0) {
            total_received += r;
            continue;
        } else if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "httpd_req_recv timed out, retrying");
            continue;
        } else {
            if (r == 0) {
                ESP_LOGW(TAG, "httpd_req_recv returned 0 (connection closed?) after %d bytes", total_received);
            } else {
                ESP_LOGW(TAG, "httpd_req_recv error %d after %d bytes", r, total_received);
            }
            break;
        }
    }

    if (total_received == 0) {
        ESP_LOGW(TAG, "No data received");
        free(buf);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }

    // Ensure interpreter and input tensor exist
    if (!input_tensor || !interpreter) {
        ESP_LOGE(TAG, "Interpreter or input tensor not initialized");
        free(buf);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Model not initialized");
        return ESP_FAIL;
    }

    // Check input tensor size and type
    if (input_tensor->bytes < MODEL_INPUT_SIZE) {
        ESP_LOGW(TAG, "Model input tensor bytes (%d) < expected (%d). Will copy up to tensor size.", input_tensor->bytes, MODEL_INPUT_SIZE);
    }

    size_t copy_bytes = MIN((size_t)total_received, (size_t)input_tensor->bytes);
    if (input_tensor->type == kTfLiteUInt8) {
        memcpy(input_tensor->data.uint8, buf, copy_bytes);
    } else if (input_tensor->type == kTfLiteInt8) {
        int8_t *dst = input_tensor->data.int8;
        for (size_t i = 0; i < copy_bytes; ++i) {
            dst[i] = (int8_t)((int)buf[i] - 128);
        }
    } else {
        ESP_LOGW(TAG, "Unhandled model input type %d. Attempting raw copy.", input_tensor->type);
        memcpy(input_tensor->data.raw, buf, copy_bytes);
    }

    free(buf);

    // Run inference
    TfLiteStatus invoke_status = interpreter->Invoke();
    float confidence = 0.0f;
    bool human = false;
    if (invoke_status != kTfLiteOk) {
        ESP_LOGE(TAG, "Invoke failed");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Inference failed");
        return ESP_FAIL;
    } else {
        output_tensor = interpreter->output(0);
        if (!output_tensor) {
            ESP_LOGE(TAG, "No output tensor");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No output tensor");
            return ESP_FAIL;
        }

        int out_size = 1;
        for (int i = 0; i < output_tensor->dims->size; ++i) {
            out_size *= output_tensor->dims->data[i];
        }
        int person_idx = MIN(1, out_size - 1);

        if (output_tensor->type == kTfLiteUInt8) {
            uint8_t *out = output_tensor->data.uint8;
            const float scale = output_tensor->params.scale;
            const int zp = output_tensor->params.zero_point;
            float val = (out[person_idx] - zp) * scale;
            if (val < 0.0f) val = 0.0f;
            if (val > 1.0f) val = 1.0f;
            confidence = val;
            human = (confidence > 0.5f);
        } else if (output_tensor->type == kTfLiteInt8) {
            int8_t *out = output_tensor->data.int8;
            const float scale = output_tensor->params.scale;
            const int zp = output_tensor->params.zero_point;
            float val = (out[person_idx] - zp) * scale;
            if (val < 0.0f) val = 0.0f;
            if (val > 1.0f) val = 1.0f;
            confidence = val;
            human = (confidence > 0.5f);
        } else if (output_tensor->type == kTfLiteFloat32) {
            float *out = output_tensor->data.f;
            float val = out[person_idx];
            if (val < 0.0f) val = 0.0f;
            if (val > 1.0f) val = 1.0f;
            confidence = val;
            human = (confidence > 0.5f);
        } else {
            ESP_LOGE(TAG, "Unsupported output tensor type %d", output_tensor->type);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Unsupported output tensor type");
            return ESP_FAIL;
        }
    }

    char json[128];
    int n = snprintf(json, sizeof(json), "{\"human\":%s,\"confidence\":%.2f}", human ? "true" : "false", confidence);
    if (n < 0 || (size_t)n >= sizeof(json)) {
        ESP_LOGW(TAG, "JSON truncated");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

// static void start_webserver(void)
// {
//     httpd_config_t config = HTTPD_DEFAULT_CONFIG();
//     config.stack_size = 4096;
//     httpd_handle_t server = NULL;
//     if (httpd_start(&server, &config) != ESP_OK) {
//         ESP_LOGE(TAG, "Failed to start http server");
//         return;
//     }

//     httpd_uri_t index_uri = {};
//     index_uri.uri = "/";
//     index_uri.method = HTTP_GET;
//     index_uri.handler = index_get_handler;
//     index_uri.user_ctx = NULL;
//     httpd_register_uri_handler(server, &index_uri);

//     httpd_uri_t infer_uri = {};
//     infer_uri.uri = "/infer";
//     infer_uri.method = HTTP_POST;
//     infer_uri.handler = infer_post_handler;
//     infer_uri.user_ctx = NULL;
//     httpd_register_uri_handler(server, &infer_uri);

//     ESP_LOGI(TAG, "HTTP server started");
// }

static void start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server");
        return;
    }

    httpd_uri_t index_uri = {
        .uri = "/", .method = HTTP_GET, .handler = index_get_handler
    };
    httpd_register_uri_handler(server, &index_uri);

    httpd_uri_t css_uri = {
        .uri = "/style.css", .method = HTTP_GET, .handler = css_get_handler
    };
    httpd_register_uri_handler(server, &css_uri);

    httpd_uri_t infer_uri = {
        .uri = "/infer", .method = HTTP_POST, .handler = infer_post_handler
    };
    httpd_register_uri_handler(server, &infer_uri);

    ESP_LOGI(TAG, "Server started");
}

// ----------------- WiFi AP -----------------
static void wifi_init_softap(void)
{
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    (void)ap_netif; // silence unused warning

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Zero-init struct and then populate fields explicitly (C++ / cross-version safe)
    wifi_config_t ap_config = {};
    // copy SSID and password safely into the fixed-size arrays
    strncpy((char *)ap_config.ap.ssid, AP_SSID, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid[sizeof(ap_config.ap.ssid) - 1] = '\0';
    ap_config.ap.ssid_len = (uint8_t)strlen((const char *)ap_config.ap.ssid);

    if (strlen(AP_PASS) > 0) {
        strncpy((char *)ap_config.ap.password, AP_PASS, sizeof(ap_config.ap.password) - 1);
        ap_config.ap.password[sizeof(ap_config.ap.password) - 1] = '\0';
        ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    } else {
        ap_config.ap.password[0] = '\0';
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ap_config.ap.max_connection = 4;
    ap_config.ap.ssid_hidden = 0;
    ap_config.ap.channel = 1;

    // Proper PMF struct initialization (wifi_pmf_config_t has fields 'capable' and 'required' in v5.x)
    ap_config.ap.pmf_cfg.capable = false;
    ap_config.ap.pmf_cfg.required = false;

    // SAE PWE method enum — choose BOTH to allow H2E and Hunt-and-Peck if supported by SDK
    #ifdef WPA3_SAE_PWE_BOTH
    ap_config.ap.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    #else
    // fallback if enum name differs in SDK
    ap_config.ap.sae_pwe_h2e = (wifi_sae_pwe_method_t)0;
    #endif

    // Some newer fields — ensure initialized to safe defaults if present
    ap_config.ap.beacon_interval = 100;
    ap_config.ap.ftm_responder = false;
    // Do not touch fields that don't exist in this SDK version (e.g. sae_h2e_identifier)
    ap_config.ap.transition_disable = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi AP started. SSID:%s PW:%s", AP_SSID, (strlen(AP_PASS) ? AP_PASS : "<open>"));
}

// ----------------- TFLM init -----------------
static bool tflm_init(void)
{
    ESP_LOGI(TAG, "Loading model...");
    model = tflite::GetModel(g_person_detect_model_data);
    if (!model) {
        ESP_LOGE(TAG, "Failed to load model data: pointer is NULL!");
        return false;
    }
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Model schema version mismatch");
        return false;
    }

    static tflite::MicroMutableOpResolver<6> resolver;
    resolver.AddConv2D();
    resolver.AddDepthwiseConv2D();
    resolver.AddAveragePool2D();
    resolver.AddFullyConnected();
    resolver.AddReshape();
    resolver.AddSoftmax();

    static tflite::MicroInterpreter static_interpreter(model, resolver, tensor_arena, TENSOR_ARENA_SIZE, nullptr);
    interpreter = &static_interpreter;

    TfLiteStatus status = interpreter->AllocateTensors();
    if (status != kTfLiteOk) {
        ESP_LOGE(TAG, "AllocateTensors() failed. Needed tensor arena too big or fragmented.");
        return false;
    }

    input_tensor = interpreter->input(0);
    output_tensor = interpreter->output(0);
    if (!input_tensor || !output_tensor) {
        ESP_LOGE(TAG, "Failed to get input/output tensors");
        return false;
    }

    ESP_LOGI(TAG, "TFLM initialized: input type=%d, bytes=%d", input_tensor->type, input_tensor->bytes);
    return true;
}


// ----------------- app_main -----------------
#ifdef __cplusplus
extern "C" {
#endif

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_softap();

    // Initialize TFLM
    if (!tflm_init()) {
        ESP_LOGE(TAG, "TFLM init failed - inference won't work");
    }

    start_webserver();
    ESP_LOGI(TAG, "Open your browser to http://192.168.4.1 and upload a picture.");
}

#ifdef __cplusplus
} // extern "C"
#endif
