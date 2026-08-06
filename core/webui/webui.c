#include "webui.h"

#include "esp_http_server.h"
#include "esp_log.h"

#include "settings.h"

static const char *TAG = "webui";

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

static esp_err_t index_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, index_html_start,
                           index_html_end - index_html_start - 1);
}

/* TODO(bring-up): implement the remaining single-purpose pages:
 *   GET/POST /calibration — read/write settings_set_mua_threshold(),
 *            settings_set_fan_min_floor(); form per docs/style-guide.md
 *   GET/POST /hostname    — cosmetic label only (settings_set_hostname_label)
 *   POST     /update      — OTA via esp_https_ota against ota_0/ota_1
 *   POST     /login       — optional password (salted hash in NVS),
 *            session cookie, not persistent Basic Auth (§7.1)
 */

esp_err_t webui_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %d", err);
        return err;
    }

    const httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_get,
    };
    httpd_register_uri_handler(server, &index_uri);

    ESP_LOGI(TAG, "web UI up on :80");
    return ESP_OK;
}
