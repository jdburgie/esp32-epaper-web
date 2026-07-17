# ESP32 E-Paper Web Display

Drive a 2.13" e-paper panel from an ESP32 (or an ESP8266/ESP-12E — see below)
and change the text from any browser on your network. No app, no cloud — just
type and submit.

## Enclosure & battery

A 3D-printable case and an 18650 + load-sharing-charger power design live in
[`enclosure/`](enclosure/) — parametric OpenSCAD source plus ready-to-print STLs,
a power BOM/wiring guide, and print/assembly notes.

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
- AsyncTCP (ESP32) — use **ESPAsyncTCP** instead if building for ESP8266 (see
  "ESP8266 (ESP-12E) port" below)

## Setup

**1. Wi-Fi credentials.** Create `secrets.h` next to the sketch (it's gitignored,
so it never gets committed and won't be in a fresh clone):

```cpp
#pragma once
#define SECRET_SSID "your-wifi-name"
#define SECRET_PASS "your-wifi-password"
#define OWM_API_KEY "your-openweathermap-key"   // for weather mode (openweathermap.org/api)

// Optional static IP (comment out USE_STATIC_IP for DHCP). Useful when the
// router has no DHCP reservation option and the station push needs a fixed
// target. Pick an address BELOW the router's DHCP pool to avoid collisions.
#define USE_STATIC_IP
#define STATIC_IP      192,168,12,50
#define STATIC_GATEWAY 192,168,12,1
#define STATIC_SUBNET  255,255,255,0
#define STATIC_DNS1    192,168,12,1
#define STATIC_DNS2    8,8,8,8
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

**Over-the-air (OTA) updates.** After the first USB flash, the device runs
`ArduinoOTA` (hostname `epaper`, port 3232), so you can flash over Wi-Fi:
```
pio run -e esp32dev_ota -t upload        # uploads to upload_port (device IP) in platformio.ini
```
The partition scheme is `min_spiffs.csv` (two ~1.9 MB app slots) — OTA needs two
app partitions, so a USB flash with this scheme is required once before OTA works.
For a password, add `#define OTA_PASSWORD "…"` to `secrets.h` and set
`upload_flags = --auth=…` in the `esp32dev_ota` env.

**3. Use it.** Open the serial monitor at **115200** — it prints the device IP once
Wi-Fi connects. Browse to that IP. The current IP is also shown in the bottom-left
of the panel at all times.

**If Wi-Fi doesn't connect** (wrong password, network out of range, moved to a
new house), the device doesn't hang or go dark. After ~20s it starts a recovery
**access point** — the panel itself displays the AP name and its IP
(`192.168.4.1`). Connect a phone/laptop to that AP, browse to the IP, and use the
**Wi-Fi card** on the page to enter new credentials; the device saves them and
reboots to try again. This also means you can change Wi-Fi networks later without
re-flashing — the Wi-Fi card is always on the page, not just during recovery.

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

**Persistence:** the last view (and Wi-Fi credentials, if set via the recovery
AP) is saved to flash — NVS/`Preferences` on ESP32, a small LittleFS JSON file
on ESP8266 (same `prefs.getString/putString/...` call sites either way) — so
after a power loss the panel restores itself: a remembered ZIP boots straight
back to that weather view; otherwise it boots to the clock.

**Controls & extras:**
- **Physical button** (GPIO27 → GND) cycles Clock → Text → Weather → Station. Also
  on the page: **Next screen** and an **Auto-cycle** toggle.
- **Auto-cycle** advances the screen on a timer (~12 s), but **dwells longer (~30 s)
  on the text screen when it has notes**.
- **Text scrolling:** messages taller than 4 lines page through a 4-line window
  (with a `start/total` indicator).
- **Battery indicator:** an 18650 sensed on GPIO34 (via a 2:1 divider) shows a
  battery % in the top-right of every screen — auto-hidden when no cell is wired.
  See [`enclosure/`](enclosure/) for the battery + charger build.

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
`dailyrainin`, and `baromrelin` and replies `200 OK`. Your existing
AmbientWeather.net cloud upload is independent and keeps working. Tap **Show
Station** on the page to put the live feed on the panel.

> **AMBWeatherPro quirk:** this console joins its parameters onto the URL with
> `&` instead of `?` (`/data/report/&tempf=…`), so there's no query delimiter and
> a normal server parses nothing. The handler works around it by parsing the raw
> URL tail when no `?` is present — so no special path config is needed on the
> console; just set `Server` to the ESP's IP.

## ESP8266 (ESP-12E) port

The whole project also runs on an ESP8266 (e.g. a NodeMCU-style dev board with
an ESP-12E module) via the `esp12e` PlatformIO environment — same firmware,
same features (weather, station push, PWA, OTA, AP-mode recovery), split from
the ESP32 code by `#if defined(ESP32) / #elif defined(ESP8266)` throughout.

```
pio run -e esp12e -t upload           # first flash, over USB
pio run -e esp12e_ota -t upload       # later updates, over Wi-Fi (after the first USB flash)
```

**Wiring (NodeMCU D-pin → GPIO).** Hardware SPI is fixed (SCK/MOSI below) and
used automatically — don't wire anything to D6. **D8/GPIO15 is intentionally
unused**: it must read LOW at boot, and an e-paper driver board's CS-idle
pull-up on that line can prevent the ESP8266 from booting at all.

| Driver board | NodeMCU pin | GPIO | Why this pin |
|--------------|-------------|------|---------------|
| SDI (MOSI)   | D7          | 13   | fixed HW SPI |
| SCLK (CLK)   | D5          | 14   | fixed HW SPI |
| CS           | D3          | 0    | output only — safe on a boot-strap pin |
| D/C          | D4          | 2    | output only (also drives the onboard LED — cosmetic) |
| RES          | D0          | 16   | no boot-strap meaning either way |
| BUSY         | D2          | 4    | **input** from the display — needs a pin with no boot meaning |
| Button       | D1          | 5    | input w/ pullup — same reasoning as BUSY |
| Battery      | A0          | —    | the only ADC pin |
| VCC / GND    | 3V3 / GND   | —    | |

**Battery sense needs empirical calibration.** ESP8266 has one 0–1V ADC; a
NodeMCU board's onboard divider ratio varies by vendor/clone, and this project
adds its **own** 100k/100k divider on top (same physical design as the ESP32
board, feeding A0 at roughly half the cell voltage — safe under any onboard
divider variant). The combined ratio isn't knowable in advance: connect a
charged cell through the divider, compare a multimeter reading to
`analogRead(A0)` in `/status.json`'s `battRawMv`, and set
`BATT_ESP8266_FULLSCALE_V` in the sketch to `(multimeter volts) / (raw/1023.0)`.

**Libraries differ from the ESP32 build:** `ESPAsyncTCP` instead of `AsyncTCP`,
`LittleFS` instead of `Preferences`, `ESP8266WiFi`/`ESP8266HTTPClient` instead
of `WiFi`/`HTTPClient` — all handled by the `esp12e` env's `lib_deps` and the
sketch's `#ifdef`s; nothing to configure by hand.

**PROGMEM matters here.** The embedded web app, logo, and PWA icon are ~14 KB +
12 KB + 23 KB constant blobs. ESP8266 only has ~80 KB of RAM total, and without
`PROGMEM` (keeping them in flash instead of copying to RAM) the firmware simply
won't link. If you regenerate `webapp.h` after editing the web app, keep the
`PROGMEM` keyword — see `webapp/README.md`.

**Static IP: this board is now the deployed device.** `secrets.h`'s
`USE_STATIC_IP` block used to be guarded to `#if defined(ESP32)` specifically so
an ESP8266 build could never collide with the ESP32 unit's static `.50`. That
guard was removed once the ESP32 unit was retired — **this ESP8266/ESP-12E
board now holds `.50`** (see JOURNAL.md). If you ever bring the old ESP32 board
back online while building from this same `secrets.h`, it will *also* try to
claim `.50` and collide with this one — give it a different address (or a
different `secrets.h`) first.

> **Verified so far:** boot, Wi-Fi connect, the recovery AP fallback (forced by
> pointing it at a nonexistent network), the web server/PWA/persistence (all
> stable over USB and OTA flashes) — all on a board with **no e-paper panel
> physically attached yet**. The wiring table above is the intended pin map,
> not yet confirmed against a real panel; the pin choices *do* specifically
> avoid the known ESP8266 boot-strap traps (see the "why this pin" column), but
> give the display module itself a first test before trusting it blind.

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
