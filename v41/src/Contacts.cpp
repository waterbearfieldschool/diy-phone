#include "Contacts.h"

#include "Log.h"
#include "PhoneNumber.h"
#include "Storage.h"

namespace {

constexpr char PRIMARY_FILE[] = "addressbook.txt";
constexpr char FALLBACK_FILE[] = "contacts.txt";

// A field is the number if it starts with '+' or is a long run of digits.
bool looksLikeNumber(const String &field) {
  if (field.startsWith("+")) return true;
  if (field.length() < 6) return false;
  for (unsigned i = 0; i < field.length(); i++) {
    if (!isDigit(field[i])) return false;
  }
  return true;
}

}  // namespace

bool Contacts::load() {
  count_ = 0;

  FsFile file = Storage::card().open(PRIMARY_FILE, O_READ);
  if (!file) file = Storage::card().open(FALLBACK_FILE, O_READ);
  if (!file) {
    LOGI("contacts", "no address book on card");
    return false;
  }

  while (file.available() && count_ < MAX_CONTACTS) {
    String line = Storage::readLine(file);
    const int comma = line.indexOf(',');
    if (comma < 0) continue;

    String first = line.substring(0, comma);
    String second = line.substring(comma + 1);
    first.trim();
    second.trim();
    if (first.length() == 0 || second.length() == 0) continue;

    const bool firstIsNumber = looksLikeNumber(first);
    entries_[count_].number = firstIsNumber ? first : second;
    entries_[count_].name = firstIsNumber ? second : first;
    count_++;
  }
  file.close();

  LOGI("contacts", "loaded %d", count_);
  return count_ > 0;
}

bool Contacts::save() const {
  FsFile file = Storage::card().open(PRIMARY_FILE, O_WRITE | O_CREAT | O_TRUNC);
  if (!file) {
    LOGE("contacts", "cannot open %s for writing", PRIMARY_FILE);
    return false;
  }

  for (int i = 0; i < count_; i++) {
    file.print(entries_[i].name);
    file.print(',');
    file.println(entries_[i].number);
  }
  file.close();

  LOGI("contacts", "saved %d", count_);
  return true;
}

bool Contacts::add(const String &number, const String &name) {
  if (count_ >= MAX_CONTACTS) {
    LOGE("contacts", "address book full (%d)", MAX_CONTACTS);
    return false;
  }
  if (nameFor(number).length() > 0) return false;

  entries_[count_].number = number;
  entries_[count_].name = name;
  count_++;

  LOGI("contacts", "added %s -> %s", name.c_str(), number.c_str());
  return save();
}

String Contacts::nameFor(const String &number) const {
  for (int i = 0; i < count_; i++) {
    if (PhoneNumber::same(number, entries_[i].number)) return entries_[i].name;
  }
  return String();
}

String Contacts::displayName(const String &number) const {
  const String name = nameFor(number);
  return name.length() > 0 ? name : number;
}
