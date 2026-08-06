#include "settings.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

/* Calibration lives in its own namespace so factory reset can erase the
 * user namespace wholesale and leave calibration untouched. */
#define NS_CAL  "hwcal"
#define NS_USER "usercfg"

static const char *TAG = "settings";

esp_err_t settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static uint8_t get_u8(const char *ns, const char *key, uint8_t fallback)
{
    nvs_handle_t h;
    uint8_t v = fallback;
    if (nvs_open(ns, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_u8(h, key, &v) != ESP_OK) {
            v = fallback;
        }
        nvs_close(h);
    }
    return v;
}

static esp_err_t set_u8(const char *ns, const char *key, uint8_t v)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(h, key, v);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

static esp_err_t get_str(const char *ns, const char *key, char *buf, size_t len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_get_str(h, key, buf, &len);
    nvs_close(h);
    return err;
}

static esp_err_t set_str(const char *ns, const char *key, const char *val)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, key, val);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

/* --- hardware calibration --- */

uint8_t settings_get_mua_threshold(uint8_t d) { return get_u8(NS_CAL, "mua_thr", d); }
esp_err_t settings_set_mua_threshold(uint8_t p) { return set_u8(NS_CAL, "mua_thr", p); }
uint8_t settings_get_mua_hysteresis(uint8_t d) { return get_u8(NS_CAL, "mua_hyst", d); }
esp_err_t settings_set_mua_hysteresis(uint8_t p) { return set_u8(NS_CAL, "mua_hyst", p); }
uint8_t settings_get_fan_min_floor(uint8_t d) { return get_u8(NS_CAL, "fan_floor", d); }
esp_err_t settings_set_fan_min_floor(uint8_t p) { return set_u8(NS_CAL, "fan_floor", p); }

bool settings_mua_threshold_is_set(void)
{
    nvs_handle_t h;
    bool set = false;
    if (nvs_open(NS_CAL, NVS_READONLY, &h) == ESP_OK) {
        uint8_t v;
        set = (nvs_get_u8(h, "mua_thr", &v) == ESP_OK);
        nvs_close(h);
    }
    return set;
}

/* --- user state --- */

uint8_t settings_get_last_used(const char *channel, uint8_t d)
{
    char key[16];
    snprintf(key, sizeof(key), "last_%s", channel);
    return get_u8(NS_USER, key, d);
}

esp_err_t settings_set_last_used(const char *channel, uint8_t p)
{
    char key[16];
    snprintf(key, sizeof(key), "last_%s", channel);
    return set_u8(NS_USER, key, p);
}

esp_err_t settings_get_hostname_label(char *buf, size_t len)
{
    return get_str(NS_USER, "host_label", buf, len);
}

esp_err_t settings_set_hostname_label(const char *label)
{
    return set_str(NS_USER, "host_label", label);
}

esp_err_t settings_get_webui_pass_hash(char *buf, size_t len)
{
    return get_str(NS_USER, "pass_hash", buf, len);
}

esp_err_t settings_set_webui_pass_hash(const char *hash)
{
    return set_str(NS_USER, "pass_hash", hash);
}

esp_err_t settings_get_wifi(char *ssid, size_t ssid_len,
                            char *pass, size_t pass_len)
{
    esp_err_t err = get_str(NS_USER, "wifi_ssid", ssid, ssid_len);
    if (err != ESP_OK) {
        return err;
    }
    /* Open networks store an empty password. */
    err = get_str(NS_USER, "wifi_pass", pass, pass_len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        pass[0] = '\0';
        err = ESP_OK;
    }
    return err;
}

esp_err_t settings_set_wifi(const char *ssid, const char *pass)
{
    esp_err_t err = set_str(NS_USER, "wifi_ssid", ssid);
    if (err != ESP_OK) {
        return err;
    }
    return set_str(NS_USER, "wifi_pass", pass ? pass : "");
}

/* --- factory reset --- */

esp_err_t settings_factory_reset(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_USER, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_erase_all(h);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    ESP_LOGW(TAG, "factory reset: user settings erased, calibration preserved");
    /* Matter fabric/commissioning data is erased by the caller via
     * esp_matter::factory_reset(), which also reboots — see core/matter. */
    return err;
}
