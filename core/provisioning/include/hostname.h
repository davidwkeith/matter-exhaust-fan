/* MAC-derived hostname + mDNS — spec §5.4, §6.3.
 *
 * The resolvable hostname is always derived from the factory-burned MAC
 * (e.g. "exhaust-2af03e") — unique against any other device on the network,
 * including other vendors' hardware reusing a generic friendly name, and it
 * eliminates the mDNS probe/collision race between multiple units rebooting
 * together. The NVS "friendly name" is a cosmetic UI label only: never the
 * hostname, never printed on the NFC tag.
 *
 * mDNS/HTTP are served over Wi-Fi, not Thread — the maintenance UI must not
 * depend on a Thread Border Router's SRP-to-mDNS proxying. Matter traffic
 * stays on Thread.
 */
#pragma once

#include <stddef.h>

#include "esp_err.h"

/* Fill buf with "<prefix>-<last-3-mac-bytes-hex>", e.g. "exhaust-2af03e". */
esp_err_t hostname_get(const char *prefix, char *buf, size_t buf_len);

/* Set the hostname and start mDNS advertising (_http._tcp) over Wi-Fi. */
esp_err_t hostname_start_mdns(const char *prefix);
