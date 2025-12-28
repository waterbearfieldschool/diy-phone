/**************************************************************************
  Minimal SIM7600 Debug Monitor v11
  Purpose: Show "hello" on display + monitor UART and keyboard via Serial
 **************************************************************************/

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// Custom SPI bus for display
SPIClass customSPI(NRF_SPIM2, A1, A2, A0);  // MISO=A1, SCK=A2, MOSI=A0

// Display pin definitions
#define TFT_CS        A3
#define TFT_RST       12
#define TFT_DC        A5

// I2C keyboard address
#define KEYBOARD_ADDR 0x5F

// Display object
Adafruit_ST7789 tft = Adafruit_ST7789(&customSPI, TFT_CS, TFT_DC, TFT_RST);

// UART buffer for line reading
String uartLineBuffer = "";

// Function declarations
void readUARTLines();
void handleKeyboard();
String getKeyName(uint8_t keyCode);

void setup() {
  // Initialize Serial for debug output
  Serial.begin(115200);
  Serial.println("=== SIM7600 Debug Monitor v11 ===");
  
  // Initialize display
  Serial.println("[DEBUG] Initializing display...");
  customSPI.begin();
  tft.init(320, 240);
  tft.setRotation(1);  // Landscape
  tft.fillScreen(ST77XX_BLACK);
  
  // Display "hello" message
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(3);
  tft.setCursor(100, 110);
  tft.println("hello");
  Serial.println("[DEBUG] Display showing 'hello'");
  
  // Initialize UART for SIM7600 (TX=D2, RX=A4)
  Serial.println("[DEBUG] Initializing UART for SIM7600...");
  Serial1.setPins(A4, 2);  // setPins(rx, tx)
  Serial1.begin(115200);
  Serial.println("[DEBUG] UART initialized at 115200 baud (TX=D2, RX=A4)");
  
  // Initialize I2C for keyboard
  Serial.println("[DEBUG] Initializing I2C for keyboard...");
  Wire.begin();
  Serial.println("[DEBUG] I2C initialized for keyboard at address 0x5F");
  
  // Test I2C connection
  Wire.requestFrom(KEYBOARD_ADDR, 1);
  if (Wire.available()) {
    uint8_t testData = Wire.read();
    Serial.println("[DEBUG] I2C keyboard test successful, received: 0x" + String(testData, HEX));
  } else {
    Serial.println("[DEBUG] WARNING: No response from I2C keyboard");
  }
  
  Serial.println("[DEBUG] Setup complete - monitoring UART and keyboard...");
  Serial.println("===============================================");
}

void loop() {
  // Monitor UART from SIM7600
  readUARTLines();
  
  // Monitor keyboard
  handleKeyboard();
  
  delay(10);  // Small delay for stability
}

void readUARTLines() {
  while (Serial1.available()) {
    char c = Serial1.read();
    
    if (c == '\r') {
      // Process complete line on carriage return
      String line = uartLineBuffer;
      line.trim();
      if (line.length() > 0) {
        Serial.println("[UART RX] " + line);
      }
      uartLineBuffer = "";
    } else if (c != '\n') {  // Ignore line feed characters
      uartLineBuffer += c;
    }
  }
}

void handleKeyboard() {
  Wire.requestFrom(KEYBOARD_ADDR, 1);
  
  if (Wire.available()) {
    uint8_t keyData = Wire.read();
    
    if (keyData != 0) {
      String keyName = getKeyName(keyData);
      char printableChar = (keyData >= 32 && keyData <= 126) ? (char)keyData : '?';
      Serial.println("[KEYBOARD] Key pressed: 0x" + String(keyData, HEX) + " (" + keyName + ") char: '" + String(printableChar) + "'");
    }
  }
}

String getKeyName(uint8_t keyCode) {
  switch (keyCode) {
    case 0xB5: return "UP";
    case 0xB6: return "DOWN";
    case 0xB4: return "LEFT";
    case 0xB7: return "RIGHT";
    case 0x0D: return "ENTER";
    case 0x1B: return "ESC";
    case 0x08: return "BACKSPACE";
    case 0x20: return "SPACE";
    case 0x09: return "TAB";
    default:
      if (keyCode >= 32 && keyCode <= 126) {
        return "'" + String((char)keyCode) + "'";
      } else {
        return "UNKNOWN";
      }
  }
}