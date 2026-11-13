// SD-card history logging (docs/sd-log.md).
//
// CSV, one file per calendar month on a FAT32 microSD in the TF slot:
//   sdInit()       -> starts the worker task that owns the card
//   sdLogSample()  -> queues one row (caller drives the 1-minute cadence)
//   sdRequestHistory()/sdTakeHistory() -> asynchronous 24 h restore
//
// Why a task: SD.begin() and a history scan each take hundreds of milliseconds
// to seconds. LVGL runs on the same task as this file's callers, so a card
// operation on that task would freeze the whole panel. The worker owns the card
// exclusively (the ESP32 SD/FS layer is not thread-safe); the GUI thread only
// posts messages and copies the status out under a lock.
//
// SPI wiring on the Waveshare board: SCK 12, MISO 13, MOSI 11, and a dummy
// GPIO 15 for the SD library. The real chip select is EXIO4 on the CH422G
// expander and is driven low once at mount (see board/waveshare_touch_lcd7/
// Pins.h).
//
// SPDX-License-Identifier: MIT
#include "storage/SdLog.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "board/BoardPins.h"
#include "board/Ch422G.h"
#include "storage/RowQueue.h"

namespace {

SPIClass s_spi(FSPI);
volatile bool s_mounted = false;
uint32_t s_nextMountMs = 0;
constexpr uint32_t kMountRetryMs = 10000;

// 4 MHz first, 400 kHz as the fallback that works on marginal cards. The SD
// stack runs every transaction, card detection included, at the given clock,
// so a fast clock is only kept when the write+read-back probe proves it.
constexpr uint32_t kSdFastHz = 4000000;
constexpr uint32_t kSdSlowHz = 400000;
int s_fastUsable = -1;  // -1 untested, 1 fast works, 0 dropped to 400 kHz
uint32_t s_spiHz = kSdSlowHz;
bool s_warnLogged = false;

constexpr int kHistMaxRows = 24 * 60 + 2;  // matches the UI ring
constexpr int kQueueCap = kHistMaxRows;    // 24 h in PSRAM
constexpr int kQueueCapNoPsram = 60;       // 1 h in internal RAM
constexpr uint32_t kQueueRetryMs = 5000;
constexpr uint32_t kProbeMs = 5000;

// The row format and its reader live in storage/CsvRow.h, so the writer here
// and the history scan below cannot drift apart.
constexpr size_t kLineCap = csvrow::kLineCap;
constexpr size_t kPathCap = csvrow::kPathCap;

char s_status[64] = "SD: --";

// --------------------------------------------------------------------------
// Parked rows
//
// The card can be pulled out while the panel is running, and a write can fail
// for other reasons too (card full, marginal card). Dropping the row would
// punch a hole in the 24 h chart, so a row that cannot be written right now is
// parked in RAM and retried later. 1442 slots = 24 h at the 1-minute cadence,
// stored in PSRAM (about 0.5 MB); the fallback without PSRAM is the 60 slots
// in internal RAM that cover one hour. On overflow the oldest row goes, because
// recent data is what the chart needs. A row is stored already formatted, so it
// keeps its original timestamp, and it remembers the file it belongs to - a
// month rollover during the outage then still splits correctly.
// --------------------------------------------------------------------------
// Die kleine Fallback-Warteschlange wird erst angelegt, wenn kein PSRAM
// da ist: 60 Zeilen x 360 B waeren 21,6 kB internen RAM umsonst, und gerade
// die braucht der Heap fuer Wi-Fi und HTTPClient.
hemsrow::Slot *s_queueSlots = nullptr;
hemsrow::Queue s_queue;
uint32_t s_nextQueueRetryMs = 0;
uint32_t s_nextProbeMs = 0;

// --------------------------------------------------------------------------
// Worker task
// --------------------------------------------------------------------------
enum SdReqType : uint8_t { SDREQ_LOG, SDREQ_HISTORY };

struct SdReq {
  uint8_t type;
  int maxRows;        // SDREQ_HISTORY
  bool waitForClock;  // SDREQ_HISTORY
  char line[kLineCap];
  char path[kPathCap];
};

constexpr int kReqQueueLen = 8;
QueueHandle_t s_reqQ = nullptr;
SemaphoreHandle_t s_lock = nullptr;

// History restore result, written by the worker, collected by the GUI.
SdHistSample *s_histOut = nullptr;
SdHistSample *s_histRing = nullptr;
SdHistSample *s_histPrevRing = nullptr;
int s_histReady = 0;
int s_histCount = 0;
bool s_histDeferred = false;

// "202611" from the current wall clock, or nullptr when it is not valid yet.
// The timestamps in the CSV are unix seconds, so the file name is derived from
// the same UTC calendar.
const char *monthKey() {
  static char key[16];
  const time_t nowT = time(nullptr);
  struct tm tmv;
  if (nowT < 1000000000 || gmtime_r(&nowT, &tmv) == nullptr) {
    return nullptr;
  }
  snprintf(key, sizeof(key), "%04d%02d", tmv.tm_year + 1900, tmv.tm_mon + 1);
  return key;
}

// Uptime key (days since boot) until the wall clock is valid. esp_timer is
// 64-bit, so no millis() wrap-around over the panel lifetime.
const char *uptimeKey() {
  static char key[16];
  const uint64_t days = esp_timer_get_time() / UINT64_C(86400000000);
  snprintf(key, sizeof(key), "UPT-%llu", (unsigned long long)days);
  return key;
}

// Absolute path of the file for this moment. Pure: safe to call from the GUI
// thread while the worker is writing another file.
void buildPath(char *out, size_t cap) {
  const char *key = monthKey();
  if (key != nullptr) {
    snprintf(out, cap, "/hist/HEMS-%s.csv", key);
  } else {
    snprintf(out, cap, "/hist/%s.csv", uptimeKey());
  }
}

// --------------------------------------------------------------------------
// Mount self-test: write /hist/PROBE, read it back, compare. A write-only
// probe proves nothing about a raised SPI clock; what breaks at high speed is
// the data coming back.
// --------------------------------------------------------------------------
constexpr size_t kProbeBytes = 512;
uint8_t s_probeTx[kProbeBytes];
uint8_t s_probeRx[kProbeBytes];

uint8_t probePattern(size_t i) { return (uint8_t)(i * 37 + 11); }

bool probeMount(float *kbsOut) {
  for (size_t i = 0; i < kProbeBytes; i++) {
    s_probeTx[i] = probePattern(i);
  }
  const int64_t t0 = esp_timer_get_time();
  {
    File probe = SD.open("/hist/PROBE", FILE_WRITE);
    if (!probe) {
      Serial.println(F("SD: could not write /hist/PROBE (self-test failed)"));
      return false;
    }
    const size_t wrote = probe.write(s_probeTx, kProbeBytes);
    probe.flush();
    probe.close();
    if (wrote != kProbeBytes) {
      Serial.printf("SD: self-test short write %u/%u\n", (unsigned)wrote,
                    (unsigned)kProbeBytes);
      return false;
    }
  }
  {
    File in = SD.open("/hist/PROBE", FILE_READ);
    if (!in) {
      Serial.println(F("SD: could not read /hist/PROBE (self-test failed)"));
      return false;
    }
    const size_t got = in.read(s_probeRx, kProbeBytes);
    in.close();
    if (got != kProbeBytes) {
      Serial.printf("SD: self-test short read %u/%u\n", (unsigned)got,
                    (unsigned)kProbeBytes);
      return false;
    }
  }
  for (size_t i = 0; i < kProbeBytes; i++) {
    if (s_probeRx[i] != probePattern(i)) {
      Serial.printf("SD: self-test mismatch at byte %u\n", (unsigned)i);
      return false;
    }
  }
  SD.remove("/hist/PROBE");
  const int64_t dt = esp_timer_get_time() - t0;
  if (kbsOut != nullptr) {
    *kbsOut = dt > 0 ? (float)(2.0 * (double)kProbeBytes * 1000.0 / (double)dt)
                     : 0.0f;
  }
  return true;
}

// One mount attempt at a given clock: bus, mount, /hist, self-test. Leaves the
// card unmounted on any failure so the next attempt starts from a clean bus.
bool tryMount(uint32_t hz, float *kbsOut) {
#if !BOARD_SD_CS_IS_GPIO
  // The card's chip select is EXIO4 on the expander. Drive it once, low, and
  // mount with the dummy pin from the board profile. Low is selected, and the
  // bus carries nothing else, so it stays selected.
  static bool reported = false;
  if (!ch422gFound()) {
    if (!reported) {
      reported = true;
      Serial.printf(
          "SD: kein IO-Expander - EXIO%d bleibt hoch, die Karte ist nicht "
          "auswaehlbar\n",
          BOARD_CH422G_PIN_SD_CS);
    }
    return false;
  }
  ch422gWrite(BOARD_CH422G_PIN_SD_CS, false);
#endif
  s_spi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  if (!SD.begin(PIN_SD_CS, s_spi, hz, "/sd", 4)) {
    SD.end();
    return false;
  }
  if (!SD.exists("/hist") && !SD.mkdir("/hist")) {
    Serial.println(F("SD: /hist laesst sich nicht anlegen"));
    SD.end();
    return false;
  }
  if (!probeMount(kbsOut)) {
    SD.end();
    return false;
  }
  return true;
}

// A month file keeps the header it was created with. A file from an older
// format is worth one line in the log; the reader skips rows it cannot parse.
void reportForeignHeader(const char *path) {
  static char lastPath[kPathCap];
  if (strcmp(lastPath, path) == 0) {
    return;
  }
  strlcpy(lastPath, path, sizeof(lastPath));

  File f = SD.open(path, FILE_READ);
  if (!f) {
    return;
  }
  const size_t size = f.size();
  if (size == 0) {
    f.close();
    return;
  }
  static char line[csvrow::kLineCap];
  const size_t got = f.read((uint8_t *)line, sizeof(line) - 1);
  f.close();
  line[got] = '\0';
  if (strncmp(line, "ts,", 3) != 0) {
    Serial.printf("SD: %s hat eine fremde Kopfzeile\n", path);
  }
}

// Append one formatted row. Returns 1 when it was written as the first row of
// a fresh file, 0 when it was appended, and -1 on any failure.
//
// The result is taken from println()'s return value, not from getWriteError():
// the ESP32 core's FS write path never sets that flag, it just returns the byte
// count it managed to write.
int appendRow(const char *path, const char *line) {
  reportForeignHeader(path);
  File f = SD.open(path, FILE_APPEND);
  if (!f) {
    return -1;
  }
  const bool freshFile = (f.size() == 0);
  if (freshFile) {
    f.println(csvrow::kHeader);
  }
  const size_t want = strlen(line) + 2;  // trailing "\r\n"
  const size_t got = f.println(line);
  f.flush();
  f.close();
  return got >= want ? (freshFile ? 1 : 0) : -1;
}

// The card as a sink for hemsrow::Queue::flushGrouped(): consecutive rows of
// one file share a single open, which matters after a long outage.
struct CardSink {
  File f;
  bool open(const char *path) {
    reportForeignHeader(path);
    f = SD.open(path, FILE_APPEND);
    if (!f) {
      return false;
    }
    if (f.size() == 0) {
      f.println(csvrow::kHeader);
    }
    return true;
  }
  bool append(const char *line) { return f.println(line) >= strlen(line) + 2; }
  void close() {
    f.flush();
    f.close();
  }
};

void setStatusLocked(const char *text) {
  if (s_lock != nullptr) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
  }
  strlcpy(s_status, text, sizeof(s_status));
  if (s_lock != nullptr) {
    xSemaphoreGive(s_lock);
  }
}

// "12 min", "1 h", "24 h" - how far back the oldest parked row reaches.
void queueSpan(int rows, char *out, size_t cap) {
  const int minutes = rows;  // one row per log interval
  if (minutes < 60) {
    snprintf(out, cap, "%d min", minutes);
  } else {
    snprintf(out, cap, "%d h", minutes / 60);
  }
}

void buildStatus() {
  char buf[64];
  char span[12];
  const int parked = s_queue.count();
  if (parked > 0) {
    queueSpan(parked, span, sizeof(span));
  }
#ifdef WARP_LANG_EN
  if (!s_mounted) {
    if (parked > 0) {
      snprintf(buf, sizeof(buf), "SD: -- | %d queued (%s)", parked, span);
    } else {
      snprintf(buf, sizeof(buf), "SD: --");
    }
  } else if (s_queue.dropped() > 0) {
    snprintf(buf, sizeof(buf), "SD: OK | %u rows lost",
             (unsigned)s_queue.dropped());
  } else if (parked > 0) {
    snprintf(buf, sizeof(buf), "SD: OK | %d queued (%s), %.1f GB free", parked,
             span, (double)(SD.totalBytes() - SD.usedBytes()) / 1.0e9);
  } else {
    snprintf(buf, sizeof(buf), "SD: OK | %.1f GB free",
             (double)(SD.totalBytes() - SD.usedBytes()) / 1.0e9);
  }
#else
  if (!s_mounted) {
    if (parked > 0) {
      snprintf(buf, sizeof(buf), "SD: -- | %d gepuffert (%s)", parked, span);
    } else {
      snprintf(buf, sizeof(buf), "SD: --");
    }
  } else if (s_queue.dropped() > 0) {
    snprintf(buf, sizeof(buf), "SD: OK | %u Zeilen verloren",
             (unsigned)s_queue.dropped());
  } else if (parked > 0) {
    snprintf(buf, sizeof(buf), "SD: OK | %d gepuffert (%s), %.1f GB frei",
             parked, span, (double)(SD.totalBytes() - SD.usedBytes()) / 1.0e9);
  } else {
    snprintf(buf, sizeof(buf), "SD: OK | %.1f GB frei",
             (double)(SD.totalBytes() - SD.usedBytes()) / 1.0e9);
  }
#endif
  setStatusLocked(buf);
}

// Retry parked rows. Stops at the first failure and keeps the rest, so the
// rows stay in chronological order inside the file.
void queueFlush() {
  CardSink sink;
  const int written = s_queue.flushGrouped(sink);
  if (written > 0) {
    Serial.printf("SD: %d gepufferte Zeile(n) nachgeschrieben\n", written);
    buildStatus();
  }
}

// --------------------------------------------------------------------------
// History restore: read the newest CSV rows back into the chart. Rows are
// appended in chronological order, so a forward scan plus a rolling window of
// maxRows keeps the newest rows without huge buffers.
// --------------------------------------------------------------------------

// Generous upper bound for one row: it only decides where the scan starts, so
// it has to stay above the longest row the format can produce.
constexpr size_t kRowBytesEst = csvrow::kLineCap;
constexpr size_t kScanBlock = 8192;

// Scan rows of `path` into `ring` (raw ring layout, one slot per parsed row
// modulo ringCap). Returns the number of parsed rows; the newest
// min(total, ringCap) rows survive in the ring.
int scanFile(const char *path, SdHistSample *ring, int ringCap) {
  if (ring == nullptr || ringCap <= 0) {
    return 0;
  }
  File f = SD.open(path, FILE_READ);
  if (!f) {
    return 0;
  }

  const size_t want = (size_t)ringCap * kRowBytesEst;
  const size_t fileSize = f.size();
  const size_t from = fileSize > want ? fileSize - want : 0;
  f.seek(from);

  static char block[kScanBlock];
  int total = 0;
  size_t carry = 0;
  bool atBoundary = from == 0;

  const uint32_t scanT0 = millis();
  while (true) {
    const size_t got = f.read((uint8_t *)block + carry, sizeof(block) - carry);
    if (got == 0) {
      break;
    }
    const size_t avail = carry + got;

    size_t lineStart = 0;
    if (!atBoundary) {
      while (lineStart < avail && block[lineStart] != '\n') {
        lineStart++;
      }
      if (lineStart >= avail) {
        break;  // no line end in a whole block; kLineCap rules this out
      }
      lineStart++;
      atBoundary = true;
    }

    while (lineStart < avail) {
      const char *nl =
          (const char *)memchr(block + lineStart, '\n', avail - lineStart);
      if (nl == nullptr) {
        break;  // unfinished line: carried over to the next block
      }
      const size_t lineEnd = (size_t)(nl - block);
      size_t len = lineEnd - lineStart;
      char *line = block + lineStart;
      if (len > 0 && line[len - 1] == '\r') {
        len--;  // CRLF, as written by Print.println()
      }
      line[len] = '\0';

      if (len >= 12 && strncmp(line, "ts,", 3) != 0) {
        csvrow::Row r;
        if (csvrow::parse(line, r)) {
          csvrow::toSample(r, ring[total % ringCap]);
          total++;
        }
      }
      lineStart = lineEnd + 1;
    }

    carry = avail - lineStart;
    if (carry > 0) {
      memmove(block, block + lineStart, carry);
    }
  }
  f.close();
  if (fileSize > want) {
    Serial.printf("SD: history scan %u B -> %u B, %d Zeilen, %lu ms\n",
                  (unsigned)fileSize, (unsigned)from, total,
                  (unsigned long)(millis() - scanT0));
  }
  return total;
}

// Move the newest min(total, cap) rows from the ring to dst, chronological.
void drainRing(const SdHistSample *ring, int total, int cap,
               SdHistSample *dst) {
  const int kept = total > cap ? cap : total;
  const int start = total - kept;
  for (int i = 0; i < kept; i++) {
    dst[i] = ring[(start + i) % cap];
  }
}

// "202610" for "202611" (wraps the year), nullptr when not derivable.
const char *prevMonthKey(char *buf, size_t len) {
  const char *key = monthKey();
  int y = 0, m = 0;
  if (key == nullptr || sscanf(key, "%4d%2d", &y, &m) != 2) {
    return nullptr;
  }
  if (m == 1) {
    y--;
    m = 12;
  } else {
    m--;
  }
  snprintf(buf, len, "%04d%02d", y, m);
  return buf;
}

// Worker side of SDREQ_HISTORY.
void sdWorkerReadHistory(int maxRows, bool waitForClock) {
  int count = 0;
  bool deferred = false;

  if (s_mounted) {
    const char *key = monthKey();
    if (key == nullptr && waitForClock) {
      deferred = true;
    } else if (s_histRing != nullptr && s_histOut != nullptr) {
      char path[40];
      if (key != nullptr) {
        snprintf(path, sizeof(path), "/hist/HEMS-%s.csv", key);
      } else {
        snprintf(path, sizeof(path), "/hist/%s.csv", uptimeKey());
      }

      char prevKeyBuf[16];
      char prevPath[40] = "";
      const char *prevKey = key != nullptr ? prevMonthKey(prevKeyBuf,
                                                          sizeof(prevKeyBuf))
                                           : nullptr;
      if (prevKey != nullptr) {
        snprintf(prevPath, sizeof(prevPath), "/hist/HEMS-%s.csv", prevKey);
      }

      const int cur = scanFile(path, s_histRing, maxRows);
      if (cur > 0) {
        if (cur >= maxRows || prevPath[0] == '\0') {
          drainRing(s_histRing, cur, cur > maxRows ? maxRows : cur, s_histOut);
          count = cur > maxRows ? maxRows : cur;
        } else {
          // Current file alone has fewer than maxRows rows - the 24 h window
          // reaches across a calendar boundary. Prepend the previous month's
          // newest rows.
          const int need = maxRows - cur;
          const int prev = scanFile(prevPath, s_histPrevRing, need);
          if (prev <= 0) {
            drainRing(s_histRing, cur, cur, s_histOut);
            count = cur;
          } else {
            const int prevKept = prev > need ? need : prev;
            drainRing(s_histPrevRing, prev, need, s_histOut);
            drainRing(s_histRing, cur, cur, s_histOut + prevKept);
            count = prevKept + cur;
          }
        }
      } else if (prevPath[0] != '\0') {
        // Early in a new month nothing is logged into its file yet, but the
        // last 24 h are all in the previous month's file.
        const int prev = scanFile(prevPath, s_histRing, maxRows);
        if (prev > 0) {
          drainRing(s_histRing, prev, prev > maxRows ? maxRows : prev, s_histOut);
          count = prev > maxRows ? maxRows : prev;
        }
      }
    }
  }

  if (s_lock != nullptr) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
  }
  s_histCount = count;
  s_histDeferred = deferred;
  s_histReady = 1;
  if (s_lock != nullptr) {
    xSemaphoreGive(s_lock);
  }
}

// Periodic card duties, owned by the worker: mount retry, presence watch and
// flushing parked rows.
void sdWorkerPeriodic(uint32_t now) {
  if (s_mounted) {
    // A card pulled out in operation is invisible until something writes and
    // that write fails, which can be a minute away. Reading the card size
    // detects removal within seconds.
    if ((int32_t)(now - s_nextProbeMs) >= 0) {
      s_nextProbeMs = now + kProbeMs;
      if (SD.cardSize() == 0) {
        if (!s_warnLogged) {
          Serial.println(F("SD: card no longer present"));
          s_warnLogged = true;
        }
        s_mounted = false;
        SD.end();
        buildStatus();
      } else {
        s_warnLogged = false;
      }
    }
    if (!s_queue.empty() && (int32_t)(now - s_nextQueueRetryMs) >= 0) {
      s_nextQueueRetryMs = now + kQueueRetryMs;
      queueFlush();
    }
    return;
  }
  if ((int32_t)(now - s_nextMountMs) < 0) {
    return;
  }
  s_nextMountMs = now + kMountRetryMs;

  uint32_t hz = s_fastUsable == 0 ? kSdSlowHz : kSdFastHz;
  float kbs = 0.0f;
  if (!tryMount(hz, &kbs)) {
    if (hz == kSdSlowHz) {
      return;
    }
    Serial.printf("SD: %lu Hz unbrauchbar, weiche auf %lu Hz zurueck\n",
                  (unsigned long)hz, (unsigned long)kSdSlowHz);
    s_fastUsable = 0;
    hz = kSdSlowHz;
    if (!tryMount(hz, &kbs)) {
      return;
    }
  } else if (s_fastUsable < 0) {
    s_fastUsable = 1;
  }
  s_spiHz = hz;
  s_mounted = true;
  s_warnLogged = false;
  char path[40];
  buildPath(path, sizeof(path));
  buildStatus();
  Serial.printf("SD: mounted at %lu Hz, self-test %.1f kB/s, %.1f GB free (%s)\n",
                (unsigned long)hz, (double)kbs,
                (double)(SD.totalBytes() - SD.usedBytes()) / 1.0e9, path);
  if (!s_queue.empty()) {
    s_nextQueueRetryMs = millis();
    queueFlush();
  }
}

// Worker side of SDREQ_LOG: actually touch the card, or park the row.
void sdWorkerWriteRow(const SdReq &req) {
  if (s_mounted) {
    const int r = appendRow(req.path, req.line);
    if (r >= 0) {
      buildStatus();
      return;
    }
    if (!s_warnLogged) {
      s_warnLogged = true;
      Serial.printf("SD: write to %s failed, card treated as gone\n", req.path);
    }
    s_mounted = false;
    SD.end();
  }
  s_queue.push(req.line, req.path);
  buildStatus();
}

void sdTask(void *) {
  s_nextMountMs = millis() + 500;
  for (;;) {
    SdReq req;
    while (s_reqQ != nullptr && xQueueReceive(s_reqQ, &req, 0) == pdTRUE) {
      switch (req.type) {
        case SDREQ_LOG:
          sdWorkerWriteRow(req);
          break;
        case SDREQ_HISTORY:
          sdWorkerReadHistory(req.maxRows, req.waitForClock);
          break;
        default:
          break;
      }
    }
    sdWorkerPeriodic(millis());
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

}  // namespace

void sdInit() {
  if (s_reqQ != nullptr) {
    return;  // already running
  }
  s_reqQ = xQueueCreate(kReqQueueLen, sizeof(SdReq));
  s_lock = xSemaphoreCreateMutex();

  hemsrow::Slot *bigSlots = (hemsrow::Slot *)heap_caps_malloc(
      sizeof(hemsrow::Slot) * kQueueCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (bigSlots != nullptr) {
    s_queue.attach(bigSlots, kQueueCap);
    Serial.printf("SD: Puffer %d Zeilen / 24 h (%u bytes, PSRAM)\n", kQueueCap,
                  (unsigned)(sizeof(hemsrow::Slot) * kQueueCap));
  } else {
    s_queueSlots = (hemsrow::Slot *)heap_caps_malloc(
        sizeof(hemsrow::Slot) * kQueueCapNoPsram,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_queueSlots != nullptr) {
      s_queue.attach(s_queueSlots, kQueueCapNoPsram);
      Serial.printf("SD: Puffer nur %d Zeilen / 1 h (kein PSRAM)\n",
                    kQueueCapNoPsram);
    } else {
      Serial.println(F("SD: kein Puffer, geparkte Zeilen gehen verloren"));
    }
  }

  // Three history buffers: result, current-month ring, previous-month ring.
  // The scan is asynchronous and the GUI only reads the result under the lock.
  SdHistSample *pool = (SdHistSample *)heap_caps_malloc(
      sizeof(SdHistSample) * kHistMaxRows * 3,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (pool != nullptr) {
    s_histOut = pool;
    s_histRing = pool + kHistMaxRows;
    s_histPrevRing = pool + kHistMaxRows * 2;
    Serial.printf("SD: Verlaufspuffer %d Zeilen (%u bytes, PSRAM)\n",
                  kHistMaxRows,
                  (unsigned)(sizeof(SdHistSample) * kHistMaxRows * 3));
  } else {
    Serial.println(F("SD: kein PSRAM fuer den Verlauf, 24-h-Restore aus"));
  }

  buildStatus();
  xTaskCreate(sdTask, "sd", 4096, nullptr, 1, nullptr);
}

void sdLogSample(const WarpData &d) {
  if (!d.valid) {
    return;  // no zero rows for a disconnected device
  }
  const time_t nowT = time(nullptr);
  if (nowT < 1000000000) {
    return;  // no rows with an unusable timestamp
  }

  SdReq req;
  memset(&req, 0, sizeof(req));
  req.type = SDREQ_LOG;
  buildPath(req.path, sizeof(req.path));
  csvrow::Row row;
  csvrow::fillRow(d, (uint32_t)nowT, row);
  if (csvrow::format(req.line, sizeof(req.line), row) == 0) {
    Serial.println(F("SD: Zeile zu lang, verworfen"));
    return;
  }

  if (s_reqQ == nullptr || xQueueSend(s_reqQ, &req, 0) != pdTRUE) {
    if (s_lock != nullptr) {
      xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    s_queue.noteDropped();
    if (s_lock != nullptr) {
      xSemaphoreGive(s_lock);
    }
    buildStatus();
    Serial.println(F("SD: worker unreachable, row dropped"));
  }
}

const char *sdStatusText() {
  static char buf[64];
  if (s_lock != nullptr) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(buf, s_status, sizeof(buf));
    xSemaphoreGive(s_lock);
  } else {
    strlcpy(buf, s_status, sizeof(buf));
  }
  return buf;
}

bool sdMounted() { return s_mounted; }

uint32_t sdSpiHz() { return s_mounted ? s_spiHz : 0; }

void sdRequestHistory(int maxRows, bool waitForClock) {
  if (s_reqQ == nullptr || maxRows <= 0 || maxRows > kHistMaxRows) {
    return;
  }
  SdReq req;
  memset(&req, 0, sizeof(req));
  req.type = SDREQ_HISTORY;
  req.maxRows = maxRows;
  req.waitForClock = waitForClock;
  xQueueSend(s_reqQ, &req, 0);
}

int sdTakeHistory(SdHistSample *out, int maxRows) {
  if (out == nullptr || s_lock == nullptr) {
    return -2;
  }
  if (s_histOut == nullptr) {
    return 0;  // no PSRAM for the history buffers; nothing to restore
  }
  int rc = -2;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  if (s_histReady) {
    if (s_histDeferred) {
      rc = -1;
    } else {
      const int n = s_histCount < maxRows ? s_histCount : maxRows;
      memcpy(out, s_histOut, (size_t)n * sizeof(SdHistSample));
      rc = n;
    }
    s_histReady = 0;
  }
  xSemaphoreGive(s_lock);
  return rc;
}
