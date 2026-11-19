/* MAC-derived hostname — spec §5.4. Same "<prefix>-<last 3 MAC bytes>"
 * form as the ESP32-C6 build (e.g. "exhaust-2af03e"), from the factory-
 * burned MAC of the given interface (wlan0 on the Pi). Advertising it over
 * mDNS is Avahi's job on Linux. */
#pragma once

#include <stddef.h>

int hostname_get(const char *prefix, const char *iface, char *buf,
                 size_t buf_len);
