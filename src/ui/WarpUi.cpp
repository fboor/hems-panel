// SPDX-License-Identifier: MIT
#include "WarpUi.h"

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
    snprintf(out, n, "%.1f kWh", wh / 1000.0);
  } else {
    snprintf(out, n, "%.0f Wh", wh);
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
  label(parent, 16, 34, 0, F_SMALL, C_MUTED,
        "Energy Manager V2  ·  Live-Werte");

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

  // Grid ---------------------------------------------------------------
  lv_obj_t *grid = card(parent, 12, 68, 250, 176);
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
  lv_obj_t *house = card(parent, 274, 68, 250, 176);
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
  lv_obj_t *bat = card(parent, 536, 68, 250, 176);
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
  lv_obj_t *pv = card(parent, 12, 256, 250, 176);
  title(pv, "PV");
  w.pvTotal = label(pv, 16, 30, 0, F_BIG, C_GREEN, "--");
  label(pv, 16, 76, 0, F_SMALL, C_MUTED, "Erzeugung");
  row(pv, 16, 108, 218, "String A", &w.pvA);
  row(pv, 16, 134, 218, "String B", &w.pvB);

  // Wallbox -----------------------------------------------------------
  lv_obj_t *wallbox = card(parent, 274, 256, 250, 176);
  title(wallbox, "Wallbox");
  w.wallboxValue = label(wallbox, 16, 30, 0, F_BIG, C_GREEN, "--");
  w.wallboxNote = label(wallbox, 16, 76, 0, F_SMALL, C_MUTED, "--");
  w.wallboxMode = label(wallbox, 16, 96, 0, F_SMALL, C_MUTED, "--");

  // Energy totals ------------------------------------------------------
  lv_obj_t *energy = card(parent, 536, 256, 250, 176);
  title(energy, "ENERGIE");
  row(energy, 16, 30, 218, "PV", &w.energyPv, F_SMALL);
  row(energy, 16, 52, 218, "Haus", &w.energyHouse, F_SMALL);
  row(energy, 16, 74, 218, "Netzbezug", &w.energyImport, F_SMALL);
  row(energy, 16, 96, 218, "Einspeisung", &w.energyExport, F_SMALL);
  row(energy, 16, 118, 218, "Akku L/E", &w.energyBattery, F_SMALL);
  label(energy, 16, 144, 218, F_SMALL, C_MUTED, "seit Inbetriebnahme",
        LV_TEXT_ALIGN_RIGHT);

  // Footer -------------------------------------------------------------
  w.updatedText = label(parent, 16, 446, 0, F_SMALL, C_MUTED,
                        "Letzte Aktualisierung: --");
  label(parent, 600, 446, 184, F_SMALL, C_MUTED, "Abfrage alle 10 s",
        LV_TEXT_ALIGN_RIGHT);
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
  if (d.charge_power > 0.1f) {
    snprintf(chargeBuf, sizeof(chargeBuf), "%.1f W", d.charge_power);
    setText(w.wallboxValue, chargeBuf);
    if (d.charge_state == 2) {
      setText(w.wallboxNote, "Laden");
    } else if (d.charge_state == 1) {
      setText(w.wallboxNote, "Bereit");
    } else if (d.charge_state == 3) {
      setText(w.wallboxNote, "Fehler");
    } else {
      setText(w.wallboxNote, "Aktiv");
    }
  } else {
    /* Check if a wallbox charger slot is configured */
    if (d.charge_name[0] != '\0') {
      snprintf(chargeBuf, sizeof(chargeBuf), "--");
      setText(w.wallboxValue, chargeBuf);
  if (d.charge_state == 1) {
    setText(w.wallboxNote, "Wartet auf Freigabe");
  } else {
    setText(w.wallboxNote, "Konfiguriert");
  }
    } else {
      setText(w.wallboxValue, "--");
      setText(w.wallboxNote, "Getrennt");
    }
  }
  const char *modeStr = d.charge_mode == 0 ? "Schnell" :
                        d.charge_mode == 1 ? "Aus" :
                        d.charge_mode == 2 ? "PV" :
                        d.charge_mode == 3 ? "Min+PV" : "?";
  snprintf(chargeBuf, sizeof(chargeBuf), "Modus: %s", modeStr);
  setText(w.wallboxMode, chargeBuf);
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

  // Footer -------------------------------------------------------------
  if (w.updatedText != nullptr) {
    uint32_t ageS = 0;
    if (d.lastUpdateMs > 0) {
      ageS = (lv_tick_get() - d.lastUpdateMs) / 1000u;
    }
    snprintf(buf, sizeof(buf), "Letzte Aktualisierung: vor %u s", ageS);
    setText(w.updatedText, buf);
  }
}

}  // namespace warpui
