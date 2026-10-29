# Matter Exhaust Fan + Light Controller

An open reference design for a Matter-native controller that replaces a
continuous-adjustment knob interface (fan speed + light dimming) with momentary
toggle switches, while preserving the original interaction model: **hold to
turn on/off, tap to step**.

Designed to be forked by any exhaust fan or range hood manufacturer for their
own hardware — the split between core firmware and board-specific configuration
is the primary design constraint, not an afterthought.

**Status:** Scaffolding — structure and core logic in place. The reference
controller is moving from the ESP32-C6 to a **Raspberry Pi 3**; the Linux
port of the ESP-only modules and hardware bring-up are pending. See [docs/architecture.md](docs/architecture.md) for the full design.

## Goals

- Native Matter device — no companion app, no cloud account, no vendor lock-in
- Preserve the existing UX: hold-to-on/off, tap-to-step, on both fan and light
- Continuously variable AC motor speed control and dimmable LED lighting
- Interlocked makeup-air (MUA) trigger for code compliance
- Forkable without touching core logic — hardware differences live in one
  config file per board
- Local-first: mDNS + local web UI for calibration; no round trip required
  for normal operation

## Non-goals

- No cloud telemetry, remote access, or account system
- No custom companion app — commissioning and control go through standard
  Matter ecosystems (Apple Home, Google Home, SmartThings, Home Assistant)
- Not targeting UL/ETL certification in this repo — that is a separate,
  manufacturer-specific process before commercial production

## Repository layout

```
core/           Core firmware (NOT forked) — Matter clusters, input state
                machine, dimmer curves, MUA logic, provisioning, web UI shell
boards/         Board configs (the ONLY thing a fork edits) — one directory
                per hardware variant containing board-config.yaml
main/           Thin ESP-IDF entry point wiring core modules together
                (legacy ESP32-C6 build; Pi 3 port pending)
tools/          Build tooling — board-config.yaml → C header generator
test/           Host-side unit tests for the portable core logic (no ESP-IDF
                required — plain gcc/make)
docs/           Architecture, wiring, calibration, NFC tags, web UI style guide
```

## Hardware at a glance

Two physically and electrically separate enclosures:

| Enclosure | Contents |
|---|---|
| **Metal box** (line voltage) | TRIAC fan dimmer (leading-edge, motor-rated), MOSFET light dimmer (trailing-edge, LED-rated), shared zero-cross detection, UART/I2C dimmer bridge, isolated 5V ≥2.5A AC-DC supply |
| **Plastic box** (low voltage, RF-clean) | Raspberry Pi 3 (Matter over Wi-Fi, BLE commissioning, local web UI), MUA dry-contact relay module |
| **2-gang wall box** (low voltage only) | 2× momentary SPDT center-off rockers, 4-conductor run to the plastic box |

The Pi 3 has no Thread radio, so Matter runs over Wi-Fi — see
[Controller platform](docs/architecture.md#controller-platform).
Microsecond-precision TRIAC gate timing never leaves the metal box — the Pi
sends `SET <percent>` commands over a 2-wire bus to a dimmer bridge instead of
driving gate timing across an open-air gap. See
[docs/wiring.md](docs/wiring.md).

## Building the firmware

The Raspberry Pi 3 (`boards/rpi3-2gang`) build doesn't exist yet. The
instructions below build the legacy ESP32-C6 firmware (`generic-2gang`).

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) (v5.x) and
[esp-matter](https://github.com/espressif/esp-matter), targeting ESP32-C6.

```sh
export ESP_MATTER_PATH=/path/to/esp-matter
idf.py set-target esp32c6
idf.py -DBOARD=generic-2gang build flash monitor
```

`BOARD` selects a directory under `boards/`; its `board-config.yaml` is
compiled into a generated header at build time. Defaults to `generic-2gang`.

The build explicitly selects **Thread** for Matter transport (the stock
esp-matter light example defaults to Wi-Fi even on Thread-capable hardware);
Wi-Fi is enabled alongside it for the mDNS/HTTP maintenance UI.

First boot: with no Wi-Fi credentials stored, the device raises an open
setup access point named after its hostname (e.g. `exhaust-2af03e`); connect
and open `http://192.168.4.1/` to point it at your network. Matter
commissioning is independent of this — the wall switches and Thread work
with Wi-Fi never configured.

The `Firmware build` GitHub Actions workflow compile-checks the firmware in
Espressif's esp-matter container for every board under `boards/`.

## Running the host tests

The core interaction logic (rocker state machine, dimmer curves, MUA
hysteresis) is portable C with no ESP-IDF dependency, so it can be tested on
any machine:

```sh
cd test && make
```

## Forking for your hardware

1. Copy `boards/rpi3-2gang/` (or `generic-2gang/` for ESP32-C6) to
   `boards/<your-board>/`
2. Edit `board-config.yaml` — GPIO map, dimmer curves, minimum fan speed
   floor, MUA defaults, hostname prefix
3. Build with `-DBOARD=<your-board>`

That's it. Forking is "write a config file and confirm the wiring diagram,"
not "understand and modify firmware logic." See
[CONTRIBUTING.md](CONTRIBUTING.md) for what belongs in core vs. board config.

## License

[Apache License 2.0](LICENSE). Chosen deliberately so manufacturers can fork
into closed commercial firmware — a copyleft license would defeat the stated
goal of the project. (Espressif's ESP-IDF and esp-matter are Apache 2.0 as
well.)
