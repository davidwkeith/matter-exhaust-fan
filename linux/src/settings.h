/* File-backed settings — the Linux counterpart of core/settings (NVS).
 *
 * Same two classes of value with the same factory-reset behavior (§5.5):
 *
 *   <state_dir>/hwcal.conf — hardware calibration, SURVIVES factory reset
 *     (mua_thr, mua_hyst, fan_floor: properties of the installed fan)
 *   <state_dir>/user.conf  — user state, DELETED on factory reset
 *     (last-used levels; later the hostname label and web UI password)
 *
 * Plain `key=value` lines. Every write goes to a temp file, is fsync'd and
 * renamed over the original, so a power cut leaves either the old or the
 * new file — never a torn one. state_dir must be on the writable partition
 * (the root filesystem is a read-only overlay, docs/wiring.md).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

int settings_init(const char *state_dir);

/* --- hardware calibration (preserved across factory reset) --- */
uint8_t settings_get_mua_threshold(uint8_t default_percent);
int settings_set_mua_threshold(uint8_t percent);
uint8_t settings_get_mua_hysteresis(uint8_t default_percent);
int settings_set_mua_hysteresis(uint8_t percent);
uint8_t settings_get_fan_min_floor(uint8_t default_percent);
int settings_set_fan_min_floor(uint8_t percent);

/* True once the MUA threshold has been calibrated — drives the
 * factory-fresh status blink (§5.6). */
bool settings_mua_threshold_is_set(void);

/* --- user state (cleared on factory reset) --- */
uint8_t settings_get_last_used(const char *channel, uint8_t default_percent);
int settings_set_last_used(const char *channel, uint8_t percent);

/* Delete user state, keep calibration (§5.5). Matter fabric data is wiped
 * separately by the caller. */
int settings_factory_reset(void);
