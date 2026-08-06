# Contributing

Thanks for your interest in the project. Two kinds of contributions have very
different bars, so read the split below before opening a PR.

## The core / board-config split

This is the primary design constraint of the repository.

**Core** (`core/`, `main/`) is shared by every fork and every hardware
variant. It contains:

- The Matter cluster implementation (Fan Control on endpoint 1, On/Off +
  Level Control on endpoint 2)
- The hold/tap/hold-off rocker state machine and factory-reset chord logic
- Dimmer curve functions and the dimmer-bridge protocol
- MUA threshold/hysteresis logic
- mDNS/NFC provisioning and the web UI shell

**Board config** (`boards/<name>/board-config.yaml`) is the only thing a
hardware fork edits: GPIO assignments, curve selection, minimum fan speed
floor, MUA defaults, hostname prefix.

The test for whether something belongs in core: *would two different
manufacturers' boards need different behavior here?* If yes, it must be
expressible in `board-config.yaml` — never as an `#ifdef` or a patched core
file. If a board need can't be expressed in the config schema, the right PR
extends the schema (with a default that keeps existing boards working), then
uses it.

## Adding a board

1. Copy `boards/generic-2gang/` to `boards/<your-board>/`
2. Edit `board-config.yaml`
3. Build with `idf.py -DBOARD=<your-board> build` and verify on hardware
4. PR should include the config plus a short wiring note in the board
   directory's README — nothing in `core/` should change

## Core changes

- Portable logic (`core/input`, `core/dimmer` curves, `core/mua`) must stay
  free of ESP-IDF includes so the host tests keep working. Add or update
  tests in `test/` for any behavior change.
- Run the host tests before pushing: `cd test && make`
- Interaction-model changes (hold thresholds, step sizes, chord timing) need
  a rationale in the PR description — the whole point of the project is
  preserving the existing UX.

## Style

- C for portable core logic, C++ only where esp-matter requires it
- Match the surrounding code; keep comments for constraints the code can't
  express (timing requirements, code-compliance rationale)
- Web UI follows [docs/style-guide.md](docs/style-guide.md): system font
  stack, single accent color, minimal chrome

## License

Contributions are accepted under the Apache License 2.0. By submitting a
contribution you agree to license it under the same terms (Apache 2.0 §5).
