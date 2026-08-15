// Contacts.h -- the address book, backed by addressbook.txt on the SD card.
#pragma once

#include <Arduino.h>

#include "Config.h"

class Contacts {
 public:
  struct Entry {
    String number;
    String name;
  };

  // Reads addressbook.txt, falling back to contacts.txt. Lines are
  // "name,number" in either order; the field that looks like a number wins.
  bool load();
  bool save() const;

  // Appends a contact and persists the book. Refuses duplicates.
  bool add(const String &number, const String &name);

  // Empty when the number is not in the book.
  String nameFor(const String &number) const;

  // The name if known, otherwise the number itself -- what the UI shows.
  String displayName(const String &number) const;

  int count() const { return count_; }
  const Entry &at(int i) const { return entries_[i]; }

 private:
  Entry entries_[MAX_CONTACTS];
  int count_ = 0;
};
