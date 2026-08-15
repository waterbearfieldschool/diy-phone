// Config.h -- every pin, dimension and capacity in the phone, in one place.
#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------- hardware
// ProMicro nRF52840. Pin numbers are Arduino pins; see promicromap.txt for
// the nRF52840 GPIO each one lands on.
namespace Pins {
  // TFT display, on SPIM2. MISO is unused by the ST7789 but the SPIClass
  // constructor requires a pin, so pin 6 is a placeholder.
  constexpr uint8_t TFT_MISO = 6;   // P1.00 (placeholder, unused)
  constexpr uint8_t TFT_SCK  = 9;   // P1.06
  constexpr uint8_t TFT_MOSI = 2;   // P0.17
  constexpr uint8_t TFT_CS   = 11;  // P0.10
  constexpr uint8_t TFT_DC   = 10;  // P0.09
  constexpr uint8_t TFT_RST  = 5;   // P0.24

  // SD card, on its own bus (SPIM3) so display traffic can't disturb it.
  constexpr uint8_t SD_MISO = 15;   // P0.02
  constexpr uint8_t SD_SCK  = 12;   // P1.11
  constexpr uint8_t SD_MOSI = 14;   // P1.15
  constexpr uint8_t SD_CS   = 13;   // P1.13

  // SIM7600 cellular modem, on Serial1.
  constexpr uint8_t MODEM_RX = 4;   // P0.22, MCU receives
  constexpr uint8_t MODEM_TX = 3;   // P0.20, MCU transmits

  // I2C keyboard uses the default Wire pins: SDA=8 (P1.04), SCL=7 (P0.11).
}

constexpr uint8_t  KEYBOARD_I2C_ADDR = 0x5F;
constexpr uint32_t MODEM_BAUD        = 115200;
constexpr uint32_t CONSOLE_BAUD      = 115200;

// ---------------------------------------------------------------- capacities
constexpr int MAX_CONTACTS        = 100;
constexpr int MAX_THREADS         = 100;  // >= MAX_CONTACTS so every contact is reachable
constexpr int MAX_THREAD_MESSAGES = 30;   // most recent messages held for the open thread
constexpr int MAX_COMPOSE_CHARS   = 160;
constexpr int MAX_SEARCH_CHARS    = 16;
constexpr int PREVIEW_CHARS       = 26;   // message preview shown in the contact row

// ---------------------------------------------------------------- screen
constexpr int SCREEN_W = 240;
constexpr int SCREEN_H = 240;

constexpr int CHAR_W  = 6;   // Adafruit_GFX built-in font at size 1
constexpr int LINE_H  = 10;

// Screen regions are defined by the UI in main.cpp; only the shared
// character metrics live here.

// ---------------------------------------------------------------- behaviour
constexpr uint32_t STATUS_REFRESH_MS = 5000;  // how often the RAM readout ticks

constexpr uint8_t SPEAKER_VOLUME_MAX = 5;  // AT+CLVL range
constexpr uint8_t MIC_GAIN_MAX       = 8;  // AT+CMICGAIN range
constexpr uint8_t SPEAKER_VOLUME_DEFAULT = 4;
constexpr uint8_t MIC_GAIN_DEFAULT       = 4;

// ---------------------------------------------------------------- keyboard
namespace Key {
  constexpr uint8_t Backspace = 0x08;
  constexpr uint8_t Tab       = 0x09;
  constexpr uint8_t Enter     = 0x0D;
  constexpr uint8_t Esc       = 0x1B;
  constexpr uint8_t Left      = 0xB4;
  constexpr uint8_t Up        = 0xB5;
  constexpr uint8_t Down      = 0xB6;
  constexpr uint8_t Right     = 0xB7;

  inline bool isPrintable(uint8_t k) { return k >= 32 && k <= 126; }
  inline bool isLetter(uint8_t k) {
    return (k >= 'A' && k <= 'Z') || (k >= 'a' && k <= 'z');
  }
}
