/* Dimmer curves — spec §5.2.
 *
 * Leading-edge (fan/TRIAC) and trailing-edge (light/MOSFET) phase-cut use
 * inverted timing logic relative to each other; the same percent value lands
 * at a different point in the AC half-cycle on each channel. That inversion
 * lives in the dimmer bridge inside the metal enclosure — these functions map
 * *user* percent (1..100) to the *power* percent sent over the bridge bus,
 * and the two channels deliberately do not share a lookup table.
 *
 * Portable C — no ESP-IDF dependency.
 */
#pragma once

#include <stdint.h>

typedef enum {
    DIMMER_CURVE_LINEAR = 0,      /* motor-appropriate linear power curve */
    DIMMER_CURVE_LOGARITHMIC = 1, /* LED-appropriate perceptual curve */
} dimmer_curve_t;

/* Map user percent to output power percent. 0 always maps to 0 (off).
 *
 * min_floor_percent is the calibrated minimum for the load — for the fan
 * this is the phase-cut level below which the motor stalls/hums instead of
 * rotating (commonly 25–35%); nonzero user input never maps below it.
 * Pass 0 for loads with no floor (light). */
uint8_t dimmer_apply_curve(dimmer_curve_t curve, uint8_t user_percent,
                           uint8_t min_floor_percent);
