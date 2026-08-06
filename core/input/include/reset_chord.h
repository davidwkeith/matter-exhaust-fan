/* Factory-reset chord: fan rocker + light rocker held simultaneously (either
 * direction on each) for 5 seconds — spec §5.5.
 *
 * The combined hold takes precedence over each channel's individual 450ms
 * hold-to-toggle: once both gangs are held, both rockers' pending press
 * actions are suppressed and the 5s timer runs instead.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "rocker.h"

typedef struct {
    uint32_t reset_hold_ms; /* 5000 */
    uint32_t both_since_ms;
    bool armed;
    bool fired;
} reset_chord_t;

void reset_chord_init(reset_chord_t *rc, uint32_t reset_hold_ms);

/* Call once per input-poll tick, after rocker_update() on both rockers.
 * Returns true exactly once when the chord completes (trigger the reset). */
bool reset_chord_update(reset_chord_t *rc, rocker_t *fan, rocker_t *light,
                        uint32_t now_ms);
