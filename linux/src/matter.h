/* Matter device model — spec §6, same C interface as core/matter.
 *
 *   Endpoint 1 — Fan:   Fan Control (0x0202), PercentSetting/PercentCurrent
 *   Endpoint 2 — Light: On/Off + Level Control
 *
 * Two implementations: linux/matter/matter_chip.cpp (connectedhomeip,
 * Thread through ot-br-posix + the ZBT-1) and matter_stub.c (wall switches
 * only, for bring-up without the SDK).
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

/* Register the callbacks. Called from the controller thread; in the Matter
 * build the CHIP stack is already up by then. */
int matter_start(const matter_callbacks_t *cbs);

/* Deliver pending remote writes to the callbacks. Called every controller
 * tick, so callbacks run on the controller thread, never the CHIP one. */
void matter_poll(void);

/* Push local (wall switch) state changes up to the Matter attributes. */
void matter_report_fan_percent(uint8_t percent);
void matter_report_light(bool on, uint8_t percent);

/* True once commissioned into at least one fabric (§5.6 status blink). */
bool matter_is_commissioned(void);

/* Wipe fabric/commissioning data and stop the Matter stack (part of
 * factory reset §5.5); the controller then exits for systemd to restart. */
void matter_factory_reset(void);

#ifdef __cplusplus
}
#endif
