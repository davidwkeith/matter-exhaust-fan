/* Momentary SPDT center-off rocker input: debounce + tap/hold classification.
 *
 * Portable C — no ESP-IDF dependency. The caller samples the two GPIO lines
 * and feeds them to rocker_update() with a millisecond timestamp; this module
 * emits TAP (released before the hold threshold) and HOLD (threshold crossed,
 * fires once per press, at the threshold — not at release, so hold-to-off
 * starts acting while the finger is still down).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ROCKER_DIR_NONE = 0,
    ROCKER_DIR_UP   = 1,
    ROCKER_DIR_DOWN = -1,
} rocker_dir_t;

typedef enum {
    ROCKER_EVT_NONE = 0,
    ROCKER_EVT_TAP,   /* released before hold_ms */
    ROCKER_EVT_HOLD,  /* held past hold_ms; fires once per press */
} rocker_evt_type_t;

typedef struct {
    rocker_evt_type_t type;
    rocker_dir_t dir;
} rocker_event_t;

typedef struct {
    /* config */
    uint32_t debounce_ms;
    uint32_t hold_ms;
    /* debounce */
    rocker_dir_t raw_dir;
    uint32_t raw_since_ms;
    /* debounced press tracking */
    rocker_dir_t stable_dir;
    uint32_t press_start_ms;
    bool hold_fired;
    bool suppressed; /* chord logic swallowed this press (factory reset) */
} rocker_t;

void rocker_init(rocker_t *r, uint32_t debounce_ms, uint32_t hold_ms);

/* Feed current (raw) contact states; returns at most one event per call. */
rocker_event_t rocker_update(rocker_t *r, bool up, bool down, uint32_t now_ms);

/* Debounced "a direction is currently pressed" — used by the reset chord. */
bool rocker_pressed(const rocker_t *r);

/* Swallow the current press: no TAP on release, no HOLD at threshold.
 * Called by the reset chord when both gangs are held together (§5.5). */
void rocker_suppress(rocker_t *r);
