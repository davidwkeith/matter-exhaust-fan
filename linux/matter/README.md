# exhaustd-matter

`exhaustd` with Matter: the same controller loop as the standalone build
([`../README.md`](../README.md)), running beside the
[connectedhomeip](https://github.com/project-chip/connectedhomeip) Linux app.
Built out of tree with GN against the pinned SDK in
`third_party/connectedhomeip` (git submodule, v1.5.1.0).

## Data model

[`data-model/exhaust-fan.zap`](data-model/exhaust-fan.zap) — composed from the SDK's own chef
device definitions (`rootnode_fan`, `rootnode_dimmablelight`):

| Endpoint | Device type | Clusters |
|---|---|---|
| 0 | Root Node | standard root clusters |
| 1 | Fan (0x002B) | Identify, Groups, Descriptor, Fan Control |
| 2 | Dimmable Light (0x0101) | Identify, Groups, On/Off, Level Control, Descriptor |

The fan endpoint's On/Off cluster is disabled so `PercentSetting` is the
one way to drive the fan. The SDK only builds data models from under its
own `examples/` tree, so `setup.sh` links this directory in as
`examples/exhaust-fan`. The `.matter` IDL next to the `.zap` is generated
from it; after editing the `.zap` (with the SDK's `zap` tool), regenerate
through that link so the template paths resolve:

```sh
scripts/tools/zap/generate.py examples/exhaust-fan/data-model/exhaust-fan.zap   # from the SDK root
```

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
linux/matter/setup.sh                 # submodule, SDK deps, bootstrap (once)
repo=$PWD; cd linux/matter/third_party/connectedhomeip && source scripts/activate.sh
cd "$repo/linux/matter"   # activate.sh changes directories, so not `cd -`
gn gen out/host && ninja -C out/host  # → out/host/exhaustd-matter
```

Building on the Pi 3 itself is slow and tight on 1GB RAM; cross-compiling
for arm64 is an open item.

Run on the Pi with Thread (needs ot-br-posix running against the ZBT-1,
see [docs/wiring.md](../../docs/wiring.md)):

```sh
exhaustd-matter --thread --ble-controller 0 --KVS /var/lib/exhaustd/chip_kvs
```

Without `--thread` it runs over IP, which is how it can be commissioned and
tested on a desktop (`chip-tool pairing onnetwork …`).

## Tests

[`test/e2e.py`](test/e2e.py) commissions the app over IP with `chip-tool`
and drives it from both sides: remote fan/light writes reach the dimmer
bridge and MUA relay, a wall-switch hold is reported back to Matter, and
SIGTERM exits cleanly. The app is built against
[`test/fake_gpio.c`](test/fake_gpio.c) (rockers and relay as files) and
talks to a pty standing in for the dimmer bridge. It needs IPv6, and runs
in the `Matter controller` CI workflow.
