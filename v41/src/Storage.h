// Storage.h -- owns the SD card and its dedicated SPI bus.
#pragma once

#include <SdFat.h>

namespace Storage {

bool begin();
SdFat &card();

// Reads one newline-terminated line, without the terminator.
String readLine(FsFile &file);

}  // namespace Storage
