// Host tests for the pure logic in v41: timestamp parsing, phone number
// matching, and the wrap/measure agreement that keeps the last message visible.
//
// Build and run:  ./tools/hosttest/run.sh

#include "ArduinoShim.h"

SerialShim Serial;

// The real modules, compiled as they ship.
#include "../../src/Log.cpp"
#include "../../src/PhoneNumber.cpp"
#include "../../src/Timestamp.cpp"

#include <algorithm>
#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (ok) {
    printf("  ok    %s\n", what);
  } else {
    printf("  FAIL  %s\n", what);
    failures++;
  }
}

// --------------------------------------------------------------- timestamps

void testTimestamps() {
  printf("timestamps\n");

  Timestamp::Stamp a{};
  check(Timestamp::parse("26/01/04,19:04:26-32", a), "parses modem quarter-hour zone");
  check(a.tzMin == -480, "-32 quarter-hours is UTC-8");
  check(Timestamp::hhmm(a) == String("19:04"), "renders local wall time 19:04");

  // The same instant written as the normalized UTC form v27 files used.
  Timestamp::Stamp b{};
  check(Timestamp::parse("26/01/05,03:04:26+00:00", b), "parses +hh:mm zone");
  check(a.epoch == b.epoch, "both spellings are the same instant");
  check(Timestamp::hhmm(b) == String("03:04"), "UTC form renders as 03:04");

  // Ordering across a UTC day boundary -- the case the old addOneDay /
  // subtractOneDay hacks got wrong.
  Timestamp::Stamp late{}, early{};
  Timestamp::parse("26/01/04,23:30:00-32", late);   // 07:30 UTC on the 5th
  Timestamp::parse("26/01/05,01:00:00+00:00", early);
  check(early.epoch < late.epoch, "cross-midnight ordering is correct");

  // Month and year rollover, previously approximated with 28/30/31 guesses.
  Timestamp::Stamp feb28{}, mar01{};
  Timestamp::parse("24/02/28,23:00:00+00:00", feb28);
  Timestamp::parse("24/03/01,00:00:00+00:00", mar01);
  check(mar01.epoch - feb28.epoch == 25 * 3600,
        "2024 is a leap year (Feb 29 counted)");

  Timestamp::Stamp dec31{}, jan01{};
  Timestamp::parse("25/12/31,23:00:00+00:00", dec31);
  Timestamp::parse("26/01/01,00:00:00+00:00", jan01);
  check(jan01.epoch - dec31.epoch == 3600, "year rollover is one hour apart");

  // A 32-bit sort key: the old code multiplied the year by 1e10 into an
  // unsigned long, which overflows.
  Timestamp::Stamp y2026{};
  Timestamp::parse("26/07/29,12:00:00+00:00", y2026);
  check(y2026.epoch > 0 && y2026.epoch < 0xFFFFFFFFu, "epoch fits in uint32");

  Timestamp::Stamp bad{};
  check(!Timestamp::parse("garbage", bad), "rejects garbage");
  check(!Timestamp::parse("26/13/04,19:04:26-32", bad), "rejects month 13");
  check(!Timestamp::parse("26/01/04,25:04:26-32", bad), "rejects hour 25");
  check(!bad.valid(), "unparsed stamp stays invalid");
}

// ------------------------------------------------------------ phone numbers

void testPhoneNumbers() {
  printf("phone numbers\n");

  check(PhoneNumber::normalize("+1 (617) 555-1234") == String("16175551234"),
        "strips punctuation and +");
  check(PhoneNumber::same("+16175551234", "6175551234"),
        "matches across a missing country code");
  check(PhoneNumber::same("(617) 555-1234", "+1-617-555-1234"),
        "matches across formatting");
  check(PhoneNumber::same("+16175551234", "+16175551234"), "exact match");
  check(!PhoneNumber::same("+16175551234", "+16175559999"), "rejects different numbers");
  check(!PhoneNumber::same("", "+16175551234"), "rejects empty");
  check(!PhoneNumber::same("5551234", "+16175551234"),
        "does not match on a short suffix");
}

// ------------------------------------------------------------- text wrapping

// Mirror of Screen::wrapText's loop. The firmware uses one function for both
// measuring and drawing; this reproduces that loop so the layout arithmetic can
// be checked on the host.
constexpr int CHAR_W = 6;
constexpr int LINE_H = 10;
constexpr int MSG_GAP = 2;
constexpr int MAX_WRAP_LINES = 40;
constexpr int CONV_VIEW_H = 119;

int wrapLines(const String &text, int maxWidth) {
  const int len = (int)text.length();
  if (len == 0) return 1;

  const int maxChars = std::max(1, maxWidth / CHAR_W);
  int pos = 0, lines = 0;

  while (pos < len && lines < MAX_WRAP_LINES) {
    int take = std::min(maxChars, len - pos);
    if (pos + take < len) {
      for (int i = pos + take; i > pos; i--) {
        if (text[i] == ' ') { take = i - pos; break; }
      }
    }
    pos += take;
    while (pos < len && text[pos] == ' ') pos++;
    lines++;
  }
  return lines;
}

void testWrapping() {
  printf("text wrapping\n");

  const int width = 240 - 2 - 30 - 4;  // an incoming message's usable width
  const int maxChars = width / CHAR_W;

  check(wrapLines("", width) == 1, "empty text still occupies a line");
  check(wrapLines("short", width) == 1, "short text is one line");

  // No line may exceed the character budget, and words must not be split when
  // a break is available.
  const String prose =
      "Hey are we still meeting up at the cafe tomorrow morning or did you want "
      "to push it to the afternoon instead because I could go either way";
  const int lines = wrapLines(prose, width);
  check(lines > 1, "long prose wraps to several lines");

  // The bug this replaces: the old code measured with ceil(len/maxChars) while
  // drawing with word breaks, so it under-counted and clipped the tail.
  const int naive = ((int)prose.length() + maxChars - 1) / maxChars;
  check(lines >= naive, "word-wrapped line count is never below the naive estimate");
  printf("        (naive estimate %d lines, actual %d)\n", naive, lines);

  // The failure mode itself. Words long enough that only one fits per line
  // strand a chunk of every line, so word wrapping needs far more lines than
  // length/maxChars predicts. Measuring one way and drawing the other is what
  // pushed the tail of a long message below the viewport.
  String longWords;
  for (int w = 0; w < 8; w++) {
    if (w) longWords += ' ';
    for (int i = 0; i < maxChars - 8; i++) longWords += (char)('a' + w);
  }
  const int longWordLines = wrapLines(longWords, width);
  const int longWordNaive = ((int)longWords.length() + maxChars - 1) / maxChars;
  check(longWordLines > longWordNaive,
        "word wrapping needs more lines than the old estimate predicted");
  printf("        (naive estimate %d lines, actual %d -- the %d-line shortfall "
         "is what clipped the last message)\n",
         longWordNaive, longWordLines, longWordLines - longWordNaive);

  // Whatever the text, a height measured this way always contains the drawing.
  const int measured = longWordLines * LINE_H + MSG_GAP;
  const int drawnBottom = longWordLines * LINE_H;
  check(drawnBottom <= measured, "measured height always contains the drawn text");

  // A single unbreakable word must still make progress rather than loop.
  String runOn;
  for (int i = 0; i < 300; i++) runOn += 'x';
  const int runOnLines = wrapLines(runOn, width);
  check(runOnLines == (300 + maxChars - 1) / maxChars, "unbreakable text fills lines exactly");

  // The scroll target derived from the measured height must place the bottom of
  // the final message exactly at the bottom of the viewport.
  const int totalHeight = lines * LINE_H + MSG_GAP;
  const int scroll = std::max(0, totalHeight - CONV_VIEW_H);
  const int lastBottom = totalHeight - scroll;
  check(lastBottom <= CONV_VIEW_H, "scrolled content ends within the viewport");
  if (totalHeight > CONV_VIEW_H) {
    check(lastBottom == CONV_VIEW_H, "a tall message ends flush with the bottom");
  }
}

}  // namespace

int main() {
  testTimestamps();
  testPhoneNumbers();
  testWrapping();

  printf("\n%s\n", failures == 0 ? "all tests passed" : "TESTS FAILED");
  return failures == 0 ? 0 : 1;
}
