# ESP32 E-Paper Web Display

Drive a 2.13" e-paper panel from an ESP32 and change the text from any browser
on your network. No app, no cloud — just type and submit.

## Hardware

- **MCU:** NodeMCU ESP32 DevKit V1 (30-pin) on a dual breakout/expansion shield
- **Driver board:** Inland e-paper driver board (SPI, 8-pin header, 5V/3.3V switch)
- **Panel:** FPC-A002 ribbon, 2.13" 250x122 b/w, SSD1680 controller

## Wiring

Driver board switch **P2 set to 3.3VIN**. All signal lines on the ESP32's
right-side header.

| Driver board | ESP32 pin | GPIO |
|--------------|-----------|------|
| SDI (MOSI)   | D23       | 23   |
| SCLK (CLK)   | D18       | 18   |
| CS           | D5        | 5    |
| D/C          | D19       | 19   |
| RES          | RX2       | 16   |
| BUSY         | D4        | 4    |
| VCC          | 3V3       | —    |
| GND          | GND       | —    |

> Keep the board switch on **3.3VIN**. The logic lines are 3.3V — feeding the
> board 5V while driving 3.3V logic is the usual way these get fried.

## Libraries (Arduino IDE Library Manager)

- GxEPD2 (Jean-Marc Zingg)
- Adafruit GFX (dependency of GxEPD2)
- ESPAsyncWebServer (ESP32Async / me-no-dev)
- AsyncTCP (ESP32 version)

## Setup

**1. Wi-Fi credentials.** Create `secrets.h` next to the sketch (it's gitignored,
so it never gets committed and won't be in a fresh clone):

```cpp
#pragma once
#define SECRET_SSID "your-wifi-name"
#define SECRET_PASS "your-wifi-password"
#define OWM_API_KEY "your-openweathermap-key"   // for weather mode (openweathermap.org/api)
```

The sketch `#include`s this file and reads `SECRET_SSID` / `SECRET_PASS` /
`OWM_API_KEY` — you do **not** edit credentials in the `.ino` anymore. A free
OpenWeatherMap key works; if you don't need weather, any non-empty string is fine.

**2. Build & flash.** Either toolchain works:

- **PlatformIO** (config is in `platformio.ini`):
  ```
  pio run -t upload                       # build + flash
  pio device monitor -p COM3 -b 115200    # watch serial
  ```
  Set `upload_port` / `monitor_port` in `platformio.ini` to your board's COM port
  (it won't necessarily be COM3). Libraries resolve automatically.
- **Arduino IDE:** open `esp32-epaper-web.ino`, install the libraries listed above,
  select your ESP32 board + port, and upload.

**3. Use it.** Open the serial monitor at **115200** — it prints the device IP once
Wi-Fi connects. Browse to that IP. The current IP is also shown in the bottom-left
of the panel at all times.

## Display modes

The control page (themed in the **Three Oak Woods** brand — palette, Nunito, and
the acorn badge served from `logo.h` at `/logo.svg`) offers these modes:

- **Clock** *(default view)* — NTP-synced date and time, redrawn every minute.
  Timezone is set by `TZ_INFO` in the sketch (default Mountain Time, auto DST).
- **Text** — type a message (multi-line supported) and hit **Update Display**.
- **Weather** — enter a US **ZIP code** and hit **Show Weather**. The panel shows
  a drawn weather icon, the **city**, a large **temperature**, the **condition**,
  and a **feels-like / humidity / wind** line. It **auto-refreshes every 5 min**.
- **Backyard Station** — live data pushed from a local **Ambient Weather** console
  (see below). Shows outdoor temp, humidity, wind + direction, gust, rain, and
  pressure, and **redraws on every push** while this mode is active. While waiting
  for the first push it shows a live counter, and **falls back to weather after
  ~90 s** so the panel never looks hung if the console isn't pushing yet.

The page also has a **Clean (de-ghost)** button that flashes the panel black↔white
to scrub e-paper ghosting; the same scrub runs automatically every 6 hours.

**Persistence:** the last view is saved to NVS (`Preferences`), so after a power
loss the panel restores itself — a remembered ZIP boots straight back to that
weather view; otherwise it boots to the clock.

Weather comes from **[OpenWeatherMap](https://openweathermap.org/api)** (current-
weather endpoint, `units=imperial`). Put a free API key in `secrets.h` as
`OWM_API_KEY`. The JSON response is parsed with ArduinoJson; the fetch uses HTTPS
with certificate validation disabled (fine for a home device). The weather icons
are drawn as 1-bit GFX primitives keyed off the OWM icon code (`01d`, `10n`, …) —
no bitmap assets needed.

> **Architecture note:** web handlers don't draw or fetch directly — they queue a
> `Pending` action that `loop()` executes. This keeps blocking SPI/TLS work off the
> AsyncTCP task and prevents two tasks touching the display at once.

### Connecting an Ambient Weather console (local push)

The ESP32 listens at **`/data/report/`** and ingests the console's "Customized"
upload — no cloud, no API key, your real backyard sensors every minute. In the
console's web UI (the *Customized* section):

| Field | Value |
|-------|-------|
| Customized | **Enable** |
| Protocol Type Same As | **AmbientWeather** |
| Server IP / Hostname | the **ESP32's IP** (give it a static IP / DHCP reservation so it stays put) |
| Path | `/data/report/` |
| Port | `80` |
| Upload Interval | `60` seconds |

The firmware parses `tempf`, `humidity`, `windspeedmph`, `windgustmph`, `winddir`,
`dailyrainin`, and `baromrelin` from the query string and replies `200 OK`. Your
existing AmbientWeather.net cloud upload is independent and keeps working. Tap
**Show Station** on the page to put the live feed on the panel.

## Panel / constructor notes

`FPC-A002` is the *cable* part number, not the controller, and a few revisions
have shipped on that same ribbon. The sketch uses `GxEPD2_213_BN` (SSD1680),
the most common match. If the screen comes up blank or garbled, swap that one
constructor line and re-flash, in this order:

1. `GxEPD2_213_BN`  (default — SSD1680)
2. `GxEPD2_213_B74` (SSD1680, newer revision)
3. `GxEPD2_213_B73` (SSD1675-based)
4. `GxEPD2_213_B72` (SSD1675-based)

The canonical list of every constructor lives in
**File → Examples → GxEPD2 → GxEPD2_Example**.

## Known tradeoffs

- **No deep sleep.** A web server needs the chip awake and on Wi-Fi, so this
  trades microamp sleep current for being interactive. The *panel* still
  hibernates between updates, so burn-in protection is intact.
- **Full-window refresh** every update (the black/white flash). Safe default,
  prevents ghosting. Partial updates are possible later for snappier redraws.
- **No auth or input sanitizing.** Fine on a trusted home network; don't expose
  it to the internet as-is. HTML special characters (`<`, `&`) render literally
  on the panel but can break the web page's "currently showing" line.
