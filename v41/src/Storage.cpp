#include "Storage.h"

#include <SPI.h>

#include "Config.h"
#include "Log.h"

namespace Storage {
namespace {

// The SD card gets its own SPI peripheral so display writes cannot corrupt a
// transfer mid-block.
SPIClass sdSPI(NRF_SPIM3, Pins::SD_MISO, Pins::SD_SCK, Pins::SD_MOSI);
SdFat sd;

}  // namespace

bool begin() {
  sdSPI.begin();
  SdSpiConfig config(Pins::SD_CS, DEDICATED_SPI, SD_SCK_MHZ(4), &sdSPI);
  if (!sd.begin(config)) {
    LOGE("sd", "initialization failed");
    return false;
  }
  LOGI("sd", "ready");
  return true;
}

SdFat &card() { return sd; }

String readLine(FsFile &file) {
  String line;
  while (file.available()) {
    const char c = file.read();
    if (c == '\n') break;
    if (c != '\r') line += c;
  }
  return line;
}

}  // namespace Storage
