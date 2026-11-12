# SD-card history log

The panel writes one CSV row per minute to the microSD card in the board's TF
slot and reads the newest rows back on boot, so the 24-hour chart survives an
update or a power loss. Without a card the panel runs unchanged; the rows are
then parked in RAM and written as soon as a card appears.

## What is written

One file per calendar month (UTC):

```
/hist/HEMS-202611.csv
/hist/HEMS-202612.csv
```

The first row of each file is the header. Every following row is one minute of
data, in the order the panel received it:

```
ts,grid_l1,grid_l2,grid_l3,grid,pv_a,pv_b,pv,external,
house_l1,house_l2,house_l3,house,bat_power,soc,bat_voltage,bat_current,
wb1_power,wb2_power,wb3_power,wb_state,wb_mode,wb_current,island,
heat_enabled,sgr_blocking,sgr_extended,p14enwg,autarky,autarky_today,
forecast_today_wh,forecast_remaining_wh,pv_total_wh,house_total_wh,
grid_import_wh,grid_export_wh,bat_charge_wh,bat_discharge_wh,temp_c
```

| Column group | Meaning | Unit |
|---|---|---|
| `ts` | unix timestamp of the sample | s |
| `grid_l1..l3`, `grid` | grid power per phase and total, positive = import | W |
| `pv_a`, `pv_b`, `pv` | PV inputs and total | W |
| `external` | external/S0 meter | W |
| `house_l1..l3`, `house` | household power per phase and total | W |
| `bat_power` | battery power, positive = charging | W |
| `soc` | battery state of charge, `-1` = no battery | % |
| `bat_voltage`, `bat_current` | battery voltage and current | V, A |
| `wb1..wb3_power` | wallbox power, `0` = charger not present | W |
| `wb_state`, `wb_mode`, `wb_current` | charge manager state, global charge mode and allocated current | –, –, mA |
| `island` | island operation, `0`/`1` | – |
| `heat_enabled` | heating configured and enabled, `-1` = no heating data | – |
| `sgr_blocking`, `sgr_extended`, `p14enwg` | SG-Ready and §14 EnWG inputs | – |
| `autarky`, `autarky_today` | self-sufficiency, `-1` = not reported | % |
| `forecast_today_wh`, `forecast_remaining_wh` | solar forecast, `-1` = not reported | Wh |
| `pv_total_wh`, `house_total_wh` | lifetime generation and consumption | Wh |
| `grid_import_wh`, `grid_export_wh` | lifetime grid import and feed-in | Wh |
| `bat_charge_wh`, `bat_discharge_wh` | lifetime battery energy | Wh |
| `temp_c` | outdoor temperature, `-999` = not reported | °C |

The format, its writer and its reader live together in
[`src/storage/CsvRow.h`](../src/storage/CsvRow.h). `csvrow::format()` and
`csvrow::parse()` are the two ends; a row written by one is read back by the
other, so a format change cannot silently break the chart. The header is only
written when a file is created, so a month file keeps the column set it started
with.

The row cadence matches the 24-hour chart: one sample per minute, 1440 rows per
day, roughly 100 kB per month. Only fresh poll results are logged; when the WARP
device is unreachable the log has a gap rather than repeated stale values.

## Restore on boot

After the card is mounted and the wall clock is valid, the panel asks the SD
worker to scan the current month file and, if that file holds fewer than 24
hours, the end of the previous month file. Only the newest rows are read: the
scan seeks to the tail of the file and keeps a rolling window, so a month file
of any size costs a bounded amount of work.

The restored rows replace the in-RAM history ring and the SG-Ready band, then
live samples continue in the same timeline. The chart therefore shows the last
24 hours (or as much as exists) immediately after a reboot. Restore runs once
per boot; it does not repeat when the card is reinserted later.

## Wall clock

The WARP module API reports its RTC time, which the panel copies into the system
clock on the first valid reading. Until that happens, the log is off: a row
without a usable timestamp would be worse than a gap. If the clock is still
missing when the card is mounted, the worker keeps the restore request waiting
instead of guessing.

The file name is derived from the same UTC clock as the row timestamps, so a
file and its rows always agree. Before the clock is valid the panel would write
to `/hist/UPT-<days>.csv` (uptime days), but no row is produced without a valid
clock, so this is only a fallback for a future change.

## Missing or failing card

The card is mounted in SPI mode and checked with a write/read-back self-test at
4 MHz. A card that fails the self-test is retried at 400 kHz. Mount failures are
retried every 10 seconds; a card pulled during operation is detected by the
periodic presence check.

Rows that cannot be written are parked in a RAM ring, oldest first, and retried
every 5 seconds. The ring holds 24 hours (1442 rows, about 0.5 MB) in PSRAM; on
a board without usable PSRAM it falls back to 60 rows (one hour) in internal
RAM. On overflow the oldest row is dropped and the count is shown in the status
line. A parked row keeps its own timestamp and target file, so a month rollover
during an outage still splits correctly across two files.

## Hardware

The TF slot is wired to the SPI bus and to the CH422G expander:

| Signal | Connection |
|---|---|
| SCK | GPIO 12 |
| MISO | GPIO 13 |
| MOSI | GPIO 11 |
| CS | EXIO4 on the CH422G, active low |
| SD library CS argument | dummy GPIO 15 |

The chip select is not a GPIO, so the Arduino `SD` library cannot drive it. The
panel drives EXIO4 low once through the expander and passes the dummy pin to
`SD.begin()`; the bus carries no other device, so the card stays selected. The
same approach is used by the vendor's SD example. Details are in
[hardware.md](hardware.md) and
[`src/board/waveshare_touch_lcd7/Pins.h`](../src/board/waveshare_touch_lcd7/Pins.h).

The card must be FAT32 formatted. It can be inserted or removed while the panel
runs.

## Status line

The **NETZWERK** card on the service page shows the SD state:

```
SD: OK | 7.4 GB frei
SD: OK | 12 gepuffert (12 min), 7.4 GB frei
SD: -- | 12 gepuffert (12 min)
SD: OK | 3 Zeilen verloren
```

`SD: --` means no card is mounted. `gepuffert` counts rows waiting in RAM, and
`verloren` counts rows that were dropped because the ring overflowed (or the
worker could not accept them). The English build uses `queued` and `rows lost`.

## Reading the files

The card is a normal FAT32 volume; copy the `/hist` folder to a PC and open the
CSV files in a spreadsheet. Empty trailing fields are not used - missing values
have explicit sentinels (`-1`, `-999`) - so a column always has the same
meaning. The unix timestamp can be converted with any spreadsheet formula or
`date -d @<ts>`.

The desktop emulator writes the same files into `/tmp/hems-panel-sd` by default
(or the directory passed as `--sd DIR`) and restores them on its next start, so
the log and the restore can be exercised without a card; see
[emulator.md](emulator.md#sd-card-log).

## Format test

The row format and the parked-row ring are header-only and free of Arduino, so
they can be checked on the build machine:

```sh
./tools/sd_format_test/run.sh
```

It compiles the shipped headers and checks the header column count, a full
format/parse round trip, the missing-value sentinels, strict parsing, the
SG-Ready mapping and the queue's FIFO, overflow and month-split behaviour.

