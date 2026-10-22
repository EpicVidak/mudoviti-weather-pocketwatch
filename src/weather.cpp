#include "weather.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include "wifi_conn.h"

#define WEATHER_URL                                                                  \
  "https://api.open-meteo.com/v1/forecast?latitude=44.787&longitude=20.457"         \
  "&hourly=temperature_2m,weather_code,is_day&timezone=Europe%2FBelgrade&forecast_days=2"
#define TZ_BELGRADE "CET-1CEST,M3.5.0,M10.5.0/3"
#define HOURS_SHOWN 12
#define RETRY_MS (5 * 60 * 1000UL)  // only while the cache can't fill the screen
#define HTTP_TIMEOUT_MS 5000
#define MAX_ENTRIES 48
#define CACHE_VERSION 1  // bump when Entry changes, so an old cache is ignored

struct Entry {
  char key[14];  // local time "YYYY-MM-DDTHH"
  float temp;    // NAN if missing
  int8_t code;   // WMO weather code, -1 if missing
  bool isDay;
};

static Entry entries[MAX_ENTRIES];
static int entryCount = 0;
static bool clockStarted = false;
static bool cacheLoaded = false;
static char fetchedHour[14] = "";    // hour key of the last successful fetch (also in flash)
static char attemptedHour[14] = "";  // hour key of the last fetch attempt (RAM only)
static uint32_t lastAttempt = 0;

static bool localNow(struct tm& t) {
  return clockStarted && getLocalTime(&t, 0);
}

// Current local hour as "YYYY-MM-DDTHH", the same format as Entry::key.
static bool currentHourKey(char (&key)[14]) {
  struct tm t;
  if (!localNow(t)) return false;
  strftime(key, sizeof(key), "%Y-%m-%dT%H", &t);
  return true;
}

// Index of the entry for the current local hour, or -1.
static int currentIndex() {
  char key[14];
  if (!currentHourKey(key)) return -1;
  for (int i = 0; i < entryCount; i++)
    if (strcmp(entries[i].key, key) == 0) return i;
  return -1;
}

// The cache lives in NVS ("weather" namespace): version, fetch hour, count, raw entries.
static void loadCache() {
  Preferences prefs;
  if (!prefs.begin("weather", true)) return;  // nothing saved yet
  int n = prefs.getInt("n", 0);
  if (prefs.getInt("v", 0) == CACHE_VERSION && n > 0 && n <= MAX_ENTRIES &&
      prefs.getBytes("entries", entries, sizeof(entries)) == n * sizeof(Entry)) {
    entryCount = n;
    prefs.getString("hour", fetchedHour, sizeof(fetchedHour));
    Serial.printf("[weather] cache: %d hours, fetched at %s\n", n, fetchedHour);
  }
  prefs.end();
}

static void saveCache() {
  Preferences prefs;
  prefs.begin("weather", false);
  prefs.putInt("v", CACHE_VERSION);
  prefs.putString("hour", fetchedHour);
  prefs.putInt("n", entryCount);
  prefs.putBytes("entries", entries, entryCount * sizeof(Entry));
  prefs.end();
}

static bool coversWindow() {
  int i = currentIndex();
  return i >= 0 && i + HOURS_SHOWN <= entryCount;
}

static bool fetch() {
  WiFiClientSecure client;
  client.setInsecure();  // public weather data; no root certificate to keep up to date
  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(client, WEATHER_URL)) {
    Serial.println("[weather] begin failed");
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[weather] HTTP %d\n", code);
    http.end();
    return false;
  }
  String body = http.getString();  // handles chunked replies, ~2.5 KB
  http.end();

  JsonDocument filter;
  filter["hourly"]["time"] = true;
  filter["hourly"]["temperature_2m"] = true;
  filter["hourly"]["weather_code"] = true;
  filter["hourly"]["is_day"] = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) {
    Serial.printf("[weather] JSON error: %s\n", err.c_str());
    return false;
  }

  JsonArray times = doc["hourly"]["time"];
  JsonArray temps = doc["hourly"]["temperature_2m"];
  JsonArray codes = doc["hourly"]["weather_code"];
  JsonArray isDay = doc["hourly"]["is_day"];
  int n = min<int>(min(times.size(), temps.size()), MAX_ENTRIES);
  if (n == 0) {
    Serial.println("[weather] empty reply");
    return false;
  }
  for (int i = 0; i < n; i++) {  // the reply is complete here, so overwriting the cache is safe
    const char* time = times[i] | "";
    strlcpy(entries[i].key, time, sizeof(entries[i].key));  // "2026-10-22T14:00" -> "2026-10-22T14"
    entries[i].temp = temps[i].isNull() ? NAN : temps[i].as<float>();
    entries[i].code = codes[i].isNull() ? -1 : codes[i].as<int>();  // missing array -> null too
    entries[i].isDay = (isDay[i] | 1) != 0;
  }
  entryCount = n;
  Serial.printf("[weather] fetched %d hours\n", n);
  return true;
}

// At most one API call per clock hour: fetch only when the cache is from an earlier hour.
// If that hour's call failed, retry every RETRY_MS only while the cache can't fill the screen.
void weatherLoop() {
  if (!cacheLoaded) {
    loadCache();
    cacheLoaded = true;
  }
  if (!wifiIsConnected()) return;
  if (!clockStarted) {
    configTzTime(TZ_BELGRADE, "pool.ntp.org", "time.google.com");
    clockStarted = true;
  }

  char hour[14];
  if (!currentHourKey(hour)) return;  // wait for NTP: the hour decides everything
  if (strcmp(hour, fetchedHour) == 0) return;  // cache is from this hour
  if (strcmp(hour, attemptedHour) == 0 &&
      (coversWindow() || millis() - lastAttempt < RETRY_MS))
    return;  // already tried this hour

  strlcpy(attemptedHour, hour, sizeof(attemptedHour));
  lastAttempt = millis();
  if (fetch()) {
    strlcpy(fetchedHour, hour, sizeof(fetchedHour));
    saveCache();
  }
}

bool weatherReady() {
  return coversWindow();
}

int weatherCurrentHour() {
  struct tm t;
  return localNow(t) ? t.tm_hour : -1;
}

// " <value padded to 5 visible chars>|". Padding counts visible characters, because
// "°" is two bytes in UTF-8 but one glyph on screen.
static String cell(const String& value, int visibleLen) {
  String c = " " + value;
  for (int i = visibleLen; i < 5; i++) c += ' ';
  return c + '|';
}

void weatherRows(String& hours, String& temps) {
  hours = "";
  temps = "";
  int start = currentIndex();
  if (start < 0) return;
  // Empty first cell so the current hour sits in the middle of the 3 visible cells.
  hours = temps = cell("", 0);
  for (int i = start; i < start + HOURS_SHOWN && i < entryCount; i++) {
    hours += cell(String(entries[i].key + 11) + "h", 3);  // "14" + "h"
    if (isnan(entries[i].temp)) {
      temps += cell("--", 2);
    } else {
      String deg = String(lroundf(entries[i].temp));
      temps += cell(deg + "°C", deg.length() + 2);
    }
  }
}

// Groups WMO weather codes (https://open-meteo.com/en/docs, "WMO Weather interpretation codes").
static WeatherIcon iconFor(int code, bool isDay) {
  switch (code) {
    case 0: return isDay ? WeatherIcon::Sun : WeatherIcon::Moon;
    case 1: case 2: return isDay ? WeatherIcon::PartlySun : WeatherIcon::PartlyMoon;
    case 3: return WeatherIcon::Cloud;
    case 45: case 48: return WeatherIcon::Fog;
    case 51: case 53: case 55: case 56: case 57: return WeatherIcon::Drizzle;
    case 61: case 63: case 65: case 66: case 67:
    case 80: case 81: case 82: return WeatherIcon::Rain;
    case 71: case 73: case 75: case 77: case 85: case 86: return WeatherIcon::Snow;
    case 95: case 96: case 99: return WeatherIcon::Storm;
    default: return WeatherIcon::None;
  }
}

int weatherIcons(WeatherIcon* out, int max) {
  int start = currentIndex();
  if (start < 0 || max < 1) return 0;
  int n = 0;
  out[n++] = WeatherIcon::None;  // the leading empty cell
  for (int i = start; i < start + HOURS_SHOWN && i < entryCount && n < max; i++)
    out[n++] = iconFor(entries[i].code, entries[i].isDay);
  return n;
}
