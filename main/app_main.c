/* Application entry point: wires the portable core modules (input state
 * machine, dimmer curves, MUA logic) to the ESP-side glue (dimmer bridge,
 * Matter endpoints, mDNS, web UI).
 *
 * All board-specific values come from the generated board_config.h — core
 * modules never contain them.
 */

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_log.h"

#include "board_config.h"
#include "channel.h"
#include "dimmer_bridge.h"
#include "dimmer_curve.h"
#include "hostname.h"
#include "matter_endpoints.h"
#include "mua.h"
#include "reset_chord.h"
#include "rocker.h"
#include "settings.h"
#include "webui.h"

static const char *TAG = "app";

#define INPUT_POLL_MS 5

static rocker_t s_fan_rocker, s_light_rocker;
static channel_t s_fan, s_light;
static reset_chord_t s_reset_chord;
static mua_t s_mua;

/* --- output plumbing ------------------------------------------------- */

static void apply_fan(void)
{
    uint8_t user = s_fan.on ? s_fan.level_percent : 0;
    uint8_t floor = settings_get_fan_min_floor(BOARD_FAN_MIN_PERCENT);
    uint8_t power = dimmer_apply_curve(BOARD_FAN_CURVE, user, floor);
    /* TODO(bring-up): execute the ramp (§5.1) — step toward `power` over
     * time instead of jumping, both for on-ramp and hold-to-off. */
    dimmer_bridge_set(DIMMER_CH_FAN, power);
    matter_report_fan_percent(user);

#if BOARD_MUA_PRESENT
    bool relay = mua_update(&s_mua, s_fan.on, user);
    gpio_set_level(BOARD_GPIO_MUA_RELAY, relay ? 1 : 0);
#endif
}

static void apply_light(void)
{
    uint8_t user = s_light.on ? s_light.level_percent : 0;
    uint8_t power = dimmer_apply_curve(BOARD_LIGHT_CURVE, user, 0);
    /* TODO(bring-up): fade instead of jump (MoveToLevelWithOnOff parity). */
    dimmer_bridge_set(DIMMER_CH_LIGHT, power);
    matter_report_light(s_light.on, user);
}

static void persist_last_used(void)
{
    settings_set_last_used("fan", s_fan.last_used_percent);
    settings_set_last_used("light", s_light.last_used_percent);
}

/* --- Matter → local -------------------------------------------------- */

static void on_matter_fan_percent(uint8_t percent)
{
    channel_set_remote(&s_fan, percent > 0, percent);
    apply_fan();
}

static void on_matter_light_state(bool on, uint8_t percent)
{
    channel_set_remote(&s_light, on, percent ? percent : s_light.level_percent);
    apply_light();
}

/* --- input ----------------------------------------------------------- */

static void input_gpio_init(void)
{
    const gpio_num_t inputs[] = {
        BOARD_GPIO_FAN_ROCKER_UP, BOARD_GPIO_FAN_ROCKER_DOWN,
        BOARD_GPIO_LIGHT_ROCKER_UP, BOARD_GPIO_LIGHT_ROCKER_DOWN,
    };
    for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
        gpio_set_direction(inputs[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(inputs[i], GPIO_PULLUP_ONLY);
    }
#if BOARD_MUA_PRESENT
    gpio_set_direction(BOARD_GPIO_MUA_RELAY, GPIO_MODE_OUTPUT);
    gpio_set_level(BOARD_GPIO_MUA_RELAY, 0);
#endif
}

/* Rockers are wired active-low (contact closes to GND, internal pull-up). */
static inline bool pressed(gpio_num_t pin)
{
    return gpio_get_level(pin) == 0;
}

static void input_task(void *arg)
{
    uint32_t now_ms = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(INPUT_POLL_MS));
        now_ms += INPUT_POLL_MS;

        rocker_event_t fan_evt = rocker_update(
            &s_fan_rocker, pressed(BOARD_GPIO_FAN_ROCKER_UP),
            pressed(BOARD_GPIO_FAN_ROCKER_DOWN), now_ms);
        rocker_event_t light_evt = rocker_update(
            &s_light_rocker, pressed(BOARD_GPIO_LIGHT_ROCKER_UP),
            pressed(BOARD_GPIO_LIGHT_ROCKER_DOWN), now_ms);

        /* Chord check AFTER rocker updates: while both gangs are held it
         * suppresses their individual tap/hold actions (§5.5). */
        if (reset_chord_update(&s_reset_chord, &s_fan_rocker, &s_light_rocker,
                               now_ms)) {
            ESP_LOGW(TAG, "factory reset chord held 5s — resetting");
            settings_factory_reset();
            matter_factory_reset(); /* erases fabrics + reboots */
        }

        channel_action_t fan_act = channel_handle_event(&s_fan, fan_evt);
        if (fan_act.type != CHANNEL_ACT_NONE) {
            apply_fan();
            persist_last_used();
        }
        channel_action_t light_act = channel_handle_event(&s_light, light_evt);
        if (light_act.type != CHANNEL_ACT_NONE) {
            apply_light();
            persist_last_used();
        }
    }
}

/* --- boot ------------------------------------------------------------ */

void app_main(void)
{
    ESP_ERROR_CHECK(settings_init());

    input_gpio_init();
    rocker_init(&s_fan_rocker, BOARD_INPUT_DEBOUNCE_MS, BOARD_INPUT_HOLD_MS);
    rocker_init(&s_light_rocker, BOARD_INPUT_DEBOUNCE_MS, BOARD_INPUT_HOLD_MS);
    reset_chord_init(&s_reset_chord, BOARD_INPUT_FACTORY_RESET_HOLD_MS);

    /* Last-used defaults to max when unset — §5.6. */
    channel_init(&s_fan, settings_get_last_used("fan", 100));
    channel_init(&s_light, settings_get_last_used("light", 100));

    mua_init(&s_mua,
             settings_get_mua_threshold(BOARD_MUA_THRESHOLD_DEFAULT),
             settings_get_mua_hysteresis(BOARD_MUA_HYSTERESIS));

    ESP_ERROR_CHECK(dimmer_bridge_init(BOARD_GPIO_DIMMER_BRIDGE_TX,
                                       BOARD_GPIO_DIMMER_BRIDGE_RX));

    static const matter_callbacks_t cbs = {
        .on_fan_percent = on_matter_fan_percent,
        .on_light_state = on_matter_light_state,
    };
    ESP_ERROR_CHECK(matter_endpoints_start(&cbs));

    /* Maintenance plane: mDNS + HTTP over Wi-Fi (never Thread) — §5.4.
     * TODO(bring-up): bring up the Wi-Fi station interface before this;
     * esp-matter owns netif init when Thread is the Matter transport. */
    ESP_ERROR_CHECK(hostname_start_mdns(BOARD_HOSTNAME_PREFIX));
    ESP_ERROR_CHECK(webui_start());

    /* TODO(bring-up): factory-fresh status indication (§5.6) — flash the
     * light channel at ~1Hz while !matter_is_commissioned() or (first flash
     * only) !settings_mua_threshold_is_set(); stop once both are satisfied. */

    xTaskCreate(input_task, "input", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "%s up (board: %s)", BOARD_HOSTNAME_PREFIX, BOARD_NAME);
}
