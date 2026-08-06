/* NVS-backed settings — spec §5.3, §5.5, §7.1.
 *
 * Two classes of value, with different factory-reset behavior:
 *
 *   Hardware calibration (SURVIVES factory reset — properties of the
 *   physically installed fan, not user preference):
 *     - mua_threshold_percent (+ hysteresis band)
 *     - fan min_speed_floor_percent
 *
 *   Everything else (CLEARED on factory reset):
 *     - Matter fabric/commissioning data (owned by esp-matter's NVS)
 *     - custom hostname label (cosmetic only — never the resolvable name)
 *     - web UI password hash (salted; never plaintext)
 *     - last-used fan speed / light brightness
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t settings_init(void);

/* --- hardware calibration (preserved across factory reset) --- */
uint8_t settings_get_mua_threshold(uint8_t default_percent);
esp_err_t settings_set_mua_threshold(uint8_t percent);
uint8_t settings_get_mua_hysteresis(uint8_t default_percent);
esp_err_t settings_set_mua_hysteresis(uint8_t percent);
uint8_t settings_get_fan_min_floor(uint8_t default_percent);
esp_err_t settings_set_fan_min_floor(uint8_t percent);

/* True only on a true first flash — used for the factory-fresh light-flash
 * status indication (§5.6) alongside "not yet commissioned". */
bool settings_mua_threshold_is_set(void);

/* --- user state (cleared on factory reset) --- */
uint8_t settings_get_last_used(const char *channel, uint8_t default_percent);
esp_err_t settings_set_last_used(const char *channel, uint8_t percent);
esp_err_t settings_get_hostname_label(char *buf, size_t buf_len);
esp_err_t settings_set_hostname_label(const char *label);
esp_err_t settings_get_webui_pass_hash(char *buf, size_t buf_len);
esp_err_t settings_set_webui_pass_hash(const char *hash);

/* Factory reset (§5.5): erase everything EXCEPT hardware calibration, then
 * reboot into factory-fresh state. Includes wiping Matter fabric data. */
esp_err_t settings_factory_reset(void);
