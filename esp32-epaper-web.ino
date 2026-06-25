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
#include <GxEPD2_BW.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include "secrets.h"   // SECRET_SSID / SECRET_PASS / OWM_API_KEY (gitignored)
#include "logo.h"      // THREEOAKWOODS_BADGE_SVG, served at /logo.svg

// ---- Wi-Fi ----
const char* ssid     = SECRET_SSID;
const char* password = SECRET_PASS;

// ---- Pins ----
#define EPD_CS    5
#define EPD_DC    19
#define EPD_RST   16
#define EPD_BUSY  4

// ---- Panel: FPC-A002 2.13" 250x122, SSD1680 ----
// If this comes up blank/garbled, try _213_B74, then _213_B73, then _213_B72.
GxEPD2_BW<GxEPD2_213_BN, GxEPD2_213_BN::HEIGHT> display(
    GxEPD2_213_BN(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

AsyncWebServer server(80);

String currentText  = "Hello World!";
String ipText       = "";   // set once Wi-Fi connects; shown bottom-left always

// ---- Display mode + weather state ----
enum DisplayMode { MODE_TEXT, MODE_WEATHER, MODE_STATION };
DisplayMode mode    = MODE_TEXT;
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

// All panel drawing + the weather fetch run from loop(), never inside an async
// web handler — that keeps blocking SPI/TLS work off the AsyncTCP task and
// avoids two tasks touching the display at once. Handlers just queue an action.
enum Pending { P_NONE, P_TEXT, P_WEATHER, P_CLEAR, P_STATION };
volatile Pending pending = P_NONE;

unsigned long lastWxFetch = 0;
const unsigned long WX_REFRESH_MS = 15UL * 60UL * 1000UL;  // refresh weather every 15 min

// Line spacing for FreeSansBold12pt7b (~17px glyphs + breathing room).
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

// ---- Draw the IP in the built-in 6x8 font, in BOTH bottom corners ----
// Must be called from inside a firstPage/nextPage paged-draw loop. The
// built-in font (setFont(NULL)) positions by the text's TOP-left; each glyph
// is 6 px wide, so the right copy is right-aligned by string length.
void drawIpLabel() {
  if (ipText.length() == 0) return;
  display.setFont(NULL);            // classic 6x8 GFX font
  display.setTextSize(1);
  display.setTextColor(GxEPD_BLACK);
  int y = display.height() - 8;
  display.setCursor(2, y);                                 // lower-left
  display.print(ipText);
  display.setCursor(display.width() - ipText.length() * 6 - 2, y);  // lower-right
  display.print(ipText);
}

// ---- Blank the panel to all-white (IP label stays) ----
void clearDisplay() {
  display.setRotation(1);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawIpLabel();
  } while (display.nextPage());
  display.hibernate();  // panel sleeps between updates -> no burn-in
}

// ---- Draw msg, honoring '\n' line breaks, block-centered ----
void drawText(const String& msg) {
  display.setRotation(1);
  display.setTextColor(GxEPD_BLACK);
  display.setFullWindow();

  // Count lines so we can center the whole block vertically.
  uint8_t lines = 1;
  for (uint16_t i = 0; i < msg.length(); i++)
    if (msg[i] == '\n') lines++;

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // Re-select the big font each page: drawIpLabel() switches to the small
    // built-in font, and paged drawing reruns this whole block per page.
    display.setFont(&FreeSansBold12pt7b);

    uint16_t blockH = lines * LINE_HEIGHT;
    int16_t  yTop   = (display.height() - blockH) / 2;  // top of first line's box

    int start = 0;
    for (uint8_t ln = 0; ln < lines; ln++) {
      int nl = msg.indexOf('\n', start);
      String line = (nl < 0) ? msg.substring(start) : msg.substring(start, nl);
      start = (nl < 0) ? msg.length() : nl + 1;

      // Center this line horizontally.
      int16_t tbx, tby; uint16_t tbw, tbh;
      display.getTextBounds(line, 0, 0, &tbx, &tby, &tbw, &tbh);
      int16_t x = (display.width() - tbw) / 2 - tbx;
      // Baseline: top of this line's box, shifted down past the glyph ascent.
      int16_t y = yTop + ln * LINE_HEIGHT - tby;
      display.setCursor(x, y);
      display.print(line);
    }

    drawIpLabel();  // always overlay the IP at bottom-left
  } while (display.nextPage());
  display.hibernate();  // panel sleeps between updates -> no burn-in
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

    drawIpLabel();  // IP stays bottom-left
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
    drawText("Waiting for\nstation data\n/data/report/");
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

    drawIpLabel();
  } while (display.nextPage());
  display.hibernate();
}

// ---- Three Oak Woods themed control page ----
String page() {
  String status;
  if (mode == MODE_WEATHER)
    status = "Weather \xE2\x80\x94 " + (weatherText.length() ? weatherText : ("ZIP " + weatherZip));
  else if (mode == MODE_STATION)
    status = "Station \xE2\x80\x94 " + stationSummary();
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
    "<div class='status'>Currently showing: <b>" + status + "</b></div>"
    "<div class='card'><h2>Text</h2>"
    "<form action='/set' method='get'>"
    "<label>Message</label>"
    "<textarea name='msg' rows='4' maxlength='120' placeholder='Type text... (Enter for a new line)'>" + currentText + "</textarea>"
    "<button type='submit'>Update Display</button></form></div>"
    "<div class='card'><h2>Weather</h2>"
    "<form action='/weather' method='get'>"
    "<label>ZIP code</label>"
    "<input name='zip' inputmode='numeric' pattern='[0-9]{5}' maxlength='5' required placeholder='e.g. 94103' value='" + weatherZip + "'>"
    "<button type='submit' class='amber'>Show Weather</button></form></div>"
    "<div class='card'><h2>Backyard Station</h2>"
    "<p style='margin:0 0 8px;color:#5b5b50;font-size:.9em'>Live from your Ambient Weather console (" + stationSummary() + ").</p>"
    "<form action='/station' method='get'>"
    "<button type='submit'>Show Station</button></form></div>"
    "<form action='/clear' method='get'>"
    "<button type='submit' class='clear'>Clear Screen</button></form>"
    "<footer>Three Oak Woods \xC2\xB7 threeoakwoods.com</footer>"
    "</div></body></html>";
  return h;
}

void setup() {
  Serial.begin(115200);

  display.init(115200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) { delay(400); Serial.print("."); }
  Serial.println();
  ipText = WiFi.localIP().toString();   // bottom-left label needs this
  Serial.print("Ready. Browse to: http://");
  Serial.println(ipText);

  drawText(currentText);   // first draw now includes the IP label

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send(200, "text/html", page());
  });

  server.on("/logo.svg", HTTP_GET, [](AsyncWebServerRequest* req){
    AsyncWebServerResponse* r = req->beginResponse(200, "image/svg+xml", THREEOAKWOODS_BADGE_SVG);
    r->addHeader("Cache-Control", "max-age=86400");
    req->send(r);
  });

  // Handlers only queue work; loop() does the drawing/fetching (off the async task).
  server.on("/set", HTTP_GET, [](AsyncWebServerRequest* req){
    if (req->hasParam("msg")) {
      currentText = req->getParam("msg")->value();
      currentText.replace("\r", "");  // textareas send \r\n; keep just \n
      if (currentText.length() == 0) currentText = " ";
    }
    mode = MODE_TEXT;
    pending = P_TEXT;
    req->redirect("/");
  });

  server.on("/weather", HTTP_GET, [](AsyncWebServerRequest* req){
    if (req->hasParam("zip")) weatherZip = req->getParam("zip")->value();
    mode = MODE_WEATHER;
    pending = P_WEATHER;
    req->redirect("/");
  });

  server.on("/station", HTTP_GET, [](AsyncWebServerRequest* req){
    mode = MODE_STATION;
    pending = P_STATION;
    req->redirect("/");
  });

  // Ingest pushes from the Ambient Weather console's "Customized" upload
  // (AmbientWeather protocol, GET with sensor fields in the query string).
  auto onReport = [](AsyncWebServerRequest* req){
    auto getF = [&](const char* k, float& dst){
      if (req->hasParam(k))       dst = req->getParam(k)->value().toFloat();
    };
    getF("tempf", st.tempf);
    getF("humidity", st.humidity);
    getF("windspeedmph", st.windmph);
    getF("windgustmph", st.gustmph);
    getF("dailyrainin", st.dailyrain);
    getF("baromrelin", st.baromin);
    if (req->hasParam("winddir")) st.winddir = req->getParam("winddir")->value().toInt();
    st.lastUpdate = millis();
    st.received = true;
    Serial.printf("Station push: %.1fF hum=%.0f wind=%.1f\n", st.tempf, st.humidity, st.windmph);
    if (mode == MODE_STATION) pending = P_STATION;   // live-refresh the panel
    req->send(200, "text/plain", "OK");              // console expects 200
  };
  server.on("/data/report/", HTTP_ANY, onReport);
  server.on("/data/report",  HTTP_ANY, onReport);    // tolerate missing trailing slash

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
    else if (job == P_CLEAR)   clearDisplay();
  }

  // Keep weather fresh while it's the active mode.
  if (mode == MODE_WEATHER && millis() - lastWxFetch > WX_REFRESH_MS) {
    fetchAndDrawWeather();
    lastWxFetch = millis();
  }

  delay(20);
}
