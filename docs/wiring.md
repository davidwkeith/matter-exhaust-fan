# Wiring

> ⚠️ **Line voltage is lethal.** The metal enclosure contains mains AC.
> Work must satisfy local electrical code; this reference design does not
> carry UL/ETL certification.

## Bill of materials

| Function | Part | Enclosure |
|---|---|---|
| MCU/radio | ESP32-C6-WROOM-1-N8, 8MB flash (or equivalent ESP32-C6/H2 module with ≥8MB flash) | External (plastic) |
| Fan speed control | Leading-edge TRIAC AC dimmer, **motor-rated** (e.g. RobotDyn AC dimmer module) | Metal box |
| Light dimming | Trailing-edge MOSFET AC dimmer, **LED-rated** | Metal box |
| Zero-cross detection | Shared between both channels | Metal box |
| Dimmer-to-MCU bridge | UART/I2C dimmer controller (e.g. DimmerLink) | Metal box |
| MUA trigger | 1-channel opto-isolated relay module, NO contact to MUA terminals | External (plastic) |
| Power supply | Isolated AC-DC buck (e.g. HLK-PM01, 5V), line-neutral tap | Metal box |
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

Rocker contacts close to ground; the ESP32-C6 inputs use internal pull-ups
(active-low). With 4 conductors and 2 SPDT center-off rockers (4 signals +
common), signal pairs share the run — confirm final conductor assignment
during bring-up, or run CAT5 and use 5 signals + common with pairs to spare.

Default GPIO map (edit `boards/<name>/board-config.yaml`, never core):

| Signal | GPIO |
|---|---|
| Fan rocker up | 4 |
| Fan rocker down | 5 |
| Light rocker up | 6 |
| Light rocker down | 7 |
| MUA relay | 10 |
| Dimmer bridge TX | 16 |
| Dimmer bridge RX | 17 |

## Metal box ↔ plastic enclosure

Two low-voltage links cross between enclosures:

1. **5V DC** from the isolated AC-DC supply to the ESP32-C6.
2. **UART** (2-wire) to the dimmer bridge. The bridge owns zero-cross sync
   and gate firing for both dimmers; microsecond gate timing never crosses
   the gap.

## MUA dry contact

The relay module lives in the plastic enclosure (it's low-voltage dry
contact — not subject to the metal-box requirement). Wire the **NO**
contact to the makeup-air damper terminals. The coil is driven by
`BOARD_GPIO_MUA_RELAY`.
