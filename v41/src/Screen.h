// Screen.h -- owns the display and every pixel drawn on it.
//
// The functions take what they draw as arguments rather than reaching for
// application state, so the UI has no opinion about how the phone is wired
// together.
#pragma once

#include <Adafruit_ST7789.h>

#include "Config.h"
#include "MessageStore.h"
#include "Threads.h"

namespace Screen {

void begin();
void splash(const char *version);
void clear();

// --- status bar
void status(uint16_t color, const char *fmt, ...);
void refreshStatus();  // repaints with a fresh RAM reading, same message

// --- panes
void drawContacts(const ThreadList &threads, int selected, int scrollRow,
                  const String &search);
void drawConversation(const String &title, const Message *messages, int count,
                      int scrollPx);
void drawCompose(const String &buffer, bool active);
void drawBorders(bool contactsActive);

// --- geometry the caller needs for scrolling
int contactRowsVisible();
// Total height in pixels of a rendered conversation. Measured with the same
// wrapping code that draws it, so a scroll target computed from this value
// always lands on a whole message.
int conversationHeight(const Message *messages, int count);

}  // namespace Screen
