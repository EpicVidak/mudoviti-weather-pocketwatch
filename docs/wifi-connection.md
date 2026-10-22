# oled-esp32

Firmware for an **ESP32-C3 Super Mini** with a **128x32 SSD1306 OLED** (I2C). This first stage handles the Wi-Fi connection:

- The board keeps a list of saved Wi-Fi networks and connects to the strongest one in range.
- Holding the onboard **BOOT** button for 3 s opens a setup hotspot (**"Pegazovo jaje"**), where you can add a new network from your phone.
- While the board is on Wi-Fi, saved networks can be viewed, added and deleted at **http://ova-muda.local**.
- The OLED always shows what the board is doing.

The next stage (fetching weather from an online API and drawing it) will build on this. See [Extending: the weather screen](#extending-the-weather-screen).

---

## Hardware

| Part | Pin / detail |
|---|---|
| OLED SDA | GPIO5 |
| OLED SCL | GPIO6 |
| OLED I2C address | `0x3C` |
| Button | Onboard **BOOT** button, GPIO9 (pressed reads LOW, internal pull-up) |

> ⚠️ Don't hold BOOT while plugging the board in or pressing reset, or the board starts in firmware-flashing mode instead of running the program. Holding it while the program is running is fine.

The ESP32-C3 only supports **2.4 GHz** Wi-Fi, so the router needs its 2.4 GHz band turned on.

---

## Building and flashing

This is a PlatformIO project ([platformio.ini](../platformio.ini)).

```sh
pio run -t upload      # build + flash
pio device monitor     # serial log at 115200 baud
```

Libraries (downloaded automatically):

| Library | Purpose |
|---|---|
| `adafruit/Adafruit SSD1306`, `adafruit/Adafruit GFX Library` | Drawing on the OLED |
| `tzapu/WiFiManager@^2.0.17` | The setup hotspot and its page |
| Built into the Arduino core: `WiFi`, `WebServer`, `ESPmDNS`, `Preferences` | Wi-Fi, the management page, the `.local` name, and storage in flash |

**Partition table.** Wi-Fi, WiFiManager and the web server together are about 1.31 MB, just over the default 1.25 MB app space. So `platformio.ini` sets:

```ini
board_build.partitions = huge_app.csv
```

That gives a 3 MB app space, of which the current build uses about 42%. The cost is **no over-the-air updates**. If OTA is needed later, switch to a custom partition table with two app slots of at least 1.5 MB each.

---

## How to use it

### First start
On a fresh board the saved list is empty. The OLED shows:

```
Nema muda :(

Stisni me za mudo
```

### Adding a network with the hotspot
1. **Hold BOOT.** The screen says *"Nastavi da stišćes..."* and a progress bar fills over 3 seconds. Then it says *"AU AU AU PUSTI / BOLI ME :("*.
2. **Release.** The board opens a hotspot called **Pegazovo jaje** and shows:
   ```
   Spremite muda moliću!

   http://ova-muda.local
   ```
   The first line is wider than the screen, so it scrolls sideways.
3. On your phone, join *Pegazovo jaje*. The setup page usually opens by itself. If not, open `http://ova-muda.local` or `http://192.168.4.1`.
4. The menu has three buttons:
   - **Dodati novo mudo** (add a network): pick your network and enter the password.
   - **Brisanje postojecih muda** (delete saved networks): the saved list, with a **Delete** button for each network.
   - **Odustajem :(** (exit): close the hotspot.
5. After **Dodati novo mudo**, the board tries that network. It is **saved only if the connection works**, so a typo'd password isn't saved.

The hotspot closes when:
- a network was added successfully,
- you **hold BOOT for 3 s** again (same progress bar and screens as when opening),
- you press **Odustajem :(** on the page, or
- **5 minutes** pass. The 5 minutes count from when the hotspot opened; using the page doesn't extend them.

> The grey **"Not connected to &lt;network&gt;"** box at the bottom of the setup pages is WiFiManager's status line. It shows the last network the board was on, which it leaves while the hotspot is open. It isn't an error.

The board then goes back to normal connecting.

You can open the hotspot at any time, including while connected, to add another network.

### Managing networks in the browser
While the board is on Wi-Fi, open **http://ova-muda.local** from any device on the same network. If `.local` names don't work on your phone (some older Android devices), use the IP address shown on the OLED.

The page shows:
- which network the board is on, its signal strength and its IP address,
- the saved networks (the current one is marked) with a **Delete** button for each,
- an **Add network** form (SSID + password). This saves straight to the list without checking the password first.

Deleting the network currently in use doesn't disconnect right away. The board stays on it until the connection drops, then picks another saved network.

### What the OLED shows

| Situation | Screen |
|---|---|
| Starting | `Starting...` |
| Scanning | `Trazimo muda...` |
| Trying a network | `Kuvam:` / `<ssid>` |
| Connected, weather not loaded yet | `Mudo: <ssid>` / `<IP address>` / `Ucitavam temperaturu...` |
| Connected, weather loaded | Hourly temperatures, see [weather.md](weather.md) |
| Nothing found / no saved networks | `Nema muda :(` / `Stisni me za mudo` |
| BOOT being held | `Nastavi da stišćes...` + progress bar, then `AU AU AU PUSTI` / `BOLI ME :(` (same when opening or closing the hotspot) |
| Hotspot open | `Spremite muda moliću!` / `http://ova-muda.local` |

---

## How it works

### File overview

```
src/
├── main.cpp         setup/loop, OLED status screen, wires the modules together
├── button.h/.cpp    debounced BOOT button: short press / long press / held time
├── wifi_store.h/.cpp  saved-network list in flash (Preferences / NVS)
├── wifi_conn.h/.cpp   non-blocking connection logic + WiFiManager hotspot
├── web_admin.h/.cpp   management web page + ova-muda.local name
└── html_util.h        htmlEscape(), shared by both web pages
```

Each module has a small set of functions and keeps its internal data private to its `.cpp`. `main.cpp` is the only file that knows about all of them.

### The main loop (`main.cpp`)

`loop()` runs about every 10 ms, and **no step in it is allowed to block**:

1. `button.update()`: read and debounce the button.
2. Handle button events. A long press toggles the hotspot:
   - **Hotspot open:** close it (`wifiStopPortal()`).
   - **Otherwise:** stop the web page first, then open the hotspot (`wifiStartPortal()`). The web page has to stop because the hotspot's own web server needs port 80.
   - A short press does nothing yet.
3. `wifiLoop()`: move the connection logic forward one step.
4. Start the web page when connected (`webAdminBegin()`) and stop it otherwise (`webAdminEnd()`). Both are safe to call every loop. Then `webAdminLoop()` answers any browser requests.
5. `updateDisplay()`: work out what the screen should show.

`showStatus()` builds a text key from the four lines, their scroll positions and the progress-bar value. It only redraws when that key differs from the last frame drawn (`lastFrameKey`, shared with the weather screen's `showWeather()`), so the screen isn't redrawn for nothing.

**Scrolling lines.** At text size 1, a character is 6 px wide, so 21 fit on the 128 px screen. A longer line is drawn with wrapping turned off, starting at a negative x position, so it slides sideways:
1. it pauses at the start: 1 s (the weather rows use 4 s, see [weather.md](weather.md)),
2. scrolls 1 px every 28 ms until its end is visible,
3. pauses 1 s at the end, then starts over.

**Serbian letters.** The built-in Adafruit font only has basic ASCII characters. `drawText()` reads the text as UTF-8. For **č ć š ž đ**, it draws the plain letter (`c s z d`) and adds the accent with a few pixels in the two empty rows above it:
- **č š ž:** a háček (ˇ)
- **ć:** an acute accent (´)
- **đ:** a short bar across the letter

Any other non-ASCII character shows as `?`. Uppercase letters with accents (Č, Ć, Š, Ž, Đ) aren't supported, because capitals fill the whole character cell and leave no room above them.

`marqueeOffset()` works out the current position from `millis()`. The position is part of the redraw key, so the screen redraws only while text is actually moving.

### Button (`button.cpp`)

- GPIO9 with `INPUT_PULLUP`. Pressed reads `LOW`.
- A change only counts after the reading has been steady for **30 ms**, which filters out switch bounce.
- Events fire **on release**:
  - held < 3000 ms → `shortPress()`
  - held ≥ 3000 ms → `longPress()`
- Each event can be read once; reading it clears it.
- `heldMs()` returns how long the button has been held so far (0 if not pressed), for the progress bar.

Firing on release means one hold can never trigger twice, and it lets the screen show "Release to add" as feedback.

### Saved-network list (`wifi_store.cpp`)

The list lives in RAM (two arrays of `String`, up to `MAX_NETWORKS = 20`). It is copied to flash with `Preferences` under the name `"wifi"`:

| Key | Contents |
|---|---|
| `n` | number of saved networks |
| `s0` … `s19` | SSIDs |
| `p0` … `p19` | passwords |

- `load()` runs once at boot.
- Every `add()` / `remove()` clears the stored list and rewrites it in full. The list is tiny and rarely changes, so this keeps the stored data consistent at almost no cost.
- `add()` with an SSID that's already saved only updates its password. When the list is full, the oldest entry (index 0) is dropped.
- `remove()` shifts the remaining entries up to close the gap.

Passwords are stored **unencrypted** in flash, and they are never sent to the web page.

### Connection logic (`wifi_conn.cpp`)

The connection code moves through a few stages, checked once per loop (`wifiLoop()`). The current stage is available from `wifiState()`.

```
          ┌────────────────────────────── connection lost ─────────────────────────────┐
          ▼                                                                            │
     ┌──────────┐ scan done ┌────────────┐  connected ┌───────────┐                    │
 ┌──►│ Scanning │──────────►│ Connecting │───────────►│ Connected │────────────────────┘
 │   └──────────┘           └────────────┘            └───────────┘
 │        │ no saved             │ 15 s timeout → next candidate;
 │        │ network in range     │ none left ↓
 │        ▼                      ▼
 │   ┌─────────────────────────────────┐
 └───│ Backoff (wait 10 s, then rescan)│   (stays here while the saved list is empty)
     └─────────────────────────────────┘

 Portal: entered from any stage on a long press; the other stages pause until it ends.
```

| Stage | What happens |
|---|---|
| **Scanning** | `WiFi.scanNetworks(true)` starts a background scan, and `WiFi.scanComplete()` is checked each loop. When it finishes, `collectCandidates()` keeps only networks in the saved list, merges repeats of the same name (e.g. two access points with one SSID) by keeping the strongest signal, and sorts them strongest first. A failed scan goes to Backoff. |
| **Connecting** | `WiFi.begin(ssid, pass)` for the current candidate, then check `WiFi.status()`. On `WL_CONNECTED` → Connected. After 15 s with no connection, try the next candidate; when none are left, go to Backoff. |
| **Connected** | Only checks `WiFi.status()`. When the connection drops it scans again, so the board can switch to whichever saved network is now strongest. |
| **Backoff** | Waits 10 s, then scans again, but only if at least one network is saved. |
| **Portal** | See below. |

Candidates are **copied** (SSID + password) when the scan finishes. So deleting a network on the web page mid-connect can't change data the connection code is still using.

Wi-Fi settings made in `wifiBegin()`:
- `WiFi.persistent(false)`: the Wi-Fi driver doesn't keep its own copy of credentials, so our list is the only one.
- `WiFi.setAutoReconnect(false)`: this code handles reconnecting, so the driver doesn't fight it.
- `WiFi.setHostname("ova-muda")`: the name shown in the router's device list.
- `WiFi.setTxPower(WIFI_POWER_8_5dBm)`, controlled by `REDUCE_TX_POWER`: many Super Mini boards have a poorly matched antenna and drop connections at full power. Lowering the power is a common fix. Set the define to `0` to use full power.

#### The setup hotspot (WiFiManager)

WiFiManager normally takes over the program until setup is finished. Here it runs in **non-blocking mode**, so the loop, button and display keep working:

```cpp
wm.setConfigPortalBlocking(false);
wm.setConfigPortalTimeout(0);            // no library timeout, see below
...
wm.startConfigPortal("Pegazovo jaje");   // returns immediately
...
wm.process();                            // called every loop while the hotspot is open
```

- **Start** (`wifiStartPortal()`): discard any scan results, disconnect, switch to the Portal stage, open the hotspot. WiFiManager runs its own web server and a DNS redirect so phones pop up the setup page by themselves.
- **Success:** `wm.process()` returns `true` only after the board actually connected with the entered details. The SSID and password are then read with `WiFi.SSID()` / `WiFi.psk()` and saved with `wifiStore::add()`. After that the hotspot shuts down and the board goes to Connected (or scans if the link has already dropped).
- **Exit button:** WiFiManager closes the hotspot itself, so `getConfigPortalActive()` becomes false. The board turns off the hotspot (`WiFi.softAPdisconnect`), goes back to `WIFI_STA` mode, sets the transmit power again and scans.
- **Timeout (5 min) or a 3 s hold:** `wifiStopPortal()` runs `wm.stopConfigPortal()`, then the same cleanup.

**Why the timeout is ours:** WiFiManager's timeout restarts every time the phone loads a page, and phones re-check captive portals constantly. So the hotspot could stay open much longer than set. Instead, the library timeout is disabled (`0`) and `wifiLoop()` closes the hotspot after `PORTAL_TIMEOUT_S` seconds, counted from when it opened. `wifiPortalSecondsLeft()` returns the time left, measured from the same start, for showing a countdown on screen. The current hotspot screen doesn't show one.

#### Customizing the hotspot pages

WiFiManager's default menu (Configure WiFi / Info / Exit / Update) is replaced in `wifiBegin()`:

```cpp
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
```

- `"custom"` is the spot in the menu where the HTML from `setCustomMenuHTML()` goes. WiFiManager's built-in button labels are fixed English text in the library, so the **whole menu** is our own HTML. The buttons simply link to WiFiManager's existing pages (`/wifi`, `/exit`) and to our `/saved` page.
- `setTitle()` sets the big heading on the start page. `setCustomHeadElement()` adds a small piece of CSS that centers it (`h1`) and the hotspot name under it (`h3`). The heading of the `/saved` page is centered the same way.
- WiFiManager's pages already declare UTF-8, so `š` in the title shows correctly.
- **Labels on WiFiManager's own pages** (e.g. the network list page) are fixed text in the library's `wm_strings_en.h`. The library lets you swap in your own copy of that file at build time, so [include/wm_strings_custom.h](../include/wm_strings_custom.h) is a full copy of it, and `platformio.ini` points to it:
  ```ini
  build_flags =
      -I include
      -D WM_STRINGS_FILE=\"wm_strings_custom.h\"
  ```
  Changed so far:
  | String | Original | Ours |
  |---|---|---|
  | `HTTP_FORM_END` | Save | Sačuvaj mudo |
  | `HTTP_SCAN_LINK` | Refresh | Osvežiti Muđu |

  To rename anything else on WiFiManager's pages, edit the matching string in that file. If WiFiManager is upgraded, compare the copy with the new `wm_strings_en.h`, because new versions may add strings the copy doesn't have.
- **Info** and **Update** are removed. Update (firmware upload) can't work anyway, because `huge_app.csv` leaves no room for a second copy of the firmware.
- `setWebServerCallback(addPortalRoutes)` runs each time WiFiManager creates the hotspot's web server. It adds two pages to `wm.server`:
  - `GET /saved`: the saved list, with a red Delete button for each network.
  - `POST /saved/delete`: calls `wifiStore::remove()`, then redirects back to `/saved`.
- To make the page look like WiFiManager's own, it uses WiFiManager's `getHTTPHead()` (same CSS and scripts) and its `HTTP_BACKBTN` / `HTTP_END` pieces. Delete uses the built-in red button style (`class='D'`). `getHTTPHead()` is `protected` in the library, so `wm` is a tiny subclass that makes it public:
  ```cpp
  class Portal : public WiFiManager {
   public:
    using WiFiManager::getHTTPHead;
  };
  ```

SSIDs are HTML-escaped with `htmlEscape()` from `src/html_util.h`. The hotspot pages and the management page share it.

### Management page (`web_admin.cpp`)

It uses the core `WebServer` on port 80 and runs only while the board is connected. `MDNS.begin("ova-muda")` makes it reachable at `ova-muda.local`.

> `.local` is the only kind of name a device can announce for itself. An address like `ova.muda` would need a DNS entry on your router, which is why the page lives at `ova-muda.local`.

| Route | Action |
|---|---|
| `GET /` | Builds the page: current connection, saved networks with Delete buttons, Add form |
| `POST /add` | `wifiStore::add(ssid, pass)`, then redirect to `/` |
| `POST /delete` | `wifiStore::remove(ssid)`, then redirect to `/` |
| anything else | redirect to `/` |

- SSIDs are escaped before being put into the page (`htmlEscape()`), since SSIDs can contain any characters.
- The page has no login by default. To require one, uncomment `ADMIN_USER` / `ADMIN_PASS` in `web_admin.cpp`, and every route will ask for a username and password.
- `webAdminEnd()` stops the server and the `.local` name. They are started again on the next connect, because the IP address may have changed.
- While the setup hotspot is open, `wifiStartPortal()` announces `ova-muda.local` on the hotspot instead, so the same address opens the setup pages. `leavePortal()` stops it again.

---

## Settings

| Constant | File | Default | Meaning |
|---|---|---|---|
| `SDA_PIN`, `SCL_PIN`, `OLED_ADDR` | main.cpp | 5, 6, 0x3C | OLED wiring |
| `BUTTON_PIN` | main.cpp | 9 | Setup button |
| `LONG_PRESS_MS` | main.cpp | 3000 | Hold time to open the hotspot |
| `PORTAL_SSID` | wifi_conn.cpp | `"Pegazovo jaje"` | Hotspot name |
| `PORTAL_TIMEOUT_S` | wifi_conn.cpp | 300 | Hotspot closes this many seconds after opening |
| `CONNECT_TIMEOUT_MS` | wifi_conn.cpp | 15000 | Time to wait for each network |
| `BACKOFF_MS` | wifi_conn.cpp | 10000 | Wait before rescanning |
| `REDUCE_TX_POWER` | wifi_conn.cpp | 1 | 8.5 dBm transmit power for Super Mini stability |
| `HOSTNAME` / `MDNS_NAME` | wifi_conn.cpp / web_admin.cpp | `"ova-muda"` | Device name / `.local` address |
| `MAX_NETWORKS` | wifi_store.h | 20 | Size of the saved list |
| `ADMIN_USER` / `ADMIN_PASS` | web_admin.cpp | (off) | Optional login for the page |

---

## The weather screen

The weather screen is built on top of this layer and is documented in [weather.md](weather.md). It uses these functions from `wifi_conn.h`:

```cpp
bool wifiIsConnected();   // true only in the Connected stage
WifiState wifiState();    // Scanning / Connecting / Connected / Backoff / Portal
String wifiSsid();        // network being tried or connected to
```

The BOOT short press is still unused, so it is free for future weather features, e.g. switching pages.

---

## Testing checklist

1. Fresh board: the OLED shows *No saved Wi-Fi*.
2. Hold BOOT 3 s: the progress bar fills, and on release the hotspot opens.
3. Join *Pegazovo jaje* and add your network. The OLED then shows the SSID and IP.
4. Open `http://ova-muda.local`: the network is listed. Add a second one, e.g. a phone hotspot.
5. Turn off the router: within about scan time + 15 s, the board switches to the other network.
6. Delete a network on the page and reboot: it's still gone (the list survives power cycles).
7. Open the hotspot, then hold BOOT 3 s again: the hotspot closes and normal connecting resumes. The same happens with **Odustajem :(** on the page, and on its own after 5 minutes.
8. In the hotspot, open **Brisanje postojecih muda** and delete one: it disappears there and on `ova-muda.local`.
9. Enter a wrong password in the hotspot: it is **not** added to the list.

Serial log prefixes: `[wifi]` for connection steps, `[store]` for list changes, `[web]` for the management page.

## Known limitations

- Passwords are stored unencrypted in flash.
- Networks added on the web page are saved without checking the password. A wrong one simply fails to connect (15 s timeout) and the board moves on.
- No OTA updates with the current partition table.
- 2.4 GHz networks only. WPA2-Enterprise networks (e.g. eduroam) are not supported.
