#include "Screen.h"

#include <SPI.h>
#include <stdarg.h>

#include "Log.h"
#include "Timestamp.h"

namespace Screen {
namespace {

// The display has its own SPI peripheral. MISO is unused but the constructor
// requires a pin.
SPIClass tftSPI(NRF_SPIM2, Pins::TFT_MISO, Pins::TFT_SCK, Pins::TFT_MOSI);
Adafruit_ST7789 tft(&tftSPI, Pins::TFT_CS, Pins::TFT_DC, Pins::TFT_RST);

String statusText = "Starting";
uint16_t statusColor = ST77XX_CYAN;

// Word-wraps `text` into `maxWidth`, and draws it when `draw` is set.
//
// Measuring and drawing share this one function: the line count it returns is
// exactly the number of lines it would paint. Two separate implementations is
// what used to leave a long final message clipped, because the estimate ignored
// word breaks and so under-counted the lines actually drawn.
//
// Lines are clipped to [clipTop, clipBottom) so a partially scrolled message
// cannot bleed into a neighbouring pane.
int wrapText(const String &text, int x, int y, int maxWidth, uint16_t color,
             int clipTop, int clipBottom, bool draw) {
  const int len = text.length();
  if (len == 0) return 1;

  const int maxChars = max(1, maxWidth / CHAR_W);
  if (draw) tft.setTextColor(color);

  int pos = 0;
  int lines = 0;

  while (pos < len && lines < MAX_WRAP_LINES) {
    int take = min(maxChars, len - pos);

    // Prefer to break at the last space that fits, unless the line runs to the
    // end of the text or is a single unbroken word.
    if (pos + take < len) {
      for (int i = pos + take; i > pos; i--) {
        if (text[i] == ' ') { take = i - pos; break; }
      }
    }

    if (draw && y + LINE_H > clipTop && y < clipBottom) {
      tft.setCursor(x, y);
      tft.print(text.substring(pos, pos + take));
    }

    pos += take;
    while (pos < len && text[pos] == ' ') pos++;  // swallow the break space
    y += LINE_H;
    lines++;
  }
  return lines;
}

int messageWidth(bool outgoing) {
  const int left = outgoing ? MSG_OUT_X : MSG_IN_X;
  return SCREEN_W - left - MSG_TIME_W - 4;
}

int messageHeight(const Message &message) {
  const int lines = wrapText(message.body, 0, 0, messageWidth(message.outgoing),
                             0, 0, 0, /*draw=*/false);
  return lines * LINE_H + MSG_GAP;
}

// Prints `text` right-aligned to `right`.
void printRight(const String &text, int right, int y, uint16_t color) {
  tft.setTextColor(color);
  tft.setCursor(right - (int)text.length() * CHAR_W, y);
  tft.print(text);
}

}  // namespace

void begin() {
  tftSPI.begin();
  tft.init(SCREEN_W, SCREEN_H);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setTextWrap(false);  // wrapping is ours to decide
}

void clear() { tft.fillScreen(ST77XX_BLACK); }

void splash(const char *version) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(10, 100);
  tft.print("DIY Phone");
  tft.setCursor(10, 120);
  tft.print(version);
  tft.setTextSize(1);
}

// -------------------------------------------------------------------- status

void status(uint16_t color, const char *fmt, ...) {
  char buf[64];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  statusText = buf;
  statusColor = color;
  LOGI("status", "%s", buf);
  refreshStatus();
}

void refreshStatus() {
  tft.fillRect(0, STATUS_Y, SCREEN_W, STATUS_H, ST77XX_BLACK);
  tft.setTextSize(1);

  tft.setTextColor(statusColor);
  tft.setCursor(2, STATUS_Y + 2);
  tft.print(statusText.substring(0, 24));

  // Free RAM, right-aligned. Green while there is comfortable headroom.
  const uint32_t freeKb = freeRam() / 1024;
  const uint16_t color = freeKb > 64 ? ST77XX_GREEN
                       : freeKb > 32 ? ST77XX_YELLOW
                                     : ST77XX_RED;
  char buf[12];
  snprintf(buf, sizeof(buf), "%luK", (unsigned long)freeKb);
  printRight(buf, SCREEN_W - 2, STATUS_Y + 2, color);

  tft.drawFastHLine(0, STATUS_Y + STATUS_H - 1, SCREEN_W, ST77XX_WHITE);
}

// ------------------------------------------------------------------ contacts

int contactRowsVisible() { return (CONTACTS_H - PANE_HEADER_H) / LINE_H; }

void drawContacts(const ThreadList &threads, int selected, int scrollRow,
                  const String &search) {
  tft.fillRect(0, CONTACTS_Y, SCREEN_W, CONTACTS_H, ST77XX_BLACK);
  tft.setTextSize(1);

  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(ROW_INSET, CONTACTS_Y + 5);
  tft.print(search.length() > 0 ? "SEARCH: " + search : String("CONTACTS"));

  char count[8];
  snprintf(count, sizeof(count), "(%d)", threads.size());
  printRight(count, SCREEN_W - ROW_INSET, CONTACTS_Y + 5, ST77XX_CYAN);

  const int rows = contactRowsVisible();
  for (int row = 0; row < rows; row++) {
    const int index = scrollRow + row;
    if (index >= threads.size()) break;

    const Thread &thread = threads[index];
    const int y = CONTACTS_Y + FIRST_ROW_Y + row * LINE_H;

    if (index == selected) {
      tft.fillRect(2, y - 1, SCREEN_W - 4, LINE_H, ST77XX_BLUE);
    }
    tft.setTextColor(ST77XX_WHITE);

    // "Name        most recent message..."
    String line = thread.name.substring(0, 10);
    while (line.length() < 12) line += ' ';
    line += thread.preview.length() > 0 ? thread.preview : String("--");

    tft.setCursor(ROW_INSET, y);
    tft.print(line);
  }
}

// -------------------------------------------------------------- conversation

int conversationHeight(const Message *messages, int count) {
  int total = 0;
  for (int i = 0; i < count; i++) total += messageHeight(messages[i]);
  return total;
}

void drawConversation(const String &title, const Message *messages, int count,
                      int scrollPx) {
  tft.fillRect(0, CONV_Y, SCREEN_W, CONV_H, ST77XX_BLACK);
  tft.setTextSize(1);

  // Messages are laid out in a virtual column and shifted up by scrollPx; only
  // what overlaps the viewport is drawn.
  int virtualY = 0;
  for (int i = 0; i < count; i++) {
    const Message &message = messages[i];
    const int height = messageHeight(message);
    const int y = CONV_VIEW_Y + virtualY - scrollPx;

    if (y < CONV_Y + CONV_H && y + height > CONV_VIEW_Y) {
      const int x = message.outgoing ? MSG_OUT_X : MSG_IN_X;
      const uint16_t color = message.outgoing ? ST77XX_GREEN : ST77XX_WHITE;
      wrapText(message.body, x, y, messageWidth(message.outgoing), color,
               CONV_VIEW_Y, CONV_Y + CONV_H, /*draw=*/true);

      // Timestamp rides the first line, so hide it when that line is scrolled
      // out of view.
      if (y >= CONV_VIEW_Y && y < CONV_Y + CONV_H) {
        printRight(Timestamp::hhmm(message.stamp), SCREEN_W - 2, y, ST77XX_CYAN);
      }
    }
    virtualY += height;
  }

  // Header last, so a scrolled message cannot overwrite it.
  tft.fillRect(0, CONV_Y, SCREEN_W, PANE_HEADER_H, ST77XX_BLACK);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(ROW_INSET, CONV_Y + 5);
  tft.print(title.length() > 0 ? title.substring(0, 25)
                               : String("Select a conversation"));
}

void drawCompose(const String &buffer, bool active) {
  tft.fillRect(0, COMPOSE_Y, SCREEN_W, COMPOSE_H, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(ROW_INSET, COMPOSE_Y + 5);

  // Show the tail of a long message so the insertion point stays on screen.
  const int room = (SCREEN_W - 2 * ROW_INSET) / CHAR_W - 3;  // "> " and cursor
  const String shown = (int)buffer.length() > room
                           ? buffer.substring(buffer.length() - room)
                           : buffer;

  tft.print("> ");
  tft.print(shown);
  if (active) tft.print('_');
}

void drawBorders(bool contactsActive) {
  const uint16_t active = ST77XX_RED;
  const uint16_t inactive = ST77XX_BLACK;
  tft.drawRect(0, CONTACTS_Y, SCREEN_W, CONTACTS_H,
               contactsActive ? active : inactive);
  tft.drawRect(0, CONV_Y, SCREEN_W, CONV_H + COMPOSE_H,
               contactsActive ? inactive : active);
}

}  // namespace Screen
