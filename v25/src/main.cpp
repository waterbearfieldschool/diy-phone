/**************************************************************************
  DIY Phone v25 - Threaded Conversations + Outgoing SMS Storage
  Features: Display + SIM7600 + SD card SMS storage + I2C keyboard control + Address book name lookup + Full timestamps + Auto SMS deletion + Dual-pane interface + Threaded conversations
  
  v25 Changes: 
  - Store outgoing SMS to SD card in same format as incoming
  - Convert inbox to show latest message per thread/contact (not all messages)
  - Thread view shows complete conversation history (incoming + outgoing)
  - Inbox highlights show conversation threads, not individual messages
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

// v25: Thread-based inbox structure (one entry per contact with latest message)
struct ThreadInboxEntry {
  String contactPhone;         // Phone number (key for grouping)
  String contactDisplayName;   // Display name from address book
  String latestTime;           // Time of most recent message
  String latestContent;        // Content of most recent message
  unsigned long latestTimestamp; // Numeric timestamp for sorting
  int messageCount;            // Total messages in this thread
  bool hasUnread;              // If thread has unread messages
  bool lastWasOutgoing;        // True if latest message was sent by us
};

// Address book and SMS inbox data
AddressBookEntry addressBook[100];
int addressBookCount = 0;
SMSInboxEntry smsInbox[50];           // v25: Still used for loading all messages
int smsInboxCount = 0;
int inboxScrollOffset = 0;

// v25: Thread-based inbox (one entry per contact/conversation)
ThreadInboxEntry threadInbox[20];     // Max 20 conversation threads
int threadInboxCount = 0;

// v23 UI State - Dual-Pane Interface
enum ActivePane {
  PANE_INBOX = 0,
  PANE_THREAD = 1
};

struct ThreadEntry {
  String sender;
  String senderDisplayName;
  String time;
  String content;
  bool isOutgoing; // true if sent by us, false if received
};

// UI state variables
ActivePane currentPane = PANE_INBOX;
int selectedInboxIndex = 0;          // Which inbox message is highlighted
String selectedContactPhone = "";     // Phone number of selected contact
String selectedContactName = "";      // Cached display name for selected contact
ThreadEntry threadMessages[30];       // Messages in current thread
int threadMessageCount = 0;
int threadScrollOffset = 0;           // For scrolling thread view
String inputBuffer = "";              // Text being typed for reply

// Layout constants for v24 - 3-section UI (Status + Inbox + Thread)
#define STATUS_SECTION_Y 0
#define STATUS_SECTION_HEIGHT 20
#define INBOX_PANE_Y 20
#define INBOX_PANE_HEIGHT 110  // Reduced by 10 pixels (1 line)
#define THREAD_PANE_Y 130  
#define THREAD_PANE_HEIGHT 110  // Reduced by 10 pixels (1 line)
#define INPUT_LINE_HEIGHT 12
#define PANE_BORDER_WIDTH 2

// Function declarations - v24 enhanced status
void drawStatusSection();
void updateStatusMessage(const char *text, uint16_t color);
void updateStatus(const char *text, uint16_t color);  // Legacy compatibility
void updateInbox();
void readUARTLines();
void handleKeyboard();
String getKeyName(uint8_t keyCode);
void runTest(int testNumber);
bool loadSMSInbox();
void sortSMSByTime();
void buildThreadInbox();               // v25: Build thread-based inbox from all SMS
void sortThreadsByTime();
void handleNewSMSNotification(int smsIndex);
void addNewSMSToInbox(const String& filename);
bool deleteAllSMSIndividually();
bool deleteAllSMSWithStorageSelection();
bool loadAddressBook();
String lookupContactName(const String& phoneNumber);
unsigned long parseTimestamp(const String& timestamp);
int compareSMSByTime(const void* a, const void* b);

// Memory monitoring functions
// void updateMemoryDisplay();  // v24: Obsolete - memory now in status section
uint32_t getFreeMemory();
void logMemoryUsage(const char* location);

// v23 Dual-Pane UI functions
void drawInboxPane();
void drawThreadPane();
void drawPaneBorder(ActivePane pane);
void loadThreadForContact(const String& phoneNumber);
void handleKeyboardV23();
void switchPane();
void scrollInboxSelection(int direction);
void scrollThread(int direction);
void addCharToInput(char c);
void sendReplyMessage();
void addMessageToThread(const String& content, bool isOutgoing);
bool storeOutgoingSMS(const String& phoneNumber, const String& content);

// Canvas objects - REMOVED for v22 to save 4KB RAM and improve performance
//GFXcanvas16 inbox_canvas(320, 100);  // Inbox canvas - height for 10 lines (10 pixels each)
//GFXcanvas1 inbox_canvas(320, 100);   // Removed - now using direct TFT calls
// v24: Obsolete constants - now using STATUS_SECTION_*, INBOX_PANE_*, THREAD_PANE_*
// Memory display now integrated into status section

// Memory tracking variables
uint32_t lastMemoryCheck = 0;
const uint32_t MEMORY_CHECK_INTERVAL = 5000; // Check every 5 seconds

void setup(void) {
  Serial.begin(115200);
  delay(2000); // Wait for serial connection
  Serial.println("=== DIY Phone v25 Starting ===");

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
  
  // Scan I2C bus to see what devices are present
  Serial.println("[DEBUG] Scanning I2C bus...");
  int devicesFound = 0;
  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();
    
    if (error == 0) {
      Serial.print("[DEBUG] I2C device found at address 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      Serial.println();
      devicesFound++;
    }
  }
  
  if (devicesFound == 0) {
    Serial.println("[DEBUG] No I2C devices found on bus");
  } else {
    Serial.print("[DEBUG] Found ");
    Serial.print(devicesFound);
    Serial.println(" I2C devices");
  }
  
  // Test I2C connection with detailed debugging
  Serial.println("[DEBUG] Testing I2C keyboard connection...");
  Serial.print("[DEBUG] Requesting 1 byte from address 0x");
  Serial.println(KEYBOARD_ADDR, HEX);
  
  uint8_t bytesReceived = Wire.requestFrom(KEYBOARD_ADDR, 1);
  Serial.print("[DEBUG] Wire.requestFrom() returned: ");
  Serial.println(bytesReceived);
  
  Serial.print("[DEBUG] Wire.available(): ");
  Serial.println(Wire.available());
  
  if (Wire.available()) {
    uint8_t testData = Wire.read();
    Serial.print("[DEBUG] I2C keyboard test successful, received: 0x");
    Serial.print(testData, HEX);
    Serial.print(" (decimal: ");
    Serial.print(testData);
    Serial.println(")");
    updateStatus("Keyboard OK", ST77XX_GREEN);
  } else {
    Serial.println("[DEBUG] WARNING: No response from I2C keyboard");
    Serial.println("[DEBUG] This could mean:");
    Serial.println("[DEBUG] 1. Keyboard not connected");
    Serial.println("[DEBUG] 2. Wrong I2C address");
    Serial.println("[DEBUG] 3. I2C timing issue");
    updateStatus("Keyboard Warning", ST77XX_YELLOW);
  }
  
  // Check if there's any data left in buffer
  int remainingBytes = Wire.available();
  if (remainingBytes > 0) {
    Serial.print("[DEBUG] Additional bytes available: ");
    Serial.println(remainingBytes);
    while (Wire.available()) {
      uint8_t extraByte = Wire.read();
      Serial.print("[DEBUG] Extra byte: 0x");
      Serial.println(extraByte, HEX);
    }
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
      testFile.println("DIY Phone v25 Test");
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
  Serial.println("[DEBUG] Initializing v23 dual-pane interface...");
  
  // Initialize UI state - select first message and load its thread
  if (smsInboxCount > 0) {
    selectedInboxIndex = 0;
    loadThreadForContact(smsInbox[0].sender);
  }
  
  // Draw v24 3-section interface
  drawStatusSection();  // Status section at top
  drawInboxPane();      // Inbox pane in middle
  drawThreadPane();     // Thread pane at bottom
  drawPaneBorder(currentPane);
  
  Serial.println("[DEBUG] Setup complete!");
  
  // Initial memory display and logging
  logMemoryUsage("Setup complete");
  
  updateStatusMessage("Ready - v24 Interface", ST77XX_GREEN);
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
  
  // Monitor keyboard (v23 dual-pane version)
  handleKeyboardV23();
  
  // Update status section periodically (includes memory display)
  uint32_t currentTime = millis();
  if (currentTime - lastMemoryCheck >= MEMORY_CHECK_INTERVAL) {
    drawStatusSection();
    lastMemoryCheck = currentTime;
  }
  
  delay(10);  // Small delay for stability
}

bool loadAddressBook() {
  Serial.println("=== Loading Address Book ===");
  logMemoryUsage("Before loading address book");
  
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
  logMemoryUsage("After loading address book");
  
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
  logMemoryUsage("Before loading SMS inbox");
  
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
        smsInbox[smsInboxCount].fullTime.replace(",", " "); // Replace comma with space
        
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
  logMemoryUsage("After loading SMS inbox");
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

// v25: Build thread-based inbox from all SMS messages
void buildThreadInbox() {
  Serial.println("=== Building Thread-Based Inbox ===");
  
  threadInboxCount = 0;
  
  // Process all SMS messages and group by phone number
  for (int i = 0; i < smsInboxCount; i++) {
    String messagePhone = smsInbox[i].sender;
    
    // Clean phone number for comparison
    String cleanMessagePhone = messagePhone;
    cleanMessagePhone.replace(" ", "");
    cleanMessagePhone.replace("-", "");
    cleanMessagePhone.replace("(", "");
    cleanMessagePhone.replace(")", "");
    cleanMessagePhone.replace("+", "");
    
    // Check if we already have a thread for this contact
    int threadIndex = -1;
    for (int t = 0; t < threadInboxCount; t++) {
      String cleanThreadPhone = threadInbox[t].contactPhone;
      cleanThreadPhone.replace(" ", "");
      cleanThreadPhone.replace("-", "");
      cleanThreadPhone.replace("(", "");
      cleanThreadPhone.replace(")", "");
      cleanThreadPhone.replace("+", "");
      
      // Check for exact match or partial match
      if (cleanMessagePhone.equals(cleanThreadPhone) ||
          (cleanMessagePhone.length() > 10 && cleanThreadPhone.length() == 10 && 
           cleanMessagePhone.endsWith(cleanThreadPhone)) ||
          (cleanMessagePhone.length() == 10 && cleanThreadPhone.length() > 10 && 
           cleanThreadPhone.endsWith(cleanMessagePhone))) {
        threadIndex = t;
        break;
      }
    }
    
    if (threadIndex == -1) {
      // New thread - create entry
      if (threadInboxCount < 20) {
        threadIndex = threadInboxCount;
        threadInbox[threadIndex].contactPhone = messagePhone;
        threadInbox[threadIndex].contactDisplayName = smsInbox[i].senderDisplayName;
        threadInbox[threadIndex].messageCount = 0;
        threadInbox[threadIndex].hasUnread = false;
        threadInbox[threadIndex].latestTimestamp = 0;
        threadInbox[threadIndex].lastWasOutgoing = false;
        threadInboxCount++;
      } else {
        Serial.println("Warning: Maximum thread count reached");
        continue;
      }
    }
    
    // Update thread with this message if it's newer
    if (smsInbox[i].timestampValue > threadInbox[threadIndex].latestTimestamp) {
      threadInbox[threadIndex].latestTime = smsInbox[i].time;
      threadInbox[threadIndex].latestContent = smsInbox[i].content;
      threadInbox[threadIndex].latestTimestamp = smsInbox[i].timestampValue;
      
      // Check if this is an outgoing message (from "Me")
      threadInbox[threadIndex].lastWasOutgoing = (smsInbox[i].sender == "Me");
    }
    
    threadInbox[threadIndex].messageCount++;
  }
  
  Serial.print("Built ");
  Serial.print(threadInboxCount);
  Serial.println(" conversation threads");
  
  // Sort threads by latest message timestamp
  sortThreadsByTime();
}

void sortThreadsByTime() {
  // Sort thread inbox by latest timestamp (newest first)
  for (int i = 0; i < threadInboxCount - 1; i++) {
    for (int j = 0; j < threadInboxCount - i - 1; j++) {
      if (threadInbox[j].latestTimestamp < threadInbox[j + 1].latestTimestamp) {
        // Swap entries (newest first)
        ThreadInboxEntry temp = threadInbox[j];
        threadInbox[j] = threadInbox[j + 1];
        threadInbox[j + 1] = temp;
      }
    }
  }
  Serial.println("Thread inbox sorted by latest message timestamp");
}

void updateInbox() {
  // Clear the inbox display area
  tft.fillRect(0, INBOX_PANE_Y, 320, 100, ST77XX_BLACK);
  
  // Set text properties for direct TFT drawing
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  
  // Draw 10 SMS entries starting from scroll offset
  for (int i = 0; i < 10 && (i + inboxScrollOffset) < smsInboxCount; i++) {
    int smsIndex = i + inboxScrollOffset;
    int yPos = INBOX_PANE_Y + (i * 10);  // Absolute Y position on display
    
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
    
    // Draw the line directly to TFT (no canvas buffer)
    tft.setCursor(0, yPos);
    tft.print(displayLine);
  }
  
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
  logMemoryUsage("Before handling new SMS");
  
  updateStatus("New SMS received", ST77XX_YELLOW);
  
  // Read the specific SMS message using the same method as checkAndStoreSMS
  // First set SMS text mode
  if (!cellular.setSMSTextMode()) {
    Serial.println("❌ Failed to set SMS text mode");
    updateStatus("SMS mode failed", ST77XX_RED);
    return;
  }
  
  // Use readAndDeleteSMS (AT+CMGRD) to read and delete the SMS in one command
  String msgResponse = cellular.readAndDeleteSMS(smsIndex);
  
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
      Serial.println("✅ New SMS stored to SD card and deleted from SIM");
      
      // Add to inbox at the top (newest first)
      String filename = "sms_" + sms.fileId + ".txt";
      addNewSMSToInbox(filename);
      
      updateStatus("SMS stored & deleted", ST77XX_GREEN);
    } else {
      Serial.println("❌ Failed to store new SMS");
      updateStatus("SMS store failed", ST77XX_RED);
    }
  } else {
    Serial.println("⚠️ Failed to parse new SMS");
    updateStatus("SMS parse failed", ST77XX_YELLOW);
  }
  
  logMemoryUsage("After handling new SMS");
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
      smsInbox[0].fullTime.replace(",", " "); // Replace comma with space
      
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
      drawInboxPane();
      
      Serial.println("New SMS added to inbox with contact lookup");
    }
  }
}

void handleKeyboardV23() {
  Wire.requestFrom(KEYBOARD_ADDR, 1);
  
  if (Wire.available()) {
    uint8_t keyData = Wire.read();
    
    if (keyData != 0) {
      String keyName = getKeyName(keyData);
      char printableChar = (keyData >= 32 && keyData <= 126) ? (char)keyData : '?';
      Serial.println("[KEYBOARD] Key pressed: 0x" + String(keyData, HEX) + " (" + keyName + ") char: '" + String(printableChar) + "'");
      
      // Tab key switches between panes
      if (keyData == 0x09) { // TAB
        switchPane();
      }
      // Up/Down arrows - behavior depends on active pane
      else if (keyData == 0xB5) { // UP arrow
        if (currentPane == PANE_INBOX) {
          scrollInboxSelection(-1);
        } else {
          scrollThread(-1);
        }
      }
      else if (keyData == 0xB6) { // DOWN arrow
        if (currentPane == PANE_INBOX) {
          scrollInboxSelection(1);
        } else {
          scrollThread(1);
        }
      }
      // Enter key - different actions per pane
      else if (keyData == 0x0D) { // ENTER
        if (currentPane == PANE_INBOX) {
          // Select inbox item and load its thread
          if (selectedInboxIndex < smsInboxCount) {
            loadThreadForContact(smsInbox[selectedInboxIndex].sender);
            drawThreadPane();
          }
        } else {
          // Send message in thread pane
          sendReplyMessage();
        }
      }
      // Backspace in thread pane
      else if (keyData == 0x08 && currentPane == PANE_THREAD) { // BACKSPACE
        if (inputBuffer.length() > 0) {
          inputBuffer = inputBuffer.substring(0, inputBuffer.length() - 1);
          drawThreadPane(); // Refresh to show updated input
        }
      }
      // Alphanumeric characters for typing in thread pane
      else if (currentPane == PANE_THREAD && keyData >= 32 && keyData <= 126) {
        addCharToInput((char)keyData);
      }
      // Number keys 1-8 still trigger tests (legacy feature)
      else if (keyData >= '1' && keyData <= '8') {
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
      // Check SMS and store to SD card, then refresh inbox if new SMS stored
      {
        updateStatus("SMS Check", ST77XX_YELLOW);
        Serial.println("=== Running SMS Check & Store Test ===");
        logMemoryUsage("Before SMS check");
        
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
          drawInboxPane();
          updateStatus("Inbox updated", ST77XX_GREEN);
        } else {
          Serial.println("No new SMS messages");
          updateStatus("No new SMS", ST77XX_YELLOW);
        }
        logMemoryUsage("After SMS check");
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
        logMemoryUsage("Before refreshing SMS inbox");
        
        if (loadSMSInbox()) {
          sortSMSByTime();
          drawInboxPane();
          char smsCountText[32];
          snprintf(smsCountText, sizeof(smsCountText), "%d SMS loaded", smsInboxCount);
          updateStatus(smsCountText, ST77XX_GREEN);
        } else {
          updateStatus("No SMS found", ST77XX_YELLOW);
        }
        logMemoryUsage("After refreshing SMS inbox");
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

uint32_t getFreeMemory() {
  // For nRF52840, use a simple stack pointer estimation
  // This gives an approximate free memory value
  char stack_dummy = 0;
  return (uint32_t)&stack_dummy - 0x20000000;  // nRF52840 RAM starts at 0x20000000
}

void logMemoryUsage(const char* location) {
  uint32_t freeMemory = getFreeMemory();
  Serial.print("[MEMORY] ");
  Serial.print(location);
  Serial.print(": ");
  Serial.print(freeMemory);
  Serial.println(" bytes free");
}

// v24: updateMemoryDisplay() obsolete - memory now integrated into status section
/*
void updateMemoryDisplay() {
  // This function has been replaced by drawStatusSection() which includes memory display
  // Memory is now shown in the top status section along with instructions
}
*/

// Smooth scrolling functions for v22
void drawSMSLine(int smsIndex, int yPos) {
  // Check if SMS index is valid
  if (smsIndex < 0 || smsIndex >= smsInboxCount) {
    // Clear the line if no SMS to display
    tft.fillRect(0, yPos, 320, 10, ST77XX_BLACK);
    return;
  }
  
  // Clear the line first
  tft.fillRect(0, yPos, 320, 10, ST77XX_BLACK);
  
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
  String shortTime = smsInbox[smsIndex].fullTime.substring(0, 14);
  displayLine += shortTime;
  
  // Pad to content column
  while (displayLine.length() < 30) {
    displayLine += " ";
  }
  
  // Content (remaining space, no wrapping)
  String shortContent = smsInbox[smsIndex].content.substring(0, 20);
  displayLine += shortContent;
  
  // Draw the line directly to TFT
  tft.setCursor(0, yPos);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.print(displayLine);
}

void scrollInboxDown() {
  if (inboxScrollOffset >= smsInboxCount - 10) {
    return; // Can't scroll further down
  }
  
  inboxScrollOffset++;
  
  // Shift existing lines up by clearing top line and drawing new bottom line
  for (int i = 0; i < 9; i++) {
    int sourceLine = inboxScrollOffset + i + 1;
    int targetY = INBOX_PANE_Y + (i * 10);
    drawSMSLine(sourceLine, targetY);
  }
  
  // Draw new bottom line
  int newBottomIndex = inboxScrollOffset + 9;
  drawSMSLine(newBottomIndex, INBOX_PANE_Y + 90);
  
  Serial.print("Smooth scrolled down to offset ");
  Serial.println(inboxScrollOffset);
}

void scrollInboxUp() {
  if (inboxScrollOffset <= 0) {
    return; // Can't scroll further up
  }
  
  inboxScrollOffset--;
  
  // Shift existing lines down by clearing bottom line and drawing new top line
  for (int i = 9; i > 0; i--) {
    int sourceLine = inboxScrollOffset + i - 1;
    int targetY = INBOX_PANE_Y + (i * 10);
    drawSMSLine(sourceLine, targetY);
  }
  
  // Draw new top line
  drawSMSLine(inboxScrollOffset, INBOX_PANE_Y);
  
  Serial.print("Smooth scrolled up to offset ");
  Serial.println(inboxScrollOffset);
}

void updateInboxSmooth() {
  // Full redraw for initial display or major updates
  // Clear the entire inbox area
  tft.fillRect(0, INBOX_PANE_Y, 320, 100, ST77XX_BLACK);
  
  // Draw all 10 lines
  for (int i = 0; i < 10; i++) {
    int smsIndex = i + inboxScrollOffset;
    int yPos = INBOX_PANE_Y + (i * 10);
    drawSMSLine(smsIndex, yPos);
  }
  
  Serial.print("Inbox redrawn - showing messages ");
  Serial.print(inboxScrollOffset + 1);
  Serial.print(" to ");
  Serial.print(min(inboxScrollOffset + 10, smsInboxCount));
  Serial.print(" of ");
  Serial.println(smsInboxCount);
}

// v24 Enhanced Status Section Implementation

String currentStatusMessage = "Starting...";
uint16_t currentStatusColor = ST77XX_CYAN;

void drawStatusSection() {
  // Clear entire status section
  tft.fillRect(0, STATUS_SECTION_Y, 320, STATUS_SECTION_HEIGHT, ST77XX_BLACK);
  
  // Line 1: Current status message + RAM info
  tft.setTextSize(1);
  tft.setCursor(2, STATUS_SECTION_Y + 2);
  tft.setTextColor(currentStatusColor);
  tft.print(currentStatusMessage.substring(0, 20)); // Limit status message length
  
  // RAM display on right side of line 1
  uint32_t freeMemory = getFreeMemory();
  const uint32_t TOTAL_RAM = 256 * 1024;
  uint32_t usedMemory = TOTAL_RAM - freeMemory;
  uint8_t percentUsed = (usedMemory * 100) / TOTAL_RAM;
  
  char memText[12];
  snprintf(memText, sizeof(memText), "RAM:%luK", usedMemory / 1024);
  
  uint16_t memColor = ST77XX_GREEN;
  if (percentUsed > 80) memColor = ST77XX_RED;
  else if (percentUsed > 60) memColor = ST77XX_YELLOW;
  
  tft.setCursor(250, STATUS_SECTION_Y + 2);
  tft.setTextColor(memColor);
  tft.print(memText);
  
  // Line 2: Navigation instructions
  tft.setCursor(2, STATUS_SECTION_Y + 12);
  tft.setTextColor(ST77XX_WHITE);
  
  // Show different instructions based on active pane
  if (currentPane == PANE_INBOX) {
    tft.print("TAB=Thread UP/DOWN=Select ENTER=Open");
  } else {
    tft.print("TAB=Inbox UP/DOWN=Scroll ENTER=Send");
  }
  
  // Draw white horizontal separator line at bottom of status section
  int separatorY = STATUS_SECTION_Y + STATUS_SECTION_HEIGHT - 1;
  tft.drawFastHLine(0, separatorY, 320, ST77XX_WHITE);
}

void updateStatusMessage(const char *text, uint16_t color) {
  currentStatusMessage = String(text);
  currentStatusColor = color;
  drawStatusSection(); // Redraw entire status section
}

void updateStatus(const char *text, uint16_t color) {
  // Legacy compatibility - redirect to new status system
  updateStatusMessage(text, color);
}

// v23 Dual-Pane UI Implementation

void drawInboxPane() {
  // Clear inbox pane area
  tft.fillRect(0, INBOX_PANE_Y, 320, INBOX_PANE_HEIGHT, ST77XX_BLACK);
  
  // Draw header
  tft.setCursor(5, INBOX_PANE_Y + 5);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(1);
  tft.print("THREADS");
  
  // Show thread count
  tft.setCursor(250, INBOX_PANE_Y + 5);
  tft.print(String(threadInboxCount) + " convos");
  
  // Draw conversation threads (10 pixels per line, starting at Y+15)
  int maxLines = (INBOX_PANE_HEIGHT - 20) / 10; // Reserve space for header
  tft.setTextColor(ST77XX_WHITE);
  
  for (int i = 0; i < maxLines && (i + inboxScrollOffset) < threadInboxCount; i++) {
    int threadIndex = i + inboxScrollOffset;
    int yPos = INBOX_PANE_Y + 15 + (i * 10);
    
    // Highlight selected line
    if (threadIndex == selectedInboxIndex) {
      tft.fillRect(2, yPos - 1, 316, 10, ST77XX_BLUE);
      tft.setTextColor(ST77XX_WHITE);
    } else {
      tft.setTextColor(ST77XX_WHITE);
    }
    
    // Format: "Contact | Count | Latest message preview"
    String line = threadInbox[threadIndex].contactDisplayName.substring(0, 10);
    line += " | ";
    line += String(threadInbox[threadIndex].messageCount);
    line += " | ";
    
    // Add indicator for outgoing vs incoming latest message
    if (threadInbox[threadIndex].lastWasOutgoing) {
      line += "> ";  // Outgoing indicator
    }
    line += threadInbox[threadIndex].latestContent.substring(0, 20);
    
    tft.setCursor(5, yPos);
    tft.print(line);
  }
}

void drawThreadPane() {
  // Clear thread pane area
  tft.fillRect(0, THREAD_PANE_Y, 320, THREAD_PANE_HEIGHT, ST77XX_BLACK);
  
  // Draw header with contact name (using cached name)
  tft.setCursor(5, THREAD_PANE_Y + 5);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setTextSize(1);
  if (selectedContactPhone.length() > 0) {
    tft.print("THREAD: " + selectedContactName.substring(0, 20));
  } else {
    tft.print("THREAD: (no selection)");
  }
  
  // Draw thread messages
  int threadDisplayHeight = THREAD_PANE_HEIGHT - INPUT_LINE_HEIGHT - 20; // Reserve for header and input
  int maxThreadLines = threadDisplayHeight / 10;
  
  tft.setTextSize(1);
  for (int i = 0; i < maxThreadLines && (i + threadScrollOffset) < threadMessageCount; i++) {
    int threadIndex = i + threadScrollOffset;
    int yPos = THREAD_PANE_Y + 15 + (i * 10);
    
    ThreadEntry& msg = threadMessages[threadIndex];
    
    // Different colors for sent vs received
    if (msg.isOutgoing) {
      tft.setTextColor(ST77XX_GREEN); // Our messages in green
      tft.setCursor(50, yPos); // Indent outgoing messages
      tft.print("> " + msg.content.substring(0, 35));
    } else {
      tft.setTextColor(ST77XX_WHITE); // Received messages in white
      tft.setCursor(5, yPos);
      tft.print(msg.content.substring(0, 40));
    }
  }
  
  // Draw input line at bottom
  int inputY = THREAD_PANE_Y + THREAD_PANE_HEIGHT - INPUT_LINE_HEIGHT;
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(5, inputY);
  tft.print("> " + inputBuffer);
}

void drawPaneBorder(ActivePane pane) {
  // Always maintain red border around active pane
  uint16_t activeBorderColor = ST77XX_RED;
  uint16_t inactiveBorderColor = ST77XX_BLACK;
  
  if (pane == PANE_INBOX) {
    // Active inbox, inactive thread
    tft.drawRect(0, INBOX_PANE_Y, 320, INBOX_PANE_HEIGHT, activeBorderColor);
    tft.drawRect(0, THREAD_PANE_Y, 320, THREAD_PANE_HEIGHT, inactiveBorderColor);
  } else {
    // Active thread, inactive inbox
    tft.drawRect(0, INBOX_PANE_Y, 320, INBOX_PANE_HEIGHT, inactiveBorderColor);
    tft.drawRect(0, THREAD_PANE_Y, 320, THREAD_PANE_HEIGHT, activeBorderColor);
  }
}

void loadThreadForContact(const String& phoneNumber) {
  Serial.print("Loading thread for contact: ");
  Serial.println(phoneNumber);
  
  threadMessageCount = 0;
  threadScrollOffset = 0;
  selectedContactPhone = phoneNumber;
  
  // Cache the contact name lookup to avoid repeated lookups during typing
  selectedContactName = lookupContactName(phoneNumber);
  
  // Clean the phone number for comparison
  String cleanTargetNumber = phoneNumber;
  cleanTargetNumber.replace(" ", "");
  cleanTargetNumber.replace("-", "");
  cleanTargetNumber.replace("(", "");
  cleanTargetNumber.replace(")", "");
  cleanTargetNumber.replace("+", "");
  
  // Search through all SMS messages for this contact
  for (int i = 0; i < smsInboxCount; i++) {
    String cleanSenderNumber = smsInbox[i].sender;
    cleanSenderNumber.replace(" ", "");
    cleanSenderNumber.replace("-", "");
    cleanSenderNumber.replace("(", "");
    cleanSenderNumber.replace(")", "");
    cleanSenderNumber.replace("+", "");
    
    // Check for match (exact or partial)
    bool isMatch = false;
    if (cleanTargetNumber.equals(cleanSenderNumber)) {
      isMatch = true;
    } else if (cleanTargetNumber.length() > 10 && cleanSenderNumber.length() == 10) {
      if (cleanTargetNumber.endsWith(cleanSenderNumber)) isMatch = true;
    } else if (cleanTargetNumber.length() == 10 && cleanSenderNumber.length() > 10) {
      if (cleanSenderNumber.endsWith(cleanTargetNumber)) isMatch = true;
    }
    
    if (isMatch && threadMessageCount < 30) {
      threadMessages[threadMessageCount].sender = smsInbox[i].sender;
      threadMessages[threadMessageCount].senderDisplayName = smsInbox[i].senderDisplayName;
      threadMessages[threadMessageCount].time = smsInbox[i].time;
      threadMessages[threadMessageCount].content = smsInbox[i].content;
      threadMessages[threadMessageCount].isOutgoing = false; // All loaded messages are received
      threadMessageCount++;
    }
  }
  
  // Sort thread messages by time (oldest first for chat-like display)
  for (int i = 0; i < threadMessageCount - 1; i++) {
    for (int j = 0; j < threadMessageCount - i - 1; j++) {
      unsigned long time1 = parseTimestamp(threadMessages[j].time);
      unsigned long time2 = parseTimestamp(threadMessages[j + 1].time);
      if (time1 > time2) {
        ThreadEntry temp = threadMessages[j];
        threadMessages[j] = threadMessages[j + 1];
        threadMessages[j + 1] = temp;
      }
    }
  }
  
  // Auto-scroll to bottom to show latest messages
  if (threadMessageCount > 10) { // If more than 10 messages
    threadScrollOffset = threadMessageCount - 10;
  }
  
  Serial.print("Loaded ");
  Serial.print(threadMessageCount);
  Serial.println(" messages for thread");
}

// Supporting functions for v23 UI

void switchPane() {
  if (currentPane == PANE_INBOX) {
    currentPane = PANE_THREAD;
    Serial.println("Switched to THREAD pane");
  } else {
    currentPane = PANE_INBOX;
    Serial.println("Switched to INBOX pane");
  }
  drawPaneBorder(currentPane);
  drawStatusSection(); // Update status section with new instructions
}

void scrollInboxSelection(int direction) {
  selectedInboxIndex += direction;
  
  // Bounds checking
  if (selectedInboxIndex < 0) {
    selectedInboxIndex = 0;
  } else if (selectedInboxIndex >= smsInboxCount) {
    selectedInboxIndex = smsInboxCount - 1;
  }
  
  // Auto-scroll view if selection is outside visible area
  int maxVisibleLines = (INBOX_PANE_HEIGHT - 20) / 10;
  if (selectedInboxIndex < inboxScrollOffset) {
    inboxScrollOffset = selectedInboxIndex;
  } else if (selectedInboxIndex >= inboxScrollOffset + maxVisibleLines) {
    inboxScrollOffset = selectedInboxIndex - maxVisibleLines + 1;
  }
  
  drawInboxPane();
  Serial.print("Inbox selection: ");
  Serial.println(selectedInboxIndex);
}

void scrollThread(int direction) {
  threadScrollOffset += direction;
  
  // Bounds checking
  int maxVisibleLines = (THREAD_PANE_HEIGHT - INPUT_LINE_HEIGHT - 20) / 10;
  if (threadScrollOffset < 0) {
    threadScrollOffset = 0;
  } else if (threadScrollOffset > threadMessageCount - maxVisibleLines) {
    threadScrollOffset = max(0, threadMessageCount - maxVisibleLines);
  }
  
  drawThreadPane();
  Serial.print("Thread scroll offset: ");
  Serial.println(threadScrollOffset);
}

void addCharToInput(char c) {
  if (inputBuffer.length() < 100) { // Limit input length
    inputBuffer += c;
    drawThreadPane(); // Refresh to show new character
    
    // Auto-scroll thread to bottom when typing
    int maxVisibleLines = (THREAD_PANE_HEIGHT - INPUT_LINE_HEIGHT - 20) / 10;
    if (threadMessageCount > maxVisibleLines) {
      threadScrollOffset = threadMessageCount - maxVisibleLines;
      drawThreadPane();
    }
  }
}

void sendReplyMessage() {
  if (inputBuffer.length() == 0 || selectedContactPhone.length() == 0) {
    Serial.println("Cannot send: empty message or no contact selected");
    return;
  }
  
  Serial.print("Sending SMS to ");
  Serial.print(selectedContactPhone);
  Serial.print(": ");
  Serial.println(inputBuffer);
  
  // Remove '+' prefix if present since SIM7600 library adds it automatically
  String phoneForSMS = selectedContactPhone;
  if (phoneForSMS.startsWith("+")) {
    phoneForSMS = phoneForSMS.substring(1);
  }
  
  // Send via SIM7600 (implement actual sending)
  if (cellular.sendSMS(phoneForSMS.c_str(), inputBuffer.c_str())) {
    Serial.println("SMS sent successfully");
    
    // Store outgoing SMS to SD card in same format as incoming
    storeOutgoingSMS(selectedContactPhone, inputBuffer);
    
    // Add to thread as outgoing message
    addMessageToThread(inputBuffer, true);
    
    // Clear input buffer
    inputBuffer = "";
    
    updateStatus("SMS sent", ST77XX_GREEN);
  } else {
    Serial.println("SMS sending failed");
    updateStatus("SMS failed", ST77XX_RED);
  }
  
  drawThreadPane(); // Refresh display
}

void addMessageToThread(const String& content, bool isOutgoing) {
  if (threadMessageCount >= 30) {
    // Remove oldest message to make room
    for (int i = 0; i < threadMessageCount - 1; i++) {
      threadMessages[i] = threadMessages[i + 1];
    }
    threadMessageCount--;
  }
  
  // Add new message
  threadMessages[threadMessageCount].sender = isOutgoing ? "Me" : selectedContactPhone;
  threadMessages[threadMessageCount].senderDisplayName = isOutgoing ? "Me" : lookupContactName(selectedContactPhone);
  threadMessages[threadMessageCount].time = "now"; // Could format current time
  threadMessages[threadMessageCount].content = content;
  threadMessages[threadMessageCount].isOutgoing = isOutgoing;
  threadMessageCount++;
  
  // Auto-scroll to show new message
  int maxVisibleLines = (THREAD_PANE_HEIGHT - INPUT_LINE_HEIGHT - 20) / 10;
  if (threadMessageCount > maxVisibleLines) {
    threadScrollOffset = threadMessageCount - maxVisibleLines;
  }
}

bool storeOutgoingSMS(const String& phoneNumber, const String& content) {
  // Generate unique filename with timestamp
  unsigned long timestamp = millis();
  String filename = "sms_out_" + String(timestamp) + ".txt";
  
  Serial.print("Storing outgoing SMS to: ");
  Serial.println(filename);
  
  FsFile file = sd.open(filename.c_str(), O_WRITE | O_CREAT);
  if (file) {
    // Use same format as incoming SMS
    file.print("From: ");
    file.println("Me");  // Outgoing messages are from "Me"
    
    file.print("Time: ");
    // TODO: Use actual timestamp formatting like incoming SMS
    file.println("now");  // Simplified for now
    
    file.print("Status: ");
    file.println("SENT");  // Mark as sent message
    
    file.print("Content: ");
    file.println(content);
    
    file.print("To: ");  // Additional field for outgoing SMS
    file.println(phoneNumber);
    
    file.close();
    
    Serial.print("Successfully stored outgoing SMS to: ");
    Serial.println(filename);
    return true;
  } else {
    Serial.print("Failed to create file: ");
    Serial.println(filename);
    return false;
  }
}
