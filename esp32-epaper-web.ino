/*
 * ESP32 E-Paper Web Display
 * --------------------------
 * NodeMCU ESP32 DevKit V1 (30-pin) + Inland 2.13" e-paper driver board.
 * Panel: FPC-A002 ribbon, 2.13" 250x122 b/w, SSD1680 controller.
 *
 * Type text in a browser on the same Wi-Fi network and the panel redraws.
 *
 * Wiring (Inland driver board -> ESP32):
 *   SDI (MOSI) -> GPIO23      D/C  -> GPIO19
 *   SCLK (CLK) -> GPIO18      RES  -> GPIO16 (RX2)
 *   CS         -> GPIO5       BUSY -> GPIO4
 *   VCC -> 3V3   GND -> GND   (driver switch P2 set to 3.3VIN)
 *
 * Libraries: GxEPD2, Adafruit GFX, ESPAsyncWebServer, AsyncTCP.
 *
 * NOTE: deep sleep and the web server are mutually exclusive. The ESP32
 * stays awake to serve requests; the PANEL still hibernates between
 * updates, which is what prevents burn-in/ghosting.
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <time.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include "secrets.h"   // SECRET_SSID / SECRET_PASS / OWM_API_KEY (gitignored)
#include "logo.h"      // THREEOAKWOODS_BADGE_SVG, served at /logo.svg
#include "webapp.h"    // WEBAPP_HTML (SPA / PWA), served at /app
#include "icon.h"      // webapp_icon_192_png[], served as the PWA icon

// PWA manifest the DEVICE serves (absolute paths for the /app origin). The copy
// in webapp/ uses relative paths for standalone/hosted use.
const char DEVICE_MANIFEST[] = R"MANIFEST({
  "name":"Three Oak Woods E-Paper","short_name":"E-Paper","start_url":"/app","scope":"/",
  "display":"standalone","background_color":"#F5F1E6","theme_color":"#2C654B",
  "icons":[{"src":"/icon-192.png","sizes":"192x192","type":"image/png","purpose":"any"},
           {"src":"/logo.svg","sizes":"any","type":"image/svg+xml","purpose":"any maskable"}]
})MANIFEST";

// Service worker the device serves (only runs if the origin is secure; harmless on HTTP).
const char DEVICE_SW[] = R"SWJS(const CACHE='epaper-dev-v1';
const SHELL=['/app','/manifest.webmanifest','/icon-192.png','/logo.svg'];
self.addEventListener('install',e=>{e.waitUntil(caches.open(CACHE).then(c=>c.addAll(SHELL)).then(()=>self.skipWaiting()));});
self.addEventListener('activate',e=>{e.waitUntil(self.clients.claim());});
self.addEventListener('fetch',e=>{const p=new URL(e.request.url).pathname;
if(/(\/status\.json|\/(set|weather|station|clock|next|cycle|clear|refresh))/.test(p))return;
e.respondWith(caches.match(e.request).then(r=>r||fetch(e.request)));});
)SWJS";

// POSIX timezone for the clock — Mountain Time (Greeley, CO), auto DST.
#define TZ_INFO "MST7MDT,M3.2.0,M11.1.0"

// ---- Wi-Fi ----
const char* ssid     = SECRET_SSID;
const char* password = SECRET_PASS;

// ---- Pins ----
#define EPD_CS    5
#define EPD_DC    19
#define EPD_RST   16
#define EPD_BUSY  4
#define PIN_BTN   27    // momentary button (to GND) — cycles screens
#define PIN_BATT  34    // 18650 voltage via 2:1 divider (ADC1, input-only)
#define BATT_RATIO 2.0  // divider multiplier (100k/100k = 2.0)

// ---- Panel: FPC-A002 2.13" 250x122, SSD1680 ----
// If this comes up blank/garbled, try _213_B74, then _213_B73, then _213_B72.
GxEPD2_BW<GxEPD2_213_BN, GxEPD2_213_BN::HEIGHT> display(
    GxEPD2_213_BN(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

AsyncWebServer server(80);

String currentText  = "Hello World!";
String ipText       = "";   // set once Wi-Fi connects; shown bottom-left always

// ---- Display mode + weather state ----
enum DisplayMode { MODE_TEXT, MODE_WEATHER, MODE_STATION, MODE_CLOCK };
DisplayMode mode    = MODE_CLOCK;   // clock is the default view
Preferences prefs;                  // NVS: remembers last mode + ZIP across power loss
int lastClockMin = -1;              // last minute drawn, so the clock redraws on change
String weatherZip   = "";   // last ZIP requested
String weatherText  = "";   // short summary, shown on the web page

// Parsed OpenWeatherMap result for the current ZIP.
struct Wx {
  String city, cond, icon, detail;
  int    temp = 0, feels = 0, humidity = 0, wind = 0;
  bool   valid = false;
} wx;

// Live data pushed by the Ambient Weather console to /data/report/ (its
// "Customized" upload, AmbientWeather protocol). NAN = field not reported.
struct Station {
  float tempf = NAN, humidity = NAN, windmph = NAN, gustmph = NAN;
  float dailyrain = NAN, baromin = NAN;
  int   winddir = -1;
  unsigned long lastUpdate = 0;   // millis() of last push
  bool  received = false;
} st;

String reqLog;     // recent /data/report hits (newest first), viewable at /debug
String lastBody;   // raw body of the most recent push, viewable at /debug
String lastReq;    // full dump (method/url/headers/params/body) of last /data/report hit

// All panel drawing + the weather fetch run from loop(), never inside an async
// web handler — that keeps blocking SPI/TLS work off the AsyncTCP task and
// avoids two tasks touching the display at once. Handlers just queue an action.
enum Pending { P_NONE, P_TEXT, P_WEATHER, P_CLEAR, P_STATION, P_DEEP, P_CLOCK, P_CYCLE };
volatile Pending pending = P_NONE;

// Battery monitor — auto-shows only when a plausible 18650 voltage is present.
float battVolts = 0; int battPct = -1; bool battValid = false;
unsigned long lastBatt = 0;
const unsigned long BATT_MS = 30UL * 1000UL;

// Screen cycling (auto). Physical button advances manually regardless.
bool autoCycle = false;
unsigned long lastCycle = 0;
const unsigned long CYCLE_MS      = 12UL * 1000UL;   // normal dwell per screen
const unsigned long TEXT_DWELL_MS = 30UL * 1000UL;   // longer dwell when text has notes
int btnReading = HIGH, btnStable = HIGH; unsigned long btnTime = 0;

// Text paging — scroll a 4-line window when the message is taller than the panel.
// Scroll steps use a PARTIAL refresh (no de-ghost flash) and stop at the bottom.
#define TEXT_VISIBLE_LINES 4
int textScroll = 0; unsigned long lastScroll = 0;
unsigned long scrollDoneAt = 0;                      // millis when scroll reached the last line
const unsigned long SCROLL_MS      = 3UL * 1000UL;   // per-line scroll cadence
const unsigned long SCROLL_HOLD_MS = 5UL * 1000UL;   // wait after the last line before auto-cycle

unsigned long lastWxFetch = 0;
const unsigned long WX_REFRESH_MS = 5UL * 60UL * 1000UL;  // refresh weather every 5 min

unsigned long lastDeep = 0;
const unsigned long DEEP_REFRESH_MS = 6UL * 60UL * 60UL * 1000UL;  // scrub ghosting every 6 h

// Station "waiting for data" handling: show a live counter, then fall back so the
// panel never looks hung if the console isn't pushing yet.
unsigned long stationModeSince = 0;
unsigned long lastWaitDraw     = 0;
const unsigned long STATION_WAIT_MS = 90UL * 1000UL;   // give up waiting after 90 s
const unsigned long WAIT_REDRAW_MS  = 20UL * 1000UL;   // refresh the waiting screen every 20 s

// Line spacing for the message font FreeSansBold12pt7b (~17px glyphs + spacing).
#define LINE_HEIGHT 22

// Keep only printable ASCII (0x20-0x7E). The 12pt font has no glyphs for the
// degree sign / wind arrows wttr.in returns, so strip them before drawing.
String asciiOnly(const String& s) {
  String out; out.reserve(s.length());
  for (uint16_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c >= 0x20 && c <= 0x7E) out += c;
  }
  out.trim();
  return out;
}

// Truncate to n chars with a trailing '.' if clipped (keeps long city/condition
// strings from overflowing the panel width).
String truncate(const String& s, uint8_t n) {
  if (s.length() <= n) return s;
  return s.substring(0, n - 1) + ".";
}

// ---- Battery: read the 18650 via the divider; auto-detect if wired ----
void readBattery() {
  uint32_t mv = analogReadMilliVolts(PIN_BATT);   // factory-calibrated on ESP32
  float v = mv * BATT_RATIO / 1000.0;
  if (v > 2.8 && v < 4.5) {                        // plausible cell -> battery present
    battVolts = v;
    battPct   = constrain((int)round((v - 3.30) / (4.20 - 3.30) * 100.0), 0, 100);
    battValid = true;
  } else {
    battValid = false;                             // floating pin / no cell -> hide
  }
}

// ---- Line helpers for the text screen ----
uint8_t lineCount(const String& s) {
  uint8_t n = 1;
  for (uint16_t i = 0; i < s.length(); i++) if (s[i] == '\n') n++;
  return n;
}
String nthLine(const String& s, uint8_t n) {
  int start = 0;
  for (uint8_t i = 0; i < n; i++) {
    int nl = s.indexOf('\n', start);
    if (nl < 0) return "";
    start = nl + 1;
  }
  int nl = s.indexOf('\n', start);
  return (nl < 0) ? s.substring(start) : s.substring(start, nl);
}
bool hasNotes() { String t = currentText; t.trim(); return t.length() > 0; }

// Push a line onto the small in-memory request log shown at /debug.
void logReq(const String& s) {
  reqLog = s + "\n" + reqLog;
  if (reqLog.length() > 1800) reqLog = reqLog.substring(0, 1800);
}

// Pull key=value out of a urlencoded body (fallback when params aren't parsed).
String fromBody(const String& body, const char* key) {
  String k = String(key) + "=";
  int i = body.indexOf(k);
  if (i < 0) return String();
  i += k.length();
  int e = body.indexOf('&', i);
  return body.substring(i, e < 0 ? body.length() : e);
}

// ---- Overlays drawn on every screen: IP (bottom-right) + battery (top-right) ----
// Call from inside a firstPage/nextPage loop. Built-in 6x8 font; 6 px/glyph.
void drawOverlays() {
  display.setFont(NULL);
  display.setTextSize(1);
  display.setTextColor(GxEPD_BLACK);

  // IP, bottom-right
  if (ipText.length())
    { display.setCursor(display.width() - ipText.length() * 6 - 2, display.height() - 8);
      display.print(ipText); }

  // Battery, top-right: "82% [###  ]"
  if (battValid) {
    String p = String(battPct) + "%";
    int by = 2, bodyW = 16, bodyH = 9;
    int iconX = display.width() - bodyW - 4;          // leave 2px for the nub
    int pctX  = iconX - (int)p.length() * 6 - 3;
    display.setCursor(pctX, by + 1); display.print(p);
    display.drawRect(iconX, by, bodyW, bodyH, GxEPD_BLACK);
    display.fillRect(iconX + bodyW, by + 2, 2, bodyH - 4, GxEPD_BLACK);   // nub
    display.fillRect(iconX + 1, by + 1, (bodyW - 2) * battPct / 100, bodyH - 2, GxEPD_BLACK);
  }
}

// ---- Blank the panel to all-white (IP label stays) ----
void clearDisplay() {
  display.setRotation(1);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawOverlays();
  } while (display.nextPage());
  display.hibernate();  // panel sleeps between updates -> no burn-in
}

// ---- Draw msg with '\n' line breaks. Centers if <=4 lines; otherwise shows a
//      4-line window from textScroll (clamped, non-wrapping) with a position
//      indicator. `partial=true` uses a partial refresh (no de-ghost flash) —
//      used for scroll steps so they just clear+redraw without the black flash.
void drawText(const String& msg, bool partial = false) {
  display.setRotation(1);
  display.setTextColor(GxEPD_BLACK);
  if (partial) display.setPartialWindow(0, 0, display.width(), display.height());
  else         display.setFullWindow();

  uint8_t total     = lineCount(msg);
  bool    scroll    = total > TEXT_VISIBLE_LINES;
  uint8_t maxScroll = scroll ? (uint8_t)(total - TEXT_VISIBLE_LINES) : 0;
  uint8_t shown     = scroll ? TEXT_VISIBLE_LINES : total;
  uint8_t startLine = scroll ? (uint8_t)min((int)textScroll, (int)maxScroll) : 0;

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&FreeSansBold12pt7b);   // re-select each page; overlays use the built-in font

    int16_t yTop = scroll ? 6 : (display.height() - shown * LINE_HEIGHT) / 2;

    for (uint8_t k = 0; k < shown; k++) {
      String line = nthLine(msg, startLine + k);
      int16_t tbx, tby; uint16_t tbw, tbh;
      display.getTextBounds(line, 0, 0, &tbx, &tby, &tbw, &tbh);
      int16_t x = (display.width() - tbw) / 2 - tbx;
      int16_t y = yTop + k * LINE_HEIGHT - tby;
      display.setCursor(x, y);
      display.print(line);
    }

    if (scroll) {   // "topLine/total" position indicator, bottom-left
      display.setFont(NULL); display.setTextSize(1);
      display.setCursor(2, display.height() - 8);
      display.print(String(startLine + 1) + "/" + String(total));
    }

    drawOverlays();
  } while (display.nextPage());
  // Don't hibernate during a scrolling sequence: hibernate wipes the controller's
  // previous-frame RAM, so the next partial update can't erase the old lines and
  // they overlay. Stay powered (initial full draw + all partial steps) so each
  // partial cleanly clears the prior screen. Non-scrolling text hibernates.
  if (!scroll) display.hibernate();
}

// ===== Weather icons: 1-bit glyphs drawn with GFX primitives, centered (cx,cy) =====
void drawSun(int cx, int cy, int r) {
  display.fillCircle(cx, cy, r, GxEPD_BLACK);
  for (int a = 0; a < 360; a += 45) {
    float t = a * 0.0174533f;
    display.drawLine(cx + cos(t) * (r + 4), cy + sin(t) * (r + 4),
                     cx + cos(t) * (r + 11), cy + sin(t) * (r + 11), GxEPD_BLACK);
  }
}
void drawCloud(int cx, int cy, int s) {  // filled silhouette; s ~ half-width
  display.fillCircle(cx - s * 0.5, cy, s * 0.42, GxEPD_BLACK);
  display.fillCircle(cx + s * 0.5, cy, s * 0.5,  GxEPD_BLACK);
  display.fillCircle(cx,           cy - s * 0.4, s * 0.55, GxEPD_BLACK);
  display.fillRect(cx - s * 0.5, cy, s, s * 0.5, GxEPD_BLACK);
}
void drawRain(int cx, int cy) {
  for (int i = -1; i <= 1; i++) display.drawLine(cx + i * 10, cy, cx + i * 10 - 4, cy + 9, GxEPD_BLACK);
}
void drawSnow(int cx, int cy) {
  for (int i = -1; i <= 1; i++) display.fillCircle(cx + i * 10, cy + 4, 2, GxEPD_BLACK);
}
void drawBolt(int cx, int cy) {
  display.fillTriangle(cx - 4, cy - 6, cx + 5, cy - 6, cx - 2, cy + 3, GxEPD_BLACK);
  display.fillTriangle(cx + 4, cy - 1, cx - 3, cy + 9, cx + 2, cy + 1, GxEPD_BLACK);
}
void drawMist(int cx, int cy) {
  for (int i = 0; i < 4; i++) display.drawFastHLine(cx - 18, cy - 9 + i * 7, 36, GxEPD_BLACK);
}
// Map an OpenWeatherMap icon code ("01d","10n",...) to a glyph.
void drawWeatherIcon(int cx, int cy, const String& code) {
  String k = code.substring(0, 2);
  if      (k == "01") drawSun(cx, cy, 15);
  else if (k == "02") { drawSun(cx - 9, cy - 9, 10); drawCloud(cx + 5, cy + 7, 28); }
  else if (k == "03" || k == "04") drawCloud(cx, cy, 32);
  else if (k == "09" || k == "10") { drawCloud(cx, cy - 7, 28); drawRain(cx, cy + 16); }
  else if (k == "11") { drawCloud(cx, cy - 7, 28); drawBolt(cx, cy + 13); }
  else if (k == "13") { drawCloud(cx, cy - 7, 28); drawSnow(cx, cy + 14); }
  else if (k == "50") drawMist(cx, cy);
  else drawCloud(cx, cy, 30);
}

// ---- Fetch current weather for weatherZip from OpenWeatherMap ----
bool fetchWeather() {
  if (weatherZip.length() == 0) return false;
  String url = "https://api.openweathermap.org/data/2.5/weather?zip=" + weatherZip +
               ",us&units=imperial&appid=" + OWM_API_KEY;
  WiFiClientSecure client;
  client.setInsecure();              // skip cert validation (home project)
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  int code = -1; String body;
  if (http.begin(client, url)) {
    code = http.GET();
    if (code == HTTP_CODE_OK) body = http.getString();
    http.end();
  }
  if (code != HTTP_CODE_OK || body.length() == 0) {
    Serial.printf("OWM fetch failed: code=%d\n", code);
    wx.valid = false;
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, body)) { Serial.println("OWM JSON parse error"); wx.valid = false; return false; }

  wx.city     = asciiOnly(String((const char*)(doc["name"] | "")));
  wx.temp     = lround((float)(doc["main"]["temp"]       | 0.0f));
  wx.feels    = lround((float)(doc["main"]["feels_like"] | 0.0f));
  wx.humidity = (int)(doc["main"]["humidity"]            | 0);
  wx.wind     = lround((float)(doc["wind"]["speed"]      | 0.0f));
  wx.cond     = asciiOnly(String((const char*)(doc["weather"][0]["description"] | "")));
  wx.icon     = String((const char*)(doc["weather"][0]["icon"] | "01d"));
  if (wx.cond.length()) wx.cond.setCharAt(0, toupper(wx.cond[0]));
  wx.detail   = "Feels " + String(wx.feels) + "  Hum " + String(wx.humidity) +
                "%  Wind " + String(wx.wind);
  wx.valid    = true;

  weatherText = wx.city + " " + String(wx.temp) + "F, " + wx.cond;
  Serial.println("Weather: " + weatherText);
  return true;
}

// ---- Fancy weather layout: city / icon / big temp / condition / details ----
void drawWeather() {
  display.setRotation(1);
  display.setTextColor(GxEPD_BLACK);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // City header + divider rule.
    display.setFont(&FreeSansBold9pt7b);
    display.setCursor(5, 15);
    display.print(truncate(wx.city, 24));
    display.drawFastHLine(0, 20, display.width(), GxEPD_BLACK);

    // Weather icon (left).
    drawWeatherIcon(40, 60, wx.icon);

    // Big temperature with a hand-drawn degree ring + small "F" (right).
    display.setFont(&FreeSansBold24pt7b);
    String t = String(wx.temp);
    int16_t bx, by; uint16_t bw, bh;
    display.getTextBounds(t, 0, 0, &bx, &by, &bw, &bh);
    int tx = 96, base = 62;
    display.setCursor(tx, base);
    display.print(t);
    int dx = tx + bw + 8, dy = base - bh + 4;
    display.drawCircle(dx, dy, 4, GxEPD_BLACK);
    display.drawCircle(dx, dy, 3, GxEPD_BLACK);
    display.setFont(&FreeSansBold12pt7b);
    display.setCursor(dx + 7, base);
    display.print("F");

    // Condition text under the temperature.
    display.setFont(&FreeSansBold9pt7b);
    display.setCursor(94, 84);
    display.print(truncate(wx.cond, 20));

    // Detail line (feels-like / humidity / wind) across the bottom.
    display.setCursor(5, 106);
    display.print(wx.detail);

    drawOverlays();  // IP stays bottom-left
  } while (display.nextPage());
  display.hibernate();
}

// ---- Fetch + render, or show an error on the panel ----
void fetchAndDrawWeather() {
  if (fetchWeather()) {
    drawWeather();
  } else {
    weatherText = "unavailable (ZIP " + weatherZip + ")";
    drawText("Weather\nunavailable\nZIP " + weatherZip);
  }
}

// 16-point compass label for a wind direction in degrees.
String compass(int deg) {
  if (deg < 0) return "";
  static const char* p[] = {"N","NNE","NE","ENE","E","ESE","SE","SSE",
                            "S","SSW","SW","WSW","W","WNW","NW","NNW"};
  return p[((deg + 11) / 22) % 16];
}

// One-line station summary for the web page (or a "no data yet" note).
String stationSummary() {
  if (!st.received) return "no data yet";
  String s = String((int)round(st.tempf)) + "F";
  if (!isnan(st.humidity)) s += ", Hum " + String((int)round(st.humidity)) + "%";
  if (!isnan(st.windmph))  s += ", Wind " + String((int)round(st.windmph));
  unsigned long age = (millis() - st.lastUpdate) / 1000;
  s += "  (" + String(age) + "s ago)";
  return s;
}

// ---- Backyard station layout: big temp (left) + stat lines (right) ----
void drawStation() {
  if (!st.received) {
    unsigned long s = (millis() - stationModeSince) / 1000;
    drawText("Waiting for\nstation push\n(" + String(s) + "s)");
    return;
  }
  display.setRotation(1);
  display.setTextColor(GxEPD_BLACK);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    display.setFont(&FreeSansBold9pt7b);
    display.setCursor(5, 15);
    display.print("Backyard Station");
    display.drawFastHLine(0, 20, display.width(), GxEPD_BLACK);

    // Big outdoor temperature (left).
    display.setFont(&FreeSansBold24pt7b);
    String t = String((int)round(st.tempf));
    int16_t bx, by; uint16_t bw, bh;
    display.getTextBounds(t, 0, 0, &bx, &by, &bw, &bh);
    int tx = 8, base = 64;
    display.setCursor(tx, base);
    display.print(t);
    int dx = tx + bw + 7, dy = base - bh + 4;
    display.drawCircle(dx, dy, 4, GxEPD_BLACK);
    display.drawCircle(dx, dy, 3, GxEPD_BLACK);
    display.setFont(&FreeSansBold12pt7b);
    display.setCursor(dx + 6, base);
    display.print("F");

    // Stat column (right).
    display.setFont(&FreeSansBold9pt7b);
    int rx = 132, ry = 40;
    if (!isnan(st.humidity)) { display.setCursor(rx, ry); display.print("Hum  " + String((int)round(st.humidity)) + "%"); ry += 18; }
    if (!isnan(st.windmph)) {
      String w = "Wind " + String((int)round(st.windmph));
      if (st.winddir >= 0) w += " " + compass(st.winddir);
      display.setCursor(rx, ry); display.print(w); ry += 18;
    }
    if (!isnan(st.gustmph))   { display.setCursor(rx, ry); display.print("Gust " + String((int)round(st.gustmph)) + " mph"); ry += 18; }
    if (!isnan(st.dailyrain)) { display.setCursor(rx, ry); display.print("Rain " + String(st.dailyrain, 2) + "\""); ry += 18; }

    // Pressure across the bottom (left).
    if (!isnan(st.baromin)) {
      display.setFont(&FreeSansBold9pt7b);
      display.setCursor(8, 104);
      display.print(String(st.baromin, 2) + " inHg");
    }

    drawOverlays();
  } while (display.nextPage());
  display.hibernate();
}

// ---- Deep refresh: flash black<->white to scrub e-paper ghosting ----
void deepRefresh() {
  display.setRotation(1);
  display.setFullWindow();
  for (int i = 0; i < 2; i++) {
    display.firstPage(); do { display.fillScreen(GxEPD_BLACK); } while (display.nextPage());
    display.firstPage(); do { display.fillScreen(GxEPD_WHITE); } while (display.nextPage());
  }
  display.hibernate();
}

// ---- Clock: big time + date, centered (default view) ----
void drawClock() {
  struct tm ti;
  bool haveTime = getLocalTime(&ti, 200);

  display.setRotation(1);
  display.setTextColor(GxEPD_BLACK);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    if (!haveTime) {
      display.setFont(&FreeSansBold12pt7b);
      const char* m = "Syncing time...";
      int16_t bx, by; uint16_t bw, bh;
      display.getTextBounds(m, 0, 0, &bx, &by, &bw, &bh);
      display.setCursor((display.width() - bw) / 2 - bx, 70);
      display.print(m);
    } else {
      char tb[16], db[28];
      strftime(tb, sizeof tb, "%I:%M %p", &ti);     // 10:42 AM
      strftime(db, sizeof db, "%a, %b %d %Y", &ti);  // Wed, Jun 24 2026
      String tStr = tb; if (tStr.startsWith("0")) tStr.remove(0, 1);  // drop leading zero hour

      int16_t bx, by; uint16_t bw, bh;
      display.setFont(&FreeSansBold24pt7b);
      display.getTextBounds(tStr, 0, 0, &bx, &by, &bw, &bh);
      display.setCursor((display.width() - bw) / 2 - bx, 60);
      display.print(tStr);

      display.setFont(&FreeSansBold12pt7b);
      display.getTextBounds(db, 0, 0, &bx, &by, &bw, &bh);
      display.setCursor((display.width() - bw) / 2 - bx, 95);
      display.print(db);
    }
    drawOverlays();
  } while (display.nextPage());
  display.hibernate();
  if (haveTime) lastClockMin = ti.tm_min;
}

// ---- Persist the view (mode + ZIP) to NVS so it survives power loss ----
void saveState() {
  prefs.putInt("mode", (int)mode);
  prefs.putString("zip", weatherZip);
  prefs.putString("text", currentText);   // remember the message box text
  prefs.putBool("cycle", autoCycle);
}

// ---- Redraw whatever the current mode should show ----
void redrawCurrent() {
  if      (mode == MODE_WEATHER) { if (wx.valid) drawWeather(); else fetchAndDrawWeather(); }
  else if (mode == MODE_STATION) drawStation();
  else if (mode == MODE_CLOCK)   drawClock();
  else                           drawText(currentText);
}

// ---- Switch to a mode (resets its per-mode timers) and redraw ----
void enterMode(DisplayMode m) {
  mode = m;
  if (m == MODE_STATION) { stationModeSince = millis(); lastWaitDraw = 0; }
  if (m == MODE_TEXT)    { textScroll = 0; lastScroll = millis(); scrollDoneAt = 0; }
  saveState();
  redrawCurrent();
  lastCycle = millis();
}

// ---- Advance to the next screen in the cycle order ----
void cycleScreen() {
  static const DisplayMode order[4] = { MODE_CLOCK, MODE_TEXT, MODE_WEATHER, MODE_STATION };
  int idx = 0;
  for (int i = 0; i < 4; i++) if (order[i] == mode) idx = i;
  enterMode(order[(idx + 1) % 4]);
}

const char* modeName() {
  switch (mode) {
    case MODE_WEATHER: return "weather";
    case MODE_STATION: return "station";
    case MODE_TEXT:    return "text";
    default:           return "clock";
  }
}

// ---- Three Oak Woods themed control page ----
String page() {
  String status;
  if (mode == MODE_WEATHER)
    status = "Weather \xE2\x80\x94 " + (weatherText.length() ? weatherText : ("ZIP " + weatherZip));
  else if (mode == MODE_STATION)
    status = "Station \xE2\x80\x94 " + stationSummary();
  else if (mode == MODE_CLOCK)
    status = "Clock (date & time)";
  else
    status = currentText;

  String h =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Three Oak Woods Display</title>"
    "<link rel='icon' type='image/svg+xml' href='/logo.svg'>"
    "<link rel='preconnect' href='https://fonts.googleapis.com'>"
    "<link href='https://fonts.googleapis.com/css2?family=Nunito:wght@400;700;800&display=swap' rel='stylesheet'>"
    "<style>"
    ":root{--green:#2C654B;--cream:#F9E7DF;--amber:#C8852A;--bark:#2B3A33;--parchment:#F5F1E6}"
    "*{box-sizing:border-box}"
    "body{font-family:'Nunito',system-ui,sans-serif;color:var(--bark);background:var(--parchment);margin:0;padding:0 16px 40px}"
    ".wrap{max-width:480px;margin:0 auto}"
    "header{display:flex;align-items:center;gap:14px;padding:22px 0 8px}"
    "header img{width:56px;height:56px}"
    "header h1{font-size:1.25em;font-weight:800;margin:0;line-height:1.1}"
    "header .sub{color:var(--green);font-weight:700;font-size:.78em;letter-spacing:.05em;text-transform:uppercase}"
    ".status{background:var(--cream);border-left:4px solid var(--amber);border-radius:8px;padding:10px 14px;margin:6px 0 18px;white-space:pre-wrap}"
    ".card{background:#fff;border:1px solid #e6ddca;border-radius:12px;padding:16px;margin:0 0 16px;box-shadow:0 1px 2px rgba(43,58,51,.06)}"
    ".card h2{margin:0 0 10px;font-size:1.05em;color:var(--green)}"
    "label{font-weight:700;font-size:.9em;display:block;margin:0 0 4px}"
    "textarea,input,button{font-family:inherit;font-size:1.05em;padding:11px;width:100%;border-radius:8px;border:1px solid #cfc6b3;margin:4px 0}"
    "textarea{resize:vertical}"
    "button{border:0;background:var(--green);color:var(--cream);font-weight:800;cursor:pointer}"
    "button:hover{filter:brightness(1.08)}"
    "button.amber{background:var(--amber)}"
    "button.clear{background:#fff;color:var(--bark);border:1px solid #cfc6b3;font-weight:700}"
    "footer{text-align:center;color:var(--green);font-size:.8em;margin-top:8px;opacity:.85}"
    "</style></head><body><div class='wrap'>"
    "<header><img src='/logo.svg' alt='Three Oak Woods'>"
    "<div><div class='sub'>Three Oak Woods</div><h1>E-Paper Display</h1></div></header>"
    "<div class='status'>Currently showing: <b>" + status + "</b>" +
      (battValid ? ("<br>Battery: " + String(battPct) + "% (" + String(battVolts, 2) + " V)") : String("")) +
      "<br>Auto-cycle: <b>" + String(autoCycle ? "ON" : "off") + "</b></div>"
    "<div style='display:flex;gap:10px;margin:0 0 16px'>"
    "<form action='/next' method='get' style='flex:1'><button type='submit' class='clear'>Next screen \xE2\x86\x92</button></form>"
    "<form action='/cycle' method='get' style='flex:1'><button type='submit'>" + String(autoCycle ? "Stop cycling" : "Auto-cycle") + "</button></form></div>"
    "<div class='card'><h2>Message</h2>"
    "<form action='/set' method='get'>"
    "<label>Message (scrolls if over 4 lines)</label>"
    "<textarea name='msg' rows='6' maxlength='400' placeholder='Type text... (Enter for a new line)'>" + currentText + "</textarea>"
    "<button type='submit'>Update Display</button></form>"
    "<form action='/clock' method='get'><button type='submit' class='clear'>Show Clock (default)</button></form></div>"
    "<div class='card'><h2>Weather</h2>"
    "<form action='/weather' method='get'>"
    "<label>ZIP code</label>"
    "<input name='zip' inputmode='numeric' pattern='[0-9]{5}' maxlength='5' required placeholder='e.g. 94103' value='" + weatherZip + "'>"
    "<button type='submit' class='amber'>Show Weather</button></form></div>"
    "<div class='card'><h2>Backyard Station</h2>"
    "<p style='margin:0 0 8px;color:#5b5b50;font-size:.9em'>Live from your Ambient Weather console (" + stationSummary() + ").</p>"
    "<form action='/station' method='get'>"
    "<button type='submit'>Show Station</button></form></div>"
    "<div style='display:flex;gap:10px'>"
    "<form action='/clear' method='get' style='flex:1'>"
    "<button type='submit' class='clear'>Clear Screen</button></form>"
    "<form action='/refresh' method='get' style='flex:1'>"
    "<button type='submit' class='clear'>Clean (de-ghost)</button></form></div>"
    "<p style='text-align:center;margin:14px 0 4px'>"
    "<a href='/app' style='color:var(--green);font-weight:800;text-decoration:none'>Open the web app \xE2\x86\x97</a></p>"
    "<footer>Three Oak Woods \xC2\xB7 threeoakwoods.com</footer>"
    "</div></body></html>";
  return h;
}

void setup() {
  Serial.begin(115200);

  display.init(115200);
  pinMode(PIN_BTN, INPUT_PULLUP);   // momentary button to GND
  readBattery();                    // so the first draw shows battery if wired

  WiFi.mode(WIFI_STA);
#ifdef USE_STATIC_IP
  // Static IP — this gateway (T-Mobile 5G) has no DHCP reservation option.
  if (!WiFi.config(IPAddress(STATIC_IP), IPAddress(STATIC_GATEWAY), IPAddress(STATIC_SUBNET),
                   IPAddress(STATIC_DNS1), IPAddress(STATIC_DNS2)))
    Serial.println("Static IP config failed; falling back to DHCP");
#endif
  WiFi.begin(ssid, password);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) { delay(400); Serial.print("."); }
  Serial.println();
  ipText = WiFi.localIP().toString();   // bottom-left label needs this
  Serial.print("Ready. Browse to: http://");
  Serial.println(ipText);

  configTzTime(TZ_INFO, "pool.ntp.org", "time.nist.gov");  // start NTP for the clock

  // Restore last view from NVS: a saved ZIP defaults back to weather, a saved
  // message restores the text screen, otherwise the default clock view.
  prefs.begin("epaper", false);
  weatherZip  = prefs.getString("zip", "");
  currentText = prefs.getString("text", currentText);   // remembered message box text
  autoCycle   = prefs.getBool("cycle", false);
  int savedMode = prefs.getInt("mode", MODE_CLOCK);
  if (savedMode == MODE_WEATHER && weatherZip.length()) {
    mode = MODE_WEATHER;
    fetchAndDrawWeather();
    lastWxFetch = millis();
  } else if (savedMode == MODE_TEXT) {
    mode = MODE_TEXT;
    drawText(currentText);
  } else {
    mode = MODE_CLOCK;
    drawClock();
  }
  lastDeep = millis();     // start the deep-refresh timer from boot

  // Allow the standalone web app (different origin) to read the API.
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send(200, "text/html", page());
  });

  server.on("/logo.svg", HTTP_GET, [](AsyncWebServerRequest* req){
    AsyncWebServerResponse* r = req->beginResponse(200, "image/svg+xml", THREEOAKWOODS_BADGE_SVG);
    r->addHeader("Cache-Control", "max-age=86400");
    req->send(r);
  });

  server.on("/app", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send(200, "text/html", WEBAPP_HTML);   // the bundled web app (PWA)
  });

  // ---- PWA assets so the app installs to the home screen ----
  server.on("/manifest.webmanifest", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send(200, "application/manifest+json", DEVICE_MANIFEST);
  });
  server.on("/sw.js", HTTP_GET, [](AsyncWebServerRequest* req){
    AsyncWebServerResponse* r = req->beginResponse(200, "application/javascript", DEVICE_SW);
    r->addHeader("Service-Worker-Allowed", "/");
    req->send(r);
  });
  auto sendIcon = [](AsyncWebServerRequest* req){
    AsyncWebServerResponse* r = req->beginResponse_P(200, "image/png",
        webapp_icon_192_png, webapp_icon_192_png_len);
    r->addHeader("Cache-Control", "max-age=86400");
    req->send(r);
  };
  server.on("/icon-192.png",        HTTP_GET, sendIcon);
  server.on("/apple-touch-icon.png", HTTP_GET, sendIcon);

  // Handlers only queue work; loop() does the drawing/fetching (off the async task).
  server.on("/set", HTTP_GET, [](AsyncWebServerRequest* req){
    if (req->hasParam("msg")) {
      currentText = req->getParam("msg")->value();
      currentText.replace("\r", "");  // textareas send \r\n; keep just \n
      if (currentText.length() == 0) currentText = " ";
    }
    mode = MODE_TEXT;
    textScroll = 0; lastScroll = millis(); scrollDoneAt = 0;
    saveState();
    pending = P_TEXT;
    req->redirect("/");
  });

  server.on("/clock", HTTP_GET, [](AsyncWebServerRequest* req){
    mode = MODE_CLOCK;
    saveState();
    pending = P_CLOCK;
    req->redirect("/");
  });

  server.on("/weather", HTTP_GET, [](AsyncWebServerRequest* req){
    if (req->hasParam("zip")) weatherZip = req->getParam("zip")->value();
    mode = MODE_WEATHER;
    saveState();              // remember the ZIP + weather view across power loss
    pending = P_WEATHER;
    req->redirect("/");
  });

  server.on("/station", HTTP_GET, [](AsyncWebServerRequest* req){
    mode = MODE_STATION;
    saveState();
    stationModeSince = millis();   // start the "waiting" timeout/counter
    lastWaitDraw = 0;
    pending = P_STATION;
    req->redirect("/");
  });

  server.on("/refresh", HTTP_GET, [](AsyncWebServerRequest* req){
    pending = P_DEEP;              // de-ghost, then redraw current mode
    req->redirect("/");
  });

  server.on("/next", HTTP_GET, [](AsyncWebServerRequest* req){
    pending = P_CYCLE;            // advance one screen (drawn in loop)
    req->redirect("/");
  });

  server.on("/cycle", HTTP_GET, [](AsyncWebServerRequest* req){
    autoCycle = !autoCycle;
    prefs.putBool("cycle", autoCycle);
    lastCycle = millis();
    req->redirect("/");
  });

  server.on("/clear", HTTP_GET, [](AsyncWebServerRequest* req){
    pending = P_CLEAR;
    req->redirect("/");
  });

  // Ingest pushes from the Ambient Weather console's "Customized" upload.
  // The console may send GET (fields in the query string) or POST (fields in the
  // body), so read a field from query -> POST param -> raw body, in that order.
  auto onReport = [](AsyncWebServerRequest* req){
    // Full dump of exactly what arrived, so we can see how the console talks.
    String dump = String(req->methodToString()) + " " + req->url() + "\n";
    int nh = req->headers();
    for (int i = 0; i < nh; i++) { const AsyncWebHeader* hh = req->getHeader(i); dump += "  " + hh->name() + ": " + hh->value() + "\n"; }
    int np = req->params();
    for (int i = 0; i < np; i++) { const AsyncWebParameter* pp = req->getParam(i); dump += "  param " + pp->name() + "=" + pp->value() + (pp->isPost() ? " [POST]" : " [GET]") + "\n"; }
    dump += "  body[" + String(lastBody.length()) + "]: " + lastBody + "\n";
    lastReq = dump;

    // The AMBWeatherPro console joins params with '&' instead of '?', so there's
    // no query delimiter and nothing parses normally. Grab the raw param tail
    // (everything after the first '?' or '&' in the URL) and parse it ourselves.
    String url = req->url();
    int qi = url.indexOf('?');
    if (qi < 0) qi = url.indexOf('&');
    String urlq = (qi >= 0) ? url.substring(qi + 1) : "";

    auto rd = [&](const char* k)->String {
      if (req->hasParam(k))        return req->getParam(k)->value();        // GET query
      if (req->hasParam(k, true))  return req->getParam(k, true)->value();  // POST form
      String v = fromBody(urlq, k);  if (v.length()) return v;             // '&'-joined URL tail
      return fromBody(lastBody, k);                                         // raw body fallback
    };
    String t = rd("tempf");
    if (t.length())                 st.tempf     = t.toFloat();
    String h = rd("humidity");      if (h.length()) st.humidity  = h.toFloat();
    String w = rd("windspeedmph");  if (w.length()) st.windmph   = w.toFloat();
    String g = rd("windgustmph");   if (g.length()) st.gustmph   = g.toFloat();
    String r = rd("dailyrainin");   if (r.length()) st.dailyrain = r.toFloat();
    String b = rd("baromrelin");    if (b.length()) st.baromin   = b.toFloat();
    String d = rd("winddir");       if (d.length()) st.winddir   = d.toInt();

    bool got = t.length() > 0;
    if (got) {
      st.lastUpdate = millis();
      st.received = true;
      if (mode == MODE_STATION) pending = P_STATION;   // live-refresh the panel
    }

    String src = req->client() ? req->client()->remoteIP().toString() : "?";
    logReq(String(millis() / 1000) + "s " + req->methodToString() + " from " + src +
           "  query=" + String(req->params()) + " bodyLen=" + String(lastBody.length()) +
           (got ? ("  tempf=" + t) : "  NO FIELDS PARSED"));
    Serial.printf("/data/report from %s parsed=%d tempf=%s\n", src.c_str(), got, t.c_str());

    req->send(200, "text/plain", "OK");                // console expects 200
  };
  // onBody captures the raw POST body so we can see/parse it even if the
  // content-type isn't auto-parsed into params.
  auto onBody = [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total){
    if (index == 0) lastBody = "";
    for (size_t i = 0; i < len && lastBody.length() < 400; i++) lastBody += (char)data[i];
  };
  server.on("/data/report/", HTTP_ANY, onReport, NULL, onBody);
  server.on("/data/report",  HTTP_ANY, onReport, NULL, onBody);   // tolerate missing trailing slash

  // Machine-readable status for the web app (CORS-enabled below).
  server.on("/status.json", HTTP_GET, [](AsyncWebServerRequest* req){
    JsonDocument d;
    d["mode"]      = modeName();
    d["ip"]        = ipText;
    d["autoCycle"] = autoCycle;
    d["text"]      = currentText;
    d["battValid"] = battValid;
    d["battPct"]   = battPct;
    d["battVolts"] = battVolts;
    JsonObject w = d["weather"].to<JsonObject>();
    w["zip"] = weatherZip; w["valid"] = wx.valid; w["city"] = wx.city;
    w["temp"] = wx.temp; w["feels"] = wx.feels; w["humidity"] = wx.humidity;
    w["wind"] = wx.wind; w["icon"] = wx.icon; w["cond"] = wx.cond;
    w["summary"] = weatherText;
    JsonObject s = d["station"].to<JsonObject>();
    s["received"] = st.received;
    if (!isnan(st.tempf))     s["tempf"]     = st.tempf;
    if (!isnan(st.humidity))  s["humidity"]  = st.humidity;
    if (!isnan(st.windmph))   s["windmph"]   = st.windmph;
    if (!isnan(st.gustmph))   s["gustmph"]   = st.gustmph;
    if (!isnan(st.dailyrain)) s["dailyrain"] = st.dailyrain;
    if (!isnan(st.baromin))   s["baromin"]   = st.baromin;
    if (st.winddir >= 0)      s["winddir"]   = st.winddir;
    s["summary"] = stationSummary();
    String out; serializeJson(d, out);
    req->send(200, "application/json", out);
  });

  // Diagnostics: who's hitting us and with what.
  server.on("/debug", HTTP_GET, [](AsyncWebServerRequest* req){
    String out = "=== recent /data/report hits (newest first) ===\n" + reqLog +
                 "\n=== full dump of last /data/report request ===\n" + lastReq + "\n";
    req->send(200, "text/plain", out);
  });
  server.onNotFound([](AsyncWebServerRequest* req){
    String src = req->client() ? req->client()->remoteIP().toString() : "?";
    logReq(String(millis() / 1000) + "s 404 " + req->methodToString() + " from " + src + "  " + req->url());
    req->send(404, "text/plain", "not found");
  });

  server.begin();
}

void loop() {
  // Execute whatever the web handlers queued (blocking SPI/TLS work lives here).
  Pending job = pending;
  if (job != P_NONE) {
    pending = P_NONE;
    if      (job == P_TEXT)    drawText(currentText);
    else if (job == P_WEATHER) { fetchAndDrawWeather(); lastWxFetch = millis(); }
    else if (job == P_STATION) drawStation();
    else if (job == P_CLOCK)   drawClock();
    else if (job == P_CLEAR)   clearDisplay();
    else if (job == P_CYCLE)   cycleScreen();
    else if (job == P_DEEP)    { deepRefresh(); redrawCurrent(); lastDeep = millis(); }
  }

  // Battery sample (cheap; just gates whether the overlay shows).
  if (millis() - lastBatt > BATT_MS) { readBattery(); lastBatt = millis(); }

  // Physical button (active-low, debounced) -> next screen.
  int reading = digitalRead(PIN_BTN);
  if (reading != btnReading) { btnReading = reading; btnTime = millis(); }
  if (millis() - btnTime > 40 && reading != btnStable) {
    btnStable = reading;
    if (btnStable == LOW) cycleScreen();
  }

  // Scroll the text screen one line at a time (PARTIAL refresh = no de-ghost
  // flash) until the last line is on screen, then hold.
  if (mode == MODE_TEXT) {
    uint8_t total = lineCount(currentText);
    if (total > TEXT_VISIBLE_LINES) {
      uint8_t maxScroll = total - TEXT_VISIBLE_LINES;
      if (textScroll < maxScroll && millis() - lastScroll > SCROLL_MS) {
        textScroll++;
        lastScroll = millis();
        drawText(currentText, true);                 // partial: clear + draw scrolled lines
        if (textScroll >= maxScroll) scrollDoneAt = millis();
      }
    }
  }

  // Auto-cycle: advance on a timer. On a scrolling text screen, don't move on
  // until SCROLL_HOLD_MS (5 s) after the last line has scrolled into view.
  if (autoCycle) {
    bool ready;
    if (mode == MODE_TEXT && lineCount(currentText) > TEXT_VISIBLE_LINES)
      ready = (scrollDoneAt != 0) && (millis() - scrollDoneAt > SCROLL_HOLD_MS);
    else if (mode == MODE_TEXT)
      ready = (millis() - lastCycle > TEXT_DWELL_MS);     // short note: fixed dwell
    else
      ready = (millis() - lastCycle > CYCLE_MS);
    if (ready) cycleScreen();
  }

  // Keep weather fresh while it's the active mode.
  if (mode == MODE_WEATHER && millis() - lastWxFetch > WX_REFRESH_MS) {
    fetchAndDrawWeather();
    lastWxFetch = millis();
  }

  // Tick the clock once per minute while it's showing.
  if (mode == MODE_CLOCK) {
    struct tm ti;
    if (getLocalTime(&ti, 0) && ti.tm_min != lastClockMin) drawClock();
  }

  // Station mode with no data yet: keep the "waiting" screen visibly counting,
  // then fall back so the panel never looks hung.
  if (mode == MODE_STATION && !st.received) {
    if (millis() - stationModeSince > STATION_WAIT_MS) {
      if (weatherZip.length()) { mode = MODE_WEATHER; fetchAndDrawWeather(); lastWxFetch = millis(); }
      else                     { mode = MODE_TEXT;    drawText(currentText); }
    } else if (millis() - lastWaitDraw > WAIT_REDRAW_MS) {
      drawStation();          // redraw waiting screen with updated counter
      lastWaitDraw = millis();
    }
  }

  // Periodically scrub e-paper ghosting, then restore the current view.
  if (millis() - lastDeep > DEEP_REFRESH_MS) {
    deepRefresh();
    redrawCurrent();
    lastDeep = millis();
  }

  delay(20);
}
