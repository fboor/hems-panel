// SPDX-License-Identifier: MIT
#include <functional>
#include "WarpUi.h"

LV_IMG_DECLARE(chart_bg);

#include <lvgl.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace warpui {
namespace {

constexpr uint32_t C_BG = 0x0E1319;
constexpr uint32_t C_CARD = 0x171F29;
constexpr uint32_t C_CARD2 = 0x222D39;
constexpr uint32_t C_LINE = 0x2B3947;
constexpr uint32_t C_TEXT = 0xE9F0F7;
constexpr uint32_t C_MUTED = 0x8899AA;
constexpr uint32_t C_GREEN = 0x3AD07F;
constexpr uint32_t C_RED = 0xFF5D5D;
constexpr uint32_t C_BLUE = 0x4AA3FF;
constexpr uint32_t C_AMBER = 0xFFB347;

// Energy-flow identity colours. Warm solar, cool grid, green storage,
// violet wallbox. Each has a dark tint for icon badges and card borders.
constexpr uint32_t C_SOLAR = 0xFFC247;
constexpr uint32_t C_BAT = 0x3AD07F;
constexpr uint32_t C_EV = 0xB388FF;
constexpr uint32_t C_SOLAR_DIM = 0x3A2E12;
constexpr uint32_t C_GRID_DIM = 0x142A40;
constexpr uint32_t C_BAT_DIM = 0x123225;
constexpr uint32_t C_EV_DIM = 0x2A1E44;
// Pale highlights for the moving particles, so they read as a separate
// layer on top of the thick coloured link.
constexpr uint32_t C_SOLAR_HI = 0xFFF0B8;
constexpr uint32_t C_GRID_HI = 0xCFE8FF;
constexpr uint32_t C_BAT_HI = 0xCFFFE4;
constexpr uint32_t C_EV_HI = 0xE9DBFF;

constexpr const lv_font_t *F_SMALL = &lv_font_montserrat_14;
constexpr const lv_font_t *F_BODY = &lv_font_montserrat_16;
constexpr const lv_font_t *F_TITLE = &lv_font_montserrat_20;
constexpr const lv_font_t *F_BIG = &lv_font_montserrat_36;
constexpr const lv_font_t *F_MED = &lv_font_montserrat_28;
constexpr const lv_font_t *F_ICON = &lv_font_montserrat_48;

struct Widgets {
  lv_obj_t *statusChip = nullptr;
  lv_obj_t *statusDot = nullptr;
  lv_obj_t *statusText = nullptr;
  lv_obj_t *autarkyValue = nullptr;
  lv_obj_t *forecastValue = nullptr;
  lv_obj_t *islandBadge = nullptr;
  lv_obj_t *islandText = nullptr;
  lv_obj_t *updatedText = nullptr;
  lv_obj_t *pageBtn1 = nullptr;
  lv_obj_t *pageBtn2 = nullptr;
  lv_obj_t *pageBtn3 = nullptr;
  lv_obj_t *pageBtn4 = nullptr;
  lv_obj_t *caption1 = nullptr;
  lv_obj_t *caption2 = nullptr;
  lv_obj_t *caption3 = nullptr;
  lv_obj_t *caption4 = nullptr;
  lv_obj_t *page1 = nullptr;
  lv_obj_t *page2 = nullptr;
  lv_obj_t *page3 = nullptr;
  lv_obj_t *page4 = nullptr;
  lv_obj_t *bgBtn = nullptr;

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
  bool gridExporting = false;
  bool batCharging = false;
  lv_obj_t *powerChart = nullptr;
  lv_chart_series_t *powerSeries = nullptr;
  lv_chart_series_t *seriesGrid = nullptr;
  lv_chart_series_t *seriesPv = nullptr;
  lv_chart_series_t *seriesHouse = nullptr;
  lv_chart_series_t *seriesBat = nullptr;
  lv_obj_t *scaleMax = nullptr;
  lv_obj_t *scaleMid = nullptr;
  lv_obj_t *scaleMin = nullptr;

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
  lv_obj_t *energyHouse = nullptr;
  lv_obj_t *energyImport = nullptr;
  lv_obj_t *energyExport = nullptr;
  lv_obj_t *energyBattery = nullptr;

  // Service page network card.
  lv_obj_t *netSsid = nullptr;
  lv_obj_t *netIp = nullptr;
  lv_obj_t *netWarp = nullptr;
  lv_obj_t *netWeb = nullptr;

  // First-boot Wi-Fi setup overlay.
  lv_obj_t *setupOverlay = nullptr;
  lv_obj_t *setupQr = nullptr;
  lv_obj_t *setupSsid = nullptr;
  lv_obj_t *setupPassword = nullptr;
  lv_obj_t *setupUrl = nullptr;
};

Widgets w;

lv_obj_t *card(lv_obj_t *parent, int x, int y, int width, int height) {
  lv_obj_t *c = lv_obj_create(parent);
  lv_obj_remove_style_all(c);
  lv_obj_set_pos(c, x, y);
  lv_obj_set_size(c, width, height);
  lv_obj_set_style_bg_color(c, lv_color_hex(C_CARD), 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(c, 10, 0);
  lv_obj_set_style_border_width(c, 1, 0);
  lv_obj_set_style_border_color(c, lv_color_hex(C_LINE), 0);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

lv_obj_t *label(lv_obj_t *parent, int x, int y, int width,
                const lv_font_t *font, uint32_t color, const char *text,
                lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_pos(l, x, y);
  if (width > 0) {
    lv_obj_set_width(l, width);
    lv_obj_set_style_text_align(l, align, 0);
  }
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  return l;
}

// Name on the left, value on the right.
lv_obj_t *row(lv_obj_t *parent, int x, int y, int width, const char *name,
              lv_obj_t **valueOut, const lv_font_t *font = F_BODY) {
  label(parent, x, y, 0, font, C_MUTED, name);
  lv_obj_t *value =
      label(parent, x, y, width, font, C_TEXT, "--", LV_TEXT_ALIGN_RIGHT);
  if (valueOut != nullptr) {
    *valueOut = value;
  }
  return value;
}

void title(lv_obj_t *parent, const char *text) {
  label(parent, 16, 10, 0, F_SMALL, C_MUTED, text);
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
  if (std::fabs(wh) >= 1000.0) {
    snprintf(out, n, "%.2f MWh", wh / 1000.0);
  } else {
    snprintf(out, n, "%.1f kWh", wh);
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
  if (l != nullptr) {
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  }
}

// A small transmission pylon for the grid node. LVGL's built-in symbol font
// has no tower glyph, so draw the few lines directly. The point arrays are
// static because lv_line keeps the pointer; there is only one grid node.
void gridTower(lv_obj_t *parent, uint32_t color) {
  static lv_point_precise_t leftLeg[2] = {{22, 6}, {11, 36}};
  static lv_point_precise_t rightLeg[2] = {{22, 6}, {33, 36}};
  static lv_point_precise_t base[2] = {{9, 36}, {35, 36}};
  static lv_point_precise_t crossTop[2] = {{17, 17}, {27, 17}};
  static lv_point_precise_t crossBottom[2] = {{14, 27}, {30, 27}};
  static lv_point_precise_t *segments[] = {leftLeg, rightLeg, base,
                                           crossTop, crossBottom};

  for (lv_point_precise_t *segment : segments) {
    lv_obj_t *line = lv_line_create(parent);
    lv_line_set_points(line, segment, 2);
    lv_obj_set_style_line_width(line, 2, 0);
    lv_obj_set_style_line_color(line, lv_color_hex(color), 0);
    lv_obj_set_style_line_rounded(line, true, 0);
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
                  bool pylon = false) {
  FlowNode n;
  n.card = lv_obj_create(parent);
  lv_obj_remove_style_all(n.card);
  lv_obj_set_pos(n.card, x, y);
  lv_obj_set_size(n.card, 176, 94);
  lv_obj_set_style_bg_color(n.card, lv_color_hex(C_CARD), 0);
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
  if (pylon) {
    gridTower(badge, accent);
  } else {
    lv_obj_t *icon = label(badge, 0, 0, 44, F_TITLE, accent, symbol,
                           LV_TEXT_ALIGN_CENTER);
    lv_obj_center(icon);
  }

  label(n.card, 68, 12, 96, F_SMALL, C_MUTED, name);
  n.value = label(n.card, 68, 30, 96, F_TITLE, accent, "--");
  n.state = label(n.card, 12, 68, 152, F_SMALL, C_MUTED, "--");
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
  lv_obj_set_style_line_color(w.flowTrack[idx], lv_color_hex(C_LINE), 0);
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
    lv_obj_set_size(dot, 12, 12);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    w.flowDot[idx][d] = dot;
  }
}

void showPage(int page) {
  // Display order. The flow view was built as page4, but it is the first page
  // users should see; the original energy/history/service pages follow it.
  lv_obj_t *pages[4] = {w.page4, w.page1, w.page2, w.page3};
  lv_obj_t *captions[4] = {w.caption4, w.caption1, w.caption2, w.caption3};
  lv_obj_t *buttons[4] = {w.pageBtn1, w.pageBtn2, w.pageBtn3, w.pageBtn4};

  for (int i = 0; i < 4; ++i) {
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
                                lv_color_hex(active ? C_BLUE : C_CARD2), 0);
      lv_obj_set_style_bg_opa(buttons[i], LV_OPA_COVER, 0);
      lv_obj_t *lbl = lv_obj_get_child(buttons[i], 0);
      if (lbl != nullptr) {
        lv_obj_set_style_text_color(
            lbl, lv_color_hex(active ? 0xFFFFFF : C_MUTED), 0);
      }
    }
  }
}

}  // namespace

void create(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, lv_color_hex(C_BG), 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

  // Header ---------------------------------------------------------------
  label(parent, 16, 8, 0, F_TITLE, C_TEXT, "WARP Energy Manager");
  // Page captions
  w.caption1 = label(parent, 16, 40, 0, F_BODY, C_MUTED, "Energie");
  w.caption2 = label(parent, 16, 40, 0, F_BODY, C_MUTED, "Verlauf");
  w.caption3 = label(parent, 16, 40, 0, F_BODY, C_MUTED, "Service");
  w.caption4 = label(parent, 16, 40, 0, F_BODY, C_MUTED, "Energiefluss");
  lv_obj_add_flag(w.caption2, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(w.caption3, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(w.caption4, LV_OBJ_FLAG_HIDDEN);

  // Autarkie and today's solar forecast. They share the header row with the
  // compact connection chip, so the right side stays readable at a glance.
  label(parent, 330, 4, 100, F_SMALL, C_MUTED, "Autarkie");
  w.autarkyValue = label(parent, 330, 20, 100, F_TITLE, C_TEXT, "--");
  label(parent, 438, 4, 110, F_SMALL, C_MUTED, "Prognose");
  w.forecastValue = label(parent, 438, 20, 110, F_TITLE, C_SOLAR, "--");

  // One pill for the connection state: "Kein WLAN", "Getrennt", "Daten" or
  // "Keine Daten". The coloured dot is part of the pill, not a second item.
  w.statusChip = lv_obj_create(parent);
  lv_obj_remove_style_all(w.statusChip);
  lv_obj_set_pos(w.statusChip, 556, 14);
  lv_obj_set_size(w.statusChip, 130, 32);
  lv_obj_set_style_radius(w.statusChip, 16, 0);
  lv_obj_set_style_bg_color(w.statusChip, lv_color_hex(C_CARD2), 0);
  lv_obj_set_style_bg_opa(w.statusChip, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(w.statusChip, 1, 0);
  lv_obj_set_style_border_color(w.statusChip, lv_color_hex(C_LINE), 0);
  lv_obj_clear_flag(w.statusChip, LV_OBJ_FLAG_SCROLLABLE);

  w.statusDot = lv_obj_create(w.statusChip);
  lv_obj_remove_style_all(w.statusDot);
  lv_obj_set_pos(w.statusDot, 12, 11);
  lv_obj_set_size(w.statusDot, 10, 10);
  lv_obj_set_style_radius(w.statusDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(w.statusDot, lv_color_hex(C_MUTED), 0);
  lv_obj_set_style_bg_opa(w.statusDot, LV_OPA_COVER, 0);
  w.statusText = label(w.statusChip, 28, 7, 94, F_SMALL, C_MUTED, "Kein WLAN");

  w.islandBadge = lv_obj_create(parent);
  lv_obj_remove_style_all(w.islandBadge);
  lv_obj_set_pos(w.islandBadge, 694, 14);
  lv_obj_set_size(w.islandBadge, 92, 30);
  lv_obj_set_style_radius(w.islandBadge, 15, 0);
  lv_obj_set_style_bg_color(w.islandBadge, lv_color_hex(C_CARD2), 0);
  lv_obj_set_style_bg_opa(w.islandBadge, LV_OPA_COVER, 0);
  w.islandText = label(w.islandBadge, 0, 6, 92, F_SMALL, C_MUTED, "Netz",
                       LV_TEXT_ALIGN_CENTER);

  lv_obj_t *page1 = lv_obj_create(parent);
  lv_obj_remove_style_all(page1);
  lv_obj_set_pos(page1, 0, 0);
  lv_obj_set_size(page1, 790, 480);
  w.page1 = page1;

  // Grid ---------------------------------------------------------------
  lv_obj_t *grid = card(page1, 12, 68, 250, 176);
  title(grid, "NETZ");
  w.gridTotal = label(grid, 16, 30, 0, F_BIG, C_TEXT, "--");
  w.gridDir = label(grid, 16, 76, 0, F_SMALL, C_MUTED, "Bezug");
  {
    static const char *names[3] = {"L1", "L2", "L3"};
    for (int i = 0; i < 3; ++i) {
      char name[8];
      snprintf(name, sizeof(name), "%s", names[i]);
      row(grid, 16, 104 + i * 22, 218, name, &w.gridPhase[i]);
    }
  }

  // Household ----------------------------------------------------------
  lv_obj_t *house = card(page1, 274, 68, 250, 176);
  title(house, "HAUS");
  w.houseTotal = label(house, 16, 30, 0, F_BIG, C_TEXT, "--");
  label(house, 16, 76, 0, F_SMALL, C_MUTED, "Verbrauch");
  {
    static const char *names[3] = {"L1", "L2", "L3"};
    for (int i = 0; i < 3; ++i) {
      row(house, 16, 104 + i * 22, 218, names[i], &w.housePhase[i]);
    }
  }

  // Battery ------------------------------------------------------------
  lv_obj_t *bat = card(page1, 536, 68, 250, 176);
  title(bat, "BATTERIE");
  w.batSoc = label(bat, 16, 28, 120, F_BIG, C_TEXT, "-- %");
  w.batDir = label(bat, 130, 42, 104, F_SMALL, C_MUTED, "--",
                   LV_TEXT_ALIGN_RIGHT);
  w.batBar = lv_bar_create(bat);
  lv_obj_remove_style_all(w.batBar);
  lv_obj_set_pos(w.batBar, 16, 82);
  lv_obj_set_size(w.batBar, 218, 12);
  lv_obj_set_style_radius(w.batBar, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(w.batBar, lv_color_hex(C_CARD2), 0);
  lv_obj_set_style_bg_opa(w.batBar, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(w.batBar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(w.batBar, lv_color_hex(C_GREEN),
                            LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(w.batBar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_bar_set_range(w.batBar, 0, 100);
  lv_bar_set_value(w.batBar, 0, LV_ANIM_OFF);

  row(bat, 16, 104, 218, "Leistung", &w.batPower);
  row(bat, 16, 128, 218, "Spannung", &w.batVoltage);
  row(bat, 16, 152, 218, "Strom", &w.batCurrent);

  // PV -----------------------------------------------------------------
  lv_obj_t *pv = card(page1, 12, 256, 250, 176);
  title(pv, "PV");
  w.pvTotal = label(pv, 16, 30, 0, F_BIG, C_GREEN, "--");
  label(pv, 16, 76, 0, F_SMALL, C_MUTED, "Erzeugung");
  row(pv, 16, 108, 218, "String A", &w.pvA);
  row(pv, 16, 134, 218, "String B", &w.pvB);

  // Wallbox -----------------------------------------------------------
  lv_obj_t *wallbox = card(page1, 274, 256, 250, 176);
  title(wallbox, "WALLBOX");
  w.wallboxValue = label(wallbox, 16, 30, 0, F_BIG, C_GREEN, "--");
  w.wallboxNote = label(wallbox, 16, 76, 0, F_SMALL, C_MUTED, "--");
  w.wallboxMode = label(wallbox, 16, 96, 0, F_SMALL, C_MUTED, "--");
  w.wallboxPhase = label(wallbox, 16, 116, 0, F_SMALL, C_MUTED, "--");

  // Energy totals ------------------------------------------------------
  lv_obj_t *energy = card(page1, 536, 256, 250, 176);
  title(energy, "ENERGIE");
  row(energy, 16, 30, 218, "PV", &w.energyPv, F_SMALL);
  row(energy, 16, 52, 218, "Verbrauch", &w.energyHouse, F_SMALL);
  row(energy, 16, 74, 218, "Netzbezug", &w.energyImport, F_SMALL);
  row(energy, 16, 96, 218, "Einspeisung", &w.energyExport, F_SMALL);
  row(energy, 16, 118, 218, "Akku", &w.energyBattery, F_SMALL);
  label(energy, 16, 144, 218, F_SMALL, C_MUTED, "seit Inbetriebnahme",
        LV_TEXT_ALIGN_RIGHT);

  // Footer -------------------------------------------------------------
  // Second page (power graph) -----------------------------------------
  w.page2 = lv_obj_create(parent);
  lv_obj_remove_style_all(w.page2);
  lv_obj_set_pos(w.page2, 0, 68);
  lv_obj_set_size(w.page2, 790, 372);
  lv_obj_add_flag(w.page2, LV_OBJ_FLAG_HIDDEN);

  w.powerChart = lv_chart_create(w.page2);
  lv_obj_set_pos(w.powerChart, 12, 0);
  lv_obj_set_size(w.powerChart, 766, 320);
  lv_chart_set_type(w.powerChart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(w.powerChart, 48);
  lv_chart_set_range(w.powerChart, LV_CHART_AXIS_PRIMARY_Y, -5000, 5000);
  lv_obj_set_style_bg_color(w.powerChart, lv_color_hex(C_CARD), 0);
  lv_obj_set_style_width(w.powerChart, 6, LV_PART_INDICATOR);
  lv_obj_set_style_height(w.powerChart, 6, LV_PART_INDICATOR);
  lv_obj_set_style_bg_image_src(w.powerChart, &chart_bg, 0);
  lv_obj_set_style_bg_image_opa(w.powerChart, LV_OPA_60, 0);
  lv_obj_set_style_border_width(w.powerChart, 1, 0);
  lv_obj_set_style_border_color(w.powerChart, lv_color_hex(C_LINE), 0);
  lv_obj_set_style_line_color(w.powerChart, lv_color_hex(C_MUTED), 0);
  lv_obj_set_style_pad_all(w.powerChart, 0, 0);
  w.scaleMax = label(w.page2, 20, 5, 60, F_SMALL, C_MUTED, "--", LV_TEXT_ALIGN_LEFT);
  w.scaleMid = label(w.page2, 20, 162, 60, F_SMALL, C_MUTED, "--", LV_TEXT_ALIGN_LEFT);
  w.scaleMin = label(w.page2, 20, 302, 60, F_SMALL, C_MUTED, "--", LV_TEXT_ALIGN_LEFT);
  w.seriesPv = lv_chart_add_series(w.powerChart, lv_color_hex(0x4A90D9), LV_CHART_AXIS_PRIMARY_Y);
  w.seriesHouse = lv_chart_add_series(w.powerChart, lv_color_hex(0xFF5D5D), LV_CHART_AXIS_PRIMARY_Y);
  w.seriesBat = lv_chart_add_series(w.powerChart, lv_color_hex(0x66BB6A), LV_CHART_AXIS_PRIMARY_Y);

  // Time labels at bottom (reversed: new values on right)
  const char *times[5] = {"24h", "18h", "12h", "6h", "0h"};
  int xpos[5] = {8, 185, 375, 565, 755};
  for (int i = 0; i < 5; ++i) {
    label(w.page2, xpos[i], 322, 40, F_SMALL, C_MUTED, times[i], LV_TEXT_ALIGN_CENTER);
  }

  // Legend (centered)
  const char *names[4] = {"Netz", "PV", "Verbrauch", "Akku"};
  lv_color_t colors[4] = {lv_color_hex(0xFFA726), lv_color_hex(0x4A90D9), lv_color_hex(0xFF5D5D), lv_color_hex(0x66BB6A)};
  for (int i = 0; i < 4; ++i) {
    lv_obj_t *dot = lv_obj_create(w.page2);
    lv_obj_remove_style_all(dot);
    int xoff = (i < 2) ? 20 : 0;
    lv_obj_set_pos(dot, 180 + i * 120 + xoff, 345);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, colors[i], 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    label(w.page2, 194 + i * 120 + xoff, 341, 0, F_SMALL, C_MUTED, names[i]);
  }

  // Third page (settings) ---------------------------------------------
  w.page3 = lv_obj_create(parent);
  lv_obj_remove_style_all(w.page3);
  lv_obj_set_pos(w.page3, 0, 68);
  lv_obj_set_size(w.page3, 790, 372);
  lv_obj_add_flag(w.page3, LV_OBJ_FLAG_HIDDEN);

  // Fourth page (energy flow) -----------------------------------------
  w.page4 = lv_obj_create(parent);
  lv_obj_remove_style_all(w.page4);
  lv_obj_set_pos(w.page4, 0, 68);
  lv_obj_set_size(w.page4, 790, 372);
  lv_obj_add_flag(w.page4, LV_OBJ_FLAG_HIDDEN);

  // Energy flow: four links around a central house hub. Links are drawn
  // first, particles next and cards last, so particles slide underneath the
  // nodes instead of crossing their text.
  flowLink(w.page4, 0, 200, 108, 332, 161, C_SOLAR);  // PV <-> house
  flowLink(w.page4, 1, 200, 264, 332, 211, C_BLUE);   // grid <-> house
  flowLink(w.page4, 2, 590, 108, 458, 161, C_BAT);    // battery <-> house
  flowLink(w.page4, 3, 458, 211, 590, 264, C_EV);     // house -> wallbox
  flowDots(w.page4, 0, C_SOLAR_HI);
  flowDots(w.page4, 1, C_GRID_HI);
  flowDots(w.page4, 2, C_BAT_HI);
  flowDots(w.page4, 3, C_EV_HI);

  FlowNode pvFlow = flowNode(w.page4, 24, 26, "PV", LV_SYMBOL_CHARGE,
                             C_SOLAR, C_SOLAR_DIM);
  w.pvNode = pvFlow.card;
  w.pvValue = pvFlow.value;
  w.pvState = pvFlow.state;

  FlowNode gridFlow = flowNode(w.page4, 24, 252, "NETZ", LV_SYMBOL_POWER,
                               C_BLUE, C_GRID_DIM, true);
  w.gridNode = gridFlow.card;
  w.gridValue = gridFlow.value;
  w.gridState = gridFlow.state;

  FlowNode batFlow = flowNode(w.page4, 590, 26, "AKKU",
                              LV_SYMBOL_BATTERY_FULL, C_BAT, C_BAT_DIM);
  w.batNode = batFlow.card;
  w.batValue = batFlow.value;
  w.batState = batFlow.state;

  FlowNode wbFlow = flowNode(w.page4, 590, 252, "WALLBOX", LV_SYMBOL_CHARGE,
                             C_EV, C_EV_DIM);
  w.wbNode = wbFlow.card;
  w.wbValue = wbFlow.value;
  w.wbState = wbFlow.state;

  // House hub in the middle: the one place everything flows to or from.
  w.hubNode = lv_obj_create(w.page4);
  lv_obj_remove_style_all(w.hubNode);
  lv_obj_set_pos(w.hubNode, 329, 120);
  lv_obj_set_size(w.hubNode, 132, 132);
  lv_obj_set_style_radius(w.hubNode, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(w.hubNode, lv_color_hex(C_CARD2), 0);
  lv_obj_set_style_bg_opa(w.hubNode, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(w.hubNode, 2, 0);
  lv_obj_set_style_border_color(w.hubNode, lv_color_hex(C_LINE), 0);
  lv_obj_clear_flag(w.hubNode, LV_OBJ_FLAG_SCROLLABLE);
  label(w.hubNode, 0, 16, 132, F_ICON, C_TEXT, LV_SYMBOL_HOME,
        LV_TEXT_ALIGN_CENTER);
  w.hubValue = label(w.hubNode, 0, 78, 132, F_MED, C_TEXT, "--",
                     LV_TEXT_ALIGN_CENTER);

  // Background cycle button
  w.bgBtn = lv_btn_create(w.page3);
  lv_obj_set_pos(w.bgBtn, 16, 18);
  lv_obj_set_size(w.bgBtn, 200, 40);
  lv_obj_t *bgLbl = lv_label_create(w.page3);
  lv_label_set_text(bgLbl, "Hintergrund");
  lv_obj_set_pos(bgLbl, 16, -4);
  lv_obj_set_style_text_color(bgLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_t *bgBtnLbl = lv_label_create(w.bgBtn);
  lv_label_set_text(bgBtnLbl, "Schwarz");
  lv_obj_center(bgBtnLbl);
  lv_obj_add_event_cb(w.bgBtn, [](lv_event_t *e) {
    static int mode = 0;
    mode = (mode + 1) % 3;
    lv_color_t colors[3] = {lv_color_hex(0x000000), lv_color_hex(0x808080), lv_color_hex(0xE6E6E6)};
    const char *names[3] = {"Schwarz", "Grau", "Weiss"};
    lv_obj_set_style_bg_color(lv_scr_act(), colors[mode], 0);
    lv_label_set_text(lv_obj_get_child(w.bgBtn, 0), names[mode]);
    // Update text colors: white on dark, black on white
    lv_color_t textColor = (mode == 2) ? lv_color_hex(0x000000) : lv_color_hex(0xFFFFFF);
    // Recursive label color update
    std::function<void(lv_obj_t*)> updateLabels = [&](lv_obj_t *obj) {
      if (lv_obj_check_type(obj, &lv_label_class)) {
        lv_obj_set_style_text_color(obj, textColor, 0);
      }
      uint32_t cnt = lv_obj_get_child_cnt(obj);
      for (uint32_t i = 0; i < cnt; i++) {
        updateLabels(lv_obj_get_child(obj, i));
      }
    };
    updateLabels(lv_scr_act());
  }, LV_EVENT_CLICKED, nullptr);

  // Network state. The setup portal stays reachable on the local network, so
  // this card is also the pointer to it when the WARP address has to change.
  lv_obj_t *net = card(w.page3, 16, 70, 758, 190);
  title(net, "NETZWERK");
  w.netSsid = label(net, 16, 34, 720, F_BODY, C_TEXT, "WLAN: --");
  w.netIp = label(net, 16, 58, 720, F_BODY, C_TEXT, "IP: --");
  w.netWarp = label(net, 16, 82, 720, F_BODY, C_TEXT, "WARP: --");
  w.netWeb = label(net, 16, 106, 720, F_BODY, C_BLUE, "Web: --");
  label(net, 16, 142, 720, F_SMALL, C_MUTED,
        "Zum Ändern die Web-Adresse im Browser öffnen.");

  auto makeNav = [](lv_obj_t *p, int x, int y, const char *text) {
    lv_obj_t *b = lv_btn_create(p);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, 50, 30);
    lv_obj_set_style_radius(b, 9, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(C_CARD2), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *lbl = lv_label_create(b);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, F_BODY, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(C_MUTED), 0);
    lv_obj_center(lbl);
    return b;
  };

  w.pageBtn1 = makeNav(parent, 500, 440, "1");
  w.pageBtn2 = makeNav(parent, 560, 440, "2");
  w.pageBtn3 = makeNav(parent, 620, 440, "3");
  w.pageBtn4 = makeNav(parent, 680, 440, "4");
  lv_obj_add_event_cb(w.pageBtn1, [](lv_event_t *) { showPage(1); },
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(w.pageBtn2, [](lv_event_t *) { showPage(2); },
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(w.pageBtn3, [](lv_event_t *) { showPage(3); },
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(w.pageBtn4, [](lv_event_t *) { showPage(4); },
                      LV_EVENT_CLICKED, nullptr);
  showPage(1);

  // Particle animation. 12.5 fps on purpose: small 12 px objects, no opacity
  // layers and no full-page invalidation. Speed scales with power, so a strong
  // flow visibly moves faster without ever exceeding the CPU budget.
  lv_timer_create([](lv_timer_t *) {
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
      const int step = 18 + static_cast<int>(42.0f * norm);
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
        const int x = static_cast<int>(x1 + (x2 - x1) * s + 0.5f) - 6;
        const int y = static_cast<int>(y1 + (y2 - y1) * s + 0.5f) - 6;
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(dot, x, y);
      }
    }
  }, 80, nullptr);
}

void setPage(int page) {
  if (page < 1) {
    page = 1;
  }
  if (page > 4) {
    page = 4;
  }
  showPage(page);
}

void showSetup(const char *ssid, const char *password, const char *url) {
  if (w.setupOverlay == nullptr) {
    lv_obj_t *overlay = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_size(overlay, 800, 480);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    w.setupOverlay = overlay;

    label(overlay, 40, 22, 0, F_BIG, C_TEXT, "WLAN einrichten");
    label(overlay, 40, 66, 0, F_BODY, C_MUTED,
          "Mit dem WARP-Panel verbinden");

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

    label(overlay, 380, 112, 0, F_SMALL, C_MUTED, "NETZWERK");
    w.setupSsid = label(overlay, 380, 134, 380, F_TITLE, C_TEXT, "--");
    label(overlay, 380, 174, 0, F_SMALL, C_MUTED, "PASSWORT");
    w.setupPassword = label(overlay, 380, 196, 380, F_TITLE, C_TEXT, "--");
    label(overlay, 380, 240, 0, F_SMALL, C_MUTED, "DANACH ÖFFNEN");
    w.setupUrl = label(overlay, 380, 262, 380, F_TITLE, C_BLUE, "--");
    label(overlay, 380, 308, 380, F_SMALL, C_MUTED,
          "QR-Code scannen oder dem Netzwerk beitreten. Im Browser "
          "WLAN-Zugang und die Adresse des WARP Energy Managers eintragen.");
    label(overlay, 40, 432, 720, F_SMALL, C_MUTED,
          "Das Einrichtungsnetz bleibt 3 Minuten ohne Aktivität aktiv.");
  }

  lv_label_set_text(w.setupSsid, ssid != nullptr ? ssid : "--");
  if (password != nullptr && password[0] != '\0') {
    lv_label_set_text(w.setupPassword, password);
  } else {
    lv_label_set_text(w.setupPassword, "offen");
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
  snprintf(buf, sizeof(buf), "WARP: %s",
           (warpUrl != nullptr && warpUrl[0] != '\0') ? warpUrl : "--");
  setTextIfChanged(w.netWarp, buf);
  snprintf(buf, sizeof(buf), "Web: %s",
           (webUrl != nullptr && webUrl[0] != '\0') ? webUrl : "--");
  setTextIfChanged(w.netWeb, buf);
}

void setConnectionState(ConnectionState state) {
  const char *text = "Kein WLAN";
  uint32_t color = C_MUTED;
  uint32_t background = C_CARD2;

  switch (state) {
    case ConnectionState::NoNet:
      text = "Kein WLAN";
      color = C_MUTED;
      background = C_CARD2;
      break;
    case ConnectionState::Disconnected:
      text = "Getrennt";
      color = C_RED;
      background = 0x2A1D22;
      break;
    case ConnectionState::Data:
      text = "Daten";
      color = C_GREEN;
      background = 0x173026;
      break;
    case ConnectionState::NoData:
      text = "Keine Daten";
      color = C_AMBER;
      background = 0x33280F;
      break;
  }

  setText(w.statusText, text);
  setColor(w.statusText, color);
  if (w.statusDot != nullptr) {
    lv_obj_set_style_bg_color(w.statusDot, lv_color_hex(color), 0);
  }
  if (w.statusChip != nullptr) {
    lv_obj_set_style_bg_color(w.statusChip, lv_color_hex(background), 0);
  }
}

void update(const WarpData &d) {
  char buf[48];
  char buf2[64];
  char chargeBuf[32];

  // Header statistics ------------------------------------------------
  if (w.autarkyValue != nullptr) {
    if (d.hasAutarky) {
      snprintf(buf, sizeof(buf), "%.0f %%", d.autarky);
      setText(w.autarkyValue, buf);
      setColor(w.autarkyValue, C_TEXT);
    } else {
      setText(w.autarkyValue, "--");
      setColor(w.autarkyValue, C_MUTED);
    }
  }
  if (w.forecastValue != nullptr) {
    if (d.hasForecast) {
      fmtEnergy(buf, sizeof(buf), d.forecastTodayWh);
      setText(w.forecastValue, buf);
      setColor(w.forecastValue, C_SOLAR);
    } else {
      setText(w.forecastValue, "--");
      setColor(w.forecastValue, C_MUTED);
    }
  }

  // Grid ---------------------------------------------------------------
  if (w.gridTotal != nullptr) {
    if (d.hasGrid) {
      fmtPower(buf, sizeof(buf), std::fabs(d.gridPower));
      snprintf(buf2, sizeof(buf2), "%s%s", d.gridPower < 0.0 ? "-" : "", buf);
      setText(w.gridTotal, buf2);
      setColor(w.gridTotal, d.gridPower < 0.0 ? C_GREEN : C_AMBER);
    } else {
      setText(w.gridTotal, "--");
      setColor(w.gridTotal, C_MUTED);
    }
  }
  setText(w.gridDir, !d.hasGrid ? "kein Messwert"
                                : (d.gridPower < 0.0 ? "Einspeisung" : "Bezug"));
  {
    const float phases[3] = {d.gridL1, d.gridL2, d.gridL3};
    for (int i = 0; i < 3; ++i) {
      if (!d.hasGrid) {
        setText(w.gridPhase[i], "--");
        setColor(w.gridPhase[i], C_MUTED);
        continue;
      }
      fmtSignedPower(buf, sizeof(buf), phases[i]);
      setText(w.gridPhase[i], buf);
      setColor(w.gridPhase[i],
               phases[i] < 0.0 ? C_GREEN : (phases[i] > 0.0 ? C_AMBER : C_TEXT));
    }
  }

  // Household ----------------------------------------------------------
  if (w.houseTotal != nullptr) {
    if (d.hasHouse) {
      fmtPower(buf, sizeof(buf), d.housePower);
      setText(w.houseTotal, buf);
      setColor(w.houseTotal, C_TEXT);
    } else {
      setText(w.houseTotal, "--");
      setColor(w.houseTotal, C_MUTED);
    }
  }
  {
    const float phases[3] = {d.houseL1, d.houseL2, d.houseL3};
    for (int i = 0; i < 3; ++i) {
      if (!d.hasHouse) {
        setText(w.housePhase[i], "--");
        continue;
      }
      fmtPower(buf, sizeof(buf), phases[i]);
      setText(w.housePhase[i], buf);
    }
  }

  // PV -----------------------------------------------------------------
  if (w.pvTotal != nullptr) {
    if (d.hasPv) {
      fmtPower(buf, sizeof(buf), d.pvPower);
      setText(w.pvTotal, buf);
      setColor(w.pvTotal, C_GREEN);
    } else {
      setText(w.pvTotal, "--");
      setColor(w.pvTotal, C_MUTED);
    }
  }
  if (d.hasPvStrings) {
    fmtPower(buf, sizeof(buf), d.pvA);
    setText(w.pvA, buf);
    fmtPower(buf, sizeof(buf), d.pvB);
    setText(w.pvB, buf);
  } else {
    setText(w.pvA, "--");
    setText(w.pvB, "--");
  }

  // Wallbox -----------------------------------------------------------
  /* Wallbox-Zustand aus charge_manager API ableiten */
  /* States: 0=unbekannt, 1=bereit, 2=laden, 3=fehler */
  if (d.charge_power > 0.1f || d.charge_current > 0.1) {
    if (d.charge_current > 0.1) {
      snprintf(chargeBuf, sizeof(chargeBuf), "%.1f A", d.charge_current / 1000.0);
    } else {
      snprintf(chargeBuf, sizeof(chargeBuf), "%.1f W", d.charge_power);
    }
    setText(w.wallboxValue, chargeBuf);
    setText(w.wallboxNote, "Laden");
  } else if (d.charge_name[0] != '\0') {
    snprintf(chargeBuf, sizeof(chargeBuf), "--");
    setText(w.wallboxValue, chargeBuf);
    if (d.charge_state == 1) {
      setText(w.wallboxNote, "Wartet auf Freigabe");
    } else if (d.charge_state == 3) {
      setText(w.wallboxNote, "Fehler");
    } else {
      setText(w.wallboxNote, "Konfiguriert");
    }
  } else {
      setText(w.wallboxValue, "--");
      setText(w.wallboxNote, "Getrennt");
  }
  const char *modeStr = d.charge_mode == 0 ? "Schnell" :
                        d.charge_mode == 1 ? "Aus" :
                        d.charge_mode == 2 ? "PV" :
                        d.charge_mode == 3 ? "Min+PV" : "?";
  snprintf(chargeBuf, sizeof(chargeBuf), "Modus: %s", modeStr);
  setText(w.wallboxMode, chargeBuf);
  snprintf(chargeBuf, sizeof(chargeBuf), "Phase: %s", d.is_3phase ? "3-phasig" : "1-phasig");
  setText(w.wallboxPhase, chargeBuf);
  if (w.batSoc != nullptr) {
    if (d.hasBattery) {
      snprintf(buf, sizeof(buf), "%.0f %%", d.soc);
      setText(w.batSoc, buf);
      setColor(w.batSoc, C_TEXT);
      if (w.batBar != nullptr) lv_bar_set_value(w.batBar, (int32_t)(d.soc + 0.5f), LV_ANIM_OFF);
    } else {
      setText(w.batSoc, "-- %");
      setColor(w.batSoc, C_MUTED);
      if (w.batBar != nullptr) lv_bar_set_value(w.batBar, 0, LV_ANIM_OFF);
    }
  }
  if (w.batDir != nullptr) {
    if (!d.hasBattery) {
      setText(w.batDir, "--");
      setColor(w.batDir, C_MUTED);
    } else if (d.batteryPower > 1.0f) {
      setText(w.batDir, "Laden");
      setColor(w.batDir, C_GREEN);
    } else if (d.batteryPower < -1.0f) {
      setText(w.batDir, "Entladen");
      setColor(w.batDir, C_AMBER);
    } else {
      setText(w.batDir, "Ruhe");
      setColor(w.batDir, C_MUTED);
    }
  }
  if (d.hasBattery) {
    const bool charging = d.batteryPower >= 0.0f;
    fmtSignedPower(buf, sizeof(buf), d.batteryPower);
    setText(w.batPower, buf);
    setColor(w.batPower, charging ? C_GREEN : C_AMBER);

    snprintf(buf, sizeof(buf), "%.1f V", d.batteryVoltage);
    setText(w.batVoltage, buf);
    snprintf(buf, sizeof(buf), "%.1f A", d.batteryCurrent);
    setText(w.batCurrent, buf);
  } else {
    setText(w.batPower, "--");
    setText(w.batVoltage, "--");
    setText(w.batCurrent, "--");
  }

  // Energy totals ------------------------------------------------------
  if (d.pvEnergyWh > 0.0f) {
    fmtEnergy(buf, sizeof(buf), d.pvEnergyWh);
  } else {
    fmtEnergy(buf, sizeof(buf), d.dayGenerationWh);
  }
  setText(w.energyPv, buf);

  if (d.houseEnergyWh > 0.0f) {
    fmtEnergy(buf, sizeof(buf), d.houseEnergyWh);
  } else {
    fmtEnergy(buf, sizeof(buf), d.dayLoadWh);
  }
  setText(w.energyHouse, buf);

  if (d.gridImportWh > 0.0f) {
    fmtEnergy(buf, sizeof(buf), d.gridImportWh);
  } else {
    fmtEnergy(buf, sizeof(buf), d.dayGridDrawWh);
  }
  setText(w.energyImport, buf);

  if (d.gridExportWh > 0.0f) {
    fmtEnergy(buf, sizeof(buf), d.gridExportWh);
  } else {
    fmtEnergy(buf, sizeof(buf), d.dayFeedInWh);
  }
  setText(w.energyExport, buf);

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
    setText(w.energyBattery, combined);
  }

  // Island badge -------------------------------------------------------
  if (w.islandBadge != nullptr && w.islandText != nullptr) {
    if (d.island) {
      setText(w.islandText, "Insel");
      lv_obj_set_style_text_color(w.islandText, lv_color_hex(C_RED), 0);
      lv_obj_set_style_bg_color(w.islandBadge, lv_color_hex(0x3A1D22), 0);
    } else {
      setText(w.islandText, "Netz");
      lv_obj_set_style_text_color(w.islandText, lv_color_hex(C_GREEN), 0);
      lv_obj_set_style_bg_color(w.islandBadge, lv_color_hex(0x173026), 0);
    }
  }

  // Power chart --------------------------------------------------------
  if (w.powerChart != nullptr) {
    static float histGrid[48] = {0};
    static float histPv[48] = {0};
    static float histHouse[48] = {0};
    static float histBat[48] = {0};
    static int histCount = 0;
    static int histIdx = 0;

    // Store current values in history
    histGrid[histIdx] = d.gridPower;
    histPv[histIdx] = d.pvPower;
    histHouse[histIdx] = d.housePower;
    histBat[histIdx] = d.batteryPower;
    histIdx = (histIdx + 1) % 48;
    if (histCount < 48) histCount++;

    // Compute min/max from all stored values
    float mn = histGrid[0], mx = histGrid[0];
    for (int i = 0; i < histCount; i++) {
      float vals[4] = {histGrid[i], histPv[i], histHouse[i], histBat[i]};
      for (int j = 0; j < 4; j++) {
        if (vals[j] < mn) mn = vals[j];
        if (vals[j] > mx) mx = vals[j];
      }
    }
    float absMax = fmax(std::fabs(mn), std::fabs(mx));
    int steps = (int)(absMax / 1000.0f) + 1;
    if (steps < 1) steps = 1;
    if (steps > 100) steps = 100;
    float range = steps * 1000.0f;
    lv_chart_set_range(w.powerChart, LV_CHART_AXIS_PRIMARY_Y,
                       (lv_coord_t)(-range), (lv_coord_t)(range));
    char sbuf[16];
    snprintf(sbuf, sizeof(sbuf), "%.0f W", range);
    setText(w.scaleMax, sbuf);
    snprintf(sbuf, sizeof(sbuf), "0 W");
    setText(w.scaleMid, sbuf);
    snprintf(sbuf, sizeof(sbuf), "%.0f W", -range);
    setText(w.scaleMin, sbuf);

    // Add data point periodically
    static int pollCount = 0;
    pollCount++;
    if (pollCount >= 3) {
      pollCount = 0;
      if (w.seriesGrid != nullptr) lv_chart_set_next_value(w.powerChart, w.seriesGrid, (lv_coord_t)d.gridPower);
      if (w.seriesPv != nullptr) lv_chart_set_next_value(w.powerChart, w.seriesPv, (lv_coord_t)d.pvPower);
      if (w.seriesHouse != nullptr) lv_chart_set_next_value(w.powerChart, w.seriesHouse, (lv_coord_t)d.housePower);
      if (w.seriesBat != nullptr) lv_chart_set_next_value(w.powerChart, w.seriesBat, (lv_coord_t)d.batteryPower);
      lv_chart_refresh(w.powerChart);
    }
  }
  // Energy flow page ---------------------------------------------------
  {
    char pbuf[24];

    // PV ---------------------------------------------------------------
    if (d.hasPv) {
      fmtPower(buf, sizeof(buf), d.pvPower);
      setText(w.pvValue, buf);
      setColor(w.pvValue, d.pvPower > 25.0f ? C_SOLAR : C_MUTED);
      setText(w.pvState, d.pvPower > 25.0f ? "Erzeugung" : "Keine Erzeugung");
      setColor(w.pvState, d.pvPower > 25.0f ? C_SOLAR : C_MUTED);
    } else {
      setText(w.pvValue, "--");
      setColor(w.pvValue, C_MUTED);
      setText(w.pvState, "kein Messwert");
      setColor(w.pvState, C_MUTED);
    }

    // Grid -------------------------------------------------------------
    if (d.hasGrid && !d.island) {
      fmtPower(buf, sizeof(buf), std::fabs(d.gridPower));
      setText(w.gridValue, buf);
      setColor(w.gridValue, std::fabs(d.gridPower) < 10.0f ? C_MUTED : C_BLUE);
      if (std::fabs(d.gridPower) < 10.0f) {
        setText(w.gridState, "Ausgeglichen");
        setColor(w.gridState, C_MUTED);
      } else if (d.gridPower < 0.0f) {
        setText(w.gridState, "Einspeisung");
        setColor(w.gridState, C_GREEN);
      } else {
        setText(w.gridState, "Bezug");
        setColor(w.gridState, C_BLUE);
      }
    } else if (d.island) {
      setText(w.gridValue, "--");
      setColor(w.gridValue, C_MUTED);
      setText(w.gridState, "Inselbetrieb");
      setColor(w.gridState, C_RED);
    } else {
      setText(w.gridValue, "--");
      setColor(w.gridValue, C_MUTED);
      setText(w.gridState, "kein Messwert");
      setColor(w.gridState, C_MUTED);
    }

    // Battery ----------------------------------------------------------
    if (d.hasBattery) {
      snprintf(buf, sizeof(buf), "%.0f %%", d.soc);
      setText(w.batValue, buf);
      setColor(w.batValue, C_BAT);
      if (d.batteryPower > 25.0f) {
        fmtPower(pbuf, sizeof(pbuf), d.batteryPower);
        snprintf(buf, sizeof(buf), "Laden %s", pbuf);
        setText(w.batState, buf);
        setColor(w.batState, C_BAT);
      } else if (d.batteryPower < -25.0f) {
        fmtPower(pbuf, sizeof(pbuf), -d.batteryPower);
        snprintf(buf, sizeof(buf), "Entladen %s", pbuf);
        setText(w.batState, buf);
        setColor(w.batState, C_BAT);
      } else {
        setText(w.batState, "Ruhe");
        setColor(w.batState, C_MUTED);
      }
    } else {
      setText(w.batValue, "--");
      setColor(w.batValue, C_MUTED);
      setText(w.batState, "kein Messwert");
      setColor(w.batState, C_MUTED);
    }

    // Wallbox ----------------------------------------------------------
    double wallboxW = d.charge_power;
    if (wallboxW <= 0.0 && d.charge_current > 0.1) {
      wallboxW = d.charge_current / 1000.0 * 230.0 * (d.is_3phase ? 3.0 : 1.0);
    }
    const bool wallboxOn = wallboxW > 10.0 || d.charge_current > 0.1;
    if (wallboxOn) {
      fmtPower(pbuf, sizeof(pbuf), wallboxW);
      setText(w.wbValue, pbuf);
      setColor(w.wbValue, C_EV);
      setText(w.wbState, d.charge_state == 2 ? "Laden" : "Verbunden");
      setColor(w.wbState, C_EV);
    } else if (d.charge_name[0] != '\0') {
      setText(w.wbValue, "--");
      setColor(w.wbValue, C_MUTED);
      if (d.charge_state == 1) {
        setText(w.wbState, "Bereit");
        setColor(w.wbState, C_MUTED);
      } else if (d.charge_state == 3) {
        setText(w.wbState, "Fehler");
        setColor(w.wbState, C_RED);
      } else {
        setText(w.wbState, "Verbunden");
        setColor(w.wbState, C_MUTED);
      }
    } else {
      setText(w.wbValue, "--");
      setColor(w.wbValue, C_MUTED);
      setText(w.wbState, "Getrennt");
      setColor(w.wbState, C_MUTED);
    }

    // House hub --------------------------------------------------------
    if (d.hasHouse) {
      fmtPower(buf, sizeof(buf), d.housePower);
      setText(w.hubValue, buf);
      setColor(w.hubValue, C_TEXT);
    } else {
      setText(w.hubValue, "--");
      setColor(w.hubValue, C_MUTED);
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
        lv_obj_set_style_line_width(w.flowLine[i],
                                    7 + static_cast<int>(5.0f * norm), 0);
        lv_obj_clear_flag(w.flowLine[i], LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(w.flowLine[i], LV_OBJ_FLAG_HIDDEN);
      }
    };

    setFlow(0, d.hasPv, d.pvPower);
    setFlow(1, d.hasGrid && !d.island, d.gridPower);
    setFlow(2, d.hasBattery, d.batteryPower);
    setFlow(3, wallboxOn, wallboxW);

    // Directions: grid/battery links run card -> hub by default.
    w.gridExporting = d.hasGrid && !d.island && d.gridPower < 0.0f;
    w.batCharging = d.hasBattery && d.batteryPower > 0.0f;
  }
}

}  // namespace warpui
