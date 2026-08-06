/* Application entry point: wires the portable core modules (input state
 * machine, ramp engine, dimmer curves, MUA logic) to the ESP-side glue
 * (dimmer bridge, Matter endpoints, Wi-Fi/mDNS maintenance plane, web UI).
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
#include "ramp.h"
#include "reset_chord.h"
#include "rocker.h"
#include "settings.h"
#include "webui.h"
#include "wifi_maintenance.h"

static const char *TAG = "app";

#define INPUT_POLL_MS 5
#define STATUS_BLINK_HALF_PERIOD_MS 500 /* ~1Hz (§5.6) */
#define STATUS_BLINK_LEVEL_PERCENT 60

static rocker_t s_fan_rocker, s_light_rocker;
static channel_t s_fan, s_light;
static reset_chord_t s_reset_chord;
static mua_t s_mua;
static ramp_t s_fan_ramp, s_light_ramp;
static volatile bool s_status_blinking;

/* --- output plumbing ------------------------------------------------- */

/* Push the fan's current (possibly mid-ramp) user level to the hardware:
 * curve → bridge, then the MUA interlock against the level actually
 * being delivered. */
static void push_fan_output(uint8_t user_percent)
{
    uint8_t floor = settings_get_fan_min_floor(BOARD_FAN_MIN_PERCENT);
    dimmer_bridge_set(DIMMER_CH_FAN,
                      dimmer_apply_curve(BOARD_FAN_CURVE, user_percent, floor));
#if BOARD_MUA_PRESENT
    bool relay = mua_update(&s_mua, user_percent > 0, user_percent);
    gpio_set_level(BOARD_GPIO_MUA_RELAY, relay ? 1 : 0);
#endif
}

static void push_light_output(uint8_t user_percent)
{
    /* The status blink owns the light output while active (§5.6); it only
     * runs while the light channel is off, so there's nothing to clobber. */
    if (s_status_blinking) {
        return;
    }
    dimmer_bridge_set(DIMMER_CH_LIGHT,
                      dimmer_apply_curve(BOARD_LIGHT_CURVE, user_percent, 0));
}

static void persist_last_used(void)
{
    settings_set_last_used("fan", s_fan.last_used_percent);
    settings_set_last_used("light", s_light.last_used_percent);
}

static void start_fan_transition(uint32_t now_ms)
{
    ramp_set_target(&s_fan_ramp, s_fan.on ? s_fan.level_percent : 0, now_ms);
    matter_report_fan_percent(s_fan.on ? s_fan.level_percent : 0);
}

static void start_light_transition(uint32_t now_ms)
{
    ramp_set_target(&s_light_ramp, s_light.on ? s_light.level_percent : 0,
                    now_ms);
    matter_report_light(s_light.on, s_light.level_percent);
}

/* --- Matter → local -------------------------------------------------- */
/* Remote writes also ramp/fade (MoveToLevelWithOnOff parity) — the ramp
 * targets are read by the input task on its next tick. */

static volatile bool s_remote_dirty;

static void on_matter_fan_percent(uint8_t percent)
{
    channel_set_remote(&s_fan, percent > 0, percent);
    s_remote_dirty = true;
}

static void on_matter_light_state(bool on, uint8_t percent)
{
    channel_set_remote(&s_light, on, percent ? percent : s_light.level_percent);
    s_remote_dirty = true;
}

/* --- web UI → local -------------------------------------------------- */

static void on_calibration_changed(void)
{
    /* u8 re-init racing the input task's reads is benign (single-byte
     * fields, transient one-tick inconsistency at worst). */
    mua_init(&s_mua,
             settings_get_mua_threshold(BOARD_MUA_THRESHOLD_DEFAULT),
             settings_get_mua_hysteresis(BOARD_MUA_HYSTERESIS));
    ESP_LOGI(TAG, "MUA re-armed from updated calibration");
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
    (void)arg;
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

        if (channel_handle_event(&s_fan, fan_evt).type != CHANNEL_ACT_NONE) {
            start_fan_transition(now_ms);
            persist_last_used();
        }
        if (channel_handle_event(&s_light, light_evt).type != CHANNEL_ACT_NONE) {
            start_light_transition(now_ms);
            persist_last_used();
        }

        if (s_remote_dirty) {
            s_remote_dirty = false;
            start_fan_transition(now_ms);
            start_light_transition(now_ms);
        }

        if (ramp_tick(&s_fan_ramp, now_ms)) {
            push_fan_output(s_fan_ramp.current_percent);
        }
        if (ramp_tick(&s_light_ramp, now_ms)) {
            push_light_output(s_light_ramp.current_percent);
        }
    }
}

/* --- factory-fresh status indication (§5.6) --------------------------- */
/* While configuration is required (not commissioned, or — first flash only
 * — MUA threshold unset), flash the light channel at ~1Hz. Reuses the
 * light output; no dedicated status LED. Runs only while the light channel
 * is off so user interaction always wins. */

static void status_blink_task(void *arg)
{
    (void)arg;
    bool lit = false;

    for (;;) {
        bool needs_config =
            !matter_is_commissioned() || !settings_mua_threshold_is_set();

        if (needs_config && !s_light.on) {
            s_status_blinking = true;
            lit = !lit;
            dimmer_bridge_set(
                DIMMER_CH_LIGHT,
                lit ? dimmer_apply_curve(BOARD_LIGHT_CURVE,
                                         STATUS_BLINK_LEVEL_PERCENT, 0)
                    : 0);
        } else if (s_status_blinking) {
            /* Condition cleared (or user turned the light on): hand the
             * output back to the ramp engine. */
            s_status_blinking = false;
            lit = false;
            dimmer_bridge_set(DIMMER_CH_LIGHT,
                              dimmer_apply_curve(BOARD_LIGHT_CURVE,
                                                 s_light_ramp.current_percent,
                                                 0));
        }
        vTaskDelay(pdMS_TO_TICKS(STATUS_BLINK_HALF_PERIOD_MS));
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

    ramp_init(&s_fan_ramp, BOARD_FAN_RAMP_MS_PER_PERCENT);
    ramp_init(&s_light_ramp, BOARD_LIGHT_FADE_MS_PER_PERCENT);

    ESP_ERROR_CHECK(dimmer_bridge_init(BOARD_GPIO_DIMMER_BRIDGE_TX,
                                       BOARD_GPIO_DIMMER_BRIDGE_RX));

    static const matter_callbacks_t cbs = {
        .on_fan_percent = on_matter_fan_percent,
        .on_light_state = on_matter_light_state,
    };
    ESP_ERROR_CHECK(matter_endpoints_start(&cbs));

    /* Maintenance plane: Wi-Fi (STA, or setup AP when unconfigured) +
     * mDNS + HTTP. Never Thread — §5.4. */
    char host[32];
    ESP_ERROR_CHECK(hostname_get(BOARD_HOSTNAME_PREFIX, host, sizeof(host)));
    ESP_ERROR_CHECK(wifi_maintenance_start(host));
    ESP_ERROR_CHECK(hostname_start_mdns(BOARD_HOSTNAME_PREFIX));

    const webui_config_t webui_cfg = {
        .mua_threshold_default = BOARD_MUA_THRESHOLD_DEFAULT,
        .mua_hysteresis_default = BOARD_MUA_HYSTERESIS,
        .fan_floor_default = BOARD_FAN_MIN_PERCENT,
        .on_calibration_changed = on_calibration_changed,
    };
    ESP_ERROR_CHECK(webui_start(&webui_cfg));

    xTaskCreate(status_blink_task, "status", 3072, NULL, 3, NULL);
    xTaskCreate(input_task, "input", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "%s up (board: %s)", host, BOARD_NAME);
}
