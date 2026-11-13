#!/usr/bin/env python3
"""Verify the day-ahead price band states on the energy balance page (3).

The band uses five colours:
  grey        no data
  blue        medium
  green       low
  red         high
  bright green negative price

The mock's `negative_prices` scenario subtracts a midday dip from the synthetic
curve, so at least part of the visible 24 h window is negative even after the
tariff adders. The normal `midsummer` curve must not use the bright-green
state.

Usage: python3 tools/test_price_band.py
Exit code 0 = all checks pass, 1 = at least one colour is missing or wrong.
"""
import os
import signal
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOCK_PORT = 8085
SHOT = "/tmp/test_price_band.png"

# Page content starts below the 68 px header; the band is at page y 306 and
# is 5 px high. Sample its middle row across the band width.
BAND_Y = 68 + 306 + 2
BAND_X0 = 172
BAND_X1 = 720

NO_DATA = (0x99, 0x99, 0x99)
MEDIUM = (0x00, 0x7B, 0xFF)
LOW = (0x2C, 0xA0, 0x2C)
HIGH = (0xD6, 0x27, 0x28)
NEGATIVE = (0x00, 0xFF, 0x00)


def close(a, b, tol=30):
    return all(abs(x - y) <= tol for x, y in zip(a, b))


def count_color(im, want):
    n = 0
    for x in range(BAND_X0, BAND_X1):
        if close(im.getpixel((x, BAND_Y)), want):
            n += 1
    return n


def shot(scenario):
    env = dict(os.environ, SDL_VIDEODRIVER="dummy")
    r = subprocess.run(
        ["./run.sh", "--shot", SHOT, "--page", "3", "--after", "6",
         "--no-sd",
         "--url", "http://localhost:%d?scenario=%s" % (MOCK_PORT, scenario)],
        cwd=ROOT, env=env, capture_output=True, text=True, timeout=240)
    if r.returncode != 0 or not os.path.isfile(SHOT):
        print("FAIL: simulator shot failed for %s" % scenario)
        print(r.stdout[-500:], r.stderr[-500:])
        return None
    from PIL import Image
    return Image.open(SHOT).convert("RGB")


def main():
    mock = subprocess.Popen(
        [sys.executable, os.path.join(ROOT, "tools/warp_mock/warp_mock.py"),
         "--port", str(MOCK_PORT)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(1.5)
        failures = 0

        im = shot("midsummer")
        if im is None:
            return 1
        counts = {
            "no data": count_color(im, NO_DATA),
            "medium": count_color(im, MEDIUM),
            "low": count_color(im, LOW),
            "high": count_color(im, HIGH),
            "negative": count_color(im, NEGATIVE),
        }
        for name, n in counts.items():
            ok = n == 0 if name in ("no data", "negative") else n > 20
            print("%s midsummer %-8s %4d px" % ("PASS" if ok else "FAIL", name, n))
            failures += 0 if ok else 1

        im = shot("negative_prices")
        if im is None:
            return 1
        counts = {
            "medium": count_color(im, MEDIUM),
            "high": count_color(im, HIGH),
            "negative": count_color(im, NEGATIVE),
        }
        for name, n in counts.items():
            ok = n > 20
            print("%s negative  %-8s %4d px" % ("PASS" if ok else "FAIL", name, n))
            failures += 0 if ok else 1

        print("---")
        print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL"))
        return 1 if failures else 0
    finally:
        mock.send_signal(signal.SIGKILL)
        mock.wait()


if __name__ == "__main__":
    sys.exit(main())
