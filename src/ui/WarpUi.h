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

// Header connection state. One compact item replaces the old dot + text pair:
// no WLAN, WLAN up but WARP unreachable, data flowing, or reachable but empty.
enum class ConnectionState {
  NoNet,
  Disconnected,
  Data,
  NoData,
};

void setConnectionState(ConnectionState state);

// Switch the visible page (1..5). Used by the emulator to render a specific
// page for screenshots; the on-screen navigation calls the same helper.
void setPage(int page);

// Called when the user picks a charge mode on the wallbox page. `charger` is
// the index into WarpData::chargers, or -1 to apply the mode to every charger
// through the power manager. `mode` is a WARP ConfigChargeMode value.
using ChargeModeHandler = void (*)(int charger, int mode);
void setChargeModeHandler(ChargeModeHandler handler);

// First-boot Wi-Fi setup overlay. The setup access point is open, and the QR
// code contains its SSID, so a phone can join without typing anything. The
// overlay is created on first use and stays allocated (hidden) afterwards.
void showSetup(const char *ssid, const char *password, const char *url);
void hideSetup();

// Service page: current WLAN, panel IP, WARP endpoint and web setup URL.
// Called again whenever one of these values changes.
void setNetworkInfo(const char *ssid, const char *ip, const char *warpUrl,
                    const char *webUrl);

}  // namespace warpui
