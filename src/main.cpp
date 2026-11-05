// WARP Energy Manager V2 panel - firmware entry point.
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
#include <esp_system.h>
#include <lvgl.h>

#include <memory>

#include "display/Backlight.h"
#include "display/Display.h"
#include "display/Touch.h"
#include "ui/WarpUi.h"
#include "warp/WarpApi.h"
#include "warp/WarpData.h"
#include "warp/WarpParse.h"

namespace {

constexpr uint32_t POLL_MS = 10000;
constexpr uint32_t UI_MS = 1000;
constexpr uint32_t HTTP_TIMEOUT_MS = 4000;

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
char g_apSsid[32] = "WARP-Panel";
// The setup network is intentionally open. The QR code and the setup screen
// carry the SSID, so joining it needs no password on any device.
constexpr char g_apPassword[] = "";

WarpData g_data;
bool g_haveData = false;

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

uint32_t g_lastPoll = 0;
uint32_t g_lastUi = 0;
uint32_t g_lastTick = 0;

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
    warpui::setNetworkInfo("--", "--", g_url, "--");
    return;
  }

  const String ssid = WiFi.SSID();
  const String ip = WiFi.localIP().toString();
  char web[80];
  if (g_mdnsOk) {
    snprintf(web, sizeof(web), "http://warp-panel.local/");
  } else {
    snprintf(web, sizeof(web), "http://%s/", ip.c_str());
  }
  warpui::setNetworkInfo(ssid.c_str(), ip.c_str(), g_url, web);
}

void onParamsSaved() {
  applyUrlParam();
  updateNetworkInfo();
}

void setupMdns() {
  if (g_mdnsOk) {
    return;
  }
  g_mdnsOk = MDNS.begin("warp-panel");
  if (g_mdnsOk) {
    MDNS.addService("http", "tcp", 80);
    Serial.println(F("mDNS: http://warp-panel.local/"));
  } else {
    Serial.println(F("mDNS start failed, use the panel IP"));
  }
}

void generateApSsid() {
  const uint64_t chip = ESP.getEfuseMac();
  snprintf(g_apSsid, sizeof(g_apSsid), "WARP-Panel-%04X",
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
  Serial.printf("WARP module API: %s (%s)\n", error.c_str(),
                g_reader->base().c_str());

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
// the power manager, otherwise the per-charger override. The next loop pass
// polls immediately so the card reflects the new state without waiting 10 s.
void setChargeMode(int charger, int mode) {
  if (!g_reader) {
    g_reader.reset(new warpapi::Reader(g_transport, g_url));
  }
  std::string error;
  const bool ok =
      charger < 0 ? g_reader->setGlobalChargeMode(mode, error)
                  : g_reader->setChargerChargeMode(charger, mode, error);
  Serial.printf("Lademodus %s: Ladepunkt %d -> %d\n", ok ? "ok" : "fehlgeschlagen",
                charger, mode);
  if (!ok && !error.empty()) {
    Serial.printf("  %s\n", error.c_str());
  }
  g_lastPoll = millis() - POLL_MS;
}

void poll() {
  if (WiFi.status() != WL_CONNECTED) {
    warpui::setConnectionState(warpui::ConnectionState::NoNet);
    return;
  }

  WarpData fresh;
  const FetchResult result = fetchWarp(fresh);
  if (result != FetchResult::Ok) {
    warpui::setConnectionState(result == FetchResult::Disconnected
                                   ? warpui::ConnectionState::Disconnected
                                   : warpui::ConnectionState::NoData);
    return;
  }
  fresh.lastUpdateMs = lv_tick_get();
  g_data = fresh;
  g_haveData = true;
  warpui::update(g_data);
  warpui::setConnectionState(warpui::ConnectionState::Data);
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
  g_wm->setHostname("warp-panel");
  g_wm->setTitle("WARP-Panel Setup");
  g_wm->setDarkMode(true);
  g_wm->setAPCallback([](WiFiManager *mgr) {
    const String ssid = mgr->getConfigPortalSSID();
    Serial.printf("Setup AP: %s (offen)\n", ssid.c_str());
    warpui::showSetup(ssid.c_str(), g_apPassword, "http://192.168.4.1");
    lv_refr_now(nullptr);
  });
  g_wm->setSaveParamsCallback(onParamsSaved);

  warpui::setConnectionState(warpui::ConnectionState::NoNet);
  const bool connected = g_wm->autoConnect(g_apSsid, g_apPassword);
  if (!connected) {
    Serial.println(F("Wi-Fi configuration did not complete, restarting"));
    delay(500);
    ESP.restart();
  }

  applyUrlParam();
  warpui::hideSetup();
  setupMdns();
  updateNetworkInfo();
  Serial.printf("Wi-Fi: %s   endpoint: %s\n", WiFi.localIP().toString().c_str(),
                g_url);

  // Keep the same configuration page reachable on the local network. The AP
  // is only used for the first boot; afterwards this is a normal HTTP server
  // on the panel's address.
  g_wm->setConfigPortalTimeout(0);
  g_wm->setConfigPortalBlocking(false);
  g_wm->startWebPortal();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\nWARP Energy Manager panel"));

  if (!displayInit()) {
    Serial.println(F("FATAL: display init failed"));
    while (true) {
      delay(10);
    }
  }

  warpui::create(lv_screen_active());
  warpui::setConnectionState(warpui::ConnectionState::NoNet);
  warpui::setChargeModeHandler(setChargeMode);
  backlightSetWakeable(touchInit());
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchReadCb);

  g_lastTick = millis();
  displayLooper();

  setupNetwork();

  ArduinoOTA.setHostname("warp-panel");
  ArduinoOTA.begin();

  g_lastPoll = millis();
  poll();
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

  if (now - g_lastPoll >= POLL_MS) {
    g_lastPoll = now;
    poll();
  }

  if (now - g_lastUi >= UI_MS) {
    g_lastUi = now;
    updateNetworkInfo();
    if (g_haveData) {
      warpui::update(g_data);
    }
  }

  displayLooper();
  delay(5);
}
