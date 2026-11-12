// Host test for the two pure-logic pieces of the SD logger: the CSV row
// (src/storage/CsvRow.h) and the parked-rows ring (src/storage/RowQueue.h).
//
// Both are header-only and free of Arduino, SD and LVGL, so this compiles the
// *shipped* headers - not a copy - with nothing but a host compiler. The card
// itself cannot be tested here, which is exactly the point: the parts that
// decide whether a day of samples survives a card outage are pure bookkeeping,
// and this is where they get checked.
//
//   ./run.sh   (or: g++ -std=c++17 -Isrc -o /tmp/sdft test_rowqueue.cpp)
//
// SPDX-License-Identifier: MIT
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "storage/CsvRow.h"
#include "storage/RowQueue.h"

static int g_checks = 0;
static int g_failed = 0;

static void check(bool ok, const char *what) {
  g_checks++;
  if (!ok) {
    g_failed++;
    printf("FAIL  %s\n", what);
  }
}

static void checkEq(long got, long want, const char *what) {
  g_checks++;
  if (got != want) {
    g_failed++;
    printf("FAIL  %s: got %ld, want %ld\n", what, got, want);
  }
}

static bool near(float got, float want) {
  const float d = got - want;
  return d < 0.02f && d > -0.02f;
}

// Every column a different number, so a swapped pair in the format string
// cannot pass the round trip.
static WarpData makeData() {
  WarpData d;
  d.valid = true;
  d.gridL1 = 101;
  d.gridL2 = 102;
  d.gridL3 = 103;
  d.gridPower = 306;
  d.pvA = 201;
  d.pvB = 202;
  d.pvPower = 403;
  d.externalPower = 7;
  d.houseL1 = 301;
  d.houseL2 = 302;
  d.houseL3 = 303;
  d.housePower = 906;
  d.batteryPower = -451;
  d.hasBattery = true;
  d.soc = 62.5f;
  d.batteryVoltage = 51.5f;
  d.batteryCurrent = -8.75f;
  d.charger_count = 3;
  d.chargers[0].power = 401;
  d.chargers[1].power = 402;
  d.chargers[2].power = 403;
  d.charge_state = 2;
  d.charge_mode = 1;
  d.charge_current = 16000;
  d.island = true;
  d.hasHeating = true;
  d.heatingEnabled = true;
  d.sgrBlocking = true;
  d.sgrExtended = false;
  d.p14Enwg = true;
  d.hasAutarky = true;
  d.autarky = 42.5f;
  d.hasAutarkyToday = true;
  d.autarkyToday = 40.5f;
  d.hasForecast = true;
  d.forecastTodayWh = 12345;
  d.hasForecastRemaining = true;
  d.forecastTodayRemainingWh = 6789;
  d.pvEnergyWh = 111111;
  d.houseEnergyWh = 222222;
  d.gridImportWh = 333333;
  d.gridExportWh = 444444;
  d.batteryChargeWh = 555555;
  d.batteryDischargeWh = 666666;
  d.hasTemps = true;
  d.tempCurrent = 215;  // tenths of a degree -> 21.5
  return d;
}

static void testHeader() {
  int columns = 1;
  for (const char *p = csvrow::kHeader; *p != '\0'; ++p) {
    if (*p == ',') {
      ++columns;
    }
  }
  checkEq(columns, csvrow::kColumns, "header column count");
  check(std::strlen(csvrow::kHeader) > 0, "header is not empty");
}

static void testRoundTrip() {
  const WarpData d = makeData();
  csvrow::Row r;
  csvrow::fillRow(d, 1789142400u, r);

  char line[csvrow::kLineCap];
  const size_t n = csvrow::format(line, sizeof(line), r);
  check(n > 0, "format succeeds");
  check(n < csvrow::kLineCap, "format leaves room in the line buffer");
  check(std::strlen(line) == n, "format returns the written length");

  csvrow::Row back;
  check(csvrow::parse(line, back), "a written row parses back");

  checkEq((long)back.ts, 1789142400L, "timestamp");
  checkEq((long)back.gridL1, 101, "grid L1");
  checkEq((long)back.gridL2, 102, "grid L2");
  checkEq((long)back.gridL3, 103, "grid L3");
  checkEq((long)back.grid, 306, "grid total");
  checkEq((long)back.pvA, 201, "PV A");
  checkEq((long)back.pvB, 202, "PV B");
  checkEq((long)back.pv, 403, "PV total");
  checkEq((long)back.external, 7, "external");
  checkEq((long)back.houseL1, 301, "house L1");
  checkEq((long)back.houseL2, 302, "house L2");
  checkEq((long)back.houseL3, 303, "house L3");
  checkEq((long)back.house, 906, "house total");
  checkEq((long)back.batteryPower, -451, "battery power");
  check(near(back.soc, 62.5f), "SOC");
  check(near(back.batteryVoltage, 51.5f), "battery voltage");
  check(near(back.batteryCurrent, -8.75f), "battery current");
  checkEq((long)back.wb1, 401, "wallbox 1");
  checkEq((long)back.wb2, 402, "wallbox 2");
  checkEq((long)back.wb3, 403, "wallbox 3");
  checkEq((long)back.wbState, 2, "wallbox state");
  checkEq((long)back.wbMode, 1, "wallbox mode");
  checkEq((long)back.wbCurrent, 16000, "wallbox current");
  checkEq((long)back.island, 1, "island");
  checkEq((long)back.heatEnabled, 1, "heating enabled");
  checkEq((long)back.sgrBlocking, 1, "SG1");
  checkEq((long)back.sgrExtended, 0, "SG2");
  checkEq((long)back.p14, 1, "P14");
  check(near(back.autarky, 42.5f), "autarky");
  check(near(back.autarkyToday, 40.5f), "autarky today");
  checkEq((long)back.forecastTodayWh, 12345, "forecast today");
  checkEq((long)back.forecastRemainingWh, 6789, "forecast remaining");
  checkEq((long)back.pvTotalWh, 111111, "PV total");
  checkEq((long)back.houseTotalWh, 222222, "house total");
  checkEq((long)back.gridImportWh, 333333, "grid import");
  checkEq((long)back.gridExportWh, 444444, "grid export");
  checkEq((long)back.batChargeWh, 555555, "battery charge");
  checkEq((long)back.batDischargeWh, 666666, "battery discharge");
  check(near(back.tempC, 21.5f), "temperature");
}

static void testWorstCase() {
  // The widest row the format can plausibly produce: ten-digit timestamp,
  // seven-digit signed powers and counters, a negative forecast. The line
  // buffer has to hold it with room to spare, or a real day would silently
  // lose its rows.
  csvrow::Row r = {};
  r.ts = 4294967295u;
  r.gridL1 = 1234567.0f;
  r.gridL2 = -1234567.0f;
  r.gridL3 = 1234567.0f;
  r.grid = -1234567.0f;
  r.pvA = 1234567.0f;
  r.pvB = -1234567.0f;
  r.pv = 1234567.0f;
  r.external = -1234567.0f;
  r.houseL1 = 1234567.0f;
  r.houseL2 = -1234567.0f;
  r.houseL3 = 1234567.0f;
  r.house = -1234567.0f;
  r.batteryPower = -1234567.0f;
  r.soc = 100.0f;
  r.batteryVoltage = 999.9f;
  r.batteryCurrent = -12345.67f;
  r.wb1 = 1234567.0f;
  r.wb2 = -1234567.0f;
  r.wb3 = 1234567.0f;
  r.wbState = 10;
  r.wbMode = 10;
  r.wbCurrent = 32000;
  r.island = 1;
  r.heatEnabled = 1;
  r.sgrBlocking = 1;
  r.sgrExtended = 1;
  r.p14 = 1;
  r.autarky = 100.0f;
  r.autarkyToday = 100.0f;
  r.forecastTodayWh = 1000000000.0f;
  r.forecastRemainingWh = -1000000000.0f;
  r.pvTotalWh = 1000000000.0f;
  r.houseTotalWh = 1000000000.0f;
  r.gridImportWh = 1000000000.0f;
  r.gridExportWh = 1000000000.0f;
  r.batChargeWh = 1000000000.0f;
  r.batDischargeWh = 1000000000.0f;
  r.tempC = -999.0f;

  char line[csvrow::kLineCap];
  const size_t n = csvrow::format(line, sizeof(line), r);
  check(n > 0, "worst-case row fits the line buffer");
  check(n + 16 < csvrow::kLineCap, "worst-case row leaves a margin");

  csvrow::Row back;
  check(csvrow::parse(line, back), "worst-case row parses back");
}

static void testSentinels() {
  WarpData d;
  d.valid = true;
  csvrow::Row r;
  csvrow::fillRow(d, 1, r);
  char line[csvrow::kLineCap];
  check(csvrow::format(line, sizeof(line), r) > 0, "missing-data row formats");

  csvrow::Row back;
  check(csvrow::parse(line, back), "missing-data row parses");
  checkEq((long)back.soc, -1, "missing SOC is -1");
  checkEq((long)back.heatEnabled, -1, "missing heating is -1");
  checkEq((long)back.autarky, -1, "missing autarky is -1");
  checkEq((long)back.tempC, -999, "missing temperature is -999");
}

static void testStrictParse() {
  const WarpData d = makeData();
  csvrow::Row r;
  csvrow::fillRow(d, 1789142400u, r);
  char line[csvrow::kLineCap];
  csvrow::format(line, sizeof(line), r);

  csvrow::Row back;
  check(!csvrow::parse(csvrow::kHeader, back), "header is not a data row");

  // A row with one value missing is refused rather than half-understood.
  std::string shortRow(line);
  const size_t lastComma = shortRow.rfind(',');
  shortRow.erase(lastComma);
  check(!csvrow::parse(shortRow.c_str(), back), "short row is refused");

  // Trailing characters are refused too.
  std::string junk(line);
  junk += "x";
  check(!csvrow::parse(junk.c_str(), back), "trailing junk is refused");

  // A complete row with a CRLF ending is accepted, as written by println().
  std::string crlf(line);
  crlf += "\r\n";
  check(csvrow::parse(crlf.c_str(), back), "CRLF row is accepted");
}

static void testSampleMapping() {
  csvrow::Row r = {};
  r.heatEnabled = 1;
  r.sgrBlocking = 0;
  r.sgrExtended = 0;
  csvrow::Sample s;
  csvrow::toSample(r, s);
  checkEq(s.heat, 0, "heat state: none");

  r.sgrBlocking = 1;
  csvrow::toSample(r, s);
  checkEq(s.heat, 1, "heat state: SG1");

  r.sgrBlocking = 0;
  r.sgrExtended = 1;
  csvrow::toSample(r, s);
  checkEq(s.heat, 2, "heat state: SG2");

  r.sgrBlocking = 1;
  r.sgrExtended = 1;
  csvrow::toSample(r, s);
  checkEq(s.heat, 3, "heat state: both");

  r.heatEnabled = -1;
  csvrow::toSample(r, s);
  checkEq(s.heat, -1, "heat state: no data");
}

static void testQueue() {
  hemsrow::Slot slots[3];
  hemsrow::Queue q;
  q.attach(slots, 3);
  check(q.empty(), "new queue is empty");

  q.push("row1", "/hist/HEMS-202611.csv");
  q.push("row2", "/hist/HEMS-202611.csv");
  q.push("row3", "/hist/HEMS-202612.csv");
  checkEq(q.count(), 3, "queue holds three rows");
  check(std::strcmp(q.front()->line, "row1") == 0, "FIFO: oldest first");
  check(std::strcmp(q.front()->path, "/hist/HEMS-202611.csv") == 0,
        "slot keeps its file");

  q.push("row4", "/hist/HEMS-202612.csv");
  checkEq(q.count(), 3, "overflow keeps the capacity");
  checkEq((long)q.dropped(), 1, "overflow counts the lost row");
  check(std::strcmp(q.front()->line, "row2") == 0,
        "overflow drops the oldest row");

  // A line that would be truncated is counted as lost, not stored broken.
  std::string tooLong(csvrow::kLineCap, 'x');
  q.push(tooLong.c_str(), "/hist/HEMS-202612.csv");
  checkEq((long)q.dropped(), 2, "overlong row is counted as lost");

  q.pop();
  check(std::strcmp(q.front()->line, "row3") == 0, "pop advances the FIFO");
}

// Sink that records what the queue hands it, and can fail on demand.
struct FakeSink {
  std::vector<std::string> opens;
  std::vector<std::string> lines;
  const char *failOn = nullptr;
  bool failOpen = false;

  bool open(const char *path) {
    if (failOpen) {
      return false;
    }
    opens.push_back(path);
    return true;
  }
  bool append(const char *line) {
    if (failOn != nullptr && std::strcmp(line, failOn) == 0) {
      return false;
    }
    lines.push_back(line);
    return true;
  }
  void close() {}
};

static void testFlushGrouped() {
  hemsrow::Slot slots[6];
  hemsrow::Queue q;
  q.attach(slots, 6);
  q.push("a1", "/hist/HEMS-202611.csv");
  q.push("a2", "/hist/HEMS-202611.csv");
  q.push("b1", "/hist/HEMS-202612.csv");
  q.push("b2", "/hist/HEMS-202612.csv");

  FakeSink sink;
  checkEq(q.flushGrouped(sink), 4, "flush writes every row");
  check(q.empty(), "flush empties the queue");
  checkEq((long)sink.opens.size(), 2, "one open per file, not per row");
  checkEq((long)sink.lines.size(), 4, "flush preserves every line");
  check(sink.lines[0] == "a1" && sink.lines[2] == "b1",
        "flush preserves the order");

  // A failing write stops the flush and keeps the failing row and the rest.
  q.push("c1", "/hist/HEMS-202612.csv");
  q.push("c2", "/hist/HEMS-202612.csv");
  FakeSink failing;
  failing.failOn = "c2";
  checkEq(q.flushGrouped(failing), 1, "failed write stops the flush");
  checkEq(q.count(), 1, "failing row stays parked");
  check(std::strcmp(q.front()->line, "c2") == 0, "the right row stays parked");

  // A card that is gone keeps everything.
  FakeSink gone;
  gone.failOpen = true;
  checkEq(q.flushGrouped(gone), 0, "missing card writes nothing");
  checkEq(q.count(), 1, "missing card keeps the parked rows");
}

int main() {
  testHeader();
  testRoundTrip();
  testWorstCase();
  testSentinels();
  testStrictParse();
  testSampleMapping();
  testQueue();
  testFlushGrouped();

  if (g_failed == 0) {
    printf("OK  %d checks\n", g_checks);
    return 0;
  }
  printf("FAILED  %d of %d checks\n", g_failed, g_checks);
  return 1;
}
