/* Per-channel (fan or light) interaction logic — spec §5.1.
 *
 *   OFF + hold (either direction) → on, ramp toward last-used level
 *   ON  + tap                     → step one increment (bounded, no wrap)
 *   ON  + hold (either direction) → ramp/fade down to off
 *
 * Portable C. This module owns the *target* state; ramp/fade execution and
 * the dimmer-curve mapping happen downstream (core/dimmer + main loop).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "rocker.h"

#define CHANNEL_STEP_PERCENT_DEFAULT 10
/* Lowest tap-steppable user level. User-space percent; the per-motor
 * phase-cut floor is applied later by the dimmer curve (§5.2). */
#define CHANNEL_MIN_PERCENT 10
#define CHANNEL_MAX_PERCENT 100

typedef enum {
    CHANNEL_ACT_NONE = 0,
    CHANNEL_ACT_RAMP_TO,  /* ramp/fade to .target_percent */
    CHANNEL_ACT_RAMP_OFF, /* ramp/fade down to off */
} channel_action_type_t;

typedef struct {
    channel_action_type_t type;
    uint8_t target_percent;
} channel_action_t;

typedef struct {
    bool on;
    uint8_t level_percent; /* current user level while on */
    /* Ramp-on target. Starts at max: with no last-used memory after a
     * factory reset, "last used" is max by definition (§5.6). */
    uint8_t last_used_percent;
    uint8_t step_percent;
} channel_t;

void channel_init(channel_t *c, uint8_t last_used_percent);

/* Apply a rocker event; returns the action the caller must execute. */
channel_action_t channel_handle_event(channel_t *c, rocker_event_t evt);

/* Remote (Matter) writes land here so local state stays coherent.
 * level_percent == 0 with on == true is ignored. */
void channel_set_remote(channel_t *c, bool on, uint8_t level_percent);
