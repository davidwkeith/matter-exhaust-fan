#include "rocker.h"

void rocker_init(rocker_t *r, uint32_t debounce_ms, uint32_t hold_ms)
{
    r->debounce_ms = debounce_ms;
    r->hold_ms = hold_ms;
    r->raw_dir = ROCKER_DIR_NONE;
    r->raw_since_ms = 0;
    r->stable_dir = ROCKER_DIR_NONE;
    r->press_start_ms = 0;
    r->hold_fired = false;
    r->suppressed = false;
}

bool rocker_pressed(const rocker_t *r)
{
    return r->stable_dir != ROCKER_DIR_NONE;
}

void rocker_suppress(rocker_t *r)
{
    r->suppressed = true;
}

rocker_event_t rocker_update(rocker_t *r, bool up, bool down, uint32_t now_ms)
{
    rocker_event_t evt = { ROCKER_EVT_NONE, ROCKER_DIR_NONE };

    /* An SPDT center-off rocker can't close both contacts; if both read
     * active it's a wiring fault or bounce — treat as released. */
    rocker_dir_t raw = ROCKER_DIR_NONE;
    if (up && !down) {
        raw = ROCKER_DIR_UP;
    } else if (down && !up) {
        raw = ROCKER_DIR_DOWN;
    }

    if (raw != r->raw_dir) {
        r->raw_dir = raw;
        r->raw_since_ms = now_ms;
    }

    if (r->raw_dir != r->stable_dir &&
        (uint32_t)(now_ms - r->raw_since_ms) >= r->debounce_ms) {
        rocker_dir_t prev = r->stable_dir;
        r->stable_dir = r->raw_dir;

        if (prev != ROCKER_DIR_NONE && !r->hold_fired && !r->suppressed) {
            /* Released (or rocked across center) before the hold threshold. */
            evt.type = ROCKER_EVT_TAP;
            evt.dir = prev;
        }
        if (r->stable_dir != ROCKER_DIR_NONE) {
            r->press_start_ms = now_ms;
        }
        r->hold_fired = false;
        r->suppressed = false;
        return evt;
    }

    if (r->stable_dir != ROCKER_DIR_NONE && !r->hold_fired &&
        (uint32_t)(now_ms - r->press_start_ms) >= r->hold_ms) {
        r->hold_fired = true;
        if (!r->suppressed) {
            evt.type = ROCKER_EVT_HOLD;
            evt.dir = r->stable_dir;
        }
    }
    return evt;
}
