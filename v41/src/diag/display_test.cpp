// display_test.cpp -- standalone wiring check for the ST7789 display.
//
// Built by the `displaytest` environment only (the phone firmware excludes
// this directory). It drives the TFT through the same SPIM2 bus and pins as
// Screen.cpp, so if this test passes, the phone's display path is wired
// correctly.
//
//   pio run -e displaytest
//
// What you should see, repeating every ~8 seconds:
//   full-screen red, green, blue, white (1s each), then a test card with
//   color bars, a white border, and an incrementing counter.
//
// Every step is also narrated on USB serial (115200), so a dead screen with
// live serial output means wiring, not firmware.

#include <Adafruit_ST7789.h>
#include <SPI.h>

#include "../Config.h"

SPIClass tftSPI(NRF_SPIM2, Pins::TFT_MISO, Pins::TFT_SCK, Pins::TFT_MOSI);
Adafruit_ST7789 tft(&tftSPI, Pins::TFT_CS, Pins::TFT_DC, Pins::TFT_RST);

void setup() {
  Serial.begin(CONSOLE_BAUD);
  const uint32_t start = millis();
  while (!Serial && millis() - start < 3000) delay(10);  // wait for USB, but don't require it

  Serial.println("display_test: ST7789 240x240 on SPIM2");
  Serial.println("  MOSI=2 (P0.17)  SCK=9 (P1.06)  CS=11 (P0.10)  DC=10 (P0.09)  RST=5 (P0.24)");

  tftSPI.begin();
  tft.init(SCREEN_W, SCREEN_H);
  tft.setRotation(3);
  Serial.println("init done -- if the screen stays white or black from here on, check wiring");
}

void solid(uint16_t color, const char *name) {
  Serial.print("fill ");
  Serial.println(name);
  tft.fillScreen(color);
  delay(1000);
}

void loop() {
  static int pass = 0;

  solid(ST77XX_RED, "RED");
  solid(ST77XX_GREEN, "GREEN");
  solid(ST77XX_BLUE, "BLUE");
  solid(ST77XX_WHITE, "WHITE");

  Serial.println("test card");
  tft.fillScreen(ST77XX_BLACK);

  // Color bars: gaps or wrong colors point at MOSI/SCK signal quality.
  const uint16_t bars[] = {ST77XX_RED,  ST77XX_YELLOW, ST77XX_GREEN,
                           ST77XX_CYAN, ST77XX_BLUE,   ST77XX_MAGENTA};
  const int barW = SCREEN_W / 6;
  for (int i = 0; i < 6; i++) {
    tft.fillRect(i * barW, 40, barW, 80, bars[i]);
  }

  // Border: all four edges visible means the panel offset/rotation is right.
  tft.drawRect(0, 0, SCREEN_W, SCREEN_H, ST77XX_WHITE);

  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 10);
  tft.print("DISPLAY OK");
  tft.setCursor(10, 140);
  tft.print("pass ");
  tft.print(++pass);

  tft.setTextSize(1);
  tft.setCursor(10, 170);
  tft.print("red|yel|grn|cyn|blu|mag bars");
  tft.setCursor(10, 182);
  tft.print("white border on all 4 edges");

  Serial.print("pass ");
  Serial.println(pass);
  delay(4000);
}
