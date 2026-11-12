#!/bin/sh
# Host test for the SD logger's pure logic: the CSV row and the parked-rows
# ring (src/storage/CsvRow.h, src/storage/RowQueue.h).
#
# Both are header-only and free of Arduino, SD and LVGL, so this compiles the
# shipped headers - not a copy - with nothing but a host C++ compiler. The card
# itself cannot be tested here; everything that decides whether a day of
# samples survives a card outage (buffer depth, FIFO order, month split, row
# format) is pure bookkeeping and is checked here.
#
# SPDX-License-Identifier: MIT
set -e

cd "$(dirname "$0")/../.."
OUT=${TMPDIR:-/tmp}/hems-sd-format-test
CXX=${CXX:-g++}

"$CXX" -std=c++17 -Wall -Wextra -Isrc -o "$OUT" tools/sd_format_test/test_rowqueue.cpp
"$OUT"
