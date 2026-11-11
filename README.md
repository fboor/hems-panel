# hems-panel — Home Energy Manager panel

An ESP32-S3 panel (7" Waveshare, 800x480) that shows the live state of a WARP
Energy Manager V2: grid per phase and exchange, PV, household, battery
SOC/power, wallboxes, SG-Ready and day-ahead exchange prices. Six pages cover
the energy flow, the current values, the 24 h balance, the wallbox charge modes,
the heating and the service information. The desktop emulator runs the same
LVGL UI source as the firmware and talks to the same WARP HTTP API.

![panel](screenshots/panel.png)

## Quick start

```sh
./run.sh                     # emulator with the module-API mock on :8082
./run.sh --shot out.png      # render one frame and exit
./run.sh --page 1 --shot flow.png
./run.sh --probe             # print the mapped values, no window
```

Firmware build and upload:

```sh
./flash.sh --build-only      # build only, no board needed
./flash.sh                   # modified board: build + upload
./flash.sh --factory         # factory board: build + upload
```

A blank panel can be flashed from the browser: start at
[docs/install/index.html](docs/install/index.html) or read the German quick guide
in [docs/install/anleitung.md](docs/install/anleitung.md).

## Documentation

The full documentation lives in [`docs/`](docs/README.md). It is ordered from
installation to development:

| Document | Contents |
|---|---|
| [Installation guide (German)](docs/install/anleitung.md) | flash a blank panel and run the first-time setup |
| [Hardware](docs/hardware.md) | board, pin map, backlight variants, bring-up checklist |
| [Firmware](docs/firmware.md) | build, flash, partitions, merged images, serial log, recovery |
| [First-time setup](docs/setup.md) | setup access point, QR code, portal, mDNS, OTA |
| [Pages and UI](docs/ui.md) | the six pages, navigation, flow view, header and service page |
| [Energy values](docs/energy.md) | counters, autarky, solar forecast, exchange prices |
| [Wallboxes](docs/wallbox.md) | measured and calculated power, charge modes |
| [Emulator and mock](docs/emulator.md) | desktop build, scenarios, real device, checks |
| [WARP module API](docs/warp-api.md) | endpoints, meter mapping, flat JSON fallback |

## Status

The module-API adapter, the emulator, the mock and the UI are verified against a
real WARP Energy Manager V2 and by rendered frames. The firmware compiles for the
7" board; hardware bring-up is still the open item. The details are in the
[documentation index](docs/README.md#status) and [`TODO.md`](TODO.md).

## License

MIT, Copyright (c) 2026 hems-panel contributors.

Third-party components keep their own licenses: LVGL and WiFiManager
(MIT), the Arduino-ESP32 core (Apache-2.0, with LGPL parts in the
underlying libraries), Montserrat (SIL OFL 1.1), Font Awesome Free
(CC BY 4.0), Material Design Icons (Apache-2.0) and DejaVu Sans
(Bitstream Vera). The background photograph in `src/chart_bg.c`
(solar panel with a leaf) is (c) 2026 Florian Boor, Creative Commons
CC BY 3.0 or later.
