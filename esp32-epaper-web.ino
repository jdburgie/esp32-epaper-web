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
#include <ESPAsyncWebServer.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include "secrets.h"   // SECRET_SSID / SECRET_PASS (gitignored)

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

String currentText = "Hello World!";
String ipText      = "";  // set once Wi-Fi connects; shown bottom-left always

// Line spacing for FreeSansBold12pt7b (~17px glyphs + breathing room).
#define LINE_HEIGHT 22

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

// ---- Minimal HTML page ----
String page() {
  String h = "<!DOCTYPE html><html><head><meta charset='utf-8'>"
             "<meta name='viewport' content='width=device-width,initial-scale=1'>"
             "<title>E-Paper</title>"
             "<style>body{font-family:sans-serif;max-width:480px;margin:40px auto;padding:0 16px}"
             "textarea,button{font-size:1.1em;padding:10px;width:100%;box-sizing:border-box;margin:6px 0}"
             "textarea{resize:vertical}"
             "button{background:#222;color:#fff;border:0;border-radius:6px;cursor:pointer}"
             "button.clear{background:#fff;color:#222;border:1px solid #ccc}</style></head><body>"
             "<h2>E-Paper Display</h2>"
             "<p>Currently showing: <b>" + currentText + "</b></p>"
             "<form action='/set' method='get'>"
             "<textarea name='msg' rows='4' maxlength='120' "
             "placeholder='Type text... (Enter for a new line)'>" + currentText + "</textarea>"
             "<button type='submit'>Update Display</button></form>"
             "<form action='/clear' method='get'>"
             "<button type='submit' class='clear'>Clear Screen</button></form>"
             "</body></html>";
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

  server.on("/set", HTTP_GET, [](AsyncWebServerRequest* req){
    if (req->hasParam("msg")) {
      currentText = req->getParam("msg")->value();
      currentText.replace("\r", "");  // textareas send \r\n; keep just \n
      if (currentText.length() == 0) currentText = " ";
    }
    req->redirect("/");   // respond first so the browser doesn't hang
    drawText(currentText); // then redraw (~2s)
  });

  server.on("/clear", HTTP_GET, [](AsyncWebServerRequest* req){
    currentText = " ";
    req->redirect("/");   // respond first so the browser doesn't hang
    clearDisplay();       // then blank the panel
  });

  server.begin();
}

void loop() {
  // AsyncWebServer runs in the background; nothing needed here.
}
