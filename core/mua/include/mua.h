/* Makeup-air (MUA) interlock — spec §5.3.
 *
 * The relay energizes whenever the fan is on AND at/above the calibrated
 * threshold percent (the phase-cut setting that produces the CA Mechanical
 * Code 505.2 threshold of 400 CFM on the installed fan). A hysteresis band
 * below the threshold prevents relay chatter from tap-stepping near the
 * boundary. Threshold and hysteresis live in NVS, never in firmware — the
 * relay is local behavior tied to PercentCurrent, not a Matter endpoint.
 *
 * Portable C — the caller drives the actual relay GPIO.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t threshold_percent;
    uint8_t hysteresis_percent;
    bool energized;
} mua_t;

void mua_init(mua_t *m, uint8_t threshold_percent, uint8_t hysteresis_percent);

/* Evaluate against current fan state; returns desired relay state
 * (true = energized / contact closed). */
bool mua_update(mua_t *m, bool fan_on, uint8_t fan_percent);
