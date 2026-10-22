#include "wifi_store.h"
#include <Preferences.h>

namespace wifiStore {

static String ssids[MAX_NETWORKS];
static String passes[MAX_NETWORKS];
static int n = 0;

static void save() {
  Preferences prefs;
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.putInt("n", n);
  for (int i = 0; i < n; i++) {
    prefs.putString(("s" + String(i)).c_str(), ssids[i]);
    prefs.putString(("p" + String(i)).c_str(), passes[i]);
  }
  prefs.end();
}

void load() {
  Preferences prefs;
  prefs.begin("wifi", true);
  n = constrain(prefs.getInt("n", 0), 0, MAX_NETWORKS);
  for (int i = 0; i < n; i++) {
    ssids[i] = prefs.getString(("s" + String(i)).c_str(), "");
    passes[i] = prefs.getString(("p" + String(i)).c_str(), "");
  }
  prefs.end();
  Serial.printf("[store] %d saved network(s)\n", n);
}

void add(const String& ssid, const String& pass) {
  if (ssid.isEmpty()) return;
  int i = find(ssid);
  if (i >= 0) {
    passes[i] = pass;
  } else {
    if (n == MAX_NETWORKS) remove(ssids[0]);
    ssids[n] = ssid;
    passes[n] = pass;
    n++;
  }
  save();
  Serial.printf("[store] saved '%s'\n", ssid.c_str());
}

bool remove(const String& ssid) {
  int i = find(ssid);
  if (i < 0) return false;
  for (int j = i; j < n - 1; j++) {
    ssids[j] = ssids[j + 1];
    passes[j] = passes[j + 1];
  }
  n--;
  save();
  Serial.printf("[store] removed '%s'\n", ssid.c_str());
  return true;
}

int count() { return n; }
String ssidAt(int i) { return (i >= 0 && i < n) ? ssids[i] : String(); }
String passAt(int i) { return (i >= 0 && i < n) ? passes[i] : String(); }

int find(const String& ssid) {
  for (int i = 0; i < n; i++)
    if (ssids[i] == ssid) return i;
  return -1;
}

}  // namespace wifiStore
