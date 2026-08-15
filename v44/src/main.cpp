// main.cpp -- v43: the bring-up console grows a real UI.
//
// Boot runs a short checklist (keyboard, modem, SIM, network) and then goes
// straight to the inbox. Every screen keeps the same chrome:
//
//   +--------------------------------------+
//   | signal bars   title   unread  sms use|   status bar, always visible
//   |                                      |
//   |               body                   |
//   |                                      |
//   | key hints for this screen            |   footer, always visible
//   +--------------------------------------+
//
// Screens: Inbox (home), Read, Compose (number then body), Status. The
// status bar doubles as a notification toast when a message arrives.
// Messages live on the SIM; unread ones are bright with a cyan dot, read
// ones dim. The inbox sorts newest first.
//
// Serial (115200) still carries logs and accepts commands -- AT passthrough,
// status, debug, ram, help -- but the screen shows no debug output.

#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <Wire.h>

#include "Config.h"
#include "Log.h"
#include "Modem.h"
#include "Timestamp.h"

SPIClass tftSPI(NRF_SPIM2, Pins::TFT_MISO, Pins::TFT_SCK, Pins::TFT_MOSI);
Adafruit_ST7789 tft(&tftSPI, Pins::TFT_CS, Pins::TFT_DC, Pins::TFT_RST);
Modem modem(&Serial1);

constexpr char VERSION[] = "v44";

// ------------------------------------------------------------------ layout
constexpr int STATUS_H = 14;             // top bar
constexpr int FOOTER_H = 12;             // bottom hint bar
constexpr int BODY_Y = STATUS_H + 2;
constexpr int FOOTER_Y = SCREEN_H - FOOTER_H;
constexpr int BODY_H = FOOTER_Y - BODY_Y - 2;
constexpr int BODY_ROWS = BODY_H / LINE_H;
// Rows start at x=2, so 40 full 6px characters would end at x=242 -- two
// pixels off the panel. 39 columns keeps the last character on screen.
constexpr int COLS = 39;

// A quiet palette on top of the stock ST77XX colors.
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint16_t BAR_BG   = rgb565(24, 26, 38);
constexpr uint16_t SEL_BG   = rgb565(44, 50, 92);
constexpr uint16_t DIM      = rgb565(128, 130, 142);
constexpr uint16_t TOAST_BG = rgb565(20, 120, 70);

// ------------------------------------------------------------------- state
enum class Screen { Boot, Inbox, Read, ComposeTo, ComposeBody, Status };
Screen screen = Screen::Boot;

bool modemUp = false;
bool keyboardUp = false;

struct Entry {
  int slot;
  bool unread;
  String sender;
  String preview;
  Timestamp::Stamp when;
};
constexpr int MAX_LIST = 32;
Entry inbox[MAX_LIST];
int inboxCount = 0;
int unreadCount = 0;
int selected = 0;
int inboxTop = 0;        // first visible row
bool inboxStale = false; // a message arrived while composing

Modem::Sms openSms;      // the message on the Read screen
int openSlot = -1;
int readScroll = 0;

String composeTo, composeBody;  // composeTo is the final +number to send to
String composeInput;            // what has been typed on the To screen
String composeName;             // contact name, when one was picked
int composeSel = -1;            // highlighted contact row; -1 = the typed field
int composeCursor = 0;          // caret position within composeBody

int csq = -1;            // cached signal, refreshed every 5s
int smsUsed = 0, smsTotal = 0;

uint32_t toastUntil = 0; // status bar shows a toast until this time
String statusEcho;       // last key echoed on the Status screen

// Missed calls live in RAM (the SIM doesn't store them); newest first.
// missedNew counts the ones not yet seen on the Status screen.
struct Missed {
  String number;
  String when;
};
constexpr int MAX_MISSED = 8;
Missed missedCalls[MAX_MISSED];
int missedCount = 0;
int missedNew = 0;

// ------------------------------------------------------------ address book
struct Contact {
  const char *name;    // lowercase, so prefix search needs no conversion
  const char *number;  // full international form
};
const Contact CONTACTS[] = {
    {"emilie", "+16463278220"},
    {"liz",    "+16174299144"},
    {"don",    "+16512524765"},
};
constexpr int CONTACT_COUNT = sizeof(CONTACTS) / sizeof(CONTACTS[0]);

// The last 10 digits, for comparing numbers that differ only in prefix form.
String last10(const String &n) {
  String d;
  for (unsigned i = 0; i < n.length(); i++) {
    if (isDigit(n[i])) d += n[i];
  }
  return (d.length() > 10) ? d.substring(d.length() - 10) : d;
}

// A typed number becomes full international form; bare 10 digits are assumed
// to be US (+1).
String normalizeNumber(const String &typed) {
  String d;
  for (unsigned i = 0; i < typed.length(); i++) {
    if (isDigit(typed[i])) d += typed[i];
  }
  if (typed.startsWith("+")) return "+" + d;
  if (d.length() == 10) return "+1" + d;
  return "+" + d;
}

const char *contactName(const String &number) {
  const String tail = last10(number);
  for (int i = 0; i < CONTACT_COUNT; i++) {
    if (last10(CONTACTS[i].number) == tail) return CONTACTS[i].name;
  }
  return nullptr;
}

// The name when we know it, otherwise the number itself.
String displayName(const String &number) {
  const char *name = contactName(number);
  return name ? String(name) : number;
}

// ------------------------------------------------------------------ chrome
void drawSignalBars(int x, int y) {
  const int strength = (csq >= 20) ? 4 : (csq >= 14) ? 3 : (csq >= 8) ? 2
                       : (csq >= 1 && csq != 99) ? 1 : 0;
  for (int i = 0; i < 4; i++) {
    const int h = 3 + i * 3;
    const uint16_t c = (i < strength) ? ST77XX_WHITE : DIM;
    tft.fillRect(x + i * 4, y + 12 - h, 3, h, c);
  }
}

const char *screenTitle() {
  switch (screen) {
    case Screen::Inbox:       return "inbox";
    case Screen::Read:        return "message";
    case Screen::ComposeTo:
    case Screen::ComposeBody: return "compose";
    case Screen::Status:      return "status";
    default:                  return VERSION;
  }
}

void drawStatusBar() {
  if (millis() < toastUntil) return;  // a toast owns the bar right now
  tft.fillRect(0, 0, SCREEN_W, STATUS_H, BAR_BG);
  drawSignalBars(2, 1);

  tft.setTextSize(1);
  tft.setCursor(24, 3);
  if (unreadCount > 0) {
    tft.setTextColor(ST77XX_CYAN);
    tft.print("*");
    tft.print(unreadCount);
  }
  if (missedNew > 0) {
    if (unreadCount > 0) tft.print(" ");
    tft.setTextColor(ST77XX_RED);
    tft.print("!");
    tft.print(missedNew);
  }

  const String title = screenTitle();
  tft.setCursor((SCREEN_W - (int)title.length() * CHAR_W) / 2, 3);
  tft.setTextColor(ST77XX_WHITE);
  tft.print(title);

  String store = String(smsUsed) + "/" + String(smsTotal);
  uint16_t storeColor = DIM;
  if (smsTotal > 0 && smsUsed >= smsTotal) { store += "!"; storeColor = ST77XX_RED; }
  else if (smsTotal > 0 && smsUsed * 5 >= smsTotal * 4) storeColor = ST77XX_ORANGE;
  tft.setCursor(SCREEN_W - (int)store.length() * CHAR_W - 4, 3);
  tft.setTextColor(storeColor);
  tft.print(store);
}

void toast(const String &text, uint16_t bg = TOAST_BG) {
  toastUntil = millis() + 4000;
  tft.fillRect(0, 0, SCREEN_W, STATUS_H, bg);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(4, 3);
  tft.print(text.substring(0, COLS - 1));
}

void drawFooter(const char *hints) {
  tft.fillRect(0, FOOTER_Y, SCREEN_W, FOOTER_H, ST77XX_BLACK);
  tft.drawFastHLine(0, FOOTER_Y, SCREEN_W, BAR_BG);
  tft.setTextSize(1);
  tft.setTextColor(DIM);
  tft.setCursor(2, FOOTER_Y + 3);
  tft.print(hints);
}

void clearBody() { tft.fillRect(0, BODY_Y, SCREEN_W, BODY_H + 2, ST77XX_BLACK); }

// Prints wrapped text starting at row `row` of the body; returns next row.
int bodyText(int row, const String &text, uint16_t color) {
  tft.setTextSize(1);
  tft.setTextColor(color);
  for (unsigned pos = 0; pos == 0 || pos < text.length(); pos += COLS) {
    if (row >= BODY_ROWS) break;
    tft.setCursor(2, BODY_Y + row * LINE_H);
    tft.print(text.substring(pos, pos + COLS));
    row++;
  }
  return row;
}

// ------------------------------------------------------------------- inbox
// Fills inbox[] from AT+CMGL="ALL", newest first.
// Header lines: +CMGL: 7,"REC UNREAD","+16175551234",,"26/08/15,10:42:03-16"
void loadInbox() {
  inboxCount = 0;
  unreadCount = 0;
  inboxStale = false;
  if (!modemUp) return;
  modem.textMode();

  String reply;
  if (!modem.command("AT+CMGL=\"ALL\"", &reply, 10000)) {
    LOGE("sms", "AT+CMGL failed");
    return;
  }

  unsigned pos = 0;
  while (pos < reply.length() && inboxCount < MAX_LIST) {
    int end = reply.indexOf('\n', pos);
    if (end < 0) end = reply.length();
    const String line = reply.substring(pos, end);
    pos = end + 1;

    if (line.startsWith("+CMGL:")) {
      Entry &m = inbox[inboxCount++];
      m.slot = line.substring(6).toInt();
      m.preview = "";
      m.when = Timestamp::Stamp();

      // Quoted fields: status, sender, ..., timestamp last.
      String quoted[4];
      int nq = 0;
      int q = line.indexOf('"');
      while (q >= 0 && nq < 4) {
        const int close = line.indexOf('"', q + 1);
        if (close < 0) break;
        quoted[nq++] = line.substring(q + 1, close);
        q = line.indexOf('"', close + 1);
      }
      m.unread = (nq > 0 && quoted[0].indexOf("UNREAD") >= 0);
      m.sender = (nq > 1) ? quoted[1] : String("?");
      if (nq > 2) Timestamp::parse(quoted[nq - 1], m.when);
      if (m.unread) unreadCount++;
    } else if (inboxCount > 0 && line.length() > 0) {
      Entry &m = inbox[inboxCount - 1];
      if (m.preview.length() < 20) {
        if (m.preview.length() > 0) m.preview += ' ';
        m.preview += line.substring(0, 20 - m.preview.length());
      }
    }
  }

  // Newest first; unknown timestamps sink. Insertion sort -- the list is tiny.
  for (int i = 1; i < inboxCount; i++) {
    const Entry key = inbox[i];
    int j = i - 1;
    while (j >= 0 && inbox[j].when.epoch < key.when.epoch) {
      inbox[j + 1] = inbox[j];
      j--;
    }
    inbox[j + 1] = key;
  }

  if (selected >= inboxCount) selected = inboxCount > 0 ? inboxCount - 1 : 0;
  LOGI("sms", "%d message(s), %d unread", inboxCount, unreadCount);
}

void drawInbox() {
  clearBody();
  drawFooter("ENT read  C compose  D del  R reload  TAB");

  if (inboxCount == 0) {
    bodyText(1, modemUp ? "inbox empty" : "modem offline -- no inbox", DIM);
    if (modemUp) bodyText(3, "C composes a message; text this", DIM);
    if (modemUp) bodyText(4, "number to fill the inbox.", DIM);
    drawStatusBar();
    return;
  }

  // Keep the selection in the visible window.
  if (selected < inboxTop) inboxTop = selected;
  if (selected >= inboxTop + BODY_ROWS) inboxTop = selected - BODY_ROWS + 1;

  tft.setTextSize(1);
  for (int row = 0; row < BODY_ROWS; row++) {
    const int i = inboxTop + row;
    if (i >= inboxCount) break;
    const Entry &m = inbox[i];
    const int y = BODY_Y + row * LINE_H;

    if (i == selected) tft.fillRect(0, y - 1, SCREEN_W, LINE_H, SEL_BG);

    tft.setCursor(2, y);
    tft.setTextColor(m.unread ? ST77XX_CYAN : DIM);
    tft.print(m.unread ? "*" : " ");

    const String hm = Timestamp::hhmm(m.when);
    String row40 = (hm.length() > 0 ? hm : String("--:--")) + " ";
    row40 += displayName(m.sender).substring(0, 13);
    while (row40.length() < 20) row40 += ' ';
    row40 += m.preview;

    tft.setCursor(2 + 2 * CHAR_W, y);
    tft.setTextColor(m.unread || i == selected ? ST77XX_WHITE : DIM);
    tft.print(row40.substring(0, COLS - 2));
  }
  drawStatusBar();
}

void enterInbox() {
  screen = Screen::Inbox;
  if (inboxStale) loadInbox();
  drawInbox();
}

// -------------------------------------------------------------------- read
bool loadMessage(int slot) {
  modem.textMode();
  char cmd[24];
  snprintf(cmd, sizeof(cmd), "AT+CMGR=%d", slot);  // CMGR also marks it read
  String reply;
  if (!modem.command(cmd, &reply, 5000)) return false;
  openSms = Modem::parseSmsReply(reply);
  openSlot = slot;
  readScroll = 0;
  return openSms.body.length() > 0 || openSms.sender.length() > 0;
}

void drawRead() {
  clearBody();
  drawFooter("UP/DN scroll  D del  R reply  ESC back");

  String from = displayName(openSms.sender);
  if (from != openSms.sender) from += "  " + openSms.sender;
  int row = bodyText(0, "From " + from, ST77XX_MAGENTA);
  Timestamp::Stamp when;
  if (Timestamp::parse(openSms.atTime, when)) {
    row = bodyText(row, "at " + Timestamp::hhmm(when) + "  (" + openSms.atTime + ")", DIM);
  }
  tft.drawFastHLine(0, BODY_Y + row * LINE_H + 2, SCREEN_W, BAR_BG);
  row++;

  // Body, scrolled by whole rows.
  const int bodyStart = row;
  unsigned pos = (unsigned)readScroll * COLS;
  for (int r = bodyStart; r < BODY_ROWS && pos < openSms.body.length(); r++, pos += COLS) {
    tft.setCursor(2, BODY_Y + r * LINE_H);
    tft.setTextColor(ST77XX_WHITE);
    tft.print(openSms.body.substring(pos, pos + COLS));
  }
  if (pos < openSms.body.length()) {
    tft.setCursor(SCREEN_W - 3 * CHAR_W - 4, FOOTER_Y - LINE_H);
    tft.setTextColor(ST77XX_CYAN);
    tft.print("...");
  }
  drawStatusBar();
}

// ----------------------------------------------------------------- compose
// Contacts whose name starts with what has been typed. An empty field
// matches everyone; typing digits matches no one (it is a manual number).
int matchingContacts(int idx[CONTACT_COUNT]) {
  String want = composeInput;
  want.toLowerCase();
  int n = 0;
  for (int i = 0; i < CONTACT_COUNT; i++) {
    if (want.length() == 0 || String(CONTACTS[i].name).startsWith(want)) idx[n++] = i;
  }
  return n;
}

void drawCompose() {
  clearBody();

  if (screen == Screen::ComposeTo) {
    drawFooter("name or number  UP/DN pick  ENT  ESC");
    int row = bodyText(0, "To: " + composeInput + (composeSel < 0 ? "_" : ""), ST77XX_CYAN);
    tft.drawFastHLine(0, BODY_Y + row * LINE_H + 2, SCREEN_W, BAR_BG);
    row++;

    int idx[CONTACT_COUNT];
    const int n = matchingContacts(idx);
    for (int i = 0; i < n && row < BODY_ROWS; i++, row++) {
      const int y = BODY_Y + row * LINE_H;
      if (i == composeSel) tft.fillRect(0, y - 1, SCREEN_W, LINE_H, SEL_BG);
      tft.setCursor(2, y);
      tft.setTextColor(i == composeSel ? ST77XX_WHITE : DIM);
      String r = String(CONTACTS[idx[i]].name);
      while (r.length() < 10) r += ' ';
      r += CONTACTS[idx[i]].number;
      tft.print(r.substring(0, COLS));
    }
  } else {
    drawFooter("type message  ENT send  ESC cancel");
    String to = composeName.length() ? composeName + "  " + composeTo : composeTo;
    int row = bodyText(0, "To: " + to, ST77XX_CYAN);
    tft.drawFastHLine(0, BODY_Y + row * LINE_H + 2, SCREEN_W, BAR_BG);
    const int start = row + 1;
    bodyText(start, composeBody, ST77XX_WHITE);
    // The caret: a cyan underline beneath the character cell it sits on.
    const int crow = start + composeCursor / COLS;
    const int ccol = composeCursor % COLS;
    if (crow < BODY_ROWS) {
      tft.drawFastHLine(2 + ccol * CHAR_W, BODY_Y + (crow + 1) * LINE_H - 2,
                        CHAR_W, ST77XX_CYAN);
    }
    const String count = String(composeBody.length()) + "/" + String(MAX_COMPOSE_CHARS);
    tft.setCursor(SCREEN_W - (int)count.length() * CHAR_W - 4, FOOTER_Y - LINE_H);
    tft.setTextColor(DIM);
    tft.print(count);
  }
  drawStatusBar();
}

// ------------------------------------------------------------------ status
void drawStatusScreen() {
  clearBody();
  drawFooter("press keys to test them  ESC back");

  int row = 0;
  row = bodyText(row, "diy-phone " + String(VERSION), ST77XX_WHITE);
  row = bodyText(row, "free RAM  " + String(freeRam()) + " bytes", DIM);
  row = bodyText(row, "keyboard  " + String(keyboardUp ? "OK" : "NOT FOUND"),
                 keyboardUp ? ST77XX_GREEN : ST77XX_RED);
  row = bodyText(row, "modem     " + String(modemUp ? "OK" : "NOT RESPONDING"),
                 modemUp ? ST77XX_GREEN : ST77XX_RED);

  if (modemUp) {
    String reply;
    if (modem.command("AT+COPS?", &reply, 5000)) {
      const int q = reply.indexOf('"');
      const int q2 = (q >= 0) ? reply.indexOf('"', q + 1) : -1;
      if (q2 > q) row = bodyText(row, "operator  " + reply.substring(q + 1, q2), DIM);
    }
    const String t = modem.networkTime();
    if (t.length() > 0) row = bodyText(row, "net time  " + t, DIM);
    row = bodyText(row, "signal    " + String(csq) + "/31", DIM);
    row = bodyText(row, "sms slots " + String(smsUsed) + " of " + String(smsTotal), DIM);
  }

  row++;
  tft.drawFastHLine(0, BODY_Y + row * LINE_H - 4, SCREEN_W, BAR_BG);
  row = bodyText(row, "missed calls", ST77XX_WHITE);
  if (missedCount == 0) {
    row = bodyText(row, "  none", DIM);
  } else {
    for (int i = 0; i < missedCount && i < 5; i++) {
      row = bodyText(row, "  " + missedCalls[i].when + "  " + displayName(missedCalls[i].number),
                     i < missedNew ? ST77XX_ORANGE : DIM);
    }
  }
  missedNew = 0;  // seen; the badge clears on the next status bar draw

  row++;
  tft.drawFastHLine(0, BODY_Y + row * LINE_H - 4, SCREEN_W, BAR_BG);
  bodyText(row, "key test: " + (statusEcho.length() ? statusEcho : String("press any key")),
           ST77XX_YELLOW);
  drawStatusBar();
}

// ------------------------------------------------------------------- modem
void refreshVitals() {
  if (!modemUp) return;
  csq = modem.signalQuality();
  int u, t;
  if (modem.smsSlots(u, t)) { smsUsed = u; smsTotal = t; }
  drawStatusBar();
}

void onNotification(const String &line) {
  LOGI("event", "%s", line.c_str());

  if (line.startsWith("+CMTI:")) {
    if (screen == Screen::ComposeTo || screen == Screen::ComposeBody) {
      inboxStale = true;  // don't run modem commands under the user's typing
      toast("New message -- inbox on ESC");
      return;
    }
    loadInbox();
    if (inboxCount > 0) {
      toast("New message from " + displayName(inbox[0].sender));
    }
    if (screen == Screen::Inbox) drawInbox();
    else drawStatusBar();  // refresh unread badge once the toast fades
  } else if (line.startsWith("RING") || line.startsWith("+CLIP")) {
    toast(line.startsWith("+CLIP") ? "Call: " + line : "Incoming call", ST77XX_RED);
  } else if (line.startsWith("MISSED_CALL:")) {
    // SIM7600 format:  MISSED_CALL: 03:35PM 16175551234
    String rest = line.substring(12);
    rest.trim();
    const int space = rest.lastIndexOf(' ');

    for (int i = min(missedCount, MAX_MISSED - 1); i > 0; i--) {
      missedCalls[i] = missedCalls[i - 1];
    }
    missedCalls[0].when = (space > 0) ? rest.substring(0, space) : String("");
    missedCalls[0].number = (space > 0) ? rest.substring(space + 1) : rest;
    if (missedCount < MAX_MISSED) missedCount++;
    missedNew++;

    toast("Missed call: " + displayName(missedCalls[0].number), ST77XX_RED);
    if (screen == Screen::Status) drawStatusScreen();
  }
}

// ---------------------------------------------------------------- keyboard
uint8_t readKey() {
  if (!keyboardUp) return 0;
  Wire.requestFrom((uint8_t)KEYBOARD_I2C_ADDR, (uint8_t)1);
  if (!Wire.available()) return 0;
  return Wire.read();
}

void deleteAndReload(int slot) {
  const bool ok = modem.deleteSms(slot);
  LOGI("sms", "delete slot %d: %s", slot, ok ? "ok" : "FAILED");
  loadInbox();
  refreshVitals();
  screen = Screen::Inbox;
  drawInbox();
}

void handleKey(uint8_t k) {
  switch (screen) {
    case Screen::Boot:
      break;  // boot advances on its own

    case Screen::Inbox:
      if (k == Key::Up && selected > 0) { selected--; drawInbox(); }
      else if (k == Key::Down && selected < inboxCount - 1) { selected++; drawInbox(); }
      else if (k == Key::Enter && inboxCount > 0) {
        if (loadMessage(inbox[selected].slot)) {
          if (inbox[selected].unread) { inbox[selected].unread = false; unreadCount--; }
          screen = Screen::Read;
          drawRead();
        }
      }
      else if ((k == 'd' || k == 'D') && inboxCount > 0) deleteAndReload(inbox[selected].slot);
      else if (k == 'c' || k == 'C') {
        composeTo = ""; composeBody = ""; composeInput = ""; composeName = "";
        composeSel = -1; composeCursor = 0;
        screen = Screen::ComposeTo; drawCompose();
      }
      else if (k == 'r' || k == 'R') { loadInbox(); drawInbox(); }
      else if (k == Key::Tab) { screen = Screen::Status; drawStatusScreen(); }
      break;

    case Screen::Read:
      if (k == Key::Down) { readScroll++; drawRead(); }
      else if (k == Key::Up && readScroll > 0) { readScroll--; drawRead(); }
      else if (k == 'd' || k == 'D') deleteAndReload(openSlot);
      else if (k == 'r' || k == 'R') {
        composeTo = openSms.sender; composeBody = ""; composeCursor = 0;
        const char *known = contactName(composeTo);
        composeName = known ? known : "";
        screen = Screen::ComposeBody; drawCompose();
      }
      else if (k == Key::Esc || k == Key::Tab) enterInbox();
      break;

    case Screen::ComposeTo: {
      int idx[CONTACT_COUNT];
      const int n = matchingContacts(idx);
      if (k == Key::Down || k == Key::Tab) {
        if (n > 0) { composeSel = (composeSel + 1) % n; drawCompose(); }
      }
      else if (k == Key::Up) {
        if (n > 0) { composeSel = (composeSel <= 0) ? n - 1 : composeSel - 1; drawCompose(); }
      }
      else if (k == Key::Enter) {
        if (composeSel >= 0 && composeSel < n) {
          composeName = CONTACTS[idx[composeSel]].name;
          composeTo = CONTACTS[idx[composeSel]].number;
          composeCursor = 0;
          screen = Screen::ComposeBody; drawCompose();
        } else if (composeInput.length() >= 3) {
          composeTo = normalizeNumber(composeInput);
          const char *known = contactName(composeTo);
          composeName = known ? known : "";
          composeCursor = 0;
          screen = Screen::ComposeBody; drawCompose();
        }
      }
      else if (k == Key::Backspace && composeInput.length() > 0) {
        composeInput.remove(composeInput.length() - 1);
        composeSel = -1; drawCompose();
      }
      else if (k == Key::Esc) enterInbox();
      else if (Key::isPrintable(k) && composeInput.length() < 20) {
        composeInput += (char)k;
        composeSel = -1; drawCompose();
      }
      break;
    }

    case Screen::ComposeBody: {
      const int len = composeBody.length();
      if (k == Key::Enter && len > 0) {
        toast("sending...", BAR_BG);
        const bool ok = modem.sendSms(composeTo, composeBody);
        LOGI("sms", "send to %s: %s", composeTo.c_str(), ok ? "ok" : "FAILED");
        enterInbox();
        toast(ok ? "sent" : "SEND FAILED", ok ? TOAST_BG : ST77XX_RED);
      }
      else if (k == Key::Left && composeCursor > 0) { composeCursor--; drawCompose(); }
      else if (k == Key::Right && composeCursor < len) { composeCursor++; drawCompose(); }
      else if (k == Key::Up && composeCursor > 0) {
        composeCursor = max(0, composeCursor - COLS); drawCompose();
      }
      else if (k == Key::Down && composeCursor < len) {
        composeCursor = min(len, composeCursor + COLS); drawCompose();
      }
      else if (k == Key::Backspace && composeCursor > 0) {
        composeBody.remove(composeCursor - 1, 1);
        composeCursor--; drawCompose();
      }
      else if (k == Key::Esc) enterInbox();
      else if (Key::isPrintable(k) && len < MAX_COMPOSE_CHARS) {
        composeBody = composeBody.substring(0, composeCursor) + String((char)k) +
                      composeBody.substring(composeCursor);
        composeCursor++; drawCompose();
      }
      break;
    }

    case Screen::Status:
      if (k == Key::Esc || k == Key::Tab) enterInbox();
      else {
        statusEcho = Key::isPrintable(k)
                         ? "'" + String((char)k) + "'  (0x" + String(k, HEX) + ")"
                         : "code 0x" + String(k, HEX);
        drawStatusScreen();
      }
      break;
  }
}

// --------------------------------------------------------- serial commands
void serialHelp() {
  Serial.println("commands:");
  Serial.println("  AT...   send any AT command to the modem, print the reply");
  Serial.println("  status  modem health check");
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
    String reply;
    modem.command("ATI", &reply, 5000);      Serial.println(reply);
    reply = ""; modem.command("AT+CPIN?", &reply, 5000); Serial.println(reply);
    reply = ""; modem.command("AT+CREG?", &reply, 5000); Serial.println(reply);
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

// -------------------------------------------------------------------- boot
// One checklist line: name, then an [ok]/[--] verdict.
void bootLine(int row, const char *name, bool ok, const char *detail = "") {
  const int y = 90 + row * 14;
  tft.setTextSize(1);
  tft.setCursor(40, y);
  tft.setTextColor(ok ? ST77XX_GREEN : ST77XX_RED);
  tft.print(ok ? "[ok] " : "[--] ");
  tft.setTextColor(ST77XX_WHITE);
  tft.print(name);
  if (detail[0]) {
    tft.setTextColor(DIM);
    tft.print("  ");
    tft.print(detail);
  }
}

void setup() {
  Serial.begin(CONSOLE_BAUD);
  const uint32_t start = millis();
  while (!Serial && millis() - start < 3000) delay(10);
  serialHelp();

  tftSPI.begin();
  tft.init(SCREEN_W, SCREEN_H);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);

  tft.setTextSize(2);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(50, 50);
  tft.print("diy-phone");
  tft.setTextColor(DIM);
  tft.setCursor(96, 70);
  tft.setTextSize(1);
  tft.print(VERSION);

  // Keyboard: without it there is no UI, so keep probing until it appears.
  Wire.begin();
  for (;;) {
    Wire.beginTransmission(KEYBOARD_I2C_ADDR);
    keyboardUp = (Wire.endTransmission() == 0);
    bootLine(0, "keyboard", keyboardUp, keyboardUp ? "" : "SDA->8 SCL->7?");
    if (keyboardUp) break;
    delay(1000);
  }
  LOGI("boot", "keyboard ok");

  // Modem: the SIM7600 can take ~20s after power-on. Give it 30, then carry
  // on regardless -- the inbox will say it is offline, and loop() retries.
  bootLine(1, "modem", false, "waiting...");
  for (int i = 0; i < 15 && !modemUp; i++) {
    modemUp = modem.begin(MODEM_BAUD, Pins::MODEM_RX, Pins::MODEM_TX);
    if (!modemUp) delay(2000);
  }
  bootLine(1, "modem", modemUp, modemUp ? "" : "TX->4 RX->3? power?");

  bool simOk = false, netOk = false;
  if (modemUp) {
    String reply;
    simOk = modem.command("AT+CPIN?", &reply, 5000) && reply.indexOf("READY") >= 0;
    bootLine(2, "SIM card", simOk);

    reply = "";
    // +CREG: 0,1 registered home / 0,5 roaming. Not worth blocking on.
    netOk = modem.command("AT+CREG?", &reply, 5000) &&
            (reply.indexOf(",1") >= 0 || reply.indexOf(",5") >= 0);
    bootLine(3, "network", netOk, netOk ? "" : "searching...");

    modem.enableCallerId();
    modem.textMode();
    modem.onNotification(onNotification);
    refreshVitals();
    loadInbox();
  }

  delay(modemUp && simOk ? 800 : 2500);  // let the checklist be read
  tft.fillScreen(ST77XX_BLACK);
  screen = Screen::Inbox;
  drawInbox();
  if (unreadCount > 0) {
    toast(String(unreadCount) + " unread message" + (unreadCount > 1 ? "s" : ""));
  }
}

void loop() {
  static uint32_t lastVitals = 0;
  static bool wasToasting = false;

  if (!modemUp) {  // boot gave up; keep trying in the background
    modemUp = modem.isResponding();
    if (modemUp) {
      modem.enableCallerId();
      modem.textMode();
      modem.onNotification(onNotification);
      refreshVitals();
      loadInbox();
      if (screen == Screen::Inbox) drawInbox();
      toast("modem online");
    }
  }

  if (millis() - lastVitals >= 5000) {
    lastVitals = millis();
    refreshVitals();
  }

  // When a toast expires, put the status bar back.
  const bool toasting = millis() < toastUntil;
  if (wasToasting && !toasting) drawStatusBar();
  wasToasting = toasting;

  const uint8_t k = readKey();
  if (k != 0) handleKey(k);

  modem.poll();
  pollSerial();
  delay(10);
}
