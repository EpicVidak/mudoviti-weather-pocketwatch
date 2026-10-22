# Mudoviti pokazivač vremena

A small weather display: an **ESP32-C3 Super Mini** with a **0.91" 128×32 SSD1306 OLED**. It connects to Wi-Fi and shows the **next 12 hours of Belgrade weather** from [Open-Meteo](https://open-meteo.com): hour, temperature and a pixel icon for each, scrolling sideways. It has no buttons except the board's own BOOT button, which opens a Wi-Fi setup hotspot. Saved networks can be managed from a phone.

The on-screen and web-page texts are deliberately silly Serbian ("Trazimo muda...", "Pegazovo jaje", "Nameštanje muda"…). **They are the owner's choice; don't "fix" or translate them.**

> **For agents:** this README is the entry point. Read it fully, then the detailed docs in [`docs/`](docs/) for whatever you're changing. Keep this file and the docs up to date when behavior changes. The repo is `github.com/EpicVidak/mudoviti-pokazivac-vremena`.

---

## Status (last updated 2026-10-22)

**Done, and all of it builds:**
1. **Wi-Fi layer.** Up to 20 saved networks; the board connects to the strongest one in range.
   - Hold BOOT 3 s to open a setup hotspot (customized WiFiManager).
   - Saved networks can be managed at `http://ova-muda.local`.
   - Details: [docs/wifi-connection.md](docs/wifi-connection.md).
2. **Weather screen.** Shows 12 hours of temperature and weather icons, with the current hour highlighted.
   - The forecast is cached in flash, with at most 1 API call per hour.
   - Details: [docs/weather.md](docs/weather.md).

**Not done / ideas:**
- A short press of BOOT is unused. A pause/restart-scrolling feature was tried and reverted (see [Lessons learned](#lessons-learned--gotchas)).
- No over-the-air (OTA) updates (see the partition note below).
- Uppercase Serbian letters with accents (Č Ć Š Ž Đ) can't be drawn.

[docs/handoff-setup.md](docs/handoff-setup.md) is the **original** handoff from before any features existed. Its wiring and board quirks are still valid. Its description of `main.cpp` and `platformio.ini` is **outdated**.

---

## Hardware

| Part | Notes |
|---|---|
| ESP32-C3 Super Mini | 2.4 GHz Wi-Fi only. No USB-UART chip: Serial goes over the chip's native USB |
| 0.91" SSD1306 OLED, 128×32, I2C | Address `0x3C` (some modules use `0x3D`). Pins labeled GND, VCC, SCK (= SCL), SDA |

| OLED pin | Super Mini pin |
|---|---|
| GND | GND |
| VCC | 3.3 |
| SDA | GPIO5 |
| SCK | GPIO6 |

| Board feature | Pin | Use |
|---|---|---|
| BOOT button | GPIO9 | The only input. Hold 3 s: open/close the setup hotspot |

- GPIO8 and GPIO9 are avoided for I2C on purpose: GPIO8 is the onboard LED, GPIO9 is the BOOT button, and both are pins the chip reads at power-up to decide how to start.
- Wire the OLED by its printed labels. The pin order differs between modules.

---

## Setting up on a fresh machine

1. **Install VS Code** and the **PlatformIO IDE** extension.
   - Without VS Code, PlatformIO Core works on its own: `pip install platformio`.
   - On Windows the CLI ends up at `%USERPROFILE%\.platformio\penv\Scripts\pio.exe`. That path is useful when `pio` isn't on the PATH.
2. **Clone the repository:**
   ```sh
   git clone https://github.com/EpicVidak/mudoviti-pokazivac-vremena.git
   ```
3. **Open the folder in VS Code.** PlatformIO downloads the toolchain and libraries on the first build (several minutes).
4. **Build, flash and watch the log:**
   ```sh
   pio run                 # build
   pio run -t upload       # build + flash over USB-C
   pio device monitor      # serial log, 115200 baud
   ```
   Or use the ✓ / → / 🔌 buttons in PlatformIO's bottom toolbar.
5. **If the upload fails or no COM port appears:** hold BOOT while plugging in USB (or hold BOOT and tap RESET), upload, then press RESET. The COM port can change after a reset, so reopen the monitor if it goes quiet. Windows 10/11 needs no extra USB driver.
6. **First run:**
   1. The board has no saved networks, so the OLED shows "Nema muda :(".
   2. Hold BOOT for 3 s and release.
   3. Join the Wi-Fi **"Pegazovo jaje"** on your phone.
   4. Pick your network on the page that opens (or open `http://192.168.4.1`).
   5. Once connected, the weather appears within a few seconds.

No API keys or secrets are needed. Open-Meteo is free and keyless, and Wi-Fi passwords are only entered on the device.

### What's in `platformio.ini`, and why

| Setting | Why |
|---|---|
| `board = esp32-c3-devkitm-1` | PlatformIO has no Super Mini board definition. This board has the same chip and flash |
| `ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=1` | Required, or `Serial` prints nothing over USB |
| `board_build.partitions = huge_app.csv` | The firmware (~1.5 MB) doesn't fit the default 1.25 MB app space. The cost is no OTA updates |
| `-I include` + `-D WM_STRINGS_FILE=\"wm_strings_custom.h\"` | WiFiManager's page labels come from our copy of its text file |
| `lib_deps` | Adafruit SSD1306 (2.5.x) + GFX (1.13.x), `tzapu/WiFiManager@^2.0.17`, `bblanchon/ArduinoJson@^7` |

The build was last verified with platform espressif32 8.0.0 (Arduino core 3.3.12), at about 48% of the 3 MB app space.

---

## How it works (overview)

The detailed explanations are in the docs; this is the map.

```
src/
├── main.cpp           setup()/loop(), all drawing: text renderer, scrolling, status screens, weather screen
├── button.h/.cpp      debounced BOOT button: shortPress() / longPress() (≥3 s, on release) / heldMs()
├── wifi_store.h/.cpp  saved networks (max 20) in flash (Preferences namespace "wifi")
├── wifi_conn.h/.cpp   non-blocking Wi-Fi stages + WiFiManager hotspot + custom "saved networks" page
├── web_admin.h/.cpp   http://ova-muda.local page (list/add/delete networks) while connected
├── html_util.h        htmlEscape(), shared by both web pages
├── weather.h/.cpp     Open-Meteo fetch, NTP clock, 48 h cache in flash, rows + icons for the screen
└── icons.h            ten 7×7 weather icon bitmaps
include/
└── wm_strings_custom.h  copy of WiFiManager's English text file with our button labels
docs/
├── wifi-connection.md   Wi-Fi layer in full detail
├── weather.md           weather screen in full detail
└── handoff-setup.md     original pre-project handoff (partly outdated)
```

### Main loop
`loop()` runs about every 10 ms, and **nothing in it may block**, except the weather fetch, which takes about 1–2 s at most once per hour. Each pass:
1. Read the button. A long press opens or closes the hotspot.
2. `wifiLoop()`
3. `weatherLoop()`
4. Start or stop the management page depending on the connection, and answer its requests.
5. `updateDisplay()`

### Wi-Fi ([docs/wifi-connection.md](docs/wifi-connection.md))
- **Connection stages:** a small state machine (Scanning → Connecting → Connected, Backoff, Portal) using a background scan. Each candidate network gets 15 s, and the board retries 10 s after nothing works.
- **Hotspot:** WiFiManager in non-blocking mode, with these changes:
  - the whole menu is custom HTML;
  - an extra `/saved` page for deleting networks;
  - our own 5-minute timeout, because the library's restarts on every page load;
  - labels replaced through `WM_STRINGS_FILE`.
- **Saving networks:** a network added through the hotspot is saved only after it actually connects.
- **Address:** `ova-muda.local` (mDNS) works both on the home network and inside the hotspot.
- **Transmit power** is lowered to 8.5 dBm, a common stability fix for the Super Mini's antenna.

### Weather ([docs/weather.md](docs/weather.md))
- **Request:** `hourly=temperature_2m,weather_code,is_day`, `timezone=Europe/Belgrade`, `forecast_days=2`. HTTPS without checking the server's certificate (`setInsecure()`).
- **Clock:** internet time with Belgrade time-zone rules (`configTzTime`), switching summer/winter time automatically.
- **Fetch rule:** fetch only when the clock hour differs from the hour of the last successful fetch. If a fetch fails, retry every 5 min **only if** the cache can't fill the 12-hour window.
- **Cache:** all 48 hours are saved in flash (Preferences namespace `"weather"`) after each fetch, so a reboot within the same hour makes no API call. Bump `CACHE_VERSION` if the stored entry layout changes.
- **Screen:**

  | Pixel rows | Content |
  |---|---|
  | 0–6 | title |
  | 9–15 | hours |
  | 17–23 | temperatures |
  | 25–31 | icons |

  - Each hour is a 7-character cell.
  - The rows start with one empty cell, so the current hour sits in the middle and is drawn **inverted**.
  - All rows have the same width, so they scroll together: 4 s pause, 28 ms per pixel.
- **Icons:** weather codes are grouped into 10 icons (Sun, Moon, PartlySun, PartlyMoon, Cloud, Fog, Drizzle, Rain, Snow, Storm). Clear and partly cloudy hours use a moon variant at night.

### Display / text rendering (`main.cpp`)
- **Font:** Adafruit's built-in 6×8 font, which is ASCII only. `drawText()` reads UTF-8:
  - `č ć š ž đ` are drawn as the plain letter plus accent pixels;
  - `°` maps to the font's degree glyph `0xF8`;
  - anything else shows as `?`.
- **Scrolling:** lines wider than 21 characters scroll sideways. The position is calculated from `millis()` (`marqueeOffset()`).
- **Redraws:** `showStatus()` (general text screens) and `showWeather()` share one "did anything change" key (`lastFrameKey`), so the screen only redraws when something visible changed.

---

## Lessons learned / gotchas

These cost time once. Don't repeat them.

| Problem | Cause / fix |
|---|---|
| `°` showed as a centered dot | Adafruit GFX shifts every character from 176 up by one unless `display.cp437(true)` is set (also in `drawChar`). It's set in `setup()` |
| Icons didn't show at all | `drawChar()` with a black background paints a black box for every character, spaces included, and erased the icons. Text is now drawn with background = foreground, i.e. transparent |
| Build "program size too big" | The default partition table is too small. `huge_app.csv` is used |
| JSON parse errors (potential) | Open-Meteo sends chunked replies. Use `http.getString()`, not the raw stream |
| Hotspot stayed open longer than set | WiFiManager's timeout restarts on every page load. We time it ourselves |
| `ova.muda` address impossible | A device can only announce `.local` names itself, so it's `ova-muda.local` |
| Scrolling "twitched" | A version that advanced its own scroll clock once per loop (for pause/resume) looked jittery and was **reverted**. Keep scrolling based on `millis()` at draw time |
| Wrong partition for the WiFiManager "Update" button | Firmware upload can't work without OTA partitions, so the button is hidden |
| Serbian/UTF-8 text alignment | Pad cells by **visible characters**, not bytes (`°` is 2 bytes) |

Hardware facts:
- The SSD1306 is **monochrome**, so there are no colors. Use inversion or frames for emphasis.
- 128×32 at the 6×8 font is 21 characters × 4 lines.
- Holding BOOT while powering up puts the board into flashing mode instead of running the program.

---

## Working on this project (for agents)

- **Verify by building:** run `pio run`. The owner tests on real hardware and reports back, and there is no automated test suite. Say clearly when something was only compiled, not seen on the device.
- **Ask before changing the UI:** layout and design choices belong to the owner. Ask first and offer a recommended option.
- **Keep the user-chosen texts and names as they are:** the UI strings, `Pegazovo jaje`, `ova-muda.local`.
- **Update the docs:** after a change, update [docs/wifi-connection.md](docs/wifi-connection.md) or [docs/weather.md](docs/weather.md), and this README.
- **Match the code style:** small modules with C-style function interfaces, `static` file-local state, `#define` settings at the top of each `.cpp`, and serial logs prefixed with `[wifi]`, `[store]`, `[web]` or `[weather]`.
