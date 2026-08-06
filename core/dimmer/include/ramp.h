/* Ramp/fade engine — executes the transitions the channel state machine
 * requests (§5.1): fan ramps toward last-used speed, light fades toward
 * last-used brightness, and hold-to-off ramps/fades down to 0.
 *
 * Operates in user-percent space; the caller maps the ramped value through
 * the dimmer curve on every change. Portable C — no ESP-IDF dependency.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t current_percent;
    uint8_t target_percent;
    uint32_t step_interval_ms; /* time per 1% step */
    uint32_t last_step_ms;
} ramp_t;

void ramp_init(ramp_t *r, uint32_t step_interval_ms);

/* Change the target; ramping starts from the current value. `jump` skips
 * the ramp entirely (used when a remote write should apply immediately). */
void ramp_set_target(ramp_t *r, uint8_t target_percent, uint32_t now_ms);
void ramp_jump_to(ramp_t *r, uint8_t percent);

/* Advance toward the target; returns true when current_percent changed
 * this tick (caller should push the new value to the dimmer). */
bool ramp_tick(ramp_t *r, uint32_t now_ms);

bool ramp_active(const ramp_t *r);
