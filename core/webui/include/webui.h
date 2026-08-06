/* Local web UI — spec §7. Local-only, served from the device over plain
 * HTTP on Wi-Fi; no forced account, no cloud round trip.
 *
 * Single-purpose pages:
 *   /            status + links
 *   /calibration MUA threshold + fan min-speed floor (writes NVS)
 *   /hostname    cosmetic friendly-name label (never the resolvable name)
 *   /update      firmware update
 *
 * Auth (§7.1): password optional and user-set — device ships open on the
 * LAN until one is configured. Stored as a salted hash in NVS, session
 * cookie after login. Known limitation: plain HTTP — the password guards
 * against casual access on a shared network, not an on-path attacker.
 */
#pragma once

#include "esp_err.h"

esp_err_t webui_start(void);
