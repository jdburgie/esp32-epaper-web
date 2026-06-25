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

### 2026-06-25 — Enclosure: refined exterior + real measured dims
- Brought in the user's refined `weather-epaper-refined.scad` as
  `enclosure/epaper-case.scad`: **measured display dims** (module 76.5×34.5×2.4,
  window 59.2×29.3, win_dx −1.0) and a redesigned exterior — beveled rear edge,
  wraparound accent band, chamfered faceplate, recessed display surround, beveled
  window opening, softened countersinks. Internals/cavity/ports unchanged.
- That refinement came off an older base, so it had dropped the **button hole**,
  **stand**, and **wall-mount keyholes** — re-added all three on top of the
  refined exterior (btn_d top hole, `stand` part, `wall_mount` keyholes).
- Re-rendered shell/front/stand STLs (all manifold-clean) + preview.

### 2026-06-25 — Persist the message box text
- `saveState()` now also writes `currentText` to NVS ("text" key); boot restores
  it (`prefs.getString("text", ...)`). Boot mode logic gained a `MODE_TEXT` branch
  so a saved message screen comes back after a power cycle (was: text reset to
  "Hello World!"; only mode+zip+cycle persisted).
- **Verified on hardware:** set "Persist me/line two", hard-rebooted (DTR/RTS) →
  came back as mode `text` with the message intact (was: reset to "Hello World!").

### 2026-06-25 — Made the web app a PWA (installable, home-screen)
- Added `webapp/manifest.webmanifest`, `webapp/sw.js`, and icons (192/512/apple-
  touch, from Three Oak Woods branding). index.html: manifest link, theme-color,
  apple-web-app meta, SW registration (guarded by `isSecureContext`), and an
  **Install app** button (`beforeinstallprompt`).
- Device serves the PWA assets: `/manifest.webmanifest` (absolute-path variant in
  `DEVICE_MANIFEST`), `/sw.js` (`DEVICE_SW`), `/icon-192.png` + `/apple-touch-icon.png`
  (from `icon.h`, a byte array via `xxd -i`). Verified all 200 w/ right MIME.
- **Key constraint documented:** service workers need a secure context; the device
  is HTTP, so no offline/auto-install-prompt there, BUT add-to-home-screen still
  gives a standalone app over HTTP (same-origin live data). HTTPS hosting would
  enable offline but mixed-content-block the HTTP device — so device-served HTTP
  is the right tradeoff. SW self-activates only if served from a secure origin.

### 2026-06-25 — Merged web-app → main; device serves the SPA at /app
- **Merged `web-app` into `main`** (fast-forward) — `/status.json`, CORS, the SPA,
  weather views, and the "Message" label are now on main.
- **Device bundles + serves the web app:** `webapp.h` is auto-generated from
  `webapp/index.html` (raw string literal, like `logo.h`) and served at **`/app`**.
  The control page got an **"Open the web app ↗"** link. SPA now defaults its host
  to `location.origin` when served over http (bundled copy targets the device
  automatically; standalone file falls back to the .50 guess).
- Verified: `/app` returns the SPA, link present on `/`. Regen note in webapp/README.
- (Still unverified on hardware: physical button + battery wiring.)

### 2026-06-25 — Weather views in the web app (`web-app` branch)
- Enriched **`/status.json`**: weather now includes `feels/humidity/wind/icon`;
  station includes `gustmph/dailyrain/baromin/winddir`.
- SPA `webapp/index.html` gained two **view cards**: a **Forecast** card (emoji
  icon via OWM code, big temp, city, condition, feels/humidity/wind) and a
  **Backyard station** card (temp, humidity, wind+compass, gust, rain, pressure).
  Moved the small weather/station stats out of the status card.
- Verified live: status.json shows Greeley 62F/79%/wind 3/icon 10n. JS syntax
  checked, div tags balanced. (Merged main into web-app first so the branch has
  battery/button/cycle/enclosure too.)

### 2026-06-25 — `web-app` branch: standalone browser SPA
*(this entry exists on the `web-app` branch)*
- Firmware (branch only): added **`/status.json`** (mode, ip, autoCycle, text,
  battery, weather, station via ArduinoJson) and a global
  **`Access-Control-Allow-Origin: *`** header (DefaultHeaders) so an off-device
  app can read it. `modeName()` helper added.
- **`webapp/index.html`** — single-file Three Oak Woods themed SPA: enter the
  device address (saved in localStorage), polls `/status.json` every 5 s, shows
  live status (battery, weather, station, auto-cycle), and drives all the
  existing GET endpoints (clock/weather/station/next/cycle/set/clear/refresh).
  Fires actions with `redirect:'manual'`. No build step / no deps.
- Verified `/status.json` + CORS header on device. To use: flash this branch,
  open `webapp/index.html`, connect to `http://192.168.12.50`.

### 2026-06-25 — Battery %, button cycling, auto-cycle, text scroll; case v2
- **Battery indicator:** GPIO34 via 2:1 divider, `analogReadMilliVolts`, mapped
  3.30–4.20 V → 0–100%. `readBattery()` auto-detects (shows only when plausible),
  battery glyph + % top-right of every screen (`drawOverlays`, renamed from
  `drawIpLabel`). `BATT_RATIO` configurable.
- **Physical button** GPIO27 `INPUT_PULLUP` → debounced in loop → `cycleScreen()`
  (Clock→Text→Weather→Station). Web equivalents: `/next` (P_CYCLE) and `/cycle`
  (toggle autoCycle, persisted to NVS "cycle").
- **Auto-cycle:** advances every `CYCLE_MS` (12 s), but `TEXT_DWELL_MS` (30 s) when
  on text with notes (`hasNotes()`).
- **Text scrolling:** `drawText` pages a 4-line window (`textScroll`, `lineCount`,
  `nthLine`) with a `n/total` indicator when >4 lines; `loop` advances every 4 s.
  Textarea bumped to rows=6/maxlength=400.
- **Enclosure v2:** added `btn_d` top button hole, `stand` part (easel/kickstand),
  optional `wall_mount` keyholes. STLs re-rendered (shell/front/stand), preview
  updated. Battery-sense + button wiring documented in enclosure/README.
- Verified on device via web: Clock→Text cycle, auto-cycle on/off, 6-line text set
  (scrolls). Button + battery are hardware-gated (logic in place, not yet wired).
- **Next:** `web-app` branch for a browser SPA hitting the device API.

### 2026-06-25 — Battery (18650 + load sharing) + 3D-printed enclosure
- Added an **`enclosure/`** project: parametric OpenSCAD case (`epaper-case.scad`)
  → shell + front plate, rendered to STLs (clean 2-manifold, no supports). Holds
  ESP32 on standoffs, 18650 in cradle saddles, charger module, USB slot + switch
  hole, 4 corner M2.5 screws. All dims are top-of-file variables (measure & tweak).
  OpenSCAD is installed at `C:\Program Files\OpenSCAD\openscad.exe`; re-render with
  `openscad -D 'part="shell"' -o x.stl epaper-case.scad`.
- **Power design (in enclosure/README):** load-sharing charger (Adafruit PowerBoost
  1000C recommended, IP5306 module as budget) → 5 V into ESP32 VIN. Bare TP4056 is
  NOT load-sharing — flagged. 18650 ~3000mAh → ~12–18 h (always-on server blocks
  deep sleep). Firmware unchanged (still just 5 V on VIN).
- Open follow-up offered: firmware **battery monitor** (18650+ via divider → ADC →
  show battery % on the panel). Not built yet.

### 2026-06-25 — STATION DATA FLOWING. Fixed the `&` vs `?` URL quirk
- Console finally pushed once Server was set to 192.168.12.50, but sent
  `query=0 NO FIELDS PARSED`. Full `/debug` dump revealed why: the **AMBWeatherPro
  console joins params with `&` not `?`** — URL was
  `/data/report/&PASSKEY=...&tempf=64.0&humidity=71&...` (User-Agent: `AMBWeatherPro`).
  No `?` delimiter → server parses nothing, even though all fields are present.
- **Fix:** in `onReport`, grab the URL tail after the first `?`/`&` and parse it
  with `fromBody()` (added as a source in `rd()`). Now works with the console's
  stock config — no special Path needed. Verified: live "Station — 64F, Hum 72%,
  Wind 3", pushes every 60s from .190 parsing cleanly.
- Added a full-request dump to `/debug` (method/url/headers/params/body) — that's
  what exposed the `&` quirk. Keep it; invaluable for this kind of thing.
- Note: boot restore currently only handles WEATHER→weather else clock; STATION
  isn't restored on boot (would show "waiting" then fill). Offered to add.

### 2026-06-24 — Static IP (T-Mobile gateway has no DHCP reservation)
- **Root issue all along:** the console's "Customized" Server field was **blank**,
  so it never pushed. `/debug` proved it — only this PC's IP (.112) ever hit
  `/data/report`, never the console (.190). The station never sent anything.
- T-Mobile 5G Gateway has **no DHCP reservation** option, so pinned the ESP with a
  **firmware static IP** instead. `WiFi.config()` guarded by `USE_STATIC_IP` in
  `secrets.h`. Chose **192.168.12.50** — ARP showed ~70 devices all in .107–.246
  (pool starts ~.107), so .2–.99 is below the pool and safe; pinged .50/.40/.30/.60
  all free. ESP now at **192.168.12.50**; DNS/NTP/weather verified working there.
- ESP MAC = `08:B6:1F:F0:01:64`. Console = `192.168.12.190` (MAC 40:91:51:59:9E:A3).
- **User action still needed:** set the console's Customized Server to
  **192.168.12.50**, Path `/data/report/`, Port 80, AmbientWeather, and Save.

### 2026-06-24 — NTP clock (default view) + NVS persistence
- **Clock mode (`MODE_CLOCK`, now the default):** NTP via `configTzTime(TZ_INFO,...)`,
  `TZ_INFO="MST7MDT,M3.2.0,M11.1.0"` (Mountain, auto DST). `drawClock()` centers a
  big time (24pt) + date (12pt); shows "Syncing time..." until NTP locks; ticks
  once per minute in loop (full refresh each minute — flashes; flagged to user).
  "Show Clock" button + `/clock` endpoint.
- **Persistence (NVS via `Preferences`, namespace "epaper"):** `saveState()` writes
  `mode` + `zip` on every view change. Boot restores: saved `mode==WEATHER` && zip
  → weather; else clock. Verified across a real reboot — saved ZIP 80631 came back
  as "Weather — Greeley" after reset.
- **Station pull vs push reminder:** the console PUSHES to
  `http://<esp-ip>/data/report/`; the ESP never pulls from the station. (OWM is the
  only outbound pull: `api.openweathermap.org/data/2.5/weather`.)
- Note: clock's per-minute full refresh is aggressive on panel life; could switch
  to partial refresh or coarser interval if wear becomes a concern.

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

- ~~TODO (2026-06-25): add weather views to the web page~~ **DONE** — SPA Forecast
  + Backyard station cards; `/status.json` enriched. Web app now served at `/app`.
- **TODO (2026-06-25): "remember test"** — read as: test the new button + battery
  on real hardware once wired (GPIO27 button, GPIO34 divider). *Awaiting
  confirmation that this is what was meant.*
- Cap rendered lines at 4 (the textarea allows more than fit the 122px panel);
  truncate or warn on overflow.
- Per-line auto-shrink to a smaller font when a line exceeds the 250px width.
- Possible later: partial-window refresh for snappier redraws (currently
  full-window, which flashes black/white each update).

---

## Estimated AI footprint

Rough, unverifiable estimate of the data-center water used by the AI assistant
across this project's build sessions: **on the order of ~1 gallon (~4 L), with at
least a 10× uncertainty band either way** — plausibly a couple of cups, plausibly
a few gallons. There is no per-conversation meter; this is extrapolated from
published per-query ranges, which are themselves contested and vary widely by
data-center cooling and location.

- **Caveat:** order-of-magnitude only; do not cite as measured. Local work
  (firmware compiles/flashes, OpenSCAD renders, `curl` tests) ran on the dev PC —
  ordinary wall power, negligible water.
- **Source:** P. Li, J. Yang, M. A. Islam, S. Ren, *"Making AI Less 'Thirsty':
  Uncovering and Addressing the Secret Water Footprint of AI Models,"* 2023
  (arXiv:2304.03271).
