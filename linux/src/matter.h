/* Matter device model — spec §6, same C interface as core/matter.
 *
 *   Endpoint 1 — Fan:   Fan Control (0x0202), PercentSetting/PercentCurrent
 *   Endpoint 2 — Light: On/Off + Level Control
 *
 * On the Pi this is a connectedhomeip Linux app started with --thread,
 * driving ot-br-posix + the ZBT-1 over D-Bus. Until that lands,
 * matter_stub.c stands in so the wall switches, dimmer bridge and MUA
 * interlock can be brought up on real hardware.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*on_fan_percent)(uint8_t percent); /* 0 = off */
    void (*on_light_state)(bool on, uint8_t percent);
} matter_callbacks_t;

int matter_start(const matter_callbacks_t *cbs);

/* Push local (wall switch) state changes up to the Matter attributes. */
void matter_report_fan_percent(uint8_t percent);
void matter_report_light(bool on, uint8_t percent);

/* True once commissioned into at least one fabric (§5.6 status blink). */
bool matter_is_commissioned(void);

/* Wipe fabric/commissioning data (part of factory reset §5.5). */
void matter_factory_reset(void);

#ifdef __cplusplus
}
#endif
