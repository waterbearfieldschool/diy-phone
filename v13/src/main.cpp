/**************************************************************************
  DIY Phone v13 - Basic SIM7600 Communication Test
  Features: Display + SIM7600 cellular communication
 **************************************************************************/

#include <Arduino.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <SPI.h>
#include "SIM7600.h"

// Custom SPI bus definition
SPIClass customSPI(NRF_SPIM2, A1, A2, A0);  // MISO=A1, SCK=A2, MOSI=A0

// Pin definitions for ItsyBitsy nRF52840
#define TFT_CS        A3
#define TFT_RST        12 // Or set to -1 and connect to Arduino RESET pin
#define TFT_DC         A5

// Create display object with custom SPI bus
Adafruit_ST7789 tft = Adafruit_ST7789(&customSPI, TFT_CS, TFT_DC, TFT_RST);

// SIM7600 cellular module - using Serial1 (hardware UART)
SIM7600 cellular(&Serial1);

void updateStatus(const char *text, uint16_t color);

GFXcanvas16 canvas(120, 60); // 16-bit, 120x10 pixels


void setup(void) {
  Serial.begin(115200);
  Serial.println("DIY Phone v13 Starting...");

  // Initialize custom SPI bus
  customSPI.begin();

  // Initialize display
  tft.init(240, 320);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);
  
  Serial.println("Display initialized");
  updateStatus("Initializing...", ST77XX_WHITE);

  // Configure Serial1 pins to match hardware: RX=A4, TX=D2
  Serial1.setPins(A4, 2);  // setPins(rx, tx)
  
  // Initialize SIM7600 at 115200 baud
  if (cellular.begin(115200)) {
    Serial.println("SIM7600 connected");
    updateStatus("SIM7600 connected", ST77XX_GREEN);
    
    // Enable caller ID
    cellular.enableCallerID();
    
    // Check signal quality
    int signal = cellular.getSignalQuality();
    char signalText[32];
    snprintf(signalText, sizeof(signalText), "Signal: %d", signal);
    Serial.println(signalText);
    
  } else {
    Serial.println("SIM7600 connection failed");
    updateStatus("SIM7600 failed", ST77XX_RED);
  }
  
  delay(1000);
}

#define statusY 10

void loop() {
  static unsigned long lastCheck = 0;
  static int testCount = 0;
  
  // Check SIM7600 every 5 seconds
  if (millis() - lastCheck > 5000) {
    lastCheck = millis();
    testCount++;
    
    char statusText[64];
    
    // Test different functions cyclically
    switch (testCount % 4) {
      case 0:
        // Check signal quality
        {
          int signal = cellular.getSignalQuality();
          snprintf(statusText, sizeof(statusText), "Signal: %d/31", signal);
          updateStatus(statusText, ST77XX_CYAN);
          Serial.println(statusText);
        }
        break;
        
      case 1:
        // Check connectivity
        if (cellular.isConnected()) {
          updateStatus("AT Commands OK", ST77XX_GREEN);
          Serial.println("SIM7600 responding to AT commands");
        } else {
          updateStatus("AT Commands Failed", ST77XX_RED);
          Serial.println("SIM7600 not responding");
        }
        break;
        
      case 2:
        // Check SMS storage and retrieve messages
        updateStatus("SMS Storage Check", ST77XX_YELLOW);
        cellular.checkSMSStorage();
        break;
        
      case 3:
        // Network status
        if (cellular.getNetworkStatus()) {
          updateStatus("Network Query OK", ST77XX_MAGENTA);
          Serial.println("Network status query successful");
        } else {
          updateStatus("Network Failed", ST77XX_RED);
          Serial.println("Network status query failed");
        }
        break;
    }
  }
  
  delay(100);
}


void updateStatus(const char *text, uint16_t color) {
  canvas.fillScreen(0x0000); // Clear canvas (not display)
  canvas.setCursor(0, 0);   // Pos. is BASE LINE when using fonts!
  canvas.setTextWrap(true);
  canvas.setTextColor(color);
  canvas.print(text);
  // Copy canvas to screen at upper-left corner.
  tft.drawRGBBitmap(0, statusY, canvas.getBuffer(), canvas.width(), canvas.height());
}


