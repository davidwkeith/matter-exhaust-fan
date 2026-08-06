# Calibration

Two values are properties of the **physically installed fan**, not user
preferences. They live in the `hwcal` NVS namespace, are editable from the
web UI (`/calibration`), and **survive factory reset**.

## MUA threshold (`mua_threshold_percent`)

The phase-cut percent at which the installed fan crosses the CA Mechanical
Code 505.2 makeup-air threshold (400 CFM for kitchen exhaust).

Procedure, with the actual fan installed:

1. Measure real CFM per speed step — anemometer traverse, or the
   manufacturer's published CFM-vs-speed curve.
2. Find the lowest percent setting at or above 400 CFM.
3. Enter it at `http://exhaust-<mac>.local/calibration`.

The relay energizes at `PercentCurrent >= threshold` and drops out below a
~3–5% hysteresis band (`hysteresis_percent`, default 4) so tap-stepping near
the boundary can't chatter the relay.

**Fan replaced?** Re-measure and update this one value. No reflash.

Before finalizing: confirm with the local AHJ which CFM rating applies
(laboratory vs. equivalent/free-air — spec sheets often list both) and
whether local amendments or exceptions apply.

## Minimum speed floor (`min_speed_floor_percent`)

The phase-cut level below which the motor stalls/hums instead of rotating —
commonly 25–35% for continuously variable AC motors.

1. From the web UI, lower the fan speed until rotation becomes unstable.
2. Set the floor just above that point.

The fan curve then rescales user 1–100% into `[floor, 100]`, so the whole
rocker range stays useful and the motor can never be commanded into the
stall region.

## Defaults

Board-config defaults (`threshold_percent_default`, `fan_min_percent`) are
**placeholders to make an uncalibrated unit safe-ish**, not substitutes for
measurement. Until `mua_threshold_percent` is written on first setup, the
light channel flashes at ~1Hz to signal configuration is required.
