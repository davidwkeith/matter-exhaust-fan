# rpi3-2gang

Reference controller: Raspberry Pi 3 (Model B or B+) in an external plastic
enclosure, driving a RobotDyn-style TRIAC dimmer (fan) and a trailing-edge
MOSFET dimmer (light) through a UART dimmer bridge in the line-voltage metal
box. Matter runs over **Wi-Fi** (the Pi 3 has no 802.15.4/Thread radio),
commissioned over BLE.

Wiring, power and OS setup are in [docs/wiring.md](../../docs/wiring.md).
GPIO numbers in `board-config.yaml` are BCM numbers, not header pin numbers.

Firmware for this board is the Linux port of `core/` (pending — see the open
items in [docs/architecture.md](../../docs/architecture.md)); it is not built
by the ESP-IDF `Firmware build` workflow.
