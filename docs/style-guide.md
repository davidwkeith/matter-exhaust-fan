# Web UI style guide

A style guide belongs in the repo, not left to each fork's taste. The
maintenance UI is a utility: fast to load off a microcontroller, obvious to
use once a year.

## Principles

- **Local-only.** No CDN fonts, no external scripts, no analytics, no cloud
  round trip. Every asset is embedded in the firmware image.
- **Zero required configuration.** Sensible defaults make the device fully
  functional before this UI is ever opened. Advanced settings exist but are
  not required reading.
- **Single-purpose pages.** One page per task: calibration, hostname label,
  firmware update. No dashboard, no tabs-within-tabs.

## Visual rules

- **Type:** system font stack —
  `-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif`
- **Accent:** exactly one accent color, `#0a7ea4`. Links, focus rings, and
  primary buttons use it; nothing else is colored.
- **Chrome:** minimal. No icon fonts, no imagery, no shadows. Content max
  width ~40rem, centered.
- **Forms:** native controls, visible labels (no placeholder-as-label),
  one primary action per page.

## Authentication (spec §7.1)

- Password is **optional and user-set** — the device ships open on the LAN
  until one is configured from the UI.
- Stored as a salted hash in NVS, never plaintext. Session cookie after
  login — not a persistent Basic Auth prompt.
- Cleared on factory reset with the rest of the user NVS namespace.
- Known limitation, stated honestly in the UI: served over plain HTTP. A
  self-signed cert on a LAN-only device creates more trust-warning friction
  than it prevents; the password protects against casual access on a shared
  network, not an on-path attacker. Revisit if deployments land on untrusted
  networks.
