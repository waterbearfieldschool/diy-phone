#include "PhoneNumber.h"

namespace PhoneNumber {

String normalize(const String &number) {
  String out;
  out.reserve(number.length());
  for (unsigned i = 0; i < number.length(); i++) {
    char c = number[i];
    if (c >= '0' && c <= '9') out += c;
  }
  return out;
}

bool same(const String &a, const String &b) {
  const String x = normalize(a);
  const String y = normalize(b);
  if (x.length() == 0 || y.length() == 0) return false;
  if (x == y) return true;

  // One side carries a country code and the other does not.
  if (x.length() == 10 && y.length() > 10) return y.endsWith(x);
  if (y.length() == 10 && x.length() > 10) return x.endsWith(y);
  return false;
}

}  // namespace PhoneNumber
