// SPDX-License-Identifier: MIT
#include "HemsUi.h"

#include <algorithm>
#include "fonts/lv_font_mdi_icons_28.h"
#include "fonts/lv_font_hems.h"

// UI language, fixed at compile time. The panel ships in German by default;
// define WARP_LANG_EN=1 (see platformio.ini, esp32-s3-touch-lcd-7-en) for
// the English build. Both languages use the same fonts (umlauts are
// subsetted glyphs, plain ASCII in English).
#ifdef WARP_LANG_EN
#define TR(de, en) (en)
#else
#define TR(de, en) (de)
#endif

#ifdef WARP_LANG_EN
constexpr bool kEnglishUi = true;
#else
constexpr bool kEnglishUi = false;
#endif

LV_IMG_DECLARE(chart_bg);

#include <lvgl.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "Mem.h"

namespace hemsui {
namespace {

// Current palette and the pieces of UI state that survive a theme switch.
ThemeId g_themeId = ThemeId::Dark;
ThemeChangeHandler g_themeChangeHandler = nullptr;
bool g_showBackgroundImage = true;
bool g_imageInitialized = false;
lv_obj_t *g_root = nullptr;
lv_timer_t *g_flowTimer = nullptr;
int g_activePage = 1;
ConnectionState g_connectionState = ConnectionState::NoNet;
WarpData g_lastData{};

const Theme &palette() { return theme(g_themeId); }

// MDI glyphs from the bundled 28 px icon font. Written as UTF-8 bytes, not
// \uXXXX, so the compiler keeps the whole code point.
constexpr const char kFlowGridIcon[] = "\xF3\xB0\xB4\xBE";  // transmission-tower
constexpr const char kFlowCarIcon[] = "\xF3\xB0\x84\x8B";   // car
// Navigation icons, same MDI font. UTF-8 bytes, not \uXXXX, see above.
constexpr const char kNavFlowIcon[] = "\xF3\xB0\x8B\x9C";    // home
constexpr const char kNavEnergyIcon[] = "\xF3\xB1\x90\x8B";  // lightning-bolt
constexpr const char kNavChartIcon[] = "\xF3\xB0\x84\xAA";   // chart-line
constexpr const char kNavServiceIcon[] = "\xF3\xB0\x96\xB7"; // wrench
constexpr const char kNavHeatIcon[] = "\xF3\xB0\x94\x8F";    // thermometer

constexpr const lv_font_t *F_SMALL = &lv_font_hems_14;
constexpr const lv_font_t *F_LEGEND = &lv_font_hems_11;  // chart legend labels
constexpr const lv_font_t *F_BODY = &lv_font_hems_16;
constexpr const lv_font_t *F_TITLE = &lv_font_hems_20;
constexpr const lv_font_t *F_BIG = &lv_font_hems_36;
constexpr const lv_font_t *F_MED = &lv_font_hems_20;
constexpr const lv_font_t *F_ICON = &lv_font_hems_48;

// The header lost 3 px so the energy and chart pages gain room above their
// cards. The small caption line moves up with it; the photo-sized panel keeps
// its 800x356 geometry. The energy cards also lost 6 px of height.
constexpr int kHeaderH = 65;
constexpr int kCaptionY = 37;
constexpr int kCardSave = 6;

// Tighter card stacks on the energy, heating and service pages. The smaller
// gaps give back room for almost equal margins above and below the cards.
constexpr int kEnergyTop = 8;
constexpr int kEnergyGap = 9;
constexpr int kHeatTop = 15;
constexpr int kHeatGap = 8;
constexpr int kServiceTop = 8;
constexpr int kServiceGap = 6;

// The chart keeps its shorter height but starts 6 px lower, so the space it
// gave back is above the plot instead of below it.
constexpr int kChartDrop = 6;

// Power history. One sample per minute is enough for a 24 h graph and keeps
// the raw ring buffer small (1440 entries). The chart itself always uses the
// same number of points; the samples are resampled into the visible window.
constexpr int kChartPoints = 240;
constexpr int kChartLineWidth = 2;
// The chart card's size, the same as the widget it replaces: a rounded card
// with a 1 px border, so the plot area is 764 x 232 at (1, 1).
constexpr int kChartW = 766;
constexpr int kChartH = 234;
constexpr int kChartBorder = 1;
constexpr uint32_t kSampleIntervalMs = 60u * 1000u;
// One missed minute plus the usual poll jitter is a gap, not a slope. Points
// between samples that far apart are left blank instead of being invented.
constexpr uint32_t kChartGapMs = kSampleIntervalMs + kSampleIntervalMs / 2u;
constexpr uint32_t kHistoryMs = 24u * 60u * 60u * 1000u;
constexpr int kMaxSamples = 24 * 60 + 2;

// The history clock is deliberately not lv_tick_get() alone. Restored rows are
// up to 24 h old, and their timestamps are derived by subtracting that age from
// "now" - with a bare tick count the oldest rows would underflow uint32_t and
// land in the future. A base of two history windows leaves enough room for the
// full 24 h and keeps every live sample in the same timeline.
uint32_t histNowMs() { return 2u * kHistoryMs + lv_tick_get(); }

// The wallbox configuration page shows one mode button per supported mode.
// WARP currently offers at most four (Off + two configurable + Fast); the
// extra slots keep the renderer safe if a future firmware adds more.
constexpr int kModeSlots = 6;
constexpr uint32_t kPendingModeMs = 15000;

struct PowerSample {
  uint32_t timeMs;
  float grid;
  float pv;
  float house;
  float battery;
  float wb1;
  float wb2;
  float soc;  // battery state of charge in %, -1 = no battery
};

// The history and chart buffers are allocated once in create(), in PSRAM
// where possible (see Mem.h). They are too large to keep in internal DRAM
// next to the LVGL draw buffer and the network stack.
PowerSample *g_samples = nullptr;
int g_sampleHead = 0;
int g_sampleCount = 0;

// SG-Ready state history for the timeline band: one entry per minute over
// 24 h, combined into one state: -1 = no data, 0 = none active,
// 1 = SG1 (blocking) active, 2 = SG2 (extended) active, 3 = both active
// (should not happen - orange marks a wrong device state).
struct SgSample {
  uint32_t timeMs;
  int8_t state;
};

SgSample *g_sgSamples = nullptr;
int g_sgHead = 0;
int g_sgCount = 0;
bool g_haveSample = false;
uint32_t g_lastSampleMs = 0;
uint32_t g_lastDataUpdateMs = 0;

// The chart draws these arrays directly (external buffers), so rebuilding a
// frame only means overwriting the values and refreshing the chart once. All
// seven series share one allocation; the named pointers below index into it.
int32_t *g_chartData = nullptr;
int32_t *g_chartGrid = nullptr;
int32_t *g_chartPv = nullptr;
int32_t *g_chartHouse = nullptr;
int32_t *g_chartBattery = nullptr;
int32_t *g_chartWb1 = nullptr;
int32_t *g_chartWb2 = nullptr;
int32_t *g_chartSoc = nullptr;  // raw SOC %, mapped into the power range

// The chart is drawn once per new sample into this RGB565 buffer and then
// shown as a plain image. LVGL's chart widget turns every line segment into
// its own draw task - each one a malloc plus a walk to the list's tail - so a
// full redraw of the chart page, which is what every page switch triggers,
// costs more than the rest of the page together. As an image the same switch
// is one opaque blit. 766 x 234 x 2 B = 350 kB, allocated in PSRAM; if that
// fails the page keeps the original chart widgets.
uint16_t *g_chartPixels = nullptr;
int g_chartRange = 5000;  // last y range, kept for re-renders

// Scratch copy of the SG ring for the timeline band, filled on every redraw
// of the heating page.
uint32_t *g_sgBandTimes = nullptr;
int8_t *g_sgBandState = nullptr;

// The flow page's background photograph is stored as an I8 image with a
// 256-entry palette. LVGL decodes an I8 image per row and per redraw: it looks
// up every pixel and expands the row to ARGB8888 before blending it into the
// RGB565 layer (LV_BIN_DECODER_RAM_LOAD is off). Converting it once at startup
// into an RGB565 copy in PSRAM turns each redraw into a straight 2-byte blit.
// 800 x 356 x 2 B = 570 kB; if that allocation fails the original image is
// used unchanged.
lv_image_dsc_t g_chartBg565{};
bool g_chartBg565Ready = false;

void prepareChartBackground() {
  const lv_image_header_t &hdr = chart_bg.header;
  if (hdr.cf != LV_COLOR_FORMAT_I8 || hdr.w == 0 || hdr.h == 0) {
    return;
  }
  const size_t px = (size_t)hdr.w * (size_t)hdr.h;
  uint16_t *dst = (uint16_t *)allocLarge(px * sizeof(uint16_t));
  if (dst == nullptr) {
    return;
  }
  const lv_color32_t *entries = (const lv_color32_t *)chart_bg.data;
  const uint8_t *indices = chart_bg.data + sizeof(lv_color32_t) * 256;
  for (size_t i = 0; i < px; ++i) {
    const lv_color32_t &c = entries[indices[i]];
    // Same rounding as LVGL's lv_color_to_u16, which the ARGB8888-to-RGB565
    // blend uses for fully opaque pixels.
    dst[i] = (uint16_t)(((c.red & 0xF8) << 8) + ((c.green & 0xFC) << 3) +
                        ((c.blue & 0xF8) >> 3));
  }
  g_chartBg565.header = hdr;
  g_chartBg565.header.cf = LV_COLOR_FORMAT_RGB565;
  g_chartBg565.header.stride = (uint16_t)(hdr.w * sizeof(uint16_t));
  g_chartBg565.data_size = (uint32_t)(px * sizeof(uint16_t));
  g_chartBg565.data = (const uint8_t *)dst;
  g_chartBg565.reserved = nullptr;
  g_chartBg565Ready = true;
}

// Backdrop drawn wherever no photograph is shown. It has the photo's exact
// 800x356 geometry, so the same object can sit behind every page. The drawing
// is a quiet line schematic - sun, PV, house, car - tinted from the theme.
constexpr int kBackdropW = 800;
constexpr int kBackdropH = 356;

void backdropPixel(uint16_t *dst, int x, int y, uint16_t c) {
  if (x < 0 || y < 0 || x >= kBackdropW || y >= kBackdropH) {
    return;
  }
  dst[y * kBackdropW + x] = c;
}

void backdropRect(uint16_t *dst, int x0, int y0, int x1, int y1, uint16_t c,
                  bool fill) {
  if (x0 > x1) {
    const int t = x0;
    x0 = x1;
    x1 = t;
  }
  if (y0 > y1) {
    const int t = y0;
    y0 = y1;
    y1 = t;
  }
  if (fill) {
    for (int y = y0; y <= y1; ++y) {
      for (int x = x0; x <= x1; ++x) {
        backdropPixel(dst, x, y, c);
      }
    }
    return;
  }
  for (int x = x0; x <= x1; ++x) {
    backdropPixel(dst, x, y0, c);
    backdropPixel(dst, x, y1, c);
  }
  for (int y = y0; y <= y1; ++y) {
    backdropPixel(dst, x0, y, c);
    backdropPixel(dst, x1, y, c);
  }
}

// Integer Bresenham with a small square brush. The brush keeps thin diagonal
// lines connected without antialiasing or per-pixel blending on the panel.
void backdropLine(uint16_t *dst, int x0, int y0, int x1, int y1, uint16_t c,
                  int thickness = 2) {
  int dx = x1 - x0;
  if (dx < 0) dx = -dx;
  int dy = y1 - y0;
  if (dy < 0) dy = -dy;
  const int sx = x0 < x1 ? 1 : -1;
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx - dy;
  for (;;) {
    for (int oy = -thickness / 2; oy <= (thickness - 1) / 2; ++oy) {
      for (int ox = -thickness / 2; ox <= (thickness - 1) / 2; ++ox) {
        backdropPixel(dst, x0 + ox, y0 + oy, c);
      }
    }
    if (x0 == x1 && y0 == y1) {
      break;
    }
    const int e2 = 2 * err;
    if (e2 > -dy) {
      err -= dy;
      x0 += sx;
    }
    if (e2 < dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void backdropPath(uint16_t *dst, const int pts[][2], int count, uint16_t c,
                  int thickness = 2) {
  for (int i = 1; i < count; ++i) {
    backdropLine(dst, pts[i - 1][0], pts[i - 1][1], pts[i][0], pts[i][1], c,
                 thickness);
  }
}

void backdropCircle(uint16_t *dst, int cx, int cy, int radius, uint16_t c,
                    bool fill) {
  const int outer = radius * radius;
  const int r = radius > 2 ? radius - 2 : 0;
  const int inner = r * r;
  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      const int d = dx * dx + dy * dy;
      if (fill ? d <= outer : (d >= inner && d <= outer)) {
        backdropPixel(dst, cx + dx, cy + dy, c);
      }
    }
  }
}

lv_image_dsc_t g_backdrop565{};
bool g_backdropReady = false;

void prepareBackdrop() {
  if (!g_backdropReady) {
    uint16_t *dst =
        (uint16_t *)allocLarge((size_t)kBackdropW * kBackdropH * sizeof(uint16_t));
    if (dst == nullptr) {
      return;
    }
    g_backdrop565.header.cf = LV_COLOR_FORMAT_RGB565;
    g_backdrop565.header.w = kBackdropW;
    g_backdrop565.header.h = kBackdropH;
    g_backdrop565.header.stride = (uint16_t)(kBackdropW * sizeof(uint16_t));
    g_backdrop565.data_size =
        (uint32_t)(kBackdropW * kBackdropH * sizeof(uint16_t));
    g_backdrop565.data = (const uint8_t *)dst;
    g_backdropReady = true;
  }

  uint16_t *dst = (uint16_t *)g_backdrop565.data;
  const Theme &t = palette();
  const uint16_t bg = lv_color_to_u16(lv_color_hex(t.backdrop));
  const uint16_t fill = lv_color_to_u16(lv_color_hex(t.backdropFill));
  const uint16_t line = lv_color_to_u16(lv_color_hex(t.backdropLine));
  const uint16_t accent = lv_color_to_u16(lv_color_hex(t.backdropAccent));
  const uint16_t solar = lv_color_to_u16(lv_color_hex(t.solar));
  const uint16_t amber = lv_color_to_u16(lv_color_hex(t.amber));
  const uint16_t ev = lv_color_to_u16(lv_color_hex(t.ev));

  for (int i = 0; i < kBackdropW * kBackdropH; ++i) {
    dst[i] = bg;
  }

  // Two quiet links tie the symbols together without turning the picture into
  // a grid. They sit behind the glyphs.
  const int sunLink[2][2] = {{282, 62}, {350, 72}};
  const int carLink[2][2] = {{448, 88}, {484, 304}};
  backdropPath(dst, sunLink, 2, line, 2);
  backdropPath(dst, carLink, 2, line, 2);

  // Sun, top-left of the clear area above the flow cards.
  backdropCircle(dst, 258, 60, 16, amber, false);
  const int rays[8][4] = {
      {258, 34, 258, 26}, {258, 86, 258, 94}, {232, 60, 224, 60},
      {284, 60, 292, 60}, {240, 42, 234, 36}, {276, 42, 282, 36},
      {240, 78, 234, 84}, {276, 78, 282, 84},
  };
  for (int i = 0; i < 8; ++i) {
    backdropLine(dst, rays[i][0], rays[i][1], rays[i][2], rays[i][3], amber, 2);
  }

  // House in the top-centre gap; the hub card starts below it.
  backdropRect(dst, 360, 56, 444, 106, fill, true);
  backdropRect(dst, 360, 56, 444, 106, line, false);
  const int roof[3][2] = {{346, 58}, {402, 22}, {458, 58}};
  backdropPath(dst, roof, 3, accent, 3);
  backdropRect(dst, 374, 68, 392, 86, line, false);
  backdropRect(dst, 412, 68, 430, 86, line, false);
  backdropRect(dst, 392, 86, 412, 106, accent, true);

  // PV array and car in the clear strip below the hub and side cards.
  backdropRect(dst, 226, 292, 326, 340, fill, true);
  backdropRect(dst, 226, 292, 326, 340, solar, false);
  backdropLine(dst, 259, 292, 259, 340, solar, 2);
  backdropLine(dst, 293, 292, 293, 340, solar, 2);
  backdropLine(dst, 226, 316, 326, 316, solar, 2);
  backdropLine(dst, 242, 340, 234, 350, solar, 2);
  backdropLine(dst, 310, 340, 318, 350, solar, 2);

  backdropRect(dst, 478, 306, 594, 332, fill, true);
  backdropRect(dst, 478, 306, 594, 332, ev, false);
  const int carRoof[4][2] = {{500, 306}, {514, 286}, {558, 286}, {572, 306}};
  backdropPath(dst, carRoof, 4, ev, 2);
  backdropLine(dst, 536, 287, 536, 305, ev, 2);
  backdropCircle(dst, 506, 336, 9, ev, false);
  backdropCircle(dst, 566, 336, 9, ev, false);
  backdropCircle(dst, 506, 336, 3, line, true);
  backdropCircle(dst, 566, 336, 3, line, true);
}

// Tapped chart position: the tapped sample is shown in the legend as
// "Label [value]" and the legend resets after 30 s.
bool g_tapActive = false;
uint32_t g_tapMs = 0;
PowerSample g_tapSample{};
char g_wbBase[2][16] = {};

struct HistoryBand;

struct Widgets {
  lv_obj_t *statusChip = nullptr;
  lv_obj_t *statusWifi = nullptr;
  lv_obj_t *statusText = nullptr;
  lv_obj_t *autarkyValue = nullptr;
  lv_obj_t *forecastValue = nullptr;
  lv_obj_t *updatedText = nullptr;
  lv_obj_t *pageBtn1 = nullptr;
  lv_obj_t *pageBtn2 = nullptr;
  lv_obj_t *pageBtn3 = nullptr;
  lv_obj_t *pageBtn4 = nullptr;
  lv_obj_t *pageBtn5 = nullptr;
  lv_obj_t *pageBtn6 = nullptr;
  lv_obj_t *caption1 = nullptr;
  lv_obj_t *caption2 = nullptr;
  lv_obj_t *caption3 = nullptr;
  lv_obj_t *caption4 = nullptr;
  lv_obj_t *caption5 = nullptr;
  lv_obj_t *caption6 = nullptr;
  lv_obj_t *backdrop = nullptr;
  lv_obj_t *page1 = nullptr;
  lv_obj_t *page2 = nullptr;
  lv_obj_t *page3 = nullptr;
  lv_obj_t *page4 = nullptr;
  lv_obj_t *page5 = nullptr;
  lv_obj_t *page6 = nullptr;
  lv_obj_t *bgBtn = nullptr;
  lv_obj_t *heatBlocking = nullptr;
  lv_obj_t *heatExtended = nullptr;
  lv_obj_t *heatStatus = nullptr;
  lv_obj_t *heatP14 = nullptr;
  lv_obj_t *heatOverride = nullptr;
  lv_obj_t *heatPartyBtn = nullptr;
  lv_obj_t *tempNow = nullptr;
  lv_obj_t *tempToday = nullptr;
  lv_obj_t *tempTomorrow = nullptr;

  // Energy flow page ----------------------------------------------------
  lv_obj_t *pvNode = nullptr;
  lv_obj_t *pvValue = nullptr;
  lv_obj_t *pvState = nullptr;
  lv_obj_t *gridNode = nullptr;
  lv_obj_t *gridValue = nullptr;
  lv_obj_t *gridState = nullptr;
  lv_obj_t *batNode = nullptr;
  lv_obj_t *batValue = nullptr;
  lv_obj_t *batState = nullptr;
  lv_obj_t *wbNode = nullptr;
  lv_obj_t *wbValue = nullptr;
  lv_obj_t *wbState = nullptr;
  lv_obj_t *hubNode = nullptr;
  lv_obj_t *hubValue = nullptr;

  // Four links: PV, grid, battery, wallbox. `flowTrack` is the dim base,
  // `flowLine` the coloured active line, `flowDot` the moving particles.
  lv_obj_t *flowTrack[4] = {};
  lv_obj_t *flowLine[4] = {};
  lv_obj_t *flowDot[4][2] = {};
  lv_point_precise_t flowPts[4][2] = {};
  float flowPower[4] = {};
  bool flowActive[4] = {};
  int flowPhase[4] = {};
  int flowWidth[4] = {};  // last line width written to each link
  bool gridExporting = false;
  bool batCharging = false;
  lv_obj_t *powerChart = nullptr;
  lv_obj_t *socChart = nullptr;
  lv_obj_t *chartImage = nullptr;
  lv_obj_t *tapLine = nullptr;
  lv_chart_series_t *powerSeries = nullptr;
  lv_chart_series_t *seriesSoc = nullptr;
  lv_chart_series_t *seriesGrid = nullptr;
  lv_chart_series_t *seriesPv = nullptr;
  lv_chart_series_t *seriesHouse = nullptr;
  lv_chart_series_t *seriesBat = nullptr;
  lv_chart_series_t *seriesWb1 = nullptr;
  lv_chart_series_t *seriesWb2 = nullptr;
  lv_obj_t *ioDot[8] = {};
  HistoryBand *sgBand = nullptr;
  HistoryBand *priceBand = nullptr;
  lv_obj_t *priceCard = nullptr;
  lv_obj_t *priceStats = nullptr;
  lv_obj_t *wbLegendDot[2] = {};
  lv_obj_t *wbLegendLabel[2] = {};
  lv_obj_t *legendLabel[7] = {};
  lv_obj_t *scaleMax = nullptr;
  lv_obj_t *scaleMid = nullptr;
  lv_obj_t *scaleMin = nullptr;
  lv_obj_t *timeLabel[5] = {};

  lv_obj_t *gridTotal = nullptr;
  lv_obj_t *gridDir = nullptr;
  lv_obj_t *gridPhase[3] = {};

  lv_obj_t *houseTotal = nullptr;
  lv_obj_t *housePhase[3] = {};

  lv_obj_t *pvTotal = nullptr;
  lv_obj_t *pvA = nullptr;
  lv_obj_t *pvB = nullptr;

  lv_obj_t *extValue = nullptr;
  lv_obj_t *extNote = nullptr;
  lv_obj_t *wallboxValue = nullptr;
  lv_obj_t *wallboxNote = nullptr;
  lv_obj_t *wallboxMode = nullptr;
  lv_obj_t *wallboxPhase = nullptr;

  lv_obj_t *batSoc = nullptr;
  lv_obj_t *batBar = nullptr;
  lv_obj_t *batPower = nullptr;
  lv_obj_t *batVoltage = nullptr;
  lv_obj_t *batCurrent = nullptr;
  lv_obj_t *batDir = nullptr;

  lv_obj_t *energyPv = nullptr;
  lv_obj_t *zPv = nullptr;
  lv_obj_t *zHouse = nullptr;
  lv_obj_t *zImport = nullptr;
  lv_obj_t *zExport = nullptr;
  lv_obj_t *zCharger = nullptr;
  lv_obj_t *zBattery = nullptr;
  lv_obj_t *energyHouse = nullptr;
  lv_obj_t *energyImport = nullptr;
  lv_obj_t *energyExport = nullptr;
  lv_obj_t *energyCharger = nullptr;
  lv_obj_t *energyBattery = nullptr;

  // Service page network card.
  lv_obj_t *netSsid = nullptr;
  lv_obj_t *netIp = nullptr;
  lv_obj_t *netWarp = nullptr;
  lv_obj_t *netWeb = nullptr;
  lv_obj_t *netSd = nullptr;

  // First-boot Wi-Fi setup overlay.
  lv_obj_t *setupOverlay = nullptr;
  lv_obj_t *setupQr = nullptr;
  lv_obj_t *setupSsid = nullptr;
  lv_obj_t *setupPassword = nullptr;
  lv_obj_t *setupUrl = nullptr;
};

Widgets w;

// The theme picks the photograph or the schematic for the flow page; the
// image switch can hide it. Other pages only show the plain 800x356 panel
// below, never the schematic.
void applyFlowBackground() {
  if (w.page4 == nullptr) {
    return;
  }
  const void *src = nullptr;
  if (g_showBackgroundImage) {
    if (palette().photoBackground) {
      src = g_chartBg565Ready ? (const void *)&g_chartBg565
                              : (const void *)&chart_bg;
    } else if (g_backdropReady) {
      src = (const void *)&g_backdrop565;
    }
  }
  lv_obj_set_style_bg_image_src(w.page4, src, 0);
  lv_obj_set_style_bg_image_opa(w.page4,
                                src != nullptr ? LV_OPA_COVER : LV_OPA_TRANSP,
                                0);
}

// One wallbox card on the configuration page. The buttons and their modes are
// filled when the card is built; the labels are refreshed on every poll.
struct ModeCard {
  lv_obj_t *card = nullptr;
  lv_obj_t *name = nullptr;
  lv_obj_t *state = nullptr;
  lv_obj_t *power = nullptr;
  lv_obj_t *buttons[kModeSlots] = {};
  int modes[kModeSlots] = {};
  int modeCount = 0;
};

ModeCard g_globalCard;
ModeCard g_chargerCards[kWarpMaxChargers];

// Rebuild the cards only when the number of chargers or the offered modes
// changes; the labels themselves are updated in place every poll.
int g_renderedChargers = -1;
int g_renderedModeCount = -1;
int g_renderedModes[kModeSlots] = {};

// A tap is shown immediately, before the next poll confirms it. The override
// expires after kPendingModeMs so a rejected command does not stick forever.
struct PendingMode {
  int mode = 0;
  uint32_t untilMs = 0;
  bool active = false;
};
PendingMode g_pendingGlobal;
PendingMode g_pendingCharger[kWarpMaxChargers];

hemsui::ChargeModeHandler g_chargeModeHandler = nullptr;
hemsui::HeatingOverrideHandler g_heatingOverrideHandler = nullptr;
bool g_lastHeatingOverride = false;
bool g_partyPending = false;
bool g_partyPendingOn = true;
uint32_t g_partyPendingUntil = 0;

// Text that sits directly on the page background follows the background
// colour. Labels inside cards keep their own palette, so they are deliberately
// not registered here. The base colour is remembered so a status change (for
// example red data instead of green) can be mapped back to a contrasting shade.
struct BackgroundLabel {
  lv_obj_t *obj;
  uint32_t baseColor;
};

constexpr int kMaxBackgroundLabels = 64;
BackgroundLabel g_backgroundLabels[kMaxBackgroundLabels];
int g_backgroundLabelCount = 0;

bool liesOnBackground(lv_obj_t *obj) {
  lv_obj_t *screen = lv_obj_get_screen(obj);
  for (lv_obj_t *p = lv_obj_get_parent(obj);
       p != nullptr && p != screen; p = lv_obj_get_parent(p)) {
    if (lv_obj_get_style_bg_opa(p, LV_PART_MAIN) > LV_OPA_TRANSP) {
      return false;
    }
  }
  return true;
}

BackgroundLabel *backgroundLabelFor(lv_obj_t *obj) {
  for (int i = 0; i < g_backgroundLabelCount; ++i) {
    if (g_backgroundLabels[i].obj == obj) {
      return &g_backgroundLabels[i];
    }
  }
  return nullptr;
}

void registerBackgroundLabel(lv_obj_t *obj, uint32_t baseColor) {
  if (g_backgroundLabelCount >= kMaxBackgroundLabels ||
      backgroundLabelFor(obj) != nullptr) {
    return;
  }
  g_backgroundLabels[g_backgroundLabelCount++] = {obj, baseColor};
}

// Muted text must stay visibly grey instead of turning plain black, and dark
// text on a light surface needs more contrast than the card palette offers.
uint32_t backgroundTextColor(uint32_t baseColor) {
  if (baseColor == palette().muted) {
    return palette().mutedOnBackground;
  }
  if (baseColor == palette().text || baseColor == palette().green ||
      baseColor == palette().red) {
    return palette().onBackground;
  }
  return baseColor;
}

lv_obj_t *card(lv_obj_t *parent, int x, int y, int width, int height) {
  lv_obj_t *c = lv_obj_create(parent);
  lv_obj_remove_style_all(c);
  lv_obj_set_pos(c, x, y);
  lv_obj_set_size(c, width, height);
  lv_obj_set_style_bg_color(c, lv_color_hex(palette().card), 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(c, 10, 0);
  lv_obj_set_style_border_width(c, 1, 0);
  lv_obj_set_style_border_color(c, lv_color_hex(palette().line), 0);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

lv_obj_t *label(lv_obj_t *parent, int x, int y, int width,
                const lv_font_t *font, uint32_t color, const char *text,
                lv_text_align_t align = LV_TEXT_ALIGN_LEFT,
                bool followBackground = true) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_pos(l, x, y);
  if (width > 0) {
    lv_obj_set_width(l, width);
    lv_obj_set_style_text_align(l, align, 0);
  }
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  if (followBackground && liesOnBackground(l)) {
    registerBackgroundLabel(l, color);
    lv_obj_set_style_text_color(
        l, lv_color_hex(backgroundTextColor(color)), 0);
  }
  return l;
}
// Timeline band: a 5 px high strip over 24 h with discrete colour-coded
// states (no data / inactive / active / extra / negative). The caption sits
// left of the band, the band itself reaches almost to the right edge. One
// segment object per run of equal states, reused across updates.
constexpr int kBandMaxSegments = 32;
constexpr int kBandX0 = 90;
constexpr int kBandX1 = 720;
constexpr int kBandH = 5;

struct HistoryBand {
  lv_obj_t *label = nullptr;
  lv_obj_t *tick[5] = {};
  int y = 0;
  int x0 = kBandX0;
  lv_obj_t *seg[kBandMaxSegments] = {};
  uint32_t colors[6] = {};  // index: state + 1 (-1 no data, 0..4)
  int lastCount = -1;
  int lastState[kBandMaxSegments] = {};
  int lastX0[kBandMaxSegments] = {};
  int lastX1[kBandMaxSegments] = {};
};

HistoryBand *createHistoryBand(lv_obj_t *parent, int y, const char *caption,
                               uint32_t noData, uint32_t inactive,
                               uint32_t active, uint32_t extra,
                               uint32_t both, int x0 = kBandX0,
                               const char *const *tickLabels = nullptr,
                               uint32_t negative = 0) {
  HistoryBand *band = new HistoryBand();
  band->y = y;
  band->x0 = x0;
  band->colors[0] = noData;
  band->colors[1] = inactive;
  band->colors[2] = active;
  band->colors[3] = extra;
  band->colors[4] = both;
  band->colors[5] = negative;
  band->label = label(parent, 16, y - 8, x0 - 26, F_BODY, palette().muted, caption);
  if (tickLabels != nullptr) {
    for (int k = 0; k < 5; ++k) {
      const int tx = x0 + (kBandX1 - x0) * k / 4;
      // Auto-width label centred on the tick: a fixed box clips the text.
      lv_obj_t *t = label(parent, 0, y + kBandH + 2, 0, F_SMALL, palette().muted,
                          tickLabels[k]);
      int lx = tx - (int)lv_obj_get_width(t) / 2;
      if (lx < 2) lx = 2;
      lv_obj_set_pos(t, lx, y + kBandH + 2);
      band->tick[k] = t;
    }
  }
  for (int i = 0; i < kBandMaxSegments; ++i) {
    lv_obj_t *segObj = lv_obj_create(parent);
    lv_obj_remove_style_all(segObj);
    lv_obj_set_size(segObj, 0, kBandH);
    lv_obj_set_style_bg_opa(segObj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(segObj, 0, 0);
    lv_obj_add_flag(segObj, LV_OBJ_FLAG_HIDDEN);
    band->seg[i] = segObj;
  }
  return band;
}

// times/states: oldest first, in one time unit (milliseconds for the SG
// history, minutes for the price forecast). The band shows the window
// [windowStart, windowStart + windowSpan); entries outside are clipped.
// Times before the first entry inside the window become an explicit
// no-data segment.
void updateHistoryBand(HistoryBand *band, const uint32_t *times,
                       const int8_t *states, int count,
                       int64_t windowStart, int64_t windowSpan) {
  if (band == nullptr || count <= 0) return;
  const int64_t windowEnd = windowStart + windowSpan;
  auto toX = [&windowStart, &windowSpan, &band](int64_t t) {
    return band->x0 + (int)((t - windowStart) * (int64_t)(kBandX1 - band->x0) / windowSpan);
  };
  int segCount = 0;
  int segState[kBandMaxSegments];
  int segX0[kBandMaxSegments];
  int segX1[kBandMaxSegments];
  // Leading no-data segment up to the first entry inside the window.
  if ((int64_t)times[0] > windowStart && segCount < kBandMaxSegments) {
    segState[segCount] = -1;
    segX0[segCount] = toX(windowStart);
    segX1[segCount] = toX((int64_t)times[0]);
    ++segCount;
  }
  // Entries before the window are skipped; the run loop below still
  // starts at the first entry, clipped to the window.
  int runState = states[0];
  int64_t runStart = (int64_t)times[0];
  for (int i = 1; i <= count; ++i) {
    const bool end = (i == count) || (states[i] != runState);
    if (end) {
      if (segCount < kBandMaxSegments) {
        const int64_t t0 = runStart > windowStart ? runStart : windowStart;
        const int64_t t1 = (i < count) ? (int64_t)times[i] : windowEnd;
        segState[segCount] = runState;
        segX0[segCount] = toX(t0);
        segX1[segCount] = toX(t1);
        ++segCount;
      }
      if (i < count) {
        runState = states[i];
        runStart = (int64_t)times[i];
      }
    }
  }
  bool changed = (segCount != band->lastCount);
  if (!changed) {
    for (int i = 0; i < segCount; ++i) {
      if (segState[i] != band->lastState[i] || segX0[i] != band->lastX0[i] ||
          segX1[i] != band->lastX1[i]) {
        changed = true;
        break;
      }
    }
  }
  if (!changed) return;
#ifdef HEMS_UI_DEBUG_BAND
  // Geometry dump for tuning the price band. Enable with
  // -DHEMS_UI_DEBUG_BAND=1 (see platformio.ini and sim/run.sh).
  if (band == w.priceBand) {
    fprintf(stderr, "BANDDBG update segCount=%d seg0=%p x0=%d x1=%d state0=%d\n",
            segCount, (void*)band->seg[0], segX0[0], segX1[0], segState[0]);
    for (int i = 0; i < segCount; ++i) {
      fprintf(stderr, "  seg[%d] obj=%p x=%d..%d state=%d\n", i, (void*)band->seg[i],
              segX0[i], segX1[i], segState[i]);
    }
  }
#endif
  for (int i = 0; i < kBandMaxSegments; ++i) {
    if (i < segCount) {
      const int w = std::max(1, segX1[i] - segX0[i]);
      lv_obj_set_pos(band->seg[i], segX0[i], band->y);
      lv_obj_set_size(band->seg[i], w, kBandH);
      lv_obj_set_style_bg_color(band->seg[i],
                                lv_color_hex(band->colors[segState[i] + 1]), 0);
      lv_obj_clear_flag(band->seg[i], LV_OBJ_FLAG_HIDDEN);
      band->lastState[i] = segState[i];
      band->lastX0[i] = segX0[i];
      band->lastX1[i] = segX1[i];
    } else {
      lv_obj_add_flag(band->seg[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  band->lastCount = segCount;
}

void setBandVisible(HistoryBand *band, bool visible) {
  if (band == nullptr) return;
  if (visible) {
    lv_obj_clear_flag(band->label, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(band->label, LV_OBJ_FLAG_HIDDEN);
  }
  for (int i = 0; i < kBandMaxSegments; ++i) {
    if (visible && i < band->lastCount) {
      lv_obj_clear_flag(band->seg[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(band->seg[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  for (int k = 0; k < 5; ++k) {
    if (band->tick[k] == nullptr) continue;
    if (visible) {
      lv_obj_clear_flag(band->tick[k], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(band->tick[k], LV_OBJ_FLAG_HIDDEN);
    }
  }
}


// Name on the left, value on the right.
lv_obj_t *row(lv_obj_t *parent, int x, int y, int width, const char *name,
              lv_obj_t **valueOut, const lv_font_t *font = F_BODY) {
  label(parent, x, y, 0, font, palette().muted, name);
  lv_obj_t *value =
      label(parent, x, y, width, font, palette().text, "--", LV_TEXT_ALIGN_RIGHT);
  if (valueOut != nullptr) {
    *valueOut = value;
  }
  return value;
}

void title(lv_obj_t *parent, const char *text) {
  label(parent, 16, 10, 0, F_SMALL, palette().muted, text);
}

void fmtPower(char *out, size_t n, double watts) {
  if (std::fabs(watts) >= 1000.0) {
    snprintf(out, n, "%.2f kW", watts / 1000.0);
  } else {
    snprintf(out, n, "%.0f W", watts);
  }
}

void fmtSignedPower(char *out, size_t n, double watts) {
  char tmp[32];
  fmtPower(tmp, sizeof(tmp), std::fabs(watts));
  snprintf(out, n, "%s%s", watts < 0.0 ? "-" : "+", tmp);
}

void fmtEnergy(char *out, size_t n, double wh) {
  if (std::fabs(wh) >= 1000000.0) {
    snprintf(out, n, "%.2f MWh", wh / 1000000.0);
  } else {
    snprintf(out, n, "%.1f kWh", wh / 1000.0);
  }
}

// Temperatures arrive in degC x 10; INT16_MAX means not available.
void fmtTemp(char *out, size_t n, int16_t t10) {
  if (t10 == INT16_MAX) {
    snprintf(out, n, "--");
  } else {
    snprintf(out, n, "%.1f °C", t10 / 10.0);
  }
}

// Forecast values in kWh: below 10 one decimal (2.6), from 10 integer (15).
void fmtForecastKwh(char *out, size_t n, double kwh) {
  if (kwh < 10.0) {
    snprintf(out, n, "%.1f", kwh);
  } else {
    snprintf(out, n, "%.0f", kwh);
  }
}

void setText(lv_obj_t *l, const char *text) {
  if (l != nullptr) {
    lv_label_set_text(l, text);
  }
}

// Network values change rarely; avoid invalidating the service page every
// second when nothing actually changed.
void setTextIfChanged(lv_obj_t *l, const char *text) {
  if (l == nullptr) {
    return;
  }
  const char *current = lv_label_get_text(l);
  if (current == nullptr || strcmp(current, text) != 0) {
    lv_label_set_text(l, text);
  }
}

// Escape the characters that have a meaning in a WIFI: QR payload. The setup
// credentials are generated from a safe alphabet, but keeping the helper
// generic makes the screen safe for a manually supplied SSID too.
void qrEscape(char *out, size_t n, const char *in) {
  size_t j = 0;
  if (out == nullptr || n == 0) {
    return;
  }
  if (in != nullptr) {
    for (size_t i = 0; in[i] != '\0' && j + 2 < n; ++i) {
      const char c = in[i];
      if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') {
        out[j++] = '\\';
      }
      out[j++] = c;
    }
  }
  out[j] = '\0';
}

void setColor(lv_obj_t *l, uint32_t color) {
  if (l == nullptr) {
    return;
  }
  BackgroundLabel *bg = backgroundLabelFor(l);
  if (bg != nullptr) {
    bg->baseColor = color;
    lv_obj_set_style_text_color(
        l, lv_color_hex(backgroundTextColor(color)), 0);
    return;
  }
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
}

// Like setColor, but only touches the object when the colour actually
// changes. Static pages (heating) otherwise invalidate and redraw every
// second for values that did not move - visible as flicker.
//
// The comparison asks the object for the colour it resolves right now instead
// of keeping a side table. The old table had 32 slots for ~50 labels and
// buttons (three mode cards alone contribute up to 36 background entries), so
// everything past the limit was restyled and invalidated every second.
void setColorIfChanged(lv_obj_t *l, uint32_t color) {
  if (l == nullptr) {
    return;
  }
  BackgroundLabel *bg = backgroundLabelFor(l);
  if (bg != nullptr) {
    // Labels on the switchable background do not show the requested colour
    // directly - setColor() maps it to a contrast variant. The registry holds
    // the base colour that is currently shown, so it doubles as the cache.
    if (bg->baseColor == color) {
      return;
    }
    setColor(l, color);
    return;
  }
  const lv_color_t want = lv_color_hex(color);
  if (lv_color_eq(lv_obj_get_style_text_color(l, LV_PART_MAIN), want)) {
    return;
  }
  lv_obj_set_style_text_color(l, want, 0);
}

// Same idea for the background colour (mode buttons, IO dots, party button).
void setBgColorIfChanged(lv_obj_t *obj, uint32_t color, bool border = false) {
  if (obj == nullptr) {
    return;
  }
  const lv_color_t want = lv_color_hex(color);
  if (border) {
    if (lv_color_eq(lv_obj_get_style_border_color(obj, LV_PART_MAIN), want)) {
      return;
    }
    lv_obj_set_style_border_color(obj, want, 0);
  } else {
    if (lv_color_eq(lv_obj_get_style_bg_color(obj, LV_PART_MAIN), want)) {
      return;
    }
    lv_obj_set_style_bg_color(obj, want, 0);
  }
}

// Wallbox configuration page -------------------------------------------

// German labels used by the WARP web interface for ConfigChargeMode.
const char *chargeModeName(int mode) {
  switch (mode) {
    case 0: return TR("Schnell", "Fast");
    case 1: return TR("Aus", "Off");
    case 2: return TR("PV", "PV");
    case 3: return TR("Min + PV", "Min + PV");
    case 4: return TR("Standard", "Standard");
    case 5: return TR("Min", "Min");
    case 6: return "Eco";
    case 7: return TR("Eco + PV", "Eco + PV");
    case 8: return TR("Eco + Min", "Eco + Min");
    case 9: return TR("Eco + Min + PV", "Eco + Min + PV");
    default: return "?";
  }
}

// CASState as reported in /charge_manager/state (`s`), named the way WARP's
// own web interface names it, so the panel and the device agree on what a box
// is doing. The states matter as a whole: 3 means the car is plugged in and
// the box would charge, which is neither charging nor blocked, and 5 is a
// fault the box reports while it still looks connected.
const char *casStateName(int state) {
  switch (state) {
    case 0: return TR("Kein Fahrzeug", "No vehicle");
    case 1:
    case 2: return TR("Warte auf Freigabe", "Waiting for release");
    case 3: return TR("Ladebereit", "Ready to charge");
    case 4: return TR("Laden", "Charging");
    case 5: return TR("Fehler", "Error");
    case 6: return TR("Fertig", "Finished");
    case 7: return TR("Blockiert", "Blocked");
    default: return TR("Bereit", "Ready");
  }
}

// The modes the panel offers on the buttons. Older firmware has no
// supported_charge_modes endpoint, so fall back to Fast and Off.
int uiModes(const WarpData &d, int *out) {
  int n = 0;
  if (d.supported_mode_count > 0) {
    for (int i = 0; i < d.supported_mode_count && n < kModeSlots; ++i) {
      out[n++] = d.supported_modes[i];
    }
  } else {
    out[n++] = 0;  // Schnell
    out[n++] = 1;  // Aus
  }
  return n;
}

void styleModeButton(lv_obj_t *button, bool selected) {
  if (button == nullptr) {
    return;
  }
  // The opacity is set once when the button is created; setting it here again
  // would invalidate every mode button on every update, even when the
  // selection did not change.
  setBgColorIfChanged(button, selected ? palette().accent : palette().buttonBg);
  // Only the fill marks a selection; the border stays like an idle button.
  setBgColorIfChanged(button, palette().line, true);
  lv_obj_t *lbl = lv_obj_get_child(button, 0);
  if (lbl != nullptr) {
    setColorIfChanged(lbl, selected ? palette().onAccent
                                   : palette().inactiveButtonText);
  }
}

void applyModeSelection(ModeCard &mc, int selected) {
  for (int i = 0; i < mc.modeCount; ++i) {
    styleModeButton(mc.buttons[i], mc.modes[i] == selected);
  }
}

void onGlobalModeClicked(lv_event_t *e) {
  const int mode = static_cast<int>(
      reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  g_pendingGlobal.mode = mode;
  g_pendingGlobal.untilMs = lv_tick_get() + kPendingModeMs;
  g_pendingGlobal.active = true;
  applyModeSelection(g_globalCard, mode);
  if (g_chargeModeHandler != nullptr) {
    g_chargeModeHandler(-1, mode);
  }
}

void onChargerModeClicked(lv_event_t *e) {
  const intptr_t code =
      reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
  const int index = static_cast<int>(code >> 8);
  const int mode = static_cast<int>(code & 0xFF);
  if (index < 0 || index >= kWarpMaxChargers) {
    return;
  }
  g_pendingCharger[index].mode = mode;
  g_pendingCharger[index].untilMs = lv_tick_get() + kPendingModeMs;
  g_pendingCharger[index].active = true;
  applyModeSelection(g_chargerCards[index], mode);
  if (g_chargeModeHandler != nullptr) {
    g_chargeModeHandler(index, mode);
  }
}

ModeCard buildModeCard(lv_obj_t *parent, bool global, int index,
                       const WarpData &d) {
  ModeCard mc;
  mc.card = lv_obj_create(parent);
  lv_obj_remove_style_all(mc.card);
  lv_obj_set_width(mc.card, LV_PCT(100));
  lv_obj_set_height(mc.card, 96);
  lv_obj_set_style_bg_color(mc.card, lv_color_hex(palette().card), 0);
  lv_obj_set_style_bg_opa(mc.card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(mc.card, 10, 0);
  lv_obj_set_style_border_width(mc.card, 1, 0);
  lv_obj_set_style_border_color(mc.card,
                                lv_color_hex(palette().line), 0);
  lv_obj_clear_flag(mc.card, LV_OBJ_FLAG_SCROLLABLE);

  if (global) {
    mc.name = label(mc.card, 16, 8, 520, F_TITLE, palette().text, TR("ALLE WALLBOXEN", "ALL WALLBOXES"));
    mc.state = label(mc.card, 16, 32, 520, F_SMALL, palette().muted,
                     TR("Modus für alle Ladepunkte", "Mode for all charging points"));
  } else {
    const WarpChargerData &c = d.chargers[index];
    char name[48];
    if (c.name[0] != '\0') {
      snprintf(name, sizeof(name), "%s", c.name);
    } else {
      snprintf(name, sizeof(name), TR("Wallbox %d", "Wallbox %d"), index + 1);
    }
    mc.name = label(mc.card, 16, 8, 430, F_TITLE, palette().text, name);
    mc.state = label(mc.card, 16, 32, 430, F_SMALL, palette().muted, "--");
    mc.power = label(mc.card, 470, 8, 272, F_TITLE, palette().muted, "--",
                     LV_TEXT_ALIGN_RIGHT);
  }

  mc.modeCount = uiModes(d, mc.modes);
  // Card content is 774 px wide (790 page minus 8 px padding per side); keep
  // 16 px left and right inside the card.
  constexpr int kAvailable = 774 - 32;
  constexpr int kGap = 8;
  int buttonWidth =
      (kAvailable - (mc.modeCount - 1) * kGap) / (mc.modeCount > 0 ? mc.modeCount : 1);
  if (buttonWidth > 170) {
    buttonWidth = 170;
  }
  int x = 16;
  for (int i = 0; i < mc.modeCount; ++i) {
    lv_obj_t *b = lv_btn_create(mc.card);
    lv_obj_set_pos(b, x, 54);
    lv_obj_set_size(b, buttonWidth, 34);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(palette().card2), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(palette().line), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *lbl = lv_label_create(b);
    lv_label_set_text(lbl, chargeModeName(mc.modes[i]));
    lv_obj_set_style_text_font(lbl, F_SMALL, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(palette().muted), 0);
    lv_obj_center(lbl);
    const intptr_t code =
        global ? static_cast<intptr_t>(mc.modes[i])
               : (static_cast<intptr_t>(index) << 8) | mc.modes[i];
    lv_obj_add_event_cb(
        b, global ? onGlobalModeClicked : onChargerModeClicked,
        LV_EVENT_CLICKED, reinterpret_cast<void *>(code));
    mc.buttons[i] = b;
    x += buttonWidth + kGap;
  }
  return mc;
}

bool wallboxLayoutChanged(const WarpData &d) {
  int count = d.charger_count;
  if (count > kWarpMaxChargers) {
    count = kWarpMaxChargers;
  }
  if (count != g_renderedChargers) {
    return true;
  }
  int modes[kModeSlots] = {};
  const int modeCount = uiModes(d, modes);
  if (modeCount != g_renderedModeCount) {
    return true;
  }
  for (int i = 0; i < modeCount; ++i) {
    if (modes[i] != g_renderedModes[i]) {
      return true;
    }
  }
  return false;
}

void rebuildModeCards(const WarpData &d) {
  if (w.page5 == nullptr) {
    return;
  }
  lv_obj_clean(w.page5);
  g_globalCard = ModeCard();
  for (int i = 0; i < kWarpMaxChargers; ++i) {
    g_chargerCards[i] = ModeCard();
  }

  int modes[kModeSlots] = {};
  g_renderedModeCount = uiModes(d, modes);
  for (int i = 0; i < g_renderedModeCount; ++i) {
    g_renderedModes[i] = modes[i];
  }
  g_renderedChargers = d.charger_count;
  if (g_renderedChargers > kWarpMaxChargers) {
    g_renderedChargers = kWarpMaxChargers;
  }

  if (g_renderedChargers <= 0) {
    lv_obj_t *empty = lv_obj_create(w.page5);
    lv_obj_remove_style_all(empty);
    lv_obj_set_width(empty, LV_PCT(100));
    lv_obj_set_height(empty, 96);
    lv_obj_set_style_bg_color(empty, lv_color_hex(palette().card), 0);
    lv_obj_set_style_bg_opa(empty, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(empty, 10, 0);
    lv_obj_set_style_border_width(empty, 1, 0);
    lv_obj_set_style_border_color(empty, lv_color_hex(palette().line), 0);
    lv_obj_clear_flag(empty, LV_OBJ_FLAG_SCROLLABLE);
    label(empty, 16, 36, 0, F_BODY, palette().muted,
          TR("Keine Wallbox konfiguriert.", "No wallbox configured."));
    return;
  }

  g_globalCard = buildModeCard(w.page5, true, -1, d);
  for (int i = 0; i < g_renderedChargers; ++i) {
    g_chargerCards[i] = buildModeCard(w.page5, false, i, d);
  }
}

void updateModeCard(ModeCard &mc, const WarpData &d, int index, bool global) {
  if (mc.card == nullptr) {
    return;
  }
  const uint32_t now = lv_tick_get();
  const int actualMode = global ? d.charge_mode : d.chargers[index].mode;
  PendingMode &pending =
      global ? g_pendingGlobal : g_pendingCharger[index];
  int shownMode = actualMode;
  if (pending.active) {
    if (actualMode == pending.mode ||
        static_cast<int32_t>(now - pending.untilMs) >= 0) {
      pending.active = false;
    } else {
      shownMode = pending.mode;
    }
  }
  applyModeSelection(mc, shownMode);

  if (global) {
    return;
  }

  const WarpChargerData &c = d.chargers[index];
  char buf[48];
  if (c.name[0] != '\0') {
    snprintf(buf, sizeof(buf), "%s", c.name);
  } else {
    snprintf(buf, sizeof(buf), TR("Wallbox %d", "Wallbox %d"), index + 1);
  }
  setTextIfChanged(mc.name, buf);

  const char *stateName = casStateName(c.state);
  if (c.estimated && c.state == 4) {
    snprintf(buf, sizeof(buf), TR("%s (berechnet)", "%s (calculated)"),
             stateName);
  } else {
    snprintf(buf, sizeof(buf), "%s", stateName);
  }
  setTextIfChanged(mc.state, buf);
  setColorIfChanged(mc.state,
           c.state == 5 ? palette().red : (c.state == 4 ? palette().ev : palette().muted));

  if (c.power > 0.5f) {
    fmtPower(buf, sizeof(buf), c.power);
    setTextIfChanged(mc.power, buf);
    setColorIfChanged(mc.power, palette().ev);
  } else {
    setTextIfChanged(mc.power, "--");
    setColorIfChanged(mc.power, palette().muted);
  }
}

// Power-history helpers -------------------------------------------------

const PowerSample &sampleAt(int index) {
  static const PowerSample empty{};
  if (g_samples == nullptr) {
    return empty;
  }
  return g_samples[(g_sampleHead + index) % kMaxSamples];
}

// History value at an age before the newest sample. The samples carry their
// own timestamp, so a point between two samples is interpolated instead of
// holding the older one: that removes the stairs without adding a sample.
// Across a gap the value is unknown and the chart leaves the point blank.
// The returned timestamp is the point's own time, which keeps a tapped legend
// consistent with the line it points at.
bool sampleAtAge(uint32_t ageMs, PowerSample &out) {
  if (g_sampleCount == 0) {
    return false;
  }
  const uint32_t newest = sampleAt(g_sampleCount - 1).timeMs;
  if (ageMs > newest - sampleAt(0).timeMs) {
    return false;  // left of the recorded history
  }

  int older = 0;
  while (older + 1 < g_sampleCount &&
         (uint32_t)(newest - sampleAt(older + 1).timeMs) >= ageMs) {
    ++older;
  }
  const PowerSample &a = sampleAt(older);
  const uint32_t ageA = newest - a.timeMs;
  if (ageA == ageMs) {
    out = a;
    return true;
  }
  if (older + 1 >= g_sampleCount) {
    return false;
  }
  const PowerSample &b = sampleAt(older + 1);
  const uint32_t ageB = newest - b.timeMs;
  if (ageA - ageB > kChartGapMs) {
    return false;
  }

  const float f = (float)(ageA - ageMs) / (float)(ageA - ageB);
  out.timeMs = newest - ageMs;
  out.grid = a.grid + (b.grid - a.grid) * f;
  out.pv = a.pv + (b.pv - a.pv) * f;
  out.house = a.house + (b.house - a.house) * f;
  out.battery = a.battery + (b.battery - a.battery) * f;
  out.wb1 = a.wb1 + (b.wb1 - a.wb1) * f;
  out.wb2 = a.wb2 + (b.wb2 - a.wb2) * f;
  // One missing battery value is enough to leave SOC out for this point.
  out.soc = (a.soc < 0.0f || b.soc < 0.0f) ? -1.0f
                                            : a.soc + (b.soc - a.soc) * f;
  return true;
}

// Keep the x labels in the same units the window uses: 45m for an hour,
// 1h30 for the middle of a two-hour window, and so on.
void formatAgeLabel(char *out, size_t n, uint32_t seconds) {
  if (seconds == 0) {
    snprintf(out, n, "0h");
  } else if (seconds >= 3600) {
    const uint32_t hours = seconds / 3600;
    const uint32_t minutes = (seconds % 3600) / 60;
    if (minutes == 0) {
      snprintf(out, n, "%uh", hours);
    } else {
      snprintf(out, n, "%uh%02u", hours, minutes);
    }
  } else {
    snprintf(out, n, "%um", (seconds + 30) / 60);
  }
}

void updateTimeLabels(int windowSec) {
  for (int i = 0; i < 5; ++i) {
    const uint32_t remaining =
        (uint32_t)((uint64_t)windowSec * (uint32_t)(4 - i) / 4u);
    char buf[16];
    formatAgeLabel(buf, sizeof(buf), remaining);
    setText(w.timeLabel[i], buf);
  }
}

// 1h, 2h, 4h, 6h, 12h, 24h. The shortest window is used until the history
// actually spans longer than it.
int selectWindow(uint32_t spanMs) {
  const uint32_t spanSec = spanMs / 1000u;
  if (spanSec <= 3600u) return 3600;
  if (spanSec <= 7200u) return 7200;
  if (spanSec <= 4u * 3600u) return 4 * 3600;
  if (spanSec <= 6u * 3600u) return 6 * 3600;
  if (spanSec <= 12u * 3600u) return 12 * 3600;
  return 24 * 3600;
}

// Value plotted at chart point pointIdx (same interpolation as
// rebuildPowerChart). Returns false in the empty left region and in a gap.
bool sampleForChartPoint(int pointIdx, PowerSample &out) {
  if (g_sampleCount == 0 || pointIdx < 0 || pointIdx >= kChartPoints) {
    return false;
  }
  const uint32_t newest = sampleAt(g_sampleCount - 1).timeMs;
  const uint32_t oldest = sampleAt(0).timeMs;
  const uint32_t windowMs = (uint32_t)selectWindow(newest - oldest) * 1000u;
  const uint32_t age = (uint32_t)((uint64_t)windowMs *
                                  (uint32_t)(kChartPoints - 1 - pointIdx) /
                                  (uint32_t)(kChartPoints - 1));
  return sampleAtAge(age, out);
}

const char *legendBaseName(int i) {
  static const char *const names[7] = {TR("Netz", "Grid"), TR("PV", "PV"),
                                       TR("Verbrauch", "Consumption"),
                                       TR("Akku", "Battery"), nullptr, nullptr,
                                       TR("Akku %", "Battery %")};
  if (i == 4 || i == 5) {
    return g_wbBase[i - 4];
  }
  return names[i];
}

void applyTapLabels() {
  const float values[7] = {g_tapSample.grid, g_tapSample.pv,
                           g_tapSample.house, g_tapSample.battery,
                           g_tapSample.wb1, g_tapSample.wb2, g_tapSample.soc};
  for (int i = 0; i < 7; ++i) {
    if (w.legendLabel[i] == nullptr) {
      continue;
    }
    char buf[48];
    if (i == 6) {
      if (g_tapSample.soc < 0.0f) {
        snprintf(buf, sizeof(buf), "%s [--]", legendBaseName(i));
      } else {
        snprintf(buf, sizeof(buf), "%s [%d%%]", legendBaseName(i),
                 (int)lroundf(g_tapSample.soc));
      }
    } else if (std::fabs(values[i]) >= 1000.0f) {
      // Compact: kW from 1000 W up, no space before the unit, so the
      // legend labels stay on one line even for large values.
      snprintf(buf, sizeof(buf), "%s [%.1fkW]", legendBaseName(i),
               values[i] / 1000.0f);
    } else {
      snprintf(buf, sizeof(buf), "%s [%dW]", legendBaseName(i),
               (int)lroundf(values[i]));
    }
    setTextIfChanged(w.legendLabel[i], buf);
  }
}

void restoreLegendLabels(const WarpData &d) {
  for (int i = 0; i < 7; ++i) {
    if (w.legendLabel[i] == nullptr) {
      continue;
    }
    if (i == 4 || i == 5) {
      const int k = i - 4;
      if (d.charger_count <= k) {
        continue;
      }
      const char *name = d.chargers[k].name[0] != '\0'
                             ? d.chargers[k].name
                             : (k == 0 ? "Wallbox 1" : "Wallbox 2");
      char shortName[16];
      snprintf(shortName, sizeof(shortName), "%.12s", name);
      snprintf(g_wbBase[k], sizeof(g_wbBase[k]), "%s", shortName);
      setTextIfChanged(w.legendLabel[i], shortName);
    } else {
      setTextIfChanged(w.legendLabel[i], legendBaseName(i));
    }
  }
}

// Chart geometry, as LVGL's chart widget computes it: x = border + w * i /
// (points - 1) and y measured down from the top of the content area, with the
// value range centred on zero.
inline int chartX(int point) {
  return kChartBorder + (kChartW - 2 * kChartBorder) * point /
                            (kChartPoints - 1);
}

inline float chartY(float value, int range) {
  const float h = (float)(kChartH - 2 * kChartBorder);
  return (float)kChartBorder + h -
         ((value + (float)range) * h) / (2.0f * (float)range);
}

// Blend one pixel of the antialiased two-pixel line into the cached image.
void blendChartPixel(int x, int y, uint16_t color, float coverage) {
  if (x < kChartBorder || x >= kChartW - kChartBorder || y < kChartBorder ||
      y >= kChartH - kChartBorder) {
    return;
  }
  uint16_t &dst = g_chartPixels[y * kChartW + x];
  if (coverage >= 0.999f) {
    dst = color;
    return;
  }
  const int alpha = (int)lroundf(coverage * 255.0f);
  if (alpha <= 0) {
    return;
  }
  const int r0 = (dst >> 11) & 0x1f;
  const int g0 = (dst >> 5) & 0x3f;
  const int b0 = dst & 0x1f;
  const int r1 = (color >> 11) & 0x1f;
  const int g1 = (color >> 5) & 0x3f;
  const int b1 = color & 0x1f;
  const int r = r0 + ((r1 - r0) * alpha + 127) / 255;
  const int g = g0 + ((g1 - g0) * alpha + 127) / 255;
  const int b = b0 + ((b1 - b0) * alpha + 127) / 255;
  dst = (uint16_t)((r << 11) | (g << 5) | b);
}

// Coverage of a pixel row by a two-pixel line centred at y: the overlap of the
// row with [y-1, y+1]. Subpixel centres and blended edges keep a shallow slope
// from turning into a staircase.
void plotChartPixel(int x, float y, uint16_t color) {
  const float lo = y - 1.0f;
  const float hi = y + 1.0f;
  for (int row = (int)floorf(lo); row <= (int)floorf(hi); ++row) {
    const float overlap =
        std::min((float)(row + 1), hi) - std::max((float)row, lo);
    if (overlap > 0.0f) {
      blendChartPixel(x, row, color, std::min(1.0f, overlap));
    }
  }
}

// Polyline through the interpolated series. Points marked NONE break the line,
// which is how a gap in the history becomes visible on the chart.
void drawChartSeries(const int32_t *values, uint16_t color) {
  if (g_chartPixels == nullptr) {
    return;
  }
  bool havePrev = false;
  float prevX = 0.0f;
  float prevY = 0.0f;
  for (int i = 0; i < kChartPoints; ++i) {
    const int32_t v = values[i];
    if (v == LV_CHART_POINT_NONE) {
      havePrev = false;
      continue;
    }
    // Keep subpixel x positions as well: with 240 points on a 764 px plot the
    // spacing is fractional, and rounding it would show up as uneven steps.
    const float x = (float)kChartBorder +
                    (float)(kChartW - 2 * kChartBorder) * (float)i /
                        (float)(kChartPoints - 1);
    const float y = chartY((float)v, g_chartRange);
    if (!havePrev) {
      plotChartPixel((int)lroundf(x), y, color);
    } else {
      const float dx = x - prevX;
      const float dy = y - prevY;
      const int steps = std::max(
          1, (int)std::ceil(std::fmax(std::fabs(dx), std::fabs(dy))));
      // Step along the segment instead of once per chart column: a steep jump
      // stays connected, a flat one is painted just once.
      for (int s = 1; s <= steps; ++s) {
        const float f = (float)s / (float)steps;
        plotChartPixel((int)lroundf(prevX + dx * f), prevY + dy * f, color);
      }
    }
    havePrev = true;
    prevX = x;
    prevY = y;
  }
}

// Repaint the whole chart image: the card first, so the rounded corners blend
// into whatever the page background is right now, then the seven series in the
// order the two chart widgets drew them (power first, SOC on top).
void renderChartImage() {
  if (g_chartPixels == nullptr || w.chartImage == nullptr) {
    return;
  }
  // Pixels outside the rounded card show the page background, so the buffer
  // starts as a copy of it before the card is drawn on top.
  lv_canvas_fill_bg(w.chartImage,
                    lv_obj_get_style_bg_color(lv_screen_active(), LV_PART_MAIN),
                    LV_OPA_COVER);
  lv_layer_t layer;
  lv_canvas_init_layer(w.chartImage, &layer);
  lv_draw_rect_dsc_t cardDsc;
  lv_draw_rect_dsc_init(&cardDsc);
  cardDsc.base.layer = &layer;
  cardDsc.bg_color = lv_color_hex(palette().card);
  cardDsc.bg_opa = LV_OPA_COVER;
  cardDsc.radius = 10;
  cardDsc.border_color = lv_color_hex(palette().line);
  cardDsc.border_opa = LV_OPA_COVER;
  cardDsc.border_width = kChartBorder;
  lv_area_t area = {0, 0, kChartW - 1, kChartH - 1};
  lv_draw_rect(&layer, &cardDsc, &area);
  lv_canvas_finish_layer(w.chartImage, &layer);

  // LVGL's chart draws its divider lines before the series: three vertical
  // lines at quarter widths and one horizontal line in the middle, in the
  // muted line colour. They run across the whole widget, over the border
  // pixels, so they are written straight into the buffer after the card.
  const uint16_t divider = lv_color_to_u16(lv_color_hex(palette().muted));
  const int plotW = kChartW - 2 * kChartBorder;
  const int plotH = kChartH - 2 * kChartBorder;
  for (int i = 1; i < 4; ++i) {
    const int x = kChartBorder + plotW * i / 4;
    for (int y = 0; y < kChartH; ++y) {
      g_chartPixels[y * kChartW + x] = divider;
    }
  }
  const int midY = kChartBorder + plotH / 2;
  for (int x = 0; x < kChartW; ++x) {
    g_chartPixels[midY * kChartW + x] = divider;
  }

  // Series colours are shared with the LVGL chart and the legend so a series
  // keeps its identity in every theme; only the card and divider are themed.
  const int32_t *const series[7] = {g_chartGrid, g_chartPv, g_chartHouse,
                                    g_chartBattery, g_chartWb1, g_chartWb2,
                                    g_chartSoc};
  for (int i = 0; i < 7; ++i) {
    if (series[i] != nullptr) {
      drawChartSeries(series[i],
                     lv_color_to_u16(lv_color_hex(kSeriesColors[i])));
    }
  }
  lv_obj_invalidate(w.chartImage);
}

void chartTapEvent(lv_event_t *e) {
  lv_obj_t *chart = w.powerChart != nullptr ? w.powerChart : w.chartImage;
  if (chart == nullptr) {
    return;
  }
  lv_point_t p{};
  lv_indev_get_point(lv_indev_get_act(), &p);
  lv_area_t coords{};
  lv_obj_get_coords(chart, &coords);
  int localX = p.x - coords.x1;
  if (localX < 0) localX = 0;
  const int chartW = coords.x2 - coords.x1;
  if (localX > chartW) localX = chartW;
  const int pointIdx =
      (int)lroundf((float)localX * (float)(kChartPoints - 1) / (float)chartW);
  PowerSample sample{};
  if (sampleForChartPoint(pointIdx, sample)) {
    g_tapSample = sample;
    g_tapMs = lv_tick_get();
    g_tapActive = true;
    // Mark the sample the legend is showing: the cursor snaps to the sample
    // position, not to the finger, so line and values belong together.
    if (w.tapLine != nullptr) {
      lv_obj_set_x(w.tapLine, 12 + chartX(pointIdx));
      lv_obj_clear_flag(w.tapLine, LV_OBJ_FLAG_HIDDEN);
    }
    applyTapLabels();
  }
}

void rebuildPowerChart();

void recordPowerSample(uint32_t now, const WarpData &d) {
  if (g_samples == nullptr || g_sgSamples == nullptr) {
    return;
  }
  if (g_sampleCount == kMaxSamples) {
    g_sampleHead = (g_sampleHead + 1) % kMaxSamples;
    --g_sampleCount;
  }
  const int index = (g_sampleHead + g_sampleCount) % kMaxSamples;
  const float wb1 = d.charger_count > 0 ? d.chargers[0].power : 0.0f;
  const float wb2 = d.charger_count > 1 ? d.chargers[1].power : 0.0f;
  const float soc = d.hasBattery ? d.soc : -1.0f;
  g_samples[index] = {now, d.gridPower, d.pvPower, d.housePower,
                      d.batteryPower, wb1, wb2, soc};
  ++g_sampleCount;

  if (g_sgCount == kMaxSamples) {
    g_sgHead = (g_sgHead + 1) % kMaxSamples;
    --g_sgCount;
  }
  const int sgIndex = (g_sgHead + g_sgCount) % kMaxSamples;
  g_sgSamples[sgIndex] = {now,
                          (int8_t)(!d.hasHeating ? -1
                                   : (d.sgrBlocking && d.sgrExtended) ? 3
                                   : d.sgrBlocking ? 1
                                   : d.sgrExtended ? 2
                                                   : 0)};
  ++g_sgCount;
  while (g_sgCount > 1 &&
         (uint32_t)(now - g_sgSamples[g_sgHead].timeMs) > kHistoryMs) {
    g_sgHead = (g_sgHead + 1) % kMaxSamples;
    --g_sgCount;
  }

  // Drop the oldest entries as soon as they fall out of the 24 h window.
  while (g_sampleCount > 1 &&
         (uint32_t)(now - sampleAt(0).timeMs) > kHistoryMs) {
    g_sampleHead = (g_sampleHead + 1) % kMaxSamples;
    --g_sampleCount;
  }
}

// Resample the raw history into the fixed chart width. Points are interpolated
// between the timestamped samples, so the curve follows the actual times and
// has no stairs. Unknown regions (before the first sample and gaps in the log)
// stay blank instead of being filled with an invented value.
void rebuildPowerChart() {
  if ((w.powerChart == nullptr && w.chartImage == nullptr) ||
      g_chartData == nullptr || g_sampleCount == 0) {
    return;
  }

  const uint32_t newest = sampleAt(g_sampleCount - 1).timeMs;
  const uint32_t oldest = sampleAt(0).timeMs;
  const int windowSec = selectWindow(newest - oldest);
  const uint32_t windowMs = (uint32_t)windowSec * 1000u;
  updateTimeLabels(windowSec);

  bool haveValue = false;
  float mn = 0.0f;
  float mx = 0.0f;

  for (int i = 0; i < kChartPoints; ++i) {
    const uint32_t age = (uint32_t)(
        (uint64_t)windowMs * (uint32_t)(kChartPoints - 1 - i) /
        (uint32_t)(kChartPoints - 1));

    PowerSample s{};
    if (!sampleAtAge(age, s)) {
      g_chartGrid[i] = LV_CHART_POINT_NONE;
      g_chartPv[i] = LV_CHART_POINT_NONE;
      g_chartHouse[i] = LV_CHART_POINT_NONE;
      g_chartBattery[i] = LV_CHART_POINT_NONE;
      g_chartWb1[i] = LV_CHART_POINT_NONE;
      g_chartWb2[i] = LV_CHART_POINT_NONE;
      g_chartSoc[i] = LV_CHART_POINT_NONE;
      continue;
    }

    const float values[6] = {s.grid, s.pv, s.house, s.battery, s.wb1, s.wb2};
    int32_t *const out[6] = {g_chartGrid, g_chartPv, g_chartHouse,
                             g_chartBattery, g_chartWb1, g_chartWb2};
    for (int j = 0; j < 6; ++j) {
      out[j][i] = (int32_t)lroundf(values[j]);
      if (!haveValue) {
        mn = mx = values[j];
        haveValue = true;
      }
      if (values[j] < mn) mn = values[j];
      if (values[j] > mx) mx = values[j];
    }
    g_chartSoc[i] = (int32_t)lroundf(s.soc);
  }

  // Round outwards on a progressive step grid: 500 W steps up to 2 kW,
  // 2000 W steps up to 10 kW, 5000 W steps above - open-ended, no cap.
  // A symmetric range keeps the zero line in the middle, which the static
  // chart background and the three scale labels use.
  const float absMax = std::fmax(std::fabs(mn), std::fabs(mx));
  const int stepW = absMax <= 2000.0f ? 500 : (absMax <= 10000.0f ? 2000 : 5000);
  int steps = (int)std::ceil(absMax / (float)stepW);
  if (steps < 1) steps = 1;
  const int range = steps * stepW;

  g_chartRange = range;
  if (w.powerChart != nullptr) {
    lv_chart_set_range(w.powerChart, LV_CHART_AXIS_PRIMARY_Y, -range, range);
    if (w.socChart != nullptr) {
      lv_chart_set_range(w.socChart, LV_CHART_AXIS_PRIMARY_Y, -range, range);
    }
  }
  for (int i = 0; i < kChartPoints; ++i) {
    if (g_chartSoc[i] == LV_CHART_POINT_NONE || g_chartSoc[i] < 0) {
      g_chartSoc[i] = LV_CHART_POINT_NONE;
    } else {
      const int pct = std::min(100, std::max(0, g_chartSoc[i]));
      g_chartSoc[i] = (int32_t)lroundf(
          ((float)pct / 100.0f) * (2.0f * (float)range) - (float)range);
    }
  }
  char buf[24];
  if (range % 1000 == 0) {
    snprintf(buf, sizeof(buf), "%d kW", range / 1000);
    setText(w.scaleMax, buf);
    snprintf(buf, sizeof(buf), "-%d kW", range / 1000);
    setText(w.scaleMin, buf);
  } else {
    snprintf(buf, sizeof(buf), "%.1f kW", range / 1000.0f);
    setText(w.scaleMax, buf);
    snprintf(buf, sizeof(buf), "-%.1f kW", range / 1000.0f);
    setText(w.scaleMin, buf);
  }
  setText(w.scaleMid, TR("0 kW", "0 kW"));
  if (w.chartImage != nullptr) {
    renderChartImage();
  } else {
    lv_chart_refresh(w.powerChart);
    if (w.socChart != nullptr) lv_chart_refresh(w.socChart);
  }
}

struct FlowNode {
  lv_obj_t *card = nullptr;
  lv_obj_t *value = nullptr;
  lv_obj_t *state = nullptr;
};

// One source/sink card on the flow page: coloured icon badge, name, live
// value and a short state line. Kept flat (no shadow/layer) so a redraw stays
// cheap on the ESP32.
FlowNode flowNode(lv_obj_t *parent, int x, int y, const char *name,
                  const char *symbol, uint32_t accent, uint32_t accentDim,
                  const lv_font_t *iconFont = F_TITLE) {
  FlowNode n;
  n.card = lv_obj_create(parent);
  lv_obj_remove_style_all(n.card);
  lv_obj_set_pos(n.card, x, y);
  lv_obj_set_size(n.card, 176, 94);
  lv_obj_set_style_bg_color(n.card, lv_color_hex(palette().card), 0);
  lv_obj_set_style_bg_opa(n.card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(n.card, 16, 0);
  lv_obj_set_style_border_width(n.card, 1, 0);
  lv_obj_set_style_border_color(n.card, lv_color_hex(accentDim), 0);
  lv_obj_clear_flag(n.card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *badge = lv_obj_create(n.card);
  lv_obj_remove_style_all(badge);
  lv_obj_set_pos(badge, 12, 14);
  lv_obj_set_size(badge, 44, 44);
  lv_obj_set_style_radius(badge, 14, 0);
  lv_obj_set_style_bg_color(badge, lv_color_hex(accentDim), 0);
  lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
  lv_obj_t *icon =
      label(badge, 0, 0, 44, iconFont, accent, symbol, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(icon);

  label(n.card, 68, 12, 96, F_SMALL, palette().muted, name);
  n.value = label(n.card, 68, 30, 96, F_TITLE, accent, "--");
  n.state = label(n.card, 12, 68, 152, F_SMALL, palette().muted, "--");
  return n;
}

// Static thick link. The dim track is always visible so the topology stays
// readable; the coloured line is shown only while power actually flows.
void flowLink(lv_obj_t *parent, int idx, int x1, int y1, int x2, int y2,
              uint32_t color) {
  w.flowPts[idx][0] = {static_cast<lv_value_precise_t>(x1),
                       static_cast<lv_value_precise_t>(y1)};
  w.flowPts[idx][1] = {static_cast<lv_value_precise_t>(x2),
                       static_cast<lv_value_precise_t>(y2)};

  w.flowTrack[idx] = lv_line_create(parent);
  lv_line_set_points(w.flowTrack[idx], w.flowPts[idx], 2);
  lv_obj_set_style_line_width(w.flowTrack[idx], 7, 0);
  lv_obj_set_style_line_color(w.flowTrack[idx], lv_color_hex(palette().line), 0);
  lv_obj_set_style_line_rounded(w.flowTrack[idx], true, 0);

  w.flowLine[idx] = lv_line_create(parent);
  lv_line_set_points(w.flowLine[idx], w.flowPts[idx], 2);
  lv_obj_set_style_line_width(w.flowLine[idx], 7, 0);
  lv_obj_set_style_line_color(w.flowLine[idx], lv_color_hex(color), 0);
  lv_obj_set_style_line_rounded(w.flowLine[idx], true, 0);
  lv_obj_add_flag(w.flowLine[idx], LV_OBJ_FLAG_HIDDEN);
}

void flowDots(lv_obj_t *parent, int idx, uint32_t color) {
  for (int d = 0; d < 2; ++d) {
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    w.flowDot[idx][d] = dot;
  }
}

void showPage(int page) {
  g_activePage = page;
  // Display order. The flow view was built as page4, but it is the first page
  // users should see; the energy, history, wallbox and service pages follow it.
  lv_obj_t *pages[6] = {w.page4, w.page1, w.page2, w.page5, w.page6, w.page3};
  lv_obj_t *captions[6] = {w.caption4, w.caption1, w.caption2, w.caption5,
                           w.caption6, w.caption3};
  lv_obj_t *buttons[6] = {w.pageBtn1, w.pageBtn2, w.pageBtn3, w.pageBtn4,
                          w.pageBtn5, w.pageBtn6};

  for (int i = 0; i < 6; ++i) {
    const bool active = (i == page - 1);
    if (pages[i] != nullptr) {
      if (active) {
        lv_obj_clear_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
      }
    }
    if (captions[i] != nullptr) {
      if (active) {
        lv_obj_clear_flag(captions[i], LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(captions[i], LV_OBJ_FLAG_HIDDEN);
      }
    }
    if (buttons[i] != nullptr) {
      lv_obj_set_style_bg_color(buttons[i],
                                lv_color_hex(active ? palette().activeButtonBg : palette().buttonBg), 0);
      lv_obj_set_style_bg_opa(buttons[i], LV_OPA_COVER, 0);
      lv_obj_t *lbl = lv_obj_get_child(buttons[i], 0);
      if (lbl != nullptr) {
        lv_obj_set_style_text_color(
            lbl, lv_color_hex(active ? palette().activeButtonText
                                   : palette().inactiveButtonText), 0);
      }
    }
  }
}

}  // namespace

// Replace the live ring with the rows read back from the SD log. The restored
// timestamps are placed relative to the same history clock live samples use, so
// the chart's window and the SG band need no special case. The first live
// update after seeding records the current values immediately
// (g_lastSampleMs = 0), which also closes the gap between the newest restored
// row and "now".
void seedHistory(const HistoryPoint *points, int count, uint32_t nowEpoch) {
  g_sampleHead = 0;
  g_sampleCount = 0;
  g_sgHead = 0;
  g_sgCount = 0;
  g_haveSample = false;
  g_lastSampleMs = 0;
  g_lastDataUpdateMs = 0;

  if (points == nullptr || count <= 0 || nowEpoch == 0) {
    return;
  }
  if (g_samples == nullptr || g_sgSamples == nullptr) {
    return;
  }

  const uint32_t base = histNowMs();
  uint32_t lastMs = 0;
  for (int i = 0; i < count; ++i) {
    const HistoryPoint &p = points[i];
    if (p.ts == 0 || p.ts > nowEpoch + 60u) {
      continue;
    }
    const uint32_t ageSec = nowEpoch - p.ts;
    if (ageSec > kHistoryMs / 1000u) {
      continue;
    }
    const uint32_t timeMs = base - ageSec * 1000u;
    if (lastMs != 0 && (int32_t)(timeMs - lastMs) <= 0) {
      continue;  // duplicate or out-of-order row
    }
    lastMs = timeMs;

    if (g_sampleCount == kMaxSamples) {
      g_sampleHead = (g_sampleHead + 1) % kMaxSamples;
      --g_sampleCount;
    }
    const int index = (g_sampleHead + g_sampleCount) % kMaxSamples;
    g_samples[index] = {timeMs, p.grid, p.pv, p.house, p.battery, p.wb1,
                        p.wb2, p.soc};
    ++g_sampleCount;

    if (g_sgCount == kMaxSamples) {
      g_sgHead = (g_sgHead + 1) % kMaxSamples;
      --g_sgCount;
    }
    const int sgIndex = (g_sgHead + g_sgCount) % kMaxSamples;
    g_sgSamples[sgIndex] = {timeMs, p.heat};
    ++g_sgCount;
  }

  g_haveSample = g_sampleCount > 0;
  if (g_sampleCount > 0) {
    rebuildPowerChart();
  }
}

void create(lv_obj_t *parent) {
  // The 24 h history and chart buffers go to PSRAM before any widget exists.
  // Sizes: 1442 samples x 32 B, 1442 SG states, seven 240-point series, the
  // SG band scratch copy and the chart image. If an allocation fails the
  // history stays empty and the chart falls back to LVGL's own widgets.
  if (g_samples == nullptr) {
    g_samples = (PowerSample *)allocLarge(sizeof(PowerSample) * kMaxSamples);
  }
  if (g_sgSamples == nullptr) {
    g_sgSamples = (SgSample *)allocLarge(sizeof(SgSample) * kMaxSamples);
  }
  if (g_sgBandTimes == nullptr) {
    g_sgBandTimes = (uint32_t *)allocLarge(sizeof(uint32_t) * kMaxSamples);
  }
  if (g_sgBandState == nullptr) {
    g_sgBandState = (int8_t *)allocLarge(sizeof(int8_t) * kMaxSamples);
  }
  if (g_chartData == nullptr) {
    g_chartData =
        (int32_t *)allocLarge(sizeof(int32_t) * kChartPoints * 7);
    if (g_chartData != nullptr) {
      g_chartGrid = g_chartData + 0 * kChartPoints;
      g_chartPv = g_chartData + 1 * kChartPoints;
      g_chartHouse = g_chartData + 2 * kChartPoints;
      g_chartBattery = g_chartData + 3 * kChartPoints;
      g_chartWb1 = g_chartData + 4 * kChartPoints;
      g_chartWb2 = g_chartData + 5 * kChartPoints;
      g_chartSoc = g_chartData + 6 * kChartPoints;
    }
  }
  if (g_chartPixels == nullptr) {
    g_chartPixels = (uint16_t *)allocLarge((size_t)kChartW * kChartH *
                                           sizeof(uint16_t));
  }
  prepareChartBackground();
  prepareBackdrop();

  lv_obj_t *screen = lv_obj_get_screen(parent);
  lv_obj_set_style_bg_color(screen, lv_color_hex(palette().bg), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  // All UI widgets live in one child of the screen. A theme switch cleans
  // this child and calls create() again, so every style is rebuilt from the
  // new palette while the Wi-Fi setup overlay outside it stays intact.
  if (g_root == nullptr) {
    g_root = lv_obj_create(parent);
    lv_obj_remove_style_all(g_root);
    lv_obj_set_pos(g_root, 0, 0);
    lv_obj_set_size(g_root, 800, 480);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(g_root, LV_OBJ_FLAG_SCROLLABLE);
  }
  lv_obj_t *setup[5] = {w.setupOverlay, w.setupQr, w.setupSsid,
                        w.setupPassword, w.setupUrl};
  delete w.sgBand;
  delete w.priceBand;
  w = Widgets{};
  w.setupOverlay = setup[0];
  w.setupQr = setup[1];
  w.setupSsid = setup[2];
  w.setupPassword = setup[3];
  w.setupUrl = setup[4];
  g_backgroundLabelCount = 0;
  g_renderedChargers = -1;
  g_renderedModeCount = -1;
  g_tapActive = false;
  if (!g_imageInitialized) {
    g_showBackgroundImage = true;
    g_imageInitialized = true;
  }
  parent = g_root;

  // Plain panel behind all pages, in the photo's exact geometry. The light
  // theme makes it visible; the dark theme matches the page background so it
  // does not stand out. Only the flow page paints its own image over it.
  w.backdrop = lv_obj_create(parent);
  lv_obj_remove_style_all(w.backdrop);
  lv_obj_set_pos(w.backdrop, 0, kHeaderH);
  lv_obj_set_size(w.backdrop, kBackdropW, kBackdropH);
  lv_obj_set_style_bg_color(w.backdrop,
                            lv_color_hex(palette().backdropPanel), 0);
  lv_obj_set_style_bg_opa(w.backdrop, LV_OPA_COVER, 0);
  lv_obj_clear_flag(w.backdrop, LV_OBJ_FLAG_SCROLLABLE);

  // Header ---------------------------------------------------------------
  label(parent, 16, 8, 0, F_TITLE, palette().text, TR("Home Energy Manager", "Home Energy Manager"));
  // Page captions
  w.caption1 = label(parent, 16, kCaptionY, 0, F_BODY, palette().muted, TR("Energie", "Energy"));
  w.caption2 = label(parent, 16, kCaptionY, 0, F_BODY, palette().muted, TR("Energiebilanz", "Energy balance"));
  w.caption3 = label(parent, 16, kCaptionY, 0, F_BODY, palette().muted, TR("Service", "Service"));
  w.caption4 = label(parent, 16, kCaptionY, 0, F_BODY, palette().muted, TR("Energiefluss", "Energy flow"));
  w.caption5 = label(parent, 16, kCaptionY, 0, F_BODY, palette().muted, TR("Wallboxen", "Wallboxes"));
  w.caption6 = label(parent, 16, kCaptionY, 0, F_BODY, palette().muted, TR("Heizung", "Heating"));
  lv_obj_add_flag(w.caption2, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(w.caption3, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(w.caption4, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(w.caption5, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(w.caption6, LV_OBJ_FLAG_HIDDEN);

  // One pill for the connection state. The WiFi glyph shows whether the
  // network is up; the text beside it shows whether data is arriving, so both
  // are visible together instead of replacing each other.
  w.statusChip = lv_obj_create(parent);
  lv_obj_remove_style_all(w.statusChip);
  lv_obj_set_pos(w.statusChip, 670, 14);
  lv_obj_set_size(w.statusChip, 115, 37);
  lv_obj_set_style_radius(w.statusChip, 18, 0);
  lv_obj_set_style_bg_color(w.statusChip, lv_color_hex(palette().card2), 0);
  lv_obj_set_style_bg_opa(w.statusChip, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(w.statusChip, 1, 0);
  lv_obj_set_style_border_color(w.statusChip, lv_color_hex(palette().line), 0);
  lv_obj_clear_flag(w.statusChip, LV_OBJ_FLAG_SCROLLABLE);

  w.statusWifi = label(w.statusChip, 10, 7, 24, F_BODY, palette().muted, LV_SYMBOL_WIFI);
  w.statusText = label(w.statusChip, 40, 10, 92, F_SMALL, palette().muted, TR("WLAN?", "WLAN?"));

  lv_obj_t *page1 = lv_obj_create(parent);
  lv_obj_remove_style_all(page1);
  lv_obj_set_pos(page1, 0, 0);
  lv_obj_set_size(page1, 790, 480);
  w.page1 = page1;

  // Grid ---------------------------------------------------------------
  lv_obj_t *grid = card(page1, 12, kHeaderH + kEnergyTop, 250, 176 - kCardSave);
  title(grid, TR("NETZ", "GRID"));
  w.gridTotal = label(grid, 16, 30, 0, F_BIG, palette().text, "--");
  w.gridDir = label(grid, 16, 76 - kCardSave, 0, F_SMALL, palette().muted, TR("Bezug", "Import"));
  {
    static const char *names[3] = {"L1", "L2", "L3"};
    for (int i = 0; i < 3; ++i) {
      char name[8];
      snprintf(name, sizeof(name), "%s", names[i]);
      row(grid, 16, 104 - kCardSave + i * 22, 218, name, &w.gridPhase[i]);
    }
  }

  // Household ----------------------------------------------------------
  lv_obj_t *house = card(page1, 274, kHeaderH + kEnergyTop, 250, 176 - kCardSave);
  title(house, TR("HAUS", "HOUSE"));
  w.houseTotal = label(house, 16, 30, 0, F_BIG, palette().text, "--");
  label(house, 16, 76 - kCardSave, 0, F_SMALL, palette().muted, TR("Verbrauch", "Consumption"));
  {
    static const char *names[3] = {"L1", "L2", "L3"};
    for (int i = 0; i < 3; ++i) {
      row(house, 16, 104 - kCardSave + i * 22, 218, names[i], &w.housePhase[i]);
    }
  }

  // Battery ------------------------------------------------------------
  lv_obj_t *bat = card(page1, 536, kHeaderH + kEnergyTop, 250, 176 - kCardSave);
  title(bat, TR("AKKU", "BATTERY"));
  w.batSoc = label(bat, 16, 28, 120, F_BIG, palette().text, "-- %");
  w.batDir = label(bat, 130, 42, 104, F_SMALL, palette().muted, "--",
                   LV_TEXT_ALIGN_RIGHT);
  w.batBar = lv_bar_create(bat);
  lv_obj_remove_style_all(w.batBar);
  lv_obj_set_pos(w.batBar, 16, 82 - kCardSave);
  lv_obj_set_size(w.batBar, 218, 12);
  lv_obj_set_style_radius(w.batBar, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(w.batBar, lv_color_hex(palette().card2), 0);
  lv_obj_set_style_bg_opa(w.batBar, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(w.batBar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(w.batBar, lv_color_hex(palette().green),
                            LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(w.batBar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_bar_set_range(w.batBar, 0, 100);
  lv_bar_set_value(w.batBar, 0, LV_ANIM_OFF);

  row(bat, 16, 104 - kCardSave, 218, TR("Leistung", "Power"), &w.batPower);
  row(bat, 16, 128 - kCardSave, 218, TR("Spannung", "Voltage"), &w.batVoltage);
  row(bat, 16, 152 - kCardSave, 218, TR("Strom", "Current"), &w.batCurrent);

  // PV -----------------------------------------------------------------
  lv_obj_t *pv = card(page1, 12,
                      kHeaderH + kEnergyTop + (176 - kCardSave) + kEnergyGap,
                      250, 166 - kCardSave);
  title(pv, TR("SOLAR", "SOLAR"));
  w.pvTotal = label(pv, 16, 30, 0, F_BIG, palette().green, "--");
  label(pv, 16, 76 - kCardSave, 0, F_SMALL, palette().muted, TR("Erzeugung", "Generation"));
  row(pv, 16, 108 - kCardSave, 218, "String A", &w.pvA);
  row(pv, 16, 134 - kCardSave, 218, "String B", &w.pvB);

  // Wallbox -----------------------------------------------------------
  lv_obj_t *wallbox = card(page1, 274,
                           kHeaderH + kEnergyTop + (176 - kCardSave) + kEnergyGap,
                           250, 166 - kCardSave);
  title(wallbox, TR("WALLBOX", "WALLBOX"));
  w.wallboxValue = label(wallbox, 16, 30, 0, F_BIG, palette().green, "--");
  w.wallboxNote = label(wallbox, 16, 76 - kCardSave, 0, F_SMALL, palette().muted, "--");
  w.wallboxMode = label(wallbox, 16, 96 - kCardSave, 0, F_SMALL, palette().muted, "--");
  w.wallboxPhase = label(wallbox, 16, 116 - kCardSave, 0, F_SMALL, palette().muted, "--");

  // Energy totals for today --------------------------------------------
  lv_obj_t *energy = card(page1, 536,
                          kHeaderH + kEnergyTop + (176 - kCardSave) + kEnergyGap,
                          250, 166 - kCardSave);
  title(energy, TR("ENERGIE HEUTE", "ENERGY TODAY"));
  // No large value to pull up here, so spread the 6 px saving across the six
  // rows: one pixel less between the title and each following line.
  row(energy, 16, 29, 218, TR("Erzeugung", "Generation"), &w.energyPv, F_SMALL);
  row(energy, 16, 50, 218, TR("Verbrauch", "Consumption"), &w.energyHouse, F_SMALL);
  row(energy, 16, 71, 218, TR("Netzbezug", "Grid import"), &w.energyImport, F_SMALL);
  row(energy, 16, 92, 218, TR("Einspeisung", "Feed-in"), &w.energyExport, F_SMALL);
  row(energy, 16, 113, 218, TR("Wallbox", "Wallbox"), &w.energyCharger, F_SMALL);
  row(energy, 16, 134, 218, TR("Akku", "Battery"), &w.energyBattery, F_SMALL);

  // Footer -------------------------------------------------------------
  // Second page (power graph) -----------------------------------------
  w.page2 = lv_obj_create(parent);
  lv_obj_remove_style_all(w.page2);
  lv_obj_set_pos(w.page2, 0, kHeaderH);
  lv_obj_set_size(w.page2, 790, 372);
  lv_obj_add_flag(w.page2, LV_OBJ_FLAG_HIDDEN);

  // Chart colours as on the panel's Energiebilanz page: load blue, PV
  // orange, grid green, battery red, wallboxes purple and brown.
  if (g_chartPixels != nullptr && g_chartData != nullptr) {
    // A pre-rendered image instead of two chart widgets. The page switch
    // then blits 766 x 234 pixels instead of rebuilding roughly 1700 line
    // draw tasks; renderChartImage() repaints the card and the series
    // whenever a new sample arrives.
    w.chartImage = lv_canvas_create(w.page2);
    lv_obj_remove_style_all(w.chartImage);
    lv_obj_set_pos(w.chartImage, 12, kChartDrop);
    lv_obj_set_size(w.chartImage, kChartW, kChartH);
    lv_canvas_set_buffer(w.chartImage, g_chartPixels, kChartW, kChartH,
                         LV_COLOR_FORMAT_RGB565);
    for (int i = 0; i < kChartPoints; ++i) {
      g_chartGrid[i] = LV_CHART_POINT_NONE;
      g_chartPv[i] = LV_CHART_POINT_NONE;
      g_chartHouse[i] = LV_CHART_POINT_NONE;
      g_chartBattery[i] = LV_CHART_POINT_NONE;
      g_chartWb1[i] = LV_CHART_POINT_NONE;
      g_chartWb2[i] = LV_CHART_POINT_NONE;
      g_chartSoc[i] = LV_CHART_POINT_NONE;
    }
    // Tap on the chart shows the sample values in the legend for 30 s.
    lv_obj_add_flag(w.chartImage, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(w.chartImage, chartTapEvent, LV_EVENT_CLICKED,
                        nullptr);
    renderChartImage();
  } else {
    w.powerChart = lv_chart_create(w.page2);
    lv_obj_set_pos(w.powerChart, 12, kChartDrop);
    lv_obj_set_size(w.powerChart, kChartW, kChartH);
    lv_chart_set_type(w.powerChart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(w.powerChart, kChartPoints);
    lv_chart_set_range(w.powerChart, LV_CHART_AXIS_PRIMARY_Y, -5000, 5000);
    lv_obj_set_style_bg_color(w.powerChart, lv_color_hex(palette().card), 0);
    lv_obj_set_style_line_width(w.powerChart, kChartLineWidth, LV_PART_ITEMS);
    // No point markers. The theme draws one rounded 2x2 rect per sample -
    // 240 of them per series, seven series - and each is its own draw task
    // with a radius mask. They sit inside the 2 px line and are invisible,
    // but on a full redraw they cost more than the lines themselves. Zero
    // size turns them off in draw_series_line().
    lv_obj_set_style_width(w.powerChart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(w.powerChart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(w.powerChart, 1, 0);
    lv_obj_set_style_border_color(w.powerChart, lv_color_hex(palette().line), 0);
    lv_obj_set_style_line_color(w.powerChart, lv_color_hex(palette().muted), 0);
    lv_obj_set_style_pad_all(w.powerChart, 0, 0);
    w.seriesGrid = lv_chart_add_series(w.powerChart, lv_color_hex(kSeriesColors[0]), LV_CHART_AXIS_PRIMARY_Y);
    w.seriesPv = lv_chart_add_series(w.powerChart, lv_color_hex(kSeriesColors[1]), LV_CHART_AXIS_PRIMARY_Y);
    w.seriesHouse = lv_chart_add_series(w.powerChart, lv_color_hex(kSeriesColors[2]), LV_CHART_AXIS_PRIMARY_Y);
    w.seriesBat = lv_chart_add_series(w.powerChart, lv_color_hex(kSeriesColors[3]), LV_CHART_AXIS_PRIMARY_Y);
    w.seriesWb1 = lv_chart_add_series(w.powerChart, lv_color_hex(kSeriesColors[4]), LV_CHART_AXIS_PRIMARY_Y);
    w.seriesWb2 = lv_chart_add_series(w.powerChart, lv_color_hex(kSeriesColors[5]), LV_CHART_AXIS_PRIMARY_Y);
    if (g_chartData != nullptr) {
      for (int i = 0; i < kChartPoints; ++i) {
        g_chartGrid[i] = LV_CHART_POINT_NONE;
        g_chartPv[i] = LV_CHART_POINT_NONE;
        g_chartHouse[i] = LV_CHART_POINT_NONE;
        g_chartBattery[i] = LV_CHART_POINT_NONE;
        g_chartWb1[i] = LV_CHART_POINT_NONE;
        g_chartWb2[i] = LV_CHART_POINT_NONE;
      }
      lv_chart_set_series_ext_y_array(w.powerChart, w.seriesGrid, g_chartGrid);
      lv_chart_set_series_ext_y_array(w.powerChart, w.seriesPv, g_chartPv);
      lv_chart_set_series_ext_y_array(w.powerChart, w.seriesHouse, g_chartHouse);
      lv_chart_set_series_ext_y_array(w.powerChart, w.seriesBat, g_chartBattery);
      lv_chart_set_series_ext_y_array(w.powerChart, w.seriesWb1, g_chartWb1);
      lv_chart_set_series_ext_y_array(w.powerChart, w.seriesWb2, g_chartWb2);
    }

    // Battery SOC on its own transparent overlay chart: same line width as
    // the power lines, but a light grey clearly lighter than the grid - it
    // is a percentage, not a power value. Same geometry as the power chart,
    // so both ranges line up; 0 % sits at the bottom edge, 100 % at the top,
    // independent of the power range.
    w.socChart = lv_chart_create(w.page2);
    lv_obj_set_pos(w.socChart, 12, kChartDrop);
    lv_obj_set_size(w.socChart, kChartW, kChartH);
    lv_chart_set_type(w.socChart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(w.socChart, kChartPoints);
    lv_chart_set_range(w.socChart, LV_CHART_AXIS_PRIMARY_Y, -5000, 5000);
    lv_obj_set_style_bg_opa(w.socChart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w.socChart, 0, 0);
    lv_obj_set_style_line_width(w.socChart, kChartLineWidth, LV_PART_ITEMS);
    lv_obj_set_style_width(w.socChart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(w.socChart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_opa(w.socChart, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(w.socChart, 0, 0);
    w.seriesSoc = lv_chart_add_series(w.socChart, lv_color_hex(kSeriesColors[6]), LV_CHART_AXIS_PRIMARY_Y);
    if (g_chartData != nullptr) {
      for (int i = 0; i < kChartPoints; ++i) {
        g_chartSoc[i] = LV_CHART_POINT_NONE;
      }
      lv_chart_set_series_ext_y_array(w.socChart, w.seriesSoc, g_chartSoc);
    }
    // Tap on the chart shows the sample values in the legend for 30 s.
    lv_obj_clear_flag(w.socChart, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(w.powerChart, chartTapEvent, LV_EVENT_CLICKED, nullptr);
  }

  // Cursor for a tapped sample: a vertical line at the tapped position, so
  // the values in the legend can be matched to a spot on the chart. A widget
  // of its own rather than part of the image, so showing and hiding it costs
  // a 1 x 234 px redraw instead of a repaint of the whole chart.
  w.tapLine = lv_obj_create(w.page2);
  lv_obj_remove_style_all(w.tapLine);
  lv_obj_set_pos(w.tapLine, 12, kChartDrop);
  lv_obj_set_size(w.tapLine, 1, kChartH);
  lv_obj_set_style_bg_color(w.tapLine, lv_color_hex(palette().text), 0);
  lv_obj_set_style_bg_opa(w.tapLine, LV_OPA_70, 0);
  lv_obj_clear_flag(w.tapLine, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(w.tapLine, LV_OBJ_FLAG_HIDDEN);

  // Y-scale labels last, so they draw over the chart. They sit at the left
  // edge of the card, where the plot is empty.
  w.scaleMax = label(w.page2, 20, 5 + kChartDrop, 60, F_SMALL, palette().muted, "--", LV_TEXT_ALIGN_LEFT, false);
  w.scaleMid = label(w.page2, 20, 116 + kChartDrop, 60, F_SMALL, palette().muted, "--", LV_TEXT_ALIGN_LEFT, false);
  w.scaleMin = label(w.page2, 20, 216, 60, F_SMALL, palette().muted, "--", LV_TEXT_ALIGN_LEFT, false);
  lv_obj_clear_flag(w.scaleMax, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(w.scaleMid, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(w.scaleMin, LV_OBJ_FLAG_CLICKABLE);

  // Day-ahead exchange price band over the next 24 h in its own card,
  // shown only when the dynamic tariff is enabled. Colours: grey = no
  // data, blue = medium, green = low, red = high, bright green = negative.
  w.priceCard = card(w.page2, 12, 289 - kCardSave + kChartDrop, 776, 64);
  {
    // Band and stats are children of page2 (not of the card): the card's
    // flex layout treats children as flow items and the band segments were
    // not rendered inside it.
    const char *const priceTicks[5] = {"0h", "6h", "12h", "18h", "24h"};
    w.priceBand = createHistoryBand(w.page2, 306 - kCardSave + kChartDrop, TR("Börsenpreis", "Exchange price"),
                                    0x999999, 0x007BFF, 0x2CA02C, 0xD62728, 0x999999,
                                    172, priceTicks, 0x00FF00);
  }
  setBandVisible(w.priceBand, false);
  w.priceStats =
      label(w.page2, 172, 332 - kCardSave + kChartDrop, 560, F_SMALL, palette().muted, "--");
  lv_obj_add_flag(w.priceStats, LV_OBJ_FLAG_HIDDEN);

  // Time labels at bottom (reversed: new values on right)
  const int xpos[5] = {8, 185, 375, 565, 755};
  for (int i = 0; i < 5; ++i) {
    w.timeLabel[i] = label(w.page2, xpos[i], 242 - kCardSave + kChartDrop, 40, F_SMALL, palette().muted,
                           "--", LV_TEXT_ALIGN_CENTER);
  }
  updateTimeLabels(3600);

  // Legend: the four meter series plus up to two wallboxes by name, in the
  // same colours as the chart lines.
  const char *names[7] = {TR("Netz", "Grid"), TR("PV", "PV"), TR("Verbrauch", "Consumption"), TR("Akku", "Battery"),
                          nullptr, nullptr, TR("Akku %", "Battery %")};
  lv_color_t colors[7];
  for (int i = 0; i < 7; ++i) {
    colors[i] = lv_color_hex(kSeriesColors[i]);
  }
  // Variable slot widths: "Verbrauch" and the wallbox names are long, so
  // they get wide slots (in the 11 px legend font their tapped values
  // "[1234 W]" fit on one line without wrapping).
  const int dotX[7] = {8, 108, 190, 318, 418, 558, 698};
  const int labelW[7] = {78, 66, 112, 78, 124, 124, 80};
  for (int i = 0; i < 7; ++i) {
    lv_obj_t *dot = lv_obj_create(w.page2);
    lv_obj_remove_style_all(dot);
    lv_obj_set_pos(dot, dotX[i], 268 - kCardSave + kChartDrop);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, colors[i], 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    if (i < 4 || i == 6) {
      w.legendLabel[i] = label(w.page2, dotX[i] + 12, 266 - kCardSave + kChartDrop, labelW[i], F_LEGEND,
                               palette().muted, names[i]);
    } else {
      w.wbLegendDot[i - 4] = dot;
      w.wbLegendLabel[i - 4] = label(w.page2, dotX[i] + 12, 266 - kCardSave + kChartDrop, labelW[i],
                                     F_LEGEND, palette().muted, "--");
      w.legendLabel[i] = w.wbLegendLabel[i - 4];
    }
  }

  // Third page (settings) ---------------------------------------------
  w.page3 = lv_obj_create(parent);
  lv_obj_remove_style_all(w.page3);
  lv_obj_set_pos(w.page3, 0, kHeaderH);
  lv_obj_set_size(w.page3, 790, 372);
  lv_obj_add_flag(w.page3, LV_OBJ_FLAG_HIDDEN);

  // Fourth page (energy flow) -----------------------------------------
  w.page4 = lv_obj_create(parent);
  lv_obj_remove_style_all(w.page4);
  lv_obj_set_pos(w.page4, 0, kHeaderH);
  lv_obj_set_size(w.page4, 800, 356);
  // The theme picks the photograph or the schematic; the image switch hides
  // either. With no image, the plain panel below shows through.
  applyFlowBackground();
  lv_obj_add_flag(w.page4, LV_OBJ_FLAG_HIDDEN);

  // Fifth page (wallbox configuration) --------------------------------
  // One card per charger. The page scrolls vertically when more boxes are
  // configured than fit on the screen.
  w.page5 = lv_obj_create(parent);
  lv_obj_remove_style_all(w.page5);
  lv_obj_set_pos(w.page5, 0, kHeaderH);
  lv_obj_set_size(w.page5, 790, 372);
  lv_obj_set_flex_flow(w.page5, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(w.page5, 8, 0);
  lv_obj_set_style_pad_row(w.page5, 8, 0);
  lv_obj_set_scroll_dir(w.page5, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(w.page5, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_add_flag(w.page5, LV_OBJ_FLAG_HIDDEN);

  // Heating page (SG-Ready + temperatures), style like the service page:
  // cards with a small muted title and body rows.
  w.page6 = lv_obj_create(parent);
  lv_obj_remove_style_all(w.page6);
  lv_obj_set_pos(w.page6, 0, kHeaderH);
  lv_obj_set_size(w.page6, 790, 372);
  lv_obj_add_flag(w.page6, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *sg = card(w.page6, 16, kHeatTop, 758, 164);
  title(sg, TR("SG-READY", "SG-READY"));
  row(sg, 16, 30, 720, "Status", &w.heatStatus);
  row(sg, 16, 52, 720, TR("Erweitert (SG2)", "Extended (SG2)"), &w.heatExtended);
  row(sg, 16, 74, 720, TR("Blockiert (SG1)", "Blocked (SG1)"), &w.heatBlocking);
  row(sg, 16, 96, 720, "§14a EnWG", &w.heatP14);
  // 24 h history band in the same card: grey = no data, blue = none active,
  // red = SG1 (blocking), green = SG2 (extended), orange = both (wrong state).
  {
    const char *const sgTicks[5] = {"24h", "18h", "12h", "6h", "0h"};
    w.sgBand = createHistoryBand(sg, 128, TR("Verlauf", "History"),
                                 0x999999, 0x007BFF, 0xD62728, 0x2CA02C, 0xFF7F0E,
                                 kBandX0, sgTicks);
  }

  lv_obj_t *party = card(w.page6, 16, kHeatTop + 164 + kHeatGap, 758, 52);
  title(party, TR("PARTYMODUS", "PARTY MODE"));
  label(party, 16, 30, 400, F_BODY, palette().muted, TR("2 Std. Override", "2 h override"));
  w.heatPartyBtn = lv_btn_create(party);
  lv_obj_set_pos(w.heatPartyBtn, 542, 6);
  lv_obj_set_size(w.heatPartyBtn, 200, 40);
  lv_obj_set_style_radius(w.heatPartyBtn, 9, 0);
  lv_obj_set_style_bg_color(w.heatPartyBtn, lv_color_hex(palette().card2), 0);
  lv_obj_set_style_bg_opa(w.heatPartyBtn, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(w.heatPartyBtn, 1, 0);
  lv_obj_set_style_border_color(w.heatPartyBtn, lv_color_hex(palette().line), 0);
  lv_obj_set_style_shadow_width(w.heatPartyBtn, 0, 0);
  lv_obj_t *partyLbl = lv_label_create(w.heatPartyBtn);
  lv_label_set_text(partyLbl, TR("Einschalten", "Turn on"));
  lv_obj_set_style_text_font(partyLbl, F_SMALL, 0);
  lv_obj_set_style_text_color(partyLbl, lv_color_hex(palette().text), 0);
  lv_obj_center(partyLbl);
  lv_obj_add_event_cb(w.heatPartyBtn, [](lv_event_t *) {
    if (g_heatingOverrideHandler != nullptr) {
      // Show the result immediately; the poll confirming it arrives a few
      // seconds later. The pending state expires after 15 s if it does not.
      g_partyPendingOn = !g_lastHeatingOverride;
      g_partyPending = true;
      g_partyPendingUntil = lv_tick_get() + 15000;
      g_heatingOverrideHandler(g_partyPendingOn);
    }
  }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *temps = card(w.page6, 16, kHeatTop + 164 + kHeatGap + 52 + kHeatGap,
                         758, 94);
  title(temps, TR("TEMPERATUR", "TEMPERATURE"));
  row(temps, 16, 28, 720, TR("Jetzt", "Now"), &w.tempNow);
  row(temps, 16, 48, 720, TR("Heute Min/Max", "Today min/max"), &w.tempToday);
  row(temps, 16, 68, 720, TR("Morgen Min/Max", "Tomorrow min/max"), &w.tempTomorrow);



  // Energy flow: four links around a central house hub. Links are drawn
  // first, particles next and cards last, so particles slide underneath the
  // nodes instead of crossing their text.
  flowLink(w.page4, 0, 200, 98, 340, 153, palette().solar);  // PV <-> house
  flowLink(w.page4, 1, 200, 254, 340, 199, palette().grid);   // grid <-> house
  flowLink(w.page4, 2, 600, 98, 460, 153, palette().battery);    // battery <-> house
  flowLink(w.page4, 3, 460, 199, 600, 254, palette().ev);     // house -> wallbox
  flowDots(w.page4, 0, palette().solarHi);
  flowDots(w.page4, 1, palette().gridHi);
  flowDots(w.page4, 2, palette().batteryHi);
  flowDots(w.page4, 3, palette().evHi);

  FlowNode pvFlow = flowNode(w.page4, 24, 16, TR("SOLAR", "SOLAR"), LV_SYMBOL_CHARGE,
                             palette().solar, palette().solarDim);
  w.pvNode = pvFlow.card;
  w.pvValue = pvFlow.value;
  w.pvState = pvFlow.state;

  FlowNode gridFlow = flowNode(w.page4, 24, 242, TR("NETZ", "GRID"), kFlowGridIcon,
                               palette().grid, palette().gridDim, &lv_font_mdi_icons_28);
  w.gridNode = gridFlow.card;
  w.gridValue = gridFlow.value;
  w.gridState = gridFlow.state;

  FlowNode batFlow = flowNode(w.page4, 600, 16, TR("AKKU", "BATTERY"),
                              LV_SYMBOL_BATTERY_FULL, palette().battery, palette().batteryDim);
  w.batNode = batFlow.card;
  w.batValue = batFlow.value;
  w.batState = batFlow.state;

  FlowNode wbFlow = flowNode(w.page4, 600, 242, TR("WALLBOX", "WALLBOX"), kFlowCarIcon,
                             palette().ev, palette().evDim, &lv_font_mdi_icons_28);
  w.wbNode = wbFlow.card;
  w.wbValue = wbFlow.value;
  w.wbState = wbFlow.state;

  // House hub in the middle: the one place everything flows to or from.
  w.hubNode = lv_obj_create(w.page4);
  lv_obj_remove_style_all(w.hubNode);
  lv_obj_set_pos(w.hubNode, 334, 110);
  lv_obj_set_size(w.hubNode, 132, 132);
  lv_obj_set_style_radius(w.hubNode, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(w.hubNode, lv_color_hex(palette().card2), 0);
  lv_obj_set_style_bg_opa(w.hubNode, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(w.hubNode, 2, 0);
  lv_obj_set_style_border_color(w.hubNode, lv_color_hex(palette().line), 0);
  lv_obj_clear_flag(w.hubNode, LV_OBJ_FLAG_SCROLLABLE);
  label(w.hubNode, 0, 16, 132, F_ICON, palette().text, LV_SYMBOL_HOME,
        LV_TEXT_ALIGN_CENTER);
  w.hubValue = label(w.hubNode, 0, 78, 132, F_MED, palette().text, "--",
                     LV_TEXT_ALIGN_CENTER);

  // Theme and image controls, styled like the network card below them.
  lv_obj_t *bg = card(w.page3, 16, kServiceTop, 758, 58);
  title(bg, "THEME");
  w.bgBtn = lv_btn_create(bg);
  lv_obj_set_pos(w.bgBtn, 542, 9);
  lv_obj_set_size(w.bgBtn, 200, 40);
  lv_obj_set_style_radius(w.bgBtn, 9, 0);
  lv_obj_set_style_bg_color(w.bgBtn, lv_color_hex(palette().buttonBg), 0);
  lv_obj_set_style_bg_opa(w.bgBtn, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(w.bgBtn, 1, 0);
  lv_obj_set_style_border_color(w.bgBtn, lv_color_hex(palette().line), 0);
  lv_obj_set_style_shadow_width(w.bgBtn, 0, 0);
  lv_obj_t *bgBtnLbl = lv_label_create(w.bgBtn);
  lv_label_set_text(bgBtnLbl, themeName(g_themeId, kEnglishUi));
  lv_obj_set_style_text_font(bgBtnLbl, F_BODY, 0);
  lv_obj_set_style_text_color(bgBtnLbl, lv_color_hex(palette().buttonText), 0);
  lv_obj_center(bgBtnLbl);

  // Flow page background image on/off, same button style, left of the theme
  // cycle button. The theme decides whether the image is a photo or schematic.
  lv_obj_t *bgImgBtn = lv_btn_create(bg);
  lv_obj_set_pos(bgImgBtn, 330, 9);
  lv_obj_set_size(bgImgBtn, 200, 40);
  lv_obj_set_style_radius(bgImgBtn, 9, 0);
  lv_obj_set_style_bg_color(bgImgBtn, lv_color_hex(palette().buttonBg), 0);
  lv_obj_set_style_bg_opa(bgImgBtn, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(bgImgBtn, 1, 0);
  lv_obj_set_style_border_color(bgImgBtn, lv_color_hex(palette().line), 0);
  lv_obj_set_style_shadow_width(bgImgBtn, 0, 0);
  lv_obj_t *bgImgBtnLbl = lv_label_create(bgImgBtn);
  lv_label_set_text(bgImgBtnLbl,
                    g_showBackgroundImage ? TR("Bild: an", "Image: on")
                                          : TR("Bild: aus", "Image: off"));
  lv_obj_set_style_text_font(bgImgBtnLbl, F_BODY, 0);
  lv_obj_set_style_text_color(bgImgBtnLbl,
                              lv_color_hex(palette().buttonText), 0);
  lv_obj_center(bgImgBtnLbl);
  lv_obj_add_event_cb(bgImgBtn, [](lv_event_t *e) {
    g_showBackgroundImage = !g_showBackgroundImage;
    applyFlowBackground();
    lv_label_set_text(
        lv_obj_get_child(static_cast<lv_obj_t *>(lv_event_get_target(e)), 0),
        g_showBackgroundImage ? TR("Bild: an", "Image: on")
                              : TR("Bild: aus", "Image: off"));
  }, LV_EVENT_CLICKED, nullptr);

  // The callback must not touch the button: setTheme() rebuilds the whole
  // widget tree, including this button and its label.
  lv_obj_add_event_cb(w.bgBtn, [](lv_event_t *) {
    setTheme(themeId(((int)currentTheme() + 1) % kThemeCount));
  }, LV_EVENT_CLICKED, nullptr);

  // Network state. The setup portal stays reachable on the local network, so
  // this card is also the pointer to it when the WARP address has to change.
  lv_obj_t *net = card(w.page3, 16, kServiceTop + 58 + kServiceGap, 370, 190);
  title(net, TR("NETZWERK", "NETWORK"));
  w.netSsid = label(net, 16, 34, 338, F_BODY, palette().text, "WLAN: --");
  w.netIp = label(net, 16, 58, 338, F_BODY, palette().text, "IP: --");
  w.netWarp = label(net, 16, 82, 338, F_BODY, palette().text, "WARP EM: --");
  w.netWeb = label(net, 16, 106, 338, F_BODY, palette().accent,
                   "Webinterface: --");
  w.netSd = label(net, 16, 130, 338, F_SMALL, palette().muted, "SD: --");
  label(net, 16, 152, 338, F_SMALL, palette().muted,
        TR("Zum Ändern die Web-Adresse im Browser öffnen.", "Open the web address in your browser to change it."));

  // Energy counters since commissioning, next to the network card.
  lv_obj_t *z = card(w.page3, 402, kServiceTop + 58 + kServiceGap, 372, 190);
  title(z, TR("ZÄHLER", "METERS"));
  row(z, 16, 30, 340, TR("Erzeugung", "Generation"), &w.zPv, F_SMALL);
  row(z, 16, 52, 340, TR("Verbrauch", "Consumption"), &w.zHouse, F_SMALL);
  row(z, 16, 74, 340, TR("Netzbezug", "Grid import"), &w.zImport, F_SMALL);
  row(z, 16, 96, 340, TR("Einspeisung", "Feed-in"), &w.zExport, F_SMALL);
  row(z, 16, 118, 340, TR("Wallbox", "Wallbox"), &w.zCharger, F_SMALL);
  row(z, 16, 140, 340, TR("Akku", "Battery"), &w.zBattery, F_SMALL);
  label(z, 16, 164, 340, F_SMALL, palette().muted, TR("seit Inbetriebnahme", "since commissioning"),
        LV_TEXT_ALIGN_RIGHT);

  // Digital IO states: 4 inputs, 2 relays, 2 SG-Ready outputs, each with a
  // dot below its label.
  lv_obj_t *io = card(w.page3, 16,
                      kServiceTop + 58 + kServiceGap + 190 + kServiceGap,
                      758, 80);
  title(io, TR("I/O STATUS", "I/O STATUS"));
  const char *ioNames[8] = {TR("Eingang 1", "Input 1"), TR("Eingang 2", "Input 2"), TR("Eingang 3", "Input 3"),
                            TR("Eingang 4", "Input 4"), TR("Relais 1", "Relay 1"), TR("Relais 2", "Relay 2"), TR("SG 1", "SG 1"),
                            TR("SG 2", "SG 2")};
  for (int i = 0; i < 8; ++i) {
    label(io, 5 + i * 95, 30, 90, F_SMALL, palette().muted, ioNames[i],
          LV_TEXT_ALIGN_CENTER);
    lv_obj_t *dot = lv_obj_create(io);
    lv_obj_remove_style_all(dot);
    lv_obj_set_pos(dot, 44 + i * 95, 50);
    lv_obj_set_size(dot, 12, 12);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(palette().muted), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    w.ioDot[i] = dot;
  }

  // Bottom bar, left of the page buttons: today's and overall autarky, plus
  // today's solar forecast as remaining / total kWh.
  label(parent, 16, 443, 0, F_SMALL, palette().muted, TR("Autarkie", "Independence"));
#ifdef WARP_LANG_EN
  w.autarkyValue = label(parent, 130, 439, 160, F_TITLE, palette().text, "--");
  label(parent, 270, 443, 0, F_SMALL, palette().muted, TR("Prognose", "Forecast"));
  w.forecastValue = label(parent, 340, 439, 150, F_TITLE, palette().text, "--");
#else
  w.autarkyValue = label(parent, 84, 439, 160, F_TITLE, palette().text, "--");
  label(parent, 234, 443, 0, F_SMALL, palette().muted, TR("Prognose", "Forecast"));
  w.forecastValue = label(parent, 318, 439, 160, F_TITLE, palette().text, "--");
#endif

  auto makeNav = [](lv_obj_t *p, int x, int y, const char *text) {
    lv_obj_t *b = lv_btn_create(p);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, 42, 38);
    lv_obj_set_style_radius(b, 9, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(palette().card2), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(palette().line), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *lbl = lv_label_create(b);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_mdi_icons_28, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(palette().muted), 0);
    lv_obj_center(lbl);
    return b;
  };

  w.pageBtn1 = makeNav(parent, 500, 434, kNavFlowIcon);
  w.pageBtn2 = makeNav(parent, 548, 434, kNavEnergyIcon);
  w.pageBtn3 = makeNav(parent, 596, 434, kNavChartIcon);
  w.pageBtn4 = makeNav(parent, 644, 434, kFlowCarIcon);
  w.pageBtn5 = makeNav(parent, 692, 434, kNavHeatIcon);
  w.pageBtn6 = makeNav(parent, 740, 434, kNavServiceIcon);
  lv_obj_add_event_cb(w.pageBtn1, [](lv_event_t *) { showPage(1); },
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(w.pageBtn2, [](lv_event_t *) { showPage(2); },
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(w.pageBtn3, [](lv_event_t *) { showPage(3); },
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(w.pageBtn4, [](lv_event_t *) { showPage(4); },
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(w.pageBtn5, [](lv_event_t *) { showPage(5); },
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(w.pageBtn6, [](lv_event_t *) { showPage(6); },
                      LV_EVENT_CLICKED, nullptr);
  showPage(1);

  // create() also runs on a theme switch. Drop the old timer first: two active
  // timers would advance the same phases twice and visibly double the speed.
  if (g_flowTimer != nullptr) {
    lv_timer_delete(g_flowTimer);
    g_flowTimer = nullptr;
  }

  // Particle animation. 15 fps: small 10 px objects, no opacity layers and no
  // full-page invalidation. Speed scales with power, so a strong flow visibly
  // moves faster without ever exceeding the CPU budget.
  //
  // Measured on the 7-inch panel at 5 fps: one tick is one LVGL pass, and a
  // pass costs ~8 ms whether its three strips cover 300 px or 12,000 - the cost
  // is the pass, not the area, so the tick rate is what the budget buys. 10 fps
  // was ~80 ms of a second and used the step the 5 fps build had, halved, so
  // the dots kept their speed and only moved in smaller increments. The step
  // stays where it is again, and at 15 fps (~120 ms of a second) it now arrives
  // half again as often: a dot crosses a link in 2.2 s at full power instead of
  // 3.3 s, and in 7.4 s instead of 11.1 s when the flow is weak. The per-tick
  // jump is unchanged, so nothing gets choppier.
  g_flowTimer = lv_timer_create([](lv_timer_t *) {
    if (w.page4 == nullptr ||
        lv_obj_has_flag(w.page4, LV_OBJ_FLAG_HIDDEN)) {
      return;
    }

    static const float fullScale[4] = {5000.0f, 5000.0f, 5000.0f, 11000.0f};
    for (int i = 0; i < 4; ++i) {
      if (!w.flowActive[i]) {
        for (int d = 0; d < 2; ++d) {
          if (w.flowDot[i][d] != nullptr) {
            lv_obj_add_flag(w.flowDot[i][d], LV_OBJ_FLAG_HIDDEN);
          }
        }
        continue;
      }

      float norm = std::fabs(w.flowPower[i]) / fullScale[i];
      if (norm > 1.0f) {
        norm = 1.0f;
      }
      const int step = 9 + static_cast<int>(21.0f * norm);
      w.flowPhase[i] = (w.flowPhase[i] + step) % 1000;

      const bool reverse =
          (i == 1 && w.gridExporting) || (i == 2 && w.batCharging);
      const int x1 = static_cast<int>(w.flowPts[i][0].x);
      const int y1 = static_cast<int>(w.flowPts[i][0].y);
      const int x2 = static_cast<int>(w.flowPts[i][1].x);
      const int y2 = static_cast<int>(w.flowPts[i][1].y);

      for (int d = 0; d < 2; ++d) {
        lv_obj_t *dot = w.flowDot[i][d];
        if (dot == nullptr) {
          continue;
        }
        int phase = (w.flowPhase[i] + d * 500) % 1000;
        float s = phase / 1000.0f;
        if (reverse) {
          s = 1.0f - s;
        }
        const int x = static_cast<int>(x1 + (x2 - x1) * s + 0.5f) - 5;
        const int y = static_cast<int>(y1 + (y2 - y1) * s + 0.5f) - 5;
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(dot, x, y);
      }
    }
  }, 66, nullptr);
}

void setPage(int page) {
  if (page < 1) {
    page = 1;
  }
  if (page > 6) {
    page = 6;
  }
  showPage(page);
}

void setChargeModeHandler(ChargeModeHandler handler) {
  g_chargeModeHandler = handler;
}

void setHeatingOverrideHandler(HeatingOverrideHandler handler) {
  g_heatingOverrideHandler = handler;
}

void showSetup(const char *ssid, const char *password, const char *url) {
  if (w.setupOverlay == nullptr) {
    lv_obj_t *overlay = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_size(overlay, 800, 480);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(palette().bg), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    w.setupOverlay = overlay;

    label(overlay, 40, 22, 0, F_BIG, palette().text, TR("WLAN einrichten", "Set up WLAN"));
    label(overlay, 40, 66, 0, F_BODY, palette().muted,
          TR("Mit dem HEMS-Panel verbinden", "Connect to the HEMS panel"));

    // White quiet zone around the QR code; the code itself is 260 px so the
    // camera can lock on from a normal arm's length.
    lv_obj_t *qrCard = lv_obj_create(overlay);
    lv_obj_remove_style_all(qrCard);
    lv_obj_set_pos(qrCard, 40, 110);
    lv_obj_set_size(qrCard, 300, 300);
    lv_obj_set_style_radius(qrCard, 18, 0);
    lv_obj_set_style_bg_color(qrCard, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(qrCard, LV_OPA_COVER, 0);
    lv_obj_clear_flag(qrCard, LV_OBJ_FLAG_SCROLLABLE);

    w.setupQr = lv_qrcode_create(qrCard);
    lv_qrcode_set_size(w.setupQr, 260);
    lv_qrcode_set_dark_color(w.setupQr, lv_color_hex(0x000000));
    lv_qrcode_set_light_color(w.setupQr, lv_color_hex(0xFFFFFF));
    lv_obj_set_pos(w.setupQr, 20, 20);

    label(overlay, 380, 112, 0, F_SMALL, palette().muted, TR("NETZWERK", "NETWORK"));
    w.setupSsid = label(overlay, 380, 134, 380, F_TITLE, palette().text, "--");
    label(overlay, 380, 174, 0, F_SMALL, palette().muted, TR("PASSWORT", "PASSWORD"));
    w.setupPassword = label(overlay, 380, 196, 380, F_TITLE, palette().text, "--");
    label(overlay, 380, 240, 0, F_SMALL, palette().muted, TR("DANACH ÖFFNEN", "OPEN AFTERWARDS"));
    w.setupUrl = label(overlay, 380, 262, 380, F_TITLE, palette().accent, "--");
    label(overlay, 380, 308, 380, F_SMALL, palette().muted,
          TR("QR-Code scannen oder dem Netzwerk beitreten. Im Browser WLAN-Zugang und die Adresse des WARP Energy Managers eintragen.", "Scan the QR code or join the network. In the browser enter the WLAN credentials and the WARP Energy Manager address."));
    label(overlay, 40, 432, 720, F_SMALL, palette().muted,
          TR("Das Einrichtungsnetz ist offen und bleibt aktiv, bis die Einrichtung abgeschlossen ist.", "The setup network is open and stays active until the setup is complete."));
  }

  lv_label_set_text(w.setupSsid, ssid != nullptr ? ssid : "--");
  if (password != nullptr && password[0] != '\0') {
    lv_label_set_text(w.setupPassword, password);
  } else {
    lv_label_set_text(w.setupPassword, TR("offen", "open"));
  }
  lv_label_set_text(w.setupUrl, url != nullptr ? url : "--");

  char ssidEsc[64];
  char passEsc[64];
  char payload[192];
  qrEscape(ssidEsc, sizeof(ssidEsc), ssid != nullptr ? ssid : "");
  qrEscape(passEsc, sizeof(passEsc), password != nullptr ? password : "");
  if (passEsc[0] != '\0') {
    snprintf(payload, sizeof(payload), "WIFI:T:WPA;S:%s;P:%s;;", ssidEsc,
             passEsc);
  } else {
    snprintf(payload, sizeof(payload), "WIFI:T:nopass;S:%s;;", ssidEsc);
  }
  lv_qrcode_update(w.setupQr, payload, strlen(payload));

  lv_obj_clear_flag(w.setupOverlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(w.setupOverlay);
}

void hideSetup() {
  if (w.setupOverlay != nullptr) {
    lv_obj_add_flag(w.setupOverlay, LV_OBJ_FLAG_HIDDEN);
  }
}

void setNetworkInfo(const char *ssid, const char *ip, const char *warpUrl,
                    const char *webUrl) {
  char buf[192];
  snprintf(buf, sizeof(buf), "WLAN: %s",
           (ssid != nullptr && ssid[0] != '\0') ? ssid : "--");
  setTextIfChanged(w.netSsid, buf);
  snprintf(buf, sizeof(buf), "IP: %s",
           (ip != nullptr && ip[0] != '\0') ? ip : "--");
  setTextIfChanged(w.netIp, buf);
  // The service page shows the WARP address without the transport scheme,
  // even though the panel always talks to it over HTTP.
  const char *warpDisplay = warpUrl;
  if (warpDisplay != nullptr && strncmp(warpDisplay, "http://", 7) == 0) {
    warpDisplay += 7;
  } else if (warpDisplay != nullptr &&
             strncmp(warpDisplay, "https://", 8) == 0) {
    warpDisplay += 8;
  }
  snprintf(buf, sizeof(buf), "WARP EM: %s",
           (warpDisplay != nullptr && warpDisplay[0] != '\0') ? warpDisplay
                                                              : "--");
  setTextIfChanged(w.netWarp, buf);
  snprintf(buf, sizeof(buf), "Webinterface: %s",
           (webUrl != nullptr && webUrl[0] != '\0') ? webUrl : "--");
  setTextIfChanged(w.netWeb, buf);
}

void setSdStatus(const char *text) {
  setTextIfChanged(w.netSd, text != nullptr ? text : "SD: --");
}

void setConnectionState(ConnectionState state) {
  g_connectionState = state;
  const char *text = TR("WLAN?", "WLAN?");
  uint32_t textColor = palette().muted;
  uint32_t wifiColor = palette().muted;
  uint32_t background = palette().card2;

  switch (state) {
    case ConnectionState::NoNet:
      text = TR("WLAN?", "WLAN?");
      textColor = palette().muted;
      wifiColor = palette().muted;
      background = palette().card2;
      break;
    case ConnectionState::Disconnected:
      // WLAN is up (green glyph) but the WARP does not answer (red text).
      text = TR("Getrennt", "Disconnected");
      textColor = palette().red;
      wifiColor = palette().green;
      background = palette().chipDisconnected;
      break;
    case ConnectionState::Data:
      text = TR("Daten", "Data");
      textColor = palette().green;
      wifiColor = palette().green;
      background = palette().chipData;
      break;
    case ConnectionState::NoData:
      // WLAN is up and the WARP answers, but it returns no measurements.
      text = TR("Daten", "Data");
      textColor = palette().red;
      wifiColor = palette().green;
      background = palette().chipNoData;
      break;
  }

  setText(w.statusText, text);
  setColor(w.statusText, textColor);
  if (w.statusWifi != nullptr) {
    lv_obj_set_style_text_color(w.statusWifi, lv_color_hex(wifiColor), 0);
  }
  if (w.statusChip != nullptr) {
    lv_obj_set_style_bg_color(w.statusChip, lv_color_hex(background), 0);
  }
}

void update(const WarpData &d) {
  g_lastData = d;
  char buf[48];
  char buf2[64];
  char chargeBuf[32];

  // Bottom-bar statistics --------------------------------------------
  if (w.autarkyValue != nullptr) {
    if (d.hasAutarky && d.hasAutarkyToday) {
      snprintf(buf, sizeof(buf), "%.0f %% / %.0f %%", d.autarkyToday,
               d.autarky);
      setTextIfChanged(w.autarkyValue, buf);
      setColorIfChanged(w.autarkyValue, palette().text);
    } else if (d.hasAutarky) {
      snprintf(buf, sizeof(buf), "%.0f %%", d.autarky);
      setTextIfChanged(w.autarkyValue, buf);
      setColorIfChanged(w.autarkyValue, palette().text);
    } else {
      setTextIfChanged(w.autarkyValue, "--");
      setColorIfChanged(w.autarkyValue, palette().muted);
    }
  }
  if (w.forecastValue != nullptr) {
    if (d.hasForecast) {
      // Remaining forecast first, then the whole-day forecast.
      char remaining[16];
      char total[16];
      fmtForecastKwh(total, sizeof(total), d.forecastTodayWh / 1000.0f);
      if (d.hasForecastRemaining) {
        fmtForecastKwh(remaining, sizeof(remaining),
                       d.forecastTodayRemainingWh / 1000.0f);
        snprintf(buf, sizeof(buf), "%s / %s kWh", remaining, total);
      } else {
        snprintf(buf, sizeof(buf), "-- / %s kWh", total);
      }
      setTextIfChanged(w.forecastValue, buf);
      setColorIfChanged(w.forecastValue, palette().text);
    } else {
      setTextIfChanged(w.forecastValue, "--");
      setColorIfChanged(w.forecastValue, palette().muted);
    }
  }

  // Grid ---------------------------------------------------------------
  if (w.gridTotal != nullptr) {
    if (d.hasGrid) {
      fmtPower(buf, sizeof(buf), std::fabs(d.gridPower));
      snprintf(buf2, sizeof(buf2), "%s%s", d.gridPower < 0.0 ? "-" : "", buf);
      setTextIfChanged(w.gridTotal, buf2);
      setColorIfChanged(w.gridTotal, d.gridPower < 0.0 ? palette().green : palette().amber);
    } else {
      setTextIfChanged(w.gridTotal, "--");
      setColorIfChanged(w.gridTotal, palette().muted);
    }
  }
  setTextIfChanged(w.gridDir, !d.hasGrid ? TR("kein Messwert", "no measurement")
                                : (d.gridPower < 0.0 ? TR("Einspeisung", "Feed-in") : TR("Bezug", "Import")));
  {
    const float phases[3] = {d.gridL1, d.gridL2, d.gridL3};
    for (int i = 0; i < 3; ++i) {
      if (!d.hasGrid) {
        setTextIfChanged(w.gridPhase[i], "--");
        setColorIfChanged(w.gridPhase[i], palette().muted);
        continue;
      }
      fmtSignedPower(buf, sizeof(buf), phases[i]);
      setTextIfChanged(w.gridPhase[i], buf);
      setColorIfChanged(w.gridPhase[i],
               phases[i] < 0.0 ? palette().green : (phases[i] > 0.0 ? palette().amber : palette().text));
    }
  }

  // Household ----------------------------------------------------------
  if (w.houseTotal != nullptr) {
    if (d.hasHouse) {
      fmtPower(buf, sizeof(buf), d.housePower);
      setTextIfChanged(w.houseTotal, buf);
      setColorIfChanged(w.houseTotal, palette().text);
    } else {
      setTextIfChanged(w.houseTotal, "--");
      setColorIfChanged(w.houseTotal, palette().muted);
    }
  }
  {
    const float phases[3] = {d.houseL1, d.houseL2, d.houseL3};
    for (int i = 0; i < 3; ++i) {
      if (!d.hasHouse) {
        setTextIfChanged(w.housePhase[i], "--");
        continue;
      }
      fmtPower(buf, sizeof(buf), phases[i]);
      setTextIfChanged(w.housePhase[i], buf);
    }
  }

  // PV -----------------------------------------------------------------
  if (w.pvTotal != nullptr) {
    if (d.hasPv) {
      fmtPower(buf, sizeof(buf), d.pvPower);
      setTextIfChanged(w.pvTotal, buf);
      setColorIfChanged(w.pvTotal, palette().green);
    } else {
      setTextIfChanged(w.pvTotal, "--");
      setColorIfChanged(w.pvTotal, palette().muted);
    }
  }
  if (d.hasPvStrings) {
    fmtPower(buf, sizeof(buf), d.pvA);
    setTextIfChanged(w.pvA, buf);
    fmtPower(buf, sizeof(buf), d.pvB);
    setTextIfChanged(w.pvB, buf);
  } else {
    setTextIfChanged(w.pvA, "--");
    setTextIfChanged(w.pvB, "--");
  }

  // Wallbox -----------------------------------------------------------
  /* CAS states as WARP names them: 0 no vehicle, 1/2 waiting for release,
   * 3 ready to charge, 4 charging, 5 error, 6 finished, 7 blocked. The power
   * is either read from a wallbox meter or calculated from the allocated
   * current and the phase voltages. The state decides what the note says:
   * naming every state after itself keeps "ready to charge" and "error"
   * visible instead of folding them into "charging" or "ready". */
  if (d.hasCharger) {
    if (d.charge_power > 0.1f || d.charge_state == 4) {
      fmtPower(chargeBuf, sizeof(chargeBuf), d.charge_power);
      setTextIfChanged(w.wallboxValue, chargeBuf);
    } else {
      setTextIfChanged(w.wallboxValue, "--");
    }
    if (d.charge_state == 4) {
      setTextIfChanged(w.wallboxNote,
                       d.chargeEstimated
                           ? TR("Laden (berechnet)", "Charging (calculated)")
                           : TR("Laden", "Charging"));
    } else {
      setTextIfChanged(w.wallboxNote, casStateName(d.charge_state));
    }
  } else {
    setTextIfChanged(w.wallboxValue, "--");
    setTextIfChanged(w.wallboxNote, TR("Getrennt", "Disconnected"));
  }
  const int overviewMode =
      d.charger_count > 0 ? d.chargers[0].mode : d.charge_mode;
  snprintf(chargeBuf, sizeof(chargeBuf), TR("Modus: %s", "Mode: %s"),
           chargeModeName(overviewMode));
  setTextIfChanged(w.wallboxMode, chargeBuf);
  if (d.hasCharger && d.charge_phases > 0) {
    snprintf(chargeBuf, sizeof(chargeBuf), TR("Phase: %s", "Phase: %s"),
             d.charge_phases >= 3 ? "3-phasig" : "1-phasig");
  } else {
    snprintf(chargeBuf, sizeof(chargeBuf), TR("Phase: --", "Phase: --"));
  }
  setTextIfChanged(w.wallboxPhase, chargeBuf);
  if (w.batSoc != nullptr) {
    if (d.hasBattery) {
      snprintf(buf, sizeof(buf), "%.0f %%", d.soc);
      setTextIfChanged(w.batSoc, buf);
      setColorIfChanged(w.batSoc, palette().text);
      if (w.batBar != nullptr) lv_bar_set_value(w.batBar, (int32_t)(d.soc + 0.5f), LV_ANIM_OFF);
    } else {
      setTextIfChanged(w.batSoc, "-- %");
      setColorIfChanged(w.batSoc, palette().muted);
      if (w.batBar != nullptr) lv_bar_set_value(w.batBar, 0, LV_ANIM_OFF);
    }
  }
  if (w.batDir != nullptr) {
    if (!d.hasBattery) {
      setTextIfChanged(w.batDir, "--");
      setColorIfChanged(w.batDir, palette().muted);
    } else if (d.batteryPower > 1.0f) {
      setTextIfChanged(w.batDir, TR("laden", "charging"));
      setColorIfChanged(w.batDir, palette().green);
    } else if (d.batteryPower < -1.0f) {
      setTextIfChanged(w.batDir, TR("entladen", "discharging"));
      setColorIfChanged(w.batDir, palette().amber);
    } else {
      setTextIfChanged(w.batDir, "inaktiv");
      setColorIfChanged(w.batDir, palette().muted);
    }
  }
  if (d.hasBattery) {
    const bool charging = d.batteryPower >= 0.0f;
    fmtSignedPower(buf, sizeof(buf), d.batteryPower);
    setTextIfChanged(w.batPower, buf);
    setColorIfChanged(w.batPower, charging ? palette().green : palette().amber);

    snprintf(buf, sizeof(buf), "%.1f V", d.batteryVoltage);
    setTextIfChanged(w.batVoltage, buf);
    snprintf(buf, sizeof(buf), "%.1f A", d.batteryCurrent);
    setTextIfChanged(w.batCurrent, buf);
  } else {
    setTextIfChanged(w.batPower, "--");
    setTextIfChanged(w.batVoltage, "--");
    setTextIfChanged(w.batCurrent, "--");
  }

  // Energy totals for today (page 2) -----------------------------------
  if (d.hasDaySums) {
    fmtEnergy(buf, sizeof(buf), d.dayGenerationWh);
    setTextIfChanged(w.energyPv, buf);
    fmtEnergy(buf, sizeof(buf), d.dayLoadWh);
    setTextIfChanged(w.energyHouse, buf);
    fmtEnergy(buf, sizeof(buf), d.dayGridDrawWh);
    setTextIfChanged(w.energyImport, buf);
    fmtEnergy(buf, sizeof(buf), d.dayFeedInWh);
    setTextIfChanged(w.energyExport, buf);
  } else {
    setTextIfChanged(w.energyPv, "--");
    setTextIfChanged(w.energyHouse, "--");
    setTextIfChanged(w.energyImport, "--");
    setTextIfChanged(w.energyExport, "--");
  }
  if (d.hasDayCharger) {
    fmtEnergy(buf, sizeof(buf), d.dayChargerWh);
    setTextIfChanged(w.energyCharger, buf);
  } else {
    setTextIfChanged(w.energyCharger, "--");
  }
  if (d.hasDayBattery) {
    char charge[24];
    char discharge[24];
    fmtEnergy(charge, sizeof(charge), d.dayBatteryChargeWh);
    fmtEnergy(discharge, sizeof(discharge), d.dayBatteryDischargeWh);
    snprintf(buf2, sizeof(buf2), "%s / %s", charge, discharge);
    setTextIfChanged(w.energyBattery, buf2);
  } else {
    setTextIfChanged(w.energyBattery, "--");
  }

  // Energy counters since commissioning (ZÄHLER card, service page) -----
  fmtEnergy(buf, sizeof(buf), d.pvEnergyWh);
  setTextIfChanged(w.zPv, buf);
  fmtEnergy(buf, sizeof(buf), d.houseEnergyWh);
  setTextIfChanged(w.zHouse, buf);
  fmtEnergy(buf, sizeof(buf), d.gridImportWh);
  setTextIfChanged(w.zImport, buf);
  fmtEnergy(buf, sizeof(buf), d.gridExportWh);
  setTextIfChanged(w.zExport, buf);
  if (d.hasChargerEnergy) {
    fmtEnergy(buf, sizeof(buf), d.chargerEnergyWh);
    setTextIfChanged(w.zCharger, buf);
  } else {
    setTextIfChanged(w.zCharger, "--");
  }

  {
    char charge[24];
    char discharge[24];
    char combined[64];
    if (d.batteryChargeWh > 0.0f || d.batteryDischargeWh > 0.0f) {
      fmtEnergy(charge, sizeof(charge), d.batteryChargeWh);
      fmtEnergy(discharge, sizeof(discharge), d.batteryDischargeWh);
      snprintf(combined, sizeof(combined), "%s / %s", charge, discharge);
    } else {
      snprintf(combined, sizeof(combined), "--");
    }
    setTextIfChanged(w.zBattery, combined);
  }

  // Power chart --------------------------------------------------------
  if (w.powerChart != nullptr || w.chartImage != nullptr) {
    const uint32_t now = histNowMs();
    const bool fresh = !g_haveSample || d.lastUpdateMs == 0 ||
                       d.lastUpdateMs != g_lastDataUpdateMs;
    const bool due = !g_haveSample ||
                     (uint32_t)(now - g_lastSampleMs) >= kSampleIntervalMs;
    if (fresh && due) {
      recordPowerSample(now, d);
      g_lastDataUpdateMs = d.lastUpdateMs;
      g_lastSampleMs = now;
      g_haveSample = true;
      rebuildPowerChart();
    }
    // Day-ahead exchange price band: next 24 h from now, one state per
    // price slot. Negative = below zero, low = below the midpoint between
    // median and minimum, high = above the midpoint between median and
    // maximum.
    const bool page2Visible =
        w.page2 != nullptr && !lv_obj_has_flag(w.page2, LV_OBJ_FLAG_HIDDEN);
    if (w.priceBand != nullptr) {
      const bool show = d.pricesEnabled && d.hasPrices && d.nowMin > 0;
      setBandVisible(w.priceBand, show);
      if (w.priceCard != nullptr) {
        if (show) {
          lv_obj_clear_flag(w.priceCard, LV_OBJ_FLAG_HIDDEN);
        } else {
          lv_obj_add_flag(w.priceCard, LV_OBJ_FLAG_HIDDEN);
        }
      }
      if (w.priceStats != nullptr) {
        if (show) {
          lv_obj_clear_flag(w.priceStats, LV_OBJ_FLAG_HIDDEN);
        } else {
          lv_obj_add_flag(w.priceStats, LV_OBJ_FLAG_HIDDEN);
        }
      }
      // The sort and the band update only matter while the balance page is on
      // screen; the flags above stay unconditional so nothing is stale when
      // the page is opened.
      if (show && page2Visible) {
        const int64_t windowStartMin = (int64_t)d.nowMin;

        static uint32_t times[200];
        static int8_t state[200];
        static int32_t win[200];
        int n = 0;
        for (int i = 0; i < d.priceCount && n < 200; ++i) {
          const int64_t tMin =
              (int64_t)d.priceFirstMin + (int64_t)i * d.priceResMin;
          // Include the slot that contains "now" (its start is before the
          // window, its end after) - excluding it would leave a grey
          // no-data gap of up to one resolution at the left edge.
          if (tMin + d.priceResMin <= windowStartMin ||
              tMin >= windowStartMin + 1440) {
            continue;
          }
          times[n] = (uint32_t)tMin;
          win[n] = d.prices[i];
          ++n;
        }
        if (n > 0) {
          int32_t sorted[200];
          for (int i = 0; i < n; ++i) sorted[i] = win[i];
          for (int i = 1; i < n; ++i) {
            const int32_t v = sorted[i];
            int j = i - 1;
            while (j >= 0 && sorted[j] > v) {
              sorted[j + 1] = sorted[j];
              --j;
            }
            sorted[j + 1] = v;
          }
          const int32_t mid = sorted[n / 2];
          const int32_t mn = sorted[0];
          const int32_t mx = sorted[n - 1];
          double sum = 0.0;
          for (int i = 0; i < n; ++i) sum += win[i];
          const double avg = sum / n;
          char sbuf[96];
          snprintf(sbuf, sizeof(sbuf),
                   TR("Min %.1f · Mittel %.1f · Max %.1f ct/kWh",
                      "Min %.1f · Mid %.1f · Max %.1f ct/kWh"),
                   mn / 1000.0, avg / 1000.0, mx / 1000.0);
          setTextIfChanged(w.priceStats, sbuf);
          const int32_t lowThresh = (mid + mn) / 2;
          const int32_t highThresh = (mid + mx) / 2;
          for (int i = 0; i < n; ++i) {
            // Negative prices get their own bright-green state and are not
            // folded into the low bucket.
            state[i] = (win[i] < 0)              ? (int8_t)4
                       : (win[i] < lowThresh)    ? (int8_t)1
                       : (win[i] > highThresh)   ? (int8_t)2
                                                 : (int8_t)0;
          }
          updateHistoryBand(w.priceBand, times, state, n, windowStartMin, 1440);

        }
      }
    }

    // IO indicator dots and wallbox legend names. The wallbox series are
    // hidden when fewer than two chargers are present.
    const bool ioState[8] = {d.ioInputs[0], d.ioInputs[1], d.ioInputs[2],
                             d.ioInputs[3], d.ioRelays[0], d.ioRelays[1],
                             d.ioSgReady[0], d.ioSgReady[1]};
    for (int i = 0; i < 8; ++i) {
      if (w.ioDot[i] != nullptr) {
        setBgColorIfChanged(w.ioDot[i],
                            d.hasIoStates && ioState[i] ? palette().green : palette().muted);
      }
    }
    for (int i = 0; i < 2; ++i) {
      const bool present = d.charger_count > i;
      if (w.wbLegendDot[i] != nullptr) {
        if (present) {
          lv_obj_clear_flag(w.wbLegendDot[i], LV_OBJ_FLAG_HIDDEN);
        } else {
          lv_obj_add_flag(w.wbLegendDot[i], LV_OBJ_FLAG_HIDDEN);
        }
      }
      if (w.wbLegendLabel[i] != nullptr) {
        if (present) {
          lv_obj_clear_flag(w.wbLegendLabel[i], LV_OBJ_FLAG_HIDDEN);
          const char *name = d.chargers[i].name[0] != '\0'
                                 ? d.chargers[i].name
                                 : (i == 0 ? "Wallbox 1" : "Wallbox 2");
          char shortName[16];
          snprintf(shortName, sizeof(shortName), "%.12s", name);
          snprintf(g_wbBase[i], sizeof(g_wbBase[i]), "%s", shortName);
          if (!g_tapActive) {
            setTextIfChanged(w.wbLegendLabel[i], shortName);
          }
        } else {
          lv_obj_add_flag(w.wbLegendLabel[i], LV_OBJ_FLAG_HIDDEN);
        }
      }
    }
  }
  // Energy flow page ---------------------------------------------------
  {
    if (g_tapActive && lv_tick_get() - g_tapMs > 30000) {
      g_tapActive = false;
      restoreLegendLabels(d);
      if (w.tapLine != nullptr) {
        lv_obj_add_flag(w.tapLine, LV_OBJ_FLAG_HIDDEN);
      }
    }
    char pbuf[24];

    // PV ---------------------------------------------------------------
    // 10 W already counts as generating: in low light a string reports a few
    // watts of diffuse light, which is real generation even though no flow is
    // visible. The flow links keep their own 25 W/10 W hysteresis below.
    constexpr float kPvGeneratingW = 10.0f;
    if (d.hasPv) {
      const bool generating = d.pvPower > kPvGeneratingW;
      fmtPower(buf, sizeof(buf), d.pvPower);
      setTextIfChanged(w.pvValue, buf);
      setColorIfChanged(w.pvValue, generating ? palette().solar : palette().muted);
      setTextIfChanged(w.pvState, generating ? TR("Erzeugung", "Generation")
                                             : TR("Keine Erzeugung", "No generation"));
      setColorIfChanged(w.pvState, generating ? palette().solar : palette().muted);
    } else {
      setTextIfChanged(w.pvValue, "--");
      setColorIfChanged(w.pvValue, palette().muted);
      setTextIfChanged(w.pvState, TR("kein Messwert", "no measurement"));
      setColorIfChanged(w.pvState, palette().muted);
    }

    // Grid -------------------------------------------------------------
    if (d.hasGrid && !d.island) {
      fmtPower(buf, sizeof(buf), std::fabs(d.gridPower));
      setTextIfChanged(w.gridValue, buf);
      setColorIfChanged(w.gridValue, std::fabs(d.gridPower) < 10.0f ? palette().muted : palette().grid);
      if (std::fabs(d.gridPower) < 10.0f) {
        setTextIfChanged(w.gridState, "ausgeglichen");
        setColorIfChanged(w.gridState, palette().muted);
      } else if (d.gridPower < 0.0f) {
        setTextIfChanged(w.gridState, TR("Einspeisung", "Feed-in"));
        setColorIfChanged(w.gridState, palette().green);
      } else {
        setTextIfChanged(w.gridState, TR("Bezug", "Import"));
        setColorIfChanged(w.gridState, palette().grid);
      }
    } else if (d.island) {
      setTextIfChanged(w.gridValue, "--");
      setColorIfChanged(w.gridValue, palette().muted);
      setTextIfChanged(w.gridState, TR("Inselbetrieb", "Island mode"));
      setColorIfChanged(w.gridState, palette().red);
    } else {
      setTextIfChanged(w.gridValue, "--");
      setColorIfChanged(w.gridValue, palette().muted);
      setTextIfChanged(w.gridState, TR("kein Messwert", "no measurement"));
      setColorIfChanged(w.gridState, palette().muted);
    }

    // Battery ----------------------------------------------------------
    if (d.hasBattery) {
      snprintf(buf, sizeof(buf), "%.0f %%", d.soc);
      setTextIfChanged(w.batValue, buf);
      setColorIfChanged(w.batValue, palette().battery);
      if (d.batteryPower > 25.0f) {
        fmtPower(pbuf, sizeof(pbuf), d.batteryPower);
        snprintf(buf, sizeof(buf), TR("Laden %s", "Charging %s"), pbuf);
        setTextIfChanged(w.batState, buf);
        setColorIfChanged(w.batState, palette().battery);
      } else if (d.batteryPower < -25.0f) {
        fmtPower(pbuf, sizeof(pbuf), -d.batteryPower);
        snprintf(buf, sizeof(buf), TR("Entladen %s", "Discharging %s"), pbuf);
        setTextIfChanged(w.batState, buf);
        setColorIfChanged(w.batState, palette().battery);
      } else {
        setTextIfChanged(w.batState, "inaktiv");
        setColorIfChanged(w.batState, palette().muted);
      }
    } else {
      setTextIfChanged(w.batValue, "--");
      setColorIfChanged(w.batValue, palette().muted);
      setTextIfChanged(w.batState, TR("kein Messwert", "no measurement"));
      setColorIfChanged(w.batState, palette().muted);
    }

    // Wallbox ----------------------------------------------------------
    // The CAS state names the node. A box that is not in state 4 is not
    // drawing, even when the charge manager has allocated a current to it, so
    // "ready to charge" and "error" stay visible instead of turning into a
    // power reading.
    const bool wallboxOn = d.hasCharger && d.charge_state == 4;
    if (wallboxOn) {
      fmtPower(pbuf, sizeof(pbuf), d.charge_power);
      setTextIfChanged(w.wbValue, pbuf);
      setColorIfChanged(w.wbValue, palette().ev);
      setTextIfChanged(w.wbState, TR("Laden", "Charging"));
      setColorIfChanged(w.wbState, palette().ev);
    } else if (d.hasCharger) {
      setTextIfChanged(w.wbValue, "--");
      setColorIfChanged(w.wbValue, palette().muted);
      setTextIfChanged(w.wbState, casStateName(d.charge_state));
      setColorIfChanged(w.wbState, d.charge_state == 5 ? palette().red : palette().muted);
    } else {
      setTextIfChanged(w.wbValue, "--");
      setColorIfChanged(w.wbValue, palette().muted);
      setTextIfChanged(w.wbState, TR("Getrennt", "Disconnected"));
      setColorIfChanged(w.wbState, palette().muted);
    }

    // House hub --------------------------------------------------------
    if (d.hasHouse) {
      fmtPower(buf, sizeof(buf), d.housePower);
      setTextIfChanged(w.hubValue, buf);
      setColorIfChanged(w.hubValue, palette().text);
    } else {
      setTextIfChanged(w.hubValue, "--");
      setColorIfChanged(w.hubValue, palette().muted);
    }

    // Links: hysteresis (25 W on, 10 W off) avoids flickering around zero.
    // Line width encodes magnitude, the particle speed in the timer does too.
    auto setFlow = [](int i, bool enabled, double power) {
      if (w.flowActive[i]) {
        if (!enabled || std::fabs(power) < 10.0) {
          w.flowActive[i] = false;
        }
      } else if (enabled && std::fabs(power) > 25.0) {
        w.flowActive[i] = true;
      }
      w.flowPower[i] = static_cast<float>(std::fabs(power));
      if (w.flowLine[i] == nullptr) {
        return;
      }
      if (w.flowActive[i]) {
        const float full = (i == 3) ? 11000.0f : 5000.0f;
        float norm = w.flowPower[i] / full;
        if (norm > 1.0f) {
          norm = 1.0f;
        }
        const int width = 7 + static_cast<int>(5.0f * norm);
        // Only restyle when the rounded width actually changes: the setter
        // invalidates the line's bounding box, and on the flow page that means
        // decoding the photo background again.
        if (w.flowWidth[i] != width) {
          w.flowWidth[i] = width;
          lv_obj_set_style_line_width(w.flowLine[i], width, 0);
        }
        lv_obj_clear_flag(w.flowLine[i], LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(w.flowLine[i], LV_OBJ_FLAG_HIDDEN);
      }
    };

    setFlow(0, d.hasPv, d.pvPower);
    setFlow(1, d.hasGrid && !d.island, d.gridPower);
    setFlow(2, d.hasBattery, d.batteryPower);
    setFlow(3, wallboxOn, d.charge_power);

    // Directions: grid/battery links run card -> hub by default.
    w.gridExporting = d.hasGrid && !d.island && d.gridPower < 0.0f;
    w.batCharging = d.hasBattery && d.batteryPower > 0.0f;
  }

  // Heating page ---------------------------------------------------------
  {
    auto onOff = [](bool v) { return v ? TR("Aktiv", "Active") : TR("Inaktiv", "Inactive"); };
    g_lastHeatingOverride = d.hasSgrOverride;
    if (d.hasHeating) {
      setTextIfChanged(w.heatBlocking, onOff(d.sgrBlocking));
      setColorIfChanged(w.heatBlocking, d.sgrBlocking ? palette().ev : palette().muted);
      setTextIfChanged(w.heatExtended, onOff(d.sgrExtended));
      setColorIfChanged(w.heatExtended, d.sgrExtended ? palette().ev : palette().muted);
      setTextIfChanged(w.heatStatus, onOff(d.heatingEnabled));
      setColorIfChanged(w.heatStatus, d.heatingEnabled ? palette().text : palette().muted);
      setTextIfChanged(w.heatP14, onOff(d.p14Enwg));
      setColorIfChanged(w.heatP14, d.p14Enwg ? palette().red : palette().muted);
    } else {
      setTextIfChanged(w.heatBlocking, "--");
      setTextIfChanged(w.heatExtended, "--");
      setTextIfChanged(w.heatStatus, "--");
      setTextIfChanged(w.heatP14, "--");
    }
    // SG history band: redraw only when the segment picture changes, and only
    // while the heating page is on screen. Copying 1442 entries every second
    // for a hidden page is pure work.
    const bool page6Visible =
        w.page6 != nullptr && !lv_obj_has_flag(w.page6, LV_OBJ_FLAG_HIDDEN);
    if (w.sgBand != nullptr && g_sgCount > 0 && page6Visible &&
        g_sgBandTimes != nullptr && g_sgBandState != nullptr) {
      for (int i = 0; i < g_sgCount; ++i) {
        const SgSample &e = g_sgSamples[(g_sgHead + i) % kMaxSamples];
        g_sgBandTimes[i] = e.timeMs;
        g_sgBandState[i] = e.state;
      }
      updateHistoryBand(w.sgBand, g_sgBandTimes, g_sgBandState, g_sgCount,
                        (int64_t)g_sgBandTimes[g_sgCount - 1] -
                            (int64_t)kHistoryMs,
                        (int64_t)kHistoryMs);
    }
    if (w.heatPartyBtn != nullptr) {
      lv_obj_t *lbl = lv_obj_get_child(w.heatPartyBtn, 0);
      // A tap shows its result immediately (pending), the real override
      // state from the next poll replaces it within seconds.
      if (g_partyPending &&
          (int32_t)(lv_tick_get() - g_partyPendingUntil) >= 0) {
        g_partyPending = false;
      }
      const bool overrideShown =
          g_partyPending ? g_partyPendingOn : d.hasSgrOverride;
      static bool partyShown = false;
      const bool partyWasShown = partyShown;
      partyShown = overrideShown;
      if (overrideShown) {
        char buf[40];
        const uint32_t remaining =
            g_partyPending && !d.hasSgrOverride
                ? 120
                : d.sgrOverrideRemainingMin;
        snprintf(buf, sizeof(buf), TR("Aus (%lu min)", "Off (%lu min)"), (unsigned long)remaining);
        setTextIfChanged(lbl, buf);
        if (!partyWasShown) {
          lv_obj_set_style_bg_color(w.heatPartyBtn, lv_color_hex(palette().ev), 0);
        }
      } else {
        setTextIfChanged(lbl, TR("Einschalten", "Turn on"));
        if (partyWasShown) {
          lv_obj_set_style_bg_color(w.heatPartyBtn, lv_color_hex(palette().card2), 0);
        }
      }
      // Always clickable: the party mode must also work when the SG-Ready
      // output is not currently blocking anything. Clear only once, a state
      // change every second would invalidate the button every second.
      static bool partyEnabled = false;
      if (!partyEnabled) {
        lv_obj_clear_state(w.heatPartyBtn, LV_STATE_DISABLED);
        partyEnabled = true;
      }
    }
    char buf[64];
    if (d.hasTemps) {
      fmtTemp(buf, sizeof(buf), d.tempCurrent);
      setTextIfChanged(w.tempNow, buf);
      char lo[16], hi[16];
      fmtTemp(lo, sizeof(lo), d.tempTodayMin);
      fmtTemp(hi, sizeof(hi), d.tempTodayMax);
      snprintf(buf, sizeof(buf), "%s / %s", lo, hi);
      setTextIfChanged(w.tempToday, buf);
      fmtTemp(lo, sizeof(lo), d.tempTomorrowMin);
      fmtTemp(hi, sizeof(hi), d.tempTomorrowMax);
      snprintf(buf, sizeof(buf), "%s / %s", lo, hi);
      setTextIfChanged(w.tempTomorrow, buf);
    } else {
      setTextIfChanged(w.tempNow, "--");
      setTextIfChanged(w.tempToday, "--");
      setTextIfChanged(w.tempTomorrow, "--");
    }
  }

  // Wallbox configuration page -----------------------------------------
  if (w.page5 != nullptr) {
    if (wallboxLayoutChanged(d)) {
      rebuildModeCards(d);
    }
    updateModeCard(g_globalCard, d, -1, true);
    for (int i = 0; i < g_renderedChargers; ++i) {
      updateModeCard(g_chargerCards[i], d, i, false);
    }
  }
}

ThemeId currentTheme() { return g_themeId; }

void setBackgroundImage(bool on) {
  g_showBackgroundImage = on;
  g_imageInitialized = true;
  applyFlowBackground();
}

void setThemeChangeHandler(ThemeChangeHandler handler) {
  g_themeChangeHandler = handler;
}

void setTheme(ThemeId id) {
  const ThemeId next = themeId((int)id);
  if (next == g_themeId) {
    return;
  }
  g_themeId = next;
  if (g_themeChangeHandler != nullptr) {
    g_themeChangeHandler(g_themeId);
  }
  if (g_root == nullptr) {
    return;
  }
  // Every colour is baked into a widget style or the chart image buffer, so a
  // full rebuild is the only way to restyle all six pages consistently. The
  // history and the chart data live outside the widget tree and survive it.
  const int page = g_activePage;
  lv_obj_clean(g_root);
  create(lv_obj_get_parent(g_root));
  setPage(page);
  setConnectionState(g_connectionState);
  update(g_lastData);
  // The chart image keeps its pixels across the rebuild. Repaint it now so the
  // card and divider follow the new palette even when no new sample arrived;
  // the series values themselves are unchanged and survive the rebuild.
  if (g_sampleCount > 0) {
    rebuildPowerChart();
  } else {
    renderChartImage();
  }
}

}  // namespace hemsui
