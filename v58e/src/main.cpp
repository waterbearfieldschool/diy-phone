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

#include <GxEPD2_BW.h>
#include <SPI.h>
#include <Wire.h>

#include "Config.h"
#include "Log.h"
#include "Modem.h"
#include "Timestamp.h"

SPIClass epdSPI(NRF_SPIM2, Pins::TFT_MISO, Pins::TFT_SCK, Pins::TFT_MOSI);
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(
    GxEPD2_154_D67(Pins::TFT_CS, Pins::TFT_DC, Pins::TFT_RST, Pins::EPD_BUSY));
Modem modem(&Serial1);

constexpr char VERSION[] = "v58e";

// ------------------------------------------------------------------ layout
constexpr int STATUS_H = 14;             // top bar
constexpr int FOOTER_H = 12;             // bottom hint bar
constexpr int BODY_Y = STATUS_H + 2;
constexpr int FOOTER_Y = SCREEN_H - FOOTER_H;
constexpr int BODY_H = FOOTER_Y - BODY_Y - 2;
constexpr int BODY_ROWS = BODY_H / LINE_H;
// Rows start at x=2; 32 six-pixel characters end at x=194 on the 200px panel.
constexpr int COLS = 32;

// Two tones only: ink on paper. Selection inverts; markers carry emphasis.
constexpr uint16_t INK   = GxEPD_BLACK;
constexpr uint16_t PAPER = GxEPD_WHITE;

// Drawing goes into the buffer; flush() pushes it to the panel. Partial
// refreshes are quick and quiet, but they accumulate ghosting, so every
// couple dozen the panel gets a full (blinking) refresh instead.
int partialsSinceFull = 0;
void flush(bool full = false) {
  if (full || partialsSinceFull >= 24) {
    display.display(false);
    partialsSinceFull = 0;
  } else {
    display.display(true);
    partialsSinceFull++;
  }
}
// Refresh just the status-bar strip (toasts, badge changes).
void flushBar() {
  display.displayWindow(0, 0, SCREEN_W, STATUS_H);
  partialsSinceFull++;
}

// ------------------------------------------------------------------- state
enum class Screen { Boot, Inbox, Read, ComposeTo, ComposeBody, Status, Call, Dial };
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

// Voice call state (ported from v53e/v54e). The SIM7600's audio (speaker
// amp + onboard mic) does the work; we route, answer, dial, hang up.
enum class CallState { None, Ringing, Dialing, Active };
CallState callState = CallState::None;
String callPeer;            // the other end's number, from +CLIP or the dial
uint32_t callStart = 0;
uint8_t speakerVolume = 3;  // AT+CLVL, 0-5
uint8_t micGain = 5;        // AT+CMICGAIN, 0-8
String dialInput;           // digits typed on the free-form dial pad
bool pickerForCall = false; // the ComposeTo picker doubles as the call picker

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
    case Screen::ComposeTo:   return pickerForCall ? "call" : "compose";
    case Screen::ComposeBody: return "compose";
    case Screen::Status:      return "status";
    case Screen::Call:        return "call";
    case Screen::Dial:        return "dial";
    default:                  return VERSION;
  }
}

void drawStatusBar() {
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
  display.fillRect(0, 0, SCREEN_W, STATUS_H, INK);
  display.setTextSize(1);
  display.setTextColor(PAPER);
  display.setCursor(4, 3);
  display.print(text.substring(0, COLS - 1));
  flushBar();
}

void drawFooter(const char *hints) {
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
  display.setTextSize(1);
  display.setTextColor(color);
  for (unsigned pos = 0; pos == 0 || pos < text.length(); pos += COLS) {
    if (row >= BODY_ROWS) break;
    display.setCursor(2, BODY_Y + row * LINE_H);
    display.print(text.substring(pos, pos + COLS));
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
  drawFooter("M:msg C:call ENT D R P 0-9 TAB");

  if (inboxCount == 0) {
    bodyText(1, modemUp ? "inbox empty" : "modem offline -- no inbox", INK);
    if (modemUp) bodyText(3, "C composes a message; text this", INK);
    if (modemUp) bodyText(4, "number to fill the inbox.", INK);
    drawStatusBar();
    flush();
    return;
  }

  // Page-jump windowing: when the selection walks off the page, leap a
  // whole screen's worth, placing the selection at the near edge of the
  // new page -- so the presses that follow move only the highlight on a
  // stable layout instead of re-flowing the list every press.
  if (selected < inboxTop) inboxTop = max(0, selected - BODY_ROWS + 1);
  if (selected >= inboxTop + BODY_ROWS) inboxTop = selected;

  display.setTextSize(1);
  for (int row = 0; row < BODY_ROWS; row++) {
    const int i = inboxTop + row;
    if (i >= inboxCount) break;
    const Entry &m = inbox[i];
    const int y = BODY_Y + row * LINE_H;

    const bool sel = (i == selected);
    // The inversion bar starts at the time column; the unread-dot column
    // stays paper on every row, so the dot reads identically whether the
    // row is selected or not.
    if (sel) display.fillRect(12, y - 1, SCREEN_W - 12, LINE_H, INK);
    const uint16_t fg = sel ? PAPER : INK;

    display.setCursor(2, y);
    display.setTextColor(INK);
    display.print(m.unread ? "*" : " ");

    const String hm = Timestamp::hhmmAt(m.when, localTzMin);
    String row40 = (hm.length() > 0 ? hm : String("--:--")) + " ";
    row40 += displayName(m.sender).substring(0, 13);
    row40 += " ";  // preview follows the name directly, no column padding
    row40 += m.preview;

    display.setCursor(2 + 2 * CHAR_W, y);
    display.setTextColor(fg);
    display.print(row40.substring(0, COLS - 2));
  }
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
  drawFooter("UP/DN D R:reply P:call ESC");

  String from = displayName(openSms.sender);
  if (from != openSms.sender) from += "  " + openSms.sender;
  int row = bodyText(0, "From " + from, INK);
  Timestamp::Stamp when;
  if (Timestamp::parse(openSms.atTime, when)) {
    row = bodyText(row, "at " + Timestamp::hhmmAt(when, localTzMin) + " local", INK);
  }
  display.drawFastHLine(0, BODY_Y + row * LINE_H + 2, SCREEN_W, INK);
  row++;

  // Body, scrolled by whole rows.
  const int bodyStart = row;
  unsigned pos = (unsigned)readScroll * COLS;
  for (int r = bodyStart; r < BODY_ROWS && pos < openSms.body.length(); r++, pos += COLS) {
    display.setCursor(2, BODY_Y + r * LINE_H);
    display.setTextColor(INK);
    display.print(openSms.body.substring(pos, pos + COLS));
  }
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
    drawFooter(pickerForCall ? "name/number  UP/DN  ENT call  ESC"
                             : "name/number  UP/DN  ENT  ESC");
    int row = bodyText(0, String(pickerForCall ? "Call: " : "To: ") + composeInput +
                              (composeSel < 0 ? "_" : ""),
                       INK);
    display.drawFastHLine(0, BODY_Y + row * LINE_H + 2, SCREEN_W, INK);
    row++;

    int idx[CONTACT_COUNT];
    const int n = matchingContacts(idx);
    for (int i = 0; i < n && row < BODY_ROWS; i++, row++) {
      const int y = BODY_Y + row * LINE_H;
      const bool sel = (i == composeSel);
      if (sel) display.fillRect(0, y - 1, SCREEN_W, LINE_H, INK);
      display.setCursor(2, y);
      display.setTextColor(sel ? PAPER : INK);
      String r = String(CONTACTS[idx[i]].name);
      while (r.length() < 10) r += ' ';
      r += CONTACTS[idx[i]].number;
      display.print(r.substring(0, COLS));
    }
  } else {
    drawFooter("type msg  ENT send  ESC");
    String to = composeName.length() ? composeName + "  " + composeTo : composeTo;
    int row = bodyText(0, "To: " + to, INK);
    display.drawFastHLine(0, BODY_Y + row * LINE_H + 2, SCREEN_W, INK);
    const int start = row + 1;
    bodyText(start, composeBody, INK);
    // The caret: a cyan underline beneath the character cell it sits on.
    const int crow = start + composeCursor / COLS;
    const int ccol = composeCursor % COLS;
    if (crow < BODY_ROWS) {
      display.drawFastHLine(2 + ccol * CHAR_W, BODY_Y + (crow + 1) * LINE_H - 2,
                        CHAR_W, INK);
    }
    const String count = String(composeBody.length()) + "/" + String(MAX_COMPOSE_CHARS);
    display.setCursor(SCREEN_W - (int)count.length() * CHAR_W - 4, FOOTER_Y - LINE_H);
    display.setTextColor(INK);
    display.print(count);
  }
  drawStatusBar();
  flush();
}

// ------------------------------------------------------------------- call
// AT+SIMTONE (arbitrary frequency) is missing from some SIM7600 firmware
// builds; AT+CPTONE's preset tones (8 = ringing tone) are the fallback.
// Probe once on the first ring and remember which one this modem speaks.
int8_t toneMethod = -1;  // -1 unprobed, 0 none, 1 SIMTONE, 2 CPTONE

void playRingBurst() {
  modem.setAudioRoute(3);
  modem.setSpeakerVolume(speakerVolume);
  if (toneMethod == -1) {
    if (modem.command("AT+SIMTONE=1,850,300,200,1800", nullptr, 1000)) toneMethod = 1;
    else if (modem.command("AT+CPTONE=8", nullptr, 1000)) toneMethod = 2;
    else { toneMethod = 0; LOGE("call", "no local ring tone support"); }
  } else if (toneMethod == 1) {
    modem.command("AT+SIMTONE=1,850,300,200,1800", nullptr, 1000);
  } else if (toneMethod == 2) {
    modem.command("AT+CPTONE=8", nullptr, 1000);
  }
}

void stopRingTone() {
  if (toneMethod == 1) modem.command("AT+SIMTONE=0", nullptr, 1000);
  else if (toneMethod == 2) modem.command("AT+CPTONE=0", nullptr, 1000);
}

void drawCall() {
  clearBody();
  drawFooter(callState == CallState::Ringing ? "ENT answer  ESC reject"
                                             : "UP/DN vol  L/R mic  ESC hang up");
  int row = 1;
  row = bodyText(row, callState == CallState::Ringing ? "incoming call"
                      : callState == CallState::Dialing ? "calling..."
                                                        : "in call",
                 INK);
  row++;
  row = bodyText(row, displayName(callPeer), INK);
  if (contactName(callPeer)) row = bodyText(row, callPeer, INK);
  row++;
  if (callState == CallState::Active) {
    const uint32_t s = (millis() - callStart) / 1000;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
    row = bodyText(row, String(buf), INK);
  }
  bodyText(row + 1, "vol " + String(speakerVolume) + "  mic " + String(micGain), INK);
  drawStatusBar();
  flush();
}

void enterCall(CallState state, const String &peer) {
  callState = state;
  callPeer = peer;
  screen = Screen::Call;
  drawCall();
}

void endCall(const char *why) {
  stopRingTone();
  callState = CallState::None;
  screen = Screen::Inbox;
  drawInbox();
  toast(why);
}

void dialNumber(const String &number) {
  if (!modemUp || callState != CallState::None) return;
  modem.setSpeakerVolume(speakerVolume);
  modem.setMicGain(micGain);
  if (modem.call(number)) {  // call() routes audio to the loudspeaker first
    callStart = millis();
    enterCall(CallState::Dialing, number);
  } else {
    toast("call failed");
  }
}

// Free-form dial pad; opened by typing a digit in the inbox.
void drawDial() {
  clearBody();
  drawFooter("0-9 type  ENT call  ESC cancel");
  int row = 1;
  row = bodyText(row, "dial: " + dialInput + "_", INK);
  row++;
  if (dialInput.length() >= 3) {
    const String full = normalizeNumber(dialInput);
    const char *known = contactName(full);
    row = bodyText(row, full, INK);  // what will actually be dialed
    if (known) bodyText(row, known, INK);
  }
  drawStatusBar();
  flush();
}

// ------------------------------------------------------------------ status
void drawStatusScreen() {
  clearBody();
  drawFooter("press keys to test  ESC back");

  int row = 0;
  row = bodyText(row, "diy-phone " + String(VERSION), INK);
  row = bodyText(row, "free RAM  " + String(freeRam()) + " bytes", INK);
  row = bodyText(row, "keyboard  " + String(keyboardUp ? "OK" : "NOT FOUND"),
                 keyboardUp ? INK : INK);
  row = bodyText(row, "modem     " + String(modemUp ? "OK" : "NOT RESPONDING"),
                 modemUp ? INK : INK);

  if (modemUp) {
    String reply;
    if (modem.command("AT+COPS?", &reply, 5000)) {
      const int q = reply.indexOf('"');
      const int q2 = (q >= 0) ? reply.indexOf('"', q + 1) : -1;
      if (q2 > q) row = bodyText(row, "operator  " + reply.substring(q + 1, q2), INK);
    }
    const String t = modem.networkTime();
    if (t.length() > 0) row = bodyText(row, "net time  " + t, INK);
    row = bodyText(row, "signal    " + String(csq) + "/31", INK);
    row = bodyText(row, "sms slots " + String(smsUsed) + " of " + String(smsTotal), INK);
  }

  row++;
  display.drawFastHLine(0, BODY_Y + row * LINE_H - 4, SCREEN_W, INK);
  row = bodyText(row, "missed calls", INK);
  if (missedCount == 0) {
    row = bodyText(row, "  none", INK);
  } else {
    for (int i = 0; i < missedCount && i < 5; i++) {
      row = bodyText(row, "  " + missedCalls[i].when + "  " + displayName(missedCalls[i].number),
                     i < missedNew ? INK : INK);
    }
  }
  missedNew = 0;  // seen; the badge clears on the next status bar draw

  row++;
  display.drawFastHLine(0, BODY_Y + row * LINE_H - 4, SCREEN_W, INK);
  bodyText(row, "key test: " + (statusEcho.length() ? statusEcho : String("press any key")),
           INK);
  drawStatusBar();
  flush();
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
  } else if (line.startsWith("RING")) {
    if (callState == CallState::None) enterCall(CallState::Ringing, "");
    if (callState == CallState::Ringing) playRingBurst();
  } else if (line.startsWith("+CLIP:")) {
    const int q = line.indexOf('"');
    const int q2 = (q >= 0) ? line.indexOf('"', q + 1) : -1;
    if (q2 > q && line.substring(q + 1, q2) != callPeer) {
      callPeer = line.substring(q + 1, q2);
      if (screen == Screen::Call) drawCall();
    }
  } else if (line.startsWith("VOICE CALL: BEGIN")) {
    callState = CallState::Active;
    callStart = millis();
    if (screen == Screen::Call) drawCall();
  } else if (line.startsWith("NO CARRIER") || line.startsWith("+CEND") ||
             line.startsWith("VOICE CALL: END")) {
    if (callState != CallState::None) endCall("call ended");
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
    stopRingTone();
    callState = CallState::None;
    if (screen == Screen::Call) {
      screen = Screen::Inbox;
      drawInbox();
    } else if (screen == Screen::Status) {
      drawStatusScreen();
    }
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
      else if (k == 'm' || k == 'M') {
        pickerForCall = false;
        // Resume a half-typed draft (left by ESC or an incoming call);
        // start fresh only when there is none.
        if (composeBody.length() > 0 && composeTo.length() > 0) {
          screen = Screen::ComposeBody;
          drawCompose();
          toast("draft resumed");
        } else if (composeInput.length() > 0 || composeBody.length() > 0) {
          screen = Screen::ComposeTo;
          drawCompose();
          toast("draft resumed");
        } else {
          composeTo = ""; composeBody = ""; composeInput = ""; composeName = "";
          composeSel = -1; composeCursor = 0;
          screen = Screen::ComposeTo; drawCompose();
        }
      }
      else if (k == 'c' || k == 'C') {
        // Call picker: the same contact list / typed number, ENTER dials.
        pickerForCall = true;
        composeInput = ""; composeSel = -1;
        screen = Screen::ComposeTo; drawCompose();
      }
      else if ((k == 'p' || k == 'P') && inboxCount > 0) dialNumber(inbox[selected].sender);
      else if (k >= '0' && k <= '9') {
        dialInput = String((char)k);
        screen = Screen::Dial;
        drawDial();
      }
      else if (k == 'r' || k == 'R') { loadInbox(); drawInbox(); }
      else if (k == Key::Esc && selected > 0) { selected = 0; drawInbox(); }
      else if (k == Key::Tab) { screen = Screen::Status; drawStatusScreen(); }
      break;

    case Screen::Read:
      if (k == Key::Down) { readScroll++; drawRead(); }
      else if (k == Key::Up && readScroll > 0) { readScroll--; drawRead(); }
      else if (k == 'p' || k == 'P') dialNumber(openSms.sender);
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
        if (pickerForCall) {
          String number;
          if (composeSel >= 0 && composeSel < n) number = CONTACTS[idx[composeSel]].number;
          else if (composeInput.length() >= 3) number = normalizeNumber(composeInput);
          if (number.length() > 0) {
            pickerForCall = false;
            composeInput = ""; composeSel = -1;
            dialNumber(number);
            if (callState == CallState::None) enterInbox();  // dial failed
          }
        } else if (composeSel >= 0 && composeSel < n) {
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
      else if (k == Key::Esc) {
        if (pickerForCall) { pickerForCall = false; composeInput = ""; composeSel = -1; }
        enterInbox();
      }
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
        if (ok) {  // a failed send stays resumable via M
          composeTo = ""; composeBody = ""; composeInput = ""; composeName = "";
          composeCursor = 0;
        }
        enterInbox();
        toast(ok ? "sent" : "SEND FAILED -- M resumes", INK);
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
      else if (k == Key::Esc) { enterInbox(); toast("draft saved -- M resumes"); }
      else if (Key::isPrintable(k) && len < MAX_COMPOSE_CHARS) {
        composeBody = composeBody.substring(0, composeCursor) + String((char)k) +
                      composeBody.substring(composeCursor);
        composeCursor++; drawCompose();
      }
      break;
    }

    case Screen::Dial:
      if (k == Key::Enter && dialInput.length() >= 3) {
        const String number = normalizeNumber(dialInput);
        dialInput = "";
        dialNumber(number);
        if (callState == CallState::None) enterInbox();  // dial failed
      }
      else if (k == Key::Backspace && dialInput.length() > 0) {
        dialInput.remove(dialInput.length() - 1);
        if (dialInput.length() == 0) enterInbox();
        else drawDial();
      }
      else if (k == Key::Esc) { dialInput = ""; enterInbox(); }
      else if (((k >= '0' && k <= '9') || k == '+') && dialInput.length() < 16) {
        dialInput += (char)k;
        drawDial();
      }
      break;

    case Screen::Call:
      if (callState == CallState::Ringing) {
        if (k == Key::Enter) {
          stopRingTone();
          modem.setAudioRoute(3);
          if (modem.answer()) {
            callState = CallState::Active;
            callStart = millis();
            modem.setSpeakerVolume(speakerVolume);
            modem.setMicGain(micGain);
            drawCall();
          }
        } else if (k == Key::Esc || k == 'h' || k == 'H') {
          stopRingTone();
          modem.hangUp();
          endCall("call rejected");
        }
      } else {
        if (k == Key::Up && speakerVolume < 5) {
          speakerVolume++; modem.setSpeakerVolume(speakerVolume); drawCall();
        } else if (k == Key::Down && speakerVolume > 0) {
          speakerVolume--; modem.setSpeakerVolume(speakerVolume); drawCall();
        } else if (k == Key::Right && micGain < 8) {
          micGain++; modem.setMicGain(micGain); drawCall();
        } else if (k == Key::Left && micGain > 0) {
          micGain--; modem.setMicGain(micGain); drawCall();
        } else if (k == Key::Esc || k == 'h' || k == 'H') {
          modem.hangUp();
          endCall("call ended");
        }
      }
      break;

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
      Serial.println();  // finish the echoed line
      handleSerialLine(pending);
      pending = "";
    } else if ((c == 0x08 || c == 0x7F) && pending.length() > 0) {
      Serial.print("\b \b");  // rub out the echoed character
      pending.remove(pending.length() - 1);
    } else if (pending.length() < 200) {
      Serial.print(c);  // local echo, so typing is visible in the monitor
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

  static uint32_t lastCallTick = 0;
  if (callState == CallState::Active && screen == Screen::Call &&
      millis() - lastCallTick >= 10000) {
    lastCallTick = millis();
    drawCall();  // updates the MM:SS readout
  }

  const uint8_t k = readKey();
  if (k != 0) handleKey(k);

  modem.poll();
  pollSerial();
  delay(10);
}
