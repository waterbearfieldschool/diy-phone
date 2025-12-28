/**************************************************************************
  DIY Phone v15 - SIM7600 with SD Card SMS Storage + SMS Reading
  Features: Display + SIM7600 + SD card SMS storage + Read SMS files
 **************************************************************************/

#include <Arduino.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <SPI.h>
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

// Create display object with custom SPI bus
Adafruit_ST7789 tft = Adafruit_ST7789(&customSPI, TFT_CS, TFT_DC, TFT_RST);

// SIM7600 cellular module - using Serial1 (hardware UART)
SIM7600 cellular(&Serial1);

// SD card object
SdFat sd;

void updateStatus(const char *text, uint16_t color);

GFXcanvas16 canvas(120, 60); // 16-bit, 120x10 pixels


void setup(void) {
  Serial.begin(115200);
  delay(2000); // Wait for serial connection
  Serial.println("=== DIY Phone v15 Starting ===");

  // Initialize custom SPI bus
  customSPI.begin();
  Serial.println("Custom SPI initialized");

  // Initialize display
  tft.init(240, 320);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);
  
  Serial.println("Display initialized");
  updateStatus("Initializing...", ST77XX_WHITE);
  Serial.println("About to initialize SD card...");

  // Initialize SD card
  if (sd.begin(SD_CS_PIN, SD_SCK_MHZ(4))) {
    Serial.println("SD card initialized");
    updateStatus("SD card OK", ST77XX_GREEN);
    
    // Test SD card by creating a test file
    FsFile testFile = sd.open("test.txt", O_WRITE | O_CREAT);
    if (testFile) {
      testFile.println("DIY Phone v15 Test");
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
    switch (testCount % 6) {
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
        // Check SMS and store to SD card
        updateStatus("SMS -> SD Card", ST77XX_YELLOW);
        cellular.checkAndStoreSMS();
        break;
        
      case 3:
        // SD card read/write test
        {
          updateStatus("SD Card Test", ST77XX_CYAN);
          Serial.println("=== SD Card Read/Write Test ===");
          
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
        
      case 4:
        // Read and print first 20 SMS files from SD card
        {
          updateStatus("Reading SMS Files", ST77XX_CYAN);
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
          snprintf(smsCountText, sizeof(smsCountText), "Read %d SMS files", smsCount);
          updateStatus(smsCountText, ST77XX_GREEN);
          Serial.print("Total SMS files read: ");
          Serial.println(smsCount);
        }
        break;
        
      case 5:
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


