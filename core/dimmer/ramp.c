#include "ramp.h"

void ramp_init(ramp_t *r, uint32_t step_interval_ms)
{
    r->current_percent = 0;
    r->target_percent = 0;
    /* A zero interval would step the full range in one tick; treat it as
     * "no ramp" explicitly rather than dividing time by zero. */
    r->step_interval_ms = step_interval_ms ? step_interval_ms : 1;
    r->last_step_ms = 0;
}

void ramp_set_target(ramp_t *r, uint8_t target_percent, uint32_t now_ms)
{
    if (target_percent > 100) {
        target_percent = 100;
    }
    r->target_percent = target_percent;
    r->last_step_ms = now_ms;
}

void ramp_jump_to(ramp_t *r, uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    r->current_percent = percent;
    r->target_percent = percent;
}

bool ramp_active(const ramp_t *r)
{
    return r->current_percent != r->target_percent;
}

bool ramp_tick(ramp_t *r, uint32_t now_ms)
{
    if (!ramp_active(r)) {
        return false;
    }
    uint32_t elapsed = now_ms - r->last_step_ms;
    if (elapsed < r->step_interval_ms) {
        return false;
    }

    /* Step as many percent as the elapsed time covers, so a slow poll loop
     * still finishes the ramp on schedule. */
    uint32_t steps = elapsed / r->step_interval_ms;
    r->last_step_ms += steps * r->step_interval_ms;

    if (r->current_percent < r->target_percent) {
        uint32_t room = (uint32_t)(r->target_percent - r->current_percent);
        r->current_percent += (steps < room) ? (uint8_t)steps : (uint8_t)room;
    } else {
        uint32_t room = (uint32_t)(r->current_percent - r->target_percent);
        r->current_percent -= (steps < room) ? (uint8_t)steps : (uint8_t)room;
    }
    return true;
}
