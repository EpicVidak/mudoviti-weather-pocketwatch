# Handoff: ESP32-C3 Super Mini + 0.91" OLED (PlatformIO)

## Status
- Goal so far: get a 128x32 SSD1306 I2C OLED running on an ESP32-C3 Super Mini.
- A starter firmware has been written (I2C scan, then a hello screen, then an uptime counter).
- **Not yet confirmed working on real hardware.** The user hasn't reported a build, upload or result.
- The end-goal project (what the display should show) **is not decided yet**. Ask the user before building features.

## Hardware
- **MCU:** ESP32-C3 Super Mini. It has no USB-UART chip, so Serial goes through the C3's native USB CDC.
- **Display:** 0.91" SSD1306 OLED, 128x32, I2C, 4 pins.
  - Pins are labeled **GND, VCC, SCK, SDA**. SCK is the I2C clock (= SCL), not SPI.
  - The I2C address is expected to be `0x3C` (`0x3D` is possible on some modules).

## Wiring
| OLED | Super Mini (silkscreen label) |
|------|-------------------------------|
| GND  | GND  |
| VCC  | 3.3  |
| SDA  | 5 (GPIO5) |
| SCK  | 6 (GPIO6) |

### Why these pins
- **GPIO8 and GPIO9 are deliberately avoided**, even though they're the Arduino core's default I2C pins on the C3.
  - GPIO8 drives the onboard blue LED (inverted: LOW = on) and is a strapping pin.
  - GPIO9 is the BOOT button and is a strapping pin.
- On the C3, any GPIO can do I2C via `Wire.begin(SDA, SCL)`.
- The display is powered from 3.3V so the I2C logic stays at the C3's voltage.
- Pin order differs between OLED modules (GND/VCC are sometimes swapped). Wire by the printed label, never by position.

## Toolchain
VS Code + PlatformIO (not the Arduino IDE).

### `platformio.ini` (project root)
```ini
[env:esp32c3_supermini]
platform = espressif32
board = esp32-c3-devkitm-1
framework = arduino

monitor_speed = 115200

build_flags =
    -D ARDUINO_USB_MODE=1
    -D ARDUINO_USB_CDC_ON_BOOT=1

lib_deps =
    adafruit/Adafruit SSD1306
    adafruit/Adafruit GFX Library
```
- PlatformIO has no Super Mini board definition. `esp32-c3-devkitm-1` is the standard stand-in (same chip and flash layout).
- The two `build_flags` are **required** for `Serial` output over USB. They are the equivalent of "USB CDC On Boot: Enabled" in the Arduino IDE.

### Project layout
```
<project>/
├── platformio.ini
└── src/
    └── main.cpp
```

### `src/main.cpp` (current behavior)
- `#include <Arduino.h>`, `Wire.h`, `Adafruit_GFX.h`, `Adafruit_SSD1306.h`
- Defines: `SDA_PIN 5`, `SCL_PIN 6`, `SCREEN_WIDTH 128`, `SCREEN_HEIGHT 32`, `OLED_ADDR 0x3C`
- `Adafruit_SSD1306 display(128, 32, &Wire, -1);` (no reset pin)
- `setup()`:
  1. `Serial.begin(115200)`, then `delay(2000)` so the monitor can attach.
  2. `Wire.begin(5, 6)`, then an I2C bus scan that prints found addresses.
  3. `display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)`. On failure it prints an error and halts.
  4. Shows "Hello!" (size 2) and "ESP32-C3 + OLED" (size 1) for 2 seconds.
- `loop()`: shows "Uptime (s):" and a size-2 seconds counter, updating once per second.

## Display layout budget (128x32)
- Text size 1 is 6x8 px per character, giving **4 lines × 21 chars**.
- Text size 2 is 12x16 px per character, giving **2 lines × 10 chars**.
- Always call `clearDisplay()` → draw → `display()`.

## Super Mini quirks
- **Upload fails or the port is missing:** hold BOOT while plugging in USB (or hold BOOT and tap RESET), then upload, then press RESET.
- **The COM port can change after a reset.** If the serial monitor goes blank, close and reopen it.
- **Weak or failing Wi-Fi** is common on these boards. Add `WiFi.setTxPower(WIFI_POWER_8_5dBm);` right after `WiFi.begin()`.

## Troubleshooting checklist
1. **Blank display:** check the serial output for the I2C scan.
   - If it finds `0x3D`, change `OLED_ADDR`.
   - If it finds nothing, check the wiring. SDA/SCK may be swapped, or there may be a loose ground.
2. **No serial output at all:** confirm the `build_flags` are present and reopen the monitor after reset.

## Next steps
1. User builds and uploads. Confirm the scan shows `0x3C` and the hello screen appears.
2. Ask the user what they want to display or build (sensor readout, Wi-Fi clock, button UI, etc.) and extend from there.