/* UART link to the dimmer bridge inside the metal enclosure — the same
 * protocol as core/dimmer/dimmer_bridge.c, over a tty (/dev/serial0, the
 * Pi 3's mini UART on BCM 14/15 — see docs/wiring.md).
 *
 * Wire protocol (ASCII, newline-terminated, one command per line):
 *   SET FAN <0-100>
 *   SET LIGHT <0-100>
 */
#pragma once

#include <stdint.h>

typedef enum {
    BRIDGE_CH_FAN = 0,
    BRIDGE_CH_LIGHT = 1,
} bridge_channel_t;

int bridge_open(const char *tty_path);

/* Send a power-percent (post-curve) set command for one channel. */
int bridge_set(bridge_channel_t ch, uint8_t power_percent);

void bridge_close(void);
