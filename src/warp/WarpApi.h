// WARP Energy Manager V2 module API -> WarpData.
//
// A real WARP device does not have the flat /json endpoint; it exposes one
// object per module (/info/name, /meters/N/config, /meters/N/value_ids,
// /meters/N/values, /power_manager/low_level_state, ...). This adapter
// discovers the configured meter slots, maps them by their WARP meter location
// (Grid, Battery, Load, PV) and reads the values through the MeterValueIDs.
//
// The transport is an interface so the same adapter runs over HTTPClient in the
// firmware and over POSIX sockets in the emulator. No Arduino or LVGL headers
// are used here.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cmath>
#include <string>
#include <vector>

#include "WarpData.h"
#include "WarpJson.h"

namespace warpapi {

// WARP "Meter Location" enum (software/src/modules/meters/).
enum Location : int {
  LOC_UNKNOWN = 0,
  LOC_OTHER = 1,
  LOC_CHARGER = 2,
  LOC_INVERTER = 3,
  LOC_GRID = 4,
  LOC_BATTERY = 5,
  LOC_LOAD = 6,
  LOC_PV = 7,
  LOC_VEHICLE = 8,
};

enum Role : int {
  ROLE_NONE = 0,
  ROLE_GRID,
  ROLE_LOAD,
  ROLE_PV,
  ROLE_BATTERY,
  ROLE_EXTERNAL,
};

// MeterValueIDs used by the panel. The CSV is in the WARP source tree:
// software/src/modules/meters/meter_value_id.csv
enum ValueId : int {
  VID_VOLTAGE_L1 = 1,
  VID_VOLTAGE_L2 = 2,
  VID_VOLTAGE_L3 = 3,
  VID_VOLTAGE_DC = 10,
  VID_POWER_L1 = 39,
  VID_POWER_L2 = 48,
  VID_POWER_L3 = 57,
  VID_POWER_TOTAL = 74,
  VID_GRID_IMPORT = 209,
  VID_GRID_EXPORT = 211,
  VID_HOUSE_ENERGY = 215,
  VID_FREQUENCY_L1 = 361,
  VID_FREQUENCY_L2 = 362,
  VID_FREQUENCY_L3 = 363,
  VID_TEMPERATURE = 381,
  VID_SOC = 387,
  VID_CURRENT_DC = 396,
  VID_POWER_DC = 404,
  VID_BATTERY_CHARGE = 413,
  VID_BATTERY_DISCHARGE = 415,
  VID_PV1_VOLTAGE = 421,
  VID_PV2_VOLTAGE = 422,
  VID_PV_AVG_VOLTAGE = 430,
  VID_PV1_POWER = 442,
  VID_PV2_POWER = 443,
  VID_PV_POWER_EXPORT = 452,
  VID_PV_POWER_IMEX = 453,
  VID_PV1_ENERGY = 454,
  VID_PV2_ENERGY = 456,
  VID_PV_ENERGY = 472,
};

class Transport {
 public:
  virtual ~Transport() = default;

  // GETs url and returns true for a complete 2xx response. error is filled on
  // failure and may be empty if the transport has no detail.
  virtual bool get(const std::string &url, std::string &body,
                   std::string &error) = 0;
};

struct Slot {
  int slot = -1;
  int location = LOC_UNKNOWN;
  int virtualMeter = 0;
  Role role = ROLE_NONE;
  char name[40] = "";
  std::vector<int> valueIds;
  std::vector<double> values;
};

inline std::string normalizeBase(const std::string &url) {
  std::string s = url;
  const size_t fragment = s.find_first_of("?#");
  if (fragment != std::string::npos) {
    s.erase(fragment);
  }
  while (!s.empty() && s.back() == '/') {
    s.pop_back();
  }
  if (s.size() >= 5 && s.compare(s.size() - 5, 5, "/json") == 0) {
    s.erase(s.size() - 5);
  }
  return s;
}

// Keeps a query string such as ?scenario=island and appends it to every module
// request, so the emulator mock can select a scenario from the --url argument.
inline std::string extractQuery(const std::string &url) {
  const size_t begin = url.find('?');
  if (begin == std::string::npos) {
    return "";
  }
  std::string query = url.substr(begin);
  const size_t fragment = query.find('#');
  if (fragment != std::string::npos) {
    query.erase(fragment);
  }
  return query;
}

inline bool containsCI(const char *text, const char *needle) {
  if (text == nullptr || needle == nullptr || *needle == '\0') {
    return false;
  }
  const size_t n = strlen(needle);
  for (const char *p = text; *p != '\0'; ++p) {
    size_t i = 0;
    while (i < n && p[i] != '\0' &&
           tolower(static_cast<unsigned char>(p[i])) ==
               tolower(static_cast<unsigned char>(needle[i]))) {
      ++i;
    }
    if (i == n) {
      return true;
    }
  }
  return false;
}

inline Role roleForLocation(int location) {
  switch (location) {
    case LOC_GRID:
      return ROLE_GRID;
    case LOC_BATTERY:
      return ROLE_BATTERY;
    case LOC_LOAD:
      return ROLE_LOAD;
    case LOC_PV:
      return ROLE_PV;
    case LOC_INVERTER:
      return ROLE_PV;
    case LOC_CHARGER:
    case LOC_VEHICLE:
    case LOC_OTHER:
      return ROLE_EXTERNAL;
    default:
      return ROLE_NONE;
  }
}

inline Role roleForVirtualMeter(int virtualMeter) {
  switch (virtualMeter) {
    case 1:
      return ROLE_PV;  // RCT inverter
    case 2:
      return ROLE_GRID;
    case 3:
      return ROLE_BATTERY;
    case 4:
      return ROLE_LOAD;
    case 5:
      return ROLE_PV;
    default:
      return ROLE_NONE;
  }
}

inline Role roleForName(const char *name) {
  if (containsCI(name, "netz") || containsCI(name, "grid")) {
    return ROLE_GRID;
  }
  if (containsCI(name, "akku") || containsCI(name, "battery") ||
      containsCI(name, "speicher")) {
    return ROLE_BATTERY;
  }
  if (containsCI(name, "pv") || containsCI(name, "solar") ||
      containsCI(name, "erzeug") || containsCI(name, "generat")) {
    return ROLE_PV;
  }
  if (containsCI(name, "haus") || containsCI(name, "verbrauch") ||
      containsCI(name, "load") || containsCI(name, "consum")) {
    return ROLE_LOAD;
  }
  return ROLE_NONE;
}

class Reader {
 public:
  Reader(Transport &transport, const std::string &url)
      : transport_(transport),
        base_(normalizeBase(url)),
        query_(extractQuery(url)) {}

  const std::string &base() const { return base_; }
  const char *deviceName() const { return deviceName_; }
  const char *firmware() const { return firmware_; }
  const std::vector<Slot> &slots() const { return slots_; }

  // Reads /info/name, /info/version and the configuration of all seven meter
  // slots. Returns false when this is not a WARP module API. The result is
  // cached; poll() calls discover() itself if necessary.
  bool discover() {
    slots_.clear();
    deviceName_[0] = '\0';
    firmware_[0] = '\0';

    std::string body;
    std::string error;
    if (!get("/info/name", body, error)) {
      return false;
    }
    if (!warpjson::string(body.data(), body.size(), "name", deviceName_,
                          sizeof(deviceName_))) {
      warpjson::string(body.data(), body.size(), "display_name", deviceName_,
                       sizeof(deviceName_));
    }
    if (get("/info/version", body, error)) {
      warpjson::string(body.data(), body.size(), "firmware", firmware_,
                       sizeof(firmware_));
    }

    for (int i = 0; i < 7; ++i) {
      const std::string path = "/meters/" + std::to_string(i);
      if (!get(path + "/config", body, error)) {
        continue;
      }

      int location = LOC_UNKNOWN;
      int virtualMeter = 0;
      const bool haveLocation =
          warpjson::integer(body.data(), body.size(), "location", location);
      const bool haveVirtual = warpjson::integer(body.data(), body.size(),
                                                 "virtual_meter", virtualMeter);
      if (!haveLocation && !haveVirtual) {
        continue;  // [0, null] = empty slot
      }

      Slot slot;
      slot.slot = i;
      slot.location = haveLocation ? location : LOC_UNKNOWN;
      slot.virtualMeter = haveVirtual ? virtualMeter : 0;
      warpjson::string(body.data(), body.size(), "display_name", slot.name,
                       sizeof(slot.name));

      slot.role = roleForLocation(slot.location);
      if (slot.role == ROLE_NONE) {
        slot.role = roleForVirtualMeter(slot.virtualMeter);
      }
      if (slot.role == ROLE_NONE) {
        slot.role = roleForName(slot.name);
      }
      if (slot.role == ROLE_NONE) {
        slot.role = ROLE_EXTERNAL;
      }

      if (!get(path + "/value_ids", body, error)) {
        continue;
      }
      double ids[64];
      size_t count = 0;
      if (!warpjson::topNumbers(body.data(), body.size(), ids, 64, count)) {
        continue;
      }
      for (size_t k = 0; k < count; ++k) {
        if (!std::isnan(ids[k])) {
          slot.valueIds.push_back(static_cast<int>(ids[k]));
        }
      }
      if (slot.valueIds.empty()) {
        continue;
      }
      slots_.push_back(std::move(slot));
    }

    return !slots_.empty();
  }

  // Fetches the current values of every discovered slot plus the power manager
  // state. Returns false if no endpoint produced usable data.
  bool poll(WarpData &d, std::string &errorOut) {
    errorOut.clear();
    if (slots_.empty() && !discover()) {
      errorOut = "kein WARP-Modul (info/name oder meters)";
      return false;
    }

    d = WarpData();
    snprintf(d.deviceName, sizeof(d.deviceName), "%s", deviceName_);
    snprintf(d.firmware, sizeof(d.firmware), "%s", firmware_);

    bool anyMeter = false;
    for (Slot &slot : slots_) {
      std::string body;
      std::string error;
      const std::string path =
          "/meters/" + std::to_string(slot.slot) + "/values";
      if (!get(path, body, error)) {
        slot.values.clear();
        continue;
      }
      double values[64];
      size_t count = 0;
      if (!warpjson::topNumbers(body.data(), body.size(), values, 64, count)) {
        slot.values.clear();
        continue;
      }
      slot.values.assign(values, values + count);
      anyMeter = true;
    }

    double powerAtMeter = NAN;
    double powerAtBattery = NAN;
    double batterySoc = NAN;
    bool powerManager = false;
    {
      std::string body;
      std::string error;
      if (get("/power_manager/low_level_state", body, error)) {
        int externalControl = 0;
        (void)externalControl;
        warpjson::number(body.data(), body.size(), "power_at_meter",
                         powerAtMeter);
        warpjson::number(body.data(), body.size(), "power_at_battery",
                         powerAtBattery);
        warpjson::number(body.data(), body.size(), "battery_soc", batterySoc);
        powerManager = true;
      }
      // parse is_3phase
      int is3 = 0;
      if (warpjson::integer(body.data(), body.size(), "is_3phase", is3)) {
        d.is_3phase = (is3 != 0);
      }
    }

    {
      std::string body;
      std::string error;
      if (get("/power_manager/charge_mode", body, error)) {
        int mode = 0;
        if (warpjson::integer(body.data(), body.size(), "mode", mode)) {
          d.charge_mode = mode;
        }
      }
    }

    /* Charge manager / Wallbox data from /charge_manager/state */
    int chargeState = 0;          // 0=unknown, 1=ready, 2=charging, 3=error
    double chargePower = 0.0;     // W
    double charge_current = 0.0;  // mA
    int chargeStatus = 0;         // 0=stopped, 1=charging, 2=complete, 3=error
    char chargeName[40] = "";
    {
      std::string body;
      std::string error;
      if (get("/charge_manager/state", body, error)) {
        // charger state
        warpjson::integer(body.data(), body.size(), "state", chargeState);
        // charger power in W
        warpjson::number(body.data(), body.size(), "ap", chargePower);
        // charger current in mA
        warpjson::number(body.data(), body.size(), "ac", charge_current);
        // status charge
        warpjson::integer(body.data(), body.size(), "sc", chargeStatus);
        // charger name
        warpjson::string(body.data(), body.size(), "n", chargeName,
                         sizeof(chargeName));
      }
    }

    if (!anyMeter && !powerManager) {
      errorOut = "keine Messwerte";
      return false;
    }

    bool gridPowerFound = false;
    bool housePowerFound = false;
    bool pvPowerFound = false;
    bool batteryPowerFound = false;
    bool batterySocFound = false;
    bool gridVoltageFound = false;

    for (const Slot &slot : slots_) {
      bool roleFound = false;

      switch (slot.role) {
        case ROLE_GRID: {
          double v = 0.0;
          if (value(slot, VID_POWER_L1, v)) {
            d.gridL1 += static_cast<float>(v);
            roleFound = true;
          }
          if (value(slot, VID_POWER_L2, v)) {
            d.gridL2 += static_cast<float>(v);
            roleFound = true;
          }
          if (value(slot, VID_POWER_L3, v)) {
            d.gridL3 += static_cast<float>(v);
            roleFound = true;
          }
          if (value(slot, VID_POWER_TOTAL, v)) {
            d.gridPower += static_cast<float>(v);
            gridPowerFound = true;
            roleFound = true;
          }
          if (value(slot, VID_GRID_IMPORT, v)) {
            d.gridImportWh = static_cast<float>(v);
            roleFound = true;
          }
          if (value(slot, VID_GRID_EXPORT, v)) {
            d.gridExportWh = static_cast<float>(v);
            roleFound = true;
          }
          if (value(slot, VID_VOLTAGE_L1, v)) {
            if (!gridVoltageFound) {
              d.gridVoltageL1 = static_cast<float>(v);
            }
            gridVoltageFound = true;
            roleFound = true;
          }
          if (value(slot, VID_VOLTAGE_L2, v)) {
            if (d.gridVoltageL2 == 0.0f) {
              d.gridVoltageL2 = static_cast<float>(v);
            }
            roleFound = true;
          }
          if (value(slot, VID_VOLTAGE_L3, v)) {
            if (d.gridVoltageL3 == 0.0f) {
              d.gridVoltageL3 = static_cast<float>(v);
            }
            roleFound = true;
          }
          if (value(slot, VID_FREQUENCY_L1, v)) {
            d.gridFrequency = static_cast<float>(v);
            roleFound = true;
          }
          break;
        }

        case ROLE_LOAD: {
          double v = 0.0;
          if (value(slot, VID_POWER_L1, v)) {
            d.houseL1 += static_cast<float>(v);
            roleFound = true;
          }
          if (value(slot, VID_POWER_L2, v)) {
            d.houseL2 += static_cast<float>(v);
            roleFound = true;
          }
          if (value(slot, VID_POWER_L3, v)) {
            d.houseL3 += static_cast<float>(v);
            roleFound = true;
          }
          if (value(slot, VID_POWER_TOTAL, v)) {
            d.housePower += static_cast<float>(v);
            housePowerFound = true;
            roleFound = true;
          }
          if (value(slot, VID_HOUSE_ENERGY, v)) {
            d.houseEnergyWh = static_cast<float>(v);
            roleFound = true;
          } else {
            double imported = 0.0;
            double exported = 0.0;
            if (value(slot, VID_GRID_IMPORT, imported) &&
                value(slot, VID_GRID_EXPORT, exported)) {
              d.houseEnergyWh = static_cast<float>(imported - exported);
              roleFound = true;
            }
          }
          break;
        }

        case ROLE_PV: {
          double v = 0.0;
          if (value(slot, VID_PV1_POWER, v)) {
            d.pvA += static_cast<float>(v);
            d.hasPvStrings = true;
            roleFound = true;
          }
          if (value(slot, VID_PV2_POWER, v)) {
            d.pvB += static_cast<float>(v);
            d.hasPvStrings = true;
            roleFound = true;
          }
          if (value(slot, VID_PV_POWER_EXPORT, v)) {
            d.pvPower += static_cast<float>(v);
            pvPowerFound = true;
            roleFound = true;
          } else if (value(slot, VID_PV_POWER_IMEX, v)) {
            d.pvPower += static_cast<float>(std::fabs(v));
            pvPowerFound = true;
            roleFound = true;
          }
          if (value(slot, VID_PV_ENERGY, v)) {
            d.pvEnergyWh = static_cast<float>(v);
            roleFound = true;
          }
          if (value(slot, VID_PV1_ENERGY, v)) {
            d.pvAEnergyWh = static_cast<float>(v);
            roleFound = true;
          }
          if (value(slot, VID_PV2_ENERGY, v)) {
            d.pvBEnergyWh = static_cast<float>(v);
            roleFound = true;
          }
          break;
        }

        case ROLE_BATTERY: {
          double v = 0.0;
          if (value(slot, VID_POWER_DC, v)) {
            d.batteryPower += static_cast<float>(v);
            batteryPowerFound = true;
            roleFound = true;
          }
          if (value(slot, VID_SOC, v)) {
            if (!batterySocFound) {
              d.soc = static_cast<float>(v);
            }
            batterySocFound = true;
            roleFound = true;
          }
          if (value(slot, VID_VOLTAGE_DC, v)) {
            if (d.batteryVoltage == 0.0f) {
              d.batteryVoltage = static_cast<float>(v);
            }
            roleFound = true;
          }
          if (value(slot, VID_CURRENT_DC, v)) {
            if (d.batteryCurrent == 0.0f) {
              d.batteryCurrent = static_cast<float>(v);
            }
            roleFound = true;
          }
          if (value(slot, VID_BATTERY_CHARGE, v)) {
            d.batteryChargeWh = static_cast<float>(v);
            roleFound = true;
          }
          if (value(slot, VID_BATTERY_DISCHARGE, v)) {
            d.batteryDischargeWh = static_cast<float>(v);
            roleFound = true;
          }
          break;
        }

        case ROLE_EXTERNAL: {
          double v = 0.0;
          if (value(slot, VID_POWER_TOTAL, v)) {
            d.externalPower += static_cast<float>(v);
            roleFound = true;
          }
          break;
        }

        case ROLE_NONE:
          break;
      }

      if (roleFound) {
        switch (slot.role) {
          case ROLE_GRID:
            d.hasGrid = true;
            break;
          case ROLE_LOAD:
            d.hasHouse = true;
            break;
          case ROLE_PV:
            d.hasPv = true;
            break;
          case ROLE_BATTERY:
            d.hasBattery = true;
            break;
          case ROLE_EXTERNAL:
            d.hasExternal = true;
            break;
          default:
            break;
        }
      }
    }

    // Fall back to the power manager for the grid and battery if no meter slot
    // supplied the value. A WARP device can be configured with only a battery
    // and no separate grid meter.
    if (!gridPowerFound && !std::isnan(powerAtMeter)) {
      d.gridPower = static_cast<float>(powerAtMeter);
      d.hasGrid = true;
    }
    if (!batteryPowerFound && !std::isnan(powerAtBattery)) {
      d.batteryPower = static_cast<float>(powerAtBattery);
      d.hasBattery = true;
    }
    if (!batterySocFound && !std::isnan(batterySoc)) {
      d.soc = static_cast<float>(batterySoc);
      d.hasBattery = true;
    }

    /* Charge manager / Wallbox: map /charge_manager/state to WarpData */
    d.charge_state = chargeState;
    d.charge_power = chargePower;
    d.charge_current = charge_current;
    d.charge_status = chargeStatus;
    snprintf(d.charge_name, sizeof(d.charge_name), "%s", chargeName);

    // Totals that the device may omit are sums of the phases/strings.
    if (!gridPowerFound && (d.gridL1 != 0.0f || d.gridL2 != 0.0f ||
                            d.gridL3 != 0.0f)) {
      d.gridPower = d.gridL1 + d.gridL2 + d.gridL3;
    }
    if (!housePowerFound && (d.houseL1 != 0.0f || d.houseL2 != 0.0f ||
                             d.houseL3 != 0.0f)) {
      d.housePower = d.houseL1 + d.houseL2 + d.houseL3;
    }
    if (!pvPowerFound) {
      d.pvPower = d.pvA + d.pvB;
    }

    // A grid meter that reports zero voltage while PV or the battery is active
    // is the reliable sign of an island situation on this hardware.
    if (gridVoltageFound && d.gridVoltageL1 < 50.0f &&
        d.gridVoltageL2 < 50.0f && d.gridVoltageL3 < 50.0f &&
        (d.pvPower > 50.0f || d.batteryPower < -50.0f)) {
      d.island = true;
    }

    // Solar forecast for today. The module returns -1 while it has no data,
    // which must not be shown as "0 kWh".
    {
      std::string body;
      std::string error;
      if (get("/solar_forecast/state", body, error)) {
        double wh = -1.0;
        if (warpjson::number(body.data(), body.size(), "wh_today", wh) &&
            wh >= 0.0) {
          d.forecastTodayWh = static_cast<float>(wh);
          d.hasForecast = true;
        }
      }
    }

    // Autarky: Prefer the cumulative meter counters: self-consumed PV is
    // generation minus export, and total consumption is that plus grid import.
    // This stays meaningful even when the device has no separate load meter.
    // Fall back to day counters, then to the live house/grid ratio when only
    // power values exist.
    if (!d.hasAutarky && (d.houseEnergyWh > 0.0f)) {
        float autarky =  (d.houseEnergyWh - d.gridImportWh) / d.houseEnergyWh * 100.0f;
        if (autarky < 0.0f) autarky = 0.0f;
        if (autarky > 100.0f) autarky = 100.0f;
        d.autarky = autarky;
        d.hasAutarky = true;
    }

    d.valid = d.hasGrid || d.hasHouse || d.hasPv || d.hasBattery ||
              d.hasExternal || !std::isnan(powerAtMeter);

    if (!d.valid) {
      errorOut = "Antwort ohne Messwerte";
      return false;
    }
    return true;
  }

 private:
  bool get(const std::string &path, std::string &body, std::string &error) {
    return transport_.get(base_ + path + query_, body, error);
  }

  static bool value(const Slot &slot, int id, double &out) {
    const size_t n = slot.valueIds.size();
    for (size_t i = 0; i < n; ++i) {
      if (slot.valueIds[i] == id) {
        if (i >= slot.values.size() || std::isnan(slot.values[i])) {
          return false;
        }
        out = slot.values[i];
        return true;
      }
    }
    return false;
  }

  Transport &transport_;
  std::string base_;
  std::string query_;
  std::vector<Slot> slots_;
  char deviceName_[40] = "";
  char firmware_[24] = "";
};

}  // namespace warpapi
