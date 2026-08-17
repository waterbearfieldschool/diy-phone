// main.cpp -- v45e: the v44 phone on a 1.54" black-and-white e-ink panel
// (Waveshare 200x200, SSD1681, driven by GxEPD2).
//
// E-ink changes how drawing works: everything renders into a RAM buffer and
// is pushed to the panel by flush() -- a quick partial refresh normally, a
// full refresh every couple dozen updates to clear ghosting. The status bar
// repaints only when its numbers change, so the panel is not flashing on a
// timer. Color collapses to ink-on-paper: selection inverts, unread carries
// a * marker, alerts an ! marker.
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

#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <GxEPD2_BW.h>
#include <SPI.h>
#include <Wire.h>

#include "Config.h"
#include "Log.h"
#include "Modem.h"
#include "Timestamp.h"

SPIClass epdSPI(NRF_SPIM2, Pins::TFT_MISO, Pins::TFT_SCK, Pins::TFT_MOSI);
// Waveshare has shipped the 1.54" V2 board with two different panels. With
// the wrong driver, FULL refresh still looks fine but PARTIAL refresh shows
// inverse ghosts of the previous frame -- exactly the garble we saw. If
// partial refresh misbehaves, swap the class:
//   GxEPD2_150_BN  -- DEPG0150BN panels (newer batches)
//   GxEPD2_154_D67 -- GDEH0154D67 panels (earlier batches)
#define PANEL GxEPD2_154_D67
GxEPD2_BW<PANEL, PANEL::HEIGHT> display(
    PANEL(Pins::TFT_CS, Pins::TFT_DC, Pins::TFT_RST, Pins::EPD_BUSY));
Modem modem(&Serial1);

constexpr char VERSION[] = "v51e";

// ------------------------------------------------------------------ layout
constexpr int STATUS_H = 14;             // top bar
constexpr int FOOTER_H = 12;             // bottom hint bar
constexpr int BODY_Y = STATUS_H + 2;
constexpr int FOOTER_Y = SCREEN_H - FOOTER_H;
constexpr int BODY_H = FOOTER_Y - BODY_Y - 2;
// Body text is FreeMono 9pt: an 11x18 cell, ~1.5x the classic font. Chrome
// (status bar, footer, toasts, boot checklist) stays at the small 6px size.
constexpr int BIG_W = 11;
constexpr int BIG_H = 18;
constexpr int BIG_BASE = 13;  // FreeFonts draw from the baseline, not the top
constexpr int BODY_ROWS = BODY_H / BIG_H;
constexpr int COLS = 17;  // big-font columns: 2 + 17*11 = 189 of 200px

// Two tones only: ink on paper. Selection inverts; markers carry emphasis.
constexpr uint16_t INK   = GxEPD_BLACK;
constexpr uint16_t PAPER = GxEPD_WHITE;

// Drawing goes into the buffer; flush() pushes it to the panel. Partial
// refreshes are quick and quiet, but they accumulate ghosting, so every
// couple dozen the panel gets a full (blinking) refresh instead.
int partialsSinceFull = 0;
void flush(bool full = false) {
  if (full || partialsSinceFull >= 3) {
    display.display(false);
    partialsSinceFull = 0;
  } else {
    display.display(true);
    partialsSinceFull++;
  }
}
// The next flush() becomes a full refresh -- call after clearing the whole
// screen, so the old content can't linger as ghosting.
void requestFullRefresh() { partialsSinceFull = 1000; }

// Refresh after a status-bar-only change (toasts, badges). A window-only
// refresh would be marginally quicker, but mixing displayWindow() with
// full-frame partial refreshes desyncs the panel's previous-frame memory
// and garbles later updates -- so always flush the whole frame.
void flushBar() { flush(); }

// For small in-place changes (typing, caret moves, key echo): a partial
// refresh that doesn't advance the full-refresh countdown, so composing a
// message doesn't blink every third keystroke.
void flushQuiet() { display.display(true); }

// Body content renders in the big font; every chrome drawer switches back.
// Bold shares FreeMono's 11px cell, so mixing weights keeps the grid.
void bigFont() { display.setFont(&FreeMono9pt7b); }
void boldFont() { display.setFont(&FreeMonoBold9pt7b); }
void chromeFont() { display.setFont(nullptr); }

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

// The phone's timezone, learned from the network clock (NITZ) so message
// times show as local wall time no matter what zone the sender was in.
int16_t localTzMin = DEFAULT_TZ_MIN;
bool tzKnown = false;
uint32_t lastTzSync = 0;

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
int signalBucket() {
  return (csq >= 20) ? 4 : (csq >= 14) ? 3 : (csq >= 8) ? 2
         : (csq >= 1 && csq != 99) ? 1 : 0;
}

void drawSignalBars(int x, int y) {
  const int strength = signalBucket();
  for (int i = 0; i < 4; i++) {
    const int h = 3 + i * 3;
    if (i < strength) display.fillRect(x + i * 4, y + 12 - h, 3, h, INK);
    else display.drawRect(x + i * 4, y + 12 - h, 3, h, INK);
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
  chromeFont();
  if (millis() < toastUntil) return;  // a toast owns the bar right now
  display.fillRect(0, 0, SCREEN_W, STATUS_H, PAPER);
  display.drawFastHLine(0, STATUS_H - 1, SCREEN_W, INK);
  drawSignalBars(2, 1);

  display.setTextSize(1);
  display.setTextColor(INK);
  display.setCursor(24, 3);
  if (unreadCount > 0) {
    display.print("*");
    display.print(unreadCount);
  }
  if (missedNew > 0) {
    if (unreadCount > 0) display.print(" ");
    display.print("!");
    display.print(missedNew);
  }

  const String title = screenTitle();
  display.setCursor((SCREEN_W - (int)title.length() * CHAR_W) / 2, 3);
  display.print(title);

  // No color for the storage warning; "!" marks full instead.
  String store = String(smsUsed) + "/" + String(smsTotal);
  if (smsTotal > 0 && smsUsed >= smsTotal) store += "!";
  display.setCursor(SCREEN_W - (int)store.length() * CHAR_W - 4, 3);
  display.print(store);
}

void toast(const String &text, uint16_t bg = INK) {
  (void)bg;  // one ink: every toast is an inverted strip
  toastUntil = millis() + 4000;
  chromeFont();
  display.fillRect(0, 0, SCREEN_W, STATUS_H, INK);
  display.setTextSize(1);
  display.setTextColor(PAPER);
  display.setCursor(4, 3);
  display.print(text.substring(0, 32));
  flushBar();
}

void drawFooter(const char *hints) {
  chromeFont();
  display.fillRect(0, FOOTER_Y, SCREEN_W, FOOTER_H, PAPER);
  display.drawFastHLine(0, FOOTER_Y, SCREEN_W, INK);
  display.setTextSize(1);
  display.setTextColor(INK);
  display.setCursor(2, FOOTER_Y + 3);
  display.print(hints);
}

void clearBody() { display.fillRect(0, BODY_Y, SCREEN_W, BODY_H + 2, PAPER); }

// Prints wrapped text starting at row `row` of the body; returns next row.
int bodyText(int row, const String &text, uint16_t color) {
  bigFont();
  display.setTextSize(1);
  display.setTextColor(color);
  for (unsigned pos = 0; pos == 0 || pos < text.length(); pos += COLS) {
    if (row >= BODY_ROWS) break;
    display.setCursor(2, BODY_Y + row * BIG_H + BIG_BASE);
    display.print(text.substring(pos, pos + COLS));
    row++;
  }
  chromeFont();
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
      if (m.preview.length() < 160) {
        if (m.preview.length() > 0) m.preview += ' ';
        m.preview += line.substring(0, 160 - m.preview.length());
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

// Prints text flowing left-to-right across an entry's two rows, starting at
// character column `col` (0..2*COLS-1); returns the column after the text.
int entryChars(const String &text, int col, int y0, bool bold) {
  if (bold) boldFont(); else bigFont();
  for (unsigned i = 0; i < text.length() && col < 2 * COLS; i++, col++) {
    display.setCursor(2 + (col % COLS) * BIG_W,
                      y0 + (col / COLS) * BIG_H + BIG_BASE);
    display.print(text[i]);
  }
  return col;
}

// One two-row preview entry at height y: bold name|time, flowing preview.
void drawEntry(int i, int y) {
  const Entry &m = inbox[i];
  display.setTextColor(INK);

  const String hm = Timestamp::hhmmAt(m.when, localTzMin);
  int col = entryChars(displayName(m.sender).substring(0, 10) + "|" +
                           ((hm.length() > 0) ? hm : String("--:--")),
                       0, y, true);
  col = entryChars("|", col, y, m.unread);
  entryChars(m.preview, col, y, m.unread);
}

void drawInbox() {
  clearBody();
  drawFooter("ENT read  C new  D del  R  TAB");

  if (inboxCount == 0) {
    int r = bodyText(1, modemUp ? "inbox empty" : "modem offline", INK);
    if (modemUp) bodyText(r + 1, "C: compose", INK);
    drawStatusBar();
    flush();
    return;
  }

  // Scrolled below the newest message: a big "(triangle) N newer" line at
  // the top. At the latest message there is no line at all -- its absence
  // is the you-are-at-the-top signal.
  // The top line is ALWAYS reserved so the layout below it never shifts --
  // an appearing/disappearing line would move every pixel under it, which
  // is the worst case for e-ink partial refresh. At the newest message it
  // reads "latest"; scrolled it shows a triangle and the count above.
  display.setTextSize(1);
  bigFont();
  display.setTextColor(INK);
  if (selected > 0) {
    display.fillTriangle(6, BODY_Y + 14, 16, BODY_Y + 14, 11, BODY_Y + 3, INK);
    display.setCursor(2 + 2 * BIG_W, BODY_Y + BIG_BASE);
    display.print(String(selected) + " newer");
  } else {
    display.setCursor(2, BODY_Y + BIG_BASE);
    display.print("latest");
  }

  // The selected message, featured below with its full text; the messages
  // after it follow as previews.
  const int y0 = BODY_Y + BIG_H;
  const int rowsAvail = BODY_ROWS - 1;

  const Entry &f = inbox[selected];
  const int featuredBodyRows =
      max(1, min((int)((f.preview.length() + COLS - 1) / COLS), 4));
  const int featuredRows = 1 + featuredBodyRows;

  // Featured header: bold with a heavy underline as the cursor bar. (An
  // inverted bar looked better but is the worst case for e-ink partial
  // refresh -- large fills come out half-driven.)
  display.setTextColor(INK);
  const String hm = Timestamp::hhmmAt(f.when, localTzMin);
  boldFont();
  display.setCursor(2, y0 + BIG_BASE);
  display.print((displayName(f.sender).substring(0, 11) + "|" +
                 ((hm.length() > 0) ? hm : String("--:--")))
                    .substring(0, COLS));
  display.fillRect(0, y0 + BIG_H - 2, SCREEN_W, 2, INK);

  // The featured message's text, ink on paper; bold while unread.
  display.setTextColor(INK);
  if (f.unread) boldFont(); else bigFont();
  for (int r = 0; r < featuredBodyRows; r++) {
    String chunk = f.preview.substring(r * COLS, (r + 1) * COLS);
    if (r == featuredBodyRows - 1 &&
        f.preview.length() > (unsigned)(featuredBodyRows * COLS)) {
      chunk = chunk.substring(0, COLS - 3) + "...";
    }
    display.setCursor(2, y0 + (1 + r) * BIG_H + BIG_BASE);
    display.print(chunk);
  }

  // Pack previews below: a short entry takes one row, a wrapping one two.
  int row = featuredRows;
  for (int i = selected + 1; i < inboxCount; i++) {
    const unsigned chars = min((unsigned)displayName(inbox[i].sender).length(), 10u) +
                           7 + inbox[i].preview.length();  // 7 = "|HH:MM|"
    const int need = (chars > (unsigned)COLS) ? 2 : 1;
    if (row + need > rowsAvail) break;
    drawEntry(i, y0 + row * BIG_H);
    row += need;
  }
  chromeFont();
  drawStatusBar();
  flush();
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
  drawFooter("UP/DN  D del  R reply  ESC");

  // Bold name|time, the sender's number beneath, a rule, then the message.
  Timestamp::Stamp when;
  const char *known = contactName(openSms.sender);
  String head = known ? String(known).substring(0, 11) : String("<unknown>");
  if (Timestamp::parse(openSms.atTime, when)) {
    head += "|" + Timestamp::hhmmAt(when, localTzMin);
  }
  boldFont();
  display.setTextSize(1);
  display.setTextColor(INK);
  display.setCursor(2, BODY_Y + BIG_BASE);
  display.print(head.substring(0, COLS));
  chromeFont();

  int row = bodyText(1, openSms.sender, INK);
  display.drawFastHLine(0, BODY_Y + row * BIG_H + 2, SCREEN_W, INK);
  row++;

  // Body, scrolled by whole rows.
  const int bodyStart = row;
  bigFont();
  unsigned pos = (unsigned)readScroll * COLS;
  for (int r = bodyStart; r < BODY_ROWS && pos < openSms.body.length(); r++, pos += COLS) {
    display.setCursor(2, BODY_Y + r * BIG_H + BIG_BASE);
    display.setTextColor(INK);
    display.print(openSms.body.substring(pos, pos + COLS));
  }
  chromeFont();
  if (pos < openSms.body.length()) {
    display.setCursor(SCREEN_W - 3 * CHAR_W - 4, FOOTER_Y - LINE_H);
    display.setTextColor(INK);
    display.print("...");
  }
  drawStatusBar();
  flush();
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
    drawFooter("name/number  UP/DN  ENT  ESC");
    int row = bodyText(0, "To: " + composeInput + (composeSel < 0 ? "_" : ""), INK);
    display.drawFastHLine(0, BODY_Y + row * BIG_H + 2, SCREEN_W, INK);
    row++;

    int idx[CONTACT_COUNT];
    const int n = matchingContacts(idx);
    bigFont();
    for (int i = 0; i < n && row < BODY_ROWS; i++, row++) {
      const int y = BODY_Y + row * BIG_H;
      const bool sel = (i == composeSel);
      if (sel) display.fillRect(0, y, SCREEN_W, BIG_H, INK);
      display.setCursor(2, y + BIG_BASE);
      display.setTextColor(sel ? PAPER : INK);
      display.print(String(CONTACTS[idx[i]].name).substring(0, COLS));
    }
    chromeFont();
  } else {
    drawFooter("type msg  ENT send  ESC");
    String to = composeName.length() ? composeName + "  " + composeTo : composeTo;
    int row = bodyText(0, "To: " + to, INK);
    display.drawFastHLine(0, BODY_Y + row * BIG_H + 2, SCREEN_W, INK);
    const int start = row + 1;
    bodyText(start, composeBody, INK);
    // The caret: a cyan underline beneath the character cell it sits on.
    const int crow = start + composeCursor / COLS;
    const int ccol = composeCursor % COLS;
    if (crow < BODY_ROWS) {
      display.drawFastHLine(2 + ccol * BIG_W, BODY_Y + (crow + 1) * BIG_H - 2,
                        BIG_W, INK);
    }
    const String count = String(composeBody.length()) + "/" + String(MAX_COMPOSE_CHARS);
    display.setCursor(SCREEN_W - (int)count.length() * CHAR_W - 4, FOOTER_Y - LINE_H);
    display.setTextColor(INK);
    display.print(count);
  }
  drawStatusBar();
  flushQuiet();
}

// ------------------------------------------------------------------ status
void drawStatusScreen() {
  clearBody();
  drawFooter("press keys to test  ESC back");

  int row = 0;
  row = bodyText(row, "diy-phone " + String(VERSION), INK);
  row = bodyText(row, String(keyboardUp ? "kb OK" : "kb ??") +
                          (modemUp ? "  modem OK" : "  modem ??"), INK);
  if (modemUp) {
    row = bodyText(row, "sig " + String(csq) + "  sms " + String(smsUsed) +
                            "/" + String(smsTotal), INK);
    String reply;
    if (modem.command("AT+COPS?", &reply, 5000)) {
      const int q = reply.indexOf('"');
      const int q2 = (q >= 0) ? reply.indexOf('"', q + 1) : -1;
      if (q2 > q) row = bodyText(row, reply.substring(q + 1, q2), INK);
    }
  }
  row = bodyText(row, "ram " + String(freeRam() / 1024) + "k", INK);

  row = bodyText(row, "missed:", INK);
  if (missedCount == 0) {
    row = bodyText(row, " none", INK);
  } else {
    for (int i = 0; i < missedCount && i < 2; i++) {
      row = bodyText(row, " " + displayName(missedCalls[i].number), INK);
    }
  }
  missedNew = 0;  // seen; the badge clears on the next status bar draw

  bodyText(row, "key: " + (statusEcho.length() ? statusEcho : String("...")), INK);
  drawStatusBar();
  flushQuiet();
}

// ------------------------------------------------------------------- modem
// AT+CCLK? returns the carrier's idea of local time, offset included. Sync
// until it works, then hourly (to ride through DST changes).
void syncTimezone() {
  Timestamp::Stamp now;
  if (Timestamp::parse(modem.networkTime(), now)) {
    if (now.tzMin != localTzMin) LOGI("time", "tz offset now %d min", now.tzMin);
    localTzMin = now.tzMin;
    tzKnown = true;
    lastTzSync = millis();
  }
}

void refreshVitals() {
  if (!modemUp) return;
  csq = modem.signalQuality();
  int u, t;
  if (modem.smsSlots(u, t)) { smsUsed = u; smsTotal = t; }
  if (!tzKnown || millis() - lastTzSync >= 3600000UL) syncTimezone();

  // Repaint the bar only when a number it shows changed -- an e-ink panel
  // should not blink on a five-second timer.
  static int shownBars = -1, shownUsed = -1, shownTotal = -1;
  static int shownUnread = -1, shownMissed = -1;
  const int bars = signalBucket();
  if (bars != shownBars || smsUsed != shownUsed || smsTotal != shownTotal ||
      unreadCount != shownUnread || missedNew != shownMissed) {
    shownBars = bars; shownUsed = smsUsed; shownTotal = smsTotal;
    shownUnread = unreadCount; shownMissed = missedNew;
    if (millis() >= toastUntil) { drawStatusBar(); flushBar(); }
  }
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
    toast(line.startsWith("+CLIP") ? "Call: " + line : "Incoming call", INK);
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

    toast("Missed call: " + displayName(missedCalls[0].number), INK);
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
        toast("sending...", INK);
        const bool ok = modem.sendSms(composeTo, composeBody);
        LOGI("sms", "send to %s: %s", composeTo.c_str(), ok ? "ok" : "FAILED");
        enterInbox();
        toast(ok ? "sent" : "SEND FAILED", ok ? INK : INK);
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
  const int y = 76 + row * 14;
  display.fillRect(0, y - 1, SCREEN_W, LINE_H + 2, PAPER);  // clear the old verdict
  display.setTextSize(1);
  display.setCursor(24, y);
  display.setTextColor(INK);
  display.print(ok ? "[ok] " : "[--] ");
  display.setTextColor(INK);
  display.print(name);
  if (detail[0]) {
    display.setTextColor(INK);
    display.print("  ");
    display.print(detail);
  }
}

void setup() {
  Serial.begin(CONSOLE_BAUD);
  const uint32_t start = millis();
  while (!Serial && millis() - start < 3000) delay(10);
  serialHelp();

  epdSPI.begin();
  display.epd2.selectSPI(epdSPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  display.init(0);
  display.setRotation(0);  // 0-3; change to match how the panel is mounted
  display.fillScreen(PAPER);
  display.setTextWrap(false);

  display.setTextSize(2);
  display.setTextColor(INK);
  display.setCursor(46, 30);
  display.print("diy-phone");
  display.setCursor(85, 54);
  display.setTextSize(1);
  display.print(VERSION);

  // Keyboard: without it there is no UI, so keep probing until it appears.
  // Probe silently; repaint only when the verdict changes (e-ink).
  Wire.begin();
  Wire.beginTransmission(KEYBOARD_I2C_ADDR);
  keyboardUp = (Wire.endTransmission() == 0);
  bootLine(0, "keyboard", keyboardUp, keyboardUp ? "" : "SDA->8 SCL->7?");
  flush(true);  // first paint: a full refresh clears the panel
  while (!keyboardUp) {
    delay(1000);
    Wire.beginTransmission(KEYBOARD_I2C_ADDR);
    keyboardUp = (Wire.endTransmission() == 0);
    if (keyboardUp) { bootLine(0, "keyboard", true, ""); flush(); }
  }
  LOGI("boot", "keyboard ok");

  // Modem: the SIM7600 can take ~20s after power-on. Give it 30, then carry
  // on regardless -- the inbox will say it is offline, and loop() retries.
  bootLine(1, "modem", false, "waiting...");
  flush();
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

  flush();  // show the finished checklist
  delay(modemUp && simOk ? 800 : 2500);  // let it be read
  display.fillScreen(PAPER);
  requestFullRefresh();  // wipe the boot screen completely, no ghosting
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
  if (wasToasting && !toasting) { drawStatusBar(); flushBar(); }
  wasToasting = toasting;

  const uint8_t k = readKey();
  if (k != 0) handleKey(k);

  modem.poll();
  pollSerial();
  delay(10);
}
