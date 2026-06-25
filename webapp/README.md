# Web app (browser SPA)

A standalone, single-file web app that controls the e-paper display from any
phone or laptop — a richer alternative to the minimal page the device serves
itself. It talks to the device's HTTP API and polls a new `/status.json`
endpoint for live state (mode, battery, weather, station).

> **Branch:** this lives on the `web-app` branch. It needs the firmware on this
> branch too (it adds `/status.json` and CORS headers). Flash the branch firmware,
> then open the app.

## Run it
1. Flash the firmware from this branch (adds `/status.json` + `Access-Control-Allow-Origin`).
2. Open [`index.html`](index.html) — just double-click it, serve it locally
   (`python -m http.server` in this folder), or host it on GitHub Pages.
3. Enter the device address (e.g. `http://192.168.12.50`) and hit **Connect**.
   It's saved in `localStorage` for next time.

## What it does
- **Live status** every 5 s from `/status.json`: current screen, battery %/volts,
  auto-cycle state, weather (city/temp), station (temp).
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
