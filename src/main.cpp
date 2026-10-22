#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "button.h"
#include "icons.h"
#include "weather.h"
#include "web_admin.h"
#include "wifi_conn.h"
#include "wifi_store.h"

#define SDA_PIN 5
#define SCL_PIN 6
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_ADDR 0x3C
#define BUTTON_PIN 9  // onboard BOOT button
#define LONG_PRESS_MS 3000

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Button button(BUTTON_PIN, LONG_PRESS_MS);

static void scanI2C() {
  Serial.println("Scanning I2C bus...");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  found device at 0x%02X\n", addr);
      found++;
    }
  }
  if (found == 0) Serial.println("  no I2C devices found - check wiring");
}

// The built-in font is ASCII only. Serbian lowercase letters are drawn as the base letter
// plus an accent in the two empty pixel rows above it (UTF-8 input).
enum Accent { NONE, CARON, ACUTE, STROKE };

static char baseGlyph(uint16_t cp, Accent& accent) {
  accent = NONE;
  switch (cp) {
    case 0x10D: accent = CARON; return 'c';   // č
    case 0x107: accent = ACUTE; return 'c';   // ć
    case 0x161: accent = CARON; return 's';   // š
    case 0x17E: accent = CARON; return 'z';   // ž
    case 0x111: accent = STROKE; return 'd';  // đ
    case 0xB0: return char(0xF8);             // ° (degree glyph in the CP437 font table)
    default: return cp < 0x80 ? char(cp) : '?';
  }
}

// Decodes one UTF-8 character starting at s[i] and advances i.
static uint16_t nextCodepoint(const String& s, unsigned& i) {
  uint8_t c = s[i++];
  if (c < 0x80 || i >= s.length()) return c;
  if ((c & 0xE0) == 0xC0) return ((c & 0x1F) << 6) | (s[i++] & 0x3F);
  while (i < s.length() && (s[i] & 0xC0) == 0x80) i++;  // longer sequences: skip
  return '?';
}

static int glyphCount(const String& s) {
  int n = 0;
  for (unsigned i = 0; i < s.length(); n++) nextCodepoint(s, i);
  return n;
}

static void drawText(int x, int y, const String& s) {
  for (unsigned i = 0; i < s.length(); x += 6) {
    Accent accent;
    // bg == fg means transparent: spaces must not paint black boxes over icons drawn earlier.
    display.drawChar(x, y, baseGlyph(nextCodepoint(s, i), accent), SSD1306_WHITE, SSD1306_WHITE, 1);
    switch (accent) {
      case CARON:
        display.drawPixel(x + 1, y, SSD1306_WHITE);
        display.drawPixel(x + 3, y, SSD1306_WHITE);
        display.drawPixel(x + 2, y + 1, SSD1306_WHITE);
        break;
      case ACUTE:
        display.drawPixel(x + 3, y, SSD1306_WHITE);
        display.drawPixel(x + 2, y + 1, SSD1306_WHITE);
        break;
      case STROKE:
        display.drawFastHLine(x + 2, y + 1, 4, SSD1306_WHITE);
        break;
      case NONE:
        break;
    }
  }
}

// Horizontal offset for a line that is `overflow` px wider than the screen:
// pause at the start, scroll to the end, pause, repeat.
static int marqueeOffset(int overflow, uint32_t startPauseMs) {
  const uint32_t endPauseMs = 1000, msPerPx = 28;
  uint32_t t = millis() % (startPauseMs + endPauseMs + overflow * msPerPx);
  if (t < startPauseMs) return 0;
  t -= startPauseMs;
  return min<int>(t / msPerPx, overflow);
}

// Content of the last frame sent to the display, shared by all screens so a redraw happens
// only when something visible changed (text, scroll position, icons, ...).
static String lastFrameKey;

static bool frameChanged(const String& key) {
  if (key == lastFrameKey) return false;
  lastFrameKey = key;
  return true;
}

static int scrollX(const String& line, uint32_t startPauseMs) {
  int overflow = glyphCount(line) * 6 - SCREEN_WIDTH;  // 6 px per char at size 1
  return overflow > 0 ? -marqueeOffset(overflow, startPauseMs) : 0;
}

// Draws up to 4 lines of size-1 text plus an optional progress bar (0-100, -1 = none),
// only touching the display when something changed. Lines wider than the screen scroll.
static void showStatus(const String& l1, const String& l2 = "", const String& l3 = "",
                       const String& l4 = "", int progress = -1) {
  const String* lines[] = {&l1, &l2, &l3, &l4};
  int x[4];
  String key = "status\n";
  for (int i = 0; i < 4; i++) {
    x[i] = scrollX(*lines[i], 1000);
    key += *lines[i] + '\n' + x[i] + '\n';
  }
  key += progress;
  if (!frameChanged(key)) return;

  display.clearDisplay();
  for (int i = 0; i < 4; i++) drawText(x[i], i * 8, *lines[i]);
  if (progress >= 0) {
    display.drawRect(0, 24, SCREEN_WIDTH, 8, SSD1306_WHITE);
    display.fillRect(2, 26, (SCREEN_WIDTH - 4) * progress / 100, 4, SSD1306_WHITE);
  }
  display.display();
}

static void drawIcon(int x, int y, WeatherIcon icon) {
  if (icon == WeatherIcon::None) return;
  const uint8_t* rows = ICONS[int(icon) - 1];
  for (int r = 0; r < ICON_SIZE; r++)
    for (int c = 0; c < ICON_SIZE; c++)
      if (rows[r] & (1 << (ICON_SIZE - 1 - c))) display.drawPixel(x + c, y + r, SSD1306_WHITE);
}

// Weather screen. Rows: title 0-6, hours 9-15, temperatures 17-23, icons 25-31.
// Hours, temperatures and icons scroll together (4 s pause before each pass);
// the current hour's cell (the second one) is drawn inverted over rows 8-31.
static void showWeather(const String& title, const String& hours, const String& temps,
                        const WeatherIcon* icons, int iconCount) {
  int xt = scrollX(title, 1000);
  int x = scrollX(hours, 4000);  // hours and temps have equal width -> same offset
  String key = "weather\n" + title + '\n' + xt + '\n' + hours + '\n' + temps + '\n' + x + '\n';
  for (int i = 0; i < iconCount; i++) key += char('A' + int(icons[i]));
  if (!frameChanged(key)) return;

  const int cellPx = WEATHER_CELL_CHARS * 6;
  display.clearDisplay();
  drawText(xt, 0, title);
  drawText(x, 9, hours);
  drawText(x, 17, temps);
  String separators;  // continue the '|' column lines through the icon row
  for (int i = 0; i < iconCount; i++) {
    separators += "      |";
    drawIcon(x + i * cellPx + 14, 25, icons[i]);  // centred between the cell's separators
  }
  drawText(x, 25, separators);
  // '|' sits 2 px into its 6 px char cell, so the separators around cell 1 are at
  // x + cellPx - 4 and x + 2 * cellPx - 4; invert everything between them.
  display.fillRect(x + cellPx - 3, 8, cellPx - 1, SCREEN_HEIGHT - 8, SSD1306_INVERSE);
  // The font's '|' is dashed; solid edges make the inverted block a clean rectangle.
  display.drawFastVLine(x + cellPx - 4, 8, SCREEN_HEIGHT - 8, SSD1306_WHITE);
  display.drawFastVLine(x + 2 * cellPx - 4, 8, SCREEN_HEIGHT - 8, SSD1306_WHITE);
  display.display();
}

static void updateDisplay() {
  uint32_t held = button.heldMs();
  if (held > 0) {
    if (held >= LONG_PRESS_MS)
      showStatus("AU AU AU PUSTI", "BOLI ME :(");
    else
      showStatus("Nastavi da stišćes...", "", "", "", held * 100 / LONG_PRESS_MS);
    return;
  }

  switch (wifiState()) {
    case WifiState::Scanning:
      showStatus("Trazimo muda...");
      break;
    case WifiState::Connecting:
      showStatus("", "Kuvam:", "", wifiSsid());
      break;
    case WifiState::Connected:
      if (weatherReady()) {
        char title[32];
        snprintf(title, sizeof(title), "Temperatura muda: %02dh", weatherCurrentHour());
        String hours, temps;
        weatherRows(hours, temps);
        WeatherIcon icons[16];
        int iconCount = weatherIcons(icons, 16);
        showWeather(title, hours, temps, icons, iconCount);
      } else {
        showStatus("Mudo: " + wifiSsid(), WiFi.localIP().toString(), "Ucitavam temperaturu...");
      }
      break;
    case WifiState::Backoff:
      showStatus("Nema muda :(", "", "Stisni me za mudo");
      break;
    case WifiState::Portal:
      showStatus("Spremite muda moliću!","", "http://ova-muda.local");
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);  // let the serial monitor attach

  Wire.begin(SDA_PIN, SCL_PIN);
  scanI2C();

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("SSD1306 init failed - check address/wiring");
    for (;;) delay(1000);
  }
  display.cp437(true);  // true CP437 glyph positions; otherwise drawChar shifts chars >= 176 by one
  showStatus("Starting...");

  button.begin();
  wifiBegin();
}

void loop() {
  button.update();
  button.shortPress();  // unused for now
  if (button.longPress()) {
    if (wifiState() == WifiState::Portal) {
      wifiStopPortal();
    } else {
      webAdminEnd();  // the portal needs port 80
      wifiStartPortal();
    }
  }

  wifiLoop();
  weatherLoop();
  if (wifiIsConnected()) webAdminBegin();
  else webAdminEnd();
  webAdminLoop();

  updateDisplay();
  delay(10);
}
