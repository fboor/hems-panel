// 480x480 ST7701 display: config via 3-wire SPI, pixels via the ESP32-S3
// LCD_CAM RGB interface (esp_lcd), rendered by LVGL.
#ifndef DISPLAY_H
#define DISPLAY_H

#include <lvgl.h>

// Power up the panel and create the LVGL display. Must be called once after
// the WiFi config portal (so the panel can show connection status) and before
// any LVGL object creation.
bool displayInit();

// LVGL heartbeat; call from the main loop.
void displayLooper();

// HSYNC and VSYNC idle level, changed while the panel runs, and the two levels
// that are set right now (1 = idle low, 0 = idle high).
//
// Why this is settable at runtime: the idle level decides where the panel starts
// latching a pixel's bits, and MEASURED on the 7-inch panel it also decides the
// colour - the same build shows a blue cast with idle high and a green one with
// idle low, and the panel's own demo leaves both at 0. Four builds for four
// combinations is four flashes; two register bits are four requests.
//
// Only valid after displayInit(), and only on a board whose panel hangs on the
// LCD_CAM RGB peripheral (that is both of ours). Returns false when there is no
// panel to ask, and leaves the current setting alone.
bool displaySetSyncPolarity(int hsIdleLow, int vsIdleLow);
bool displaySyncPolarity(int *hsIdleLow, int *vsIdleLow);

// Frames the panel has shown since boot, and the rate over the last call. The
// timings say what that rate must be - the scan reads the frame buffer from PSRAM
// once per frame, and PSRAM is also where the network stack and LVGL keep their
// buffers. A rate below the theoretical one means the scan did not get its data,
// which looks on glass like flicker and has nothing to do with what is drawn.
uint32_t displayFrameCount();
float displayFrameRate();

// What the panel spent its time on since the last call, all times in
// microseconds. Split three ways because they are three different costs and a
// change usually shortens one of them: the wait is the flush callback holding
// back until the scan is off the strip, the draw is the driver's copy into the
// frame buffer plus its cache write-back, and the refresh is the whole LVGL
// pass, which contains both. The driver copies the area and then writes back a
// range covering the whole frame buffer, so a draw costs the frame buffer, not
// the area - against a frame that is 26 ms long on the 7-inch timings.
struct DisplayStats {
  uint32_t flushes;        // strips drawn
  uint32_t refreshes;      // LVGL refresh passes
  uint32_t waitTotalUs;    // time spent waiting for the scan
  uint32_t waitMaxUs;
  uint32_t drawTotalUs;    // time inside draw_bitmap()
  uint32_t drawMaxUs;
  uint32_t refreshTotalUs; // whole LVGL pass, flushes included
  uint32_t refreshMaxUs;
  // Draw time distribution: below 0.5 ms, below 2 ms, below 5 ms, 5 ms or more.
  // The mean says little when a page switch and a moving dot are both "a draw".
  uint32_t drawBucket[4];
};
void displayStats(DisplayStats *out);

// Handle used internally; keeps the compiler honest in gui/.
lv_display_t *dispGetHandle();

// The panel's per-channel colour correction, as applied by the flush callback.
// A screenshot renders LVGL's own buffer, i.e. *before* this correction, so
// running the values through the same function is what makes the file show what
// the panel actually shows rather than what LVGL asked for.
uint16_t dispCorrectPixel(uint16_t rgb565);

#endif // DISPLAY_H