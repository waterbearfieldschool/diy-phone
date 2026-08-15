// ArduinoShim.h -- just enough Arduino for the host tests.
//
// The point of the shim is that the tests compile the REAL Timestamp.cpp and
// PhoneNumber.cpp, so what passes here is the code that ships.
#pragma once

#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

class String {
 public:
  String() = default;
  String(const char *s) : s_(s ? s : "") {}
  String(const std::string &s) : s_(s) {}
  explicit String(int v) : s_(std::to_string(v)) {}
  explicit String(unsigned long v) : s_(std::to_string(v)) {}

  unsigned length() const { return (unsigned)s_.size(); }
  const char *c_str() const { return s_.c_str(); }
  void reserve(unsigned) {}

  char operator[](unsigned i) const { return s_[i]; }

  int indexOf(char c) const { return find(s_.find(c)); }
  int indexOf(char c, unsigned from) const { return find(s_.find(c, from)); }
  int indexOf(const String &t) const { return find(s_.find(t.s_)); }
  int indexOf(const String &t, unsigned from) const { return find(s_.find(t.s_, from)); }
  int lastIndexOf(char c) const { return find(s_.rfind(c)); }

  String substring(unsigned from) const {
    return from >= s_.size() ? String() : String(s_.substr(from));
  }
  String substring(unsigned from, unsigned to) const {
    if (from >= s_.size() || to <= from) return String();
    return String(s_.substr(from, to - from));
  }

  long toInt() const {
    try { return std::stol(s_); } catch (...) { return 0; }
  }

  void trim() {
    const char *ws = " \t\r\n";
    const size_t b = s_.find_first_not_of(ws);
    if (b == std::string::npos) { s_.clear(); return; }
    s_ = s_.substr(b, s_.find_last_not_of(ws) - b + 1);
  }
  void toLowerCase() {
    for (auto &c : s_) c = (char)std::tolower((unsigned char)c);
  }
  void replace(const String &from, const String &to) {
    size_t at = 0;
    while ((at = s_.find(from.s_, at)) != std::string::npos) {
      s_.replace(at, from.s_.size(), to.s_);
      at += to.s_.size();
    }
  }

  bool startsWith(const String &t) const { return s_.rfind(t.s_, 0) == 0; }
  bool endsWith(const String &t) const {
    return s_.size() >= t.s_.size() &&
           s_.compare(s_.size() - t.s_.size(), t.s_.size(), t.s_) == 0;
  }
  bool equalsIgnoreCase(const String &t) const {
    if (s_.size() != t.s_.size()) return false;
    for (size_t i = 0; i < s_.size(); i++) {
      if (std::tolower((unsigned char)s_[i]) != std::tolower((unsigned char)t.s_[i])) return false;
    }
    return true;
  }

  String &operator+=(const String &t) { s_ += t.s_; return *this; }
  String &operator+=(char c) { s_ += c; return *this; }
  bool operator==(const String &t) const { return s_ == t.s_; }
  bool operator!=(const String &t) const { return s_ != t.s_; }
  friend String operator+(String a, const String &b) { a += b; return a; }

 private:
  static int find(size_t pos) { return pos == std::string::npos ? -1 : (int)pos; }
  std::string s_;
};

inline bool isDigit(char c) { return std::isdigit((unsigned char)c) != 0; }
inline bool isAlphaNumeric(char c) { return std::isalnum((unsigned char)c) != 0; }
inline unsigned long millis() { return 0; }

// Log.cpp writes through Serial; send it to stderr so test output stays clean.
struct SerialShim {
  void print(char c) { fputc(c, stderr); }
  void print(const char *s) { fputs(s, stderr); }
  void println(const char *s) { fputs(s, stderr); fputc('\n', stderr); }
};
extern SerialShim Serial;
