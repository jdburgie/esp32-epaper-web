# Web app (PWA)

A web app that controls the e-paper display from any phone or laptop — a richer
alternative to the minimal page the device serves itself. It talks to the
device's HTTP API and polls `/status.json` for live state (mode, battery,
weather, station). It's a **PWA**: add it to your home screen and it runs
full-screen/standalone with the Three Oak Woods icon.

## Install (add to home screen)
Open the app on the device (`http://192.168.12.50/app`), then:
- **Android (Chrome):** ⋮ menu → **Add to Home screen** (or the **Install app**
  button if it appears).
- **iOS (Safari):** Share → **Add to Home Screen**.

It installs with the badge icon and opens chrome-less, talking to the device
over your LAN.

> **Why no offline / no auto-install prompt:** service workers (offline caching
> and the one-tap install prompt) require a **secure context** — HTTPS or
> localhost. The device serves plain **HTTP**, so the service worker doesn't
> register there (the app still installs to the home screen and works; it just
> isn't cached offline). Hosting the app on HTTPS *would* enable offline, but
> then the browser blocks it from reading the HTTP device (mixed content). Since
> live device data is the whole point, **HTTP-from-the-device is the right call**
> here. The service worker is included and self-activates **if** you ever serve
> the app from a secure origin.

## Run it
**Easiest — the device serves it.** The firmware bundles this app and serves it at
**`/app`**, and the device's control page has an **"Open the web app ↗"** link.
Just browse to the device (e.g. `http://192.168.12.50/`) and click it — no
hosting, and it auto-targets the device it's served from (same origin, no CORS).

**Standalone** — you can also open [`index.html`](index.html) directly (double-click,
`python -m http.server`, or GitHub Pages), then enter the device address and hit
**Connect** (saved in `localStorage`). Cross-origin reads work because the firmware
sends `Access-Control-Allow-Origin: *`.

> **Regenerating the bundle:** the device serves `webapp.h` (auto-generated from
> `index.html`). After editing the SPA, regenerate it from the repo root:
> ```
> { printf '#pragma once\nconst char WEBAPP_HTML[] = R"WEBAPP(\n'; cat webapp/index.html; printf '\n)WEBAPP";\n'; } > webapp.h
> ```
> (prepend the two `// Auto-generated` comment lines if you like), then reflash.

## What it does
- **Live status** every 5 s from `/status.json`: current screen, battery %/volts,
  auto-cycle state.
- **Weather view:** OpenWeatherMap conditions — emoji icon, big temp, city,
  description, and feels-like / humidity / wind.
- **Backyard station view:** live console data — temp, humidity, wind + compass
  direction, gust, rain today, and pressure.
- **Controls** (call the device's existing endpoints):
  - Screens: Clock, Station, Weather (with ZIP), Next, Auto-cycle toggle.
  - Text: multi-line note → Update display.
  - Clear, Clean (de-ghost).

## Why CORS
The app is served from a *different origin* than the device (a file or a host,
not the ESP itself), so the browser blocks reading the device's responses unless
the device sends `Access-Control-Allow-Origin`. The branch firmware adds that
header globally; the action endpoints are simple GETs (no preflight needed).

## Notes
- All control endpoints are plain GET and the app fires them with
  `redirect:'manual'` so it doesn't drag down the device's HTML on every tap.
- The device logo is loaded live from `http://<device>/logo.svg`.
- No build step, no dependencies — one HTML file (Nunito is loaded from Google
  Fonts; it falls back to a system sans if offline).
