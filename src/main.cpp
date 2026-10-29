// WARP Energy Manager V2 panel - firmware entry point.
//
// Boot order: display, UI, Wi-Fi configuration portal, then the 10 s poll of
// the device's JSON endpoint. The same UI and parser run in the desktop
// emulator (sim/), so a page that works there is the page this draws.
//
// SPDX-License-Identifier: MIT
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiManager.h>
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

bool fetchWarp(WarpData &out) {
  if (!g_reader) {
    g_reader.reset(new warpapi::Reader(g_transport, g_url));
  }

  std::string error;
  if (g_reader->poll(out, error)) {
    return true;
  }
  Serial.printf("WARP module API: %s (%s)\n", error.c_str(),
                g_reader->base().c_str());

  // A flat JSON bridge is still accepted for development, but the real device
  // uses the module API above.
  if (strstr(g_url, "/json") == nullptr) {
    return false;
  }
  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);
  if (!http.begin(g_url)) {
    Serial.printf("HTTP begin failed: %s\n", g_url);
    return false;
  }
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("HTTP %d from %s\n", code, g_url);
    http.end();
    return false;
  }
  const String body = http.getString();
  http.end();
  return warpparse::parse(body.c_str(), body.length(), out);
}

void poll() {
  warpui::setStatus("Lade ...", true);
  WarpData fresh;
  if (!fetchWarp(fresh)) {
    warpui::setStatus("Keine Daten", false);
    return;
  }
  fresh.lastUpdateMs = lv_tick_get();
  g_data = fresh;
  g_haveData = true;
  warpui::update(g_data);
  warpui::setStatus("Verbunden", true);

  Serial.printf(
      "grid %.0f/%.0f/%.0f (%.0f) W  pv %.0f W  ext %.0f W  house %.0f W  "
      "battery %.0f%% %.0f W %.1f V %.1f A  island %d\n",
      g_data.gridL1, g_data.gridL2, g_data.gridL3, g_data.gridPower,
      g_data.pvPower, g_data.externalPower, g_data.housePower, g_data.soc,
      g_data.batteryPower, g_data.batteryVoltage, g_data.batteryCurrent,
      g_data.island ? 1 : 0);
}

void connectWifi() {
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  wm.setHostname("warp-panel");

  // The URL field is prefilled from NVS, so a second boot only needs the
  // portal when the device address changes.
  WiFiManagerParameter urlParam("warp_url", "WARP Basis-URL", g_url,
                                sizeof(g_url));
  wm.addParameter(&urlParam);

  warpui::setStatus("WLAN ...", false);
  const bool connected = wm.autoConnect("WARP-Panel");
  if (!connected) {
    Serial.println("Wi-Fi configuration timed out, restarting");
    delay(500);
    ESP.restart();
  }

  const char *value = urlParam.getValue();
  if (value != nullptr && value[0] != '\0' &&
      strncmp(value, g_url, sizeof(g_url)) != 0) {
    snprintf(g_url, sizeof(g_url), "%s", value);
    saveUrl();
    g_reader.reset();
  }
  Serial.printf("Wi-Fi: %s   endpoint: %s\n", WiFi.localIP().toString().c_str(),
                g_url);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\nWARP Energy Manager panel"));

  loadUrl();

  if (!displayInit()) {
    Serial.println(F("FATAL: display init failed"));
    while (true) {
      delay(10);
    }
  }

  warpui::create(lv_screen_active());
  warpui::setStatus("Startet ...", false);
  backlightSetWakeable(touchInit());
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchReadCb);

  g_lastTick = millis();
  displayLooper();

  connectWifi();

  ArduinoOTA.setHostname("warp-panel");
  ArduinoOTA.begin();

  warpui::setStatus("Lade ...", true);
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

  if (now - g_lastPoll >= POLL_MS) {
    g_lastPoll = now;
    poll();
  }

  if (g_haveData && now - g_lastUi >= UI_MS) {
    g_lastUi = now;
    warpui::update(g_data);
  }

  displayLooper();
  delay(5);
}
