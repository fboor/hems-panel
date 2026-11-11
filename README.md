# hems-panel — Home Energy Manager panel

A wall-mounted touch panel for the **WARP Energy Manager V2**. It shows the live
energy state of the home at a glance: where the power comes from, where it goes,
how self-sufficient the house is, and what the wallboxes and heating are doing.
The panel talks to the WARP device over the local network and needs no cloud
account of its own.

**Design goals:** easy to set up and use, as local as possible, and no
additional infrastructure beyond the panel and the WARP Energy Manager.

![Energy flow page](screenshots/flow.png)

## Features

- **Energy flow at a glance** — an animated view of PV, grid, battery,
  household and wallbox power, including island operation.
- **Live values and counters** — current power, battery state, and cumulative
  generation, consumption, import and export.
- **24-hour energy balance** — power history, wallbox series and the exchange
  price band, with sample values on tap.
- **Wallbox control** — see every charger and change its charge mode directly
  on the panel.
- **Heating and SG-Ready** — operating state, temperatures and a temporary
  party-mode override.
- **Autarky and solar forecast** — today's and overall self-sufficiency plus
  the remaining and planned PV generation.
- **Dynamic tariffs** — day-ahead exchange prices and the resulting import
  price.
- **Simple setup** — first-boot Wi-Fi portal with QR code, later configuration
  from the built-in web page, and OTA updates.

The interface has six pages: Energy flow, Energy, Energy balance, Wallboxes,
Heating and Service. Details are in [Pages and UI](docs/ui.md).

## Supported hardware

| Hardware | Role |
|---|---|
| [Waveshare ESP32-S3-Touch-LCD-7](https://www.waveshare.com/esp32-s3-touch-lcd-7.htm) — 7", 800 × 480, capacitive touch | the panel |
| [WARP Energy Manager V2](https://warp-charger.com/en/products/warp-energy-manager) | provides the energy data over the local network |

Both the factory board and the modified board with the backlight trace cut for
PWM dimming are supported. The **ESP32-S3-Touch-LCD-7B** (1024 × 600) is not.
See [Hardware](docs/hardware.md) for the pin map, backlight variants and the
bring-up checklist.

## Install

A blank panel can be flashed from the browser: start at
[docs/install/index.html](docs/install/index.html) or read the German quick
guide in [docs/install/anleitung.md](docs/install/anleitung.md). On first boot
the panel opens a setup network with a QR code; enter the WLAN and the WARP
address and it starts showing data.

To build and flash from source, see [Firmware](docs/firmware.md). In short:

```sh
./flash.sh --build-only      # build only, no board needed
./flash.sh                   # modified board: build + upload
./flash.sh --factory         # factory board: build + upload
```

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

## Development

The desktop emulator runs the same LVGL UI source as the firmware and talks to
the same WARP HTTP API. It is the primary development target and needs no panel:

```sh
./run.sh                     # emulator with the module-API mock on :8082
./run.sh --shot out.png      # render one frame and exit
./run.sh --probe             # print the mapped values, no window
```

Scenarios, mock commands and checks are in
[Emulator and mock](docs/emulator.md).

## Status

The panel has been verified against a real WARP Energy Manager V2 and by
rendered frames. The firmware compiles for the 7" board; hardware bring-up is
still the open item. Details are in the
[documentation index](docs/README.md#status).

## License

MIT, Copyright (c) 2026 hems-panel contributors.

Third-party components keep their own licenses: LVGL and WiFiManager
(MIT), the Arduino-ESP32 core (Apache-2.0, with LGPL parts in the
underlying libraries), Montserrat (SIL OFL 1.1), Font Awesome Free
(CC BY 4.0), Material Design Icons (Apache-2.0) and DejaVu Sans
(Bitstream Vera). The background photograph in `src/chart_bg.c`
(solar panel with a leaf) is (c) 2026 Florian Boor, Creative Commons
CC BY 3.0 or later.

## History

This project was started as part of a research study about the abilities of AI
and was implemented by machine workers completely.
