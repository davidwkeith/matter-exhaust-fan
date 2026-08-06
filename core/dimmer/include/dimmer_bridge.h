/* UART link to the dimmer bridge inside the metal enclosure — spec §3.3.
 *
 * Gate-trigger timing is microsecond-precision and never leaves the metal
 * box: the bridge (e.g. a RobotDyn DimmerLink) owns zero-cross sync and gate
 * firing for both channels; the MCU only sends power-percent set commands
 * over a 2-wire bus.
 *
 * Wire protocol (ASCII, newline-terminated, one command per line):
 *   SET FAN <0-100>
 *   SET LIGHT <0-100>
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef enum {
    DIMMER_CH_FAN = 0,
    DIMMER_CH_LIGHT = 1,
} dimmer_channel_t;

esp_err_t dimmer_bridge_init(int tx_gpio, int rx_gpio);

/* Send a power-percent (post-curve) set command for one channel. */
esp_err_t dimmer_bridge_set(dimmer_channel_t ch, uint8_t power_percent);
