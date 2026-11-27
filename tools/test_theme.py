#!/usr/bin/env python3
"""Verify the runtime theme switch on the service page (display page 6).

Checks that the dark and light palettes repaint both the page background and
the cards, that tapping the THEME button switches to the next palette without
a restart, and that the light flow page uses the drawn schematic instead of
the photograph. The chart is checked too: its card must follow the theme while
the series keep their theme-independent colours.

Usage: python3 tools/test_theme.py
Exit code 0 = all checks pass, 1 = at least one failed.
"""
import math
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from collections import Counter

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOCK_PORT = 8084
DARK_BG = (8, 16, 24)
DARK_CARD = (16, 28, 41)
LIGHT_BG = (230, 238, 238)
LIGHT_CARD = (255, 255, 255)
SCHEMATIC_GRID = (197, 210, 205)
# Centre of the THEME button: page at (16, 0), button at (542, 9), 200x40,
# and the page starts at screen y 68.
THEME_BUTTON = (658, 97)
# Chart series colours after RGB565 conversion, from Theme.h/kSeriesColors.
SERIES_GREEN = (41, 161, 41)
SERIES_ORANGE = (255, 127, 12)
CSV_HEADER = (
    "ts,grid_l1,grid_l2,grid_l3,grid,pv_a,pv_b,pv,external,"
    "house_l1,house_l2,house_l3,house,bat_power,soc,bat_voltage,bat_current,"
    "wb1_power,wb2_power,wb3_power,wb_state,wb_mode,wb_current,island,"
    "heat_enabled,sgr_blocking,sgr_extended,p14enwg,autarky,autarky_today,"
    "forecast_today_wh,forecast_remaining_wh,pv_total_wh,house_total_wh,"
    "grid_import_wh,grid_export_wh,bat_charge_wh,bat_discharge_wh,temp_c"
)


def close(a, b, tol=18):
    return all(abs(x - y) <= tol for x, y in zip(a, b))


def count_near(pixels, target, tol=12):
    return sum(1 for k in pixels if close(k, target, tol))


def shot(name, page, extra=(), sd=None):
    path = "/tmp/test_theme_%s.png" % name
    cmd = ["./run.sh", "--shot", path, "--page", str(page), "--after", "3",
           "--url", "http://localhost:%d" % MOCK_PORT]
    cmd.extend(("--sd", sd) if sd is not None else ("--no-sd",))
    cmd.extend(extra)
    r = subprocess.run(cmd, cwd=ROOT, env=dict(os.environ,
                                               SDL_VIDEODRIVER="dummy"),
                       capture_output=True, text=True, timeout=240)
    if r.returncode != 0 or not os.path.isfile(path):
        print("FAIL: simulator shot %s failed" % name)
        print(r.stdout[-500:], r.stderr[-500:])
        return None
    return Image.open(path).convert("RGB")


def make_sd_history(root):
    """Write two hours of continuous rows so the chart has visible series."""
    hist = os.path.join(root, "hist")
    os.makedirs(hist, exist_ok=True)
    path = os.path.join(hist, "HEMS-%s.csv" % time.strftime("%Y%m", time.gmtime()))
    now = int(time.time())
    rows = []
    for i in range(121):
        ts = now - (120 - i) * 60
        pv = 900 + 250 * (0.5 + 0.5 * math.sin(i / 12.0))
        house = 700 + 100 * (0.5 + 0.5 * math.cos(i / 17.0))
        grid = 200 + 80 * (0.5 + 0.5 * math.sin(i / 9.0))
        values = [ts, grid / 3, grid / 3, grid / 3, grid,
                  pv / 2, pv / 2, pv, 0,
                  house / 3, house / 3, house / 3, house,
                  100, 80, 48.0, 0.0,
                  0, 0, 0, 0, 0, 0, 0,
                  1, 0, 0, 0, 85.7, 51.2, 0, 0,
                  24100, 12000, 16004, 16003, 2561, 4000, 5.2]
        rows.append(",".join(str(v) if isinstance(v, int) else "%.1f" % v
                             for v in values))
    with open(path, "w") as f:
        f.write(CSV_HEADER + "\n" + "\n".join(rows) + "\n")
    return root


def main():
    sd_dir = None
    subprocess.run("pgrep -f warp_mock.py | xargs -r kill -9", shell=True)
    mock = subprocess.Popen(
        [sys.executable, os.path.join(ROOT, "tools/warp_mock/warp_mock.py"),
         "--port", str(MOCK_PORT)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(1.5)
        failures = 0
        sd_dir = make_sd_history(tempfile.mkdtemp(prefix="hems-theme-sd-"))

        dark = shot("dark", 6)
        light = shot("light", 6, ("--theme", "light"))
        switched = shot("switched", 6,
                        ("--tap", str(THEME_BUTTON[0]), str(THEME_BUTTON[1]),
                         "--tapdelay", "0"))
        flow = shot("flow_light", 1, ("--theme", "light"))
        chart_dark = shot("chart_dark", 3, sd=sd_dir)
        chart_light = shot("chart_light", 3, ("--theme", "light"), sd=sd_dir)
        if None in (dark, light, switched, flow, chart_dark, chart_light):
            return 1

        for name, im, bg, card in [
            ("dark initial", dark, DARK_BG, DARK_CARD),
            ("light from CLI", light, LIGHT_BG, LIGHT_CARD),
            ("light after tap", switched, LIGHT_BG, LIGHT_CARD),
        ]:
            gotBg = im.getpixel((5, 5))
            gotCard = im.getpixel((30, 100))
            ok = close(gotBg, bg) and close(gotCard, card)
            print("%s %-16s bg %s card %s" %
                  ("PASS" if ok else "FAIL", name, gotBg, gotCard))
            failures += 0 if ok else 1

        colors = Counter(flow.crop((0, 68, 800, 424)).getdata())
        gridPixels = colors[SCHEMATIC_GRID]
        ok = gridPixels > 1000 and close(colors.most_common(1)[0][0],
                                         LIGHT_BG, 18)
        print("%s %-16s grid px %d" %
              ("PASS" if ok else "FAIL", "light schematic", gridPixels))
        failures += 0 if ok else 1

        # The card follows the palette, but the series must keep their identity.
        for name, im, card in [
            ("dark chart card", chart_dark, DARK_CARD),
            ("light chart card", chart_light, LIGHT_CARD),
        ]:
            gotCard = im.getpixel((30, 100))
            ok = close(gotCard, card)
            print("%s %-16s card %s" %
                  ("PASS" if ok else "FAIL", name, gotCard))
            failures += 0 if ok else 1
        for name, im in [("dark", chart_dark), ("light", chart_light)]:
            pixels = list(im.crop((12, 68, 778, 308)).getdata())
            green = count_near(pixels, SERIES_GREEN)
            orange = count_near(pixels, SERIES_ORANGE)
            ok = green >= 100 and orange >= 100
            print("%s %-16s green %d orange %d" %
                  ("PASS" if ok else "FAIL", "%s series" % name,
                   green, orange))
            failures += 0 if ok else 1

        print("---")
        print("RESULT: %s (%d/8 checks correct)" %
              ("PASS" if failures == 0 else "FAIL", 8 - failures))
        return 1 if failures else 0
    finally:
        mock.send_signal(signal.SIGKILL)
        mock.wait()
        if sd_dir is not None:
            shutil.rmtree(sd_dir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
