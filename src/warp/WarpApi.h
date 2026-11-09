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
  ROLE_CHARGER,
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

  // POSTs body to url and returns true for a complete 2xx response. The
  // default keeps transports that only implement GET working; those devices
  // simply have no daily history.
  virtual bool post(const std::string &url, const std::string &body,
                    std::string &response, std::string &error) {
    (void)url;
    (void)body;
    (void)response;
    error = "POST nicht unterstuetzt";
    return false;
  }
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

// WARP reports the meter energy value IDs in kWh (meter_value_id.csv, unit
// "kWh"), while WarpData and the UI work in Wh.
inline float kwhToWh(double kwh) { return static_cast<float>(kwh * 1000.0); }

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
      return ROLE_CHARGER;
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

    /* Charge manager / Wallbox. /charge_manager/state contains an array of
     * chargers. `s` is the CAS state, `ac` the allocated current in mA, `ap`
     * the allocated phases and `sp` the supported phases. The overview and
     * flow pages use the first/combined box; the configuration page keeps all
     * of them. */
    {
      std::string body;
      std::string error;
      if (get("/charge_manager/state", body, error)) {
        warpjson::forEachObject(
            body.data(), body.size(), "chargers",
            [&d](const char *obj, size_t len) {
              if (d.charger_count >= kWarpMaxChargers) {
                return true;  // keep scanning, ignore the rest
              }
              WarpChargerData c;
              int state = 0;
              if (warpjson::integer(obj, len, "s", state)) {
                c.state = state;
              }
              double current = 0.0;
              if (warpjson::number(obj, len, "ac", current)) {
                c.current = current;
              }
              double allocated = 0.0;
              double supported = 0.0;
              warpjson::number(obj, len, "ap", allocated);
              warpjson::number(obj, len, "sp", supported);
              if (allocated >= 1.0) {
                c.phases = static_cast<int>(allocated + 0.5);
              } else if (supported >= 1.0) {
                c.phases = static_cast<int>(supported) & 3;
              }
              warpjson::string(obj, len, "n", c.name, sizeof(c.name));
              c.mode = d.charge_mode;  // fallback when charge_modes is absent
              d.chargers[d.charger_count++] = c;
              return true;
            });
      }
    }

    /* Per-charger modes and the modes the charge manager currently supports.
     * Both are top-level arrays. */
    {
      std::string body;
      std::string error;
      if (get("/charge_manager/charge_modes", body, error)) {
        double modes[kWarpMaxChargers];
        size_t count = 0;
        if (warpjson::topNumbers(body.data(), body.size(), modes,
                                 kWarpMaxChargers, count)) {
          const size_t n = count < static_cast<size_t>(d.charger_count)
                               ? count
                               : static_cast<size_t>(d.charger_count);
          for (size_t i = 0; i < n; ++i) {
            if (!std::isnan(modes[i])) {
              d.chargers[i].mode = static_cast<int>(modes[i] + 0.5);
            }
          }
        }
      }
    }
    {
      std::string body;
      std::string error;
      if (get("/charge_manager/supported_charge_modes", body, error)) {
        double modes[kWarpMaxModes];
        size_t count = 0;
        if (warpjson::topNumbers(body.data(), body.size(), modes,
                                 kWarpMaxModes, count)) {
          d.supported_mode_count = 0;
          for (size_t i = 0;
               i < count && d.supported_mode_count < kWarpMaxModes; ++i) {
            if (!std::isnan(modes[i])) {
              d.supported_modes[d.supported_mode_count++] =
                  static_cast<int>(modes[i] + 0.5);
            }
          }
        }
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
            d.gridImportWh = kwhToWh(v);
            roleFound = true;
          }
          if (value(slot, VID_GRID_EXPORT, v)) {
            d.gridExportWh = kwhToWh(v);
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
            d.houseEnergyWh = kwhToWh(v);
            roleFound = true;
          } else {
            double imported = 0.0;
            double exported = 0.0;
            if (value(slot, VID_GRID_IMPORT, imported) &&
                value(slot, VID_GRID_EXPORT, exported)) {
              d.houseEnergyWh = kwhToWh(imported - exported);
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
            d.pvEnergyWh = kwhToWh(v);
            roleFound = true;
          }
          if (value(slot, VID_PV1_ENERGY, v)) {
            d.pvAEnergyWh = kwhToWh(v);
            roleFound = true;
          }
          if (value(slot, VID_PV2_ENERGY, v)) {
            d.pvBEnergyWh = kwhToWh(v);
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
            d.batteryChargeWh = kwhToWh(v);
            roleFound = true;
          }
          if (value(slot, VID_BATTERY_DISCHARGE, v)) {
            d.batteryDischargeWh = kwhToWh(v);
            roleFound = true;
          }
          break;
        }

        case ROLE_CHARGER: {
          double v = 0.0;
          if (value(slot, VID_POWER_TOTAL, v)) {
            d.charge_power += static_cast<float>(v);
            d.chargeMeter = true;
            roleFound = true;
          } else {
            double sum = 0.0;
            bool any = false;
            if (value(slot, VID_POWER_L1, v)) {
              sum += v;
              any = true;
            }
            if (value(slot, VID_POWER_L2, v)) {
              sum += v;
              any = true;
            }
            if (value(slot, VID_POWER_L3, v)) {
              sum += v;
              any = true;
            }
            if (any) {
              d.charge_power += static_cast<float>(sum);
              d.chargeMeter = true;
              roleFound = true;
            }
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
          case ROLE_CHARGER:
            d.hasCharger = true;
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

    /* Charge manager / Wallbox. A configured charger is enough to show the
     * card; the power comes from a wallbox meter when one exists, otherwise it
     * is estimated from the allocated current and the measured grid voltage. */
    d.hasCharger = d.hasCharger || d.charger_count > 0;
    if (d.charger_count > 0) {
      const WarpChargerData &first = d.chargers[0];
      d.charge_state = first.state;
      d.charge_phases = first.phases;
      snprintf(d.charge_name, sizeof(d.charge_name), "%s", first.name);
      double totalCurrent = 0.0;
      for (int i = 0; i < d.charger_count; ++i) {
        totalCurrent += d.chargers[i].current;
      }
      d.charge_current = totalCurrent;
    }

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

    /* Per-charger power. Without a wallbox meter it is the allocated current
     * multiplied by the phase voltages. Missing voltage falls back to 230 V,
     * which also covers island operation with the grid meter switched off.
     * With one charger the meter value is exact; with several boxes only the
     * combined meter value exists, so it is split proportionally to the
     * current estimates for the cards while the overview keeps the total. */
    {
      double v1 = d.gridVoltageL1;
      double v2 = d.gridVoltageL2;
      double v3 = d.gridVoltageL3;
      if (v1 < 50.0) v1 = 230.0;
      if (v2 < 50.0) v2 = 230.0;
      if (v3 < 50.0) v3 = 230.0;

      float totalEstimate = 0.0f;
      for (int i = 0; i < d.charger_count; ++i) {
        WarpChargerData &c = d.chargers[i];
        const double amps = c.current / 1000.0;
        const double volts = (c.phases >= 3) ? (v1 + v2 + v3) : v1;
        const float estimate = static_cast<float>(amps * volts);
        c.power = estimate;
        c.estimated = estimate > 0.0f;
        c.meter = false;
        totalEstimate += estimate;
      }

      if (d.chargeMeter) {
        // d.charge_power already holds the combined meter value.
        if (d.charger_count == 1) {
          d.chargers[0].power = d.charge_power;
          d.chargers[0].meter = true;
          d.chargers[0].estimated = false;
        } else if (d.charger_count > 1) {
          for (int i = 0; i < d.charger_count; ++i) {
            WarpChargerData &c = d.chargers[i];
            if (totalEstimate > 0.0f) {
              c.power = d.charge_power * (c.power / totalEstimate);
            } else {
              c.power = d.charge_power / d.charger_count;
            }
            c.meter = true;
            c.estimated = false;
          }
        }
        d.chargeEstimated = false;
      } else {
        d.charge_power = totalEstimate;
        d.chargeEstimated = totalEstimate > 0.0f;
      }
    }

    // The load meter usually measures the whole house connection including the
    // wallbox. Subtract the charging power so the displayed consumption is the
    // household only.
    const int chargePhases = d.charge_phases;
    if (d.hasHouse && d.charge_power > 0.0f) {
      d.housePower -= d.charge_power;
      if (d.housePower < 0.0f) {
        d.housePower = 0.0f;
      }
      if (d.houseL1 != 0.0f || d.houseL2 != 0.0f || d.houseL3 != 0.0f) {
        if (chargePhases >= 3) {
          const float share = d.charge_power / 3.0f;
          d.houseL1 -= share;
          d.houseL2 -= share;
          d.houseL3 -= share;
        } else {
          d.houseL1 -= d.charge_power;
        }
        if (d.houseL1 < 0.0f) d.houseL1 = 0.0f;
        if (d.houseL2 < 0.0f) d.houseL2 = 0.0f;
        if (d.houseL3 < 0.0f) d.houseL3 = 0.0f;
      }
    }

    // A grid meter that reports zero voltage while PV or the battery is active
    // is the reliable sign of an island situation on this hardware.
    if (gridVoltageFound && d.gridVoltageL1 < 50.0f &&
        d.gridVoltageL2 < 50.0f && d.gridVoltageL3 < 50.0f &&
        (d.pvPower > 50.0f || d.batteryPower < -50.0f)) {
      d.island = true;
    }

    // Solar forecast for today. The module returns -1 while it has no data,
    // which must not be shown as "0 kWh". wh_today_remaining is the part of
    // the forecast still ahead, as opposed to the whole-day wh_today.
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
        double whRemaining = -1.0;
        if (warpjson::number(body.data(), body.size(), "wh_today_remaining",
                             whRemaining) &&
            whRemaining >= 0.0) {
          d.forecastTodayRemainingWh = static_cast<float>(whRemaining);
          d.hasForecastRemaining = true;
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

    // Today's autarky needs two daily-history rows: today's cumulative
    // counters and yesterday's as the baseline. The SD-card history updates
    // every five minutes, so six polls (about a minute) are enough.
    if (dailyTtl_ <= 0) {
      dailyHasAutarky_ = readDailyAutarky(d);
      dailyAutarky_ = dailyHasAutarky_ ? d.autarkyToday : 0.0f;
      dailyHasDaySums_ = d.hasDaySums;
      dailyDayGen_ = d.dayGenerationWh;
      dailyDayLoad_ = d.dayLoadWh;
      dailyDayDraw_ = d.dayGridDrawWh;
      dailyDayFeed_ = d.dayFeedInWh;
      dailyTtl_ = 6;
    } else {
      --dailyTtl_;
    }
    d.hasAutarkyToday = dailyHasAutarky_;
    d.autarkyToday = dailyAutarky_;
    d.hasDaySums = dailyHasDaySums_;
    d.dayGenerationWh = dailyDayGen_;
    d.dayLoadWh = dailyDayLoad_;
    d.dayGridDrawWh = dailyDayDraw_;
    d.dayFeedInWh = dailyDayFeed_;

    // Slow-changing state (heating, temperatures, IO): fetch every 6th poll
    // (~60 s) and serve the cache in between. Fewer HTTP round trips means
    // less network and PSRAM traffic competing with the display scan.
    if (slowTtl_ <= 0) {
      fetchSlow(d);
      slowCache_ = d;
      slowTtl_ = 6;
    } else {
      --slowTtl_;
      copySlow(d);
    }

    d.valid = d.hasGrid || d.hasHouse || d.hasPv || d.hasBattery ||
              d.hasExternal || !std::isnan(powerAtMeter);

    if (!d.valid) {
      errorOut = "Antwort ohne Messwerte";
      return false;
    }
    return true;
  }

  /* Applies a charge mode to every charger through the power manager. This is
   * the same command the WARP web interface's global mode selector sends. */
  bool setGlobalChargeMode(int mode, std::string &error) {
    const std::string body = "{\"mode\":" + std::to_string(mode) + "}";
    return command("/power_manager/charge_mode_update", body, error);
  }

  /* Overrides the charge mode of one charger. The charge manager accepts the
   * bare integer as the request body on the per-charger path; the setting lasts
   * for the current session until the vehicle is unplugged or reset. */
  bool setChargerChargeMode(int index, int mode, std::string &error) {
    if (index < 0) {
      error = "ungueltiger Ladepunkt";
      return false;
    }
    const std::string path =
        "/charge_manager/charge_modes/" + std::to_string(index);
    return command(path, std::to_string(mode), error);
  }

  /* Temporary SG-Ready override (the web interface's "Override", used here
   * as the party mode): on=true sets override_until to now + 2 hours,
   * on=false discards it with override_until = 0. */
  bool setSgrOverride(bool on, std::string &error) {
    uint32_t until = 0;
    if (on) {
      uint32_t nowMin = 0;
      if (!nowMinutes(nowMin)) {
        error = "keine Uhrzeit vom Geraet";
        return false;
      }
      until = nowMin + 120;
    }
    const std::string body =
        "{\"override_until\":" + std::to_string(until) + "}";
    return command("/heating/sgr_blocking_override_update", body, error);
  }

 private:
  // POSTs a command and treats a 2xx response as success. The firmware answers
  // commands with an empty body, so the response itself is not inspected.
  bool command(const std::string &path, const std::string &body,
               std::string &error) {
    std::string response;
    return transport_.post(base_ + path + query_, body, response, error);
  }

  bool get(const std::string &path, std::string &body, std::string &error) {
    return transport_.get(base_ + path + query_, body, error);
  }

  // Current device time in minutes since the Unix epoch, from /rtc/time.
  // The heating override_until uses the same unit, so the remaining time
  // is a plain difference of the two.
  bool nowMinutes(uint32_t &out) {
    std::string body;
    std::string error;
    if (!get("/rtc/time", body, error)) {
      return false;
    }
    int year = 0, month = 0, day = 0, hour = 0, minute = 0;
    if (!warpjson::integer(body.data(), body.size(), "year", year) ||
        !warpjson::integer(body.data(), body.size(), "month", month) ||
        !warpjson::integer(body.data(), body.size(), "day", day) ||
        !warpjson::integer(body.data(), body.size(), "hour", hour) ||
        !warpjson::integer(body.data(), body.size(), "minute", minute)) {
      return false;
    }
    // Days from civil (Howard Hinnant's algorithm), then minutes of day.
    int y = year - (month <= 2 ? 1 : 0);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = static_cast<int64_t>(era) * 146097 + doe - 719468;
    out = static_cast<uint32_t>(days * 1440 + hour * 60 + minute);
    return true;
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

  // Fetches one month of the daily history. The device stores 17 values per
  // day: import counters for meter slots 0..6, export counters for 0..6, then
  // three price values. Counters are cumulative kWh, so a day's energy is the
  // difference to the previous day's row.
  bool fetchDaily(int year, int month, std::vector<double> &values,
                  size_t &count) {
    char request[48];
    snprintf(request, sizeof(request), "{\"year\":%d,\"month\":%d}", year,
             month);
    std::string response;
    std::string error;
    if (!transport_.post(base_ + "/energy_manager/history_energy_manager_daily" +
                             query_,
                         request, response, error)) {
      return false;
    }
    values.assign(17 * 31, NAN);
    return warpjson::topNumbers(response.data(), response.size(), values.data(),
                                values.size(), count) &&
           count >= 17;
  }

  static bool dailyDayValid(const std::vector<double> &values, size_t count,
                            int day, int loadSlot, int gridSlot) {
    if (day < 0) {
      return false;
    }
    const size_t base = static_cast<size_t>(day) * 17;
    if (base + 17 > count) {
      return false;
    }
    return !std::isnan(values[base + loadSlot]) &&
           !std::isnan(values[base + gridSlot]);
  }

  static int dailyLastValid(const std::vector<double> &values, size_t count,
                            int loadSlot, int gridSlot, int before) {
    for (int day = before - 1; day >= 0; --day) {
      if (dailyDayValid(values, count, day, loadSlot, gridSlot)) {
        return day;
      }
    }
    return -1;
  }

  static double dailyLoadNet(const std::vector<double> &values, size_t count,
                             int day, int slot) {
    const size_t base = static_cast<size_t>(day) * 17;
    if (base + 17 > count || std::isnan(values[base + slot])) {
      return NAN;
    }
    const double exported = values[base + 7 + slot];
    return values[base + slot] - (std::isnan(exported) ? 0.0 : exported);
  }

  static double dailyExport(const std::vector<double> &values, size_t count,
                            int day, int slot) {
    const size_t base = static_cast<size_t>(day) * 17;
    if (base + 17 > count || std::isnan(values[base + 7 + slot])) {
      return NAN;
    }
    return values[base + 7 + slot];
  }

  static double dailyGridImport(const std::vector<double> &values,
                                size_t count, int day, int slot) {
    const size_t base = static_cast<size_t>(day) * 17;
    if (base + 17 > count) {
      return NAN;
    }
    return values[base + slot];
  }

  void fetchSlow(WarpData &d) {
    // Heating (SG-Ready) and outdoor temperatures. Both modules are optional;
    // a missing endpoint simply leaves hasHeating/hasTemps false.
    {
      std::string body;
      std::string error;
      if (get("/heating/state", body, error)) {
        bool v = false;
        if (warpjson::boolean(body.data(), body.size(), "enabled", v)) {
          d.heatingEnabled = v;
        }
        if (warpjson::boolean(body.data(), body.size(), "sgr_blocking", v)) {
          d.sgrBlocking = v;
        }
        if (warpjson::boolean(body.data(), body.size(), "sgr_extended", v)) {
          d.sgrExtended = v;
        }
        if (warpjson::boolean(body.data(), body.size(), "p14enwg", v)) {
          d.p14Enwg = v;
        }
        if (warpjson::boolean(body.data(), body.size(), "automation_override",
                              v)) {
          d.automationOverride = v;
        }
        d.hasHeating = true;
        std::string overrideBody;
        std::string overrideError;
        if (get("/heating/sgr_blocking_override", overrideBody,
                overrideError)) {
          double until = 0.0;
          if (warpjson::number(overrideBody.data(), overrideBody.size(),
                               "override_until", until) &&
              until > 0.0) {
            d.hasSgrOverride = true;
            uint32_t nowMin = 0;
            if (nowMinutes(nowMin)) {
              d.sgrOverrideRemainingMin =
                  until > nowMin ? static_cast<uint32_t>(until - nowMin) : 0;
            }
          }
        }
      }
      if (get("/temperatures/state", body, error)) {
        auto temp = [&body](const char *key, int16_t &out) {
          double v = 0.0;
          if (warpjson::number(body.data(), body.size(), key, v)) {
            out = static_cast<int16_t>(v);
            return true;
          }
          return false;
        };
        bool any = false;
        any |= temp("current", d.tempCurrent);
        any |= temp("today_min", d.tempTodayMin);
        any |= temp("today_max", d.tempTodayMax);
        any |= temp("today_avg", d.tempTodayAvg);
        any |= temp("tomorrow_min", d.tempTomorrowMin);
        any |= temp("tomorrow_max", d.tempTomorrowMax);
        any |= temp("tomorrow_avg", d.tempTomorrowAvg);
        d.hasTemps = any;
      }
    }

    // IO states (inputs, relays, SG-Ready outputs) for the chart page.
    {
      std::string body;
      std::string error;
      if (get("/energy_manager/state", body, error)) {
        size_t nInputs = 0, nRelays = 0, nSg = 0;
        const bool okInputs = warpjson::arrayBooleans(
            body.data(), body.size(), "inputs", d.ioInputs, 4, nInputs);
        const bool okRelays = warpjson::arrayBooleans(
            body.data(), body.size(), "relays", d.ioRelays, 2, nRelays);
        const bool okSg = warpjson::arrayBooleans(
            body.data(), body.size(), "sg_ready_outputs", d.ioSgReady, 2, nSg);
        d.hasIoStates = okInputs || okRelays || okSg;
      }
    }
  }

  void copySlow(WarpData &d) {
    d.hasHeating = slowCache_.hasHeating;
    d.heatingEnabled = slowCache_.heatingEnabled;
    d.sgrBlocking = slowCache_.sgrBlocking;
    d.sgrExtended = slowCache_.sgrExtended;
    d.p14Enwg = slowCache_.p14Enwg;
    d.automationOverride = slowCache_.automationOverride;
    d.hasSgrOverride = slowCache_.hasSgrOverride;
    d.sgrOverrideRemainingMin = slowCache_.sgrOverrideRemainingMin;
    d.hasTemps = slowCache_.hasTemps;
    d.tempCurrent = slowCache_.tempCurrent;
    d.tempTodayMin = slowCache_.tempTodayMin;
    d.tempTodayMax = slowCache_.tempTodayMax;
    d.tempTodayAvg = slowCache_.tempTodayAvg;
    d.tempTomorrowMin = slowCache_.tempTomorrowMin;
    d.tempTomorrowMax = slowCache_.tempTomorrowMax;
    d.tempTomorrowAvg = slowCache_.tempTomorrowAvg;
    d.hasIoStates = slowCache_.hasIoStates;
    for (int i = 0; i < 4; ++i) d.ioInputs[i] = slowCache_.ioInputs[i];
    for (int i = 0; i < 2; ++i) {
      d.ioRelays[i] = slowCache_.ioRelays[i];
      d.ioSgReady[i] = slowCache_.ioSgReady[i];
    }
  }

  bool readDailyAutarky(WarpData &d) {
    int loadSlot = -1;
    int gridSlot = -1;
    int pvSlot = -1;
    for (const Slot &slot : slots_) {
      if (slot.role == ROLE_LOAD && loadSlot < 0) {
        loadSlot = slot.slot;
      }
      if (slot.role == ROLE_GRID && gridSlot < 0) {
        gridSlot = slot.slot;
      }
      if (slot.role == ROLE_PV && pvSlot < 0) {
        pvSlot = slot.slot;
      }
    }
    if (loadSlot < 0 || gridSlot < 0) {
      return false;  // no consumption or no grid import to compare
    }

    int year = 0;
    int month = 0;
    {
      std::string body;
      std::string error;
      if (!get("/rtc/time", body, error) ||
          !warpjson::integer(body.data(), body.size(), "year", year) ||
          !warpjson::integer(body.data(), body.size(), "month", month)) {
        return false;
      }
    }
    if (year < 2000 || month < 1 || month > 12) {
      return false;
    }

    std::vector<double> values;
    size_t count = 0;
    if (!fetchDaily(year, month, values, count)) {
      return false;
    }

    const int today =
        dailyLastValid(values, count, loadSlot, gridSlot,
                       static_cast<int>(count / 17));
    if (today < 0) {
      return false;
    }

    double previousLoad = NAN;
    double previousGrid = NAN;
    double previousPvExport = NAN;
    double previousGridExport = NAN;
    if (today > 0) {
      const int previous =
          dailyLastValid(values, count, loadSlot, gridSlot, today);
      if (previous < 0) {
        return false;
      }
      previousLoad = dailyLoadNet(values, count, previous, loadSlot);
      previousGrid = dailyGridImport(values, count, previous, gridSlot);
      if (pvSlot >= 0) {
        previousPvExport = dailyExport(values, count, previous, pvSlot);
      }
      previousGridExport = dailyExport(values, count, previous, gridSlot);
    } else {
      // The first of the month: yesterday lives in the previous month.
      int previousYear = year;
      int previousMonth = month - 1;
      if (previousMonth < 1) {
        previousMonth = 12;
        --previousYear;
      }
      std::vector<double> previousValues;
      size_t previousCount = 0;
      if (!fetchDaily(previousYear, previousMonth, previousValues,
                      previousCount)) {
        return false;
      }
      const int previous = dailyLastValid(
          previousValues, previousCount, loadSlot, gridSlot,
          static_cast<int>(previousCount / 17));
      if (previous < 0) {
        return false;
      }
      previousLoad =
          dailyLoadNet(previousValues, previousCount, previous, loadSlot);
      previousGrid =
          dailyGridImport(previousValues, previousCount, previous, gridSlot);
      if (pvSlot >= 0) {
        previousPvExport =
            dailyExport(previousValues, previousCount, previous, pvSlot);
      }
      previousGridExport =
          dailyExport(previousValues, previousCount, previous, gridSlot);
    }

    const double todayLoad = dailyLoadNet(values, count, today, loadSlot);
    const double todayGrid = dailyGridImport(values, count, today, gridSlot);
    const double loadDelta = todayLoad - previousLoad;
    const double gridDelta = todayGrid - previousGrid;

    // Today's sums from the same day-over-day deltas (kWh -> Wh). Set before
    // the autarky guard below: a near-empty day still has sums. PV generation
    // is the export counter of the PV slot, feed-in the export counter of
    // the grid slot.
    bool anySum = false;
    if (!std::isnan(loadDelta)) {
      d.dayLoadWh = (float)(std::max(0.0, loadDelta) * 1000.0);
      anySum = true;
    }
    if (!std::isnan(gridDelta)) {
      d.dayGridDrawWh = (float)(std::max(0.0, gridDelta) * 1000.0);
      anySum = true;
    }
    if (pvSlot >= 0) {
      const double pvDelta =
          dailyExport(values, count, today, pvSlot) - previousPvExport;
      if (!std::isnan(pvDelta)) {
        d.dayGenerationWh = (float)(std::max(0.0, pvDelta) * 1000.0);
        anySum = true;
      }
    }
    {
      const double feedDelta =
          dailyExport(values, count, today, gridSlot) - previousGridExport;
      if (!std::isnan(feedDelta)) {
        d.dayFeedInWh = (float)(std::max(0.0, feedDelta) * 1000.0);
        anySum = true;
      }
    }
    d.hasDaySums = anySum;

    if (!(loadDelta > 0.01) || std::isnan(gridDelta)) {
      return false;
    }

    float autarky =
        static_cast<float>((loadDelta - gridDelta) / loadDelta * 100.0);
    if (autarky < 0.0f) {
      autarky = 0.0f;
    }
    if (autarky > 100.0f) {
      autarky = 100.0f;
    }
    d.autarkyToday = autarky;
    d.hasAutarkyToday = true;
    return true;
  }

  Transport &transport_;
  std::string base_;
  std::string query_;
  std::vector<Slot> slots_;
  char deviceName_[40] = "";
  char firmware_[24] = "";
  int dailyTtl_ = 0;
  int priceTtl_ = 0;  // fetchSlow ticks (~60 s): 15 ticks ~ 15 min
  bool dailyHasAutarky_ = false;
  float dailyAutarky_ = 0.0f;
  bool dailyHasDaySums_ = false;
  float dailyDayGen_ = 0.0f;
  float dailyDayLoad_ = 0.0f;
  float dailyDayDraw_ = 0.0f;
  float dailyDayFeed_ = 0.0f;
  int slowTtl_ = 0;
  WarpData slowCache_;
};

}  // namespace warpapi
