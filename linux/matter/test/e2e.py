#!/usr/bin/env python3
"""End-to-end test: commission exhaustd-matter over IP with chip-tool and
drive it from both sides.

The app runs against test/fake_gpio.c (rockers and relay as files) and a
pty standing in for the dimmer bridge, which this script answers with "OK"
like the real bridge. Needs IPv6 (Matter does), so it runs in CI.

  e2e.py --app out/sim/exhaustd-matter --chip-tool <path>/chip-tool
"""

import argparse
import os
import pty
import re
import select
import subprocess
import sys
import tempfile
import time
import tty

parser = argparse.ArgumentParser()
parser.add_argument("--app", required=True)
parser.add_argument("--chip-tool", required=True)
args = parser.parse_args()
app_path = os.path.abspath(args.app)
chip_tool_path = os.path.abspath(args.chip_tool)

work = tempfile.mkdtemp(prefix="exhaustd-e2e-")
os.chdir(work)
os.mkdir("state")
os.mkdir("chip-tool-store")

with open("inputs", "w") as f:
    f.write("1111")

master, slave = pty.openpty()
tty.setraw(slave)
app = subprocess.Popen(
    [app_path, "--uart", os.ttyname(slave), "--state-dir", "state", "--iface", "lo",
     "--KVS", os.path.join(work, "kvs")],
    stdout=open("app.log", "w"), stderr=subprocess.STDOUT)

bridge = []  # dimmer bridge commands, in order
pending = b""
failures = []


def pump(seconds):
    """Answer the dimmer bridge for `seconds`."""
    global pending
    end = time.time() + seconds
    while time.time() < end:
        ready, _, _ = select.select([master], [], [], 0.02)
        if ready:
            pending += os.read(master, 256)
            while b"\n" in pending:
                line, pending = pending.split(b"\n", 1)
                bridge.append(line.decode())
                os.write(master, b"OK\n")


def chip_tool(*cmd, timeout=120):
    p = subprocess.Popen([chip_tool_path, *cmd, "--storage-directory", "chip-tool-store"],
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    end = time.time() + timeout
    while p.poll() is None and time.time() < end:
        pump(0.2)
    if p.poll() is None:
        p.kill()
    return p.stdout.read()


def last(channel):
    sent = [c for c in bridge if c.startswith(f"SET {channel} ")]
    return int(sent[-1].split()[-1]) if sent else None


def relay():
    try:
        with open("relay") as f:
            return f.read().strip()
    except OSError:
        return None


def check(name, ok, detail=""):
    print(f"{'PASS' if ok else 'FAIL'}: {name} {detail}")
    if not ok:
        failures.append(name)


def attr(out, name):
    found = re.findall(rf"{name}: (\S+)", out)
    return found[-1] if found else None


try:
    pump(5)
    if app.poll() is not None:
        failures.append("app exited during startup")
        raise SystemExit
    check("boots and drives the bridge", last("FAN") == 0, f"(bridge: {bridge[:3]})")

    out = chip_tool("pairing", "onnetwork", "1", "20202021", timeout=180)
    check("commissions over IP", "Device commissioning completed with success" in out)

    # Remote fan write: 70% user → 30 + 0.7 * 70 = 79% power on the
    # linear curve with the 30% floor; MUA threshold 60 closes the relay.
    chip_tool("fancontrol", "write", "percent-setting", "70", "1", "1")
    pump(5)
    check("remote fan write ramps the fan", last("FAN") == 79, f"(last FAN {last('FAN')})")
    check("MUA relay closes above threshold", relay() == "1")
    out = chip_tool("fancontrol", "read", "percent-current", "1", "1")
    check("PercentCurrent reports the speed", attr(out, "PercentCurrent") == "70",
          f"(got {attr(out, 'PercentCurrent')})")

    chip_tool("onoff", "on", "1", "2")
    pump(2)
    on_level = last("LIGHT")
    check("remote light on", bool(on_level), f"(last LIGHT {on_level})")
    chip_tool("levelcontrol", "move-to-level", "127", "0", "0", "0", "1", "2")
    pump(2)
    check("remote level change", last("LIGHT") not in (None, 0, on_level),
          f"(last LIGHT {last('LIGHT')})")

    # Wall switch: hold fan down → ramp off; Matter sees the new setting.
    with open("inputs", "w") as f:
        f.write("1011")
    pump(0.6)
    with open("inputs", "w") as f:
        f.write("1111")
    pump(4)
    check("wall hold ramps the fan off", last("FAN") == 0, f"(last FAN {last('FAN')})")
    check("MUA relay opens", relay() == "0")
    out = chip_tool("fancontrol", "read", "percent-setting", "1", "1")
    check("wall change is reported to Matter", attr(out, "PercentSetting") == "0",
          f"(got {attr(out, 'PercentSetting')})")
except SystemExit:
    pass
finally:
    app.terminate()
    try:
        app.wait(timeout=15)
    except subprocess.TimeoutExpired:
        app.kill()
        app.wait()

if "app exited during startup" not in failures:
    check("SIGTERM exits cleanly", app.returncode == 0, f"(rc {app.returncode})")

if failures:
    print(f"\n{len(failures)} check(s) failed; app log:\n")
    with open("app.log") as f:
        sys.stdout.write(f.read()[-20000:])
    sys.exit(1)
print("\nall end-to-end checks passed")
