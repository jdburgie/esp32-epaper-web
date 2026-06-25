# Project Journal

A running log of work on the ESP32 e-paper web display, so the project can be
picked up from any machine. Newest entries at the top. Hardware, wiring, and
usage live in [README.md](README.md); this file tracks **state, decisions, and
history**.

---

## Working from another computer (bootstrap)

1. **Clone the repo** and open the folder.
2. **Recreate `secrets.h`** — it's gitignored (holds Wi-Fi creds) so it does NOT
   travel with the repo. Create `secrets.h` next to the `.ino` with:
   ```cpp
   #pragma once
   #define SECRET_SSID "your-wifi-name"
   #define SECRET_PASS "your-wifi-password"
   #define OWM_API_KEY "your-openweathermap-key"   // weather mode
   ```
3. **Build/flash.** Two supported paths:
   - **PlatformIO** (used on the primary machine):
     ```
     pio run                 # compile
     pio run -t upload       # flash (set the right port in platformio.ini)
     pio device monitor -p COM3 -b 115200
     ```
     Board env is `esp32dev` (board `esp32doit-devkit-v1`). Libraries auto-resolve
     from `platformio.ini`. **Set `upload_port`/`monitor_port`** to the COM port on
     the new machine (it won't necessarily be COM3).
   - **Arduino IDE**: open `esp32-epaper-web.ino`, install the libraries listed in
     the README, select the ESP32 board + port, upload.
4. **Find the device IP** from the serial monitor (printed once Wi-Fi connects),
   browse to it, type text, **Update Display**.

> **Note:** the README's "Setup" section predates `secrets.h` and still says to
> put `ssid`/`password` directly in the `.ino`. The creds now come from
> `secrets.h` instead — recreate that file rather than editing the sketch.

---

## Repo layout / files

| File | Purpose |
|------|---------|
| `esp32-epaper-web.ino` | The sketch (Wi-Fi + async web server + GxEPD2 draw). |
| `secrets.h` | Wi-Fi creds. **Gitignored** — recreate per machine (see above). |
| `logo.h` | Three Oak Woods badge SVG (from oaklink/branding), served at `/logo.svg`. |
| `platformio.ini` | PlatformIO build/flash config (env `esp32dev`). |
| `README.md` | Hardware, wiring, library list, panel-constructor notes. |
| `JOURNAL.md` | This file. |
| `epaper-sketch_jun24a.ino.bak` | Old static bring-up prototype. Local-only (gitignored), NOT built — won't be in a fresh clone. |

> **PlatformIO gotcha:** PlatformIO concatenates *every* `*.ino` in `src_dir`
> into one sketch. A second `.ino` causes `setup()`/`loop()`/`display`
> redefinition errors — that's why the old prototype carries a `.bak` extension.
> `build_src_filter` does **not** exclude `.ino` files.

---

## Log

### 2026-06-24 — Fix station push parsing (POST) + /debug diagnostics
- **Root cause of "/data/report/ failing":** the console likely POSTs the readings
  in the request body, but the handler only read the **query string** → zero fields
  parsed, panel stuck on "no data". Now reads each field **query → POST param →
  raw body** (`rd()` + `fromBody()`). Confirmed: simulated POST parses `tempf`.
- The `/data/report/` traffic is **local** — it's the console (its LAN IP) doing
  outbound pushes, NOT ambientweather.net. The cloud never connects into the LAN.
- Added **`/debug`** (recent hits: method, source IP, param count, parsed tempf,
  + last raw body) and an `onNotFound` that logs 404s — so wrong path/method from
  the real console is visible without a serial monitor.

### 2026-06-24 — Deep-refresh (de-ghost) + station waiting timeout
- **Deep refresh** `deepRefresh()`: flashes the panel black↔white x2 to scrub
  SSD1680 ghosting, then `redrawCurrent()` restores the view. Runs automatically
  every 6 h (`DEEP_REFRESH_MS`) and on demand via a **Clean (de-ghost)** button
  (`/refresh` → `P_DEEP`). Resolves the humidity-digit ghosting seen earlier.
- **Station "waiting" no longer looks hung:** the waiting screen shows a live
  `(Ns)` counter and redraws every 20 s; after 90 s (`STATION_WAIT_MS`) with no
  push it **falls back** to weather (if a ZIP is set) or text. `stationModeSince`
  set when `/station` is tapped. Verified: fell back to "Weather — Greeley" after
  ~90 s of no data.

### 2026-06-24 — IP to lower-right + weather detail overflow fix
- IP label moved to the **lower-right** corner only (`drawIpLabel()`,
  right-aligned by `len*6`). Briefly tried both corners; user wanted right only.
- Staying on **DHCP** (no static IP in firmware) — DHCP reservation handles the
  stable address for the station push.
- Fixed weather **detail line overflowing** the 250px width (real panel photo
  showed "Wind" with its value cut off): tightened `wx.detail` — dropped the "F"
  after feels, triple→double spaces.
- Open: a photo showed faint **ghosting** on the humidity digits ("58%" with a
  prior value behind). Likely SSD1680 ghosting; remedy if it bothers: periodic
  deep full-refresh / clearScreen pass. Not done yet.

### 2026-06-24 — Local Ambient Weather station (push) + layout mockup
- **Third mode: Backyard Station.** The ESP32 now ingests the Ambient Weather
  console's "Customized" upload (AmbientWeather protocol) at **`/data/report/`** —
  fully local, no API key, real backyard sensors. Parses `tempf, humidity,
  windspeedmph, windgustmph, winddir, dailyrainin, baromrelin` from the query
  string, stores them in `struct Station st`, replies `200 OK`. Panel **redraws on
  each push** while in station mode (handler sets `pending=P_STATION`).
- `drawStation()`: big outdoor temp (left) + stat column (humidity, wind+compass,
  gust, rain) + pressure, IP bottom-left. `compass()` maps winddir° → 16-point.
  Shows "Waiting for station data" until the first push arrives.
- Page gets a **Backyard Station** card (Show Station) + a live summary line;
  `/station` endpoint queues `P_STATION`. Added `MODE_STATION` / `P_STATION`.
- **Why push over the AmbientWeather.net cloud API:** no keys, offline-resilient,
  the console's Customized server field was empty/unused. Cloud upload still runs
  in parallel, untouched.
- **Console config** (their device = AMBWeatherPro_V5.2.7): Customized=Enable,
  Protocol=AmbientWeather, Server=<ESP32 IP>, Path=`/data/report/`, Port=80,
  Interval=60. ESP32 needs a **stable IP** (DHCP reservation) so the console keeps
  reaching it — currently DHCP .184. Static-IP-in-firmware not added yet (offered).
- **Verified** with a simulated push: `tempf=72.5&humidity=68&windspeedmph=8...`
  → status "Station — 73F, Hum 68%, Wind 8". Real console not yet pointed at it.
- Also rendered an SVG mockup of the weather-mode panel layout for reference.

### 2026-06-24 — Fancy weather: OpenWeatherMap + drawn icons
- **Switched weather source wttr.in → OpenWeatherMap** (current-weather endpoint,
  `units=imperial`). Key lives in `secrets.h` as `OWM_API_KEY` (gitignored). JSON
  parsed with **ArduinoJson** (added to `lib_deps`). Now gets the real **city
  name** (wttr.in only echoed the ZIP), plus feels-like / humidity / wind / icon.
- **Fancy panel layout** (`drawWeather()`): city header + divider rule, a drawn
  **weather icon** on the left, **big temperature** (FreeSansBold24pt) with a
  hand-drawn degree ring + small "F", condition text, and a feels/hum/wind detail
  line — IP still bottom-left. Added fonts FreeSansBold9pt7b + FreeSansBold24pt7b.
- **Weather icons** are 1-bit GFX primitives (`drawSun/drawCloud/drawRain/drawSnow/
  drawBolt/drawMist`) mapped from the OWM icon code (`01`–`50`) — no bitmaps.
- **Partition bump:** `board_build.partitions = huge_app.csv` (3MB app) since fonts
  + TLS + JSON pushed the default ~1.3MB partition close. Firmware ~1.03MB.
- **Verified:** ZIP 94103 → "San Francisco 61F, Few clouds" parsed and drawn.
- Wi-Fi: SSID "Three Oak Wood". (Reminder: `pio device monitor` resets the board.)

### 2026-06-24 — Weather mode + Three Oak Woods themed page
- **Two display modes:** Text (existing) and **Weather**. The page now has a Text
  card and a Weather card; the Weather card asks for a US **ZIP** ("Show Weather"),
  which is how you switch into weather mode. `mode` (MODE_TEXT/MODE_WEATHER) +
  `weatherZip`/`weatherText` track state.
- **Weather source:** wttr.in, **no API key** — takes the ZIP directly and returns
  a one-line `%l|%t|%C` format we split (no JSON lib). HTTPS via `WiFiClientSecure`
  + `setInsecure()`, `User-Agent: curl/*` so wttr returns plain text. Output is
  ASCII-filtered (`asciiOnly()`) to drop the degree sign / wind arrows the 12pt
  font can't render. Panel shows ZIP / temp / condition; **auto-refresh every 15
  min** while in weather mode. (wttr.in echoes the ZIP as the location label, so
  line 1 is the ZIP, not the city name — could fetch the city separately later.)
- **Concurrency fix:** web handlers no longer draw/fetch inline. They set a
  `Pending` action (P_TEXT/P_WEATHER/P_CLEAR) and `loop()` executes it. Keeps
  blocking SPI/TLS off the AsyncTCP task and avoids two tasks hitting the display.
- **Three Oak Woods theme:** page restyled with the company palette (Forest Green
  `#2C654B`, Cream, Acorn Amber, Bark, Parchment), Nunito font, and the acorn
  **badge logo** (`logo.h`, served at `/logo.svg`) in the header + as favicon.
  Assets/colors pulled from `oaklink/branding/`.
- **Verified on hardware:** ZIP 94103 → "94103 / 68F / Partly Cloudy" drawn; text
  mode round-trips; page + logo + favicon serve correctly. Firmware ~1 MB now
  (TLS stack). Note: opening `pio device monitor` resets the board (DTR/RTS), so
  test HTTP endpoints *without* a monitor attached, or expect a reboot race.

### 2026-06-24 — Persistent IP label + README/repo refresh
- **IP on panel:** the device IP now renders in the built-in 6x8 font at the
  bottom-left **at all times** — on both `drawText()` and `clearDisplay()`, so it
  survives a Clear. New `ipText` global + `drawIpLabel()` helper (called inside
  the paged-draw loop). Boot draw moved to *after* Wi-Fi connects so the first
  render already has the IP. Note: paged drawing reruns the draw block per page,
  so the big font is re-selected each page (the label switches to the small font).
- **README:** Setup section rewritten for `secrets.h` (no more editing creds in
  the `.ino`) and documents both PlatformIO and Arduino IDE flashing.
- **Repo:** project is now an actual git repo, pushed to
  https://github.com/jdburgie/esp32-epaper-web (private). The earlier note that
  this repo already existed was wrong — created it fresh today.

### 2026-06-24 — Multi-line + clear-screen, moved to PlatformIO
- **Multi-line text:** swapped the single-line `<input>` for a `<textarea rows=4>`
  (Enter = newline), bumped `maxlength` 80→120. `drawText()` now counts `\n`,
  computes block height (`lines * LINE_HEIGHT`, `LINE_HEIGHT 22`), centers the
  block vertically, and centers each line horizontally on its own width. `/set`
  strips `\r` (browsers send `\r\n` from textareas).
- **Clear-screen button:** new `clearDisplay()` (white full-window fill +
  hibernate) and a `/clear` GET endpoint, with a secondary "Clear Screen" button
  under the update form. Both handlers `redirect("/")` *before* the ~2s redraw so
  the browser doesn't hang.
- **Tooling:** added `platformio.ini` and started building/flashing with
  PlatformIO (arduino-cli not installed). First build failed on a duplicate
  `.ino` (the old bring-up prototype) — renamed it to `.ino.bak`.
- **Creds:** moved Wi-Fi creds out of the sketch into gitignored `secrets.h`;
  `.gitignore` now also covers `.pio/`, `secrets.h`, `*.ino.bak`.
- **Verified on hardware:** flashed to the DevKit (COM3), joined Wi-Fi, served the
  updated UI; a 3-line test message rendered centered and evenly spaced —
  `LINE_HEIGHT 22` looks right, no adjustment needed.

### (earlier) — Initial build
- Got the FPC-A002 / SSD1680 2.13" panel drawing with `GxEPD2_213_BN`.
- Added Wi-Fi + ESPAsyncWebServer; browser form updates the displayed text.
- Panel hibernates between updates for burn-in protection; no deep sleep because
  the web server needs the chip awake.

---

## Open follow-ups (not started)

- Cap rendered lines at 4 (the textarea allows more than fit the 122px panel);
  truncate or warn on overflow.
- Per-line auto-shrink to a smaller font when a line exceeds the 250px width.
- Possible later: partial-window refresh for snappier redraws (currently
  full-window, which flashes black/white each update).
