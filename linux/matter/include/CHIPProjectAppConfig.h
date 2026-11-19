/* Project configuration for exhaustd-matter (Raspberry Pi controller). */
#pragma once

// include the CHIPProjectConfig from config/standalone
#include <CHIPProjectConfig.h>

#define CHIP_DEVICE_CONFIG_ENABLE_COMMISSIONABLE_DEVICE_TYPE 1

// Primary device type advertised during commissioning: Fan (0x002B). The
// light is the second endpoint.
#define CHIP_DEVICE_CONFIG_DEVICE_TYPE 43

#define CHIP_DEVICE_CONFIG_ENABLE_COMMISSIONABLE_DEVICE_NAME 1

#define CHIP_DEVICE_CONFIG_DEVICE_NAME "Exhaust Fan"
