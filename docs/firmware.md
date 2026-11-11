# Firmware: build, flash and recovery

The firmware builds the same `src/warp` and `src/ui` sources as the desktop
emulator, plus the display, touch, backlight and Wi-Fi drivers for the panel.
PlatformIO uses `espressif32@6.8.0` (Arduino-ESP32 core 3.x), LVGL 9 and
WiFiManager as its library dependencies.

## Environments

| Environment | Board | UI | Command |
|---|---|---|---|
| `esp32-s3-touch-lcd-7` | modified board, backlight on GPIO 6 | German | `./flash.sh` |
| `esp32-s3-touch-lcd-7-factory` | factory board, backlight on EXIO2 | German | `./flash.sh --factory` |
| `esp32-s3-touch-lcd-7-en` | modified board | English | `pio run -e esp32-s3-touch-lcd-7-en -t upload` |
| `esp32-s3-touch-lcd-7-factory-en` | factory board | English | `pio run -e esp32-s3-touch-lcd-7-factory-en -t upload` |

German is the default UI language. English is a compile-time build
(`-DWARP_LANG_EN=1`), not a runtime setting.

The two electrical variants are described in [hardware.md](hardware.md#the-backlight-trace).
Flashing the default image onto an unmodified board puts two outputs on the
backlight enable net.

## Build and flash

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

The upload uses 460800 baud; the monitor uses 115200. The board profile is
`boards/waveshare-esp32-s3-touch-lcd-7.json`, the partition table is
`partitions/16mb_app.csv`.

## Flash layout

A normal upload writes four images:

| Offset | Image |
|---|---|
| `0x000000` | `bootloader.bin` |
| `0x008000` | `partitions.bin` |
| `0x00e000` | `boot_app0.bin` |
| `0x010000` | `firmware.bin` (app0, limit 7 MB) |

The second OTA slot starts at `0x710000`; SPIFFS starts at `0xE10000`.

## Merged image

For flashing without PlatformIO, or for keeping one file per board variant:

```sh
./flash.sh --factory --merged                 # -> .pio/build/.../firmware-merged.bin
./flash.sh --merged /tmp/hems-panel.bin       # choose the output path
```

The merged image contains the bootloader, partition table, `boot_app0` and the
application, ready for offset `0x0`. The script prints the exact `esptool.py
write_flash` command, including the `dio` mode PlatformIO uses for this board.

## USB ports and download mode

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

## Serial log

The application's `Serial` is the native USB CDC port (`ARDUINO_USB_MODE=1`,
`ARDUINO_USB_CDC_ON_BOOT=1`), at 115200 baud. It appears after boot, once the
firmware has switched EXIO5 to USB. With both cables connected,
`./flash.sh --monitor` waits for that port and opens it. The UART port shows
only the ROM bootloader output after the application starts.

```sh
pio device monitor -e esp32-s3-touch-lcd-7 --port /dev/ttyACM1 -b 115200
```

## Recovery and erase

`./flash.sh --erase` erases the whole 16 MB flash before flashing. NVS, the WLAN
credentials and the WARP endpoint are lost; the setup access point opens again
on the next boot. Use it after a bad image, when the panel comes from a different
project, or when the partition table has changed.

If the panel cannot reach the configured WLAN after a change, power-cycle it.
The setup access point opens again because the saved connection failed. The
first-boot flow is described in [setup.md](setup.md).

## Firmware updates over the air

The running firmware enables OTA under the hostname `hems-panel`. A panel that
is already installed can be updated over the air with that hostname, or with the
PlatformIO upload target. The browser installer is for a blank panel.
