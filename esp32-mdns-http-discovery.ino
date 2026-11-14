/*
 * ESP32 mDNS Hostname and Service Discovery
 * -----------------------------------------
 * Runs a small HTTP server on an ESP32 and makes it reachable at a friendly
 * name - http://fluidwire-node.local/ - instead of an IP address that changes
 * every time the router hands out a new DHCP lease. The board also advertises
 * its server as an _http._tcp service with TXT records, so a laptop, phone or
 * another ESP32 can find it by browsing the network instead of scanning IPs.
 *
 * What it demonstrates:
 *   - MDNS.begin() to claim a <hostname>.local name
 *   - MDNS.addService() / addServiceTxt() to advertise an HTTP service
 *   - MDNS.queryService() to browse for OTHER _http._tcp devices on the LAN
 *   - a WebServer with an HTML status page and two JSON endpoints
 *   - re-registering mDNS after a WiFi drop so the name keeps resolving
 *
 * Endpoints:
 *   GET /               HTML status page (auto-refreshes every 10 s)
 *   GET /api/status     JSON: hostname, ip, rssi, uptime, heap, request count
 *   GET /api/browse     JSON: every _http._tcp service found on the network
 *                       (blocks ~3 s while the query runs)
 *
 * Required libraries:
 *   none from Library Manager - WiFi, WebServer and ESPmDNS are built into
 *   the ESP32 Arduino core.
 *
 * Wiring:
 *   none - just the ESP32 dev board and USB.
 *
 * Setup:
 *   1. Copy config.h.example to config.h and fill in WiFi + the hostname.
 *   2. Flash, open Serial Monitor at 115200.
 *   3. From a laptop on the same WiFi open http://fluidwire-node.local/
 *
 * (c) 2026 Fluidwire. Released under the MIT License.
 * https://github.com/fluidwire
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "config.h"

// --- Tunables ---------------------------------------------------------------
const uint16_t HTTP_PORT          = 80;
const uint32_t STATUS_INTERVAL_MS = 30000UL;   // serial status line
const uint32_t WIFI_RETRY_MS      = 5000UL;    // reconnect attempt spacing
const uint32_t WIFI_BOOT_TIMEOUT  = 20000UL;   // give up waiting at boot, loop() retries

// --- State ------------------------------------------------------------------
WebServer server(HTTP_PORT);
bool     mdnsUp          = false;
bool     wifiWasUp       = false;
uint32_t lastStatusMs    = 0;
uint32_t lastWifiTryMs   = 0;
uint32_t requestCount    = 0;

// --- Helpers ----------------------------------------------------------------
String serviceAddress(int idx) {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  return MDNS.address(idx).toString();     // core 3.x API
#else
  return MDNS.IP(idx).toString();          // core 2.x API
#endif
}

String jsonEscape(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c == '\n' || c == '\r') { out += ' '; }
    else out += c;
  }
  return out;
}

void logRequest(const char* path) {
  requestCount++;
  Serial.printf("[http] GET %s from %s\n", path, server.client().remoteIP().toString().c_str());
}

// --- mDNS -------------------------------------------------------------------
void startMdns() {
  if (mdnsUp) {
    MDNS.end();
    mdnsUp = false;
  }
  if (!MDNS.begin(MDNS_HOSTNAME)) {
    Serial.println("[mdns] start FAILED - hostname taken or invalid?");
    return;
  }
  MDNS.setInstanceName(MDNS_INSTANCE);
  MDNS.addService("http", "tcp", HTTP_PORT);
  MDNS.addServiceTxt("http", "tcp", "vendor", "fluidwire");
  MDNS.addServiceTxt("http", "tcp", "board", "esp32");
  MDNS.addServiceTxt("http", "tcp", "path", "/");
  mdnsUp = true;
  Serial.printf("[mdns] advertising http://%s.local/ (_http._tcp on port %u)\n",
                MDNS_HOSTNAME, HTTP_PORT);
}

// --- WiFi -------------------------------------------------------------------
void connectWifi() {
  Serial.printf("[wifi] connecting to %s\n", WIFI_SSID);
  WiFi.persistent(false);
  WiFi.setHostname(MDNS_HOSTNAME);          // DHCP hostname too, before mode()
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_BOOT_TIMEOUT) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();
  lastWifiTryMs = millis();
}

void onWifiUp() {
  Serial.printf("[wifi] IP: %s  hostname: %s  RSSI: %d dBm\n",
                WiFi.localIP().toString().c_str(), WiFi.getHostname(), WiFi.RSSI());
  startMdns();
}

// --- HTTP handlers ----------------------------------------------------------
void handleRoot() {
  logRequest("/");
  String page;
  page.reserve(1400);
  page += F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
            "<meta http-equiv='refresh' content='10'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>");
  page += MDNS_HOSTNAME;
  page += F(".local</title><style>body{font-family:system-ui,sans-serif;margin:2rem;"
            "max-width:40rem}table{border-collapse:collapse}td{padding:.3rem .8rem;"
            "border-bottom:1px solid #ddd}td:first-child{color:#555}code{background:#f3f3f3;"
            "padding:.1rem .3rem}</style></head><body><h1>");
  page += MDNS_INSTANCE;
  page += F("</h1><p>You reached this ESP32 by name, not by IP.</p><table>");
  page += F("<tr><td>hostname</td><td><code>http://"); page += MDNS_HOSTNAME; page += F(".local/</code></td></tr>");
  page += F("<tr><td>ip</td><td>");        page += WiFi.localIP().toString(); page += F("</td></tr>");
  page += F("<tr><td>rssi</td><td>");      page += (int)WiFi.RSSI();   page += F(" dBm</td></tr>");
  page += F("<tr><td>uptime</td><td>");    page += millis() / 1000UL; page += F(" s</td></tr>");
  page += F("<tr><td>free heap</td><td>"); page += ESP.getFreeHeap(); page += F(" bytes</td></tr>");
  page += F("<tr><td>requests</td><td>");  page += requestCount; page += F("</td></tr>");
  page += F("</table><p>JSON: <a href='/api/status'>/api/status</a> &middot; "
            "<a href='/api/browse'>/api/browse</a> (lists other _http._tcp devices, takes ~3 s)</p>"
            "<p style='color:#888'>IoT example by <a href='https://fluidwire.com/'>Fluidwire</a></p>"
            "</body></html>");
  server.send(200, "text/html", page);
}

void handleStatus() {
  logRequest("/api/status");
  String json;
  json.reserve(256);
  json += F("{\"hostname\":\"");  json += MDNS_HOSTNAME; json += F(".local\"");
  json += F(",\"instance\":\"");  json += jsonEscape(MDNS_INSTANCE); json += '"';
  json += F(",\"ip\":\"");        json += WiFi.localIP().toString(); json += '"';
  json += F(",\"rssi\":");        json += (int)WiFi.RSSI();
  json += F(",\"uptime_s\":");    json += millis() / 1000UL;
  json += F(",\"heap\":");        json += ESP.getFreeHeap();
  json += F(",\"requests\":");    json += requestCount;
  json += F(",\"mdns\":");        json += mdnsUp ? F("true") : F("false");
  json += '}';
  server.send(200, "application/json", json);
}

void handleBrowse() {
  logRequest("/api/browse");
  int n = MDNS.queryService("http", "tcp");   // blocks ~3 s
  Serial.printf("[mdns] browse _http._tcp: %d found\n", n);
  String json;
  json.reserve(128 + n * 96);
  json += F("{\"service\":\"_http._tcp\",\"count\":");
  json += n;
  json += F(",\"devices\":[");
  for (int i = 0; i < n; i++) {
    if (i) json += ',';
    json += F("{\"hostname\":\"");
    json += jsonEscape(MDNS.hostname(i));
    json += F("\",\"ip\":\"");
    json += serviceAddress(i);
    json += F("\",\"port\":");
    json += MDNS.port(i);
    json += F(",\"txt\":{");
    int t = MDNS.numTxt(i);
    for (int k = 0; k < t; k++) {
      if (k) json += ',';
      json += '"'; json += jsonEscape(MDNS.txtKey(i, k)); json += F("\":\"");
      json += jsonEscape(MDNS.txt(i, k)); json += '"';
    }
    json += F("}}");
  }
  json += F("]}");
  server.send(200, "application/json", json);
}

void handleNotFound() {
  requestCount++;
  server.send(404, "text/plain", "not found - try / or /api/status");
}

// --- Arduino ----------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[main] esp32-mdns-http-discovery");

  connectWifi();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/browse", HTTP_GET, handleBrowse);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.printf("[http] server listening on port %u\n", HTTP_PORT);

  if (WiFi.status() == WL_CONNECTED) {
    wifiWasUp = true;
    onWifiUp();
  } else {
    Serial.println("[wifi] not connected yet - will keep retrying in loop()");
  }
}

void loop() {
  bool up = (WiFi.status() == WL_CONNECTED);

  if (up && !wifiWasUp) {
    wifiWasUp = true;
    Serial.println("[wifi] reconnected");
    onWifiUp();                              // mDNS must be re-registered
  } else if (!up && wifiWasUp) {
    wifiWasUp = false;
    mdnsUp = false;
    Serial.println("[wifi] connection lost");
  }

  if (!up && millis() - lastWifiTryMs >= WIFI_RETRY_MS) {
    lastWifiTryMs = millis();
    Serial.println("[wifi] retrying...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  server.handleClient();

  if (millis() - lastStatusMs >= STATUS_INTERVAL_MS) {
    lastStatusMs = millis();
    Serial.printf("[status] uptime=%lus ip=%s rssi=%ddBm requests=%lu mdns=%s heap=%u\n",
                  (unsigned long)(millis() / 1000UL),
                  up ? WiFi.localIP().toString().c_str() : "none",
                  up ? WiFi.RSSI() : 0,
                  (unsigned long)requestCount,
                  mdnsUp ? "up" : "down",
                  ESP.getFreeHeap());
  }
  delay(2);
}
