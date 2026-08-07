/* Local web UI — spec §7. Local-only, served from the device over plain
 * HTTP on Wi-Fi; no forced account, no cloud round trip.
 *
 * Single-purpose pages:
 *   /            status + links
 *   /calibration MUA threshold + hysteresis + fan min-speed floor (NVS)
 *   /hostname    cosmetic friendly-name label (never the resolvable name)
 *   /wifi        maintenance-plane Wi-Fi credentials (reboots to apply)
 *   /update      firmware upload (esp_ota_*) to the inactive OTA slot,
 *                reboots on success; auto-rollback on boot failure
 *
 * Auth (§7.1): password optional and user-set — device ships open on the
 * LAN until one is configured. Stored as a salted hash in NVS, session
 * cookie after login. Known limitation: plain HTTP — the password guards
 * against casual access on a shared network, not an on-path attacker.
 * TODO(bring-up): login/session handling is not implemented yet (#6) — once
 * it lands, /update is the highest-priority page to gate behind it.
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef struct {
    /* Board defaults shown until calibration is written (core code never
     * includes the generated board header — main passes these through). */
    uint8_t mua_threshold_default;
    uint8_t mua_hysteresis_default;
    uint8_t fan_floor_default;
    /* Invoked after calibration values change so the caller re-reads NVS
     * (e.g. re-init the MUA evaluator). May be NULL. */
    void (*on_calibration_changed)(void);
} webui_config_t;

esp_err_t webui_start(const webui_config_t *config);
