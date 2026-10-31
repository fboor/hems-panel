// WARP panel screen, built with LVGL. Shared by firmware and emulator.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <lvgl.h>

#include "warp/WarpData.h"

namespace warpui {

// Builds the screen below `parent` (usually lv_screen_active()).
void create(lv_obj_t *parent);

// Copies the values into the labels. Safe to call before create().
void update(const WarpData &data);

// Header status: dot colour and text ("Verbunden", "Keine Daten", ...).
void setStatus(const char *text, bool ok);

// Switch the visible page (1..4). Used by the emulator to render a specific
// page for screenshots; the on-screen navigation calls the same helper.
void setPage(int page);

}  // namespace warpui
