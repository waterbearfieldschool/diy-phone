// sim_test.cpp -- wiring check for the SIM7600, using the display as output.
//
// Built by the `simtest` environment only, together with the real Modem and
// Log modules -- the modem is driven through the same UART setup and command
// path the phone uses.
//
//   pio run -e simtest
//
// On boot it runs through: AT (is the UART wired?), ATI (which modem), CPIN?
// (is a SIM inserted?), then keeps a signal/registration line updated every
// 5 seconds. Incoming RING / +CMTI notifications are printed as they arrive,
// so calling or texting the SIM is the full end-to-end test.
//
// Everything is mirrored on USB serial (115200), including raw AT traffic.

#include <Adafruit_ST7789.h>
#include <SPI.h>

#include "../Config.h"
#include "../Log.h"
#include "../Modem.h"

SPIClass tftSPI(NRF_SPIM2, Pins::TFT_MISO, Pins::TFT_SCK, Pins::TFT_MOSI);
Adafruit_ST7789 tft(&tftSPI, Pins::TFT_CS, Pins::TFT_DC, Pins::TFT_RST);
Modem modem(&Serial1);

// ---- a minimal scrolling console on the TFT ----
// Header (2 fixed lines) + log area. When the log area fills, it clears.
// LINE_H comes from Config.h
constexpr int LOG_Y = 2 * LINE_H + 4;
int logY = LOG_Y;

void logLine(const String &text, uint16_t color = ST77XX_WHITE) {
  Serial.println(text);
  if (logY + LINE_H > SCREEN_H) {
    tft.fillRect(0, LOG_Y, SCREEN_W, SCREEN_H - LOG_Y, ST77XX_BLACK);
    logY = LOG_Y;
  }
  tft.setTextColor(color);
  tft.setCursor(0, logY);
  tft.print(text.substring(0, 40));  // one screen row; serial has the full line
  logY += LINE_H;
}

void headerLine(int row, const String &text, uint16_t color) {
  tft.fillRect(0, row * LINE_H, SCREEN_W, LINE_H, ST77XX_BLACK);
  tft.setTextColor(color);
  tft.setCursor(0, row * LINE_H);
  tft.print(text.substring(0, 40));
}

// Runs one AT command and reports it as a pass/fail line plus its reply.
bool check(const char *label, const char *cmd) {
  String reply;
  const bool ok = modem.command(cmd, &reply, 5000);
  logLine(String(label) + (ok ? ": OK" : ": FAILED"), ok ? ST77XX_GREEN : ST77XX_RED);
  reply.trim();
  if (reply.length() > 0) logLine("  " + reply.substring(0, 38), ST77XX_CYAN);
  return ok;
}

void onNotification(const String &line) {
  logLine("EVENT: " + line, ST77XX_YELLOW);
}

void setup() {
  Serial.begin(CONSOLE_BAUD);
  const uint32_t start = millis();
  while (!Serial && millis() - start < 3000) delay(10);
  logLevel = LogLevel::Debug;  // raw AT TX/RX on USB serial

  tftSPI.begin();
  tft.init(SCREEN_W, SCREEN_H);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setTextWrap(false);

  headerLine(0, "SIM7600 test  RX=4 TX=3  115200", ST77XX_WHITE);
  headerLine(1, "starting...", ST77XX_CYAN);

  // The SIM7600 can need many seconds after power-on before its UART answers.
  logLine("waiting for modem (up to 30s)...");
  bool up = false;
  for (int i = 0; i < 10 && !up; i++) {
    up = modem.begin(MODEM_BAUD, Pins::MODEM_RX, Pins::MODEM_TX);
    if (!up) delay(2000);
  }

  if (!up) {
    headerLine(1, "NO RESPONSE from modem", ST77XX_RED);
    logLine("AT: no reply", ST77XX_RED);
    logLine("check: TX->4, RX->3 (crossed),", ST77XX_ORANGE);
    logLine("  common GND, modem power LED on,", ST77XX_ORANGE);
    logLine("  3.3V logic level", ST77XX_ORANGE);
    return;  // loop() keeps retrying
  }

  headerLine(1, "modem: UART OK", ST77XX_GREEN);
  logLine("AT: OK -- UART wiring good", ST77XX_GREEN);

  check("model (ATI)", "ATI");
  check("SIM card (AT+CPIN?)", "AT+CPIN?");
  check("registration (AT+CREG?)", "AT+CREG?");

  modem.enableCallerId();
  modem.textMode();
  modem.onNotification(onNotification);
  logLine("now call or text the SIM to", ST77XX_YELLOW);
  logLine("  see live RING/+CMTI events", ST77XX_YELLOW);
}

void loop() {
  static uint32_t lastStatus = 0;
  static bool up = false;

  if (!up) {  // setup() may have given up; keep trying quietly
    up = modem.isResponding();
    if (up) headerLine(1, "modem: UART OK (late)", ST77XX_GREEN);
  }

  if (up && millis() - lastStatus >= 5000) {
    lastStatus = millis();
    const int csq = modem.signalQuality();  // 0-31, 99/-1 = unknown
    String s = "signal: " + String(csq) + "/31";
    if (csq <= 0 || csq == 99) s += "  (no signal yet)";
    else if (csq < 10) s += "  (weak)";
    else s += "  (good)";
    const String t = modem.networkTime();
    if (t.length() > 0) s += "  net time OK";
    headerLine(1, s, (csq > 0 && csq != 99) ? ST77XX_GREEN : ST77XX_ORANGE);
  }

  modem.poll();  // dispatches RING / +CMTI to onNotification
  delay(10);
}
