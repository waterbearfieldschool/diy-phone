// Timestamp.h -- SMS timestamps as an absolute instant plus a UTC offset.
//
// The modem reports time as "YY/MM/DD,hh:mm:ss±zz", where zz is the timezone in
// quarter-hours ("-32" is UTC-8). Older message files on the SD card instead
// carry a normalized "...+00:00" form. Both collapse to the same Stamp, so
// nothing downstream has to know which format a file used.
#pragma once

#include <Arduino.h>

namespace Timestamp {

struct Stamp {
  uint32_t epoch = 0;  // seconds since 2000-01-01T00:00:00Z; 0 means "unknown"
  int16_t tzMin = 0;   // minutes east of UTC, for rendering local wall time

  bool valid() const { return epoch != 0; }
};

// Parses either modem format. Returns false and leaves `out` untouched on
// anything unrecognizable.
bool parse(const String &text, Stamp &out);

// Wall time as "HH:MM" in the stamp's own timezone. Empty when invalid.
String hhmm(const Stamp &stamp);

// The same instant rendered at a specific offset (minutes east of UTC) --
// use this to show every message in the phone's local time rather than the
// timezone the sender's carrier stamped it with.
String hhmmAt(const Stamp &stamp, int16_t tzMin);

}  // namespace Timestamp
