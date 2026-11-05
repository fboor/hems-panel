# TODO

## Hardware bring-up (Waveshare ESP32-S3-Touch-LCD-7)

- [ ] Flash over the UART Type-C port and confirm the ROM boot log
- [ ] Confirm which backlight variant the board is; use `--factory` if unsure
- [ ] Verify dimming on the cut board or on/off on the factory board
- [ ] Verify display bring-up, colours, orientation and touch
- [ ] Verify the native-USB application log after boot
- [ ] Verify setup AP, QR code, WLAN config and mDNS/OTA

## Values

- [x] Energy panel values: WARP reports kWh (not Wh); converted on ingest and shown as kWh/MWh
- [ ] Energy panel: only house energy (VID 215) available on this device; PV/grid energy IDs not present
- [ ] Verify 1p/3p charge mode display on real wallbox
- [ ] Kill old emulator instances before starting new ones
