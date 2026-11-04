# WARP Energy Manager V2 panel

An ESP32-S3 panel (7" Waveshare, 800x480) that shows the live state of a WARP
Energy Manager V2: grid per phase and exchange, PV, external/S0, household,
battery SOC/power/voltage/current, island mode and energy totals. The header
shows autarky and today's solar forecast next to one compact connection-state
chip.

The desktop emulator is the primary development target. It runs the same LVGL UI
source as the firmware and talks to the same WARP HTTP API.

![panel](screenshots/panel.png)

The first page is a live energy-flow view: PV, grid, battery and wallbox
around the house hub, with link width and particle speed following the actual
power. The grid node uses the same MDI transmission-tower icon as `rct-panel`. Flow
particles only redraw small areas, and the animation timer stops when the page
is hidden. The energy overview, history and service pages follow in their
previous order.

The header keeps one connection chip: a WiFi glyph (green when the network is
up) plus `Kein WLAN`, `Getrennt`, `Daten` or `Keine Daten`. It sits at the far
right, where the old `Netz` badge used to be. The freed space shows autarky and
today's solar forecast, both with their captions below the values. Autarky comes
from the WARP energy counters when available; without them the panel falls back
to the live house/grid ratio.

![energy flow](screenshots/flow.png)

## Why the earlier attempt showed no data

The `rct-panel` emulator only knows three device drivers: `SIM` (its own mock),
`RCT` (TCP) and `OIG` (TCP). It has no WARP HTTP driver. Pointing it at a WARP
device therefore connects nothing useful: the data source and the display never
meet.

The other trap is that a real WARP firmware does **not** serve a flat `/json`
endpoint (`/json` is 404). It exposes modules instead:

```
/info/name, /info/version
/meters/N/config, /meters/N/value_ids, /meters/N/values
/power_manager/low_level_state
/charge_manager/state
/solar_forecast/state
```

`src/warp/WarpApi.h` discovers the configured meter slots, identifies them by
their WARP location (Grid, Battery, Load, PV) and maps the values by
`MeterValueID`. `src/warp/WarpParse.h` remains as a fallback for a flat JSON
bridge.

## Quick start (emulator)

Requirements: `g++`, SDL2, zlib, LVGL sources. LVGL is taken from the sibling
PlatformIO install by default:

```
../rct-panel/.pio/libdeps/esp32-s3/lvgl
```

Override with `LVGL_DIR=/path/to/lvgl`.

```sh
./run.sh                     # starts the module-API mock on :8082, opens the window
./run.sh --shot out.png      # render one frame and exit
./run.sh --shot flow.png --page 1   # render a specific page (1..4)
./run.sh --shot setup.png --setup   # render the Wi-Fi setup screen
./run.sh --probe             # print the mapped values, no window
./run.sh --verbose           # log every HTTP request (off by default)
```

The mock serves the real module API and also keeps a flat `/json` for the
fallback parser. Its values drift (`--live`), so the page visibly updates every
10 s.

Scenarios:

```sh
./run.sh --url 'http://localhost:8082?scenario=island'
./run.sh --shot island.png --url 'http://localhost:8082?scenario=island'
```

Available: `midsummer`, `evening`, `island`, `low_solar`, `grid_failure`,
`wallbox`, `wallbox_3p`, `wallbox_meter`. The query string is carried through to
every module request, so the emulator sees the selected scenario without
restarting the mock. `wallbox` and `wallbox_3p` exercise the calculated power
(1 and 3 phases), `wallbox_meter` adds a meter with location `Charger`.

## Real device

The WEM2 on the development network is at `192.168.1.222`:

```sh
./run.sh --url http://192.168.1.222
./run.sh --probe --url http://192.168.1.222
SDL_VIDEODRIVER=dummy ./run.sh --url http://192.168.1.222 \
  --shot screenshots/real.png --after 4
```

`--probe` prints the discovered meter slots and the mapped values, so a broken
field mapping is visible immediately. It falls back to printing the raw body if
the URL is a flat JSON endpoint.

### Meter mapping

The adapter reads the slot configuration and value IDs, then selects the values
the UI needs:

| Role | WARP location | Values |
|---|---|---|
| grid | 4 | power L1/L2/L3 `39/48/57`, total `74`, import `209`, export `211`, voltage `1/2/3`, frequency `361` |
| battery | 5 | voltage `10`, current `396`, power `404`, charge `413`, discharge `415`, SOC `387` |
| household | 6 | power L1/L2/L3 `39/48/57`, total `74`, energy `215` |
| PV | 7 | string power `442/443`, total `452`, string energy `454/456`, total `472` |
| wallbox | 2 or 8 | power L1/L2/L3 `39/48/57`, total `74` |

A meter is also matched by its `virtual_meter` value and, as a last resort, by
keywords in `display_name` ("Netz", "Akku", "PV", "Verbrauch", ...). If no grid
or battery meter is configured, the adapter falls back to
`/power_manager/low_level_state` for `power_at_meter`, `power_at_battery` and
`battery_soc`.

Signs: grid positive = import, negative = export. Battery positive = charging,
negative = discharging. Island mode is inferred when all grid phase voltages are
near zero while PV or the battery is active.

### Wallbox power

The wallbox power comes from a meter with location `Charger` (2) or `Vehicle`
(8) when one is configured. Without a wallbox meter the panel calculates it from
the allocated current in `/charge_manager/state` (`ac`, mA) and the measured
grid phase voltages:

```
P = ac / 1000 * (V1 + V2 + V3)   # 3 allocated phases
P = ac / 1000 * V1               # 1 allocated phase
```

`ap` (allocated phases) selects 1 or 3 phases; `sp` (supported phases) and
`is_3phase` are fallbacks. Missing phase voltages fall back to 230 V, so the
estimate also works in island mode while the grid meter is off.

The load meter normally measures the whole house connection, including the
wallbox. The panel subtracts the wallbox power from the displayed household
power (clamped at zero) so the house value shows the household consumption only.
The wallbox card marks calculated values as `geschätzt`.

![wallbox](screenshots/wallbox.png)

### Flat fallback field names

`src/warp/WarpParse.h` maps a flat JSON object to `WarpData`. Each field accepts
a few aliases; the first match wins.

| UI | JSON keys (first match) |
|---|---|
| grid L1/L2/L3 | `grid_power_l1..l3`, `grid_l1..l3` |
| grid exchange | `grid_exchange`, `grid_power`, `grid_power_total` |
| PV strings | `pv_power_a`, `pv_power_b` |
| PV total | `pv_power`, `pv_power_total` (else A+B) |
| external / S0 | `external_power`, `s0_power` |
| household L1/L2/L3 | `household_power_l1..l3`, `house_power_l1..l3` |
| household total | `household_power`, `house_power` (else sum) |
| battery | `battery_soc`, `battery_power`, `battery_voltage`, `battery_current` |
| island | `island_flag`, `island`, `island_mode` |
| energy totals | `pv_energy_wh`, `house_energy_wh`, `grid_import_wh`, `grid_export_wh`, `battery_charge_wh`, `battery_discharge_wh` |
| today (legacy) | `day_generation_wh`, `day_load_wh`, `day_feed_in_wh`, `day_grid_draw_wh` |
| autarky | `autarky`, `autarkie`, `self_sufficiency` (otherwise calculated from energy totals or live power) |
| forecast | `wh_today`, `forecast_today_wh`, `solar_forecast_today_wh` |
| identity | `device_name`, `firmware_version` |

The energy card shows the cumulative meter totals since commissioning. The
period counters (day/month/year) are not in the UI yet; the WARP daily-history
endpoint is available if they should come from the device rather than from a
panel-side baseline.

## Layout

```
src/warp/       data model, module-API adapter, JSON scanner, flat fallback
src/ui/         LVGL screen, shared by emulator and firmware
sim/            SDL2 emulator, POSIX HTTP client, lv_conf override
tools/warp_mock/  module-API mock with scenarios
boards/         PlatformIO board definitions (Waveshare 7", Guition 4")
partitions/     16 MB OTA partition table
flash.sh        build + upload wrapper for both backlight variants
include/        lv_conf.h used by both builds
```

## Firmware

The firmware builds the same `src/warp` and `src/ui` sources as the emulator,
plus the display, touch and backlight drivers for the Waveshare 7" board.

### Board variants: the backlight trace

Two images exist, one per electrical build. They are not a runtime setting, and
the difference is a short, not a colour.

| Environment | Command | Board | Backlight |
| --- | --- | --- | --- |
| `esp32-s3-touch-lcd-7` | `./flash.sh` | mod: EXIO2/DISP trace cut, GPIO 6 wired to the enable net | PWM dimming on GPIO 6 |
| `esp32-s3-touch-lcd-7-factory` | `./flash.sh --factory` | factory: EXIO2 still drives DISP | on/off through EXIO2 |

With the cut, EXIO2 drives nothing and GPIO 6 owns the light. Without the cut,
EXIO2 is still on the enable net; a PWM from GPIO 6 would put two push-pull
outputs on the same line while they disagree. The factory image therefore never
touches GPIO 6, and the default image must never be flashed onto an unmodified
board.

If you do not know which board you have, flash the factory image first: it is
electrically safe on both because it never touches GPIO 6. If the backlight then
stays dark, the trace is cut and the default image is the right one.

The choice is `BOARD_CH422G_LCD_DISP_CUT` in
`src/board/waveshare_touch_lcd7/Pins.h`. It defaults to `1`; the factory
environment passes `-DBOARD_CH422G_LCD_DISP_CUT=0`.

### Build and flash

`flash.sh` wraps the PlatformIO build and upload. It refuses to upload when no
serial device is connected; use `--build-only` to build without a board.

```sh
./flash.sh --build-only         # build only, no board needed
./flash.sh                      # modified board: build + upload
./flash.sh --factory            # factory board: build + upload
./flash.sh --erase              # erase the whole 16 MB first
./flash.sh --monitor            # open the serial log afterwards
./flash.sh --port /dev/ttyACM0  # skip auto-detection
```

The upload uses 460800 baud, the monitor 115200. The board profile is
`boards/waveshare-esp32-s3-touch-lcd-7.json`, the partition table
`partitions/16mb_app.csv`. A normal upload writes all four images:

| Offset | Image |
| --- | --- |
| `0x000000` | `bootloader.bin` |
| `0x008000` | `partitions.bin` |
| `0x00e000` | `boot_app0.bin` |
| `0x010000` | `firmware.bin` (app0, limit 7 MB) |

The second OTA slot starts at `0x710000`; SPIFFS at `0xE10000`.

### Merged image (single file)

For flashing without PlatformIO, or for keeping one file per board variant:

```sh
./flash.sh --factory --merged                 # -> .pio/build/.../firmware-merged.bin
./flash.sh --merged /tmp/warp-panel.bin       # choose the output path
```

The merged image contains bootloader, partition table, `boot_app0` and the app,
ready for offset `0x0`. The script prints the exact `esptool.py write_flash`
command, including the `dio` mode PlatformIO uses for this board.

### USB ports and download mode

- Flash through the **UART Type-C** port, labelled `UART` on the board. It is
  the CH343 USB-to-UART bridge with the automatic download circuit. On Linux it
  appears as `/dev/ttyACM*` or `/dev/ttyUSB*`.
- The other **USB Type-C** port is the ESP32-S3 native USB on GPIO 19/20. EXIO5
  switches those pins between USB (low) and CAN (high). After reset EXIO5 is
  high, so this is not the port to flash through; the running firmware sets it
  low, which is why the application log appears there.
- Use a USB **data** cable. Charge-only cables give no port.
- The user must be in the `dialout` group to open the port (on some
  distributions `uucp`): `sudo usermod -aG dialout $USER`, then log in again.
- If the automatic reset does not work, enter download mode by hand: hold
  **BOOT**, press and release **RESET**, then release **BOOT**. Flash, then
  press **RESET** to start the firmware.
- If an upload fails at 460800 baud, add `upload_speed = 115200` to the
  environment in `platformio.ini` and retry.

### Serial log

The application's `Serial` is the native USB CDC port
(`ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=1`), at 115200 baud. It appears
after boot, once the firmware has switched EXIO5 to USB. With both cables
connected, `./flash.sh --monitor` waits for that port and opens it. The UART
port shows only the ROM bootloader output after the application starts.

```sh
pio device monitor -e esp32-s3-touch-lcd-7 --port /dev/ttyACM1 -b 115200
```

### Recovery and erase

`./flash.sh --erase` erases the whole 16 MB flash before flashing. NVS, the
WLAN credentials and the WARP endpoint are lost; the setup access point opens
again on the next boot. Use it after a bad image, when the panel comes from a
different project, or when the partition table has changed.

### First-time setup

On first boot, or when the saved WLAN cannot be reached, the panel opens a
setup access point:

- SSID: `WARP-Panel-XXXX` (the last four hex digits of the chip MAC).
- Password: random per boot, ten characters plus a hyphen, shown on the panel.
- Portal: <http://192.168.4.1/> (the captive portal usually opens by itself).

The panel shows a QR code that contains the WLAN join data
(`WIFI:T:WPA;S:...;P:...;;`), so a phone can join the access point without
typing the password. The same screen shows the SSID, password and portal URL.
The setup AP stays active for three minutes without web activity; after that
the panel restarts if no WLAN was configured.

![Wi-Fi setup screen](screenshots/setup.png)

The portal has two fields:

1. **WLAN**: SSID and password of the local network.
2. **WARP Energy Manager (IP oder Hostname)**: a bare address such as
   `192.168.1.222`, a hostname, or a full URL. It is normalized to
   `http://<address>` before it is stored in NVS. A trailing `/json` is still
   accepted for the flat fallback.

After a successful connection the same configuration page remains available on
the local network, normally at <http://warp-panel.local/> (mDNS) or at the
panel's IP address. It can be used to change the WARP address without wiping
the WLAN credentials. The service page (page 4) shows the current WLAN, panel
IP, WARP endpoint and web setup URL. The panel uses DHCP; only the WARP address
is configured manually.

If a WLAN change made through the local web page cannot be reached, power-cycle
the panel. The AP fallback opens again because the saved connection failed.
OTA is enabled under the hostname `warp-panel`.

The display/touch driver files in `src/display` and `src/board` are ported from
the MIT-licensed `rct-panel` project; their headers carry the original notes on
the panel timings and the CH422G expander.

## Status

- Module-API adapter: verified against the real WEM2 at `192.168.1.222`; all four
  meter slots (grid, PV, household, battery) map to the expected values.
- Emulator, mock and UI: working, verified by rendered frames.
- Firmware: compiles for the 7" board (2.0 MB flash, 53 kB static RAM). Nothing
  has run on the panel yet; display bring-up is the open item. The Wi-Fi setup
  screen and portal are implemented but still need hardware verification.
- Flashing: prepared for the modified and the factory board, with
  `flash.sh`, separate PlatformIO environments and a merged-image workflow.
  No real board has been connected yet, so the upload itself, the backlight
  level, display, touch, Wi-Fi and the native-USB log still need verification.

## License

MIT.
