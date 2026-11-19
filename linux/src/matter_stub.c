/* Placeholder until the connectedhomeip Linux app is integrated.
 *
 * Reports itself commissioned so the §5.6 status blink tracks only the MUA
 * calibration — there is no Matter state to configure yet. */

#include "matter.h"

#include "log.h"

static const char *TAG = "matter";

int matter_start(const matter_callbacks_t *cbs)
{
    (void)cbs;
    LOGW(TAG, "built without Matter: wall switches only");
    return 0;
}

void matter_report_fan_percent(uint8_t percent) { (void)percent; }

void matter_report_light(bool on, uint8_t percent)
{
    (void)on;
    (void)percent;
}

bool matter_is_commissioned(void) { return true; }

void matter_factory_reset(void) {}
