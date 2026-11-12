// JSON -> WarpData.
//
// The key names are configurable by editing the alias lists below. The defaults
// follow the flat object the panel expects from the Energy Manager, with a few
// common variants accepted so a renamed field does not blank the whole page.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <stddef.h>
#include <string.h>

#include "WarpData.h"
#include "WarpJson.h"

namespace warpparse {

inline bool numAny(const char *json, size_t len, const char *const *keys,
                   size_t n, float &out) {
  double d = 0.0;
  for (size_t i = 0; i < n; ++i) {
    if (warpjson::number(json, len, keys[i], d)) {
      out = static_cast<float>(d);
      return true;
    }
  }
  return false;
}

inline bool boolAny(const char *json, size_t len, const char *const *keys,
                    size_t n, bool &out) {
  bool b = false;
  for (size_t i = 0; i < n; ++i) {
    if (warpjson::boolean(json, len, keys[i], b)) {
      out = b;
      return true;
    }
  }
  return false;
}

inline bool strAny(const char *json, size_t len, const char *const *keys,
                   size_t n, char *out, size_t cap) {
  for (size_t i = 0; i < n; ++i) {
    if (warpjson::string(json, len, keys[i], out, cap)) {
      return true;
    }
  }
  return false;
}

#define WARP_KEYS(name, ...)                                      \
  static const char *const name[] = {__VA_ARGS__};                \
  constexpr size_t name##Count = sizeof(name) / sizeof(name[0])

// Parses a JSON document into d. Returns false only when the document contains
// no recognisable value at all, so a partial response still updates the page.
inline bool parse(const char *json, size_t len, WarpData &d) {
  if (json == nullptr || len == 0) {
    return false;
  }

  WARP_KEYS(kGridL1, "grid_power_l1", "grid_l1", "grid_power_L1", "power_grid_l1");
  WARP_KEYS(kGridL2, "grid_power_l2", "grid_l2", "grid_power_L2", "power_grid_l2");
  WARP_KEYS(kGridL3, "grid_power_l3", "grid_l3", "grid_power_L3", "power_grid_l3");
  WARP_KEYS(kGrid, "grid_exchange", "grid_power", "grid_power_total",
            "power_grid_total", "grid");
  WARP_KEYS(kPvA, "pv_power_a", "pv_a", "pv_power_1", "generator_power_a");
  WARP_KEYS(kPvB, "pv_power_b", "pv_b", "pv_power_2", "generator_power_b");
  WARP_KEYS(kPv, "pv_power", "pv_power_total", "generator_power",
            "power_pv_total");
  WARP_KEYS(kExt, "external_power", "s0_power", "external_power_total",
            "power_external");
  WARP_KEYS(kHouseL1, "household_power_l1", "house_power_l1",
            "household_l1", "power_house_l1");
  WARP_KEYS(kHouseL2, "household_power_l2", "house_power_l2",
            "household_l2", "power_house_l2");
  WARP_KEYS(kHouseL3, "household_power_l3", "house_power_l3",
            "household_l3", "power_house_l3");
  WARP_KEYS(kHouse, "household_power", "house_power", "household_power_total",
            "power_house_total");
  WARP_KEYS(kSoc, "battery_soc", "soc", "battery_state_of_charge");
  WARP_KEYS(kBatPower, "battery_power", "bat_power", "power_battery");
  WARP_KEYS(kBatVoltage, "battery_voltage", "bat_voltage", "voltage_battery");
  WARP_KEYS(kBatCurrent, "battery_current", "bat_current", "current_battery");
  WARP_KEYS(kIsland, "island_flag", "island", "island_mode", "is_grid_off");
  WARP_KEYS(kDayGen, "day_generation_wh", "day_generation", "day_pv_wh",
            "today_generation_wh");
  WARP_KEYS(kDayLoad, "day_load_wh", "day_load", "day_house_wh",
            "today_load_wh");
  WARP_KEYS(kDayFeedIn, "day_feed_in_wh", "day_feed_in", "day_export_wh",
            "today_feed_in_wh");
  WARP_KEYS(kDayDraw, "day_grid_draw_wh", "day_grid_draw", "day_import_wh",
            "today_grid_draw_wh");
  WARP_KEYS(kDayCharger, "day_charger_wh", "day_wallbox_wh",
            "today_wallbox_wh");
  WARP_KEYS(kChargerEnergy, "charger_energy_wh", "wallbox_energy_wh",
            "wallbox_total_wh", "charger_total_wh");
  WARP_KEYS(kPvEnergy, "pv_energy_wh", "pv_generation_wh", "pv_total_wh");
  WARP_KEYS(kHouseEnergy, "house_energy_wh", "household_energy_wh",
            "house_total_wh");
  WARP_KEYS(kGridImport, "grid_import_wh", "grid_draw_wh", "grid_load_wh");
  WARP_KEYS(kGridExport, "grid_export_wh", "grid_feed_in_wh");
  WARP_KEYS(kBatCharge, "battery_charge_wh", "bat_charge_wh");
  WARP_KEYS(kBatDischarge, "battery_discharge_wh", "bat_discharge_wh");
  WARP_KEYS(kGridVoltageL1, "grid_voltage_l1", "voltage_grid_l1");
  WARP_KEYS(kGridVoltageL2, "grid_voltage_l2", "voltage_grid_l2");
  WARP_KEYS(kGridVoltageL3, "grid_voltage_l3", "voltage_grid_l3");
  WARP_KEYS(kGridFrequency, "grid_frequency", "frequency_grid");
  WARP_KEYS(kAutarky, "autarky", "autarkie", "self_sufficiency");
  WARP_KEYS(kForecast, "forecast_today_wh", "solar_forecast_today_wh",
            "wh_today", "forecast_wh");
  WARP_KEYS(kForecastRemaining, "forecast_today_remaining_wh",
            "solar_forecast_today_remaining_wh", "wh_today_remaining");
  WARP_KEYS(kName, "device_name", "name", "hostname");
  WARP_KEYS(kFirmware, "firmware_version", "firmware", "version");

  bool found = false;

  found |= numAny(json, len, kGridL1, kGridL1Count, d.gridL1);
  found |= numAny(json, len, kGridL2, kGridL2Count, d.gridL2);
  found |= numAny(json, len, kGridL3, kGridL3Count, d.gridL3);
  found |= numAny(json, len, kGrid, kGridCount, d.gridPower);
  found |= numAny(json, len, kPvA, kPvACount, d.pvA);
  found |= numAny(json, len, kPvB, kPvBCount, d.pvB);
  found |= numAny(json, len, kPv, kPvCount, d.pvPower);
  found |= numAny(json, len, kExt, kExtCount, d.externalPower);
  found |= numAny(json, len, kHouseL1, kHouseL1Count, d.houseL1);
  found |= numAny(json, len, kHouseL2, kHouseL2Count, d.houseL2);
  found |= numAny(json, len, kHouseL3, kHouseL3Count, d.houseL3);
  found |= numAny(json, len, kHouse, kHouseCount, d.housePower);
  found |= numAny(json, len, kSoc, kSocCount, d.soc);
  found |= numAny(json, len, kBatPower, kBatPowerCount, d.batteryPower);
  found |= numAny(json, len, kBatVoltage, kBatVoltageCount, d.batteryVoltage);
  found |= numAny(json, len, kBatCurrent, kBatCurrentCount, d.batteryCurrent);
  found |= boolAny(json, len, kIsland, kIslandCount, d.island);
  found |= numAny(json, len, kDayGen, kDayGenCount, d.dayGenerationWh);
  found |= numAny(json, len, kDayLoad, kDayLoadCount, d.dayLoadWh);
  found |= numAny(json, len, kDayFeedIn, kDayFeedInCount, d.dayFeedInWh);
  found |= numAny(json, len, kDayDraw, kDayDrawCount, d.dayGridDrawWh);
  if (numAny(json, len, kDayCharger, kDayChargerCount, d.dayChargerWh)) {
    d.hasDayCharger = true;
    found = true;
  }
  found |= numAny(json, len, kPvEnergy, kPvEnergyCount, d.pvEnergyWh);
  found |= numAny(json, len, kHouseEnergy, kHouseEnergyCount, d.houseEnergyWh);
  found |= numAny(json, len, kGridImport, kGridImportCount, d.gridImportWh);
  found |= numAny(json, len, kGridExport, kGridExportCount, d.gridExportWh);
  found |= numAny(json, len, kBatCharge, kBatChargeCount, d.batteryChargeWh);
  found |= numAny(json, len, kBatDischarge, kBatDischargeCount,
                  d.batteryDischargeWh);
  if (numAny(json, len, kChargerEnergy, kChargerEnergyCount,
             d.chargerEnergyWh)) {
    d.hasChargerEnergy = true;
    found = true;
  }
  found |= numAny(json, len, kGridVoltageL1, kGridVoltageL1Count,
                  d.gridVoltageL1);
  found |= numAny(json, len, kGridVoltageL2, kGridVoltageL2Count,
                  d.gridVoltageL2);
  found |= numAny(json, len, kGridVoltageL3, kGridVoltageL3Count,
                  d.gridVoltageL3);
  found |= numAny(json, len, kGridFrequency, kGridFrequencyCount,
                  d.gridFrequency);
  if (numAny(json, len, kAutarky, kAutarkyCount, d.autarky)) {
    d.hasAutarky = true;
    found = true;
  }

  if (numAny(json, len, kForecast, kForecastCount, d.forecastTodayWh)) {
    d.hasForecast = d.forecastTodayWh >= 0.0f;
    found = true;
  }
  if (numAny(json, len, kForecastRemaining, kForecastRemainingCount,
             d.forecastTodayRemainingWh)) {
    d.hasForecastRemaining = d.forecastTodayRemainingWh >= 0.0f;
    found = true;
  }
  found |= strAny(json, len, kName, kNameCount, d.deviceName,
                  sizeof(d.deviceName));
  found |= strAny(json, len, kFirmware, kFirmwareCount, d.firmware,
                  sizeof(d.firmware));

  // Autarky fallback: calculate from energy counters if backend didn't provide it.
  if (!d.hasAutarky && (d.houseEnergyWh > 0.0f)) {
      float autarky =  (d.houseEnergyWh - d.gridImportWh) / d.houseEnergyWh * 100.0f;
      if (autarky < 0.0f) autarky = 0.0f;
      if (autarky > 100.0f) autarky = 100.0f;
      d.autarky = autarky;
      d.hasAutarky = true;
  }

  // Totals that the device may omit are sums of the phases it did send.
  if (!numAny(json, len, kGrid, kGridCount, d.gridPower) ||
      (d.gridPower == 0.0f && (d.gridL1 || d.gridL2 || d.gridL3))) {
    d.gridPower = d.gridL1 + d.gridL2 + d.gridL3;
  }
  if (!numAny(json, len, kPv, kPvCount, d.pvPower) ||
      (d.pvPower == 0.0f && (d.pvA || d.pvB))) {
    d.pvPower = d.pvA + d.pvB;
  }
  if (!numAny(json, len, kHouse, kHouseCount, d.housePower) ||
      (d.housePower == 0.0f && (d.houseL1 || d.houseL2 || d.houseL3))) {
    d.housePower = d.houseL1 + d.houseL2 + d.houseL3;
  }

  if (found) {
    d.valid = true;
    d.hasGrid = d.gridPower != 0.0f || d.gridL1 != 0.0f ||
                d.gridL2 != 0.0f || d.gridL3 != 0.0f;
    d.hasHouse = d.housePower != 0.0f || d.houseL1 != 0.0f ||
                 d.houseL2 != 0.0f || d.houseL3 != 0.0f;
    d.hasPv = d.pvPower != 0.0f || d.pvA != 0.0f || d.pvB != 0.0f;
    d.hasPvStrings = d.pvA != 0.0f || d.pvB != 0.0f;
    d.hasBattery = d.soc != 0.0f || d.batteryPower != 0.0f ||
                   d.batteryVoltage != 0.0f;
    d.hasExternal = d.externalPower != 0.0f;
  }
  return found;
}

#undef WARP_KEYS

}  // namespace warpparse
