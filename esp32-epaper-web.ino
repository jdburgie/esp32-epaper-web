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
#include <ESPAsyncWebServer.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include "secrets.h"   // SECRET_SSID / SECRET_PASS (gitignored)
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
enum DisplayMode { MODE_TEXT, MODE_WEATHER };
DisplayMode mode    = MODE_TEXT;
String weatherZip   = "";   // last ZIP requested
String weatherText  = "";   // short summary, shown on the web page

// All panel drawing + the weather fetch run from loop(), never inside an async
// web handler — that keeps blocking SPI/TLS work off the AsyncTCP task and
// avoids two tasks touching the display at once. Handlers just queue an action.
enum Pending { P_NONE, P_TEXT, P_WEATHER, P_CLEAR };
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

// ---- Draw the IP in the built-in 6x8 font, bottom-left corner ----
// Must be called from inside a firstPage/nextPage paged-draw loop. The
// built-in font (setFont(NULL)) positions by the text's TOP-left.
void drawIpLabel() {
  if (ipText.length() == 0) return;
  display.setFont(NULL);            // classic 6x8 GFX font
  display.setTextSize(1);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(2, display.height() - 8);
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

// ---- Fetch weather for weatherZip from wttr.in and draw it ----
// wttr.in takes a US ZIP directly and, with ?format=, returns one line we can
// split — no JSON parser needed. Format "%l|%t|%C" -> "City, State, ...|+72F|Sunny".
void fetchAndDrawWeather() {
  if (weatherZip.length() == 0) { drawText("Enter a ZIP\nfor weather"); return; }

  String url = "https://wttr.in/" + weatherZip + "?format=%l|%t|%C";
  WiFiClientSecure client;
  client.setInsecure();              // skip cert validation (home project)
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  String body;
  int code = -1;
  if (http.begin(client, url)) {
    http.addHeader("User-Agent", "curl/8.0");   // wttr.in gives plain text to curl-like UAs
    code = http.GET();
    if (code == HTTP_CODE_OK) body = http.getString();
    http.end();
  }

  body.trim();
  if (code != HTTP_CODE_OK || body.length() == 0 || body.startsWith("Unknown location")) {
    weatherText = "unavailable (ZIP " + weatherZip + ")";
    drawText("Weather\nunavailable\nZIP " + weatherZip);
    Serial.printf("Weather fetch failed: code=%d body=%s\n", code, body.c_str());
    return;
  }

  // Split "loc|temp|cond" on '|'.
  int p1 = body.indexOf('|');
  int p2 = body.indexOf('|', p1 + 1);
  String loc  = (p1 < 0) ? body : body.substring(0, p1);
  String temp = (p1 < 0) ? "" : body.substring(p1 + 1, p2 < 0 ? body.length() : p2);
  String cond = (p2 < 0) ? "" : body.substring(p2 + 1);

  String city = loc.substring(0, loc.indexOf(',') < 0 ? loc.length() : loc.indexOf(','));
  city = asciiOnly(city);
  temp = asciiOnly(temp); temp.replace("+", "");   // "+72F" -> "72F"
  cond = asciiOnly(cond);

  weatherText = city + " " + temp + ", " + cond;   // for the web page
  drawText(city + "\n" + temp + "\n" + cond);       // City / temp / condition
  Serial.println("Weather: " + weatherText);
}

// ---- Three Oak Woods themed control page ----
String page() {
  String status = (mode == MODE_WEATHER)
      ? ("Weather \xE2\x80\x94 " + (weatherText.length() ? weatherText : ("ZIP " + weatherZip)))
      : currentText;

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

  server.on("/clear", HTTP_GET, [](AsyncWebServerRequest* req){
    pending = P_CLEAR;
    req->redirect("/");
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
    else if (job == P_CLEAR)   clearDisplay();
  }

  // Keep weather fresh while it's the active mode.
  if (mode == MODE_WEATHER && millis() - lastWxFetch > WX_REFRESH_MS) {
    fetchAndDrawWeather();
    lastWxFetch = millis();
  }

  delay(20);
}
