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

constexpr const lv_font_t *F_SMALL = &lv_font_montserrat_14;
constexpr const lv_font_t *F_BODY = &lv_font_montserrat_16;
constexpr const lv_font_t *F_TITLE = &lv_font_montserrat_20;
constexpr const lv_font_t *F_BIG = &lv_font_montserrat_36;
constexpr const lv_font_t *F_MED = &lv_font_montserrat_28;

struct Widgets {
  lv_obj_t *statusDot = nullptr;
  lv_obj_t *statusText = nullptr;
  lv_obj_t *islandBadge = nullptr;
  lv_obj_t *islandText = nullptr;
  lv_obj_t *updatedText = nullptr;
  lv_obj_t *pageBtn1 = nullptr;
  lv_obj_t *pageBtn2 = nullptr;
  lv_obj_t *pageBtn3 = nullptr;
  lv_obj_t *caption1 = nullptr;
  lv_obj_t *pageBtn4 = nullptr;
  lv_obj_t *page4 = nullptr;
  lv_obj_t *flowLine1 = nullptr;
  lv_obj_t *flowLine2 = nullptr;
  lv_obj_t *flowLine3 = nullptr;
  lv_obj_t *flowLine4 = nullptr;
  lv_obj_t *batFill = nullptr;
  lv_obj_t *batBolt = nullptr;
  lv_obj_t *flowDot1 = nullptr;
  lv_obj_t *flowDot2 = nullptr;
  lv_obj_t *flowDot3 = nullptr;
  lv_obj_t *flowDot4 = nullptr;
  lv_obj_t *caption2 = nullptr;
  lv_obj_t *caption3 = nullptr;
  lv_obj_t *page1 = nullptr;
  lv_obj_t *page2 = nullptr;
  lv_obj_t *page3 = nullptr;
  lv_obj_t *bgBtn = nullptr;
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
  bool flow1Active = false, flow2Active = false, flow3Active = false, flow4Active = false;
  int animPhase = 0;
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

void setColor(lv_obj_t *l, uint32_t color) {
  if (l != nullptr) {
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
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
  lv_obj_add_flag(w.caption2, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(w.caption3, LV_OBJ_FLAG_HIDDEN);

  w.statusDot = lv_obj_create(parent);
  lv_obj_remove_style_all(w.statusDot);
  lv_obj_set_pos(w.statusDot, 548, 21);
  lv_obj_set_size(w.statusDot, 10, 10);
  lv_obj_set_style_radius(w.statusDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(w.statusDot, lv_color_hex(C_MUTED), 0);
  lv_obj_set_style_bg_opa(w.statusDot, LV_OPA_COVER, 0);
  w.statusText = label(parent, 566, 18, 120, F_SMALL, C_MUTED, "Startet ...");

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

  // Simple energy flow nodes
  // PV (top left)
  lv_obj_t *pvNode = lv_obj_create(w.page4);
  lv_obj_remove_style_all(pvNode);
  lv_obj_set_pos(pvNode, 40, 40);
  lv_obj_set_size(pvNode, 60, 60);
  lv_obj_set_style_radius(pvNode, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(pvNode, lv_color_hex(0x4A90D9), 0);
  lv_obj_set_style_bg_opa(pvNode, LV_OPA_COVER, 0);
  lv_obj_t *pvIcon = lv_label_create(pvNode);
  lv_label_set_text(pvIcon, LV_SYMBOL_CHARGE);
  lv_obj_center(pvIcon);
  lv_obj_set_style_text_color(pvIcon, lv_color_hex(0xFFFFFF), 0);
  label(w.page4, 50, 105, 0, F_SMALL, C_TEXT, "PV");

  // Grid (bottom left)
  lv_obj_t *gridNode = lv_obj_create(w.page4);
  lv_obj_remove_style_all(gridNode);
  lv_obj_set_pos(gridNode, 40, 240);
  lv_obj_set_size(gridNode, 60, 60);
  lv_obj_set_style_radius(gridNode, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(gridNode, lv_color_hex(0xFFA726), 0);
  lv_obj_set_style_bg_opa(gridNode, LV_OPA_COVER, 0);
  lv_obj_t *gridIcon = lv_label_create(gridNode);
  lv_label_set_text(gridIcon, LV_SYMBOL_POWER);
  lv_obj_center(gridIcon);
  lv_obj_set_style_text_color(gridIcon, lv_color_hex(0xFFFFFF), 0);
  label(w.page4, 45, 305, 0, F_SMALL, C_TEXT, "Netz");

  // House (center)
  lv_obj_t *houseNode = lv_obj_create(w.page4);
  lv_obj_remove_style_all(houseNode);
  lv_obj_set_pos(houseNode, 365, 140);
  lv_obj_set_size(houseNode, 60, 60);
  lv_obj_set_style_radius(houseNode, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(houseNode, lv_color_hex(0xFF5D5D), 0);
  lv_obj_set_style_bg_opa(houseNode, LV_OPA_COVER, 0);
  lv_obj_t *houseIcon = lv_label_create(houseNode);
  lv_label_set_text(houseIcon, LV_SYMBOL_HOME);
  lv_obj_center(houseIcon);
  lv_obj_set_style_text_color(houseIcon, lv_color_hex(0xFFFFFF), 0);
  label(w.page4, 360, 205, 0, F_SMALL, C_TEXT, "Haus");

  // Battery (bottom center)
  lv_obj_t *batNode = lv_obj_create(w.page4);
  lv_obj_remove_style_all(batNode);
  lv_obj_set_pos(batNode, 365, 260);
  lv_obj_set_size(batNode, 60, 60);
  lv_obj_set_style_radius(batNode, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(batNode, lv_color_hex(0x66BB6A), 0);
  lv_obj_set_style_bg_opa(batNode, LV_OPA_COVER, 0);
  w.batFill = lv_obj_create(batNode);
  lv_obj_remove_style_all(w.batFill);
  lv_obj_set_pos(w.batFill, 0, 0);
  lv_obj_set_size(w.batFill, 60, 60);
  lv_obj_set_style_bg_color(w.batFill, lv_color_hex(0x66BB6A), 0);
  lv_obj_set_style_bg_opa(w.batFill, LV_OPA_COVER, 0);
  lv_obj_t *batIcon = lv_label_create(batNode);
  lv_label_set_text(batIcon, LV_SYMBOL_BATTERY_FULL);
  lv_obj_center(batIcon);
  lv_obj_set_style_text_color(batIcon, lv_color_hex(0xFFFFFF), 0);
  w.batBolt = lv_label_create(batNode);
  lv_label_set_text(w.batBolt, LV_SYMBOL_CHARGE);
  lv_obj_center(w.batBolt);
  lv_obj_set_style_text_color(w.batBolt, lv_color_hex(0xFFFF00), 0);
  lv_obj_add_flag(w.batBolt, LV_OBJ_FLAG_HIDDEN);
  label(w.page4, 355, 325, 0, F_SMALL, C_TEXT, "Akku");

  // Wallbox (right)
  lv_obj_t *wbNode = lv_obj_create(w.page4);
  lv_obj_remove_style_all(wbNode);
  lv_obj_set_pos(wbNode, 690, 140);
  lv_obj_set_size(wbNode, 60, 60);
  lv_obj_set_style_radius(wbNode, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(wbNode, lv_color_hex(0x9C27B0), 0);
  lv_obj_set_style_bg_opa(wbNode, LV_OPA_COVER, 0);
  lv_obj_t *wbIcon = lv_label_create(wbNode);
  lv_label_set_text(wbIcon, LV_SYMBOL_CHARGE);
  lv_obj_center(wbIcon);
  lv_obj_set_style_text_color(wbIcon, lv_color_hex(0xFFFFFF), 0);
  label(w.page4, 675, 205, 0, F_SMALL, C_TEXT, "Wallbox");

  // Energy flow lines (simple)
  static lv_point_precise_t pvToHouse[] = {{70, 70}, {365, 170}};
  static lv_point_precise_t gridToHouse[] = {{70, 270}, {365, 170}};
  static lv_point_precise_t batToHouse[] = {{395, 290}, {395, 200}};
  static lv_point_precise_t houseToWb[] = {{425, 170}, {690, 170}};
  w.flowLine1 = lv_line_create(w.page4);
  lv_line_set_points(w.flowLine1, pvToHouse, 2);
  lv_obj_set_style_line_width(w.flowLine1, 2, 0);
  lv_obj_set_style_line_color(w.flowLine1, lv_color_hex(0x4A90D9), 0);
  w.flowDot1 = lv_obj_create(w.page4);
  lv_obj_remove_style_all(w.flowDot1);
  lv_obj_set_size(w.flowDot1, 6, 6);
  lv_obj_set_style_radius(w.flowDot1, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(w.flowDot1, lv_color_hex(0x4A90D9), 0);
  lv_obj_set_style_bg_opa(w.flowDot1, LV_OPA_COVER, 0);
  lv_obj_add_flag(w.flowDot1, LV_OBJ_FLAG_HIDDEN);

  w.flowLine2 = lv_line_create(w.page4);
  lv_line_set_points(w.flowLine2, gridToHouse, 2);
  lv_obj_set_style_line_width(w.flowLine2, 2, 0);
  lv_obj_set_style_line_color(w.flowLine2, lv_color_hex(0xFFA726), 0);
  w.flowDot2 = lv_obj_create(w.page4);
  lv_obj_remove_style_all(w.flowDot2);
  lv_obj_set_size(w.flowDot2, 6, 6);
  lv_obj_set_style_radius(w.flowDot2, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(w.flowDot2, lv_color_hex(0xFFA726), 0);
  lv_obj_set_style_bg_opa(w.flowDot2, LV_OPA_COVER, 0);
  lv_obj_add_flag(w.flowDot2, LV_OBJ_FLAG_HIDDEN);

  w.flowLine3 = lv_line_create(w.page4);
  lv_line_set_points(w.flowLine3, batToHouse, 2);
  lv_obj_set_style_line_width(w.flowLine3, 2, 0);
  lv_obj_set_style_line_color(w.flowLine3, lv_color_hex(0x66BB6A), 0);
  w.flowDot3 = lv_obj_create(w.page4);
  lv_obj_remove_style_all(w.flowDot3);
  lv_obj_set_size(w.flowDot3, 6, 6);
  lv_obj_set_style_radius(w.flowDot3, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(w.flowDot3, lv_color_hex(0x66BB6A), 0);
  lv_obj_set_style_bg_opa(w.flowDot3, LV_OPA_COVER, 0);
  lv_obj_add_flag(w.flowDot3, LV_OBJ_FLAG_HIDDEN);

  w.flowLine4 = lv_line_create(w.page4);
  lv_line_set_points(w.flowLine4, houseToWb, 2);
  lv_obj_set_style_line_width(w.flowLine4, 2, 0);
  lv_obj_set_style_line_color(w.flowLine4, lv_color_hex(0x9C27B0), 0);
  w.flowDot4 = lv_obj_create(w.page4);
  lv_obj_remove_style_all(w.flowDot4);
  lv_obj_set_size(w.flowDot4, 6, 6);
  lv_obj_set_style_radius(w.flowDot4, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(w.flowDot4, lv_color_hex(0x9C27B0), 0);
  lv_obj_set_style_bg_opa(w.flowDot4, LV_OPA_COVER, 0);
  lv_obj_add_flag(w.flowDot4, LV_OBJ_FLAG_HIDDEN);

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
  w.pageBtn1 = lv_btn_create(parent);
  lv_obj_set_pos(w.pageBtn1, 500, 440);
  lv_obj_set_size(w.pageBtn1, 50, 30);
  lv_obj_t *lbl1 = lv_label_create(w.pageBtn1);
  lv_label_set_text(lbl1, "1");
  lv_obj_center(lbl1);
  lv_obj_add_event_cb(w.pageBtn1, [](lv_event_t *e) {
    lv_obj_clear_flag(w.page1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(w.page2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(w.page3, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(w.page4, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(w.caption1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(w.caption2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(w.caption3, LV_OBJ_FLAG_HIDDEN);
  }, LV_EVENT_CLICKED, nullptr);

  w.pageBtn2 = lv_btn_create(parent);
  lv_obj_set_pos(w.pageBtn2, 560, 440);
  lv_obj_set_size(w.pageBtn2, 50, 30);
  lv_obj_t *lbl2 = lv_label_create(w.pageBtn2);
  lv_label_set_text(lbl2, "2");
  lv_obj_center(lbl2);
  lv_obj_add_event_cb(w.pageBtn2, [](lv_event_t *e) {
    lv_obj_add_flag(w.page1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(w.page2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(w.page3, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(w.page4, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(w.caption1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(w.caption2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(w.caption3, LV_OBJ_FLAG_HIDDEN);
  }, LV_EVENT_CLICKED, nullptr);

  w.pageBtn3 = lv_btn_create(parent);
  lv_obj_set_pos(w.pageBtn3, 620, 440);
  lv_obj_set_size(w.pageBtn3, 50, 30);
  lv_obj_t *lbl3 = lv_label_create(w.pageBtn3);
  lv_label_set_text(lbl3, "3");
  lv_obj_center(lbl3);
  lv_obj_add_event_cb(w.pageBtn3, [](lv_event_t *e) {
    lv_obj_add_flag(w.page1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(w.page2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(w.page3, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(w.page4, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(w.caption1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(w.caption2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(w.caption3, LV_OBJ_FLAG_HIDDEN);
  }, LV_EVENT_CLICKED, nullptr);

  w.pageBtn4 = lv_btn_create(parent);
  lv_obj_set_pos(w.pageBtn4, 680, 440);
  lv_obj_set_size(w.pageBtn4, 50, 30);
  lv_obj_t *lbl4 = lv_label_create(w.pageBtn4);
  lv_label_set_text(lbl4, "4");
  lv_obj_center(lbl4);
  lv_obj_add_event_cb(w.pageBtn4, [](lv_event_t *e) {
    lv_obj_add_flag(w.page1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(w.page2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(w.page3, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(w.page4, LV_OBJ_FLAG_HIDDEN);
  }, LV_EVENT_CLICKED, nullptr);
  lv_timer_create([](lv_timer_t *t) {
    w.animPhase = (w.animPhase + 8) % 400;
    float tt = w.animPhase / 400.0f;
    if (w.flowDot1 != nullptr) {
      if (w.flow1Active) {
        lv_obj_clear_flag(w.flowDot1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(w.flowDot1, (int)(70 + (365-70) * tt) - 3, (int)(70 + (170-70) * tt) - 3);
      } else {
        lv_obj_add_flag(w.flowDot1, LV_OBJ_FLAG_HIDDEN);
      }
    }
    if (w.flowDot2 != nullptr) {
      if (w.flow2Active) {
        lv_obj_clear_flag(w.flowDot2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(w.flowDot2, (int)(70 + (365-70) * tt) - 3, (int)(270 + (170-270) * tt) - 3);
      } else {
        lv_obj_add_flag(w.flowDot2, LV_OBJ_FLAG_HIDDEN);
      }
    }
    if (w.flowDot3 != nullptr) {
      if (w.flow3Active) {
        lv_obj_clear_flag(w.flowDot3, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(w.flowDot3, 395 - 3, (int)(290 + (200-290) * tt) - 3);
      } else {
        lv_obj_add_flag(w.flowDot3, LV_OBJ_FLAG_HIDDEN);
      }
    }
    if (w.flowDot4 != nullptr) {
      if (w.flow4Active) {
        lv_obj_clear_flag(w.flowDot4, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(w.flowDot4, (int)(425 + (690-425) * tt) - 3, 170 - 3);
      } else {
        lv_obj_add_flag(w.flowDot4, LV_OBJ_FLAG_HIDDEN);
      }
    }
  }, 50, nullptr);
}

void setStatus(const char *text, bool ok) {
  if (w.statusText != nullptr) {
    lv_label_set_text(w.statusText, text);
  }
  if (w.statusText != nullptr) {
    lv_obj_set_style_text_color(w.statusText,
                                lv_color_hex(ok ? C_GREEN : C_RED), 0);
  }
  if (w.statusDot != nullptr) {
    lv_obj_set_style_bg_color(w.statusDot, lv_color_hex(ok ? C_GREEN : C_RED),
                              0);
  }
}

void update(const WarpData &d) {
  char buf[48];
  char buf2[64];
  char chargeBuf[32];

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
  // Battery SoC fill and charge bolt
  if (w.batFill != nullptr) {
    int fillH = (int)(60.0f * d.soc / 100.0f);
    if (fillH < 0) fillH = 0;
    if (fillH > 60) fillH = 60;
    lv_obj_set_size(w.batFill, 60, fillH);
    lv_obj_set_pos(w.batFill, 0, 60 - fillH);
  }
  if (w.batBolt != nullptr) {
    if (d.batteryPower < -10.0f) lv_obj_clear_flag(w.batBolt, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(w.batBolt, LV_OBJ_FLAG_HIDDEN);
  }
  // Energy flow line visibility with hysteresis (25W on, 10W off)
  if (w.flowLine1 != nullptr) {
    if (w.flow1Active) { if (std::fabs(d.pvPower) < 10.0f) w.flow1Active = false; }
    else { if (std::fabs(d.pvPower) > 25.0f) w.flow1Active = true; }
    if (w.flow1Active) lv_obj_clear_flag(w.flowLine1, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(w.flowLine1, LV_OBJ_FLAG_HIDDEN);
  }
  if (w.flowLine2 != nullptr) {
    if (w.flow2Active) { if (std::fabs(d.gridPower) < 10.0f) w.flow2Active = false; }
    else { if (std::fabs(d.gridPower) > 25.0f) w.flow2Active = true; }
    if (w.flow2Active) lv_obj_clear_flag(w.flowLine2, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(w.flowLine2, LV_OBJ_FLAG_HIDDEN);
  }
  if (w.flowLine3 != nullptr) {
    if (w.flow3Active) { if (std::fabs(d.batteryPower) < 10.0f) w.flow3Active = false; }
    else { if (std::fabs(d.batteryPower) > 25.0f) w.flow3Active = true; }
    if (w.flow3Active) lv_obj_clear_flag(w.flowLine3, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(w.flowLine3, LV_OBJ_FLAG_HIDDEN);
  }
  if (w.flowLine4 != nullptr) {
    if (w.flow4Active) { if (std::fabs(d.housePower) < 10.0f) w.flow4Active = false; }
    else { if (std::fabs(d.housePower) > 25.0f) w.flow4Active = true; }
    if (w.flow4Active) lv_obj_clear_flag(w.flowLine4, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(w.flowLine4, LV_OBJ_FLAG_HIDDEN);
  }
}

}  // namespace warpui
