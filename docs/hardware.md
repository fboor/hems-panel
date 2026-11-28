# Hardware and bring-up

The panel runs on the **Waveshare ESP32-S3-Touch-LCD-7** with capacitive touch:
an 800 × 480 RGB panel, a GT911 touch controller, a CH422G I/O expander, 16 MB
flash and 8 MB PSRAM. The display driver also contains the init path for the
Guition 4" 480 × 480 panel, but the current PlatformIO environment targets the
7" board.

> **Not the 7B.** Waveshare sells several boards with "LCD-7" in the name. The
> 7B has a 1024 × 600 panel and uses EXIO6 for `LCD_VDD_EN`; its RGB data pins
> match this board, so a wrong profile builds and looks plausible while it draws
> an 800-line picture on a 1024-line panel. The revision belongs in the board
> profile name, not in a runtime switch.

## Pin map

The numbers below are from the vendor's pin table and the vendor demo that the
firmware compiles against.

| Signal | GPIO |
|---|---|
| LCD HSYNC / VSYNC / DE / PCLK | 46 / 3 / 5 / 7 |
| LCD B0..B4 | 14, 38, 18, 17, 10 |
| LCD G0..G5 | 39, 0, 45, 48, 47, 21 |
| LCD R0..R4 | 1, 2, 42, 41, 40 |
| Touch I²C SDA / SCL | 8 / 9 |
| Touch controller | GT911 at `0x5D` (`0x14` alternate), INT on GPIO 4 |
| CH422G expander | I²C `0x20`, 400 kHz |
| SD card SCK / MISO / MOSI | 12 / 13 / 11 |
| SD card chip select | EXIO4 on the expander (dummy GPIO 15 for the SD library) |
| Backlight | GPIO 6, PWM |
| Switched output | reserved; not enabled in the current build |

The expander carries these functions:

| Expander pin | Function |
|---|---|
| EXIO1 | touch reset |
| EXIO2 | backlight enable on the board, kept high |
| EXIO3 | LCD reset, 10 ms low then 100 ms high |
| EXIO4 | SD card chip select, active low |
| EXIO5 | USB/CAN selector: low = native USB on GPIO 19/20, high = CAN |

The panel reset is not a GPIO. It is EXIO3 and has to be pulsed before the
display object is created; without it the panel lights but draws nothing.

## Backlight

There is one image for the board. GPIO 6 drives the enable net through LEDC, so
the light dims to 30 % after three minutes without a touch and goes out after
five. The CH422G's EXIO2 is kept high at start-up (`BOARD_CH422G_START_IO 0x1E`,
the vendor's own "backlight on" value): on a board that still wires EXIO2 to the
enable net it agrees with GPIO 6 at full brightness instead of pulling against
it. A 100 Ω series resistor (R114) limits the current if the two ever disagree.

## Switched output

The output is reserved but not enabled in the current build
(`BOARD_RELAY_PRESENT 0`). There is no spare expander pin: EXIO2 carries the
board's backlight enable, and GPIO 8 and 9 are the I²C bus shared by the touch
controller and the CH422G. If a relay is added later, the coil goes to the
isolated side of an optocoupler module, so its current never passes through the
board.

## Bring-up checklist

The emulator and the host checks cover the logic. These items need the panel on
the bench, in this order:

1. **Backlight** — the panel lights up. GPIO 6 dims it to 30 % after three
   minutes without a touch and switches it off after five; a touch brings it
   back and restarts both timers.
2. **Display** — a non-garbage image means the RGB timings are right. A shifted
   or colour-swapped picture points at the porches or the R/B order in
   `src/display/Display.cpp`.
3. **Touch** — tap a button on the lit panel. If nothing registers, check the
   GT911 reset on EXIO1 and the I²C bus.
4. **WLAN and setup** — join the setup access point, open the portal, enter the
   WLAN and the WARP address, and confirm that the panel leaves the setup screen.
5. **WARP link** — watch the serial log while the panel polls the module API.
   `--probe` against the same address shows the mapped values without the panel.
6. **Native USB log** — confirm that the application log appears on the USB-C
   port after boot, not only the ROM output on the UART port.

## Board notes

- The RGB panel is driven from PSRAM at 16 MHz. A line that arrives late shows
  as a jumping picture; the colours stay right, which is how a late line is told
  from a panel that is out of sync.
- The SD card's chip select is on the expander and the Arduino SD class wants a
  GPIO. The panel therefore drives EXIO4 low once through the CH422G and passes
  the dummy GPIO 15 to `SD.begin()`; the bus carries no other device, so the card
  stays selected. The card is used for the history log, see
  [sd-log.md](sd-log.md).
- The board profile, the display timings and the expander bring-up carry their
  measurements and open questions in the source headers. Read those before
  changing a pin.
