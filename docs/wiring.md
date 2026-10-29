# Wiring

> ⚠️ **Line voltage is lethal.** The metal enclosure contains mains AC.
> Work must satisfy local electrical code; this reference design does not
> carry UL/ETL certification.

## Bill of materials

| Function | Part | Enclosure |
|---|---|---|
| Controller | Raspberry Pi 3 Model B or B+ (Wi-Fi + BLE onboard), 64-bit Raspberry Pi OS Lite on an industrial/high-endurance microSD | External (plastic) |
| Thread radio | [Home Assistant Connect ZBT-1](https://toolbox.openhomefoundation.org/home-assistant-connect-zbt-1/) flashed with its [OpenThread RCP firmware](https://connectzbt1.home-assistant.io/about-firmware-options), on a ~30cm USB extension cable | External (plastic) |
| Fan speed control | Leading-edge TRIAC AC dimmer, **motor-rated** (e.g. RobotDyn AC dimmer module) | Metal box |
| Light dimming | Trailing-edge MOSFET AC dimmer, **LED-rated** | Metal box |
| Zero-cross detection | Shared between both channels | Metal box |
| Dimmer-to-MCU bridge | UART/I2C dimmer controller (e.g. DimmerLink) | Metal box |
| MUA trigger | 1-channel opto-isolated relay module, **high-level trigger** (3.3V input), NO contact to MUA terminals | External (plastic) |
| Power supply | Isolated AC-DC, **5V ≥2.5A** (e.g. [Mean Well IRM-15-5](https://www.meanwell.com/webapp/product/search.aspx?prod=IRM-15), 5V 3A), line-neutral tap — an HLK-PM01 (0.6A) cannot run a Pi 3 | Metal box |
| Wall interface | 2× momentary SPDT center-off toggles | 2-gang wall box |
| Wall-to-module link | 4-conductor low-voltage cable (CAT5 or similar) | Between box and enclosure |
| Commissioning aids | 2× NTAG213/215 passive NFC stickers | On/near external enclosure |

## Wall box (low voltage only)

The 2-gang box has a neutral present but the wall plate itself carries **no
line voltage** — only the 4-conductor low-voltage run:

| Conductor | Signal |
|---|---|
| 1 | Fan rocker common → GND |
| 2 | Fan rocker up/down (see note) |
| 3 | Light rocker up/down (see note) |
| 4 | GND / spare |

Rocker contacts close to ground; the Pi inputs are active-low with pull-ups.
The Pi's internal pull-ups are weak (~50–65kΩ, per the
[GPIO docs](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#gpio)),
so fit external 4.7k–10k pull-ups to 3.3V at the Pi end of the run. Pi GPIO
is **3.3V only — not 5V tolerant**; nothing on the wall run may carry 5V. With 4 conductors and 2 SPDT center-off rockers (4 signals +
common), signal pairs share the run — confirm final conductor assignment
during bring-up, or run CAT5 and use 5 signals + common with pairs to spare.

Default GPIO map (BCM numbering; edit `boards/<name>/board-config.yaml`,
never core):

| Signal | BCM GPIO | Header pin |
|---|---|---|
| Fan rocker up | 17 | 11 |
| Fan rocker down | 27 | 13 |
| Light rocker up | 22 | 15 |
| Light rocker down | 23 | 16 |
| MUA relay | 24 | 18 |
| Dimmer bridge TX | 14 | 8 |
| Dimmer bridge RX | 15 | 10 |
| GND | — | 6, 9, 14, 20 |

Pins were chosen to avoid BCM 2/3 (fixed 1.8kΩ I2C pull-ups) and to boot
pulled **down**, so the MUA relay stays open while Linux boots.

## Metal box ↔ plastic enclosure

Two low-voltage links cross between enclosures:

1. **5V DC** from the isolated AC-DC supply to the Pi (5V on header pin 2,
   GND on pin 6 — or the micro-USB jack). Keep the run short and heavy enough (≥22 AWG) that the Pi
   doesn't brown out — under-voltage throttles the CPU and corrupts SD cards.
2. **UART** (2-wire, 3.3V levels) to the dimmer bridge. The bridge owns
   zero-cross sync and gate firing for both dimmers; microsecond gate timing
   never crosses the gap — which matters even more on a Pi, since Linux
   can't guarantee microsecond GPIO timing. Confirm the bridge accepts 3.3V
   logic, or add a level shifter.

### Pi 3 UART setup

On the Pi 3 the full PL011 UART drives Bluetooth and GPIO 14/15 get the
mini UART ([Configure UARTs](https://www.raspberrypi.com/documentation/computers/configuration.html#configure-uarts)).
Matter commissioning needs BLE, so **keep Bluetooth on the PL011** and use
the mini UART for the bridge. In `/boot/firmware/config.txt`:

```
enable_uart=1
```

`enable_uart=1` pins the core clock so the mini UART's baud rate is stable.
Remove `console=serial0,115200` from `cmdline.txt` so the kernel console
doesn't talk to the dimmer bridge. Don't use `dtoverlay=disable-bt`.

### Thread radio (ZBT-1)

- Flash the ZBT-1 with its **Thread** (OpenThread RCP) firmware before
  installing it — it ships with Zigbee firmware. The
  [firmware page](https://connectzbt1.home-assistant.io/firmware-update)
  flashes it from a browser.
- Plug it in through a short USB extension cable so the 802.15.4 antenna
  sits away from the Pi's own 2.4GHz Wi-Fi/BLE antenna and USB noise.
  Both stay inside the plastic enclosure.
- Install [ot-br-posix](https://openthread.io/guides/border-router/raspberry-pi)
  and point it at the dongle by its stable path
  (`/dev/serial/by-id/usb-Nabu_Casa_…`), not `/dev/ttyUSB0`.
- Its draw comes out of the same 5V ≥2.5A supply.

### OS hardening

- Enable the read-only overlay root (`raspi-config` → Performance Options →
  Overlay File System) so a power cut mid-write can't corrupt the SD card.
  Calibration and user state live on a small separate writable partition.
- Enable the hardware watchdog (`dtparam=watchdog=on`, systemd
  `RuntimeWatchdogSec`) so a hung controller reboots.
- Expect ~20–30s from power-on before the wall rockers respond; the
  ESP32-C6 was ready in under a second. The MUA relay stays open throughout.

## MUA dry contact

The relay module lives in the plastic enclosure (it's low-voltage dry
contact — not subject to the metal-box requirement). Wire the **NO**
contact to the makeup-air damper terminals. The input is driven by
`BOARD_GPIO_MUA_RELAY` (BCM 24) at 3.3V; power the module's coil from the
Pi's 5V rail (pin 4). Use a **high-level-trigger** module (or set its H/L
jumper to H): an active-low module would close the MUA contact while the Pi
is booting.
