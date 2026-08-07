/* Host tests for the portable core logic: rocker debounce/tap/hold,
 * channel interaction rules, factory-reset chord, dimmer curves, and MUA
 * hysteresis. Timing constants mirror the spec (25ms debounce, 450ms hold,
 * 5s reset chord).
 */

#include <stdio.h>
#include <stdlib.h>

#include "channel.h"
#include "dimmer_curve.h"
#include "mua.h"
#include "ramp.h"
#include "reset_chord.h"
#include "rocker.h"

#define DEBOUNCE_MS 25
#define HOLD_MS 450
#define RESET_MS 5000
#define TICK_MS 5

static int g_failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_failures++; \
    } \
} while (0)

/* Advance one rocker by duration_ms with fixed contact state, counting
 * events. Returns the last non-none event. now is shared global time. */
static uint32_t g_now;

static rocker_event_t run_rocker(rocker_t *r, bool up, bool down,
                                 uint32_t duration_ms, int *taps, int *holds)
{
    rocker_event_t last = { ROCKER_EVT_NONE, ROCKER_DIR_NONE };
    for (uint32_t t = 0; t < duration_ms; t += TICK_MS) {
        g_now += TICK_MS;
        rocker_event_t e = rocker_update(r, up, down, g_now);
        if (e.type == ROCKER_EVT_TAP && taps) (*taps)++;
        if (e.type == ROCKER_EVT_HOLD && holds) (*holds)++;
        if (e.type != ROCKER_EVT_NONE) last = e;
    }
    return last;
}

static void test_rocker_tap(void)
{
    rocker_t r;
    rocker_init(&r, DEBOUNCE_MS, HOLD_MS);
    int taps = 0, holds = 0;

    run_rocker(&r, true, false, 100, &taps, &holds);  /* short press up */
    rocker_event_t e = run_rocker(&r, false, false, 100, &taps, &holds);

    CHECK(taps == 1);
    CHECK(holds == 0);
    CHECK(e.type == ROCKER_EVT_TAP && e.dir == ROCKER_DIR_UP);
}

static void test_rocker_hold(void)
{
    rocker_t r;
    rocker_init(&r, DEBOUNCE_MS, HOLD_MS);
    int taps = 0, holds = 0;

    rocker_event_t e = run_rocker(&r, false, true, 600, &taps, &holds);
    CHECK(holds == 1); /* fires once at the threshold, not repeatedly */
    CHECK(e.type == ROCKER_EVT_HOLD && e.dir == ROCKER_DIR_DOWN);

    run_rocker(&r, false, false, 100, &taps, &holds);
    CHECK(taps == 0); /* release after hold is not also a tap */
}

static void test_rocker_debounce(void)
{
    rocker_t r;
    rocker_init(&r, DEBOUNCE_MS, HOLD_MS);
    int taps = 0, holds = 0;

    /* A 10ms glitch never debounces into a press. */
    run_rocker(&r, true, false, 10, &taps, &holds);
    run_rocker(&r, false, false, 200, &taps, &holds);
    CHECK(taps == 0 && holds == 0);

    /* Both contacts active (wiring fault) reads as released. */
    run_rocker(&r, true, true, 600, &taps, &holds);
    CHECK(taps == 0 && holds == 0);
}

static void test_channel_interaction(void)
{
    channel_t c;
    channel_init(&c, 0); /* unset last-used → defaults to max (§5.6) */
    CHECK(c.last_used_percent == 100);

    rocker_event_t hold_up = { ROCKER_EVT_HOLD, ROCKER_DIR_UP };
    rocker_event_t hold_down = { ROCKER_EVT_HOLD, ROCKER_DIR_DOWN };
    rocker_event_t tap_up = { ROCKER_EVT_TAP, ROCKER_DIR_UP };
    rocker_event_t tap_down = { ROCKER_EVT_TAP, ROCKER_DIR_DOWN };

    /* Tap while off is a no-op. */
    channel_action_t a = channel_handle_event(&c, tap_up);
    CHECK(a.type == CHANNEL_ACT_NONE && !c.on);

    /* Hold (down direction!) while off turns on toward last-used. */
    a = channel_handle_event(&c, hold_down);
    CHECK(a.type == CHANNEL_ACT_RAMP_TO && a.target_percent == 100);
    CHECK(c.on && c.level_percent == 100);

    /* Tap up at max: bounded, no wrap, no action. */
    a = channel_handle_event(&c, tap_up);
    CHECK(a.type == CHANNEL_ACT_NONE && c.level_percent == 100);

    /* Tap down steps down. */
    a = channel_handle_event(&c, tap_down);
    CHECK(a.type == CHANNEL_ACT_RAMP_TO && c.level_percent == 90);

    /* Hold while on turns off and remembers the level. */
    a = channel_handle_event(&c, hold_up);
    CHECK(a.type == CHANNEL_ACT_RAMP_OFF && !c.on);
    CHECK(c.last_used_percent == 90);

    /* Next hold-on ramps to the remembered level. */
    a = channel_handle_event(&c, hold_up);
    CHECK(a.type == CHANNEL_ACT_RAMP_TO && a.target_percent == 90);

    /* Tap down repeatedly: clamps at the minimum, never wraps/turns off. */
    for (int i = 0; i < 20; i++) {
        channel_handle_event(&c, tap_down);
    }
    CHECK(c.on && c.level_percent == CHANNEL_MIN_PERCENT);
}

static void test_reset_chord(void)
{
    rocker_t fan, light;
    reset_chord_t rc;
    rocker_init(&fan, DEBOUNCE_MS, HOLD_MS);
    rocker_init(&light, DEBOUNCE_MS, HOLD_MS);
    reset_chord_init(&rc, RESET_MS);

    int fan_holds = 0, fan_taps = 0, light_holds = 0, light_taps = 0;
    int fires = 0;

    /* Both gangs pressed together and held past 5s. */
    for (uint32_t t = 0; t < RESET_MS + 1000; t += TICK_MS) {
        g_now += TICK_MS;
        rocker_event_t fe = rocker_update(&fan, true, false, g_now);
        rocker_event_t le = rocker_update(&light, false, true, g_now);
        if (fe.type == ROCKER_EVT_HOLD) fan_holds++;
        if (fe.type == ROCKER_EVT_TAP) fan_taps++;
        if (le.type == ROCKER_EVT_HOLD) light_holds++;
        if (le.type == ROCKER_EVT_TAP) light_taps++;
        if (reset_chord_update(&rc, &fan, &light, g_now)) fires++;
    }
    /* Chord takes precedence: per-channel holds suppressed, fires once. */
    CHECK(fan_holds == 0 && light_holds == 0);
    CHECK(fires == 1);

    /* Release both — no stray taps. */
    for (uint32_t t = 0; t < 200; t += TICK_MS) {
        g_now += TICK_MS;
        rocker_event_t fe = rocker_update(&fan, false, false, g_now);
        rocker_event_t le = rocker_update(&light, false, false, g_now);
        if (fe.type == ROCKER_EVT_TAP) fan_taps++;
        if (le.type == ROCKER_EVT_TAP) light_taps++;
        reset_chord_update(&rc, &fan, &light, g_now);
    }
    CHECK(fan_taps == 0 && light_taps == 0);

    /* A short both-press (under 5s) must NOT fire. */
    fires = 0;
    for (uint32_t t = 0; t < 1000; t += TICK_MS) {
        g_now += TICK_MS;
        rocker_update(&fan, true, false, g_now);
        rocker_update(&light, true, false, g_now);
        if (reset_chord_update(&rc, &fan, &light, g_now)) fires++;
    }
    for (uint32_t t = 0; t < 200; t += TICK_MS) {
        g_now += TICK_MS;
        rocker_update(&fan, false, false, g_now);
        rocker_update(&light, false, false, g_now);
        reset_chord_update(&rc, &fan, &light, g_now);
    }
    CHECK(fires == 0);
}

static void test_dimmer_curves(void)
{
    /* Off is off, regardless of curve or floor. */
    CHECK(dimmer_apply_curve(DIMMER_CURVE_LINEAR, 0, 30) == 0);
    CHECK(dimmer_apply_curve(DIMMER_CURVE_LOGARITHMIC, 0, 0) == 0);

    /* Fan linear with 30% stall floor: nonzero input never lands below the
     * floor; full scale reaches 100. */
    CHECK(dimmer_apply_curve(DIMMER_CURVE_LINEAR, 1, 30) >= 30);
    CHECK(dimmer_apply_curve(DIMMER_CURVE_LINEAR, 100, 30) == 100);

    /* Light log curve: monotonic non-decreasing, full scale reaches 100,
     * low end compressed relative to linear (perceptual). */
    uint8_t prev = 0;
    for (int u = 1; u <= 100; u++) {
        uint8_t v = dimmer_apply_curve(DIMMER_CURVE_LOGARITHMIC, (uint8_t)u, 0);
        CHECK(v >= prev);
        prev = v;
    }
    CHECK(dimmer_apply_curve(DIMMER_CURVE_LOGARITHMIC, 100, 0) == 100);
    CHECK(dimmer_apply_curve(DIMMER_CURVE_LOGARITHMIC, 50, 0) <
          dimmer_apply_curve(DIMMER_CURVE_LINEAR, 50, 0));
}

static void test_mua_hysteresis(void)
{
    mua_t m;
    mua_init(&m, 60, 4);

    CHECK(mua_update(&m, true, 50) == false);
    CHECK(mua_update(&m, true, 60) == true);   /* energizes at threshold */
    CHECK(mua_update(&m, true, 57) == true);   /* inside band: stays on */
    CHECK(mua_update(&m, true, 56) == true);   /* threshold - hyst: still on */
    CHECK(mua_update(&m, true, 55) == false);  /* below band: drops out */
    CHECK(mua_update(&m, true, 59) == false);  /* re-energize needs >= 60 */
    CHECK(mua_update(&m, true, 60) == true);
    CHECK(mua_update(&m, false, 100) == false); /* fan off: always open */
}

static void test_ramp(void)
{
    ramp_t r;
    ramp_init(&r, 40); /* 40ms per 1% */

    /* Nothing happens before the first step interval elapses. */
    ramp_set_target(&r, 50, 1000);
    CHECK(ramp_active(&r));
    CHECK(ramp_tick(&r, 1039) == false && r.current_percent == 0);
    CHECK(ramp_tick(&r, 1040) == true && r.current_percent == 1);

    /* A slow poll loop catches up: elapsed time covers multiple steps. */
    CHECK(ramp_tick(&r, 1440) == true && r.current_percent == 11);

    /* Ramp completes exactly at the target and goes inactive. */
    CHECK(ramp_tick(&r, 10000) == true && r.current_percent == 50);
    CHECK(!ramp_active(&r));
    CHECK(ramp_tick(&r, 20000) == false);

    /* Downward ramp (hold-to-off). */
    ramp_set_target(&r, 0, 20000);
    CHECK(ramp_tick(&r, 20040) == true && r.current_percent == 49);
    CHECK(ramp_tick(&r, 30000) == true && r.current_percent == 0);

    /* Remote writes apply immediately. */
    ramp_jump_to(&r, 75);
    CHECK(r.current_percent == 75 && !ramp_active(&r));
}

int main(void)
{
    test_rocker_tap();
    test_rocker_hold();
    test_rocker_debounce();
    test_channel_interaction();
    test_reset_chord();
    test_dimmer_curves();
    test_mua_hysteresis();
    test_ramp();

    if (g_failures) {
        fprintf(stderr, "%d check(s) FAILED\n", g_failures);
        return EXIT_FAILURE;
    }
    printf("all tests passed\n");
    return EXIT_SUCCESS;
}
