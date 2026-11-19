# exhaustd — Raspberry Pi 3 controller

The Linux port of the controller firmware for the
[`rpi3-2gang`](../boards/rpi3-2gang/) board. It reuses the portable core
modules unchanged (`core/input`, `core/dimmer` curves + ramp, `core/mua`) and
replaces the ESP-only glue with Linux equivalents:

| ESP32-C6 (`main/`, `core/`) | Pi 3 (`linux/src/`) |
|---|---|
| `driver/gpio` | `gpio.c`: kernel GPIO character device (uAPI v2), no libgpiod |
| UART driver | `bridge.c`: termios on `/dev/serial0`, same `SET FAN/LIGHT <pct>` protocol |
| NVS (`core/settings`) | `settings.c`: `hwcal.conf` + `user.conf`, atomic rename writes |
| eFuse MAC hostname | `hostname.c`: `wlan0` MAC → `exhaust-xxxxxx` |
| FreeRTOS tasks | one 5ms poll loop in `main.c` |
| task watchdog / OTA rollback | systemd `Type=notify` + `WatchdogSec=` (`exhaustd.service`) |
| esp-matter | **not yet**: `matter_stub.c` stands in (wall switches only) |

## Status

Done: wall rockers (tap/hold, factory-reset chord), fan ramp and light fade
through the dimmer bridge, MUA interlock, status blink until the MUA
threshold is calibrated, persisted last-used levels.

Not yet: Matter (connectedhomeip Linux app with `--thread` over
ot-br-posix + ZBT-1), the web UI (calibration currently has no editor; the
defaults from `board-config.yaml` apply), and mDNS (Avahi).

## Build and install (on the Pi)

```sh
sudo apt install cmake gcc python3-yaml
cmake -S linux -B build-linux        # -DBOARD=<name> for another board
cmake --build build-linux
sudo cmake --install build-linux     # /usr/local/sbin + systemd unit
sudo systemctl enable --now exhaustd
journalctl -u exhaustd -f
```

Before enabling it, set up the Pi as in [docs/wiring.md](../docs/wiring.md):
`enable_uart=1` and no serial console on `/dev/serial0`.

**Persistent state:** the unit uses `StateDirectory=exhaustd` with
`DynamicUser=yes`, so state lives in `/var/lib/private/exhaustd`. With the
read-only overlay root enabled, that path must be on the writable
partition (mount or bind it there), or calibration is lost on every reboot.

Options (all have Pi defaults): `--gpiochip /dev/gpiochip0`,
`--uart /dev/serial0`, `--state-dir /var/lib/exhaustd`, `--iface wlan0`.

## Tests

`make -C test` runs the core logic tests plus `test_linux_settings.c`. CI
also builds `exhaustd` for `rpi3-2gang`.
