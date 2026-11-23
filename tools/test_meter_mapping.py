#!/usr/bin/env python3
"""Verify that meter roles come from the WARP module configuration.

The `meter_mapping` mock scenario gives the meters misleading display names and
locations: the grid and battery slots report no location, the wallbox meter is
named "PV", one duplicate meter is marked excluded and one slot is unassigned.
Only the module configs identify the first three. The panel must follow them,
skip the excluded duplicate and leave the unknown slot without a role instead of
guessing from the name or treating it as an external meter.

Usage: python3 tools/test_meter_mapping.py
Exit code 0 = all checks pass, 1 = at least one role is wrong.
"""
import os
import re
import signal
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOCK_PORT = 8088

# warpapi::Role, in declaration order in src/warp/WarpApi.h.
NONE, GRID, LOAD, PV, BATTERY, CHARGER, EXTERNAL = range(7)

EXPECTED = {
    "midsummer": [LOAD, PV, GRID, BATTERY],
    "meter_mapping": [LOAD, PV, GRID, BATTERY, CHARGER, NONE, NONE],
}

SLOT = re.compile(r"\s+slot (\d+)\s+location \d+\s+role (\d+)\s+(.*)")


def probe(scenario):
    env = dict(os.environ, SDL_VIDEODRIVER="dummy")
    r = subprocess.run(
        ["./run.sh", "--probe", "--no-sd",
         "--url", "http://localhost:%d?scenario=%s" % (MOCK_PORT, scenario)],
        cwd=ROOT, env=env, capture_output=True, text=True, timeout=240)
    if r.returncode != 0:
        print("FAIL: probe failed for %s" % scenario)
        print(r.stdout[-800:], r.stderr[-800:])
        return None
    found = {}
    for match in SLOT.finditer(r.stdout):
        index = int(match.group(1))
        found[index] = (int(match.group(2)), match.group(3).strip())
    return found


def check(scenario, found):
    if found is None:
        return 1
    want = EXPECTED[scenario]
    failures = 0
    for index, role in enumerate(want):
        got, name = found.get(index, (-1, "<missing>"))
        ok = got == role
        print("%s %-14s slot %d  role %d (want %d)  %s"
              % ("PASS" if ok else "FAIL", scenario, index, got, role, name))
        failures += 0 if ok else 1
    if EXTERNAL in [role for role, _ in found.values()]:
        print("FAIL %-14s an unassigned meter got the external role" % scenario)
        failures += 1
    return failures


def main():
    mock = subprocess.Popen(
        [sys.executable, os.path.join(ROOT, "tools/warp_mock/warp_mock.py"),
         "--port", str(MOCK_PORT)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(1.5)
        failures = 0
        for scenario in EXPECTED:
            failures += check(scenario, probe(scenario))
        print("---")
        print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL"))
        return 1 if failures else 0
    finally:
        mock.send_signal(signal.SIGKILL)
        mock.wait()


if __name__ == "__main__":
    sys.exit(main())
