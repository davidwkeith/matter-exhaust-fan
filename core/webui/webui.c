#include "webui.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "settings.h"
#include "wifi_maintenance.h"

static const char *TAG = "webui";

static webui_config_t s_cfg;

/* --- style-guide chrome (docs/style-guide.md): system fonts, one accent
 * color, minimal markup, ~40rem column ------------------------------- */

static const char PAGE_HEAD[] =
    "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<title>Exhaust Fan Controller</title><style>"
    ":root{--accent:#0a7ea4}"
    "body{font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",Roboto,"
    "Helvetica,Arial,sans-serif;max-width:40rem;margin:2rem auto;"
    "padding:0 1rem;color:#1a1a1a;background:#fff;line-height:1.5}"
    "h1{font-size:1.4rem}a{color:var(--accent)}"
    "nav ul{list-style:none;padding:0}nav li{margin:.5rem 0}"
    "label{display:block;margin:.75rem 0 .25rem}"
    "input{font:inherit;padding:.3rem;width:12rem}"
    "button{font:inherit;margin-top:1rem;padding:.4rem 1.2rem;"
    "background:var(--accent);color:#fff;border:0;border-radius:.25rem}"
    ".note{color:#555;font-size:.9rem}"
    ".ok{color:var(--accent)}"
    "</style></head><body>";

static const char PAGE_FOOT[] = "<p><a href=\"/\">Home</a></p></body></html>";

/* --- tiny form helpers ------------------------------------------------ */

/* Percent-decode + '+'→space, in place. */
static void url_decode(char *s)
{
    char *out = s;
    while (*s) {
        if (*s == '+') {
            *out++ = ' ';
            s++;
        } else if (*s == '%' && s[1] && s[2]) {
            char hex[3] = { s[1], s[2], 0 };
            *out++ = (char)strtol(hex, NULL, 16);
            s += 3;
        } else {
            *out++ = *s++;
        }
    }
    *out = '\0';
}

/* Extract a field from an application/x-www-form-urlencoded body. */
static bool form_get(const char *body, const char *key, char *val, size_t len)
{
    size_t klen = strlen(key);
    const char *p = body;
    while (p && *p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            const char *v = p + klen + 1;
            const char *end = strchr(v, '&');
            size_t vlen = end ? (size_t)(end - v) : strlen(v);
            if (vlen >= len) {
                vlen = len - 1;
            }
            memcpy(val, v, vlen);
            val[vlen] = '\0';
            url_decode(val);
            return true;
        }
        p = strchr(p, '&');
        if (p) {
            p++;
        }
    }
    return false;
}

static bool form_get_u8(const char *body, const char *key, uint8_t min,
                        uint8_t max, uint8_t *out)
{
    char buf[8];
    if (!form_get(body, key, buf, sizeof(buf))) {
        return false;
    }
    char *end;
    long v = strtol(buf, &end, 10);
    if (end == buf || *end != '\0' || v < min || v > max) {
        return false;
    }
    *out = (uint8_t)v;
    return true;
}

static esp_err_t read_body(httpd_req_t *req, char *buf, size_t len)
{
    if (req->content_len >= len) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t off = 0;
    while (off < req->content_len) {
        int r = httpd_req_recv(req, buf + off, req->content_len - off);
        if (r <= 0) {
            return ESP_FAIL;
        }
        off += r;
    }
    buf[off] = '\0';
    return ESP_OK;
}

static esp_err_t send_page(httpd_req_t *req, const char *body_html)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send_chunk(req, PAGE_HEAD, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, body_html, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, PAGE_FOOT, HTTPD_RESP_USE_STRLEN);
    return httpd_resp_send_chunk(req, NULL, 0);
}

/* --- pages ------------------------------------------------------------ */

static esp_err_t index_get(httpd_req_t *req)
{
    char body[768];
    snprintf(body, sizeof(body),
        "<h1>Exhaust Fan Controller</h1>"
        "%s"
        "<p>Local maintenance interface. Normal operation never requires "
        "this page.</p>"
        "<nav><ul>"
        "<li><a href=\"/calibration\">Calibration</a> — makeup-air "
        "threshold, minimum fan speed</li>"
        "<li><a href=\"/hostname\">Device name</a> — cosmetic label only</li>"
        "<li><a href=\"/wifi\">Wi-Fi</a> — maintenance network</li>"
        "<li><a href=\"/update\">Firmware update</a></li>"
        "</ul></nav>",
        wifi_maintenance_in_setup_mode()
            ? "<p class=\"note\"><strong>Setup mode:</strong> this open "
              "access point exists only to configure Wi-Fi. Please finish "
              "<a href=\"/wifi\">Wi-Fi setup</a> promptly.</p>"
            : "");
    return send_page(req, body);
}

static esp_err_t calibration_get(httpd_req_t *req)
{
    uint8_t thr = settings_get_mua_threshold(s_cfg.mua_threshold_default);
    uint8_t hyst = settings_get_mua_hysteresis(s_cfg.mua_hysteresis_default);
    uint8_t floor = settings_get_fan_min_floor(s_cfg.fan_floor_default);

    char body[1024];
    snprintf(body, sizeof(body),
        "<h1>Calibration</h1>"
        "%s"
        "<p class=\"note\">These values describe the installed fan and "
        "survive factory reset. See docs/calibration.md for the measurement "
        "procedure (CA Mechanical Code 505.2 — 400 CFM).</p>"
        "<form method=\"post\">"
        "<label for=\"t\">Makeup-air threshold (%% fan speed)</label>"
        "<input id=\"t\" name=\"mua_threshold\" type=\"number\" min=\"1\" "
        "max=\"100\" value=\"%u\" required>"
        "<label for=\"h\">Hysteresis band (%%)</label>"
        "<input id=\"h\" name=\"mua_hysteresis\" type=\"number\" min=\"0\" "
        "max=\"20\" value=\"%u\" required>"
        "<label for=\"f\">Minimum fan speed floor (%%)</label>"
        "<input id=\"f\" name=\"fan_floor\" type=\"number\" min=\"0\" "
        "max=\"60\" value=\"%u\" required>"
        "<button>Save</button></form>",
        settings_mua_threshold_is_set()
            ? ""
            : "<p class=\"note\"><strong>Not yet calibrated</strong> — the "
              "values below are board defaults, not measurements.</p>",
        thr, hyst, floor);
    return send_page(req, body);
}

static esp_err_t calibration_post(httpd_req_t *req)
{
    char body[256];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
    }

    uint8_t thr, hyst, floor;
    if (!form_get_u8(body, "mua_threshold", 1, 100, &thr) ||
        !form_get_u8(body, "mua_hysteresis", 0, 20, &hyst) ||
        !form_get_u8(body, "fan_floor", 0, 60, &floor)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "values out of range");
    }

    settings_set_mua_threshold(thr);
    settings_set_mua_hysteresis(hyst);
    settings_set_fan_min_floor(floor);
    ESP_LOGI(TAG, "calibration saved: thr=%u hyst=%u floor=%u", thr, hyst,
             floor);
    if (s_cfg.on_calibration_changed) {
        s_cfg.on_calibration_changed();
    }
    return send_page(req,
        "<h1>Calibration</h1><p class=\"ok\">Saved.</p>"
        "<p><a href=\"/calibration\">Back to calibration</a></p>");
}

static esp_err_t hostname_get(httpd_req_t *req)
{
    char label[64] = "";
    settings_get_hostname_label(label, sizeof(label));

    char body[640];
    snprintf(body, sizeof(body),
        "<h1>Device name</h1>"
        "<p class=\"note\">Cosmetic label shown in this UI only. The "
        "network hostname is fixed and derived from the hardware address "
        "— it never changes and is what the NFC tag points to.</p>"
        "<form method=\"post\">"
        "<label for=\"l\">Label</label>"
        "<input id=\"l\" name=\"label\" maxlength=\"48\" value=\"%s\">"
        "<button>Save</button></form>",
        label);
    return send_page(req, body);
}

static esp_err_t hostname_post(httpd_req_t *req)
{
    char body[160];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
    }
    char label[64];
    if (!form_get(body, "label", label, sizeof(label))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing label");
    }
    settings_set_hostname_label(label);
    return send_page(req,
        "<h1>Device name</h1><p class=\"ok\">Saved.</p>");
}

static void reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1500)); /* let the HTTP response flush */
    esp_restart();
}

static esp_err_t wifi_get(httpd_req_t *req)
{
    char ssid[33] = "";
    char pass[65];
    settings_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass));

    char body[640];
    snprintf(body, sizeof(body),
        "<h1>Wi-Fi</h1>"
        "<p class=\"note\">Used only for this maintenance interface. "
        "Matter control runs over Thread and does not depend on it.</p>"
        "<form method=\"post\">"
        "<label for=\"s\">Network name (SSID)</label>"
        "<input id=\"s\" name=\"ssid\" maxlength=\"32\" value=\"%s\" required>"
        "<label for=\"p\">Password</label>"
        "<input id=\"p\" name=\"pass\" type=\"password\" maxlength=\"64\">"
        "<button>Save and reboot</button></form>",
        ssid);
    return send_page(req, body);
}

static esp_err_t wifi_post(httpd_req_t *req)
{
    char body[256];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
    }
    char ssid[33], pass[65];
    if (!form_get(body, "ssid", ssid, sizeof(ssid)) || ssid[0] == '\0') {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing ssid");
    }
    if (!form_get(body, "pass", pass, sizeof(pass))) {
        pass[0] = '\0';
    }
    settings_set_wifi(ssid, pass);
    ESP_LOGI(TAG, "Wi-Fi credentials saved (ssid=\"%s\"), rebooting", ssid);
    esp_err_t err = send_page(req,
        "<h1>Wi-Fi</h1><p class=\"ok\">Saved — rebooting to join the "
        "network. Reconnect via <code>http://&lt;hostname&gt;.local/</code> "
        "(see the NFC tag).</p>");
    xTaskCreate(reboot_task, "reboot", 2048, NULL, 1, NULL);
    return err;
}

/* Raw-binary upload (no multipart parsing needed): the page's inline script
 * POSTs the .bin file body directly and reports progress via XHR upload
 * events. Written to the inactive ota_0/ota_1 slot with esp_ota_*; the
 * running slot is left untouched until esp_ota_set_boot_partition succeeds,
 * so a bad upload never disturbs the working firmware. */

static const char UPDATE_SCRIPT[] =
    "<script>"
    "document.getElementById('f').addEventListener('submit',function(ev){"
    "ev.preventDefault();"
    "var file=document.getElementById('file').files[0];"
    "var status=document.getElementById('status');"
    "if(!file)return;"
    "var xhr=new XMLHttpRequest();"
    "xhr.open('POST','/update');"
    "xhr.setRequestHeader('Content-Type','application/octet-stream');"
    "xhr.upload.onprogress=function(e){"
    "if(e.lengthComputable){"
    "status.textContent='Uploading: '+Math.round(100*e.loaded/e.total)+'%';"
    "}};"
    "xhr.onload=function(){document.open();document.write(xhr.responseText);"
    "document.close();};"
    "xhr.onerror=function(){status.textContent='Upload failed — connection "
    "lost.';};"
    "status.textContent='Uploading…';"
    "xhr.send(file);"
    "});"
    "</script>";

static esp_err_t update_get(httpd_req_t *req)
{
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);

    char body[1536];
    snprintf(body, sizeof(body),
        "<h1>Firmware update</h1>"
        "<p class=\"note\">Upload a firmware <code>.bin</code> built for "
        "this board (up to %lu KB). The current firmware stays in the "
        "other OTA slot — if the new image fails to boot, the device "
        "automatically rolls back to it.</p>"
        "<form id=\"f\">"
        "<label for=\"file\">Firmware image</label>"
        "<input id=\"file\" name=\"file\" type=\"file\" accept=\".bin\" "
        "required>"
        "<button type=\"submit\">Upload and reboot</button>"
        "</form>"
        "<p id=\"status\" class=\"note\" role=\"status\"></p>"
        "%s",
        target ? (unsigned long)(target->size / 1024) : 0UL, UPDATE_SCRIPT);
    return send_page(req, body);
}

#define OTA_CHUNK_SIZE 4096

static esp_err_t update_post(httpd_req_t *req)
{
    if (req->content_len == 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
    }

    const esp_partition_t *update_partition =
        esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "no OTA partition available");
    }
    if (req->content_len > update_partition->size) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "image too large for the OTA partition");
    }

    esp_ota_handle_t ota_handle;
    esp_err_t err =
        esp_ota_begin(update_partition, req->content_len, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "could not start OTA write");
    }

    static uint8_t buf[OTA_CHUNK_SIZE]; /* too large for the httpd stack */
    size_t received = 0;
    while (received < req->content_len) {
        size_t want = req->content_len - received;
        if (want > OTA_CHUNK_SIZE) {
            want = OTA_CHUNK_SIZE;
        }
        int r = httpd_req_recv(req, (char *)buf, want);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            esp_ota_abort(ota_handle);
            ESP_LOGE(TAG, "OTA upload interrupted after %u/%u bytes",
                     (unsigned)received, (unsigned)req->content_len);
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                       "upload interrupted");
        }
        err = esp_ota_write(ota_handle, buf, (size_t)r);
        if (err != ESP_OK) {
            esp_ota_abort(ota_handle);
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            return send_page(req,
                "<h1>Firmware update</h1><p class=\"note\">Upload "
                "rejected — not a valid firmware image for this board. "
                "Nothing was flashed.</p>"
                "<p><a href=\"/update\">Try again</a></p>");
        }
        received += (size_t)r;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        return send_page(req,
            "<h1>Firmware update</h1><p class=\"note\">Image failed "
            "validation after upload — nothing was flashed. Re-download "
            "it and try again.</p>"
            "<p><a href=\"/update\">Try again</a></p>");
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s",
                 esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "could not select the new firmware");
    }

    ESP_LOGW(TAG, "OTA update written to \"%s\", rebooting",
             update_partition->label);
    esp_err_t resp_err = send_page(req,
        "<h1>Firmware update</h1><p class=\"ok\">Upload complete — "
        "rebooting into the new firmware.</p>"
        "<p class=\"note\">If it fails to come back up, the device "
        "automatically rolls back to the version you're on now.</p>");
    xTaskCreate(reboot_task, "reboot", 2048, NULL, 1, NULL);
    return resp_err;
}

/* --- server ----------------------------------------------------------- */

esp_err_t webui_start(const webui_config_t *config)
{
    s_cfg = *config;

    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.max_uri_handlers = 12;
    http_cfg.stack_size = 8192; /* OTA write handler needs headroom */
    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &http_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %d", err);
        return err;
    }

    const httpd_uri_t routes[] = {
        { .uri = "/",            .method = HTTP_GET,  .handler = index_get },
        { .uri = "/calibration", .method = HTTP_GET,  .handler = calibration_get },
        { .uri = "/calibration", .method = HTTP_POST, .handler = calibration_post },
        { .uri = "/hostname",    .method = HTTP_GET,  .handler = hostname_get },
        { .uri = "/hostname",    .method = HTTP_POST, .handler = hostname_post },
        { .uri = "/wifi",        .method = HTTP_GET,  .handler = wifi_get },
        { .uri = "/wifi",        .method = HTTP_POST, .handler = wifi_post },
        { .uri = "/update",      .method = HTTP_GET,  .handler = update_get },
        { .uri = "/update",      .method = HTTP_POST, .handler = update_post },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }

    ESP_LOGI(TAG, "web UI up on :80");
    return ESP_OK;
}
