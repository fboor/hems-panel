// Emulator stand-in for src/storage/SdLog.cpp - see SimSdLog.h.
//
// SPDX-License-Identifier: MIT
#include "SimSdLog.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "storage/RowQueue.h"

namespace simsd {
namespace {

constexpr int kHistMaxRows = 24 * 60 + 2;
constexpr int kQueueCap = kHistMaxRows;
constexpr uint32_t kRetryMs = 5000;

bool g_enabled = false;
bool g_mounted = false;
std::string g_dir;
std::string g_histDir;

hemsrow::Slot g_slots[kQueueCap];
hemsrow::Queue g_queue;
uint32_t g_nextRetryMs = 0;
char g_status[96] = "SD: aus";

// mkdir -p for the two levels the log needs. Plain POSIX so the emulator stays
// free of extra dependencies.
bool mkdirs(const std::string &path) {
  if (path.empty()) {
    return false;
  }
  std::string cur;
  size_t i = 0;
  if (path[0] == '/') {
    cur = "/";
    i = 1;
  }
  while (i <= path.size()) {
    const size_t slash = path.find('/', i);
    const size_t end = slash == std::string::npos ? path.size() : slash;
    if (end > i) {
      cur.append(path, i, end - i);
      if (mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) {
        return false;
      }
      cur.push_back('/');
    }
    if (slash == std::string::npos) {
      break;
    }
    i = slash + 1;
  }
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// "202611" from the current wall clock, UTC - the same calendar as the unix
// timestamps in the rows.
void monthKey(char *out, size_t cap) {
  const time_t now = time(nullptr);
  struct tm tmv;
  if (now < 1000000000 || gmtime_r(&now, &tmv) == nullptr) {
    snprintf(out, cap, "UPT");
    return;
  }
  snprintf(out, cap, "%04d%02d", tmv.tm_year + 1900, tmv.tm_mon + 1);
}

void buildPath(char *out, size_t cap) {
  char key[32];
  monthKey(key, sizeof(key));
  snprintf(out, cap, "%s/HEMS-%s.csv", g_histDir.c_str(), key);
}

void buildStatus() {
#ifdef WARP_LANG_EN
  const char *queuedWord = "queued";
  const char *lostWord = "lost";
#else
  const char *queuedWord = "gepuffert";
  const char *lostWord = "verloren";
#endif
  const int parked = g_queue.count();
  const unsigned lost = g_queue.dropped();

  if (!g_enabled) {
    snprintf(g_status, sizeof(g_status), "SD: aus");
  } else if (!g_mounted) {
    snprintf(g_status, sizeof(g_status), "SD: FEHLER | %.48s", g_dir.c_str());
  } else if (lost > 0) {
    snprintf(g_status, sizeof(g_status), "SD: OK | %u %s | %.32s", lost,
             lostWord, g_dir.c_str());
  } else if (parked > 0) {
    snprintf(g_status, sizeof(g_status), "SD: OK | %d %s | %.32s", parked,
             queuedWord, g_dir.c_str());
  } else {
    snprintf(g_status, sizeof(g_status), "SD: OK | %.48s", g_dir.c_str());
  }
}

// One append. Returns false on any short write, so the caller can park the row.
bool writeRow(const char *path, const char *line) {
  FILE *f = fopen(path, "ab");
  if (f == nullptr) {
    return false;
  }
  bool ok = true;
  if (fseek(f, 0, SEEK_END) == 0 && ftell(f) == 0) {
    ok = fputs(csvrow::kHeader, f) >= 0 && fputc('\n', f) != EOF;
  }
  if (ok) {
    ok = fputs(line, f) >= 0 && fputc('\n', f) != EOF;
  }
  if (fflush(f) != 0) {
    ok = false;
  }
  if (fclose(f) != 0) {
    ok = false;
  }
  return ok;
}

// Sink for hemsrow::Queue::flushGrouped(): consecutive rows of one file share
// one open, exactly like the firmware's CardSink.
struct FileSink {
  FILE *f = nullptr;
  bool open(const char *path) {
    f = fopen(path, "ab");
    if (f == nullptr) {
      return false;
    }
    if (fseek(f, 0, SEEK_END) == 0 && ftell(f) == 0) {
      if (fputs(csvrow::kHeader, f) < 0 || fputc('\n', f) == EOF) {
        fclose(f);
        f = nullptr;
        return false;
      }
    }
    return true;
  }
  bool append(const char *line) {
    return f != nullptr && fputs(line, f) >= 0 && fputc('\n', f) != EOF;
  }
  void close() {
    if (f != nullptr) {
      fflush(f);
      fclose(f);
      f = nullptr;
    }
  }
};

void flushQueue() {
  FileSink sink;
  const int written = g_queue.flushGrouped(sink);
  if (written > 0) {
    fprintf(stderr, "SD (Emulator): %d gepufferte Zeile(n) nachgeschrieben\n",
            written);
  }
  buildStatus();
}

// Full paths of the month files, oldest first. The names sort correctly
// because the month key is fixed-width.
std::vector<std::string> historyFiles() {
  std::vector<std::string> files;
  DIR *d = opendir(g_histDir.c_str());
  if (d == nullptr) {
    return files;
  }
  while (dirent *e = readdir(d)) {
    const char *name = e->d_name;
    const size_t len = strlen(name);
    if (len > 8 && strncmp(name, "HEMS-", 5) == 0 &&
        strcmp(name + len - 4, ".csv") == 0) {
      files.push_back(g_histDir + "/" + name);
    }
  }
  closedir(d);
  std::sort(files.begin(), files.end());
  return files;
}

}  // namespace

bool init(const std::string &dir) {
  g_enabled = false;
  g_mounted = false;
  g_dir = dir;
  g_histDir = dir + "/hist";

  if (dir.empty()) {
    buildStatus();
    return false;
  }
  if (!mkdirs(g_histDir)) {
    fprintf(stderr, "SD (Emulator): Verzeichnis nicht nutzbar: %s\n",
            g_histDir.c_str());
    buildStatus();
    return false;
  }

  g_queue.attach(g_slots, kQueueCap);
  g_enabled = true;
  g_mounted = true;
  buildStatus();
  fprintf(stderr, "SD (Emulator): schreibt nach %s\n", g_histDir.c_str());
  return true;
}

bool enabled() { return g_enabled; }

bool mounted() { return g_mounted; }

const char *dir() { return g_dir.c_str(); }

void logSample(const WarpData &d) {
  if (!g_enabled || !d.valid) {
    return;
  }
  const time_t now = time(nullptr);
  if (now < 1000000000) {
    return;
  }

  char path[512];
  buildPath(path, sizeof(path));
  csvrow::Row row;
  csvrow::fillRow(d, (uint32_t)now, row);
  char line[csvrow::kLineCap];
  if (csvrow::format(line, sizeof(line), row) == 0) {
    fprintf(stderr, "SD (Emulator): Zeile zu lang, verworfen\n");
    return;
  }

  if (!writeRow(path, line)) {
    g_queue.push(line, path);
    g_nextRetryMs = 0;
    fprintf(stderr, "SD (Emulator): Schreiben fehlgeschlagen, Zeile gepuffert\n");
  }
  buildStatus();
}

void tick() {
  if (!g_enabled || g_queue.empty()) {
    return;
  }
  const uint32_t now = static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
  if (g_nextRetryMs != 0 && static_cast<int32_t>(now - g_nextRetryMs) < 0) {
    return;
  }
  g_nextRetryMs = now + kRetryMs;
  flushQueue();
}

const char *statusText() { return g_status; }

int restore(csvrow::Sample *out, int maxRows) {
  if (!g_enabled || out == nullptr || maxRows <= 0) {
    return 0;
  }
  const std::vector<std::string> files = historyFiles();
  if (files.empty()) {
    return 0;
  }

  std::vector<csvrow::Sample> ring(static_cast<size_t>(maxRows));
  int total = 0;
  for (const std::string &path : files) {
    FILE *f = fopen(path.c_str(), "rb");
    if (f == nullptr) {
      continue;
    }
    char line[csvrow::kLineCap];
    while (fgets(line, sizeof(line), f) != nullptr) {
      csvrow::Row r;
      if (!csvrow::parse(line, r)) {
        continue;  // header, half-written row or a foreign format
      }
      csvrow::toSample(r, ring[static_cast<size_t>(total % maxRows)]);
      ++total;
    }
    fclose(f);
  }

  const int kept = total > maxRows ? maxRows : total;
  const int start = total - kept;
  for (int i = 0; i < kept; ++i) {
    out[i] = ring[static_cast<size_t>((start + i) % maxRows)];
  }
  return kept;
}

}  // namespace simsd
