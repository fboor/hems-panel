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

// First-boot Wi-Fi setup overlay. The QR code contains the setup access point
// credentials, so a phone can join without typing the password. The overlay is
// created on first use and stays allocated (hidden) afterwards.
void showSetup(const char *ssid, const char *password, const char *url);
void hideSetup();

// Service page: current WLAN, panel IP, WARP endpoint and web setup URL.
// Called again whenever one of these values changes.
void setNetworkInfo(const char *ssid, const char *ip, const char *warpUrl,
                    const char *webUrl);

}  // namespace warpui
