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

// Four stacked bands filling the 240px height exactly.
constexpr int STATUS_Y  = 0;
constexpr int STATUS_H  = 12;
constexpr int CONTACTS_Y = STATUS_Y + STATUS_H;          // 12
constexpr int CONTACTS_H = 69;
constexpr int CONV_Y     = CONTACTS_Y + CONTACTS_H;      // 81
constexpr int CONV_H     = 139;
constexpr int COMPOSE_Y  = CONV_Y + CONV_H;              // 220
constexpr int COMPOSE_H  = SCREEN_H - COMPOSE_Y;         // 20

constexpr int PANE_HEADER_H = 20;  // title strip at the top of a pane
constexpr int ROW_INSET     = 5;   // left inset for text inside a pane
constexpr int FIRST_ROW_Y   = 15;  // first list row, measured from the pane top

// Conversation bubbles: incoming flush left, outgoing indented a third across.
constexpr int MSG_IN_X   = 2;
constexpr int MSG_OUT_X  = SCREEN_W / 3;
constexpr int MSG_TIME_W = 30;     // room for "HH:MM" at the right edge
constexpr int MSG_GAP    = 2;      // blank pixels between messages
constexpr int MAX_WRAP_LINES = 40; // guard against a pathological message

constexpr int CONV_VIEW_Y = CONV_Y + PANE_HEADER_H;      // 101
constexpr int CONV_VIEW_H = CONV_H - PANE_HEADER_H;      // 119
constexpr int SCROLL_STEP = LINE_H;

// ---------------------------------------------------------------- behaviour
constexpr uint32_t SEARCH_TIMEOUT_MS = 2000;  // idle time that clears contact search
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
