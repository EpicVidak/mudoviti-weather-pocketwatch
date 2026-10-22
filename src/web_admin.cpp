#include "web_admin.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "html_util.h"
#include "wifi_store.h"

#define MDNS_NAME "ova-muda"
// Uncomment to require a login for the page.
// #define ADMIN_USER "admin"
// #define ADMIN_PASS "changeme"

static WebServer server(80);
static bool running = false;
static bool routesAdded = false;

static bool checkAuth() {
#ifdef ADMIN_USER
  if (!server.authenticate(ADMIN_USER, ADMIN_PASS)) {
    server.requestAuthentication();
    return false;
  }
#endif
  return true;
}

static void redirectHome() {
  server.sendHeader("Location", "/");
  server.send(303);
}

static void handleRoot() {
  if (!checkAuth()) return;
  String current = WiFi.SSID();
  String html =
      "<!doctype html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Wi-Fi networks</title><style>"
      "body{font-family:sans-serif;max-width:480px;margin:auto;padding:16px}"
      "table{width:100%;border-collapse:collapse}td{padding:6px;border-bottom:1px solid #ccc}"
      "input{padding:6px;margin:4px 0;width:100%;box-sizing:border-box}"
      "button{padding:6px 12px}.cur{font-weight:bold}"
      "</style></head><body><h2>Saved Wi-Fi networks</h2>";
  html += "<p>Connected to <b>" + htmlEscape(current) + "</b> (" +
          String(WiFi.RSSI()) + " dBm), IP " + WiFi.localIP().toString() + "</p>";

  html += "<table>";
  if (wifiStore::count() == 0) html += "<tr><td>No saved networks</td></tr>";
  for (int i = 0; i < wifiStore::count(); i++) {
    String ssid = htmlEscape(wifiStore::ssidAt(i));
    bool isCur = wifiStore::ssidAt(i) == current;
    html += "<tr><td" + String(isCur ? " class='cur'" : "") + ">" + ssid +
            (isCur ? " (current)" : "") +
            "</td><td style='text-align:right'><form method='post' action='/delete'>"
            "<input type='hidden' name='ssid' value=\"" + ssid + "\">"
            "<button onclick=\"return confirm('Delete this network?')\">Delete</button>"
            "</form></td></tr>";
  }
  html += "</table>";

  html += "<h3>Add network</h3><form method='post' action='/add'>"
          "<input name='ssid' placeholder='SSID' required maxlength='32'>"
          "<input name='pass' type='password' placeholder='Password' maxlength='63'>"
          "<button>Add</button></form>";
  html += "<p><small>Up to " + String(wifiStore::MAX_NETWORKS) +
          " networks; adding more drops the oldest.</small></p></body></html>";
  server.send(200, "text/html", html);
}

static void handleAdd() {
  if (!checkAuth()) return;
  wifiStore::add(server.arg("ssid"), server.arg("pass"));
  redirectHome();
}

static void handleDelete() {
  if (!checkAuth()) return;
  wifiStore::remove(server.arg("ssid"));
  redirectHome();
}

void webAdminBegin() {
  if (running) return;
  if (!routesAdded) {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/add", HTTP_POST, handleAdd);
    server.on("/delete", HTTP_POST, handleDelete);
    server.onNotFound(redirectHome);
    routesAdded = true;
  }
  server.begin();
  if (MDNS.begin(MDNS_NAME)) MDNS.addService("http", "tcp", 80);
  running = true;
  Serial.println("[web] admin page at http://" MDNS_NAME ".local");
}

void webAdminEnd() {
  if (!running) return;
  server.stop();
  MDNS.end();
  running = false;
}

void webAdminLoop() {
  if (running) server.handleClient();
}
