# generic-2gang

Reference module: ESP32-C6-WROOM-1-N8 (8MB flash) in an external plastic
enclosure, driving a RobotDyn-style TRIAC dimmer (fan) and a trailing-edge
MOSFET dimmer (light) through a UART dimmer bridge in the line-voltage metal
box. Prototyped on a Seeed XIAO ESP32-C6 dev board (same silicon/radio, but
only 4MB flash — too tight once Matter's cluster tables and the Wi-Fi/BLE
stacks are in the same binary as a second OTA slot).

Wiring is documented in [docs/wiring.md](../../docs/wiring.md). GPIO numbers
in `board-config.yaml` refer to ESP32-C6 GPIO numbers, not XIAO pin labels.
