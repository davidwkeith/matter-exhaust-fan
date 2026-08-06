#include "mua.h"

void mua_init(mua_t *m, uint8_t threshold_percent, uint8_t hysteresis_percent)
{
    m->threshold_percent = threshold_percent;
    m->hysteresis_percent = hysteresis_percent;
    m->energized = false;
}

bool mua_update(mua_t *m, bool fan_on, uint8_t fan_percent)
{
    if (!fan_on) {
        m->energized = false;
        return m->energized;
    }

    if (!m->energized) {
        if (fan_percent >= m->threshold_percent) {
            m->energized = true;
        }
    } else {
        /* Drop out only once clearly below the band, so tap-stepping right
         * at the threshold doesn't chatter the relay. */
        uint8_t drop = (m->threshold_percent > m->hysteresis_percent)
                           ? (uint8_t)(m->threshold_percent - m->hysteresis_percent)
                           : 0;
        if (fan_percent < drop) {
            m->energized = false;
        }
    }
    return m->energized;
}
