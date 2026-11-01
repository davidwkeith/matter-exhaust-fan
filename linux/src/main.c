/* exhaustd — Raspberry Pi 3 controller entry point. The Linux counterpart
 * of main/app_main.c: wires the portable core modules (rocker state
 * machine, ramp engine, dimmer curves, MUA logic) to Linux I/O (GPIO
 * character device, tty dimmer bridge, file-backed settings).
 *
 * Single-threaded: one 5ms poll loop does input, ramps, the status blink
 * and the systemd watchdog ping, so no state is shared across threads.
 *
 * All board-specific values come from the generated board_config.h.
 */

#define _DEFAULT_SOURCE

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "board_config.h"
#include "bridge.h"
#include "channel.h"
#include "dimmer_curve.h"
#include "gpio.h"
#include "hostname.h"
#include "log.h"
#include "matter.h"
#include "mua.h"
#include "ramp.h"
#include "reset_chord.h"
#include "rocker.h"
#include "sd_notify.h"
#include "settings.h"

static const char *TAG = "exhaustd";

#define INPUT_POLL_MS 5
#define STATUS_BLINK_HALF_PERIOD_MS 500 /* ~1Hz (§5.6) */
#define STATUS_BLINK_LEVEL_PERCENT 60
#define WATCHDOG_PING_MS 1000

/* Bit order of the GPIO input request. */
enum { IN_FAN_UP, IN_FAN_DOWN, IN_LIGHT_UP, IN_LIGHT_DOWN, IN_COUNT };

static gpio_lines_t s_gpio;
static rocker_t s_fan_rocker, s_light_rocker;
static channel_t s_fan, s_light;
static reset_chord_t s_reset_chord;
static mua_t s_mua;
static ramp_t s_fan_ramp, s_light_ramp;
static bool s_status_blinking;
static bool s_remote_dirty;
static volatile sig_atomic_t s_stop;

/* --- output plumbing ------------------------------------------------- */

static void push_fan_output(uint8_t user_percent)
{
    uint8_t floor = settings_get_fan_min_floor(BOARD_FAN_MIN_PERCENT);
    bridge_set(BRIDGE_CH_FAN,
               dimmer_apply_curve(BOARD_FAN_CURVE, user_percent, floor));
#if BOARD_MUA_PRESENT
    gpio_set_relay(&s_gpio, mua_update(&s_mua, user_percent > 0, user_percent));
#endif
}

static void push_light_output(uint8_t user_percent)
{
    /* The status blink owns the light output while active (§5.6). */
    if (s_status_blinking) {
        return;
    }
    bridge_set(BRIDGE_CH_LIGHT,
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

/* --- input ----------------------------------------------------------- */

static void input_tick(uint32_t now_ms)
{
    unsigned bits;
    if (gpio_read_inputs(&s_gpio, &bits) != 0) {
        return;
    }
    /* Rockers are wired active-low (contact closes to GND, pull-up). */
#define PRESSED(i) (((bits >> (i)) & 1u) == 0)
    rocker_event_t fan_evt = rocker_update(
        &s_fan_rocker, PRESSED(IN_FAN_UP), PRESSED(IN_FAN_DOWN), now_ms);
    rocker_event_t light_evt = rocker_update(
        &s_light_rocker, PRESSED(IN_LIGHT_UP), PRESSED(IN_LIGHT_DOWN), now_ms);
#undef PRESSED

    /* Chord check AFTER rocker updates: while both gangs are held it
     * suppresses their individual tap/hold actions (§5.5). */
    if (reset_chord_update(&s_reset_chord, &s_fan_rocker, &s_light_rocker,
                           now_ms)) {
        LOGW(TAG, "factory reset chord held — resetting");
        settings_factory_reset();
        matter_factory_reset();
        s_stop = 1; /* systemd (Restart=always) brings us back up fresh */
        return;
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

/* --- factory-fresh status indication (§5.6) --------------------------- */
/* While configuration is required, flash the light channel at ~1Hz. Runs
 * only while the light channel is off so user interaction always wins. */

static void status_blink_tick(void)
{
    static bool lit;
    bool needs_config =
        !matter_is_commissioned() || !settings_mua_threshold_is_set();

    if (needs_config && !s_light.on) {
        s_status_blinking = true;
        lit = !lit;
        bridge_set(BRIDGE_CH_LIGHT,
                   lit ? dimmer_apply_curve(BOARD_LIGHT_CURVE,
                                            STATUS_BLINK_LEVEL_PERCENT, 0)
                       : 0);
    } else if (s_status_blinking) {
        /* Condition cleared (or user turned the light on): hand the output
         * back to the ramp engine. */
        s_status_blinking = false;
        lit = false;
        bridge_set(BRIDGE_CH_LIGHT,
                   dimmer_apply_curve(BOARD_LIGHT_CURVE,
                                      s_light_ramp.current_percent, 0));
    }
}

/* --- boot / loop ----------------------------------------------------- */

static void on_signal(int sig)
{
    (void)sig;
    s_stop = 1;
}

/* Signed: negative when t is before t0. */
static long long diff_ms(const struct timespec *t0, const struct timespec *t)
{
    return (long long)(t->tv_sec - t0->tv_sec) * 1000 +
           (t->tv_nsec - t0->tv_nsec) / 1000000;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [--gpiochip PATH] [--uart PATH] [--state-dir DIR] "
            "[--iface IF]\n"
            "  defaults: /dev/gpiochip0 /dev/serial0 /var/lib/exhaustd wlan0\n",
            argv0);
}

int main(int argc, char **argv)
{
    const char *chip = "/dev/gpiochip0";
    const char *uart = "/dev/serial0";
    const char *state_dir = "/var/lib/exhaustd";
    const char *iface = "wlan0";

    for (int i = 1; i < argc; i++) {
        const char **opt = NULL;
        if (strcmp(argv[i], "--gpiochip") == 0) opt = &chip;
        else if (strcmp(argv[i], "--uart") == 0) opt = &uart;
        else if (strcmp(argv[i], "--state-dir") == 0) opt = &state_dir;
        else if (strcmp(argv[i], "--iface") == 0) opt = &iface;
        if (!opt || i + 1 >= argc) {
            usage(argv[0]);
            return strcmp(argv[i], "--help") == 0 ? EXIT_SUCCESS : 2;
        }
        *opt = argv[++i];
    }
    setvbuf(stderr, NULL, _IOLBF, 0);

    if (settings_init(state_dir) != 0) {
        return EXIT_FAILURE;
    }

    const unsigned inputs[IN_COUNT] = {
        [IN_FAN_UP] = BOARD_GPIO_FAN_ROCKER_UP,
        [IN_FAN_DOWN] = BOARD_GPIO_FAN_ROCKER_DOWN,
        [IN_LIGHT_UP] = BOARD_GPIO_LIGHT_ROCKER_UP,
        [IN_LIGHT_DOWN] = BOARD_GPIO_LIGHT_ROCKER_DOWN,
    };
    int relay = BOARD_MUA_PRESENT ? BOARD_GPIO_MUA_RELAY : -1;
    if (gpio_open(&s_gpio, chip, inputs, IN_COUNT, relay) != 0) {
        return EXIT_FAILURE;
    }
    if (bridge_open(uart) != 0) {
        gpio_close(&s_gpio);
        return EXIT_FAILURE;
    }

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

    /* Boot state is off: whatever the bridge held before a restart, the
     * outputs now match the channel state. */
    push_fan_output(0);
    push_light_output(0);

    static const matter_callbacks_t cbs = {
        .on_fan_percent = on_matter_fan_percent,
        .on_light_state = on_matter_light_state,
    };
    matter_start(&cbs);

    struct sigaction sa = { .sa_handler = on_signal };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    char host[32] = "unknown";
    hostname_get(BOARD_HOSTNAME_PREFIX, iface, host, sizeof(host));
    LOGI(TAG, "%s up (board: %s)", host, BOARD_NAME);
    sd_notify_send("READY=1");

    struct timespec t0, next;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    next = t0;
    uint32_t last_blink_ms = 0, last_ping_ms = 0;

    while (!s_stop) {
        next.tv_nsec += INPUT_POLL_MS * 1000000L;
        if (next.tv_nsec >= 1000000000L) {
            next.tv_nsec -= 1000000000L;
            next.tv_sec++;
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint32_t now_ms = (uint32_t)diff_ms(&t0, &now);
        /* Fell behind (e.g. a slow SD write): resync instead of bursting
         * through missed ticks. */
        if (diff_ms(&next, &now) > 10 * INPUT_POLL_MS) {
            next = now;
        }

        input_tick(now_ms);

        if (now_ms - last_blink_ms >= STATUS_BLINK_HALF_PERIOD_MS) {
            last_blink_ms = now_ms;
            status_blink_tick();
        }
        if (now_ms - last_ping_ms >= WATCHDOG_PING_MS) {
            last_ping_ms = now_ms;
            sd_notify_send("WATCHDOG=1");
        }
    }

    sd_notify_send("STOPPING=1");
    /* Stop the fan before releasing the relay line: the relay must never
     * open (MUA off) while the fan is still pulling air. */
    bridge_set(BRIDGE_CH_FAN, 0);
    gpio_set_relay(&s_gpio, false);
    bridge_close();
    gpio_close(&s_gpio);
    LOGI(TAG, "stopped");
    return EXIT_SUCCESS;
}
