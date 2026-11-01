# rpi3-2gang

Reference controller: Raspberry Pi 3 (Model B or B+) in an external plastic
enclosure, driving a RobotDyn-style TRIAC dimmer (fan) and a trailing-edge
MOSFET dimmer (light) through a UART dimmer bridge in the line-voltage metal
box. Matter runs over **Thread** through a Home Assistant Connect ZBT-1 USB
dongle (OpenThread RCP firmware + ot-br-posix), commissioned over BLE; Wi-Fi
carries only the maintenance UI.

Wiring, power and OS setup are in [docs/wiring.md](../../docs/wiring.md).
GPIO numbers in `board-config.yaml` are BCM numbers, not header pin numbers.

Firmware for this board is [`exhaustd`](../../linux/README.md), the Linux
port of `core/`; CI builds it in the `Raspberry Pi controller build` job,
not the ESP-IDF `Firmware build` workflow.
