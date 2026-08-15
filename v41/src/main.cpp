/******************************************************************************
  DIY Phone v41 -- ProMicro nRF52840

  A restructuring of v40. Same phone, same keys, same files on the SD card; the
  logic is split into modules with one implementation of each idea:

    Config.h        pins, layout, capacities
    Log            leveled tracing
    PhoneNumber    number normalization and comparison
    Timestamp      modem timestamps as an instant + UTC offset
    Storage        the SD card
    Contacts       the address book
    MessageStore   sms_*.txt files; one reader for all four historic layouts
    Threads        contact list and conversation loading
    Modem          SIM7600, sole owner of the modem UART
    Screen         every pixel
    main.cpp       state and key handling (this file)

  See README.md for what changed and why.

  Keys
    TAB           switch pane
    UP/DOWN       in a call: speaker volume. Otherwise: move the contact
                  selection, or scroll the conversation
    LEFT/RIGHT    microphone gain
    ENTER         answer a ringing call; else open the selected thread, or send
    ESC           hang up; else clear the contact search
    C             call the selected contact          (contacts pane)
    H             hang up                            (during a call)
    A-Z           search contacts                    (contacts pane)
                  type the message                   (conversation pane)
    1-8           diagnostics                        (contacts pane)
    9             toggle debug logging               (contacts pane)
 ******************************************************************************/

#include <Arduino.h>
#include <Wire.h>

#include "Config.h"
#include "Contacts.h"
#include "Log.h"
#include "MessageStore.h"
#include "Modem.h"
#include "PhoneNumber.h"
#include "Screen.h"
#include "Storage.h"
#include "Threads.h"
#include "Timestamp.h"

namespace {

constexpr char VERSION[] = "v41";

// ---------------------------------------------------------------------- state

enum class Pane : uint8_t { Contacts, Conversation };

enum class CallState : uint8_t {
  Idle,
  Ringing,  // RING seen, not yet answered
  Active,   // answered, or a call we placed
};

Modem modem(&Serial1);
Contacts contacts;
ThreadList threads;

Message conversation[MAX_THREAD_MESSAGES];
int conversationCount = 0;

Pane pane = Pane::Contacts;

// Contact list. `selected` and `scrollRow` index the *filtered* list, so
// searching and arrow navigation compose instead of fighting each other.
int selected = 0;
int scrollRow = 0;
String search;
uint32_t lastSearchKey = 0;

// Open conversation.
String peerNumber;
String peerName;
String compose;
int scrollPx = 0;
bool followingLatest = true;  // cleared once the user scrolls up

CallState callState = CallState::Idle;
uint8_t speakerVolume = SPEAKER_VOLUME_DEFAULT;
uint8_t micGain = MIC_GAIN_DEFAULT;

uint32_t lastStatusRefresh = 0;

// -------------------------------------------------------------- contact list

void redrawContacts() {
  Screen::drawContacts(threads, selected, scrollRow, search);
}

// Keeps `selected` inside the list and `scrollRow` such that it is visible.
void clampSelection() {
  const int size = threads.size();
  if (size == 0) {
    selected = scrollRow = 0;
    return;
  }
  selected = constrain(selected, 0, size - 1);

  const int rows = Screen::contactRowsVisible();
  if (selected < scrollRow) scrollRow = selected;
  if (selected >= scrollRow + rows) scrollRow = selected - rows + 1;
  scrollRow = constrain(scrollRow, 0, max(0, size - rows));
}

// Re-filters the list while keeping the same conversation highlighted, so
// clearing a search does not jump the selection back to the top.
void reselect(const String &keepNumber) {
  selected = 0;
  if (keepNumber.length() > 0) {
    for (int i = 0; i < threads.size(); i++) {
      if (PhoneNumber::same(threads[i].number, keepNumber)) {
        selected = i;
        break;
      }
    }
  }
  clampSelection();
}

void refreshThreads() {
  const String keep = threads.size() > 0 ? threads[selected].number : String();
  threads.rebuild(contacts);
  reselect(keep);
  redrawContacts();
}

// ------------------------------------------------------------- conversation

// Scroll offset that puts the newest message flush with the bottom of the
// viewport. Uses the same measurement the renderer uses, so the last message is
// never clipped however long it is.
int bottomScroll() {
  return max(0, Screen::conversationHeight(conversation, conversationCount) - CONV_VIEW_H);
}

void redrawConversation() {
  Screen::drawConversation(peerName, conversation, conversationCount, scrollPx);
  Screen::drawCompose(compose, pane == Pane::Conversation && peerNumber.length() > 0);
}

void openConversation(const String &number, const String &name) {
  peerNumber = number;
  peerName = name;
  conversationCount = loadConversation(number, conversation, MAX_THREAD_MESSAGES);
  followingLatest = true;
  scrollPx = bottomScroll();
  redrawConversation();
}

// Re-reads the open conversation after it changed on disk. Someone reading back
// through history keeps their place; someone sitting at the bottom follows the
// new message down.
void reloadConversation() {
  if (peerNumber.length() == 0) return;

  conversationCount = loadConversation(peerNumber, conversation, MAX_THREAD_MESSAGES);
  if (followingLatest) scrollPx = bottomScroll();
  else scrollPx = min(scrollPx, bottomScroll());
  redrawConversation();
}

void scrollConversation(int direction) {
  const int bottom = bottomScroll();
  scrollPx = constrain(scrollPx + direction * SCROLL_STEP, 0, bottom);
  // Back at the bottom means "keep following new messages".
  followingLatest = (scrollPx >= bottom);
  redrawConversation();
}

// ---------------------------------------------------------------------- calls

void endCall(const char *reason) {
  callState = CallState::Idle;
  Screen::status(ST77XX_CYAN, "%s", reason);
}

void placeCall(const String &number, const String &name) {
  Screen::status(ST77XX_YELLOW, "Calling %s", name.substring(0, 14).c_str());
  if (modem.call(number)) {
    callState = CallState::Active;
    Screen::status(ST77XX_GREEN, "In call");
  } else {
    Screen::status(ST77XX_RED, "Call failed");
  }
}

void adjustSpeaker(int delta) {
  const int level = constrain((int)speakerVolume + delta, 0, SPEAKER_VOLUME_MAX);
  speakerVolume = (uint8_t)level;
  modem.setSpeakerVolume(speakerVolume);
  Screen::status(ST77XX_CYAN, "Speaker %d/%d", speakerVolume, SPEAKER_VOLUME_MAX);
}

void adjustMic(int delta) {
  const int level = constrain((int)micGain + delta, 0, MIC_GAIN_MAX);
  micGain = (uint8_t)level;
  modem.setMicGain(micGain);
  Screen::status(ST77XX_CYAN, "Mic %d/%d", micGain, MIC_GAIN_MAX);
}

// ------------------------------------------------------- incoming messages

// Files an unrecognized sender under "Unknown <last 4 digits>" so their thread
// has a name and they can be called back.
void learnSender(const String &number) {
  if (number.length() == 0) return;
  if (contacts.nameFor(number).length() > 0) return;

  const String tail =
      number.length() >= 4 ? number.substring(number.length() - 4) : number;
  contacts.add(number, "Unknown " + tail);
}

// Stores a newly arrived SMS and refreshes whatever is on screen.
void receiveSms(uint8_t slot) {
  Screen::status(ST77XX_YELLOW, "New message");

  Modem::Sms sms;
  if (!modem.readAndDeleteSms(slot, sms)) {
    Screen::status(ST77XX_RED, "SMS read failed");
    return;
  }

  learnSender(sms.sender);

  if (!MessageStore::saveIncoming(sms.sender, sms.atTime, sms.body)) {
    Screen::status(ST77XX_RED, "SMS save failed");
    return;
  }

  refreshThreads();
  if (peerNumber.length() > 0 && PhoneNumber::same(peerNumber, sms.sender)) {
    peerName = contacts.displayName(peerNumber);  // may have just been learned
    reloadConversation();
  }
  Screen::status(ST77XX_GREEN, "Message from %s",
                 contacts.displayName(sms.sender).substring(0, 12).c_str());
}

// Unsolicited modem output, dispatched from Modem::poll() in the main loop.
void onModemNotification(const String &line) {
  if (line == "RING") {
    callState = CallState::Ringing;
    Screen::status(ST77XX_YELLOW, "Incoming call");

  } else if (line.startsWith("+CLIP:")) {
    const int open = line.indexOf('"');
    const int close = line.indexOf('"', open + 1);
    if (open >= 0 && close > open) {
      const String caller = line.substring(open + 1, close);
      Screen::status(ST77XX_YELLOW, "Call: %s",
                     contacts.displayName(caller).substring(0, 14).c_str());
    }

  } else if (line == "NO CARRIER" || line.startsWith("+CEND:") ||
             line == "BUSY" || line == "NO ANSWER") {
    endCall("Call ended");

  } else if (line.startsWith("+CMTI:")) {
    // +CMTI: "SM",25 -- a message landed in SIM slot 25.
    const int comma = line.lastIndexOf(',');
    if (comma >= 0) receiveSms((uint8_t)line.substring(comma + 1).toInt());
  }
}

// ---------------------------------------------------------------- diagnostics

void runDiagnostic(uint8_t number) {
  switch (number) {
    case 1: {
      const int quality = modem.signalQuality();
      Screen::status(ST77XX_CYAN, "Signal %d/31", quality);
      break;
    }
    case 2:
      if (modem.isResponding()) Screen::status(ST77XX_GREEN, "Modem OK");
      else Screen::status(ST77XX_RED, "Modem silent");
      break;

    case 3: {
      // Drain any messages sitting on the SIM into files on the card.
      int used = 0, total = 0;
      if (!modem.smsSlots(used, total)) {
        Screen::status(ST77XX_RED, "SIM query failed");
        break;
      }
      Screen::status(ST77XX_CYAN, "Reading %d SMS", used);

      int stored = 0;
      for (int slot = 1; slot <= total; slot++) {
        Modem::Sms sms;
        if (!modem.readAndDeleteSms((uint8_t)slot, sms)) continue;
        learnSender(sms.sender);
        if (MessageStore::saveIncoming(sms.sender, sms.atTime, sms.body)) stored++;
      }

      if (stored > 0) {
        refreshThreads();
        reloadConversation();
      }
      Screen::status(stored > 0 ? ST77XX_GREEN : ST77XX_YELLOW, "Stored %d SMS", stored);
      break;
    }

    case 4: {
      char name[32];
      snprintf(name, sizeof(name), "test_%lu.txt", millis());
      FsFile file = Storage::card().open(name, O_RDWR | O_CREAT | O_TRUNC);
      if (!file) {
        Screen::status(ST77XX_RED, "SD write failed");
        break;
      }
      file.println("v41 storage check");
      file.close();
      Screen::status(ST77XX_GREEN, "SD OK (%d msgs)", MessageStore::countFiles());
      break;
    }

    case 5:
      refreshThreads();
      reloadConversation();
      Screen::status(ST77XX_GREEN, "%d conversations", threads.total());
      break;

    case 6:
      if (modem.networkStatus()) Screen::status(ST77XX_GREEN, "Network OK");
      else Screen::status(ST77XX_RED, "Network failed");
      break;

    case 7: {
      // Delete the SIM's messages one slot at a time. Files on the card are
      // left alone.
      int used = 0, total = 0;
      if (!modem.smsSlots(used, total)) total = 30;
      int deleted = 0;
      for (int slot = 1; slot <= total; slot++) {
        if (modem.deleteSms((uint8_t)slot)) deleted++;
      }
      Screen::status(deleted > 0 ? ST77XX_GREEN : ST77XX_RED, "Deleted %d", deleted);
      break;
    }

    case 8:
      if (modem.deleteAllSms()) Screen::status(ST77XX_GREEN, "SIM cleared");
      else Screen::status(ST77XX_RED, "Clear failed");
      break;

    case 9:
      logLevel = (logLevel == LogLevel::Debug) ? LogLevel::Info : LogLevel::Debug;
      Screen::status(ST77XX_CYAN, "Debug %s",
                     logLevel == LogLevel::Debug ? "on" : "off");
      break;

    default:
      break;
  }
}

// -------------------------------------------------------------- key handling

void switchPane() {
  pane = (pane == Pane::Contacts) ? Pane::Conversation : Pane::Contacts;
  Screen::drawBorders(pane == Pane::Contacts);
  Screen::drawCompose(compose, pane == Pane::Conversation && peerNumber.length() > 0);
}

void sendComposed() {
  if (compose.length() == 0 || peerNumber.length() == 0) return;

  Screen::status(ST77XX_YELLOW, "Sending");
  if (!modem.sendSms(peerNumber, compose)) {
    Screen::status(ST77XX_RED, "Send failed");
    return;
  }

  // Timestamp the sent message with network time so it sorts against received
  // ones; without the network we would have no clock at all.
  const String atTime = modem.networkTime();
  if (atTime.length() > 0) {
    MessageStore::saveOutgoing(peerNumber, atTime, compose);
  } else {
    LOGE("sms", "no network time; sent message not stored");
  }

  compose = "";
  followingLatest = true;  // always scroll down to show what we just sent
  reloadConversation();
  refreshThreads();
  Screen::status(ST77XX_GREEN, "Sent");
}

void updateSearch(const String &next) {
  // Narrowing a search jumps to the first match; widening or clearing it keeps
  // whatever was highlighted.
  const bool narrowing = next.length() > search.length();
  const String keep =
      (!narrowing && threads.size() > 0) ? threads[selected].number : String();

  search = next;
  lastSearchKey = millis();
  threads.filter(search);
  scrollRow = 0;
  reselect(keep);
  redrawContacts();
}

// Keys whose meaning does not depend on which pane is focused. Returns true
// when the key was consumed.
bool handleGlobalKey(uint8_t key) {
  switch (key) {
    case Key::Tab:
      switchPane();
      return true;

    case Key::Up:
    case Key::Down: {
      const int direction = (key == Key::Down) ? 1 : -1;
      if (callState == CallState::Active) {
        adjustSpeaker(-direction);  // up is louder
      } else if (pane == Pane::Contacts) {
        selected += direction;
        clampSelection();
        redrawContacts();
      } else {
        scrollConversation(direction);
      }
      return true;
    }

    case Key::Left:
      adjustMic(-1);
      return true;

    case Key::Right:
      adjustMic(1);
      return true;

    case Key::Enter:
      if (callState == CallState::Ringing) {
        if (modem.answer()) {
          callState = CallState::Active;
          Screen::status(ST77XX_GREEN, "Call connected");
        } else {
          callState = CallState::Idle;
          Screen::status(ST77XX_RED, "Answer failed");
        }
        return true;
      }
      return false;  // pane-specific

    case Key::Esc:
      if (callState != CallState::Idle) {
        modem.hangUp();
        endCall("Call ended");
        return true;
      }
      if (pane == Pane::Contacts && search.length() > 0) {
        updateSearch("");
        return true;
      }
      return true;

    case 'H':
    case 'h':
      // Only steals the key mid-call, so 'h' still types in a message.
      if (callState != CallState::Idle) {
        modem.hangUp();
        endCall("Call ended");
        return true;
      }
      return false;

    default:
      return false;
  }
}

void handleContactsKey(uint8_t key) {
  if (key == Key::Enter) {
    if (threads.size() == 0) return;
    const Thread &thread = threads[selected];
    openConversation(thread.number, thread.name);
    pane = Pane::Conversation;
    Screen::drawBorders(false);
    return;
  }

  if (key == 'C' || key == 'c') {
    if (threads.size() == 0) return;
    const Thread &thread = threads[selected];
    placeCall(thread.number, thread.name);
    return;
  }

  if (Key::isLetter(key)) {
    if (search.length() < MAX_SEARCH_CHARS) {
      updateSearch(search + (char)toupper(key));
    }
    return;
  }

  if (key == Key::Backspace) {
    if (search.length() > 0) updateSearch(search.substring(0, search.length() - 1));
    return;
  }

  if (key >= '1' && key <= '9') runDiagnostic(key - '0');
}

void handleConversationKey(uint8_t key) {
  if (key == Key::Enter) {
    sendComposed();
    return;
  }

  if (key == Key::Backspace) {
    if (compose.length() > 0) {
      compose = compose.substring(0, compose.length() - 1);
      Screen::drawCompose(compose, true);
    }
    return;
  }

  if (Key::isPrintable(key) && (int)compose.length() < MAX_COMPOSE_CHARS) {
    compose += (char)key;
    Screen::drawCompose(compose, true);
  }
}

void handleKeyboard() {
  Wire.requestFrom((uint8_t)KEYBOARD_I2C_ADDR, (uint8_t)1);
  if (!Wire.available()) return;

  const uint8_t key = Wire.read();
  if (key == 0) return;

  LOGD("key", "0x%02X", key);

  if (handleGlobalKey(key)) return;
  if (pane == Pane::Contacts) handleContactsKey(key);
  else handleConversationKey(key);
}

// Contact search is transient: it clears itself after a pause so the full list
// comes back without the user having to.
void expireSearch() {
  if (search.length() == 0) return;
  if (millis() - lastSearchKey < SEARCH_TIMEOUT_MS) return;
  updateSearch("");
}

}  // namespace

// ---------------------------------------------------------------------- setup

void setup() {
  Serial.begin(CONSOLE_BAUD);
  const uint32_t start = millis();
  while (!Serial && millis() - start < 3000) delay(10);
  LOGI("boot", "DIY Phone %s", VERSION);

  Screen::begin();
  Screen::splash(VERSION);

  Wire.begin();
  Wire.beginTransmission(KEYBOARD_I2C_ADDR);
  if (Wire.endTransmission() == 0) LOGI("boot", "keyboard present");
  else LOGE("boot", "keyboard not responding at 0x%02X", KEYBOARD_I2C_ADDR);

  if (Storage::begin()) {
    contacts.load();
  } else {
    Screen::status(ST77XX_RED, "No SD card");
    delay(1000);
  }

  if (modem.begin(MODEM_BAUD, Pins::MODEM_RX, Pins::MODEM_TX)) {
    modem.enableCallerId();
    modem.textMode();
    modem.setSpeakerVolume(speakerVolume);
    modem.setMicGain(micGain);
    LOGI("boot", "signal %d/31", modem.signalQuality());
  } else {
    Screen::status(ST77XX_RED, "Modem failed");
    delay(1000);
  }
  modem.onNotification(onModemNotification);

  threads.rebuild(contacts);
  clampSelection();

  Screen::clear();
  Screen::drawBorders(pane == Pane::Contacts);
  redrawContacts();
  redrawConversation();
  Screen::status(ST77XX_GREEN, "Ready %s", VERSION);
}

void loop() {
  modem.poll();
  handleKeyboard();
  expireSearch();

  if (millis() - lastStatusRefresh >= STATUS_REFRESH_MS) {
    lastStatusRefresh = millis();
    Screen::refreshStatus();
  }

  delay(10);
}
