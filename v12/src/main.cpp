/**************************************************************************
  SIM7600 Debug Monitor v12
  Purpose: Show "hello" + monitor UART/keyboard + read SMS files from SD
  Translated SMS loading logic from p26.py CircuitPython to Arduino
 **************************************************************************/

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <SdFat.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// Custom SPI bus for display
SPIClass customSPI(NRF_SPIM2, A1, A2, A0);  // MISO=A1, SCK=A2, MOSI=A0

// Display pin definitions
#define TFT_CS        A3
#define TFT_RST       12
#define TFT_DC        A5

// SD card pin definition
#define SD_CS         10

// I2C keyboard address
#define KEYBOARD_ADDR 0x5F

// Display and SD objects
Adafruit_ST7789 tft = Adafruit_ST7789(&customSPI, TFT_CS, TFT_DC, TFT_RST);
SdFat SD;
File dataFile;

// UART buffer for line reading
String uartLineBuffer = "";

// SMS data structure
struct SMSMessage {
  String filename;
  String sender;
  String time;
  String status;
  String content;
};

// Function declarations
void readUARTLines();
void handleKeyboard();
String getKeyName(uint8_t keyCode);
void setupSDCard();
void loadSMSFromSD();
void printSMSToSerial(SMSMessage* messages, int count);
void createTestSMSFiles();

void setup() {
  // Initialize Serial for debug output
  Serial.begin(115200);
  Serial.println("=== SIM7600 Debug Monitor v12 ===");
  
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
  
  // Initialize SD card
  setupSDCard();
  
  // Create some test SMS files for demonstration
  createTestSMSFiles();
  
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
  Serial.println("[DEBUG] Press 'n' key to load SMS files from SD card");
  Serial.println("===============================================");
}

void loop() {
  // Monitor UART from SIM7600
  readUARTLines();
  
  // Monitor keyboard
  handleKeyboard();
  
  delay(10);  // Small delay for stability
}

void setupSDCard() {
  Serial.println("[DEBUG] Initializing SD card...");
  
  if (!SD.begin(SD_CS)) {
    Serial.println("[DEBUG] ERROR: SD card initialization failed!");
    return;
  }
  
  Serial.println("[DEBUG] SD card initialized successfully");
  
  // Test SD card with a simple write/read
  dataFile = SD.open("test.txt", FILE_WRITE);
  if (dataFile) {
    dataFile.println("Hello SD card!");
    dataFile.close();
    Serial.println("[DEBUG] SD card test write successful");
  } else {
    Serial.println("[DEBUG] WARNING: SD card test write failed");
  }
}

void createTestSMSFiles() {
  Serial.println("[DEBUG] Creating test SMS files...");
  
  // Create test SMS file 1
  dataFile = SD.open("sms_251226_143000.txt", FILE_WRITE);
  if (dataFile) {
    dataFile.println("From: +16512524765");
    dataFile.println("Time: 25/12/26,14:30:00-32");
    dataFile.println("Status: REC READ");
    dataFile.println("Content: Hello from test message 1");
    dataFile.close();
    Serial.println("[DEBUG] Created test SMS file 1");
  }
  
  // Create test SMS file 2
  dataFile = SD.open("sms_251226_145500.txt", FILE_WRITE);
  if (dataFile) {
    dataFile.println("From: +17813230341");
    dataFile.println("Time: 25/12/26,14:55:00-32");
    dataFile.println("Status: REC UNREAD");
    dataFile.println("Content: This is test message number 2 with longer content");
    dataFile.close();
    Serial.println("[DEBUG] Created test SMS file 2");
  }
  
  // Create test SMS file 3
  dataFile = SD.open("sms_251226_160000.txt", FILE_WRITE);
  if (dataFile) {
    dataFile.println("From: +16174299144");
    dataFile.println("Time: 25/12/26,16:00:00-32");
    dataFile.println("Status: REC READ");
    dataFile.println("Content: Short msg 3");
    dataFile.close();
    Serial.println("[DEBUG] Created test SMS file 3");
  }
}

void loadSMSFromSD() {
  Serial.println("[DEBUG] Loading SMS files from SD card...");
  Serial.println("==========================================");
  
  // Array to store SMS messages
  SMSMessage smsMessages[20];
  int smsCount = 0;
  
  // List directory contents (simplified approach)
  File root = SD.open("/");
  
  while (true && smsCount < 20) {
    File entry = root.openNextFile();
    if (!entry) {
      // No more files
      break;
    }
    
    char nameBuffer[64];
    entry.getName(nameBuffer, sizeof(nameBuffer));
    String filename = String(nameBuffer);
    
    // Check if it's an SMS file (starts with "sms_" and ends with ".txt")
    if (filename.startsWith("sms_") && filename.endsWith(".txt")) {
      Serial.println("[DEBUG] Found SMS file: " + filename);
      
      entry.close();
      
      // Read the SMS file content
      dataFile = SD.open(filename.c_str(), FILE_READ);
      if (dataFile) {
        String lines[4];
        int lineCount = 0;
        
        // Read up to 4 lines
        while (dataFile.available() && lineCount < 4) {
          lines[lineCount] = dataFile.readStringUntil('\n');
          lines[lineCount].trim();
          lineCount++;
        }
        dataFile.close();
        
        // Parse SMS data if we have at least 4 lines
        if (lineCount >= 4) {
          smsMessages[smsCount].filename = filename;
          smsMessages[smsCount].sender = lines[0];
          smsMessages[smsCount].sender.replace("From: ", "");
          smsMessages[smsCount].time = lines[1];
          smsMessages[smsCount].time.replace("Time: ", "");
          smsMessages[smsCount].status = lines[2];
          smsMessages[smsCount].status.replace("Status: ", "");
          smsMessages[smsCount].content = lines[3];
          smsMessages[smsCount].content.replace("Content: ", "");
          
          smsCount++;
          Serial.println("[DEBUG] Successfully parsed SMS: " + filename);
        } else {
          Serial.println("[DEBUG] Invalid SMS file format: " + filename);
        }
      } else {
        Serial.println("[DEBUG] Failed to open SMS file: " + filename);
      }
    } else {
      entry.close();
    }
  }
  
  root.close();
  
  Serial.println("[DEBUG] Loaded " + String(smsCount) + " SMS messages");
  Serial.println("==========================================");
  
  // Print all loaded SMS messages to serial
  printSMSToSerial(smsMessages, smsCount);
}

void printSMSToSerial(SMSMessage* messages, int count) {
  Serial.println("");
  Serial.println("=== SMS MESSAGES FROM SD CARD ===");
  Serial.println("Found " + String(count) + " messages:");
  Serial.println("");
  
  for (int i = 0; i < count; i++) {
    Serial.println("--- Message " + String(i + 1) + " ---");
    Serial.println("File: " + messages[i].filename);
    Serial.println("From: " + messages[i].sender);
    Serial.println("Time: " + messages[i].time);
    Serial.println("Status: " + messages[i].status);
    Serial.println("Content: " + messages[i].content);
    Serial.println("");
  }
  
  Serial.println("=== END SMS MESSAGES ===");
  Serial.println("");
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
      
      // Check for 'n' key to load SMS files
      if (keyData == 'n' || keyData == 'N') {
        Serial.println("[KEYBOARD] 'N' pressed - loading SMS files from SD card...");
        loadSMSFromSD();
      }
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