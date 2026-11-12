#!/usr/bin/env python3
"""Verify the I/O status dots on the service page (display page 6).

The mock reports fixed IO states from /energy_manager/state:
  inputs         [True, False, True, False]
  relays         [False, True]
  sg_ready_outputs [True, False]
The panel draws one dot per state: C_GREEN (0x3AD07F) when active,
C_MUTED (0x8899AA) otherwise. This test screenshots page 6 in the
simulator and checks the centre pixel of each of the 8 dots.

Usage: python3 tools/test_io_status.py
Exit code 0 = all dots correct, 1 = at least one wrong.
"""
import os
import signal
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOCK_PORT = 8083
SHOT = "/tmp/test_io_status.png"

# Expected dot colours in display order: inputs 1-4, relays 1-2, SG 1-2.
EXPECTED = [
    (0x3A, 0xD0, 0x7F),  # Eingang 1 active
    (0x88, 0x99, 0xAA),  # Eingang 2 inactive
    (0x3A, 0xD0, 0x7F),  # Eingang 3 active
    (0x88, 0x99, 0xAA),  # Eingang 4 inactive
    (0x88, 0x99, 0xAA),  # Relais 1 inactive
    (0x3A, 0xD0, 0x7F),  # Relais 2 active
    (0x3A, 0xD0, 0x7F),  # SG 1 active
    (0x88, 0x99, 0xAA),  # SG 2 inactive
]
NAMES = ["Eingang 1", "Eingang 2", "Eingang 3", "Eingang 4",
         "Relais 1", "Relais 2", "SG 1", "SG 2"]


def dot_centers():
    # I/O STATUS card at page-relative (16, 272), page at screen y 68;
    # dots card-relative at (44 + i*95, 50), 12x12 -> centre +5.
    for i in range(8):
        yield 16 + 44 + i * 95 + 5, 68 + 272 + 50 + 5


def close(a, b, tol=40):
    return all(abs(x - y) <= tol for x, y in zip(a, b))


def main():
    # A leftover mock would serve stale scenario data and lie.
    subprocess.run(
        "pgrep -f warp_mock.py | xargs -r kill -9", shell=True)
    mock = subprocess.Popen(
        [sys.executable, os.path.join(ROOT, "tools/warp_mock/warp_mock.py"),
         "--port", str(MOCK_PORT)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(1.5)
        env = dict(os.environ, SDL_VIDEODRIVER="dummy")
        r = subprocess.run(
            ["./run.sh", "--shot", SHOT, "--page", "6", "--after", "6",
             "--no-sd",
             "--url", "http://localhost:%d" % MOCK_PORT],
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=240)
        if r.returncode != 0 or not os.path.isfile(SHOT):
            print("FAIL: simulator shot failed")
            print(r.stdout[-500:], r.stderr[-500:])
            return 1

        from PIL import Image
        im = Image.open(SHOT).convert("RGB")
        failures = 0
        for i, ((x, y), want) in enumerate(zip(dot_centers(), EXPECTED)):
            got = im.getpixel((x, y))
            ok = close(got, want)
            print("%s %-10s want #%02X%02X%02X got #%02X%02X%02X  %s"
                  % ("PASS" if ok else "FAIL", NAMES[i],
                     want[0], want[1], want[2], got[0], got[1], got[2],
                     "" if ok else "<--"))
            failures += 0 if ok else 1
        print("---")
        print("RESULT: %s (%d/8 dots correct)"
              % ("PASS" if failures == 0 else "FAIL", 8 - failures))
        return 1 if failures else 0
    finally:
        mock.send_signal(signal.SIGKILL)
        mock.wait()


if __name__ == "__main__":
    sys.exit(main())
