// Complete colour schemes for the panel. A theme owns every surface, text and
// accent colour so switching it restyles the whole screen, not just the page
// background. The chart and the history bands keep their data colours: only
// their surrounding surface follows the theme.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>

namespace hemsui {

enum class ThemeId : uint8_t {
  Dark = 0,
  Light = 1,
};

// Series colours are shared by every theme on purpose. A series must keep its
// identity when the surrounding surface changes; the chart image, its LVGL
// fallback and the legend all use this one table.
constexpr uint32_t kSeriesColors[7] = {
    0x2CA02C, 0xFF7F0E, 0x007BFF, 0xD62728,
    0x9467BD, 0x8C564B, 0xC9CED4,
};

struct Theme {
  const char *nameDe;
  const char *nameEn;
  // The photo only suits the dark palette. The light palette draws a simple
  // schematic instead.
  bool photoBackground;

  uint32_t bg;
  uint32_t card;
  uint32_t card2;
  uint32_t line;
  uint32_t text;
  uint32_t muted;

  // Text directly on the page background needs a darker shade on light
  // surfaces; text inside cards keeps the normal palette colours.
  uint32_t onBackground;
  uint32_t mutedOnBackground;

  // Selection and button colours, including inactive buttons.
  uint32_t accent;
  uint32_t onAccent;
  uint32_t buttonBg;
  uint32_t buttonText;
  uint32_t activeButtonBg;
  uint32_t activeButtonText;
  uint32_t inactiveButtonText;

  // Semantic and energy-flow identity colours.
  uint32_t green;
  uint32_t red;
  uint32_t grid;
  uint32_t amber;
  uint32_t solar;
  uint32_t battery;
  uint32_t ev;
  uint32_t solarDim;
  uint32_t gridDim;
  uint32_t batteryDim;
  uint32_t evDim;
  uint32_t solarHi;
  uint32_t gridHi;
  uint32_t batteryHi;
  uint32_t evHi;

  // Connection chip surfaces.
  uint32_t chipDisconnected;
  uint32_t chipData;
  uint32_t chipNoData;

  // Backdrop drawn where no photograph is shown. It has the photo's 800x356
  // geometry and is tinted per theme, so the same artwork works on every page.
  uint32_t backdrop;
  uint32_t backdropFill;
  uint32_t backdropLine;
};

constexpr int kThemeCount = 2;

inline const Theme &theme(ThemeId id) {
  static const Theme themes[kThemeCount] = {
      // Dark: the original panel palette.
      {
          "Dunkel", "Dark", true,
          0x0E1319, 0x171F29, 0x222D39, 0x2B3947, 0xE9F0F7, 0x8899AA,
          0xE9F0F7, 0x8899AA,
          0x4AA3FF, 0xFFFFFF,
          0x222D39, 0xE9F0F7, 0x4AA3FF, 0xFFFFFF, 0x8899AA,
          0x3AD07F, 0xFF5D5D, 0x4AA3FF, 0xFFB347,
          0xFFC247, 0x3AD07F, 0xB388FF,
          0x3A2E12, 0x142A40, 0x123225, 0x2A1E44,
          0xFFF0B8, 0xCFE8FF, 0xCFFFE4, 0xE9DBFF,
          0x2A1D22, 0x173026, 0x33280F,
          0x10171F, 0x171F29, 0x34465A,
      },
      // Light: white cards on a grey-green surface, green as the accent.
      {
          "Hell", "Light", false,
          0xE7ECE8, 0xFFFFFF, 0xF1F5F2, 0xC6D1C9, 0x1D2A22, 0x607166,
          0x1D2A22, 0x52665A,
          0x2E8B57, 0xFFFFFF,
          0xF1F5F2, 0x1D2A22, 0x2E8B57, 0xFFFFFF, 0x607166,
          0x2E8B57, 0xB23A34, 0x2C6E8E, 0xB07A1E,
          0xC98A1B, 0x2E8B57, 0x7755A8,
          0xF6EBD5, 0xDDEAF1, 0xDDEEE4, 0xE8E0F2,
          0x7A5410, 0x16435C, 0x145033, 0x4B3470,
          0xF3DEDC, 0xDDEEE4, 0xF5E8CF,
          0xE7ECE8, 0xF4F7F5, 0xB6C4BA,
      },
  };
  const unsigned i = (unsigned)id < kThemeCount ? (unsigned)id : 0u;
  return themes[i];
}

// NVS stores a small integer, so clamp it to a known palette on read.
inline ThemeId themeId(int value) {
  if (value < 0 || value >= kThemeCount) {
    return ThemeId::Dark;
  }
  return (ThemeId)(uint8_t)value;
}

inline const char *themeName(ThemeId id, bool english) {
  const Theme &t = theme(id);
  return english ? t.nameEn : t.nameDe;
}

}  // namespace hemsui
