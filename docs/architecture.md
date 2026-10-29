# Architecture

Condensed from the reference design spec. The interaction model being
preserved: **hold to turn on/off, tap to step**, on both the fan and light
channels — exactly what the original continuous-adjustment knobs did, minus
the knobs.

## System split — three physical locations

```
┌─────────────────────────┐     ┌──────────────────────────────┐
│ 2-gang wall box         │     │ External plastic enclosure   │
│ (low voltage ONLY)      │     │ (low voltage, RF-clean)      │
│                         │     │                              │
│  2× momentary SPDT      │────▶│  Raspberry Pi 3 (Matter over │
│  center-off rockers     │ 4-  │  Wi-Fi, BLE commissioning)   │
│                         │cond │  MUA relay (dry contact)     │
└─────────────────────────┘     └──────────────┬───────────────┘
                                               │ 2-wire UART
                                               │ ("SET <ch> <pct>")
                                ┌──────────────▼───────────────┐
                                │ Metal enclosure (line volt.) │
                                │                              │
                                │  TRIAC fan dimmer (leading)  │
                                │  MOSFET light dimmer (trail) │
                                │  Shared zero-cross detect    │
                                │  UART/I2C dimmer bridge      │
                                │  Isolated AC-DC supply       │
                                └──────────────────────────────┘
```

Why this split:

- **Metal box** — anything touching mains AC lives in a code-required metal
  enclosure.
- **Plastic box** — a sealed metal enclosure would shadow the Pi's onboard
  chip antenna; the controller sits outside it for clean Wi-Fi/BLE RF. The MUA relay is
  a low-voltage dry contact, so it isn't subject to the metal-box
  requirement.
- **Wall plate** — no AC switching gear and no RF-blocking metal at the
  wall; just two momentary SPDT center-off rockers and a 4-conductor
  low-voltage run. Maintained toggles can't give clean hold-duration
  detection, hence momentary.
- **Dimmer bridge** — TRIAC gate-trigger timing is microsecond-precision and
  must not cross an open-air gap to a separately mounted MCU (noise pickup
  risks a false fire). A UART/I2C bridge (e.g. RobotDyn DimmerLink) inside
  the metal box owns zero-cross sync and gate firing; the MCU sends
  `SET <percent>` commands. Timing-critical logic never leaves the metal
  enclosure — and never runs on Linux, which can't guarantee microsecond
  GPIO timing.

## Controller platform

The reference controller is a **Raspberry Pi 3** (Model B or B+, BCM2837,
1GB RAM) running 64-bit Raspberry Pi OS Lite. It replaces the original
ESP32-C6 module; the consequences:

- **Matter over Wi-Fi, not Thread.** The Pi 3 has Wi-Fi and BLE but no
  802.15.4 radio. The controller runs on the
  [connectedhomeip Linux platform](https://github.com/project-chip/connectedhomeip/blob/master/examples/lighting-app/linux/README.md)
  with `--wifi`; commissioning is over BLE (BlueZ), and the commissioner
  hands over the Wi-Fi credentials. Upstream documents the Pi 4; the Pi 3
  runs the same aarch64 build. (Thread is possible later with a USB
  802.15.4 RCP and [ot-br-posix](https://openthread.io/guides/border-router/raspberry-pi),
  but isn't in scope.)
- **No setup SoftAP.** Matter commissioning supplies Wi-Fi, so the ESP's
  open setup access point goes away. Ethernet is an option where a cable
  reaches the enclosure.
- **Persistent state** moves from NVS to files on a small writable
  partition, with the root filesystem read-only (overlayfs) against power
  loss. The `hwcal` vs. user-state split and factory-reset rules are
  unchanged.
- **Hostname** derives from the `wlan0` MAC (`/sys/class/net/wlan0/address`)
  — still factory-burned and unique.
- **Boot time** is ~20–30s before the rockers respond, versus sub-second on
  the ESP32-C6. The MUA relay pin boots pulled down (open).
- **Power** needs 5V ≥2.5A, not the 0.6A HLK-PM01. See [wiring.md](wiring.md).

The ESP-IDF firmware under `core/` and `main/` targets the legacy
`generic-2gang` (ESP32-C6) board. The portable modules (`core/input`,
`core/dimmer` curves/ramp, `core/mua`) carry over to the Pi unchanged; the
ESP-only modules need Linux equivalents (see open items).

## Firmware structure

| Module | Role | Portable? |
|---|---|---|
| `core/input` | Rocker debounce + tap/hold classification, per-channel state machine, factory-reset chord | Yes — host-tested |
| `core/dimmer` | User-percent → power-percent curves; ramp/fade engine; UART bridge protocol | Curves and ramp yes; bridge is ESP-only |
| `core/mua` | Threshold + hysteresis relay logic | Yes — host-tested |
| `core/settings` | NVS wrapper; calibration vs. user-state namespaces | ESP-only — Pi: file store on writable partition |
| `core/matter` | esp-matter node: Fan Control (EP1), dimmable light (EP2) | ESP-only — Pi: connectedhomeip Linux app |
| `core/provisioning` | MAC-derived hostname, mDNS, maintenance Wi-Fi (STA + setup SoftAP) | ESP-only — Pi: hostname + Avahi; Wi-Fi via Matter commissioning |
| `core/webui` | Local HTTP maintenance UI | ESP-only — Pi: needs port |
| `main` | Board-config include + glue | ESP-only — Pi: needs port (GPIO via libgpiod, UART via `/dev/serial0`) |

### Interaction state machine (per channel, §5.1)

States `OFF → RAMPING → ON → OFF`; debounce ~25ms, hold threshold ~450ms:

- **OFF + hold** (either direction) → on, ramp/fade toward last-used level
- **ON + tap** → step one increment in the pressed direction (bounded, no wrap)
- **ON + hold** (either direction) → ramp/fade down → off

After a factory reset there is no last-used memory, so both channels default
to **max** on first turn-on — consistent with "ramp toward last-used," where
last-used starts at max by definition.

### Dimmer curves (§5.2)

Leading-edge (fan) and trailing-edge (light) phase-cut use inverted timing
logic relative to each other — the same percent lands at a different point in
the AC half-cycle per channel. The inversion lives in the dimmer bridge; the
firmware keeps two independent curve functions (never a shared lookup table):

- **Fan:** linear power curve with a calibrated **minimum speed floor**
  (commonly 25–35%) — below it, phase-cut power can't sustain rotation and
  the motor stalls/hums.
- **Light:** logarithmic/perceptual curve for LEDs.

### Makeup air (§5.3)

Relay closes when the fan is on and `PercentCurrent >=
mua_threshold_percent`; a 3–5% hysteresis band prevents chatter from
tap-stepping near the boundary. The threshold is **calibration data in NVS**,
not firmware: it's the phase-cut percent at which the installed fan crosses
the CA Mechanical Code 505.2 threshold (400 CFM for kitchen exhaust). Replace
the fan → re-measure → rewrite one NVS value. See
[calibration.md](calibration.md).

The relay is local firmware behavior, deliberately **not** a Matter endpoint.

### Matter device model (§6)

Single node, two endpoints:

| Endpoint | Cluster | Notes |
|---|---|---|
| 1 — Fan | Fan Control (0x0202) | `PercentSetting`/`PercentCurrent` (0x0002/0x0003) for continuous speed; no discrete SpeedMax/SpeedSetting |
| 2 — Light | On/Off + Level Control | Standard dimmable light; `MoveToLevelWithOnOff` for fades |

Transport is **Wi-Fi** on the Pi 3 (no Thread radio). The legacy ESP32-C6
build selects Thread explicitly in `sdkconfig.defaults`. The QR/manual
pairing payload printed at first boot
is the required commissioning baseline; NFC tags are an aid on top
([nfc-tags.md](nfc-tags.md)).

### Hostname and the maintenance plane (§5.4)

- Resolvable hostname is derived from the factory-burned MAC (`wlan0` on
  the Pi; `esp_efuse_mac_get_default()` on the ESP32-C6), e.g.
  `exhaust-2af03e.local` — unique
  against any other device, and immune to the mDNS probe/collision race when
  multiple units reboot together after a power outage.
- The NVS "friendly name" is cosmetic only — never the hostname, never on
  the NFC tag.
- mDNS/HTTP and Matter share the Pi's Wi-Fi link, so the maintenance UI
  needs no Thread Border Router.
- Wi-Fi bootstrap: Matter commissioning over BLE provisions the Wi-Fi
  network (cleared on factory reset). The wall switches never depend on
  Wi-Fi being up; Matter control does. (Legacy ESP32-C6: open setup SoftAP
  at `http://192.168.4.1/`.)
- VLAN caveat: mDNS multicast doesn't cross subnets. On a segmented network
  (e.g. an isolated IoT VLAN on UniFi), enable the mDNS reflector or
  `.local` resolution breaks silently.

### Factory reset (§5.5–5.6)

Hold **both** rockers (either direction each) for 5 seconds. The chord timer
takes precedence over each channel's individual 450ms hold action. On
trigger, persistent state is erased **except hardware calibration** (`mua_threshold`,
hysteresis, `fan min_speed_floor`) — those are properties of the installed
fan, not user preference. Matter fabric data, hostname label, web UI
password hash, and last-used levels are all cleared.

While configuration is required (not commissioned, or — first flash only —
MUA threshold unset), the light channel flashes at ~1Hz as the status
indicator. No dedicated status LED in the BOM.

## Regulatory note (California kitchen hoods)

CA Mechanical Code 505.2 requires makeup air above 400 CFM for kitchen
exhaust. Confirm with the local AHJ which CFM rating applies (laboratory vs.
equivalent/free-air) and whether local amendments apply. The 400 CFM figure
is fixed by code; only the phase-cut percent that produces it is
fan-specific, which is exactly why it lives in NVS.

## Open items

Tracked from the spec:

- [ ] Confirm installed fan model and its published CFM-vs-speed curve (or
      plan for anemometer measurement)
- [ ] Confirm the LED driver is trailing-edge/MOSFET compatible
- [ ] Flat network vs. IoT VLAN → mDNS reflector setting
- [ ] Confirm MUA unit actuation time → possible fan-ramp lead-in delay
- [ ] Define exact chorded-hold edge cases (one gang held, other briefly
      tapped mid-window) — see TODO in `core/input/reset_chord.c`
- [x] Ramp/fade execution, Wi-Fi maintenance plane, factory-fresh status
      blink, web UI calibration/hostname/Wi-Fi/update pages
- [x] Optional web UI password (§7.1): salted SHA-256 in NVS, session
      cookie, gates POST handlers and `/update`
- [ ] Port the ESP-only modules to the Raspberry Pi 3: connectedhomeip
      Linux app (Fan Control EP1, dimmable light EP2, `--wifi`), libgpiod
      rocker/relay I/O, `/dev/serial0` dimmer bridge, file-backed settings,
      web UI, systemd unit with watchdog, and a Pi build in CI
- [ ] Replace ESP OTA (`/update`) with an image/package update path for the Pi
- [ ] Measure Pi 3 boot-to-rockers-live time; decide if it's acceptable
- [ ] Remaining bring-up on hardware (legacy ESP32-C6): validate the
      esp-matter glue against
      the pinned release (the `Firmware build` CI workflow compile-checks
      this), verify the dimmer-bridge protocol against the real bridge
