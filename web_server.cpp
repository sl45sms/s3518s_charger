#include "web_server.h"

#include <Arduino.h>

#if defined(ARDUINO_ARCH_ESP32)
#define ENABLE_WEBSERVER 1
#else
#define ENABLE_WEBSERVER 0
#endif

#if ENABLE_WEBSERVER
#ifndef YOUR_WIFI_SSID
#define YOUR_WIFI_SSID ""
#endif
#ifndef YOUR_WIFI_PASSWORD
#define YOUR_WIFI_PASSWORD ""
#endif

#include <WiFi.h>
#include <WebServer.h>

static constexpr const char *WIFI_SSID = YOUR_WIFI_SSID;
static constexpr const char *WIFI_PASS = YOUR_WIFI_PASSWORD;
static constexpr uint16_t WEB_PORT = 80;

static WebServer server(WEB_PORT);

struct CachedStatus {
  uint32_t ms;
  int vin_mV;
  int vout_mV;
  int iout_usbc_mA;
  int iout_usba_mA;
  int pdVersion;
  SW35xx::fastChargeType_t fastChargeType;
};

static CachedStatus gStatus = {0};

enum class WifiState : uint8_t { Idle, Connecting, Connected };
static WifiState gWifiState = WifiState::Idle;
static uint32_t gNextWifiActionMs = 0;
static bool gLoggedMissingWifi = false;

static void updateCachedStatus(const Sw3518Status &status, uint32_t nowMs) {
  gStatus.ms = nowMs;
  gStatus.vin_mV = status.vin_mV;
  gStatus.vout_mV = status.vout_mV;
  gStatus.iout_usbc_mA = status.iout_usbc_mA;
  gStatus.iout_usba_mA = status.iout_usba_mA;
  gStatus.fastChargeType = status.fastChargeType;
  gStatus.pdVersion = status.pdVersion;
}

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1"/>
  <title>SW3518 Monitor</title>
  <style>
    body{font-family:system-ui,Segoe UI,Arial,sans-serif;margin:16px;max-width:720px}
    .row{display:flex;gap:12px;flex-wrap:wrap}
    .card{border:1px solid #ddd;border-radius:8px;padding:12px;min-width:200px}
    .k{color:#666;font-size:12px}
    .v{font-size:22px;font-weight:600}
    code{background:#f6f6f6;padding:2px 6px;border-radius:6px}
  </style>
</head>
<body>
  <h2>SW3518 Status</h2>
  <div class="row">
    <div class="card"><div class="k">VIN</div><div class="v" id="vin">-</div></div>
    <div class="card"><div class="k">VOUT</div><div class="v" id="vout">-</div></div>
    <div class="card"><div class="k">USB-C</div><div class="v" id="usbc">-</div></div>
    <div class="card"><div class="k">USB-A</div><div class="v" id="usba">-</div></div>
    <div class="card"><div class="k">Type</div><div class="v" id="type">-</div></div>
    <div class="card"><div class="k">PD</div><div class="v" id="pd">-</div></div>
  </div>
  <p>API: <code>/api/status</code></p>
  <script>
    async function tick(){
      try{
        const r = await fetch('/api/status', {cache:'no-store'});
        if(!r.ok) throw new Error(r.status);
        const j = await r.json();
        document.getElementById('vin').textContent  = j.vin_mV + ' mV';
        document.getElementById('vout').textContent = j.vout_mV + ' mV';
        document.getElementById('usbc').textContent = j.iout_usbc_mA + ' mA';
        document.getElementById('usba').textContent = j.iout_usba_mA + ' mA';
        document.getElementById('type').textContent = j.fastChargeType;
        document.getElementById('pd').textContent   = j.pdVersion ? ('v' + j.pdVersion) : '-';
      }catch(e){
      }
      setTimeout(tick, 500);
    }
    tick();
  </script>
</body>
</html>
)HTML";

static void handleRoot() {
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

static void handleStatus() {
  char json[256];
  const char *typeStr = fastChargeType2String(gStatus.fastChargeType);
  const uint16_t total_mA = (uint16_t)(gStatus.iout_usbc_mA + gStatus.iout_usba_mA);

  snprintf(json, sizeof(json),
           "{\"ms\":%lu,\"vin_mV\":%d,\"vout_mV\":%d,\"iout_usbc_mA\":%d,\"iout_usba_mA\":%d,\"iout_total_mA\":%u,\"fastChargeType\":\"%s\",\"pdVersion\":%d}",
           (unsigned long)gStatus.ms,
           gStatus.vin_mV,
           gStatus.vout_mV,
           gStatus.iout_usbc_mA,
           gStatus.iout_usba_mA,
           (unsigned)total_mA,
           typeStr,
           gStatus.pdVersion);

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json; charset=utf-8", json);
}

static void webSetupRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.onNotFound([]() { server.send(404, "text/plain; charset=utf-8", "Not found"); });
}

static void wifiLoop(uint32_t now) {
  if (WIFI_SSID[0] == '\0' || WIFI_PASS[0] == '\0') {
    if (!gLoggedMissingWifi) {
      gLoggedMissingWifi = true;
      Serial.println("WiFi disabled: set YOUR_WIFI_SSID/YOUR_WIFI_PASSWORD (see build.sh .env)");
    }
    return;
  }

  const wl_status_t st = WiFi.status();

  if (st == WL_CONNECT_FAILED) {
    gWifiState = WifiState::Idle;
    gNextWifiActionMs = now + 5000;
    WiFi.disconnect(true);
    Serial.println("WiFi connect failed; retrying in 5s");
    return;
  }

  if (st == WL_CONNECTED) {
    if (gWifiState != WifiState::Connected) {
      gWifiState = WifiState::Connected;
      Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
      server.begin();
      Serial.printf("HTTP server started on port %u\n", (unsigned)WEB_PORT);
    }
    return;
  }

  if (now < gNextWifiActionMs) return;

  gNextWifiActionMs = now + 10000;

  if (gWifiState != WifiState::Connecting) {
    gWifiState = WifiState::Connecting;
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.printf("WiFi connecting to %s...\n", WIFI_SSID);
  } else {
    WiFi.disconnect(false);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.println("WiFi retry...");
  }
}

bool webEnabled() { return ENABLE_WEBSERVER; }

void webInit() {
  webSetupRoutes();

  uint8_t macAddr[6];
  WiFi.macAddress(macAddr);
  Serial.print("MAC Address: ");
  for (int i = 0; i < 6; i++) {
    if (macAddr[i] < 0x10) Serial.print("0");
    Serial.print(macAddr[i], HEX);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
}

void webLoop(uint32_t nowMs) {
  wifiLoop(nowMs);
  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();
  }
}

void webUpdateStatus(const Sw3518Status &status, uint32_t nowMs) { updateCachedStatus(status, nowMs); }

#else

bool webEnabled() { return ENABLE_WEBSERVER; }
void webInit() {}
void webLoop(uint32_t) {}
void webUpdateStatus(const Sw3518Status &, uint32_t) {}

#endif
