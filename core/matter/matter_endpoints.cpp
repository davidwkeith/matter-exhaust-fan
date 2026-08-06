/* esp-matter glue for the two endpoints described in matter_endpoints.h.
 *
 * TODO(bring-up): this is scaffolding — the endpoint creation below follows
 * the esp-matter API shape but has not yet been compiled against a specific
 * esp-matter release. Validate against the pinned esp-matter version during
 * hardware bring-up.
 */

#include "matter_endpoints.h"

#include "esp_log.h"

#include <esp_matter.h>
#include <esp_matter_endpoint.h>

static const char *TAG = "matter";

using namespace esp_matter;
using namespace esp_matter::endpoint;

static matter_callbacks_t s_cbs;
static uint16_t s_fan_endpoint_id = 0;
static uint16_t s_light_endpoint_id = 0;

static esp_err_t app_attribute_update_cb(attribute::callback_type_t type,
                                         uint16_t endpoint_id,
                                         uint32_t cluster_id,
                                         uint32_t attribute_id,
                                         esp_matter_attr_val_t *val,
                                         void *priv_data)
{
    if (type != attribute::PRE_UPDATE) {
        return ESP_OK;
    }

    if (endpoint_id == s_fan_endpoint_id &&
        cluster_id == chip::app::Clusters::FanControl::Id &&
        attribute_id == chip::app::Clusters::FanControl::Attributes::PercentSetting::Id) {
        if (s_cbs.on_fan_percent) {
            s_cbs.on_fan_percent(val->val.u8);
        }
    } else if (endpoint_id == s_light_endpoint_id) {
        if (cluster_id == chip::app::Clusters::OnOff::Id &&
            attribute_id == chip::app::Clusters::OnOff::Attributes::OnOff::Id) {
            if (s_cbs.on_light_state) {
                s_cbs.on_light_state(val->val.b, 0 /* keep level */);
            }
        } else if (cluster_id == chip::app::Clusters::LevelControl::Id &&
                   attribute_id == chip::app::Clusters::LevelControl::Attributes::CurrentLevel::Id) {
            if (s_cbs.on_light_state) {
                /* Matter level is 1..254; map to percent. */
                s_cbs.on_light_state(true, (uint8_t)((val->val.u8 * 100) / 254));
            }
        }
    }
    return ESP_OK;
}

extern "C" esp_err_t matter_endpoints_start(const matter_callbacks_t *cbs)
{
    s_cbs = *cbs;

    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, nullptr);
    if (!node) {
        ESP_LOGE(TAG, "node creation failed");
        return ESP_FAIL;
    }

    /* Endpoint 1 — Fan Control (0x0202), continuous PercentSetting only. */
    fan::config_t fan_config;
    endpoint_t *fan_ep = fan::create(node, &fan_config, ENDPOINT_FLAG_NONE, nullptr);
    if (!fan_ep) {
        ESP_LOGE(TAG, "fan endpoint creation failed");
        return ESP_FAIL;
    }
    s_fan_endpoint_id = endpoint::get_id(fan_ep);

    /* Endpoint 2 — dimmable light (On/Off + Level Control). */
    dimmable_light::config_t light_config;
    endpoint_t *light_ep =
        dimmable_light::create(node, &light_config, ENDPOINT_FLAG_NONE, nullptr);
    if (!light_ep) {
        ESP_LOGE(TAG, "light endpoint creation failed");
        return ESP_FAIL;
    }
    s_light_endpoint_id = endpoint::get_id(light_ep);

    esp_err_t err = esp_matter::start(nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_matter::start failed: %d", err);
        return err;
    }

    ESP_LOGI(TAG, "Matter up: fan=EP%u light=EP%u (Thread transport)",
             s_fan_endpoint_id, s_light_endpoint_id);
    return ESP_OK;
}

extern "C" esp_err_t matter_report_fan_percent(uint8_t percent)
{
    esp_matter_attr_val_t val = esp_matter_uint8(percent);
    return attribute::update(
        s_fan_endpoint_id, chip::app::Clusters::FanControl::Id,
        chip::app::Clusters::FanControl::Attributes::PercentCurrent::Id, &val);
}

extern "C" esp_err_t matter_report_light(bool on, uint8_t percent)
{
    esp_matter_attr_val_t on_val = esp_matter_bool(on);
    esp_err_t err = attribute::update(
        s_light_endpoint_id, chip::app::Clusters::OnOff::Id,
        chip::app::Clusters::OnOff::Attributes::OnOff::Id, &on_val);
    if (err != ESP_OK || !on) {
        return err;
    }
    esp_matter_attr_val_t lvl = esp_matter_uint8((uint8_t)((percent * 254) / 100));
    return attribute::update(
        s_light_endpoint_id, chip::app::Clusters::LevelControl::Id,
        chip::app::Clusters::LevelControl::Attributes::CurrentLevel::Id, &lvl);
}

extern "C" bool matter_is_commissioned(void)
{
    return chip::Server::GetInstance().GetFabricTable().FabricCount() > 0;
}

extern "C" void matter_factory_reset(void)
{
    /* Erases Matter NVS (fabrics, commissioning) and reboots. Hardware
     * calibration survives in its own namespace — see core/settings. */
    esp_matter::factory_reset();
}
