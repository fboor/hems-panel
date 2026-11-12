// SD-card history logging (see docs/sd-log.md).
//
// SPI mode on the TF slot of the Waveshare ESP32-S3-Touch-LCD-7. One CSV row
// per minute, one file per calendar month:
//
//   /hist/HEMS-202611.csv
//
// The card may be pulled out while the panel runs, and a write can fail for
// other reasons too (full, marginal card). Rows that cannot be written are
// parked in a RAM ring and retried; on overflow the oldest row goes and the
// loss shows up in sdStatusText(). The ring holds 24 h (1440 rows) in PSRAM
// when the panel has PSRAM, and falls back to one hour in internal RAM.
//
// The card belongs to one worker task (mounting and scanning it block for
// hundreds of milliseconds), so the GUI thread only posts requests and copies
// status out under a lock.
//
// SPDX-License-Identifier: MIT
#ifndef HEMS_STORAGE_SDLOG_H
#define HEMS_STORAGE_SDLOG_H

#include <stdint.h>

#include "storage/CsvRow.h"
#include "warp/WarpData.h"

// The chart row format lives in storage/CsvRow.h: sdLogSample() writes it, the
// history scan reads it back.
using SdHistSample = csvrow::Sample;

// Start the worker task that owns the card. Non-blocking.
void sdInit();

// Append one CSV row for this snapshot. If the card is missing or the write
// fails, the row is parked in the RAM ring and retried; on overflow the oldest
// row goes and the loss shows up in sdStatusText(). Returns immediately.
void sdLogSample(const WarpData &d);

// Service page status, e.g. "SD: OK | 7,4 GB frei" or
// "SD: -- | 12 gepuffert (12 min)". Thread-safe.
const char *sdStatusText();
bool sdMounted();

// --------------------------------------------------------------------------
// History restore for the 24 h chart. Asynchronous, because the scan reads
// whole CSV files and must not run on the GUI thread:
//   1. sdRequestHistory(maxRows, waitForClock) - ask, returns immediately
//   2. sdTakeHistory(out, maxRows) each GUI tick - collect when it is ready
// maxRows must be <= 1442 (1442 * 1 min = 24 h).
//
// waitForClock: with waitForClock=true and the wall clock not yet valid, the
// worker defers and sdTakeHistory() returns -1 so the caller can retry. Pass
// false to accept the pre-clock uptime-named file.
//
// sdTakeHistory returns:
//   >= 0  number of rows copied into out (chronological, oldest first)
//    -1   deferred, clock not ready yet - ask again
//    -2   nothing to collect yet, still running or not requested
// --------------------------------------------------------------------------
void sdRequestHistory(int maxRows, bool waitForClock);
int sdTakeHistory(SdHistSample *out, int maxRows);

// SPI clock the card is currently mounted at, in Hz (0 when no card). 4000000
// is the normal case, 400000 means the card failed the self-test at 4 MHz and
// the bus was dropped back.
uint32_t sdSpiHz();

#endif  // HEMS_STORAGE_SDLOG_H
