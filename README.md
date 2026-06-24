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

1. Open `esp32-epaper-web.ino` in the Arduino IDE.
2. Set `ssid` and `password` near the top.
3. Select your ESP32 board and port, then upload.
4. Open Serial Monitor at **115200** — it prints the device IP once Wi-Fi connects.
5. Browse to that IP, type text, hit **Update Display**.

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
