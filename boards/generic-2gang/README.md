# generic-2gang

Reference board: Seeed XIAO ESP32-C6 in an external plastic enclosure, driving
a RobotDyn-style TRIAC dimmer (fan) and a trailing-edge MOSFET dimmer (light)
through a UART dimmer bridge in the line-voltage metal box.

Wiring is documented in [docs/wiring.md](../../docs/wiring.md). GPIO numbers
in `board-config.yaml` refer to ESP32-C6 GPIO numbers, not XIAO pin labels.
