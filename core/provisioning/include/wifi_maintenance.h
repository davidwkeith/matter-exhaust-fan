/* Wi-Fi maintenance plane — carries mDNS + the local web UI (§5.4).
 * Matter traffic stays on Thread; Wi-Fi exists only so the maintenance UI
 * doesn't depend on a Thread Border Router's SRP-to-mDNS proxying.
 *
 * Bootstrapping: credentials live in NVS (user namespace — cleared on
 * factory reset). When none are stored, the device brings up an open
 * SoftAP named after the MAC-derived hostname (e.g. "exhaust-2af03e") so
 * the web UI is reachable at http://192.168.4.1/ to enter credentials.
 * After a successful save the device reboots into station mode.
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

/* Bring up STA from stored credentials, or the setup SoftAP when none are
 * stored. Returns after the interface is started (not necessarily
 * connected — STA retries in the background). */
esp_err_t wifi_maintenance_start(const char *hostname);

/* True when running the setup SoftAP (no stored credentials). */
bool wifi_maintenance_in_setup_mode(void);
