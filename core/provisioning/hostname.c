#include "hostname.h"

#include <stdio.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "mdns.h"

static const char *TAG = "hostname";

esp_err_t hostname_get(const char *prefix, char *buf, size_t buf_len)
{
    uint8_t mac[6];
    esp_err_t err = esp_efuse_mac_get_default(mac);
    if (err != ESP_OK) {
        return err;
    }
    int n = snprintf(buf, buf_len, "%s-%02x%02x%02x", prefix,
                     mac[3], mac[4], mac[5]);
    return (n > 0 && (size_t)n < buf_len) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t hostname_start_mdns(const char *prefix)
{
    char host[32];
    esp_err_t err = hostname_get(prefix, host, sizeof(host));
    if (err != ESP_OK) {
        return err;
    }

    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(host));
    ESP_ERROR_CHECK(mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0));
    ESP_LOGI(TAG, "mDNS up: http://%s.local/", host);

    /* Deployment note (§6.3): on a VLAN-segmented network mDNS multicast
     * won't cross subnets without a reflector — see docs/architecture.md. */
    return ESP_OK;
}
