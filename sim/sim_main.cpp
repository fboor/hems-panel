// WARP Energy Manager panel - desktop emulator.
//
// Same UI source as the firmware, SDL window instead of the RGB panel, POSIX
// socket instead of WiFiClient. The point is that a picture from here shows the
// layout the panel will show.
//
//   run.sh --url http://192.168.1.222
//   run.sh --shot out.png --after 3
//   run.sh --probe          # discover the module API, print mapped values
//
// SPDX-License-Identifier: MIT
#include <SDL2/SDL.h>
#include <lvgl.h>
#include <zlib.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "ui/WarpUi.h"
#include "warp/WarpApi.h"
#include "warp/WarpData.h"
#include "warp/WarpParse.h"

#include "WarpHttpPosix.h"

namespace {

class PosixTransport : public warpapi::Transport {
 public:
  PosixTransport(int timeoutMs, bool verbose)
      : timeoutMs_(timeoutMs), verbose_(verbose) {}

  bool get(const std::string &url, std::string &body,
           std::string &error) override {
    if (verbose_) {
      fprintf(stderr, "transport: GET %s\n", url.c_str());
      fflush(stderr);
    }
    const warphttp::Response r = warphttp::get(url, timeoutMs_);
    if (verbose_) {
      fprintf(stderr, "transport: done status=%d err=%s\n", r.status,
              r.error.c_str());
      fflush(stderr);
    }
    body = r.body;
    error = r.error;
    return r.ok();
  }

 private:
  int timeoutMs_;
  bool verbose_;
};

struct Options {
  std::string url = "http://localhost:8082";
  std::string shot;
    int intervalMs = 10000;
  int timeoutMs = 5000;
  int afterS = 2;
  int page = 1;
  bool probe = false;
  bool setup = false;
  bool verbose = false;
};

struct Shared {
  std::mutex mu;
  WarpData data;
  warpui::ConnectionState state = warpui::ConnectionState::NoNet;
  unsigned seq = 0;
};

Shared g_shared;
std::atomic<bool> g_stop{false};
std::atomic<bool> g_quit{false};

bool flatFallback(const Options &opt, WarpData &parsed) {
  if (opt.url.find("/json") == std::string::npos) {
    return false;
  }
  const warphttp::Response r = warphttp::get(opt.url, opt.timeoutMs);
  if (!r.ok()) {
    fprintf(stderr, "WARP flat: %s (%s)\n", r.error.c_str(), opt.url.c_str());
    return false;
  }
  return warpparse::parse(r.body.data(), r.body.size(), parsed);
}

void fetchLoop(const Options &opt) {
  if (opt.verbose) {
    fprintf(stderr, "fetchLoop: start\n");
    fflush(stderr);
  }
  PosixTransport transport(opt.timeoutMs, opt.verbose);
  warpapi::Reader reader(transport, opt.url);
  std::string lastError;

  for (;;) {
    WarpData parsed;
    std::string moduleError;
    bool ok = reader.poll(parsed, moduleError);
    if (!ok) {
      ok = flatFallback(opt, parsed);
      if (!ok && moduleError != lastError) {
        fprintf(stderr, "WARP: %s (%s)\n", moduleError.c_str(),
                reader.base().c_str());
        lastError = moduleError;
      }
    } else {
      lastError.clear();
    }

    {
      std::lock_guard<std::mutex> lock(g_shared.mu);
      if (ok) {
        parsed.lastUpdateMs = lv_tick_get();
        g_shared.data = parsed;
        g_shared.state = warpui::ConnectionState::Data;
      } else {
        g_shared.state = reader.slots().empty()
                             ? warpui::ConnectionState::Disconnected
                             : warpui::ConnectionState::NoData;
      }
      ++g_shared.seq;
    }

    for (int waited = 0; waited < opt.intervalMs && !g_stop; waited += 100) {
      SDL_Delay(100);
    }
    if (g_stop) {
      return;
    }
  }
}

uint32_t g_lastTick = 0;

void advanceTick(uint32_t now) {
  const uint32_t delta = now - g_lastTick;
  if (delta > 0) {
    lv_tick_inc(delta);
    g_lastTick = now;
  }
}

int SDLCALL quitWatch(void *, SDL_Event *ev) {
  if (ev->type == SDL_QUIT) {
    g_quit = true;
  }
  return 1;
}

bool writePng(const char *path, int width, int height) {
  std::string buf;
  buf.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 2u);

  lv_image_dsc_t dsc = {};
  const lv_result_t r = lv_snapshot_take_to_buf(
      lv_screen_active(), LV_COLOR_FORMAT_RGB565, &dsc, buf.data(),
      static_cast<uint32_t>(buf.size()));
  if (r != LV_RESULT_OK) {
    fprintf(stderr, "Snapshot fehlgeschlagen\n");
    return false;
  }

  const uint8_t *rgb565 = static_cast<const uint8_t *>(dsc.data);
  std::string raw;
  raw.reserve(static_cast<size_t>(height) *
              (1u + static_cast<size_t>(width) * 3u));
  for (int y = 0; y < height; ++y) {
    raw.push_back(0);
    for (int x = 0; x < width; ++x) {
      const size_t si = (static_cast<size_t>(y) * static_cast<size_t>(width) +
                         static_cast<size_t>(x)) *
                        2u;
      const uint16_t v =
          static_cast<uint16_t>(rgb565[si] | (static_cast<uint16_t>(rgb565[si + 1]) << 8));
      raw.push_back(static_cast<char>(((v >> 11) & 0x1F) * 255u / 31u));
      raw.push_back(static_cast<char>(((v >> 5) & 0x3F) * 255u / 63u));
      raw.push_back(static_cast<char>((v & 0x1F) * 255u / 31u));
    }
  }
  lv_snapshot_free(&dsc);

  uLongf zsize = compressBound(static_cast<uLong>(raw.size()));
  std::string z;
  z.resize(zsize);
  if (compress2(reinterpret_cast<Bytef *>(&z[0]), &zsize,
                reinterpret_cast<const Bytef *>(raw.data()),
                static_cast<uLong>(raw.size()), 6) != Z_OK) {
    fprintf(stderr, "PNG-Kompression fehlgeschlagen\n");
    return false;
  }
  z.resize(zsize);

  FILE *fp = fopen(path, "wb");
  if (fp == nullptr) {
    fprintf(stderr, "Bild nicht schreibbar: %s\n", path);
    return false;
  }

  auto put32 = [](uint32_t v, std::string &out) {
    out.push_back(static_cast<char>(v >> 24));
    out.push_back(static_cast<char>(v >> 16));
    out.push_back(static_cast<char>(v >> 8));
    out.push_back(static_cast<char>(v));
  };
  auto chunk = [&](const char *type, const void *data, size_t n) {
    std::string body(type);
    body.append(static_cast<const char *>(data), n);
    std::string c;
    put32(static_cast<uint32_t>(n), c);
    c += body;
    put32(static_cast<uint32_t>(
              ::crc32(0, reinterpret_cast<const Bytef *>(body.c_str()),
                      static_cast<uInt>(body.size())) &
              0xFFFFFFFFu),
          c);
    fwrite(c.data(), 1, c.size(), fp);
  };

  static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  fwrite(sig, 1, sizeof(sig), fp);
  {
    std::string ihdr;
    put32(static_cast<uint32_t>(width), ihdr);
    put32(static_cast<uint32_t>(height), ihdr);
    const unsigned char rest[5] = {8, 2, 0, 0, 0};
    ihdr.append(reinterpret_cast<const char *>(rest), sizeof(rest));
    chunk("IHDR", ihdr.data(), ihdr.size());
  }
  chunk("IDAT", z.data(), z.size());
  chunk("IEND", "", 0);
  fclose(fp);
  return true;
}

void printData(const WarpData &d) {
  printf(
      "device: %s  firmware: %s\n"
      "grid:   %.1f / %.1f / %.1f W  total %.1f W  import %.1f kWh  "
      "export %.1f kWh  voltage %.0f/%.0f/%.0f V\n"
      "house:  %.1f / %.1f / %.1f W  total %.1f W  energy %.1f kWh\n"
      "pv:     %.1f + %.1f W  total %.1f W  energy %.1f kWh\n"
      "battery: %.1f %%  %.1f W  %.1f V  %.1f A  charge %.1f kWh  "
      "discharge %.1f kWh\n"
      "external: %.1f W  island: %s  autarky: %.0f %%  forecast: %.1f kWh\n",
      d.deviceName, d.firmware, d.gridL1, d.gridL2, d.gridL3, d.gridPower,
      d.gridImportWh / 1000.0f, d.gridExportWh / 1000.0f, d.gridVoltageL1,
      d.gridVoltageL2, d.gridVoltageL3, d.houseL1, d.houseL2, d.houseL3,
      d.housePower, d.houseEnergyWh / 1000.0f, d.pvA, d.pvB, d.pvPower,
      d.pvEnergyWh / 1000.0f, d.soc, d.batteryPower, d.batteryVoltage,
      d.batteryCurrent, d.batteryChargeWh / 1000.0f,
      d.batteryDischargeWh / 1000.0f, d.externalPower,
      d.island ? "yes" : "no", d.autarky, d.forecastTodayWh / 1000.0f);
  printf("wallbox: %.1f W  %.1f A  %d ph  state %d  power %s%s\n",
         d.charge_power, d.charge_current / 1000.0, d.charge_phases,
         d.charge_state, d.chargeMeter ? "meter" : "calculated",
         d.chargeEstimated ? " (estimated)" : "");
}

void usage() {
  printf(
      "warp-panel Emulator\n"
      "  --url URL        WARP base URL or flat /json endpoint\n"
      "                   (default http://localhost:8082)\n"
      "  --interval MS    poll interval (default 10000)\n"
      "  --timeout MS     HTTP timeout (default 5000)\n"
      "  --shot FILE      render one frame to PNG, then exit\n"
      "  --after S        seconds to wait before the shot (default 2)\n"
      "  --page N         page to show, 1..4 (default 1)\n"
      "  --setup          show the first-boot Wi-Fi setup screen\n"
      "  --probe          read once, print the mapped values, exit\n"
      "  -v, --verbose    log every HTTP request\n"
      "  --help           this text\n");
}

}  // namespace

int main(int argc, char **argv) {
  Options opt;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto value = [&](const char *fallback) -> std::string {
      return (i + 1 < argc) ? argv[++i] : fallback;
    };
    if (a == "--url") {
      opt.url = value(opt.url.c_str());
    } else if (a == "--shot") {
      opt.shot = value("");
    } else if (a == "--interval") {
      opt.intervalMs = atoi(value("10000").c_str());
    } else if (a == "--timeout") {
      opt.timeoutMs = atoi(value("5000").c_str());
    } else if (a == "--after") {
      opt.afterS = atoi(value("2").c_str());
    } else if (a == "--page") {
      opt.page = atoi(value("1").c_str());
    } else if (a == "--setup") {
      opt.setup = true;
    } else if (a == "--probe") {
      opt.probe = true;
    } else if (a == "--verbose" || a == "-v") {
      opt.verbose = true;
    } else if (a == "--help" || a == "-h") {
      usage();
      return 0;
    } else {
      fprintf(stderr, "unbekanntes Argument: %s\n", a.c_str());
      usage();
      return 2;
    }
  }

  if (opt.probe) {
    PosixTransport transport(opt.timeoutMs, opt.verbose);
    warpapi::Reader reader(transport, opt.url);
    printf("url:  %s\nbase: %s\n", opt.url.c_str(), reader.base().c_str());

    WarpData d;
    std::string error;
    const bool ok = reader.poll(d, error);
    if (ok) {
      printf("module API ok, %zu meter slot(s)\n", reader.slots().size());
      for (const warpapi::Slot &s : reader.slots()) {
        printf("  slot %d  location %d  role %d  %s\n", s.slot, s.location,
               static_cast<int>(s.role), s.name);
      }
      printf("\n");
      printData(d);
      return 0;
    }

    printf("module API: %s\n", error.c_str());
    if (opt.url.find("/json") == std::string::npos) {
      printf("(kein /json-Fallback fuer diese URL)\n");
      return 1;
    }
    const warphttp::Response r = warphttp::get(opt.url, opt.timeoutMs);
    printf("flat URL: status %d, %zu byte, error %s\n\n", r.status,
           r.body.size(), r.error.empty() ? "-" : r.error.c_str());
    if (r.ok()) {
      fwrite(r.body.data(), 1, r.body.size(), stdout);
      printf("\n");
      WarpData flat;
      if (warpparse::parse(r.body.data(), r.body.size(), flat)) {
        printf("\nflat parser:\n");
        printData(flat);
        return 0;
      }
    }
    return 1;
  }

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }

  const int width = 800;
  const int height = 480;

  lv_init();
  lv_display_t *disp = lv_sdl_window_create(width, height);
  if (disp == nullptr) {
    fprintf(stderr, "lv_sdl_window_create: kein Fenster\n");
    SDL_Quit();
    return 1;
  }
  lv_sdl_window_set_title(disp, "WARP Energy Manager (Emulator)");
  lv_sdl_mouse_create();
  // SDL_AddEventWatch(quitWatch, nullptr);

  warpui::create(lv_screen_active());
  warpui::setPage(opt.page);
  warpui::setConnectionState(warpui::ConnectionState::NoNet);
  warpui::setNetworkInfo("WLAN-Demo", "192.168.1.50", "http://192.168.1.222",
                         "http://warp-panel.local/");
  if (opt.setup) {
    warpui::showSetup("WARP-Panel-A1B2", "K7M4P-QX2R9",
                      "http://192.168.4.1");
  }

  g_lastTick = SDL_GetTicks();
  std::thread worker(fetchLoop, opt);

  const uint32_t startMs = SDL_GetTicks();
  unsigned seenSeq = 0;
  uint32_t lastUiMs = 0;
  bool shotDone = false;

  while (!g_quit) {
    const uint32_t now = SDL_GetTicks();
    advanceTick(now);

    WarpData data;
    warpui::ConnectionState state = warpui::ConnectionState::NoNet;
    unsigned seq = 0;
    {
      std::lock_guard<std::mutex> lock(g_shared.mu);
      data = g_shared.data;
      state = g_shared.state;
      seq = g_shared.seq;
    }

    warpui::setConnectionState(state);

    /* Update data on every pass through the main loop */
    if (seq != seenSeq) {
      warpui::update(data);
      seenSeq = seq;
      lastUiMs = now;
    }

    uint32_t wait = lv_timer_handler();
    if (wait > 5) {
      wait = 5;
    }
    SDL_Delay(wait);

    if (!opt.shot.empty() && !shotDone) {
      const uint32_t elapsed = SDL_GetTicks() - startMs;
      if (elapsed >= static_cast<uint32_t>(opt.afterS) * 1000u) {
        for (int i = 0; i < 20; ++i) {
          advanceTick(SDL_GetTicks());
          lv_timer_handler();
          SDL_Delay(5);
        }
        if (!writePng(opt.shot.c_str(), width, height)) {
          g_stop = true;
          worker.join();
          SDL_Quit();
          return 1;
        }
        printf("Bild: %s (%d x %d)\n", opt.shot.c_str(), width, height);
        shotDone = true;
        g_quit = true;
      }
    }
  }

  g_stop = true;
  worker.join();
  SDL_Quit();
  return 0;
}
