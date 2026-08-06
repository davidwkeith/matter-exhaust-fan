#include "reset_chord.h"

void reset_chord_init(reset_chord_t *rc, uint32_t reset_hold_ms)
{
    rc->reset_hold_ms = reset_hold_ms;
    rc->both_since_ms = 0;
    rc->armed = false;
    rc->fired = false;
}

bool reset_chord_update(reset_chord_t *rc, rocker_t *fan, rocker_t *light,
                        uint32_t now_ms)
{
    bool both = rocker_pressed(fan) && rocker_pressed(light);

    if (!both) {
        rc->armed = false;
        rc->fired = false;
        return false;
    }

    if (!rc->armed) {
        rc->armed = true;
        rc->both_since_ms = now_ms;
    }

    /* Suppress each press for as long as the chord is held, so neither
     * channel fires its own tap/hold action. If one gang's 450ms hold
     * already fired before the second gang was pressed, that action has
     * already happened and is not undone.
     *
     * TODO(spec §10): define the remaining chorded-hold edge cases — e.g.
     * one gang held, the other briefly tapped mid-window — before calling
     * this behavior final. */
    rocker_suppress(fan);
    rocker_suppress(light);

    if (!rc->fired &&
        (uint32_t)(now_ms - rc->both_since_ms) >= rc->reset_hold_ms) {
        rc->fired = true;
        return true;
    }
    return false;
}
