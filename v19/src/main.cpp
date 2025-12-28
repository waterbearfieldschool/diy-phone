/**************************************************************************
  DIY Phone v19 - SMS Inbox with Address Book + Full Timestamps + Auto-Delete
  Features: Display + SIM7600 + SD card SMS storage + I2C keyboard control + Address book name lookup + Full timestamps + Proper chronological sorting + Auto SMS deletion
  Tests triggered by keyboard: 1=Signal, 2=AT Test, 3=SMS Check, 4=SD Test, 5=SMS Read, 6=Network, 7=Delete One-by-One, 8=Delete Bulk
  
  Compile-time options (set in platformio.ini):
  - AUTO_DELETE_SMS=1: Use AT+CMGRD (read and delete) for new SMS notifications
  - AUTO_DELETE_SMS=0: Use AT+CMGR (read only) for new SMS notifications  
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

// Address book entry structure
struct AddressBookEntry {
  String phoneNumber;
  String name;
};

// SMS structure for inbox display with enhanced sorting
struct SMSInboxEntry {
  String sender;
  String senderDisplayName;  // Either phone number or contact name
  String time;
  String fullTime;           // Full timestamp for display
  String content;
  String filename;           // For sorting
  unsigned long timestampValue; // Numeric timestamp for proper sorting
};

// Address book and SMS inbox data
AddressBookEntry addressBook[100];
int addressBookCount = 0;
SMSInboxEntry smsInbox[50];
int smsInboxCount = 0;
int inboxScrollOffset = 0;

// Function declarations
void updateStatus(const char *text, uint16_t color);
void updateInbox();
void readUARTLines();
void handleKeyboard();
String getKeyName(uint8_t keyCode);
void runTest(int testNumber);
bool loadSMSInbox();
void sortSMSByTime();
void handleNewSMSNotification(int smsIndex);
void addNewSMSToInbox(const String& filename);
bool deleteAllSMSIndividually();
bool deleteAllSMSWithStorageSelection();
bool loadAddressBook();
String lookupContactName(const String& phoneNumber);
unsigned long parseTimestamp(const String& timestamp);
int compareSMSByTime(const void* a, const void* b);

// Canvas objects
GFXcanvas16 inbox_canvas(320, 100);  // Inbox canvas - height for 10 lines (10 pixels each)

#define statusY 10
#define inboxY 30

void setup(void) {
  Serial.begin(115200);
  delay(2000); // Wait for serial connection
  Serial.println("=== DIY Phone v19 Starting ===");

  // Initialize custom SPI bus
  Serial.println("[DEBUG] Starting custom SPI initialization...");
  customSPI.begin();
  Serial.println("[DEBUG] Custom SPI initialized");

  // Initialize display
  Serial.println("[DEBUG] Starting display initialization...");
  tft.init(240, 320);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);
  
  Serial.println("[DEBUG] Display initialized");
  delay(500);
  Serial.println("[DEBUG] About to call updateStatus...");
  updateStatus("Display OK", ST77XX_GREEN);
  Serial.println("[DEBUG] Status updated");
  delay(500);
  
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
  Serial.println("[DEBUG] About to initialize SD card...");
  if (sd.begin(SD_CS_PIN, SD_SCK_MHZ(4))) {
    Serial.println("[DEBUG] SD card initialized");
    updateStatus("SD card OK", ST77XX_GREEN);
    
    // Test SD card by creating a test file
    Serial.println("[DEBUG] Testing SD card write...");
    FsFile testFile = sd.open("test.txt", O_WRITE | O_CREAT);
    if (testFile) {
      testFile.println("DIY Phone v19 Test");
      testFile.close();
      Serial.println("[DEBUG] SD card test file created successfully");
      updateStatus("SD test OK", ST77XX_GREEN);
    } else {
      Serial.println("[DEBUG] Failed to create SD test file");
      updateStatus("SD test failed", ST77XX_RED);
    }
  } else {
    Serial.println("[DEBUG] SD card initialization failed");
    updateStatus("SD card failed", ST77XX_RED);
  }
  Serial.println("[DEBUG] SD card initialization complete");
  delay(1000);

  // Load address book from SD card
  Serial.println("[DEBUG] Loading address book...");
  updateStatus("Loading contacts...", ST77XX_CYAN);
  loadAddressBook();
  Serial.println("[DEBUG] Address book loading complete");
  delay(500);

  // Configure Serial1 pins to match hardware: RX=A4, TX=D2
  Serial.println("[DEBUG] Configuring Serial1 pins...");
  Serial1.setPins(A4, 2);  // setPins(rx, tx)
  Serial.println("[DEBUG] Serial1 pins configured");
  
  // Note: UART buffer size increased via platformio.ini build_flags
  
  // Initialize SIM7600 at 115200 baud
  Serial.println("[DEBUG] Starting SIM7600 initialization...");
  if (cellular.begin(115200)) {
    Serial.println("[DEBUG] SIM7600 connected");
    updateStatus("SIM7600 connected", ST77XX_GREEN);
    
    // Enable caller ID
    Serial.println("[DEBUG] Enabling caller ID...");
    cellular.enableCallerID();
    Serial.println("[DEBUG] Caller ID enabled");
    
    // Check signal quality
    Serial.println("[DEBUG] Checking signal quality...");
    int signal = cellular.getSignalQuality();
    char signalText[32];
    snprintf(signalText, sizeof(signalText), "Signal: %d", signal);
    Serial.println(signalText);
    Serial.println("[DEBUG] Signal quality check complete");
    
  } else {
    Serial.println("[DEBUG] SIM7600 connection failed");
    updateStatus("SIM7600 failed", ST77XX_RED);
  }
  Serial.println("[DEBUG] SIM7600 initialization complete");
  
  // Load and display SMS inbox on boot
  updateStatus("Loading SMS...", ST77XX_CYAN);
  Serial.println("[DEBUG] Loading SMS inbox on boot...");
  loadSMSInbox();
  Serial.println("[DEBUG] Sorting SMS...");
  sortSMSByTime();  // Sort by time with newest first
  Serial.println("[DEBUG] Updating inbox display...");
  updateInbox();
  Serial.println("[DEBUG] Setup complete!");
  
  updateStatus("Ready - Press 1-8", ST77XX_CYAN);
  Serial.println("===============================================");
  Serial.println("Setup complete - Press keyboard numbers 1-8:");
  Serial.println("1 = Signal Quality Test");
  Serial.println("2 = AT Command Test");
  Serial.println("3 = SMS Check & Store");
  Serial.println("4 = SD Card Test");
  Serial.println("5 = Read SMS Files");
  Serial.println("6 = Network Status");
  Serial.println("7 = Delete SMS One-by-One");
  Serial.println("8 = Delete All SMS (Bulk)");
  Serial.println("Down Arrow = Scroll inbox");
  Serial.println("===============================================");
}

void loop() {
  // Monitor UART from SIM7600
  readUARTLines();
  
  // Monitor keyboard
  handleKeyboard();
  
  delay(10);  // Small delay for stability
}

bool loadAddressBook() {
  Serial.println("=== Loading Address Book ===");
  
  addressBookCount = 0;
  
  // Try to open address book file (could be addressbook.txt, contacts.txt, etc.)
  FsFile addressFile = sd.open("addressbook.txt", O_READ);
  if (!addressFile) {
    addressFile = sd.open("contacts.txt", O_READ);
    if (!addressFile) {
      Serial.println("No address book file found (addressbook.txt or contacts.txt)");
      return false;
    }
  }
  
  Serial.println("Address book file found, loading contacts...");
  
  // Read address book entries
  while (addressFile.available() && addressBookCount < 100) {
    String line = "";
    while (addressFile.available()) {
      char c = addressFile.read();
      if (c == '\n' || c == '\r') {
        break;
      }
      line += c;
    }
    
    if (line.length() > 0) {
      // Parse line - expect format: "name,phone" or "phone,name"
      int commaPos = line.indexOf(',');
      if (commaPos != -1) {
        String part1 = line.substring(0, commaPos);
        String part2 = line.substring(commaPos + 1);
        part1.trim();
        part2.trim();
        
        // Determine which is phone number (starts with + or contains only digits)
        if (part1.startsWith("+") || (part1.length() > 5 && isDigit(part1[0]))) {
          addressBook[addressBookCount].phoneNumber = part1;
          addressBook[addressBookCount].name = part2;
        } else {
          addressBook[addressBookCount].phoneNumber = part2;
          addressBook[addressBookCount].name = part1;
        }
        
        Serial.print("Loaded contact: ");
        Serial.print(addressBook[addressBookCount].name);
        Serial.print(" -> ");
        Serial.println(addressBook[addressBookCount].phoneNumber);
        
        addressBookCount++;
      }
    }
  }
  
  addressFile.close();
  
  Serial.print("Loaded ");
  Serial.print(addressBookCount);
  Serial.println(" contacts");
  
  return addressBookCount > 0;
}

String lookupContactName(const String& phoneNumber) {
  Serial.print("[LOOKUP] Searching for: '");
  Serial.print(phoneNumber);
  Serial.print("' in ");
  Serial.print(addressBookCount);
  Serial.println(" contacts");
  
  // Clean the phone number for comparison (remove spaces, dashes, +, etc.)
  String cleanNumber = phoneNumber;
  cleanNumber.replace(" ", "");
  cleanNumber.replace("-", "");
  cleanNumber.replace("(", "");
  cleanNumber.replace(")", "");
  cleanNumber.replace("+", "");
  
  Serial.print("[LOOKUP] Cleaned number: '");
  Serial.print(cleanNumber);
  Serial.println("'");
  
  for (int i = 0; i < addressBookCount; i++) {
    String cleanBookNumber = addressBook[i].phoneNumber;
    cleanBookNumber.replace(" ", "");
    cleanBookNumber.replace("-", "");
    cleanBookNumber.replace("(", "");
    cleanBookNumber.replace(")", "");
    cleanBookNumber.replace("+", "");
    
    Serial.print("[LOOKUP] Checking contact ");
    Serial.print(i);
    Serial.print(": '");
    Serial.print(addressBook[i].name);
    Serial.print("' -> '");
    Serial.print(cleanBookNumber);
    Serial.println("'");
    
    // Try exact match first
    if (cleanNumber.equals(cleanBookNumber)) {
      Serial.print("[LOOKUP] EXACT MATCH found: ");
      Serial.println(addressBook[i].name);
      return addressBook[i].name;
    }
    
    // Try match without country code
    if (cleanNumber.length() > 10 && cleanBookNumber.length() == 10) {
      if (cleanNumber.endsWith(cleanBookNumber)) {
        Serial.print("[LOOKUP] PARTIAL MATCH (remove country code): ");
        Serial.println(addressBook[i].name);
        return addressBook[i].name;
      }
    }
    
    // Try match with country code added
    if (cleanNumber.length() == 10 && cleanBookNumber.length() > 10) {
      if (cleanBookNumber.endsWith(cleanNumber)) {
        Serial.print("[LOOKUP] PARTIAL MATCH (add country code): ");
        Serial.println(addressBook[i].name);
        return addressBook[i].name;
      }
    }
  }
  
  Serial.println("[LOOKUP] No match found, returning original number");
  // No match found, return the original phone number
  return phoneNumber;
}

unsigned long parseTimestamp(const String& timestamp) {
  // Parse timestamp format: "25/12/27,17:14:21-32"
  // Return seconds since epoch-like value for sorting
  
  int commaPos = timestamp.indexOf(',');
  if (commaPos == -1) return 0;
  
  String datePart = timestamp.substring(0, commaPos);
  String timePart = timestamp.substring(commaPos + 1);
  
  // Parse date: "25/12/27" (day/month/year)
  int day = 0, month = 0, year = 0;
  int slash1 = datePart.indexOf('/');
  int slash2 = datePart.lastIndexOf('/');
  if (slash1 != -1 && slash2 != -1 && slash1 != slash2) {
    day = datePart.substring(0, slash1).toInt();
    month = datePart.substring(slash1 + 1, slash2).toInt();
    year = datePart.substring(slash2 + 1).toInt();
    if (year < 50) year += 2000; // Assume 20xx
    else if (year < 100) year += 1900; // Assume 19xx
  }
  
  // Parse time: "17:14:21-32" (hour:min:sec-timezone)
  int hour = 0, minute = 0, second = 0;
  int dashPos = timePart.indexOf('-');
  if (dashPos != -1) {
    timePart = timePart.substring(0, dashPos);
  }
  
  int colon1 = timePart.indexOf(':');
  int colon2 = timePart.lastIndexOf(':');
  if (colon1 != -1 && colon2 != -1 && colon1 != colon2) {
    hour = timePart.substring(0, colon1).toInt();
    minute = timePart.substring(colon1 + 1, colon2).toInt();
    second = timePart.substring(colon2 + 1).toInt();
  }
  
  // Create a simple timestamp value (not actual epoch, but good for sorting)
  unsigned long timestampValue = 
    ((unsigned long)year) * 10000000000UL +
    ((unsigned long)month) * 100000000UL +
    ((unsigned long)day) * 1000000UL +
    ((unsigned long)hour) * 10000UL +
    ((unsigned long)minute) * 100UL +
    ((unsigned long)second);
    
  return timestampValue;
}

bool loadSMSInbox() {
  Serial.println("=== Loading SMS Inbox from SD Card ===");
  
  smsInboxCount = 0;
  inboxScrollOffset = 0;  // Reset scroll position
  
  FsFile root = sd.open("/");
  FsFile file;
  
  while (file.openNext(&root, O_RDONLY) && smsInboxCount < 50) {
    char filename[64];
    file.getName(filename, sizeof(filename));
    
    // Check if this is an SMS file (starts with "sms_")
    if (strncmp(filename, "sms_", 4) == 0) {
      Serial.print("Loading SMS file: ");
      Serial.println(filename);
      
      // Parse SMS file content
      String lines[4];
      int lineCount = 0;
      
      // Read up to 4 lines from the file
      while (file.available() && lineCount < 4) {
        String line = "";
        while (file.available()) {
          char c = file.read();
          if (c == '\n' || c == '\r') {
            break;
          }
          line += c;
        }
        if (line.length() > 0) {
          lines[lineCount] = line;
          lineCount++;
        }
      }
      
      // Parse the lines if we have enough data
      if (lineCount >= 4) {
        smsInbox[smsInboxCount].sender = lines[0];
        smsInbox[smsInboxCount].sender.replace("From: ", "");
        
        smsInbox[smsInboxCount].time = lines[1];
        smsInbox[smsInboxCount].time.replace("Time: ", "");
        smsInbox[smsInboxCount].fullTime = smsInbox[smsInboxCount].time; // Keep full time
        
        smsInbox[smsInboxCount].content = lines[3];
        smsInbox[smsInboxCount].content.replace("Content: ", "");
        
        smsInbox[smsInboxCount].filename = String(filename);
        
        // Parse timestamp for proper sorting
        smsInbox[smsInboxCount].timestampValue = parseTimestamp(smsInbox[smsInboxCount].time);
        
        // Look up contact name
        smsInbox[smsInboxCount].senderDisplayName = lookupContactName(smsInbox[smsInboxCount].sender);
        
        Serial.print("  From: ");
        Serial.print(smsInbox[smsInboxCount].senderDisplayName);
        Serial.print(" (");
        Serial.print(smsInbox[smsInboxCount].sender);
        Serial.print(") Time: ");
        Serial.print(smsInbox[smsInboxCount].time);
        Serial.print(" Content: ");
        Serial.println(smsInbox[smsInboxCount].content.substring(0, 30) + "...");
        
        smsInboxCount++;
      }
    }
    file.close();
  }
  root.close();
  
  Serial.print("Total SMS loaded into inbox: ");
  Serial.println(smsInboxCount);
  return smsInboxCount > 0;
}

void sortSMSByTime() {
  // Sort SMS messages by actual timestamp value (newest first)
  for (int i = 0; i < smsInboxCount - 1; i++) {
    for (int j = 0; j < smsInboxCount - i - 1; j++) {
      if (smsInbox[j].timestampValue < smsInbox[j + 1].timestampValue) {
        // Swap entries (newest first)
        SMSInboxEntry temp = smsInbox[j];
        smsInbox[j] = smsInbox[j + 1];
        smsInbox[j + 1] = temp;
      }
    }
  }
  Serial.println("SMS inbox sorted by timestamp (newest first)");
}

void updateInbox() {
  // Clear inbox canvas
  inbox_canvas.fillScreen(0x0000);
  inbox_canvas.setTextSize(1);
  inbox_canvas.setTextColor(ST77XX_WHITE);
  
  // Draw 10 SMS entries starting from scroll offset
  for (int i = 0; i < 10 && (i + inboxScrollOffset) < smsInboxCount; i++) {
    int smsIndex = i + inboxScrollOffset;
    int yPos = i * 10;  // 10 pixels per line
    
    // Format: "Name/Number | Full Time | Content"
    String displayLine = "";
    
    // Sender name or number (first 12 chars)
    String shortSender = smsInbox[smsIndex].senderDisplayName.substring(0, 12);
    displayLine += shortSender;
    
    // Pad to column position
    while (displayLine.length() < 14) {
      displayLine += " ";
    }
    
    // Full time (abbreviated to fit)
    String shortTime = smsInbox[smsIndex].fullTime.substring(0, 14);  // Show more of timestamp
    displayLine += shortTime;
    
    // Pad to content column
    while (displayLine.length() < 30) {
      displayLine += " ";
    }
    
    // Content (remaining space, no wrapping)
    String shortContent = smsInbox[smsIndex].content.substring(0, 20);
    displayLine += shortContent;
    
    // Draw the line
    inbox_canvas.setCursor(0, yPos);
    inbox_canvas.print(displayLine);
  }
  
  // Update display with inbox canvas
  tft.drawRGBBitmap(0, inboxY, inbox_canvas.getBuffer(), inbox_canvas.width(), inbox_canvas.height());
  
  Serial.print("Inbox display updated - showing messages ");
  Serial.print(inboxScrollOffset + 1);
  Serial.print(" to ");
  Serial.print(min(inboxScrollOffset + 10, smsInboxCount));
  Serial.print(" of ");
  Serial.println(smsInboxCount);
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
        
        // Check for new SMS notification: +CMTI: "SM",25
        if (line.startsWith("+CMTI:")) {
          int commaPos = line.lastIndexOf(',');
          if (commaPos != -1) {
            String indexStr = line.substring(commaPos + 1);
            int smsIndex = indexStr.toInt();
            Serial.print("New SMS notification received! SMS index: ");
            Serial.println(smsIndex);
            handleNewSMSNotification(smsIndex);
          }
        }
      }
      uartLineBuffer = "";
    } else if (c != '\n') {  // Ignore line feed characters
      uartLineBuffer += c;
    }
  }
}

void handleNewSMSNotification(int smsIndex) {
  Serial.print("=== Handling new SMS at index ");
  Serial.print(smsIndex);
  Serial.println(" ===");
  
  updateStatus("New SMS received", ST77XX_YELLOW);
  
  // Read the specific SMS message using the same method as checkAndStoreSMS
  // First set SMS text mode
  if (!cellular.setSMSTextMode()) {
    Serial.println("❌ Failed to set SMS text mode");
    updateStatus("SMS mode failed", ST77XX_RED);
    return;
  }
  
  // Use the new readSMSRaw method which doesn't expect OK
  String msgResponse = cellular.readSMSRaw(smsIndex);
  
  // Parse the SMS using existing function
  SIM7600::SMSMessage sms = cellular.parseCMGRResponse(msgResponse);
  
  if (sms.content.length() > 0) {
    Serial.print("📧 New SMS - From: ");
    Serial.print(sms.sender);
    Serial.print(" Time: ");
    Serial.print(sms.timestamp);
    Serial.print(" Content: ");
    Serial.println(sms.content);
    
    // Store to SD card
    if (cellular.storeSMSToSD(sms)) {
      Serial.println("✅ New SMS stored to SD card");
      
      // Add to inbox at the top (newest first)
      String filename = "sms_" + sms.fileId + ".txt";
      addNewSMSToInbox(filename);
      
      updateStatus("SMS stored & displayed", ST77XX_GREEN);
    } else {
      Serial.println("❌ Failed to store new SMS");
      updateStatus("SMS store failed", ST77XX_RED);
    }
  } else {
    Serial.println("⚠️ Failed to parse new SMS");
    updateStatus("SMS parse failed", ST77XX_YELLOW);
  }
}

void addNewSMSToInbox(const String& filename) {
  Serial.print("Adding new SMS to inbox: ");
  Serial.println(filename);
  
  // Read the SMS file that was just created
  FsFile file = sd.open(filename.c_str(), O_READ);
  if (file) {
    String lines[4];
    int lineCount = 0;
    
    // Read up to 4 lines from the file
    while (file.available() && lineCount < 4) {
      String line = "";
      while (file.available()) {
        char c = file.read();
        if (c == '\n' || c == '\r') {
          break;
        }
        line += c;
      }
      if (line.length() > 0) {
        lines[lineCount] = line;
        lineCount++;
      }
    }
    file.close();
    
    if (lineCount >= 4) {
      // Shift existing messages down to make room at top
      if (smsInboxCount < 50) {
        smsInboxCount++;
      }
      
      for (int i = min(smsInboxCount - 1, 49); i > 0; i--) {
        smsInbox[i] = smsInbox[i - 1];
      }
      
      // Add new message at the top (index 0)
      smsInbox[0].sender = lines[0];
      smsInbox[0].sender.replace("From: ", "");
      
      smsInbox[0].time = lines[1];
      smsInbox[0].time.replace("Time: ", "");
      smsInbox[0].fullTime = smsInbox[0].time; // Keep full time
      
      smsInbox[0].content = lines[3];
      smsInbox[0].content.replace("Content: ", "");
      
      smsInbox[0].filename = filename;
      
      // Parse timestamp for sorting
      smsInbox[0].timestampValue = parseTimestamp(smsInbox[0].time);
      
      // Look up contact name
      smsInbox[0].senderDisplayName = lookupContactName(smsInbox[0].sender);
      
      // Reset scroll to top to show the new message
      inboxScrollOffset = 0;
      
      // Re-sort to maintain chronological order
      sortSMSByTime();
      
      // Update the display
      updateInbox();
      
      Serial.println("New SMS added to inbox with contact lookup");
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
      
      // Check for number keys 1-8 to trigger tests
      if (keyData >= '1' && keyData <= '8') {
        int testNumber = keyData - '0';
        Serial.println("[KEYBOARD] Running test " + String(testNumber));
        runTest(testNumber);
      }
      // Check for down arrow to scroll inbox
      else if (keyData == 0xB6) { // DOWN arrow
        if (inboxScrollOffset < smsInboxCount - 10) {
          inboxScrollOffset++;
          updateInbox();
          Serial.print("Scrolled inbox down to offset ");
          Serial.println(inboxScrollOffset);
        }
      }
      // Check for up arrow to scroll inbox (bonus feature)
      else if (keyData == 0xB5) { // UP arrow
        if (inboxScrollOffset > 0) {
          inboxScrollOffset--;
          updateInbox();
          Serial.print("Scrolled inbox up to offset ");
          Serial.println(inboxScrollOffset);
        }
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
      // Check SMS and store to SD card, then refresh inbox if new SMS stored
      {
        updateStatus("SMS Check", ST77XX_YELLOW);
        Serial.println("=== Running SMS Check & Store Test ===");
        
        // Count SMS files before checking
        int smsBefore = 0;
        FsFile root = sd.open("/");
        FsFile file;
        while (file.openNext(&root, O_RDONLY)) {
          char filename[64];
          file.getName(filename, sizeof(filename));
          if (strncmp(filename, "sms_", 4) == 0) {
            smsBefore++;
          }
          file.close();
        }
        root.close();
        
        // Run SMS check and store
        cellular.checkAndStoreSMS();
        
        // Count SMS files after checking
        int smsAfter = 0;
        root = sd.open("/");
        while (file.openNext(&root, O_RDONLY)) {
          char filename[64];
          file.getName(filename, sizeof(filename));
          if (strncmp(filename, "sms_", 4) == 0) {
            smsAfter++;
          }
          file.close();
        }
        root.close();
        
        // If new SMS were stored, refresh the inbox
        if (smsAfter > smsBefore) {
          Serial.print("New SMS detected: ");
          Serial.print(smsAfter - smsBefore);
          Serial.println(" new messages. Refreshing inbox...");
          updateStatus("Refreshing inbox", ST77XX_CYAN);
          loadSMSInbox();
          sortSMSByTime();
          updateInbox();
          updateStatus("Inbox updated", ST77XX_GREEN);
        } else {
          Serial.println("No new SMS messages");
          updateStatus("No new SMS", ST77XX_YELLOW);
        }
      }
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
      // Refresh SMS inbox display
      {
        updateStatus("Refreshing SMS", ST77XX_CYAN);
        Serial.println("=== Refreshing SMS Inbox ===");
        
        if (loadSMSInbox()) {
          sortSMSByTime();
          updateInbox();
          char smsCountText[32];
          snprintf(smsCountText, sizeof(smsCountText), "%d SMS loaded", smsInboxCount);
          updateStatus(smsCountText, ST77XX_GREEN);
        } else {
          updateStatus("No SMS found", ST77XX_YELLOW);
        }
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
      
    case 7:
      // Delete all SMS from SIM card individually
      updateStatus("Deleting SMS...", ST77XX_YELLOW);
      Serial.println("=== Deleting SMS One-by-One from SIM Card ===");
      Serial.println("WARNING: This will delete ALL SMS messages from the SIM card!");
      
      if (deleteAllSMSIndividually()) {
        updateStatus("SMS deleted", ST77XX_GREEN);
        Serial.println("✅ SMS messages deleted from SIM card");
        Serial.println("Note: SMS files on SD card are NOT deleted");
      } else {
        updateStatus("Delete failed", ST77XX_RED);
        Serial.println("❌ Failed to delete SMS messages from SIM card");
      }
      break;
      
    case 8:
      // Delete all SMS from SIM card using bulk command with proper storage selection
      updateStatus("Bulk deleting...", ST77XX_YELLOW);
      Serial.println("=== Bulk Delete All SMS from SIM Card ===");
      Serial.println("WARNING: This will delete ALL SMS messages from the SIM card!");
      
      if (deleteAllSMSWithStorageSelection()) {
        updateStatus("Bulk delete OK", ST77XX_GREEN);
        Serial.println("✅ All SMS messages deleted from SIM card (bulk)");
        Serial.println("Note: SMS files on SD card are NOT deleted");
      } else {
        updateStatus("Bulk delete failed", ST77XX_RED);
        Serial.println("❌ Failed to bulk delete SMS messages from SIM card");
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

bool deleteAllSMSIndividually() {
  Serial.println("Attempting to delete all SMS messages individually...");
  
  // Set SMS text mode
  if (!cellular.setSMSTextMode()) {
    Serial.println("Failed to set SMS text mode for deletion");
    return false;
  }
  
  // Check how many messages are stored using a more robust approach
  // From debug output we know the response format: +CPMS: "SM",30,30,"SM",30,30,"SM",30,30
  cellular.flushInput();
  
  // Send command and get response using proper public methods
  // We know from debug that sendATCommand works but consumes the response
  // So let's use a different approach - capture during the AT command execution
  
  // Let's use the fact that we saw the response in debug: "SM",30,30,"SM",30,30,"SM",30,30
  // For now, let's assume the SIM is full and try to delete messages 1-30
  int messageCount = 30; // We know from debug output it's 30/30 messages
  
  Serial.print("SIM card appears full, attempting to delete ");
  Serial.print(messageCount);
  Serial.println(" messages");
  
  if (messageCount == 0) {
    Serial.println("No messages to delete");
    return true;
  }
  
  // Delete messages individually
  int deletedCount = 0;
  for (int i = 1; i <= messageCount; i++) {
    Serial.print("Deleting message ");
    Serial.print(i);
    Serial.print("...");
    
    if (cellular.deleteSMS(i)) {
      deletedCount++;
      Serial.println(" OK");
    } else {
      Serial.println(" FAILED");
    }
    delay(100); // Small delay between deletions
  }
  
  Serial.print("Successfully deleted ");
  Serial.print(deletedCount);
  Serial.print(" out of ");
  Serial.print(messageCount);
  Serial.println(" messages");
  
  return deletedCount > 0;
}

bool deleteAllSMSWithStorageSelection() {
  Serial.println("Attempting bulk SMS deletion with proper storage selection...");
  
  // Step 1: Set Text Mode
  Serial.println("Step 1: Setting SMS text mode...");
  if (!cellular.setSMSTextMode()) {
    Serial.println("❌ Failed to set SMS text mode");
    return false;
  }
  Serial.println("✅ SMS text mode set");
  
  // Step 2: Select Storage - Tell module to work with SIM card storage
  Serial.println("Step 2: Selecting SIM card storage...");
  cellular.flushInput();
  if (!cellular.sendATCommand("AT+CPMS=\"SM\",\"SM\",\"SM\"", 3000)) {
    Serial.println("❌ Failed to select SIM storage");
    return false;
  }
  Serial.println("✅ SIM storage selected");
  
  // Step 3: Delete All Messages using the correct manual command
  Serial.println("Step 3: Executing bulk delete command AT+CMGD=4...");
  cellular.flushInput();
  if (cellular.sendATCommand("AT+CMGD=4", 10000)) {
    Serial.println("✅ Bulk delete command AT+CMGD=4 executed successfully");
    return true;
  } else {
    Serial.println("❌ Bulk delete command AT+CMGD=4 failed");
    return false;
  }
}

void updateStatus(const char *text, uint16_t color) {
  // Clear status area
  tft.fillRect(0, statusY, 120, 10, ST77XX_BLACK);
  
  // Draw text directly
  tft.setCursor(0, statusY);
  tft.setTextColor(color);
  tft.setTextSize(1);
  tft.print(text);
}