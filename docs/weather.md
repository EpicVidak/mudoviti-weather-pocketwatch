# Weather screen: hourly temperature and conditions for Belgrade

While the board is on Wi-Fi, the OLED shows the temperature and a weather icon (sun, cloud, rain…) for the **next 12 hours in Belgrade**: the current hour plus the 11 after it, continuing past midnight. The data comes from [Open-Meteo](https://open-meteo.com), which is free and needs no API key.

This builds on the Wi-Fi layer described in [wifi-connection.md](wifi-connection.md).

```
Temperatura muda: 14h        ← pixel rows 0–6: title + current hour (21 chars, fits exactly)
      █ 14h  █ 15h  |…       ← rows 9–15: hours
      █ 21°C █ 22°C |…       ← rows 17–23: temperatures
      █  ☂   █  ☁   |…       ← rows 25–31: 7×7 weather icons
 ↑ empty ↑ current hour: block drawn inverted (lit background, dark content), rows 8–31
```

Three blocks fit on the screen at once. The rows start with **one empty block**, so while the screen is still the current hour sits **in the middle**, drawn inverted, with the next hour to its right. Hours, temperatures, icons and the inverted block all scroll sideways together through the 12 hours. Each pass:
1. holds still for **4 s**,
2. scrolls to the end in about 12 s,
3. pauses 1 s,
4. starts over.

Until the first data has arrived and the clock is set, the screen shows the connection info instead:

```
Mudo: <ssid>
<IP address>
Ucitavam temperaturu...
```

---

## Files

| File | Role |
|---|---|
| [src/weather.h](../src/weather.h) / [src/weather.cpp](../src/weather.cpp) | Fetching, parsing, the clock, building the hour and temperature rows, mapping weather codes to icons |
| [src/icons.h](../src/icons.h) | The 10 icon bitmaps (7×7 px) |
| [src/main.cpp](../src/main.cpp) | `weatherLoop()` in `loop()`, `showWeather()` (the weather screen), `drawIcon()`, the `°` character |
| [platformio.ini](../platformio.ini) | Adds `bblanchon/ArduinoJson@^7` |

`HTTPClient`, `WiFiClientSecure` and the time functions are part of the ESP32 Arduino core. Flash use went from about 42% to 48% of the 3 MB app space, mostly because of the HTTPS (TLS) code.

---

## Interface (`weather.h`)

```cpp
void weatherLoop();          // call every loop; only does anything while Wi-Fi is connected
bool weatherReady();         // clock is set AND data covers the next 12 hours
int  weatherCurrentHour();   // local hour 0-23, or -1 if the clock isn't set yet
void weatherRows(String& hours, String& temps);   // hour row + temperature row, always equal length
int  weatherIcons(WeatherIcon* out, int max);     // one icon per cell of weatherRows(); returns the count
```

`main.cpp` uses it in the `WifiState::Connected` case:

```cpp
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
```

---

## How it works

### 1. The API request

```
https://api.open-meteo.com/v1/forecast?latitude=44.787&longitude=20.457
    &hourly=temperature_2m,weather_code,is_day&timezone=Europe%2FBelgrade&forecast_days=2
```

| Parameter | Why |
|---|---|
| `latitude`, `longitude` | Central Belgrade |
| `hourly=temperature_2m,weather_code,is_day` | Per hour: air temperature, the weather type as a WMO code, and whether it's daytime (1/0). Only these three, which keeps the reply small (about 2.5 KB) |
| `timezone=Europe/Belgrade` | Times in the reply are already Belgrade time (e.g. `"2026-10-22T14:00"`), so no time-zone math is needed |
| `forecast_days=2` | 48 hours, so "now + 11 hours" is always covered, even late in the evening |

The relevant part of the reply:

```json
"hourly": {
  "time":           ["2026-10-22T00:00", "2026-10-22T01:00", ...],
  "temperature_2m": [12.3, 11.8, ...],
  "weather_code":   [3, 51, ...],
  "is_day":         [0, 0, ...]
}
```

**HTTPS without checking the certificate.** The request goes through `WiFiClientSecure` with `setInsecure()`. The connection is encrypted, but the server's certificate isn't checked. Checking it would mean building a root certificate into the firmware, and that certificate eventually expires, which would break fetching until the firmware is updated. For public weather data, the worst case of skipping the check is a faked temperature, so the simpler option was chosen.

**Reading the reply.** Open-Meteo sends the reply in chunks (`Transfer-Encoding: chunked`). `http.getString()` reassembles the chunks; reading the raw stream would not. The text then goes to ArduinoJson with a **filter**, so only those four `hourly` arrays are kept in memory.

Each hour is stored as:

```cpp
struct Entry {
  char key[14];  // "2026-10-22T14": the time string cut after the hour
  float temp;    // NAN if the API returned null
  int8_t code;   // WMO weather code, -1 if missing
  bool isDay;
};
```

Up to 48 entries (`MAX_ENTRIES`).

### 2. The clock (NTP)

The board has no real-time clock, so it gets the time from the internet:

```cpp
configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com");
```

- This is called once, on the first Wi-Fi connection. The ESP32 then re-syncs the time in the background by itself.
- The time-zone string means Central European Time (UTC+1). Summer time (UTC+2) runs from the last Sunday in March to the last Sunday in October, so clock changes happen automatically.
- `getLocalTime(&t, 0)` reads the clock without waiting. Until the first sync it returns `false`, so `weatherReady()` stays false and the "Ucitavam temperaturu..." screen stays up.

### 3. Finding "now" in the data

The current local time is formatted exactly like the stored keys:

```cpp
strftime(key, sizeof(key), "%Y-%m-%dT%H", &t);   // "2026-10-22T14"
```

and looked up in the entries. The window is that entry plus the next 11 (`HOURS_SHOWN = 12`). Comparing full date + hour strings means crossing midnight needs no special handling: `23h` is followed by the next day's `00h`.

`weatherReady()` is true only if the current hour is found **and** at least 12 entries start there.

### 4. When data is fetched, and the cache

**At most one API call per clock hour.** The forecast only changes meaningfully from hour to hour, so `weatherLoop()` fetches only when the hour has moved on since the last successful fetch:

| Situation | What happens |
|---|---|
| Clock not set yet (NTP) | Nothing. The hour decides everything, so it waits |
| Cache is from the current hour (`fetchedHour == now`) | Nothing. No API call |
| Cache is from an earlier hour, or there is none | **One fetch** |
| That fetch failed, but the cache still covers the next 12 hours | Nothing until the next hour. The screen keeps working from the cache |
| That fetch failed and the cache **can't** fill the screen (e.g. brand-new board, or offline for many hours) | Retry every 5 min (`RETRY_MS`) until one works |

The hour is compared as the same `"YYYY-MM-DDTHH"` string used for the entries. So `14h → 15h` and `23h → 00h` (next day) both count as a new hour.

**The cache is kept in flash**, so it survives reboots and power cuts. After every successful fetch, `saveCache()` writes it to NVS (the `Preferences` storage the Wi-Fi list also uses) under the name `"weather"`:

| Key | Contents |
|---|---|
| `v` | `CACHE_VERSION` (1) |
| `hour` | hour of the fetch, e.g. `"2026-10-22T14"` |
| `n` | number of stored hours |
| `entries` | the `Entry` array as raw bytes |

- `loadCache()` reads it once, on the first `weatherLoop()` call after boot. It accepts the data only if the version matches and the size is exactly `n` entries, so a cache written by an older firmware with a different `Entry` layout is ignored. **Bump `CACHE_VERSION` whenever `Entry` changes.**
- After a reboot within the same hour, the cache is used and **no API call** is made. The screen appears as soon as the board knows the time, a few seconds after Wi-Fi connects.
- Reboot after a different hour started: one fetch, as usual.
- **Flash wear:** at most one write of about 1.2 KB per hour (~8,800 per year). NVS spreads writes over its flash pages, so this is well within the flash's lifetime.

**Why 48 hours are cached, not only the 12 on screen.** The screen shows the current hour + 11, and that window moves every hour. If a fetch fails, the window can still move forward inside the stored 48 hours. With only 12 cached hours, a single failed fetch would leave the 12th hour missing and the weather screen would disappear. It costs 1.2 KB of flash instead of 300 bytes.

The attempt hour (`attemptedHour`) is kept in RAM only. So a reboot right after a *failed* fetch allows one more attempt in that hour; after a *successful* fetch, the saved `hour` prevents any extra call.

Errors are logged to serial:

```
[weather] cache: 48 hours, fetched at 2026-10-22T14   ← loaded from flash at boot
[weather] fetched 48 hours
[weather] HTTP -1          ← connection failed / timed out
[weather] JSON error: ...  ← unexpected reply
```

**Blocking.** A fetch runs directly in `loop()` and takes roughly 1–2 s (DNS lookup, TLS handshake, request), with a 5 s timeout (`HTTP_TIMEOUT_MS`). During that time scrolling pauses and the button isn't read. This happens at most once per hour, so it was kept simple instead of moving the fetch to a separate FreeRTOS task.

### 5. Building the hour and temperature rows

Each hour becomes one **7-character cell**: a space, the value padded to 5 characters, and `|`:

```
" 09h  |"
" 21°C |"
" -5°C |"
"-12°C |"
" --   |"   ← temperature missing in the reply
```

`cell(value, visibleLen)` does the padding. It counts **visible characters**, not bytes, because `°` takes 2 bytes in UTF-8 but is one character on screen. Padding by bytes would make the temperature row one character shorter per cell and break the alignment.

**Why the hour and temperature rows stay aligned while scrolling.** Both rows always have the same number of characters: (1 empty + 12) × 7 = 91. `showWeather()` works out a line's scroll position from its width and the current `millis()` (`marqueeOffset()`), so two lines of the same width always get the same position. They move in step, with each temperature under its hour.

### 6. The inverted current-hour block

The SSD1306 is monochrome (each pixel is on or off), so the current hour is marked by drawing its block **inverted**: lit background, dark text and icon.

- `weatherRows()` starts all rows with one empty cell (`cell("", 0)` → `"      |"`). The current hour is therefore the second cell. `weather.h` exposes the cell width as `WEATHER_CELL_CHARS = 7` (42 px).
- `showWeather()` first draws everything normally, then flips the block:
  ```cpp
  display.fillRect(x + cellPx - 3, 8, cellPx - 1, SCREEN_HEIGHT - 8, SSD1306_INVERSE);
  display.drawFastVLine(x + cellPx - 4, 8, SCREEN_HEIGHT - 8, SSD1306_WHITE);
  display.drawFastVLine(x + 2 * cellPx - 4, 8, SCREEN_HEIGHT - 8, SSD1306_WHITE);
  ```
  - `SSD1306_INVERSE` flips every pixel in the rectangle, so text and icons that were lit become dark on a lit background. No second drawing pass is needed.
  - **x:** the font draws `|` in pixel column 2 of its 6 px cell. So the separators around the second cell are at `x + 38` and `x + 80`, and the rectangle covers everything between them (41 px). `x` is the current scroll position, so the block moves with the rows.
  - **Edges:** the font's `|` is dashed, which made the block's sides look ragged. Two solid vertical lines on the separators make it a clean 43 px wide block.
  - **y:** rows 8–31, from just under the title to the bottom of the screen.

**Why inverted and not a frame:** an earlier version framed the current hour with a 2 px padding. With a third row for icons there's no room left on a 32 px tall screen. The inverted block takes no extra space, so the screen keeps a plain 8 px line grid:

| Pixel rows | Content |
|---|---|
| 0–6 | title |
| 7 | gap |
| 8 | top of the inverted block |
| 9–15 | hours |
| 17–23 | temperatures |
| 25–31 | icons |

### 7. Weather icons

**Weather codes → icons.** Open-Meteo describes the weather with standard **WMO weather codes**. `iconFor()` in `weather.cpp` groups them into 10 icons. Clear and partly cloudy use `is_day` to pick a sun or moon variant:

| WMO code | Meaning | Icon |
|---|---|---|
| 0 | Clear sky | Sun (day) / Moon (night) |
| 1, 2 | Mainly clear, partly cloudy | PartlySun / PartlyMoon |
| 3 | Overcast | Cloud |
| 45, 48 | Fog | Fog |
| 51–57 | Drizzle (incl. freezing) | Drizzle |
| 61–67, 80–82 | Rain, rain showers (incl. freezing) | Rain |
| 71–77, 85, 86 | Snow, snow showers | Snow |
| 95, 96, 99 | Thunderstorm (with/without hail) | Storm (lightning bolt) |
| anything else / missing | | no icon |

`weatherIcons()` returns one icon per cell of `weatherRows()`, starting with `None` for the empty first cell, so `icons[i]` always belongs to cell `i`.

**The icons** (`src/icons.h`), 7×7 px, `#` = lit:

```
Sun       Moon      PartlySun PartlyMoon Cloud
...#...   ..###..   .#.....   .##....   .......
.#...#.   .##....   ###....   #......   ..##...
..###..   ##.....   .#.##..   #..##..   .####..
#.###.#   ##.....   ..####.   #.####.   .#####.
..###..   ##.....   .######   .######   #######
.#...#.   .##....   #######   #######   #######
...#...   ..###..   .......   .......   .......

Fog       Drizzle   Rain      Snow      Storm
.......   ..##...   ..##...   ...#...   ....#..
#######   .####..   .####..   .#.#.#.   ...##..
.......   #######   #######   ..###..   ..##...
.######   #######   #######   #######   .#####.
.......   .......   .......   ..###..   ...##..
#####..   .#...#.   #.#.#.#   .#.#.#.   ..##...
.......   ...#...   .#.#.#.   ...#...   ..#....
```

Each icon is stored as 7 bytes, one per row, with bit 6 = the leftmost pixel. The order of `ICONS[]` follows the `WeatherIcon` enum, and `ICONS[int(icon) - 1]` picks one (`None` has no entry). To change an icon, edit its row bytes in `icons.h`.

**Drawing.** `drawIcon()` lights the pixels one by one. Each icon is placed 14 px into its cell, which centres it between the cell's two `|` separators. The separators are continued through the icon row by drawing a line of `"      |"` cells under the temperatures, so the grid has unbroken columns.

**Text is drawn with a transparent background.** `drawText()` calls `drawChar()` with the same color for foreground and background, which the Adafruit library treats as "don't paint the background". With a black background, every character (spaces included) paints a black 6×8 box. The separator line drawn after the icons then erased all of them, and the first version showed no icons at all. The screen is cleared before each frame, so the background boxes aren't needed anywhere.

**Redraws.** `showWeather()` builds its "did anything change" key from the title, both text rows, the scroll positions **and the icon list**. The key is shared with `showStatus()` (`lastFrameKey`), so switching between the weather screen and any other screen always redraws.

### 8. The `°` character

The built-in Adafruit font is ASCII only, and unknown characters show as `?` (see "Serbian letters" in [wifi-connection.md](wifi-connection.md)). The font table itself, though, is the old IBM PC character set (CP437), which has a degree sign at position `0xF8`. `baseGlyph()` in `main.cpp` maps it:

```cpp
case 0xB0: return char(0xF8);   // ° (U+00B0) -> CP437 degree glyph
```

**`display.cp437(true)` is required** (called in `setup()`). The font table was once missing one character, and for compatibility with old code the library still shifts every character from position 176 up by one unless CP437 mode is on. That shift happens even in `drawChar()`. Without it, `0xF8` draws the next glyph, `0xF9`, which is a centered dot (`·`) that looks like a multiplication sign. With CP437 mode on, `0xF8` is the real degree sign: a small 4×4 ring at the top of the character cell.

---

## Settings (`src/weather.cpp`)

| Constant | Default | Meaning |
|---|---|---|
| `WEATHER_URL` | Belgrade, `forecast_days=2` | Change `latitude`/`longitude` for another city (and `timezone` + `TZ_BELGRADE` if it's in another time zone) |
| `TZ_BELGRADE` | `CET-1CEST,M3.5.0,M10.5.0/3` | Time-zone rule for the clock |
| `HOURS_SHOWN` | 12 | Number of hours on the screen. If you raise it above 24, raise `forecast_days` too |
| `RETRY_MS` | 5 min | Retry interval after a failed fetch, used **only** when the cache can't fill the screen |
| `CACHE_VERSION` | 1 | Bump whenever `Entry` changes, so an old cache in flash is ignored |
| `HTTP_TIMEOUT_MS` | 5 s | Connect and read timeout |
| `MAX_ENTRIES` | 48 | How many hours of the reply are stored |

The scroll speed is set in `marqueeOffset()` in `main.cpp` (`msPerPx = 28`). The 4 s pause before the weather rows start scrolling is the `4000` in `scrollX(hours, 4000)` inside `showWeather()`; other screens use 1 s.

---

## Testing checklist

1. Connect to a saved network. The serial log shows `[weather] fetched 48 hours`. Within a few seconds the OLED switches from "Ucitavam temperaturu..." to the weather screen.
2. Reboot the board within the same hour. The log shows `[weather] cache: 48 hours, fetched at …` and **no** `fetched` line. The weather screen comes back as soon as the time is synced.
3. Row 1 shows the current hour. The hour row starts at the current hour and lists 12 hours, continuing past midnight in the evening (e.g. `20h … 23h | 00h … 07h`).
4. Compare the numbers and icons with the API URL above opened in a browser (`weather_code` → icon table above).
5. While still: an empty block, then the inverted current hour (with its icon) in the middle, then the next hour. Watch a full scroll: the rows, icons and inverted block move together, the `°` is a small raised ring, and every `|` lines up.
6. Unplug the router: the Wi-Fi screens appear. After reconnecting, the weather screen returns right away with the stored data.
7. Leave it running across an hour change: the title and the first column move to the new hour, and the log shows exactly one `[weather] fetched 48 hours` for the new hour.
8. With the cache filled, block internet access on the router for an hour change: no retries in the log; the screen keeps showing the forecast, shifted by one hour.

## Known limitations

- The server certificate isn't checked (see above).
- Scrolling pauses for 1–2 s during each fetch, at most once per hour.
- The weather screen needs the internet time (NTP) to know the hour, so even with a cache it only appears once Wi-Fi is connected and the time is synced.
- Temperatures are rounded to whole degrees.
