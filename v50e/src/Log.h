// Log.h -- one leveled logger, so tracing can be turned down instead of deleted.
//
// Levels are runtime-adjustable (the '9' diagnostic toggles Debug) and the
// Debug macro compiles to nothing when LOG_COMPILE_DEBUG is 0.
#pragma once

#include <Arduino.h>

#ifndef LOG_COMPILE_DEBUG
#define LOG_COMPILE_DEBUG 1
#endif

enum class LogLevel : uint8_t { None = 0, Error = 1, Info = 2, Debug = 3 };

extern LogLevel logLevel;

void logAt(LogLevel level, const char *tag, const char *fmt, ...);

#define LOGE(tag, ...) logAt(LogLevel::Error, tag, __VA_ARGS__)
#define LOGI(tag, ...) logAt(LogLevel::Info, tag, __VA_ARGS__)

#if LOG_COMPILE_DEBUG
#define LOGD(tag, ...) logAt(LogLevel::Debug, tag, __VA_ARGS__)
#else
#define LOGD(tag, ...) ((void)0)
#endif

// Bytes between the top of the heap and the current stack pointer.
uint32_t freeRam();
