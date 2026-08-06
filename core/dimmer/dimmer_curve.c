#include "dimmer_curve.h"

static uint8_t curve_linear(uint8_t user)
{
    return user;
}

/* Perceptual (approximately logarithmic) brightness curve for LEDs: a
 * square-law approximation of the CIE lightness curve, integer-only.
 * Low taps produce visibly even brightness steps instead of the top of the
 * range doing nothing and the bottom jumping. */
static uint8_t curve_logarithmic(uint8_t user)
{
    uint16_t u = user;
    uint8_t out = (uint8_t)((u * u + 50) / 100);
    return out == 0 ? 1 : out;
}

uint8_t dimmer_apply_curve(dimmer_curve_t curve, uint8_t user_percent,
                           uint8_t min_floor_percent)
{
    if (user_percent == 0) {
        return 0;
    }
    if (user_percent > 100) {
        user_percent = 100;
    }

    uint8_t out;
    switch (curve) {
    case DIMMER_CURVE_LOGARITHMIC:
        out = curve_logarithmic(user_percent);
        break;
    case DIMMER_CURVE_LINEAR:
    default:
        out = curve_linear(user_percent);
        break;
    }

    /* Rescale into [floor, 100] so the full user range stays useful:
     * user 1% sits just above the stall floor, user 100% is full power. */
    if (min_floor_percent > 0 && min_floor_percent < 100) {
        out = (uint8_t)(min_floor_percent +
                        ((uint16_t)out * (100 - min_floor_percent)) / 100);
    }
    return out;
}
