/* Matter device model — spec §6. Single node, two endpoints:
 *
 *   Endpoint 1 — Fan:   Fan Control cluster (0x0202).
 *     PercentSetting / PercentCurrent (0x0002 / 0x0003) drive continuous
 *     speed — no discrete SpeedMax/SpeedSetting, the motor is continuously
 *     variable.
 *   Endpoint 2 — Light: On/Off + Level Control (standard dimmable light);
 *     MoveToLevelWithOnOff for fade transitions.
 *
 * The MUA relay is local firmware behavior tied to PercentCurrent, NOT a
 * Matter endpoint — no ecosystem-visible control for it.
 *
 * C API so the C main loop can drive the C++ esp-matter stack.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Called by the Matter stack when an ecosystem writes fan speed or light
 * on/off/level — wire these into the channel state in main. */
typedef struct {
    void (*on_fan_percent)(uint8_t percent);          /* 0 = off */
    void (*on_light_state)(bool on, uint8_t percent);
} matter_callbacks_t;

/* Init the stack, create both endpoints, start commissioning if needed.
 * Transport is Thread (selected in sdkconfig, §6.1); the onboarding
 * QR/manual pairing payload is printed to the console at first boot. */
esp_err_t matter_endpoints_start(const matter_callbacks_t *cbs);

/* Push local (wall switch) state changes up to the Matter attributes. */
esp_err_t matter_report_fan_percent(uint8_t percent);
esp_err_t matter_report_light(bool on, uint8_t percent);

/* True once commissioned into at least one fabric — used for the
 * factory-fresh light-flash status indication (§5.6). */
bool matter_is_commissioned(void);

/* Wipe fabric/commissioning data and reboot (part of factory reset §5.5). */
void matter_factory_reset(void);

#ifdef __cplusplus
}
#endif
