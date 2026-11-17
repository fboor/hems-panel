// Home Energy Manager panel - firmware entry point.
//
// Boot order: display, UI, Wi-Fi configuration portal, then the 10 s poll of
// the device's JSON endpoint. The same UI and parser run in the desktop
// emulator (sim/), so a page that works there is the page this draws.
//
// SPDX-License-Identifier: MIT
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <lvgl.h>
#include <sys/time.h>
#include <time.h>

#include <memory>
#include <atomic>

#include "display/Backlight.h"
#include "display/Display.h"
#include "display/Touch.h"
#include "storage/SdLog.h"
#include "ui/HemsUi.h"
#include "warp/WarpApi.h"
#include "warp/WarpData.h"
#include "warp/WarpParse.h"

namespace {

constexpr uint32_t POLL_MS = 10000;
constexpr uint32_t UI_MS = 1000;
constexpr uint32_t HTTP_TIMEOUT_MS = 4000;

// Diagnostic heartbeat: the display's refresh, flush and heap numbers every
// five seconds, for tuning. Off by default, because the block is about 450
// bytes and the UART ring holds 256, so the print blocks the UI task for a
// dozen milliseconds and shows up as a hitch in the flow animation. The
// counters it reads keep running either way - the beam margin is computed
// from the same draw times.
#ifdef HEMS_DIAG
constexpr uint32_t HEARTBEAT_MS = 5000;
#endif

// The WARP module API lives at the bare host. A URL ending in /json is also
// accepted and normalized by warpapi::Reader, so an older configuration keeps
// working.
constexpr char DEFAULT_URL[] = "http://192.168.1.222";

char g_url[96] = "http://192.168.1.222";

// The setup portal is used twice: blocking on first boot (open AP + captive
// portal, no timeout), and non-blocking afterwards on the local network so the
// WARP address can be changed without wiping the Wi-Fi credentials.
WiFiManager *g_wm = nullptr;
WiFiManagerParameter *g_urlParam = nullptr;
bool g_mdnsOk = false;
char g_apSsid[32] = "HEMS-Panel";
// The setup network is intentionally open. The QR code and the setup screen
// carry the SSID, so joining it needs no password on any device.
constexpr char g_apPassword[] = "";

WarpData g_data;
bool g_haveData = false;

// Polling runs on core 0 so its blocking HTTP requests never freeze the
// LVGL animation on core 1. The task writes g_pollData under g_dataMutex;
// the UI thread copies it out and is the only caller of hemsui::*.
WarpData g_pollData;
SemaphoreHandle_t g_dataMutex = nullptr;
std::atomic<bool> g_dataReady{false};
std::atomic<int> g_nextConnState{-1};  // hemsui::ConnectionState, -1 = none
std::atomic<bool> g_pollNow{false};
std::atomic<bool> g_cmdPending{false};
std::atomic<int> g_cmdCharger{-1};
std::atomic<int> g_cmdMode{0};
std::atomic<bool> g_heatCmdPending{false};
std::atomic<bool> g_heatCmdOn{true};

class HttpTransport : public warpapi::Transport {
 public:
  bool get(const std::string &url, std::string &body,
           std::string &error) override {
    HTTPClient http;
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setReuse(false);
    if (!http.begin(url.c_str())) {
      error = "begin";
      return false;
    }
    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
      error = "HTTP " + std::to_string(code);
      http.end();
      return false;
    }
    const String payload = http.getString();
    body.assign(payload.c_str(), payload.length());
    http.end();
    return true;
  }

  bool post(const std::string &url, const std::string &body,
            std::string &response, std::string &error) override {
    HTTPClient http;
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setReuse(false);
    if (!http.begin(url.c_str())) {
      error = "begin";
      return false;
    }
    http.addHeader("Content-Type", "application/json");
    const int code = http.POST(String(body.c_str()));
    if (code != HTTP_CODE_OK) {
      error = "HTTP " + std::to_string(code);
      http.end();
      return false;
    }
    const String payload = http.getString();
    response.assign(payload.c_str(), payload.length());
    http.end();
    return true;
  }
};

HttpTransport g_transport;
std::unique_ptr<warpapi::Reader> g_reader;

uint32_t g_lastUi = 0;
uint32_t g_lastTick = 0;
#ifdef HEMS_DIAG
uint32_t g_lastHeartbeat = 0;
#endif

// SD history logging. The UI ring holds 24 h at one sample per minute; the
// restore buffers are allocated in PSRAM in setup().
constexpr int kHistoryRows = 24 * 60 + 2;
SdHistSample *g_histSamples = nullptr;
hemsui::HistoryPoint *g_histPoints = nullptr;
uint32_t g_lastLogEpoch = 0;
bool g_historyRequested = false;
bool g_historySeeded = false;
uint32_t g_historyRequestMs = 0;

// The WARP reports its RTC time as unix minutes. The panel has no other wall
// clock, so the first valid reading becomes the system clock - the CSV rows and
// the monthly file names need it.
void syncClockFromWarp(const WarpData &d) {
  if (d.nowMin == 0 || time(nullptr) >= 1000000000) {
    return;
  }
  struct timeval tv {};
  tv.tv_sec = (time_t)d.nowMin * 60;
  tv.tv_usec = 0;
  if (settimeofday(&tv, nullptr) == 0) {
    Serial.printf("Uhrzeit von WARP uebernommen: %lu min\n",
                  (unsigned long)d.nowMin);
  }
}

// One CSV row per minute. Only called when a fresh dataset arrived, so a
// disconnected WARP leaves a gap in the log instead of repeating stale values.
void logSampleIfDue(const WarpData &d) {
  const time_t now = time(nullptr);
  if (now < 1000000000 || !d.valid) {
    return;
  }
  if (g_lastLogEpoch != 0 &&
      (uint32_t)now - g_lastLogEpoch < 60u) {
    return;
  }
  g_lastLogEpoch = (uint32_t)now;
  sdLogSample(d);
}

// Seed the 24 h chart once from the newest CSV rows. The worker scans the card
// asynchronously; this only asks and collects.
void restoreHistoryIfReady() {
  if (g_historySeeded || g_histSamples == nullptr || g_histPoints == nullptr) {
    return;
  }
  const time_t now = time(nullptr);
  if (now < 1000000000 || !sdMounted()) {
    return;
  }

  if (!g_historyRequested) {
    sdRequestHistory(kHistoryRows, true);
    g_historyRequested = true;
    g_historyRequestMs = millis();
    return;
  }

  const int n = sdTakeHistory(g_histSamples, kHistoryRows);
  if (n == -2) {
    // Still running, or the request never reached the worker (queue full,
    // remount). Ask again after a while instead of waiting forever.
    if ((uint32_t)(millis() - g_historyRequestMs) > 15000u) {
      g_historyRequested = false;
    }
    return;
  }
  if (n == -1) {
    g_historyRequested = false;  // clock was not valid yet; retry
    return;
  }

  for (int i = 0; i < n; ++i) {
    const SdHistSample &s = g_histSamples[i];
    g_histPoints[i] = {s.ts, s.grid, s.pv, s.house, s.battery,
                       s.wb1, s.wb2, s.soc, s.heat};
  }
  hemsui::seedHistory(g_histPoints, n, (uint32_t)now);
  g_historySeeded = true;
  Serial.printf("SD: %d Verlaufszeilen geladen\n", n);
}

void loadUrl() {
  Preferences prefs;
  prefs.begin("warp", true);
  const String saved = prefs.getString("url", DEFAULT_URL);
  prefs.end();
  snprintf(g_url, sizeof(g_url), "%s", saved.c_str());
}

void saveUrl() {
  Preferences prefs;
  prefs.begin("warp", false);
  prefs.putString("url", g_url);
  prefs.end();
}

// The form accepts a bare IP, a hostname or a full URL. Store one canonical
// form so the reader and the service page always see the same string.
void normalizeUrl(const char *input, char *out, size_t n) {
  String s(input != nullptr ? input : "");
  s.trim();
  if (s.length() == 0 || s == "http://" || s == "https://") {
    s = DEFAULT_URL;
  }
  if (!s.startsWith("http://") && !s.startsWith("https://")) {
    s = "http://" + s;
  }
  while (s.endsWith("/")) {
    s.remove(s.length() - 1);
  }
  snprintf(out, n, "%s", s.c_str());
}

void applyUrlParam() {
  if (g_urlParam == nullptr) {
    return;
  }
  const char *value = g_urlParam->getValue();
  char normalized[sizeof(g_url)];
  normalizeUrl(value, normalized, sizeof(normalized));
  if (strncmp(normalized, g_url, sizeof(g_url)) != 0) {
    snprintf(g_url, sizeof(g_url), "%s", normalized);
    saveUrl();
    g_reader.reset();
    g_urlParam->setValue(g_url, sizeof(g_url));
    Serial.printf("WARP endpoint: %s\n", g_url);
  }
}

void updateNetworkInfo() {
  if (WiFi.status() != WL_CONNECTED) {
    hemsui::setNetworkInfo("--", "--", g_url, "--");
    return;
  }

  const String ssid = WiFi.SSID();
  const String ip = WiFi.localIP().toString();
  char web[80];
  if (g_mdnsOk) {
    snprintf(web, sizeof(web), "http://hems-panel.local/");
  } else {
    snprintf(web, sizeof(web), "http://%s/", ip.c_str());
  }
  hemsui::setNetworkInfo(ssid.c_str(), ip.c_str(), g_url, web);
}

void onParamsSaved() {
  applyUrlParam();
  updateNetworkInfo();
}

void setupMdns() {
  if (g_mdnsOk) {
    return;
  }
  g_mdnsOk = MDNS.begin("hems-panel");
  if (g_mdnsOk) {
    MDNS.addService("http", "tcp", 80);
    Serial.println(F("mDNS: http://hems-panel.local/"));
  } else {
    Serial.println(F("mDNS start failed, use the panel IP"));
  }
}

void generateApSsid() {
  const uint64_t chip = ESP.getEfuseMac();
  snprintf(g_apSsid, sizeof(g_apSsid), "HEMS-Panel-%04X",
           static_cast<unsigned>(chip & 0xFFFFu));
}

enum class FetchResult { Ok, Disconnected, NoData };

FetchResult fetchWarp(WarpData &out) {
  if (!g_reader) {
    g_reader.reset(new warpapi::Reader(g_transport, g_url));
  }

  std::string error;
  if (g_reader->poll(out, error)) {
    return FetchResult::Ok;
  }
  Serial.printf("WARP module API: %s (%s) heap %u\n", error.c_str(),
                g_reader->base().c_str(), ESP.getFreeHeap());

  // A flat JSON bridge is still accepted for development, but the real device
  // uses the module API above.
  if (strstr(g_url, "/json") == nullptr) {
    // A discovered device that simply returned no measurements is reachable;
    // no discovered modules means the WARP itself is not answering.
    return g_reader->slots().empty() ? FetchResult::Disconnected
                                     : FetchResult::NoData;
  }
  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);
  if (!http.begin(g_url)) {
    Serial.printf("HTTP begin failed: %s\n", g_url);
    return FetchResult::Disconnected;
  }
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("HTTP %d from %s\n", code, g_url);
    http.end();
    return FetchResult::Disconnected;
  }
  const String body = http.getString();
  http.end();
  return warpparse::parse(body.c_str(), body.length(), out)
             ? FetchResult::Ok
             : FetchResult::NoData;
}

// Applies a mode from the wallbox page. `charger` -1 means all chargers via
// the power manager, otherwise the per-charger override. The command runs on
// the poll task (core 0) together with the next poll, so the card reflects
// the new state without blocking the UI thread.
void setChargeMode(int charger, int mode) {
  g_cmdCharger = charger;
  g_cmdMode = mode;
  g_cmdPending = true;
  g_pollNow = true;
}

void applyChargeMode() {
  if (!g_reader) {
    g_reader.reset(new warpapi::Reader(g_transport, g_url));
  }
  const int charger = g_cmdCharger;
  const int mode = g_cmdMode;
  std::string error;
  const bool ok =
      charger < 0 ? g_reader->setGlobalChargeMode(mode, error)
                  : g_reader->setChargerChargeMode(charger, mode, error);
  Serial.printf("Lademodus %s: Ladepunkt %d -> %d\n", ok ? "ok" : "fehlgeschlagen",
                charger, mode);
  if (!ok && !error.empty()) {
    Serial.printf("  %s\n", error.c_str());
  }
}

void setHeatingOverride(bool on) {
  g_heatCmdOn = on;
  g_heatCmdPending = true;
  g_pollNow = true;
}

void applyHeatingOverride() {
  if (!g_reader) {
    g_reader.reset(new warpapi::Reader(g_transport, g_url));
  }
  const bool on = g_heatCmdOn;
  std::string error;
  const bool ok = g_reader->setSgrOverride(on, error);
  Serial.printf("Heizung Override %s: %s\n", on ? "an" : "aus",
                ok ? "ok" : "fehlgeschlagen");
  if (!ok && !error.empty()) {
    Serial.printf("  %s\n", error.c_str());
  }
}

void pollTask(void *) {
  for (;;) {
    if (g_cmdPending.exchange(false)) {
      applyChargeMode();
      g_pollNow = true;
    }
    if (g_heatCmdPending.exchange(false)) {
      applyHeatingOverride();
      g_pollNow = true;
    }
    if (WiFi.status() != WL_CONNECTED) {
      g_nextConnState = static_cast<int>(hemsui::ConnectionState::NoNet);
    } else {
      WarpData fresh;
      const FetchResult result = fetchWarp(fresh);
      if (result == FetchResult::Ok) {
        fresh.lastUpdateMs = lv_tick_get();
        if (xSemaphoreTake(g_dataMutex, portMAX_DELAY) == pdTRUE) {
          g_pollData = fresh;
          xSemaphoreGive(g_dataMutex);
        }
        g_dataReady = true;
        g_nextConnState = static_cast<int>(hemsui::ConnectionState::Data);
      } else {
        g_nextConnState = static_cast<int>(
            result == FetchResult::Disconnected
                ? hemsui::ConnectionState::Disconnected
                : hemsui::ConnectionState::NoData);
      }
    }
    // Sleep in 100 ms slices so a charge-mode tap or a Wi-Fi change is
    // picked up within a second, not only at the next poll.
    for (uint32_t waited = 0; waited < POLL_MS && !g_pollNow; waited += 100) {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    g_pollNow = false;
  }
}

// Wi-Fi-Ereignisse, damit im Log steht, ob die Verbindung laeuft oder
// abgebaut wird - "kein WARP-Modul" bedeutet WL_CONNECTED und eine fallende
// HTTP-Anfrage, was beides getrennt nachvollziehbar sein muss.
void onWifiEvent(arduino_event_t *ev) {
  if (ev == nullptr) {
    return;
  }
  if (ev->event_id == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    Serial.printf("Wi-Fi: IP %s, heap %u\n",
                  WiFi.localIP().toString().c_str(), ESP.getFreeHeap());
  } else if (ev->event_id == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
    Serial.println(F("Wi-Fi: verbunden"));
  } else if (ev->event_id == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    Serial.printf("Wi-Fi: getrennt, Grund %u (%s), heap %u\n",
                  ev->event_info.wifi_sta_disconnected.reason,
                  WiFi.disconnectReasonName(
                      (wifi_err_reason_t)ev->event_info.wifi_sta_disconnected.reason),
                  ESP.getFreeHeap());
  }
}

void setupNetwork() {
  loadUrl();
  char normalized[sizeof(g_url)];
  normalizeUrl(g_url, normalized, sizeof(normalized));
  if (strncmp(normalized, g_url, sizeof(g_url)) != 0) {
    snprintf(g_url, sizeof(g_url), "%s", normalized);
    saveUrl();
  }

  generateApSsid();

  g_wm = new WiFiManager();
  g_urlParam = new WiFiManagerParameter(
      "warp_url", "WARP Energy Manager (IP oder Hostname)", g_url,
      sizeof(g_url));
  g_wm->addParameter(g_urlParam);

  g_wm->setDebugOutput(false);
  // No timeout on the first-boot portal: the setup screen with the QR code
  // stays visible until the user has configured a WLAN and the panel joined it.
  g_wm->setConfigPortalTimeout(0);
  g_wm->setConnectTimeout(20);
  g_wm->setHostname("hems-panel");
  g_wm->setTitle("HEMS-Panel Setup");
  g_wm->setDarkMode(true);
  g_wm->setAPCallback([](WiFiManager *mgr) {
    const String ssid = mgr->getConfigPortalSSID();
    Serial.printf("Setup AP: %s (offen)\n", ssid.c_str());
    hemsui::showSetup(ssid.c_str(), g_apPassword, "http://192.168.4.1");
    lv_refr_now(nullptr);
  });
  g_wm->setSaveParamsCallback(onParamsSaved);

  hemsui::setConnectionState(hemsui::ConnectionState::NoNet);
  WiFi.onEvent(onWifiEvent, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(onWifiEvent, ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(onWifiEvent, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  // No modem sleep: on the Waveshare board's weak onboard antenna, modem
  // sleep makes beacons/packets get missed and TCP sessions drop.
  WiFi.setSleep(false);
  const bool connected = g_wm->autoConnect(g_apSsid, g_apPassword);
  if (!connected) {
    Serial.println(F("Wi-Fi configuration did not complete, restarting"));
    delay(500);
    ESP.restart();
  }

  applyUrlParam();
  hemsui::hideSetup();
  setupMdns();
  updateNetworkInfo();
  Serial.printf("Wi-Fi: %s   RSSI: %d dBm   heap: %u   PSRAM: %u   endpoint: %s\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI(),
                ESP.getFreeHeap(), ESP.getPsramSize(), g_url);

  // Keep the same configuration page reachable on the local network. The AP
  // is only used for the first boot; afterwards this is a normal HTTP server
  // on the panel's address.
  g_wm->setConfigPortalTimeout(0);
  g_wm->setConfigPortalBlocking(false);
  g_wm->startWebPortal();
}

#ifdef HEMS_DIAG
// The panel's own numbers, once per heartbeat: the scan rate, how often LVGL
// refreshed, and the three parts a refresh is made of. Printed instead of read
// in a debugger because the question is always "better than the build before",
// and that needs a number from both - and because two of the three parts are
// invisible from outside: the beam wait happens before the driver is called and
// the renderer's share is hidden inside the refresh pass that also contains the
// flushes.
void printHeartbeat() {
  DisplayStats st = {};
  displayStats(&st);
  const float secs = HEARTBEAT_MS / 1000.0f;
  const float refrPerS = (float)st.refreshes / secs;
  const float stripsPerS = (float)st.flushes / secs;
  const float refrAvg =
      st.refreshes ? (float)st.refreshTotalUs / (float)st.refreshes / 1000.0f : 0.0f;
  const float drawAvg =
      st.flushes ? (float)st.drawTotalUs / (float)st.flushes / 1000.0f : 0.0f;
  const float waitAvg =
      st.flushes ? (float)st.waitTotalUs / (float)st.flushes / 1000.0f : 0.0f;
  // The renderer's own share: the refresh pass contains the flushes, so what is
  // left after taking them out is drawing.
  const uint32_t renderUs =
      st.refreshTotalUs > st.waitTotalUs + st.drawTotalUs
          ? st.refreshTotalUs - st.waitTotalUs - st.drawTotalUs
          : 0;
  const float renderAvg =
      st.refreshes ? (float)renderUs / (float)st.refreshes / 1000.0f : 0.0f;
  Serial.printf(
      "[hb] %4.1f fps | refr %4.1f/s %5.1f ms avg %5.1f max | strips %4.1f/s "
      "draw %5.2f avg %5.1f max | wait %5.2f avg %5.1f max | render %5.1f "
      "avg | heap %6u psram %7u\n",
      displayFrameRate(), refrPerS, refrAvg, (float)st.refreshMaxUs / 1000.0f,
      stripsPerS, drawAvg, (float)st.drawMaxUs / 1000.0f, waitAvg,
      (float)st.waitMaxUs / 1000.0f, renderAvg, ESP.getFreeHeap(),
      ESP.getFreePsram());
  Serial.printf(
      "[hb] draw ms: <0.5 %u  <2 %u  <5 %u  >=5 %u | px/refr %u | slowest "
      "refr %u px in %u strips start %u gap %u@%u tail %u ms frames %u\n",
      (unsigned)st.drawBucket[0], (unsigned)st.drawBucket[1],
      (unsigned)st.drawBucket[2], (unsigned)st.drawBucket[3],
      (unsigned)(st.refreshes ? st.pxTotal / st.refreshes : 0),
      (unsigned)st.refreshMaxPx, (unsigned)st.refreshMaxStrips,
      (unsigned)(st.refreshMaxStartUs / 1000),
      (unsigned)(st.refreshMaxGapUs / 1000),
      (unsigned)st.refreshMaxGapY,
      (unsigned)(st.refreshMaxTailUs / 1000),
      (unsigned)st.refreshMaxFrames);
  Serial.printf(
      "[hb] refr ms: <10 %u  <30 %u  <100 %u  >=100 %u | heap largest %u\n",
      (unsigned)st.refrBucket[0], (unsigned)st.refrBucket[1],
      (unsigned)st.refrBucket[2], (unsigned)st.refrBucket[3],
      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}
#endif  // HEMS_DIAG

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\nHome Energy Manager panel"));
  Serial.printf("Reset-Grund: %d\n", (int)esp_reset_reason());

  if (!displayInit()) {
    Serial.println(F("FATAL: display init failed"));
    while (true) {
      delay(10);
    }
  }

  hemsui::create(lv_screen_active());
  hemsui::setConnectionState(hemsui::ConnectionState::NoNet);
  hemsui::setChargeModeHandler(setChargeMode);
  hemsui::setHeatingOverrideHandler(setHeatingOverride);
  backlightSetWakeable(touchInit());
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchReadCb);

  g_lastTick = millis();
  displayLooper();

  sdInit();
  g_histSamples = (SdHistSample *)heap_caps_malloc(
      sizeof(SdHistSample) * kHistoryRows, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  g_histPoints = (hemsui::HistoryPoint *)heap_caps_malloc(
      sizeof(hemsui::HistoryPoint) * kHistoryRows,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (g_histSamples == nullptr || g_histPoints == nullptr) {
    Serial.println(F("SD: kein PSRAM fuer den 24-h-Verlauf, Restore aus"));
  }

  setupNetwork();

  ArduinoOTA.setHostname("hems-panel");
  ArduinoOTA.begin();

  g_dataMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(pollTask, "poll", 16384, nullptr, 2, nullptr, 0);
}

void loop() {
  const uint32_t now = millis();
  const uint32_t delta = now - g_lastTick;
  if (delta > 0) {
    lv_tick_inc(delta);
    g_lastTick = now;
  }

  ArduinoOTA.handle();

  if (g_wm != nullptr && g_wm->process()) {
    setupMdns();
    updateNetworkInfo();
  }

  if (g_dataReady.exchange(false)) {
    if (xSemaphoreTake(g_dataMutex, portMAX_DELAY) == pdTRUE) {
      g_data = g_pollData;
      xSemaphoreGive(g_dataMutex);
    }
    g_haveData = true;
    syncClockFromWarp(g_data);
    logSampleIfDue(g_data);
    hemsui::update(g_data);
  }
  const int connState = g_nextConnState.exchange(-1);
  if (connState >= 0) {
    hemsui::setConnectionState(
        static_cast<hemsui::ConnectionState>(connState));
  }

  if (now - g_lastUi >= UI_MS) {
    g_lastUi = now;
    updateNetworkInfo();
    hemsui::setSdStatus(sdStatusText());
    restoreHistoryIfReady();
    if (g_haveData) {
      hemsui::update(g_data);
    }
  }

  displayLooper();
#ifdef HEMS_DIAG
  if (now - g_lastHeartbeat >= HEARTBEAT_MS) {
    g_lastHeartbeat = now;
    printHeartbeat();
  }
#endif
  delay(5);
}
