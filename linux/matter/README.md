# exhaustd-matter

`exhaustd` with Matter: the same controller loop as the standalone build
([`../README.md`](../README.md)), running beside the
[connectedhomeip](https://github.com/project-chip/connectedhomeip) Linux app.
Built out of tree with GN against the pinned SDK in
`third_party/connectedhomeip` (git submodule, v1.5.1.0).

## Data model

[`exhaust-fan.zap`](exhaust-fan.zap) — composed from the SDK's own chef
device definitions (`rootnode_fan`, `rootnode_dimmablelight`):

| Endpoint | Device type | Clusters |
|---|---|---|
| 0 | Root Node | standard root clusters |
| 1 | Fan (0x002B) | Identify, Groups, Descriptor, Fan Control |
| 2 | Dimmable Light (0x0101) | Identify, Groups, On/Off, Level Control, Descriptor |

The `.matter` IDL next to it is generated from the `.zap`; regenerate both
with the SDK's ZAP tooling after editing the data model.

## Threads

The CHIP event loop owns the main thread. The controller loop runs on its
own thread, started from `ApplicationInit()`:

- remote writes (Fan Control `PercentSetting`, On/Off, Level Control
  `CurrentLevel`) are queued by the attribute-change callback and handed
  to the controller on its next 5ms tick (`matter_poll()`);
- wall-switch changes are written back under the CHIP stack lock
  (`PercentSetting` + `PercentCurrent`, `OnOff` + `CurrentLevel`);
- SIGTERM stops the event loop, then the controller (fan off before the
  MUA relay opens); a factory-reset chord wipes fabrics, KVS and the
  Thread dataset, then stops both for systemd to restart.

## Build

```sh
git submodule update --init linux/matter/third_party/connectedhomeip
cd linux/matter/third_party/connectedhomeip
scripts/checkout_submodules.py --shallow --platform linux
source scripts/activate.sh            # bootstrap: GN, ninja, ZAP, Python env
cd ../..                              # linux/matter
gn gen out/host && ninja -C out/host  # → out/host/exhaustd-matter
```

Run on the Pi with Thread (needs ot-br-posix running against the ZBT-1,
see [docs/wiring.md](../../docs/wiring.md)):

```sh
exhaustd-matter --thread --ble-controller 0 --KVS /var/lib/exhaustd/chip_kvs
```

Without `--thread` it runs over IP, which is how it can be commissioned and
tested on a desktop (`chip-tool pairing onnetwork …`).
