#include "Timestamp.h"

#include "Log.h"

namespace Timestamp {
namespace {

constexpr int32_t DAYS_1970_TO_2000 = 10957;

// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm).
int32_t daysFromCivil(int32_t y, uint8_t m, uint8_t d) {
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = (uint32_t)(y - era * 400);
  const uint32_t doy = (153u * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

// Reads the "±zz" or "±hh:mm" tail and returns the offset in minutes.
int16_t parseZone(const String &zone) {
  if (zone.length() < 2) return 0;
  if (zone.indexOf(':') >= 0) {
    const int sign = (zone[0] == '-') ? -1 : 1;
    const int colon = zone.indexOf(':');
    const int hours = zone.substring(1, colon).toInt();
    const int mins = zone.substring(colon + 1).toInt();
    return (int16_t)(sign * (hours * 60 + mins));
  }
  return (int16_t)(zone.toInt() * 15);  // quarter-hours
}

}  // namespace

bool parse(const String &text, Stamp &out) {
  const int comma = text.indexOf(',');
  if (comma < 0) return false;

  const String date = text.substring(0, comma);
  String time = text.substring(comma + 1);

  // Split the timezone off the time. Search past position 0 so a leading sign
  // can never be mistaken for the separator.
  int zoneAt = -1;
  for (unsigned i = 1; i < time.length(); i++) {
    if (time[i] == '+' || time[i] == '-') { zoneAt = i; break; }
  }
  int16_t tzMin = 0;
  if (zoneAt > 0) {
    tzMin = parseZone(time.substring(zoneAt));
    time = time.substring(0, zoneAt);
  }

  const int slash1 = date.indexOf('/');
  const int slash2 = date.lastIndexOf('/');
  const int colon1 = time.indexOf(':');
  const int colon2 = time.lastIndexOf(':');
  if (slash1 < 0 || slash2 <= slash1 || colon1 < 0 || colon2 <= colon1) {
    LOGD("time", "unparseable: %s", text.c_str());
    return false;
  }

  int year = date.substring(0, slash1).toInt();
  const int month = date.substring(slash1 + 1, slash2).toInt();
  const int day = date.substring(slash2 + 1).toInt();
  const int hour = time.substring(0, colon1).toInt();
  const int minute = time.substring(colon1 + 1, colon2).toInt();
  const int second = time.substring(colon2 + 1).toInt();

  if (year < 100) year += (year < 70) ? 2000 : 1900;

  if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 ||
      minute > 59 || second > 59 || year < 2000) {
    LOGD("time", "out of range: %s", text.c_str());
    return false;
  }

  const int32_t days = daysFromCivil(year, (uint8_t)month, (uint8_t)day) - DAYS_1970_TO_2000;
  if (days < 0) return false;

  // The parsed wall time is local; shift back to UTC so stamps from different
  // timezones sort against each other correctly.
  const int32_t local = days * 86400L + hour * 3600L + minute * 60L + second;
  const int32_t utc = local - (int32_t)tzMin * 60;
  if (utc <= 0) return false;

  out.epoch = (uint32_t)utc;
  out.tzMin = tzMin;
  return true;
}

String hhmm(const Stamp &stamp) {
  if (!stamp.valid()) return String();

  const uint32_t local = stamp.epoch + (int32_t)stamp.tzMin * 60;
  const uint32_t secOfDay = local % 86400u;

  char buf[6];
  snprintf(buf, sizeof(buf), "%02u:%02u",
           (unsigned)(secOfDay / 3600), (unsigned)((secOfDay % 3600) / 60));
  return String(buf);
}

}  // namespace Timestamp
