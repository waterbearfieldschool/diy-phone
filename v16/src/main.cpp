/**************************************************************************
  DIY Phone v16 - SIM7600 with I2C Keyboard Control
  Features: Display + SIM7600 + SD card SMS storage + I2C keyboard control
  Tests triggered by keyboard: 1=Signal, 2=AT Test, 3=SMS Check, 4=SD Test, 5=SMS Read, 6=Network
 **************************************************************************/

#include <Arduino.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <SPI.h>
#include <Wire.h>
#include <SdFat.h>
#include "SIM7600.h"

// Custom SPI bus definition
SPIClass customSPI(NRF_SPIM2, A1, A2, A0);  // MISO=A1, SCK=A2, MOSI=A0

// Pin definitions for ItsyBitsy nRF52840
#define TFT_CS        A3
#define TFT_RST        12 // Or set to -1 and connect to Arduino RESET pin
#define TFT_DC         A5

// SD card pin (using hardware SPI)
#define SD_CS_PIN     10

// I2C keyboard address
#define KEYBOARD_ADDR 0x5F

// Create display object with custom SPI bus
Adafruit_ST7789 tft = Adafruit_ST7789(&customSPI, TFT_CS, TFT_DC, TFT_RST);

// SIM7600 cellular module - using Serial1 (hardware UART)
SIM7600 cellular(&Serial1);

// SD card object
SdFat sd;

// UART buffer for line reading
String uartLineBuffer = "";

// Function declarations
void updateStatus(const char *text, uint16_t color);
void readUARTLines();
void handleKeyboard();
String getKeyName(uint8_t keyCode);
void runTest(int testNumber);

GFXcanvas16 canvas(120, 60); // 16-bit, 120x60 pixels

#define statusY 10

void setup(void) {
  Serial.begin(115200);
  delay(2000); // Wait for serial connection
  Serial.println("=== DIY Phone v16 Starting ===");

  // Initialize custom SPI bus
  customSPI.begin();
  Serial.println("Custom SPI initialized");

  // Initialize display
  tft.init(240, 320);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);
  
  Serial.println("Display initialized");
  updateStatus("Initializing...", ST77XX_WHITE);
  
  // Initialize I2C for keyboard
  Serial.println("[DEBUG] Initializing I2C for keyboard...");
  Wire.begin();
  Serial.println("[DEBUG] I2C initialized for keyboard at address 0x5F");
  
  // Test I2C connection
  Wire.requestFrom(KEYBOARD_ADDR, 1);
  if (Wire.available()) {
    uint8_t testData = Wire.read();
    Serial.println("[DEBUG] I2C keyboard test successful, received: 0x" + String(testData, HEX));
    updateStatus("Keyboard OK", ST77XX_GREEN);
  } else {
    Serial.println("[DEBUG] WARNING: No response from I2C keyboard");
    updateStatus("Keyboard Warning", ST77XX_YELLOW);
  }
  delay(1000);

  // Initialize SD card
  Serial.println("About to initialize SD card...");
  if (sd.begin(SD_CS_PIN, SD_SCK_MHZ(4))) {
    Serial.println("SD card initialized");
    updateStatus("SD card OK", ST77XX_GREEN);
    
    // Test SD card by creating a test file
    FsFile testFile = sd.open("test.txt", O_WRITE | O_CREAT);
    if (testFile) {
      testFile.println("DIY Phone v16 Test");
      testFile.close();
      Serial.println("SD card test file created successfully");
      updateStatus("SD test OK", ST77XX_GREEN);
    } else {
      Serial.println("Failed to create SD test file");
      updateStatus("SD test failed", ST77XX_RED);
    }
  } else {
    Serial.println("SD card initialization failed");
    updateStatus("SD card failed", ST77XX_RED);
  }
  delay(1000);

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
  
  updateStatus("Ready - Press 1-6", ST77XX_CYAN);
  Serial.println("===============================================");
  Serial.println("Setup complete - Press keyboard numbers 1-6:");
  Serial.println("1 = Signal Quality Test");
  Serial.println("2 = AT Command Test");
  Serial.println("3 = SMS Check & Store");
  Serial.println("4 = SD Card Test");
  Serial.println("5 = Read SMS Files");
  Serial.println("6 = Network Status");
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
      
      // Check for number keys 1-6 to trigger tests
      if (keyData >= '1' && keyData <= '6') {
        int testNumber = keyData - '0';
        Serial.println("[KEYBOARD] Running test " + String(testNumber));
        runTest(testNumber);
      }
    }
  }
}

void runTest(int testNumber) {
  char statusText[64];
  
  switch (testNumber) {
    case 1:
      // Check signal quality
      {
        updateStatus("Signal Test", ST77XX_CYAN);
        Serial.println("=== Running Signal Quality Test ===");
        int signal = cellular.getSignalQuality();
        snprintf(statusText, sizeof(statusText), "Signal: %d/31", signal);
        updateStatus(statusText, ST77XX_CYAN);
        Serial.println(statusText);
      }
      break;
      
    case 2:
      // Check connectivity
      updateStatus("AT Test", ST77XX_YELLOW);
      Serial.println("=== Running AT Command Test ===");
      if (cellular.isConnected()) {
        updateStatus("AT Commands OK", ST77XX_GREEN);
        Serial.println("SIM7600 responding to AT commands");
      } else {
        updateStatus("AT Commands Failed", ST77XX_RED);
        Serial.println("SIM7600 not responding");
      }
      break;
      
    case 3:
      // Check SMS and store to SD card
      updateStatus("SMS Check", ST77XX_YELLOW);
      Serial.println("=== Running SMS Check & Store Test ===");
      cellular.checkAndStoreSMS();
      break;
      
    case 4:
      // SD card read/write test
      {
        updateStatus("SD Test", ST77XX_CYAN);
        Serial.println("=== Running SD Card Read/Write Test ===");
        
        // Write test
        char testFilename[32];
        snprintf(testFilename, sizeof(testFilename), "test_%lu.txt", millis());
        
        FsFile testFile = sd.open(testFilename, O_WRITE | O_CREAT);
        if (testFile) {
          testFile.print("Test write at: ");
          testFile.println(millis());
          testFile.close();
          Serial.print("✓ Created file: ");
          Serial.println(testFilename);
          
          // Read test
          FsFile readFile = sd.open(testFilename, O_READ);
          if (readFile) {
            Serial.print("✓ File contents: ");
            while (readFile.available()) {
              Serial.write(readFile.read());
            }
            readFile.close();
            updateStatus("SD Test OK", ST77XX_GREEN);
          } else {
            Serial.println("✗ Failed to read file");
            updateStatus("SD Read Failed", ST77XX_RED);
          }
        } else {
          Serial.println("✗ Failed to create test file");
          updateStatus("SD Write Failed", ST77XX_RED);
        }
      }
      break;
      
    case 5:
      // Read and print first 20 SMS files from SD card
      {
        updateStatus("Reading SMS", ST77XX_CYAN);
        Serial.println("=== Reading First 20 SMS Files from SD Card ===");
        
        int smsCount = 0;
        FsFile root = sd.open("/");
        FsFile file;
        
        while (file.openNext(&root, O_RDONLY) && smsCount < 20) {
          char filename[64];
          file.getName(filename, sizeof(filename));
          
          // Check if this is an SMS file (starts with "sms_")
          if (strncmp(filename, "sms_", 4) == 0) {
            smsCount++;
            Serial.print("=== SMS File #");
            Serial.print(smsCount);
            Serial.print(": ");
            Serial.print(filename);
            Serial.println(" ===");
            
            // Read and print file contents
            while (file.available()) {
              Serial.write(file.read());
            }
            Serial.println("--- End SMS ---\n");
          }
          file.close();
        }
        root.close();
        
        char smsCountText[32];
        snprintf(smsCountText, sizeof(smsCountText), "Read %d SMS", smsCount);
        updateStatus(smsCountText, ST77XX_GREEN);
        Serial.print("Total SMS files read: ");
        Serial.println(smsCount);
      }
      break;
      
    case 6:
      // Network status  
      updateStatus("Network Test", ST77XX_MAGENTA);
      Serial.println("=== Running Network Status Test ===");
      if (cellular.getNetworkStatus()) {
        updateStatus("Network OK", ST77XX_GREEN);
        Serial.println("Network status query successful");
      } else {
        updateStatus("Network Failed", ST77XX_RED);
        Serial.println("Network status query failed");
      }
      break;
      
    default:
      Serial.println("Unknown test number: " + String(testNumber));
      break;
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

void updateStatus(const char *text, uint16_t color) {
  canvas.fillScreen(0x0000); // Clear canvas (not display)
  canvas.setCursor(0, 0);   // Pos. is BASE LINE when using fonts!
  canvas.setTextWrap(true);
  canvas.setTextColor(color);
  canvas.print(text);
  // Copy canvas to screen at upper-left corner.
  tft.drawRGBBitmap(0, statusY, canvas.getBuffer(), canvas.width(), canvas.height());
}