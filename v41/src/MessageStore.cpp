#include "MessageStore.h"

#include "Log.h"
#include "PhoneNumber.h"

namespace MessageStore {
namespace {

constexpr char FILE_PREFIX[] = "sms_";

// Splits "Key: value". Returns false for a continuation line.
bool splitField(const String &line, String &key, String &value) {
  const int colon = line.indexOf(':');
  if (colon <= 0) return false;

  key = line.substring(0, colon);
  for (unsigned i = 0; i < key.length(); i++) {
    // Keys are single bare words; anything else is body text that happens to
    // contain a colon (a URL, a time of day).
    if (!isAlphaNumeric(key[i])) return false;
  }

  value = line.substring(colon + 1);
  value.trim();
  return true;
}

// "26/01/04,19:04:26-32" -> "260104_190426", used as the filename.
String fileIdFor(const String &atTime) {
  const int comma = atTime.indexOf(',');
  if (comma < 0) return String();

  String date = atTime.substring(0, comma);
  String time = atTime.substring(comma + 1);
  date.replace("/", "");

  for (unsigned i = 1; i < time.length(); i++) {
    if (time[i] == '+' || time[i] == '-') { time = time.substring(0, i); break; }
  }
  time.replace(":", "");

  return date + "_" + time;
}

bool write(const String &filename, const String &from, const String &to,
           const String &atTime, bool outgoing, const String &body) {
  FsFile file = Storage::card().open(filename.c_str(), O_RDWR | O_CREAT | O_TRUNC);
  if (!file) {
    LOGE("sms", "cannot create %s", filename.c_str());
    return false;
  }

  if (from.length()) file.println("From: " + from);
  if (to.length()) file.println("To: " + to);
  file.println("Time: " + atTime);
  file.println(outgoing ? "Dir: OUT" : "Dir: IN");
  file.println("Content: " + body);
  file.close();

  LOGI("sms", "wrote %s", filename.c_str());
  return true;
}

}  // namespace

bool isMessageFile(const char *filename) {
  return strncmp(filename, FILE_PREFIX, sizeof(FILE_PREFIX) - 1) == 0;
}

bool parseFile(FsFile &file, Message &out) {
  String from, to, dir, timeField, localTimeField, body;
  bool inBody = false;

  while (file.available()) {
    const String line = Storage::readLine(file);

    // Content is the last field in every layout, so once it starts, the rest of
    // the file is body -- including lines that happen to look like "Key: value".
    if (inBody) {
      body += '\n';
      body += line;
      continue;
    }

    String key, value;
    if (!splitField(line, key, value)) continue;

    if (key == "From") from = value;
    else if (key == "To") to = value;
    else if (key == "Dir") dir = value;
    else if (key == "Time") timeField = value;
    else if (key == "LocalTime") localTimeField = value;
    else if (key == "Content") { body = value; inBody = true; }
    // "Status" and anything else is informational only.
  }
  body.trim();

  if (body.length() == 0) return false;

  // Explicit direction if the file states one, otherwise a "To:" field means
  // we were the sender.
  out.outgoing = dir.length() ? dir.equalsIgnoreCase("OUT") : (to.length() > 0);
  out.peer = out.outgoing ? to : from;
  if (out.peer.length() == 0) return false;

  // Prefer LocalTime: it carries the real UTC offset, so wall-clock display is
  // right. "Time:" in those same files was already normalized to +00:00.
  const String &best = localTimeField.length() ? localTimeField : timeField;
  Timestamp::parse(best, out.stamp);  // an unparseable stamp sorts as unknown

  out.body = body;
  return true;
}

int countFiles() {
  int count = 0;
  FsFile root = Storage::card().open("/");
  if (!root) return 0;

  FsFile file;
  while (file.openNext(&root, O_RDONLY)) {
    char filename[64];
    file.getName(filename, sizeof(filename));
    if (isMessageFile(filename)) count++;
    file.close();
  }
  root.close();
  return count;
}

bool saveIncoming(const String &from, const String &atTime, const String &body) {
  const String id = fileIdFor(atTime);
  if (id.length() == 0 || body.length() == 0) return false;

  const String filename = String(FILE_PREFIX) + id + ".txt";
  if (Storage::card().exists(filename.c_str())) {
    LOGD("sms", "%s already stored", filename.c_str());
    return false;
  }
  return write(filename, from, String(), atTime, false, body);
}

bool saveOutgoing(const String &to, const String &atTime, const String &body) {
  if (body.length() == 0) return false;

  // Named from the timestamp, like received messages. Two messages can share a
  // second, so a counter breaks ties -- deliberately not millis(), which resets
  // on reboot and could then overwrite an older sent message.
  String id = fileIdFor(atTime);
  if (id.length() == 0) id = String(millis());

  const String base = String(FILE_PREFIX) + "out_" + id;
  String filename = base + ".txt";
  for (int n = 1; n < 100 && Storage::card().exists(filename.c_str()); n++) {
    filename = base + "_" + String(n) + ".txt";
  }
  return write(filename, String(), to, atTime, true, body);
}

}  // namespace MessageStore
