// WARP Energy Manager V2 panel - data model.
//
// One struct, filled from the device's JSON and read by the UI. Kept free of
// Arduino and LVGL so the firmware and the desktop emulator share it verbatim.
#pragma once

#include <stdint.h>

struct WarpData {
  bool valid = false;
  uint32_t lastUpdateMs = 0;

  // Grid, W. Positive = import, negative = export.
  float gridL1 = 0.0f;
  float gridL2 = 0.0f;
  float gridL3 = 0.0f;
  float gridPower = 0.0f;      // total exchange

  // PV, W.
  float pvA = 0.0f;
  float pvB = 0.0f;
  float pvPower = 0.0f;

  // External / S0 meter, W.
  float externalPower = 0.0f;

  // Household, W.
  float houseL1 = 0.0f;
  float houseL2 = 0.0f;
  float houseL3 = 0.0f;
  float housePower = 0.0f;

  // Battery.
  float soc = 0.0f;            // %
  float batteryPower = 0.0f;   // W, positive = charging
  float batteryVoltage = 0.0f; // V
  float batteryCurrent = 0.0f; // A

  // Charge manager / Wallbox.
  int charge_state = 0;        // 0=unknown, 1=ready, 2=charging, 3=error
  float charge_power = 0.0f;   // W from charge_manager/state
  double charge_current = 0.0;      // mA from charge_manager/state
  int charge_status = 0;       // 0=stopped, 1=charging, 2=complete, 3=error
  char charge_name[40] = "";   // Charger name from API
  int charge_mode = 0;         // 0=Fast,1=Off,2=PV,3=Min+PV from power_manager/charge_mode

  // Grid connection.
  bool island = false;
  bool is_3phase = false;

  // Energy counters since commissioning, Wh. Filled from the meter value IDs on
  // a real WARP device; the day_* aliases are the fallback for flat JSON.
  float pvEnergyWh = 0.0f;
  float pvAEnergyWh = 0.0f;
  float pvBEnergyWh = 0.0f;
  float houseEnergyWh = 0.0f;
  float gridImportWh = 0.0f;
  float gridExportWh = 0.0f;
  float batteryChargeWh = 0.0f;
  float batteryDischargeWh = 0.0f;

  // Day counters, Wh (flat JSON only).
  float dayGenerationWh = 0.0f;
  float dayLoadWh = 0.0f;
  float dayFeedInWh = 0.0f;
  float dayGridDrawWh = 0.0f;

  // Grid voltage / frequency, V / Hz.
  float gridVoltageL1 = 0.0f;
  float gridVoltageL2 = 0.0f;
  float gridVoltageL3 = 0.0f;
  float gridFrequency = 0.0f;

  // Autarky: share of consumption supplied by PV/battery instead of the grid,
  // in percent. Calculated from the cumulative energy counters when the device
  // provides them, otherwise from the current house/grid power.
  bool hasAutarky = false;
  float autarky = 0.0f;

  // Solar forecast for today, Wh. WARP reports -1 when the forecast module has
  // no usable data, so hasForecast distinguishes "0 Wh" from "not available".
  bool hasForecast = false;
  float forecastTodayWh = 0.0f;

  // Which parts of the page have a usable source. The UI uses these to show a
  // dash instead of a misleading zero when a meter is not configured.
  bool hasGrid = false;
  bool hasHouse = false;
  bool hasPv = false;
  bool hasPvStrings = false;
  bool hasBattery = false;
  bool hasExternal = false;

  // Identity.
  char deviceName[40] = "";
  char firmware[24] = "";
};
