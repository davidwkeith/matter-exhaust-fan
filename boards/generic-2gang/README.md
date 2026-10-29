# generic-2gang (legacy ESP32-C6)

> Superseded by [`rpi3-2gang`](../rpi3-2gang/) as the reference controller.
> Kept because the ESP-IDF firmware in `core/` still builds for it.

Module: ESP32-C6-WROOM-1-N8 (8MB flash) in an external plastic enclosure,
driving a RobotDyn-style TRIAC dimmer (fan) and a trailing-edge MOSFET dimmer
(light) through a UART dimmer bridge in the line-voltage metal box. Matter
over Thread, Wi-Fi for the maintenance UI.

GPIO numbers in `board-config.yaml` are ESP32-C6 GPIO numbers: fan rocker
up/down 4/5, light rocker up/down 6/7, MUA relay 10, dimmer bridge TX/RX
16/17. Power from an HLK-PM01 (5V) is sufficient for this module.
