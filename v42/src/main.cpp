// main.cpp -- v42: hardware bring-up console, grown into a tiny SMS client.
//
// Modes, switched by keyboard:
//   Console     boot diagnostics, live modem events, and a keyboard echo
//               test: any key pressed is shown with its code. TAB opens the
//               message list.
//   List        messages stored on the SIM. UP/DOWN select, ENTER views,
//               d deletes, c composes a new message, r reloads, ESC console.
//   View        one full message. d deletes, r replies, ESC back.
//   Compose     type a number (ENTER), then the body (ENTER sends). ESC
//               cancels. BACKSPACE edits.
//
// Everything is mirrored on USB serial (115200), which also accepts commands
// -- see serialHelp(). Typing a line that starts with AT sends it straight
// to the modem.

#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <Wire.h>

#include "Config.h"
#include "Log.h"
#include "Modem.h"

SPIClass tftSPI(NRF_SPIM2, Pins::TFT_MISO, Pins::TFT_SCK, Pins::TFT_MOSI);
Adafruit_ST7789 tft(&tftSPI, Pins::TFT_CS, Pins::TFT_DC, Pins::TFT_RST);
Modem modem(&Serial1);

bool modemUp = false;
bool keyboardUp = false;

enum class Mode { Console, List, View, ComposeTo, ComposeBody };
Mode mode = Mode::Console;

// Messages currently on the SIM, from AT+CMGL.
struct SlotInfo {
  int slot;
  String sender;
  String preview;
};
constexpr int MAX_LIST = 32;
SlotInfo msgs[MAX_LIST];
int msgCount = 0;
int selected = 0;

Modem::Sms viewSms;   // the message open in View mode
int viewSlot = -1;
String composeTo, composeBody;

constexpr int COLS = 40;  // display rows are 40 chars at text size 1

// ------------------------------------------------------------ screen console
// Two fixed header rows, then a body area. In Console mode the body is a
// scrolling log that clears when full; other modes redraw it whole.
constexpr int LOG_Y = 2 * LINE_H + 4;
int logY = LOG_Y;

void clearBody() {
  tft.fillRect(0, LOG_Y, SCREEN_W, SCREEN_H - LOG_Y, ST77XX_BLACK);
  logY = LOG_Y;
}

void logLine(const String &text, uint16_t color = ST77XX_WHITE) {
  Serial.println(text);
  if (mode != Mode::Console) return;  // serial only; the screen shows a UI
  if (logY + LINE_H > SCREEN_H) clearBody();
  tft.setTextColor(color);
  tft.setCursor(0, logY);
  tft.print(text.substring(0, COLS));
  logY += LINE_H;
}

void headerLine(int row, const String &text, uint16_t color) {
  tft.fillRect(0, row * LINE_H, SCREEN_W, LINE_H, ST77XX_BLACK);
  tft.setTextColor(color);
  tft.setCursor(0, row * LINE_H);
  tft.print(text.substring(0, COLS));
}

// Prints wrapped text starting at y; returns the y after the last row.
int printWrapped(int y, const String &text, uint16_t color) {
  tft.setTextColor(color);
  for (unsigned pos = 0; pos == 0 || pos < text.length(); pos += COLS) {
    if (y + LINE_H > SCREEN_H) break;
    tft.setCursor(0, y);
    tft.print(text.substring(pos, pos + COLS));
    y += LINE_H;
  }
  return y;
}

// ------------------------------------------------------------------ modes
void drawConsole() {
  headerLine(0, "v42 console   TAB:messages", ST77XX_WHITE);
  clearBody();
  logLine("keys echo here -- press any key", ST77XX_CYAN);
}

void drawList() {
  headerLine(0, "msgs  ENT:view d:del c:new r:load ESC", ST77XX_WHITE);
  clearBody();
  if (msgCount == 0) {
    printWrapped(LOG_Y, "no messages on SIM", ST77XX_CYAN);
    printWrapped(LOG_Y + LINE_H, "(text this phone, then press r)", ST77XX_CYAN);
    return;
  }
  for (int i = 0; i < msgCount; i++) {
    const int y = LOG_Y + i * LINE_H;
    if (y + LINE_H > SCREEN_H) break;
    String row = (i == selected) ? ">" : " ";
    row += String(msgs[i].slot) + " " + msgs[i].sender.substring(0, 13) + " " + msgs[i].preview;
    tft.setTextColor(i == selected ? ST77XX_YELLOW : ST77XX_WHITE);
    tft.setCursor(0, y);
    tft.print(row.substring(0, COLS));
  }
}

void drawView() {
  headerLine(0, "message  d:del r:reply ESC:back", ST77XX_WHITE);
  clearBody();
  int y = printWrapped(LOG_Y, "From " + viewSms.sender, ST77XX_MAGENTA);
  if (viewSms.atTime.length() > 0) y = printWrapped(y, viewSms.atTime, ST77XX_CYAN);
  printWrapped(y + 4, viewSms.body, ST77XX_WHITE);
}

void drawCompose() {
  headerLine(0, mode == Mode::ComposeTo ? "compose  ENTER:next ESC:cancel"
                                        : "compose  ENTER:send ESC:cancel",
             ST77XX_WHITE);
  clearBody();
  int y = printWrapped(LOG_Y, "To: " + composeTo + (mode == Mode::ComposeTo ? "_" : ""),
                       ST77XX_CYAN);
  if (mode == Mode::ComposeBody) {
    printWrapped(y + 4, composeBody + "_", ST77XX_WHITE);
  }
}

// ------------------------------------------------------------------ modem
bool check(const char *label, const char *cmd) {
  String reply;
  const bool ok = modem.command(cmd, &reply, 5000);
  logLine(String(label) + (ok ? ": OK" : ": FAILED"), ok ? ST77XX_GREEN : ST77XX_RED);
  reply.trim();
  if (reply.length() > 0) logLine("  " + reply.substring(0, 38), ST77XX_CYAN);
  return ok;
}

void healthCheck() {
  check("model (ATI)", "ATI");
  check("SIM card (AT+CPIN?)", "AT+CPIN?");
  check("registration (AT+CREG?)", "AT+CREG?");
}

// Fills msgs[] from AT+CMGL="ALL".
// Reply lines look like:  +CMGL: 7,"REC READ","+16175551234",,"26/08/15,..."
// followed by the body until the next +CMGL header.
void loadList() {
  msgCount = 0;
  if (!modemUp) return;
  modem.textMode();

  String reply;
  if (!modem.command("AT+CMGL=\"ALL\"", &reply, 10000)) {
    logLine("AT+CMGL failed", ST77XX_RED);
    return;
  }

  unsigned pos = 0;
  while (pos < reply.length() && msgCount < MAX_LIST) {
    int end = reply.indexOf('\n', pos);
    if (end < 0) end = reply.length();
    const String line = reply.substring(pos, end);
    pos = end + 1;

    if (line.startsWith("+CMGL:")) {
      SlotInfo &m = msgs[msgCount++];
      m.slot = line.substring(6).toInt();
      m.preview = "";
      // The sender is the second quoted field (the first is the status).
      int q1 = line.indexOf('"');
      int q2 = line.indexOf('"', q1 + 1);
      int q3 = line.indexOf('"', q2 + 1);
      int q4 = line.indexOf('"', q3 + 1);
      m.sender = (q4 > q3) ? line.substring(q3 + 1, q4) : String("?");
    } else if (msgCount > 0 && line.length() > 0) {
      SlotInfo &m = msgs[msgCount - 1];
      if (m.preview.length() < 24) {
        if (m.preview.length() > 0) m.preview += ' ';
        m.preview += line.substring(0, 24 - m.preview.length());
      }
    }
  }

  if (selected >= msgCount) selected = msgCount > 0 ? msgCount - 1 : 0;
  LOGI("sms", "%d message(s) on SIM", msgCount);
}

// Reads one slot into viewSms (AT+CMGR keeps it on the SIM).
bool loadMessage(int slot) {
  modem.textMode();
  char cmd[24];
  snprintf(cmd, sizeof(cmd), "AT+CMGR=%d", slot);
  String reply;
  if (!modem.command(cmd, &reply, 5000)) return false;
  viewSms = Modem::parseSmsReply(reply);
  viewSlot = slot;
  return viewSms.body.length() > 0 || viewSms.sender.length() > 0;
}

void deleteAndReload(int slot) {
  const bool ok = modem.deleteSms(slot);
  LOGI("sms", "delete slot %d: %s", slot, ok ? "ok" : "FAILED");
  loadList();
  mode = Mode::List;
  drawList();
}

void refreshStatus() {
  const int csq = modem.signalQuality();
  String s = "sig " + String(csq) + "/31";
  if (csq <= 0 || csq == 99) s += " (none)";
  else if (csq < 10) s += " (weak)";

  // SIM message storage, from AT+CPMS?. Turns orange at 80% full, red when
  // completely full (a full SIM silently rejects incoming texts).
  int used = 0, total = 0;
  bool simFull = false, simNearFull = false;
  if (modem.smsSlots(used, total)) {
    s += "  sms " + String(used) + "/" + String(total);
    simFull = used >= total;
    simNearFull = used * 5 >= total * 4;
    if (simFull) s += " FULL!";
    else if (simNearFull) s += " (nearly full)";
  }

  const uint16_t color = simFull ? ST77XX_RED
                       : (simNearFull || csq <= 0 || csq == 99) ? ST77XX_ORANGE
                       : ST77XX_GREEN;
  headerLine(1, s, color);
}

void onNotification(const String &line) {
  logLine("EVENT: " + line, ST77XX_YELLOW);

  if (line.startsWith("+CMTI:")) {
    if (mode == Mode::List) {          // watching the list: refresh it live
      loadList();
      drawList();
    } else if (mode == Mode::Console) {  // console: fetch and show inline
      const int comma = line.lastIndexOf(',');
      if (comma >= 0 && loadMessage(line.substring(comma + 1).toInt())) {
        logLine("SMS from " + viewSms.sender, ST77XX_MAGENTA);
        for (unsigned p = 0; p < viewSms.body.length(); p += 38) {
          logLine("  " + viewSms.body.substring(p, p + 38));
        }
      }
    }
    // View/Compose: don't repaint under the user; the list reloads on return.
  }
}

// ------------------------------------------------------------------ keyboard
uint8_t readKey() {
  if (!keyboardUp) return 0;
  Wire.requestFrom((uint8_t)KEYBOARD_I2C_ADDR, (uint8_t)1);
  if (!Wire.available()) return 0;
  return Wire.read();
}

void enterList() {
  mode = Mode::List;
  loadList();
  drawList();
}

void handleKey(uint8_t k) {
  LOGD("key", "0x%02X", k);

  switch (mode) {
    case Mode::Console:
      if (k == Key::Tab) enterList();
      else if (Key::isPrintable(k)) {
        logLine("key: '" + String((char)k) + "'  (0x" + String(k, HEX) + ")", ST77XX_GREEN);
      } else {
        logLine("key code 0x" + String(k, HEX), ST77XX_GREEN);
      }
      break;

    case Mode::List:
      if (k == Key::Up && selected > 0) { selected--; drawList(); }
      else if (k == Key::Down && selected < msgCount - 1) { selected++; drawList(); }
      else if (k == Key::Enter && msgCount > 0) {
        if (loadMessage(msgs[selected].slot)) { mode = Mode::View; drawView(); }
      }
      else if (k == 'd' && msgCount > 0) deleteAndReload(msgs[selected].slot);
      else if (k == 'c') { composeTo = ""; composeBody = ""; mode = Mode::ComposeTo; drawCompose(); }
      else if (k == 'r') { loadList(); drawList(); }
      else if (k == Key::Esc || k == Key::Tab) { mode = Mode::Console; drawConsole(); }
      break;

    case Mode::View:
      if (k == 'd') deleteAndReload(viewSlot);
      else if (k == 'r') {
        composeTo = viewSms.sender; composeBody = "";
        mode = Mode::ComposeBody; drawCompose();
      }
      else if (k == Key::Esc || k == Key::Tab) { mode = Mode::List; drawList(); }
      break;

    case Mode::ComposeTo:
      if (k == Key::Enter && composeTo.length() > 0) { mode = Mode::ComposeBody; drawCompose(); }
      else if (k == Key::Backspace && composeTo.length() > 0) {
        composeTo.remove(composeTo.length() - 1); drawCompose();
      }
      else if (k == Key::Esc) enterList();
      else if (Key::isPrintable(k) && composeTo.length() < 20) {
        composeTo += (char)k; drawCompose();
      }
      break;

    case Mode::ComposeBody:
      if (k == Key::Enter && composeBody.length() > 0) {
        headerLine(0, "sending...", ST77XX_CYAN);
        const bool ok = modem.sendSms(composeTo, composeBody);
        LOGI("sms", "send to %s: %s", composeTo.c_str(), ok ? "ok" : "FAILED");
        enterList();
        headerLine(1, ok ? "sent OK" : "SEND FAILED", ok ? ST77XX_GREEN : ST77XX_RED);
      }
      else if (k == Key::Backspace && composeBody.length() > 0) {
        composeBody.remove(composeBody.length() - 1); drawCompose();
      }
      else if (k == Key::Esc) enterList();
      else if (Key::isPrintable(k) && composeBody.length() < MAX_COMPOSE_CHARS) {
        composeBody += (char)k; drawCompose();
      }
      break;
  }
}

// --------------------------------------------------------- serial commands
void serialHelp() {
  Serial.println("commands:");
  Serial.println("  AT...   send any AT command to the modem, print the reply");
  Serial.println("  status  re-run the modem health check");
  Serial.println("  debug   toggle raw AT TX/RX tracing");
  Serial.println("  ram     show free RAM");
  Serial.println("  help    this list");
}

void handleSerialLine(String line) {
  line.trim();
  if (line.length() == 0) return;

  String upper = line;
  upper.toUpperCase();

  if (upper.startsWith("AT")) {
    String reply;
    const bool ok = modem.command(line.c_str(), &reply, 10000);
    if (reply.length() > 0) Serial.println(reply);
    Serial.println(ok ? "OK" : "FAILED");
  } else if (line == "status") {
    healthCheck();
  } else if (line == "debug") {
    logLevel = (logLevel == LogLevel::Debug) ? LogLevel::Info : LogLevel::Debug;
    Serial.println(logLevel == LogLevel::Debug ? "debug on" : "debug off");
  } else if (line == "ram") {
    Serial.print(freeRam());
    Serial.println(" bytes free");
  } else {
    serialHelp();
  }
}

void pollSerial() {
  static String pending;
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == '\r' || c == '\n') {
      handleSerialLine(pending);
      pending = "";
    } else if (pending.length() < 200) {
      pending += c;
    }
  }
}

// ------------------------------------------------------------------- arduino
void setup() {
  Serial.begin(CONSOLE_BAUD);
  const uint32_t start = millis();
  while (!Serial && millis() - start < 3000) delay(10);

  tftSPI.begin();
  tft.init(SCREEN_W, SCREEN_H);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setTextWrap(false);

  headerLine(0, "diy-phone v42", ST77XX_WHITE);
  headerLine(1, "starting...", ST77XX_CYAN);
  logLine("display OK");
  serialHelp();

  Wire.begin();
  Wire.beginTransmission(KEYBOARD_I2C_ADDR);
  keyboardUp = (Wire.endTransmission() == 0);
  logLine(keyboardUp ? "keyboard OK (0x5F)" : "keyboard NOT FOUND at 0x5F",
          keyboardUp ? ST77XX_GREEN : ST77XX_RED);
  if (!keyboardUp) logLine("  check SDA->8, SCL->7, 3.3V, GND", ST77XX_ORANGE);

  // The SIM7600 can need many seconds after power-on before its UART answers.
  logLine("waiting for modem (up to 30s)...");
  for (int i = 0; i < 10 && !modemUp; i++) {
    modemUp = modem.begin(MODEM_BAUD, Pins::MODEM_RX, Pins::MODEM_TX);
    if (!modemUp) delay(2000);
  }

  if (modemUp) {
    logLine("AT: OK -- UART wiring good", ST77XX_GREEN);
    healthCheck();
    modem.enableCallerId();
    modem.textMode();
    modem.onNotification(onNotification);
    logLine("TAB: message list  |  text me!", ST77XX_YELLOW);
  } else {
    headerLine(1, "NO RESPONSE from modem", ST77XX_RED);
    logLine("AT: no reply -- check TX->4, RX->3,", ST77XX_ORANGE);
    logLine("  common GND, modem power", ST77XX_ORANGE);
  }
}

void loop() {
  static uint32_t lastStatus = 0;

  if (!modemUp) {  // setup() gave up; keep trying quietly
    modemUp = modem.isResponding();
    if (modemUp) {
      logLine("modem responded late -- UART OK", ST77XX_GREEN);
      healthCheck();
      modem.enableCallerId();
      modem.textMode();
      modem.onNotification(onNotification);
    }
  }

  if (!keyboardUp && millis() - lastStatus >= 5000) {  // hot-plug friendly
    Wire.beginTransmission(KEYBOARD_I2C_ADDR);
    if (Wire.endTransmission() == 0) {
      keyboardUp = true;
      logLine("keyboard OK (plugged in late)", ST77XX_GREEN);
    }
  }

  if (modemUp && millis() - lastStatus >= 5000) {
    lastStatus = millis();
    refreshStatus();
  } else if (millis() - lastStatus >= 5000) {
    lastStatus = millis();
  }

  const uint8_t k = readKey();
  if (k != 0) handleKey(k);

  modem.poll();
  pollSerial();
  delay(10);
}
