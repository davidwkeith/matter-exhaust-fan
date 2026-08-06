#include "wifi_maintenance.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "settings.h"

static const char *TAG = "wifi_maint";

static bool s_setup_mode;

bool wifi_maintenance_in_setup_mode(void)
{
    return s_setup_mode;
}

static void sta_event_handler(void *arg, esp_event_base_t base, int32_t id,
                              void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        /* Maintenance plane is best-effort: retry forever, quietly. Normal
         * operation (wall switches, Matter over Thread) never depends on
         * Wi-Fi being up. */
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "maintenance UI reachable at " IPSTR,
                 IP2STR(&evt->ip_info.ip));
    }
}

static esp_err_t start_sta(const char *hostname, const char *ssid,
                           const char *pass)
{
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(netif, hostname);

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               sta_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               sta_event_handler, NULL));

    wifi_config_t cfg = { 0 };
    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "STA starting, ssid=\"%s\"", ssid);
    return ESP_OK;
}

static esp_err_t start_setup_ap(const char *hostname)
{
    s_setup_mode = true;
    esp_netif_create_default_wifi_ap();

    wifi_config_t cfg = { 0 };
    size_t len = strlen(hostname);
    if (len >= sizeof(cfg.ap.ssid)) {
        len = sizeof(cfg.ap.ssid) - 1;
    }
    memcpy(cfg.ap.ssid, hostname, len);
    cfg.ap.ssid_len = len;
    cfg.ap.max_connection = 2;
    /* Open network: it exists only long enough to hand over LAN
     * credentials, on a device with no cloud connectivity. The web UI
     * banner tells the user to finish setup promptly. */
    cfg.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGW(TAG, "no Wi-Fi credentials: setup AP \"%s\" up, UI at http://192.168.4.1/",
             hostname);
    return ESP_OK;
}

esp_err_t wifi_maintenance_start(const char *hostname)
{
    /* esp-matter has already run esp_netif_init()/esp_event_loop_create_
     * default() by the time this is called from app_main; both are
     * idempotent-safe to rely on here. */
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    char ssid[33] = { 0 };
    char pass[65] = { 0 };
    if (settings_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass)) == ESP_OK &&
        ssid[0] != '\0') {
        return start_sta(hostname, ssid, pass);
    }
    return start_setup_ap(hostname);
}
