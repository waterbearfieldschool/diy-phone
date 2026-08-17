#include "Log.h"

#include <stdarg.h>

LogLevel logLevel = LogLevel::Info;

void logAt(LogLevel level, const char *tag, const char *fmt, ...) {
  if (level > logLevel) return;

  char buf[160];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  Serial.print('[');
  Serial.print(tag);
  Serial.print("] ");
  Serial.println(buf);
}

extern "C" char *sbrk(int incr);

uint32_t freeRam() {
  char stackTop;
  return (uint32_t)(&stackTop - reinterpret_cast<char *>(sbrk(0)));
}
