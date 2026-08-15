// MessageStore.h -- SMS messages as sms_*.txt files on the SD card.
//
// Every message file is a set of "Key: value" lines. Four generations of this
// project wrote four layouts, so the reader is key-driven rather than
// position-driven and accepts all of them:
//
//   From/Time/Status/Content                     (incoming, v26)
//   From/To/Time/Status/Content                  (outgoing, v26)
//   From/To/Time/LocalTime/Status/Content        (dual timestamp, v27)
//   From|To/Time/Dir/Content                     (v41)
//
// Unrecognized keys are ignored and any line without a key continues the
// previous Content, so a multi-line SMS body survives a round trip.
#pragma once

#include <Arduino.h>
#include <SdFat.h>

#include "Storage.h"
#include "Timestamp.h"

struct Message {
  String peer;   // the other party, whichever direction this went
  String body;
  Timestamp::Stamp stamp;
  bool outgoing = false;
};

namespace MessageStore {

// True when a directory entry is one of our message files.
bool isMessageFile(const char *filename);

// Fills `out` from an open message file. False if the file has no usable body.
bool parseFile(FsFile &file, Message &out);

// Walks every message on the card, calling visit(const Message&) for each.
// This is the only place that iterates the card, so scanning stays consistent
// between building the contact list and opening a single thread.
template <typename Visitor>
void forEach(Visitor &&visit) {
  FsFile root = Storage::card().open("/");
  if (!root) return;

  FsFile file;
  while (file.openNext(&root, O_RDONLY)) {
    char filename[64];
    file.getName(filename, sizeof(filename));
    if (isMessageFile(filename)) {
      Message message;
      if (parseFile(file, message)) visit(message);
    }
    file.close();
  }
  root.close();
}

// Number of message files on the card.
int countFiles();

// Writes a received message. `atTime` is the modem's raw timestamp string.
// Named after that timestamp, so re-reading the same SIM slot cannot duplicate
// it; returns false if the file already exists.
bool saveIncoming(const String &from, const String &atTime, const String &body);

// Writes a message we sent.
bool saveOutgoing(const String &to, const String &atTime, const String &body);

}  // namespace MessageStore
