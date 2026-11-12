// Home Energy Manager panel - data model.
//
// One struct, filled from the device's JSON and read by the UI. Kept free of
// Arduino and LVGL so the firmware and the desktop emulator share it verbatim.
#pragma once

#include <stdint.h>

// The panel has room for the controls of three wallboxes on the
// configuration page, so that is the supported maximum. Extra chargers
// reported by the charge manager are ignored.
constexpr int kWarpMaxChargers = 3;
constexpr int kWarpMaxModes = 10;

// One wallbox as reported by /charge_manager/state and
// /charge_manager/charge_modes. The overview and flow pages use the aggregate
// fields below; the configuration page shows one card per entry here.
struct WarpChargerData {
  char name[40] = "";
  int state = 0;         // CASState from charge_manager/state
  int mode = 0;          // ConfigChargeMode for this charger
  float power = 0.0f;    // W, wallbox meter value or estimate
  double current = 0.0;  // mA allocated current
  int phases = 0;        // 1..3, 0 = unknown
  bool meter = false;    // power came from a dedicated wallbox meter
  bool estimated = false;
};

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
  bool hasCharger = false;      // charge_manager reported at least one charger
  bool chargeMeter = false;     // a dedicated wallbox meter supplied the power
  bool chargeEstimated = false; // power derived from current and voltage
  int charge_state = 0;         // CASState from charge_manager/state
  float charge_power = 0.0f;    // W, wallbox meter value or estimate
  double charge_current = 0.0;  // mA allocated current
  char charge_name[40] = "";    // Charger name from API
  int charge_mode = 0;          // global mode from power_manager/charge_mode
  int charge_phases = 0;        // phases used by the wallbox (0 = unknown)

  // All wallboxes reported by the charge manager. The configuration page
  // shows one card per charger; the aggregate fields above describe the first
  // (or the combined) box for the overview and flow pages.
  WarpChargerData chargers[kWarpMaxChargers];
  int charger_count = 0;

  // Modes the charge manager currently offers. Empty when the endpoint is not
  // available (older firmware); the UI then falls back to Off/Fast.
  int supported_modes[kWarpMaxModes] = {};
  int supported_mode_count = 0;

  // Heating (SG-Ready for the heat pump) and outdoor temperatures.
  // Temperatures arrive in °C x 10, INT16_MAX means not available.
  bool hasHeating = false;
  bool heatingEnabled = false;
  bool sgrBlocking = false;
  bool sgrExtended = false;
  bool p14Enwg = false;
  bool automationOverride = false;
  bool hasSgrOverride = false;        // override_until != 0
  uint32_t sgrOverrideRemainingMin = 0;
  bool hasTemps = false;
  int16_t tempCurrent = INT16_MAX;
  int16_t tempTodayMin = INT16_MAX;
  int16_t tempTodayMax = INT16_MAX;
  int16_t tempTodayAvg = INT16_MAX;
  int16_t tempTomorrowMin = INT16_MAX;
  int16_t tempTomorrowMax = INT16_MAX;
  int16_t tempTomorrowAvg = INT16_MAX;

  // IO states from energy_manager/state: 4 inputs, 2 relays, 2 SG-Ready
  // outputs. Shown as current-state indicators on the chart page.
  bool hasIoStates = false;
  bool ioInputs[4] = {};
  bool ioRelays[2] = {};
  bool ioSgReady[2] = {};

  // Grid connection.
  bool island = false;
  bool is_3phase = false;

  // Energy counters since commissioning, Wh. Filled from the meter value IDs on
  // a real WARP device (which reports kWh, converted on ingest); the day_*
  // aliases are the fallback for flat JSON.
  float pvEnergyWh = 0.0f;
  float pvAEnergyWh = 0.0f;
  float pvBEnergyWh = 0.0f;
  float houseEnergyWh = 0.0f;
  float gridImportWh = 0.0f;
  float gridExportWh = 0.0f;
  float batteryChargeWh = 0.0f;
  float batteryDischargeWh = 0.0f;

  // Wallbox energy counters, Wh. Sum of every meter slot with location
  // Charger/Vehicle that reports an energy counter. `hasChargerEnergy` is set
  // when at least one such meter exists, so the UI can show a dash instead of
  // a misleading zero.
  bool hasChargerEnergy = false;
  float chargerEnergyWh = 0.0f;  // since commissioning
  bool hasDayCharger = false;
  float dayChargerWh = 0.0f;     // today, from the daily history

  // Day counters, Wh (flat JSON only).
  // Dynamic tariff (day-ahead) prices, ct/kWh/1000. prices[0] is valid
  // from priceFirstMin (unix minutes), entries are priceResMin apart.
  // nowMin is the device unix time in minutes (slow cache).
  bool hasPrices = false;
  int32_t vatBp = 0;          // VAT in basis points (1900 = 19 %)
  int32_t gridCosts = 0;      // ct/kWh/1000
  int32_t supplierMarkup = 0; // ct/kWh/1000
  bool pricesEnabled = false;
  uint32_t priceFirstMin = 0;
  int priceResMin = 15;
  int priceCount = 0;
  int32_t prices[200] = {};
  uint32_t nowMin = 0;

  bool hasDaySums = false;
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

  // Today's autarky from the device's daily energy history. Only available
  // when the WARP provides cumulative day counters for load and grid.
  bool hasAutarkyToday = false;
  float autarkyToday = 0.0f;

  // Solar forecast for today, Wh. WARP reports -1 when the forecast module has
  // no usable data, so hasForecast distinguishes "0 Wh" from "not available".
  bool hasForecast = false;
  float forecastTodayWh = 0.0f;

  // Remaining part of today's forecast, Wh. Both values come from the same
  // solar_forecast/state response.
  bool hasForecastRemaining = false;
  float forecastTodayRemainingWh = 0.0f;

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
