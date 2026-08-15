// PhoneNumber.h -- the one place that knows how to compare two phone numbers.
#pragma once

#include <Arduino.h>

namespace PhoneNumber {

// Strips punctuation and the leading '+', leaving digits only.
String normalize(const String &number);

// True when both refer to the same subscriber. Tolerates a missing country
// code on either side by matching a 10-digit number against the tail of a
// longer one, which is how contacts and SMS headers routinely disagree.
bool same(const String &a, const String &b);

}  // namespace PhoneNumber
