// Emulator stand-in for the panel's SD-card log.
//
// The firmware worker in src/storage/SdLog.cpp owns an SD card through the
// Arduino SD library. The emulator has no card, so this writes the same CSV
// rows into a directory on the host instead:
//
//   <dir>/hist/HEMS-YYYYMM.csv
//
// The row format and the parked-row ring are the shipped headers from
// src/storage, so the emulator exercises the same format and the same queue
// logic as the panel. Only the card access is different.
//
// SPDX-License-Identifier: MIT
#ifndef HEMS_SIM_SIMSDLOG_H
#define HEMS_SIM_SIMSDLOG_H

#include <string>

#include "storage/CsvRow.h"
#include "warp/WarpData.h"

namespace simsd {

using Sample = csvrow::Sample;

// Start logging into `dir` (created if missing). An empty dir disables the
// emulator log. Returns false when the directory cannot be used.
bool init(const std::string &dir);

bool enabled();
bool mounted();
const char *dir();

// Append one row for this snapshot. On a write failure the row is parked in
// the same ring the firmware uses and retried by tick().
void logSample(const WarpData &d);

// Retry parked rows (throttled). Call from the main loop.
void tick();

// Service-page status, e.g. "SD: OK | /tmp/hems-panel-sd".
const char *statusText();

// Read the newest maxRows rows from the log, oldest first. Returns the number
// of rows copied, or 0 when there is nothing to restore.
int restore(csvrow::Sample *out, int maxRows);

}  // namespace simsd

#endif  // HEMS_SIM_SIMSDLOG_H
