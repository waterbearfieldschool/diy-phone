// Threads.h -- the contact list (one row per conversation) and the loader for
// the messages of a single conversation.
#pragma once

#include <Arduino.h>

#include "Config.h"
#include "Contacts.h"
#include "MessageStore.h"

struct Thread {
  String number;
  String name;     // contact name, or the number when unknown
  String preview;  // first characters of the most recent message
  Timestamp::Stamp latest;
  uint16_t count = 0;
};

// Every contact plus anyone who has messaged us, ordered by recent activity and
// filterable by a typed prefix.
//
// Sorting and filtering permute index arrays rather than the entries, so no
// String is ever copied to reorder the list.
class ThreadList {
 public:
  // Rebuilds from the address book and every message on the card.
  void rebuild(const Contacts &contacts);

  // Restricts the visible rows to names starting with `prefix` (empty shows
  // all). Case-insensitive.
  void filter(const String &prefix);

  // Visible rows, after filtering.
  int size() const { return viewCount_; }
  const Thread &operator[](int i) const { return entries_[view_[i]]; }
  int total() const { return count_; }

 private:
  Thread *find(const String &number);
  Thread *append();
  void sortByRecency();

  Thread entries_[MAX_THREADS];
  uint8_t order_[MAX_THREADS];  // entry indices, most recent first
  uint8_t view_[MAX_THREADS];   // subset of order_ passing the filter
  int count_ = 0;
  int viewCount_ = 0;
  String filter_;
};

// Loads up to MAX_THREAD_MESSAGES of the most recent messages exchanged with
// `peer`, oldest first. Returns how many were written to `out`.
int loadConversation(const String &peer, Message *out, int capacity);
