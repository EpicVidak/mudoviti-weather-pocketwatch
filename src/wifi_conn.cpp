#include "wifi_conn.h"
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include "html_util.h"
#include "wifi_store.h"

#define HOSTNAME "ova-muda"
#define PORTAL_SSID "Pegazovo jaje"
#define PORTAL_TIMEOUT_S 300
#define CONNECT_TIMEOUT_MS 15000
#define BACKOFF_MS 10000
// Lower TX power: fixes flaky connections on many ESP32-C3 Super Mini boards.
#define REDUCE_TX_POWER 1

struct Candidate {
  String ssid;
  String pass;
};

class Portal : public WiFiManager {
 public:
  using WiFiManager::getHTTPHead;  // protected in the library; gives our page WiFiManager's look
};

static Portal wm;
static WifiState state = WifiState::Scanning;
static uint32_t stateSince = 0;
static Candidate candidates[wifiStore::MAX_NETWORKS];
static int candidateCount = 0;
static int candidateIdx = 0;
static String currentSsid;

static void setState(WifiState s) {
  state = s;
  stateSince = millis();
}

static void applyTxPower() {
#if REDUCE_TX_POWER
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
#endif
}

static void startScan() {
  WiFi.disconnect();
  WiFi.scanDelete();
  WiFi.scanNetworks(true);
  currentSsid = "";
  setState(WifiState::Scanning);
}

static void tryCandidate() {
  const Candidate& c = candidates[candidateIdx];
  currentSsid = c.ssid;
  Serial.printf("[wifi] connecting to '%s'\n", c.ssid.c_str());
  WiFi.disconnect();
  WiFi.begin(c.ssid.c_str(), c.pass.c_str());
  setState(WifiState::Connecting);
}

static void nextCandidateOrBackoff() {
  candidateIdx++;
  if (candidateIdx < candidateCount) {
    tryCandidate();
  } else {
    WiFi.disconnect();
    currentSsid = "";
    setState(WifiState::Backoff);
  }
}

// Picks saved networks seen in the scan, strongest first.
static void collectCandidates(int found) {
  int32_t rssi[wifiStore::MAX_NETWORKS];
  candidateCount = 0;
  for (int i = 0; i < found; i++) {
    int idx = wifiStore::find(WiFi.SSID(i));
    if (idx < 0) continue;
    bool dup = false;
    for (int j = 0; j < candidateCount; j++) {
      if (candidates[j].ssid == WiFi.SSID(i)) {
        rssi[j] = max(rssi[j], WiFi.RSSI(i));
        dup = true;
      }
    }
    if (dup) continue;
    candidates[candidateCount] = {wifiStore::ssidAt(idx), wifiStore::passAt(idx)};
    rssi[candidateCount] = WiFi.RSSI(i);
    candidateCount++;
  }
  for (int i = 1; i < candidateCount; i++) {
    for (int j = i; j > 0 && rssi[j] > rssi[j - 1]; j--) {
      std::swap(rssi[j], rssi[j - 1]);
      std::swap(candidates[j], candidates[j - 1]);
    }
  }
  WiFi.scanDelete();
}

// "Saved networks" page inside the setup hotspot.
static void handleSaved() {
  String page = wm.getHTTPHead("Brisanje postojecih muda");
  page += "<h3>Brisanje postojecih muda</h3>";
  if (wifiStore::count() == 0) page += "<div class='msg'>No saved networks</div>";
  for (int i = 0; i < wifiStore::count(); i++) {
    String ssid = htmlEscape(wifiStore::ssidAt(i));
    page += "<div style='display:flex;align-items:center;gap:8px'>"
            "<span style='flex:1;font-weight:700'>" + ssid + "</span>"
            "<form method='post' action='/saved/delete' style='margin:0'>"
            "<input type='hidden' name='ssid' value=\"" + ssid + "\">"
            "<button class='D' style='width:auto;padding:0 16px' "
            "onclick=\"return confirm('Delete this network?')\">Delete</button></form></div>";
  }
  page += FPSTR(HTTP_BACKBTN);
  page += FPSTR(HTTP_END);
  wm.server->send(200, "text/html", page);
}

static void handleSavedDelete() {
  wifiStore::remove(wm.server->arg("ssid"));
  wm.server->sendHeader("Location", "/saved");
  wm.server->send(303);
}

// Runs each time WiFiManager creates the hotspot's web server.
static void addPortalRoutes() {
  wm.server->on("/saved", HTTP_GET, handleSaved);
  wm.server->on("/saved/delete", HTTP_POST, handleSavedDelete);
}

static void leavePortal() {
  MDNS.end();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  applyTxPower();
}

void wifiBegin() {
  wifiStore::load();
  WiFi.persistent(false);
  WiFi.setHostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  applyTxPower();

  wm.setConfigPortalBlocking(false);
  // No WiFiManager timeout: it restarts on every page load, so we time the portal ourselves.
  wm.setConfigPortalTimeout(0);
  wm.setHostname(HOSTNAME);
  // The whole menu is custom HTML so the buttons can be renamed. It links to WiFiManager's
  // own /wifi and /exit pages plus our /saved page (Info and Update dropped - no OTA partition).
  static std::vector<const char*> menu = {"custom"};
  wm.setMenu(menu);
  wm.setCustomMenuHTML(
      "<form action='/wifi' method='get'><button>Dodati novo mudo</button></form><br/>\n"
      "<form action='/saved' method='get'><button>Brisanje postojecih muda</button></form><br/>\n"
      "<hr><br/>"
      "<form action='/exit' method='get'><button>Odustajem :(</button></form><br/>\n");
  wm.setCustomHeadElement("<style>h1,h3{text-align:center}</style>");
  wm.setTitle("Nameštanje muda");
  wm.setWebServerCallback(addPortalRoutes);

  if (wifiStore::count() > 0) startScan();
  else setState(WifiState::Backoff);
}

void wifiLoop() {
  switch (state) {
    case WifiState::Scanning: {
      int found = WiFi.scanComplete();
      if (found == WIFI_SCAN_RUNNING) break;
      if (found < 0) {  // scan failed
        setState(WifiState::Backoff);
        break;
      }
      collectCandidates(found);
      Serial.printf("[wifi] scan: %d network(s), %d saved in range\n", found, candidateCount);
      candidateIdx = -1;
      nextCandidateOrBackoff();
      break;
    }

    case WifiState::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[wifi] connected to '%s', IP %s\n", currentSsid.c_str(),
                      WiFi.localIP().toString().c_str());
        setState(WifiState::Connected);
      } else if (millis() - stateSince > CONNECT_TIMEOUT_MS) {
        Serial.printf("[wifi] '%s' timed out\n", currentSsid.c_str());
        nextCandidateOrBackoff();
      }
      break;

    case WifiState::Connected:
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[wifi] connection lost");
        startScan();
      }
      break;

    case WifiState::Backoff:
      if (millis() - stateSince > BACKOFF_MS && wifiStore::count() > 0) startScan();
      break;

    case WifiState::Portal:
      if (wm.process()) {
        // WiFiManager only reports success after it actually connected.
        currentSsid = WiFi.SSID();
        wifiStore::add(currentSsid, WiFi.psk());
        Serial.printf("[wifi] portal added '%s'\n", currentSsid.c_str());
        if (wm.getConfigPortalActive()) wm.stopConfigPortal();
        leavePortal();
        if (WiFi.status() == WL_CONNECTED) setState(WifiState::Connected);
        else startScan();
      } else if (!wm.getConfigPortalActive()) {  // "Exit" on the page
        Serial.println("[wifi] portal closed from the page");
        leavePortal();
        startScan();
      } else if (millis() - stateSince >= PORTAL_TIMEOUT_S * 1000UL) {
        Serial.println("[wifi] portal timed out");
        wifiStopPortal();
      }
      break;
  }
}

bool wifiIsConnected() {
  return state == WifiState::Connected;
}

WifiState wifiState() {
  return state;
}

String wifiSsid() {
  return currentSsid;
}

void wifiStartPortal() {
  if (state == WifiState::Portal) return;
  Serial.println("[wifi] starting portal");
  WiFi.scanDelete();
  WiFi.disconnect();
  currentSsid = "";
  setState(WifiState::Portal);
  wm.startConfigPortal(PORTAL_SSID);
  // Also reachable as http://ova-muda.local while the hotspot is open.
  if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
}

void wifiStopPortal() {
  if (state != WifiState::Portal) return;
  Serial.println("[wifi] closing portal");
  wm.stopConfigPortal();
  leavePortal();
  startScan();
}

uint32_t wifiPortalSecondsLeft() {
  if (state != WifiState::Portal) return 0;
  uint32_t elapsed = (millis() - stateSince) / 1000;
  return elapsed >= PORTAL_TIMEOUT_S ? 0 : PORTAL_TIMEOUT_S - elapsed;
}
