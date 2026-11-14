#!/usr/bin/env python3
"""Verify today's energy totals when the daily history has a gap.

The WARP keeps cumulative meter counters per day. If it missed a day (for
example because its network was down), the next day's counter delta spans two
days and would show roughly double the real consumption and generation. The
panel must instead fall back to today's 5-minute power history, which never
reaches beyond the current day.

The mock's `daily_gap` scenario removes yesterday's grid/PV/battery counters
and leaves the day before as the last complete row, so the old "last valid
day" baseline would inflate the totals. Both scenarios must report the
scenario's day values.

Usage: python3 tools/test_daily_gap.py
Exit code 0 = all checks pass, 1 = at least one value is wrong.
"""
import datetime
import os
import re
import signal
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOCK_PORT = 8086

EXPECTED = {
    "gen": 12500.0,
    "load": 8300.0,
    "draw": 1328.0,
    "feed": 3100.0,
    "charge": 1800.0,
    "discharge": 1200.0,
}

DAY_SUMS = re.compile(
    r"day sums: gen (-?[\d.]+) Wh\s+load (-?[\d.]+) Wh\s+"
    r"draw (-?[\d.]+) Wh\s+feed (-?[\d.]+) Wh")
DAY_BATTERY = re.compile(
    r"day battery: charge (-?[\d.]+) Wh\s+discharge (-?[\d.]+) Wh")


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
    sums = DAY_SUMS.search(r.stdout)
    battery = DAY_BATTERY.search(r.stdout)
    if sums is None or battery is None:
        print("FAIL: no day sums for %s" % scenario)
        print(r.stdout[-800:])
        return None
    values = {
        "gen": float(sums.group(1)),
        "load": float(sums.group(2)),
        "draw": float(sums.group(3)),
        "feed": float(sums.group(4)),
        "charge": float(battery.group(1)),
        "discharge": float(battery.group(2)),
    }
    return values


def check(scenario, values):
    failures = 0
    for name, want in EXPECTED.items():
        got = values[name]
        ok = abs(got - want) <= 1.0
        print("%s %-10s %-10s %8.0f Wh (want %.0f)"
              % ("PASS" if ok else "FAIL", scenario, name, got, want))
        failures += 0 if ok else 1
    return failures


def main():
    # The gap needs a row two days back, so the test only makes sense from the
    # third day of the month onward.
    if datetime.datetime.now().day < 3:
        print("SKIP: the daily-gap scenario needs a day two days back")
        return 0

    mock = subprocess.Popen(
        [sys.executable, os.path.join(ROOT, "tools/warp_mock/warp_mock.py"),
         "--port", str(MOCK_PORT)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(1.5)
        failures = 0
        for scenario in ("midsummer", "daily_gap"):
            values = probe(scenario)
            if values is None:
                return 1
            failures += check(scenario, values)
        print("---")
        print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL"))
        return 1 if failures else 0
    finally:
        mock.send_signal(signal.SIGKILL)
        mock.wait()


if __name__ == "__main__":
    sys.exit(main())
