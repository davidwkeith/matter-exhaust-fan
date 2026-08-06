# NFC tags

Two **separate** passive tags (NTAG213/215), placed on or near the external
enclosure. Deliberately not combined onto one tag: multi-record NDEF tap
behavior is OS-arbitrated and inconsistent, and the pairing record becomes
permanently stale after first commissioning while sharing tap-target
ambiguity with the still-relevant configuration record.

| Tag | Content | Lifecycle |
|---|---|---|
| **Left — pair** | Matter onboarding payload as an NDEF URI record (Matter spec §5: NFC uses the NDEF URI RTD) | Effectively one-time — initial commissioning or post-factory-reset only |
| **Right — configure** | `http://exhaust-<mac>.local/` as an NDEF URI record | Used indefinitely |

Notes:

- The pairing payload is the same onboarding payload esp-matter generates at
  first boot (QR / manual pairing code). NFC is an aid; the QR and manual
  code remain the required commissioning baseline.
- The configure tag URL uses the **MAC-derived hostname**, never the
  cosmetic friendly-name label — the label can change; the tag can't.
- Writing the tags: any NDEF-capable phone app or `libnfc` tooling works;
  write-protect the tags after writing.
