# Home Energy Manager panel — documentation

This folder is the reference for the panel. The top-level
[`README.md`](../README.md) is the short overview; this page is the entry point
for everything that needs more than a paragraph.

The documents are ordered the way a reader usually needs them: install and
commission the panel first, then the hardware and firmware behind it, then the
pages and values on the screen, and finally the development tools and the WARP
API.

## Reading order

1. [Installation guide (German)](install/anleitung.md) — flash a blank panel and
   run the first-time setup.
2. [Hardware](hardware.md) — the board, its pin map, the backlight variants and
   the bring-up checklist.
3. [Firmware](firmware.md) — build, flash, partitions, merged images, USB ports,
   serial log and recovery.
4. [First-time setup](setup.md) — setup access point, QR code, portal, mDNS and
   OTA.
5. [Pages and UI](ui.md) — the six pages, navigation, flow view, header and
   service page.
6. [Energy values](energy.md) — counters, autarky, solar forecast and day-ahead
   exchange prices.
7. [Wallboxes](wallbox.md) — measured and calculated charging power, and the
   charge-mode page.
8. [Emulator and mock](emulator.md) — run the same UI on a desktop, select
   scenarios and talk to a real WARP device.
9. [WARP module API](warp-api.md) — the HTTP endpoints, meter mapping and the
   flat JSON fallback.

## Find it quickly

| If you want to… | Read |
|---|---|
| flash a blank panel from the browser | [install/anleitung.md](install/anleitung.md) |
| build or flash from source | [firmware.md](firmware.md) |
| check a pin, the backlight or the board variant | [hardware.md](hardware.md) |
| join the panel to a WLAN and set the WARP address | [setup.md](setup.md) |
| know what a page or a header chip means | [ui.md](ui.md) |
| understand autarky, forecast or exchange prices | [energy.md](energy.md) |
| change a wallbox charge mode | [wallbox.md](wallbox.md) |
| develop without the panel | [emulator.md](emulator.md) |
| find the WARP value behind a number | [warp-api.md](warp-api.md) |

## Repository layout

```
src/warp/         data model, WARP module-API adapter, JSON scanner, flat fallback
src/ui/           LVGL screen, shared by the emulator and the firmware
src/display/      RGB panel, touch and backlight drivers
src/board/        board pins and the CH422G I/O expander
sim/              SDL2 emulator, POSIX HTTP client, lv_conf override
tools/warp_mock/  module-API mock with scenarios
boards/           PlatformIO board definitions
partitions/       16 MB OTA partition table
docs/             this documentation and the browser installer
screenshots/      rendered pages used by the README and the docs
flash.sh          build and upload wrapper for both backlight variants
include/          lv_conf.h and the firmware version used by both builds
```

## Status

- The module-API adapter, the emulator, the mock and the UI are verified against
  a real WARP Energy Manager V2 and by rendered frames.
- The firmware compiles for the 7" board. Hardware bring-up — backlight,
  display, touch, Wi-Fi setup and the native-USB log — is still the open item;
  the checklist is in [hardware.md](hardware.md#bring-up-checklist).
- The browser installer and the German quick guide live in
  [`install/`](install/); the top-level [`LICENSE`](../LICENSE) covers the
  project code.
