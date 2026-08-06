#include "channel.h"

static uint8_t clamp(int v)
{
    if (v < CHANNEL_MIN_PERCENT) return CHANNEL_MIN_PERCENT;
    if (v > CHANNEL_MAX_PERCENT) return CHANNEL_MAX_PERCENT;
    return (uint8_t)v;
}

void channel_init(channel_t *c, uint8_t last_used_percent)
{
    c->on = false;
    c->level_percent = 0;
    c->last_used_percent = last_used_percent ? clamp(last_used_percent)
                                             : CHANNEL_MAX_PERCENT;
    c->step_percent = CHANNEL_STEP_PERCENT_DEFAULT;
}

channel_action_t channel_handle_event(channel_t *c, rocker_event_t evt)
{
    channel_action_t act = { CHANNEL_ACT_NONE, 0 };

    switch (evt.type) {
    case ROCKER_EVT_HOLD:
        if (!c->on) {
            /* Hold in either direction turns on toward last-used (§5.1). */
            c->on = true;
            c->level_percent = c->last_used_percent;
            act.type = CHANNEL_ACT_RAMP_TO;
            act.target_percent = c->level_percent;
        } else {
            c->last_used_percent = c->level_percent;
            c->on = false;
            c->level_percent = 0;
            act.type = CHANNEL_ACT_RAMP_OFF;
        }
        break;

    case ROCKER_EVT_TAP:
        if (c->on) {
            /* Bounded step, no wrap. */
            int next = (int)c->level_percent +
                       (evt.dir == ROCKER_DIR_UP ? c->step_percent
                                                 : -c->step_percent);
            uint8_t clamped = clamp(next);
            if (clamped != c->level_percent) {
                c->level_percent = clamped;
                act.type = CHANNEL_ACT_RAMP_TO;
                act.target_percent = c->level_percent;
            }
        }
        /* Tap while off is a no-op — only hold turns a channel on. */
        break;

    default:
        break;
    }
    return act;
}

void channel_set_remote(channel_t *c, bool on, uint8_t level_percent)
{
    if (on) {
        if (level_percent == 0) {
            return;
        }
        c->on = true;
        c->level_percent = clamp(level_percent);
    } else {
        if (c->on) {
            c->last_used_percent = c->level_percent;
        }
        c->on = false;
        c->level_percent = 0;
    }
}
