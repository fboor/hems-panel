# Desktop emulator and mock

The emulator is the primary development target. It compiles the same `src/warp`
and `src/ui` sources as the firmware, draws into an SDL2 window instead of the
RGB panel and uses a POSIX HTTP client instead of the ESP32 network stack. A
frame rendered here is the frame the panel draws.

## Requirements

- `g++`, SDL2 and zlib
- LVGL sources. The build uses the project's PlatformIO library directory by
  default:

  ```
  .pio/libdeps/esp32-s3-touch-lcd-7/lvgl
  ```

  Override it with `LVGL_DIR=/path/to/lvgl` if LVGL lives somewhere else. If the
  directory does not exist yet, run `pio run -e esp32-s3-touch-lcd-7` once so
  PlatformIO fetches LVGL.

## Running

`./run.sh` starts the module-API mock on port 8082 and opens the emulator:

```sh
./run.sh                     # mock on :8082 + emulator window
./run.sh --shot out.png      # render one frame and exit
./run.sh --page 1 --shot flow.png
./run.sh --page 6 --shot service.png
./run.sh --setup --shot setup.png
./run.sh --probe             # print the mapped values, no window
./run.sh --verbose           # log every HTTP request
./run.sh --url http://192.168.1.222   # real device, no mock
```

The page numbers are 1–6, in the same order as the on-screen pages
([ui.md](ui.md#page-order)). `--after N` waits N seconds before the shot. The
default is 2 seconds; use a larger value when a real device is slow to answer.

Set `HEMS_UI_DEBUG_BAND=1` to log the price-band segment geometry while tuning
the history chart. The firmware build uses the same flag as
`-DHEMS_UI_DEBUG_BAND=1` (see `platformio.ini`).

## SD-card log

The emulator writes the same CSV history log as the panel, into a directory on
the host instead of an SD card:

```sh
./run.sh                              # /tmp/hems-panel-sd/hist/HEMS-YYYYMM.csv
./run.sh --sd /tmp/my-hems-log        # any other directory
./run.sh --no-sd                      # no log, no restore
```

The directory is created on start. The emulator appends one row per minute,
using the shipped `src/storage/CsvRow.h` format and the same parked-row queue;
the service page shows the directory in its `SD:` line. On the next start the
newest rows are read back into the 24-hour chart, so a rendered frame can show
the restored history.

This is a host-side stand-in for `src/storage/SdLog.cpp`: the card mount, the
SPI wiring and the Arduino SD library are not involved. The row format and the
queue are shared with the firmware and are checked by
`tools/sd_format_test/run.sh`.

Because the default directory persists between runs, a screenshot taken after
an earlier session can contain the restored history. Pass `--no-sd` for a frame
that only depends on the mock data.

The mock serves the module API and also keeps a flat `/json` endpoint for the
fallback parser. Its values drift in `--live` mode, so the page visibly updates
every 10 seconds.

## Scenarios

The scenario is selected with a query string on the URL. The query is carried
through to every module request, so the emulator sees the selected scenario
without restarting the mock.

```sh
./run.sh --url 'http://localhost:8082?scenario=island'
./run.sh --shot island.png --url 'http://localhost:8082?scenario=island'
```

Available scenarios:

| Scenario | What it exercises |
|---|---|
| `midsummer` | normal sunny day |
| `evening` | no PV, battery discharging, grid export |
| `island` | grid off, PV and battery active |
| `low_solar` | low PV, grid import |
| `grid_failure` | grid failure with island operation |
| `wallbox` | one-phase charging, calculated power |
| `wallbox_3p` | three-phase charging, calculated power |
| `wallbox_meter` | wallbox with its own meter |
| `wallboxes` | three chargers with different states and modes |

`wallbox` and `wallbox_3p` exercise the calculated power (1 and 3 phases);
`wallbox_meter` adds a meter with location `Charger`; `wallboxes` configures
three boxes for the configuration page.

The mock also accepts the charge-mode commands and remembers the result, so the
mode buttons can be exercised without a real device:

```sh
curl -X POST -d '{"mode":2}' http://localhost:8082/power_manager/charge_mode_update
curl -X POST -d '2' 'http://localhost:8082/charge_manager/charge_modes/0?scenario=wallboxes'
```

Commands answer with an empty `200`, like the WARP firmware. The heating override
has a matching endpoint; the body carries the Unix minute until which the
override lasts (`0` cancels it):

```sh
curl -X POST -d '{"override_until":0}' http://localhost:8082/heating/sgr_blocking_override_update
```

## Real device

The development WEM2 is reachable at `192.168.1.222`:

```sh
./run.sh --url http://192.168.1.222
./run.sh --probe --url http://192.168.1.222
SDL_VIDEODRIVER=dummy ./run.sh --url http://192.168.1.222 \
  --shot screenshots/real.png --after 4
```

`--probe` prints the discovered meter slots and the mapped values, so a broken
field mapping is visible immediately. If the URL is a flat JSON endpoint,
`--probe` falls back to printing the raw body.

## Checks

`tools/test_io_status.py` starts the mock, renders the service page and checks
the centre pixel of each of the eight I/O dots. It needs the emulator build and
Pillow, and it is a useful smoke test after changing the service page:

```sh
python3 tools/test_io_status.py
```
