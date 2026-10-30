idf_component_register(SRCS "dimmer_curve.c" "dimmer_bridge.c" "ramp.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver esp_driver_uart)
