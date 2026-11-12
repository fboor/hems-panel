// The CSV row of the SD history log: one line of a month file, both ends of
// the format.
//
// Header-only and free of Arduino, SD and LVGL on purpose. The row format is
// the contract between three places - the writer (SdLog worker), the reader
// that fills the 24 h chart, and the file a user copies off the card - so the
// format and its round trip live in one testable place.
//
// One row per minute, unix seconds first. Powers in W, temperatures in degC
// with one decimal, counters in Wh. Fixed decimal places keep the file readable
// in a spreadsheet and make a written row come back exactly as it went in.
//
// The round trip is checked by tools/sd_format_test/run.sh, which compiles
// these headers on the build machine.
//
// SPDX-License-Identifier: MIT
#ifndef HEMS_STORAGE_CSVROW_H
#define HEMS_STORAGE_CSVROW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "warp/WarpData.h"

namespace csvrow {

// One row, as numbers. The fields are in the same order as the header below.
struct Row {
  uint32_t ts; // unix seconds

  float gridL1, gridL2, gridL3, grid;
  float pvA, pvB, pv;
  float external;
  float houseL1, houseL2, houseL3, house;
  float batteryPower, soc, batteryVoltage, batteryCurrent;
  float wb1, wb2, wb3;
  float wbState, wbMode, wbCurrent;
  float island;
  float heatEnabled, sgrBlocking, sgrExtended, p14;
  float autarky, autarkyToday;
  float forecastTodayWh, forecastRemainingWh;
  float pvTotalWh, houseTotalWh, gridImportWh, gridExportWh;
  float batChargeWh, batDischargeWh;
  float tempC;
};

// One point of the 24 h chart. The six powers are in W, soc is percent.
struct Sample {
  uint32_t ts;
  float grid;
  float pv;
  float house;
  float battery;
  float wb1;
  float wb2;
  float soc;
  int8_t heat; // -1 no data, 0 none, 1 SG1 blocking, 2 SG2 extended, 3 both
};

static const char kHeader[] =
    "ts,grid_l1,grid_l2,grid_l3,grid,pv_a,pv_b,pv,external,"
    "house_l1,house_l2,house_l3,house,bat_power,soc,bat_voltage,bat_current,"
    "wb1_power,wb2_power,wb3_power,wb_state,wb_mode,wb_current,island,"
    "heat_enabled,sgr_blocking,sgr_extended,p14enwg,autarky,autarky_today,"
    "forecast_today_wh,forecast_remaining_wh,pv_total_wh,house_total_wh,"
    "grid_import_wh,grid_export_wh,bat_charge_wh,bat_discharge_wh,temp_c";

static const int kColumns = 39;

// Long enough for every column at its worst-case width plus the commas. The
// host test formats the worst case it can construct and asserts the room that
// is left, so this number is checked rather than believed.
static const size_t kLineCap = 320;

// Path buffer for a queued row's file name.
static const size_t kPathCap = 40;

// Fill one row from the data model. Missing values are written as -1 where the
// model has a flag for them, so a reader can tell "not configured" from 0.
inline void fillRow(const WarpData &d, uint32_t ts, Row &r) {
  r.ts = ts;

  r.gridL1 = d.gridL1;
  r.gridL2 = d.gridL2;
  r.gridL3 = d.gridL3;
  r.grid = d.gridPower;

  r.pvA = d.pvA;
  r.pvB = d.pvB;
  r.pv = d.pvPower;
  r.external = d.externalPower;

  r.houseL1 = d.houseL1;
  r.houseL2 = d.houseL2;
  r.houseL3 = d.houseL3;
  r.house = d.housePower;

  r.batteryPower = d.batteryPower;
  r.soc = d.hasBattery ? d.soc : -1.0f;
  r.batteryVoltage = d.batteryVoltage;
  r.batteryCurrent = d.batteryCurrent;

  r.wb1 = d.charger_count > 0 ? d.chargers[0].power : 0.0f;
  r.wb2 = d.charger_count > 1 ? d.chargers[1].power : 0.0f;
  r.wb3 = d.charger_count > 2 ? d.chargers[2].power : 0.0f;
  r.wbState = d.charge_state;
  r.wbMode = d.charge_mode;
  r.wbCurrent = (float)d.charge_current;

  r.island = d.island ? 1.0f : 0.0f;

  r.heatEnabled = d.hasHeating ? (d.heatingEnabled ? 1.0f : 0.0f) : -1.0f;
  r.sgrBlocking = d.sgrBlocking ? 1.0f : 0.0f;
  r.sgrExtended = d.sgrExtended ? 1.0f : 0.0f;
  r.p14 = d.p14Enwg ? 1.0f : 0.0f;

  r.autarky = d.hasAutarky ? d.autarky : -1.0f;
  r.autarkyToday = d.hasAutarkyToday ? d.autarkyToday : -1.0f;
  r.forecastTodayWh = d.hasForecast ? d.forecastTodayWh : -1.0f;
  r.forecastRemainingWh =
      d.hasForecastRemaining ? d.forecastTodayRemainingWh : -1.0f;

  r.pvTotalWh = d.pvEnergyWh;
  r.houseTotalWh = d.houseEnergyWh;
  r.gridImportWh = d.gridImportWh;
  r.gridExportWh = d.gridExportWh;
  r.batChargeWh = d.batteryChargeWh;
  r.batDischargeWh = d.batteryDischargeWh;

  r.tempC = (d.hasTemps && d.tempCurrent != INT16_MAX)
                ? (float)d.tempCurrent / 10.0f
                : -999.0f;
}

// Format one row into out (always NUL-terminated, truncated rather than
// overflowing). Returns the number of characters written, or 0 if out was too
// small for the whole row.
inline size_t format(char *out, size_t cap, const Row &r) {
  if (out == nullptr || cap == 0) {
    return 0;
  }
  const int n = snprintf(
      out, cap,
      "%lu,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,"
      "%.0f,%.0f,%.0f,%.0f,%.0f,%.1f,%.1f,%.2f,"
      "%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,"
      "%.0f,%.0f,%.0f,%.0f,%.1f,%.1f,"
      "%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.1f",
      (unsigned long)r.ts, (double)r.gridL1, (double)r.gridL2,
      (double)r.gridL3, (double)r.grid, (double)r.pvA, (double)r.pvB,
      (double)r.pv, (double)r.external, (double)r.houseL1,
      (double)r.houseL2, (double)r.houseL3, (double)r.house,
      (double)r.batteryPower, (double)r.soc, (double)r.batteryVoltage,
      (double)r.batteryCurrent, (double)r.wb1, (double)r.wb2, (double)r.wb3,
      (double)r.wbState, (double)r.wbMode, (double)r.wbCurrent,
      (double)r.island, (double)r.heatEnabled, (double)r.sgrBlocking,
      (double)r.sgrExtended, (double)r.p14, (double)r.autarky,
      (double)r.autarkyToday, (double)r.forecastTodayWh,
      (double)r.forecastRemainingWh, (double)r.pvTotalWh,
      (double)r.houseTotalWh, (double)r.gridImportWh, (double)r.gridExportWh,
      (double)r.batChargeWh, (double)r.batDischargeWh, (double)r.tempC);
  if (n < 0) {
    out[0] = '\0';
    return 0;
  }
  if ((size_t)n >= cap) {
    return 0;
  }
  return (size_t)n;
}

// Read one row back. Strict: exactly kColumns values, nothing before the
// timestamp and nothing but a line ending after the last value. A row that is
// not exactly understood is skipped rather than half-understood.
inline bool parse(const char *line, Row &r) {
  if (line == nullptr) {
    return false;
  }

  char *end = nullptr;
  const unsigned long ts = strtoul(line, &end, 10);
  if (end == line) {
    return false;
  }
  const char *p = end;

  float *dst[kColumns - 1] = {
      &r.gridL1,        &r.gridL2,       &r.gridL3,      &r.grid,
      &r.pvA,           &r.pvB,          &r.pv,          &r.external,
      &r.houseL1,       &r.houseL2,      &r.houseL3,     &r.house,
      &r.batteryPower,  &r.soc,          &r.batteryVoltage, &r.batteryCurrent,
      &r.wb1,           &r.wb2,          &r.wb3,         &r.wbState,
      &r.wbMode,        &r.wbCurrent,    &r.island,      &r.heatEnabled,
      &r.sgrBlocking,   &r.sgrExtended,  &r.p14,         &r.autarky,
      &r.autarkyToday,  &r.forecastTodayWh, &r.forecastRemainingWh,
      &r.pvTotalWh,     &r.houseTotalWh, &r.gridImportWh, &r.gridExportWh,
      &r.batChargeWh,   &r.batDischargeWh, &r.tempC};

  for (int i = 0; i < kColumns - 1; ++i) {
    if (*p != ',') {
      return false;
    }
    ++p;
    const float v = strtof(p, &end);
    if (end == p) {
      return false;
    }
    *dst[i] = v;
    p = end;
  }

  while (*p == '\r' || *p == '\n') {
    ++p;
  }
  if (*p != '\0') {
    return false;
  }

  r.ts = (uint32_t)ts;
  return true;
}

// Row -> chart point. The heat state is the same combination the live UI uses:
// -1 no data, 0 none active, 1 SG1, 2 SG2, 3 both.
inline void toSample(const Row &r, Sample &s) {
  s.ts = r.ts;
  s.grid = r.grid;
  s.pv = r.pv;
  s.house = r.house;
  s.battery = r.batteryPower;
  s.wb1 = r.wb1;
  s.wb2 = r.wb2;
  s.soc = r.soc;
  if (r.heatEnabled < 0.0f) {
    s.heat = -1;
  } else if (r.sgrBlocking > 0.0f && r.sgrExtended > 0.0f) {
    s.heat = 3;
  } else if (r.sgrBlocking > 0.0f) {
    s.heat = 1;
  } else if (r.sgrExtended > 0.0f) {
    s.heat = 2;
  } else {
    s.heat = 0;
  }
}

} // namespace csvrow

#endif // HEMS_STORAGE_CSVROW_H
