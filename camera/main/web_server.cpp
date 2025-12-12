#include "web_server.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "web_server";

#define PGM_PATH "/sdcard/test.pgm"

// --- HTTPD Handlers ---

// Stream the saved PGM file from SD in small chunks
static esp_err_t pgm_handler(httpd_req_t *req)
{
    FILE *f = fopen(PGM_PATH, "rb");
    if (!f) {
        ESP_LOGE(TAG, "PGM not found at %s", PGM_PATH);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "PGM not found");
        return ESP_FAIL;
    }

    // Set MIME type for PGM (portable graymap)
    httpd_resp_set_type(req, "image/x-portable-graymap");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    // Stream file in small chunks to avoid large allocations and DMA pressure
    char buf[512];
    size_t n;
    esp_err_t res = ESP_OK;

    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        res = httpd_resp_send_chunk(req, buf, n);
        if (res != ESP_OK) {
            ESP_LOGW(TAG, "send_chunk failed (%d), client may have closed the connection", (int)res);
            break;
        }
        // Optional small delay to be gentle on SD/CPU under heavy load
        // vTaskDelay(pdMS_TO_TICKS(1));
    }

    fclose(f);

    // Terminate chunked response
    httpd_resp_send_chunk(req, NULL, 0);

    // If we broke due to send error, report failure
    if (res != ESP_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

// Minimal favicon handler (avoids 404 noise in logs)
static esp_err_t favicon_handler(httpd_req_t *req)
{
    static const unsigned char blank_ico[] = {
        0x00,0x00,0x01,0x00,0x01,0x00,0x10,0x10,0x00,0x00,0x01,0x00,0x04,0x00,
        0x28,0x01,0x00,0x00, // tiny placeholder; browsers won't show anything
    };
    httpd_resp_set_type(req, "image/x-icon");
    return httpd_resp_send(req, (const char*)blank_ico, sizeof(blank_ico));
}

// Simple HTML page that embeds the PGM
// Handler for the main page with JS to render PGM
static esp_err_t index_handler(httpd_req_t *req) {
    const char *html =
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<title>PGM Viewer</title>"
        "<style>"
        "body{font-family:sans-serif;margin:16px;background:#f9f9f9;color:#333}"
        "#status{margin:8px 0;padding:8px;background:#eee;border:1px solid #ccc}"
        "canvas{border:1px solid #666;display:block;margin-top:12px;max-width:100%}"
        "</style></head><body>"
        "<h1>Last Capture (PGM)</h1>"
        "<div id=\"status\">Initializing…</div>"
        "<canvas id=\"pgmCanvas\"></canvas>"
        "<script>"
        "async function loadPGM(){"
        "  const status=document.getElementById('status');"
        "  try{"
        "    status.textContent='Fetching PGM file…';"
        "    const resp=await fetch('/pgm');"
        "    if(!resp.ok){status.textContent='Error: '+resp.statusText;return;}"
        "    const buf=await resp.arrayBuffer();"
        "    status.textContent='Parsing header…';"
        "    const bytes=new Uint8Array(buf);"
        "    let headerText='';let i=0;"
        "    while(i<bytes.length && headerText.indexOf('\\n255')===-1){"
        "      headerText+=String.fromCharCode(bytes[i++]);"
        "    }"
        "    const headerParts=headerText.trim().split(/\\s+/);"
        "    if(headerParts[0]!=='P5'){status.textContent='Unsupported format';return;}"
        "    const w=parseInt(headerParts[1]);"
        "    const h=parseInt(headerParts[2]);"
        "    status.textContent='Rendering '+w+'x'+h+' image…';"
        "    const pixelStart=i;"
        "    const pixels=bytes.slice(pixelStart);"
        "    const canvas=document.getElementById('pgmCanvas');"
        "    canvas.width=w;canvas.height=h;"
        "    const ctx=canvas.getContext('2d');"
        "    const imgData=ctx.createImageData(w,h);"
        "    for(let y=0;y<h;y++){for(let x=0;x<w;x++){"
        "      const v=pixels[y*w+x];"
        "      const idx=(y*w+x)*4;"
        "      imgData.data[idx]=v;"
        "      imgData.data[idx+1]=v;"
        "      imgData.data[idx+2]=v;"
        "      imgData.data[idx+3]=255;"
        "    }}"
        "    ctx.putImageData(imgData,0,0);"
        "    status.textContent='Done.';"
        "  }catch(e){status.textContent='Error: '+e;}"
        "}"
        "loadPGM();"
        "</script>"
        "</body></html>";

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, strlen(html));
}



// --- Wi‑Fi SoftAP Setup ---

// --- Server start ---

esp_err_t start_web_server()
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.stack_size = 16384; // larger stack to improve robustness

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL
    };

    httpd_uri_t pgm_uri = {
        .uri = "/pgm",
        .method = HTTP_GET,
        .handler = pgm_handler,
        .user_ctx = NULL
    };

    httpd_uri_t favicon_uri = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = favicon_handler,
        .user_ctx = NULL
    };

    // Note: NVS and Wi-Fi are already initialized in main.cpp (Station Mode).
    // The server will simply bind to the active interface.

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &pgm_uri);
        httpd_register_uri_handler(server, &favicon_uri);
        ESP_LOGI(TAG, "HTTP server started");
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Error starting server!");
    return ESP_FAIL;
}
