#include "Threads.h"

#include <utility>

#include "Log.h"
#include "PhoneNumber.h"

Thread *ThreadList::find(const String &number) {
  for (int i = 0; i < count_; i++) {
    if (PhoneNumber::same(number, entries_[i].number)) return &entries_[i];
  }
  return nullptr;
}

Thread *ThreadList::append() {
  if (count_ >= MAX_THREADS) return nullptr;
  return &entries_[count_++];
}

void ThreadList::rebuild(const Contacts &contacts) {
  count_ = 0;

  // Start from the address book so contacts with no history are still callable.
  for (int i = 0; i < contacts.count() && count_ < MAX_THREADS; i++) {
    Thread *thread = append();
    thread->number = contacts.at(i).number;
    thread->name = contacts.at(i).name;
    thread->preview = "";
    thread->latest = Timestamp::Stamp{};
    thread->count = 0;
  }

  MessageStore::forEach([&](const Message &message) {
    Thread *thread = find(message.peer);
    if (!thread) {
      thread = append();
      if (!thread) return;  // list full; remaining messages are dropped
      thread->number = message.peer;
      thread->name = contacts.displayName(message.peer);
      thread->count = 0;
    }

    thread->count++;
    if (message.stamp.epoch >= thread->latest.epoch) {
      thread->latest = message.stamp;
      thread->preview = message.body.substring(0, PREVIEW_CHARS);
    }
  });

  sortByRecency();
  filter(filter_);
  LOGI("threads", "%d conversations", count_);
}

void ThreadList::sortByRecency() {
  for (int i = 0; i < count_; i++) order_[i] = (uint8_t)i;

  // Insertion sort on the index array: newest first, and never more than
  // MAX_THREADS byte moves per element.
  for (int i = 1; i < count_; i++) {
    const uint8_t key = order_[i];
    int j = i - 1;
    while (j >= 0 && entries_[order_[j]].latest.epoch < entries_[key].latest.epoch) {
      order_[j + 1] = order_[j];
      j--;
    }
    order_[j + 1] = key;
  }
}

void ThreadList::filter(const String &prefix) {
  filter_ = prefix;
  String needle = prefix;
  needle.toLowerCase();

  viewCount_ = 0;
  for (int i = 0; i < count_; i++) {
    if (needle.length() > 0) {
      String name = entries_[order_[i]].name;
      name.toLowerCase();
      if (!name.startsWith(needle)) continue;
    }
    view_[viewCount_++] = order_[i];
  }
}

int loadConversation(const String &peer, Message *out, int capacity) {
  int count = 0;

  MessageStore::forEach([&](const Message &message) {
    if (!PhoneNumber::same(message.peer, peer)) return;

    if (count < capacity) {
      out[count++] = message;
      return;
    }

    // Full: evict the oldest, but only for something newer than it.
    int oldest = 0;
    for (int i = 1; i < capacity; i++) {
      if (out[i].stamp.epoch < out[oldest].stamp.epoch) oldest = i;
    }
    if (message.stamp.epoch > out[oldest].stamp.epoch) out[oldest] = message;
  });

  // Oldest first, so the newest message sits at the bottom of the pane.
  // Messages with an unparseable timestamp (epoch 0) drift to the top.
  for (int i = 1; i < count; i++) {
    for (int j = i; j > 0 && out[j - 1].stamp.epoch > out[j].stamp.epoch; j--) {
      std::swap(out[j - 1], out[j]);
    }
  }

  LOGI("threads", "%d messages with %s", count, peer.c_str());
  return count;
}
