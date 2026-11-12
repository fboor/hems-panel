// Home Energy Manager panel screen, built with LVGL. Shared by firmware and emulator.
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

// Switch the visible page (1..6). Used by the emulator to render a specific
// page for screenshots; the on-screen navigation calls the same helper.
void setPage(int page);

// One point restored from the SD log into the 24 h history. `ts` is the unix
// timestamp of the row; `heat` is -1 no data, 0 none, 1 SG1, 2 SG2, 3 both.
struct HistoryPoint {
  uint32_t ts;
  float grid;
  float pv;
  float house;
  float battery;
  float wb1;
  float wb2;
  float soc;
  int8_t heat;
};

// Replace the in-RAM 24 h history with rows read back from the SD log. The
// timestamps are anchored to the panel's own history clock, so the chart and
// the SG band can continue seamlessly with live samples. `nowEpoch` is the
// current wall clock in unix seconds (0 leaves the history untouched).
void seedHistory(const HistoryPoint *points, int count, uint32_t nowEpoch);

// Service page: SD-card status line (see storage/SdLog.h).
void setSdStatus(const char *text);

// Called when the user picks a charge mode on the wallbox page. `charger` is
// the index into WarpData::chargers, or -1 to apply the mode to every charger
// through the power manager. `mode` is a WARP ConfigChargeMode value.
using ChargeModeHandler = void (*)(int charger, int mode);
void setChargeModeHandler(ChargeModeHandler handler);

// Called when the user toggles the temporary SG-Ready override (party mode)
// on the heating page. `on` activates it for 15 minutes, off discards it.
using HeatingOverrideHandler = void (*)(bool on);
void setHeatingOverrideHandler(HeatingOverrideHandler handler);

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
