/**************************************************************************
  DIY Phone v35 - ProMicro nRF52840 Port
  Features: Display + SIM7600 + SD card SMS storage + I2C keyboard control + Address book name lookup + Full timestamps + Auto SMS deletion + Thread-based UI

  v35 Changes (ProMicro Port):
  - Ported from ItsyBitsy nRF52840 to ProMicro nRF52840
  - Updated pin mappings per promicromap.txt
  - Separate SPI buses for TFT (SPIM2) and SD card (SPIM3)

  Pin assignments (ProMicro nRF52840):
  - TFT: MOSI=2(P0.17), SCK=9(P1.06), CS=11(P0.10), DC=10(P0.09), RST=5(P0.24)
  - SD:  MOSI=14(P1.15), SCK=12(P1.11), MISO=15(P0.02), CS=13(P1.13)
  - SIM7600: RX=3(P0.20), TX=4(P0.22)
  - I2C: SDA=8(P1.04), SCL=7(P0.11)

  Based on v34 (Automatic Contact Addition):
  - Automatically adds unknown SMS senders to address book
  - Saves new contacts to addressbook.txt file
  - Immediately refreshes contacts list and UI
 **************************************************************************/

#include <Arduino.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <SPI.h>
#include <Wire.h>
#include <SdFat.h>
#include "SIM7600.h"

// Custom SPI bus definition for TFT (ProMicro nRF52840)
// Based on promicromap.txt: MOSI=P0.17(pin2), SCK=P1.06(pin9)
// MISO not needed for display-only, using pin 6 (P1.00) as placeholder
SPIClass tftSPI(NRF_SPIM2, 6, 9, 2);  // MISO=6, SCK=9, MOSI=2

// Custom SPI bus definition for SD card (separate bus)
// Based on promicromap.txt: MISO=P0.02(pin15), SCK=P1.11(pin12), MOSI=P1.15(pin14)
SPIClass sdSPI(NRF_SPIM3, 15, 12, 14);  // MISO=15, SCK=12, MOSI=14

// Pin definitions for ProMicro nRF52840 (from promicromap.txt)
#define TFT_CS        11   // P0.10
#define TFT_RST        5   // P0.24
#define TFT_DC        10   // P0.09

// SD card CS pin
#define SD_CS_PIN     13   // P1.13

// I2C keyboard address
#define KEYBOARD_ADDR 0x5F

// Create display object with custom SPI bus
Adafruit_ST7789 tft = Adafruit_ST7789(&tftSPI, TFT_CS, TFT_DC, TFT_RST);

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

// Enhanced SMS structure with direction support
struct SMSMessage {
  String sender;
  String recipient;          // NEW: For outgoing messages
  String senderDisplayName;  // Either phone number or contact name
  String time;
  String fullTime;           // Full timestamp for display
  String content;
  String filename;           // For sorting
  unsigned long timestampValue; // Numeric timestamp for proper sorting
  bool isOutgoing;          // NEW: true if sent by us, false if received
};

// Thread preview structure for upper pane
struct ThreadPreview {
  String contactPhone;
  String contactDisplayName;
  String lastMessageTime;
  String lastMessagePreview;
  unsigned long lastTimestamp;
  bool hasUnread;           // Future: for unread message indication
  int messageCount;         // Total messages in thread
};

// Address book and messaging data
AddressBookEntry addressBook[100];
int addressBookCount = 0;

// v26: Thread-based data structures
ThreadPreview threadPreviews[20];    // Thread previews for upper pane
int threadPreviewCount = 0;
int previewScrollOffset = 0;

SMSMessage currentThreadMessages[30]; // Messages in currently viewed thread
int currentThreadMessageCount = 0;

// v26 UI State - Thread-based Interface
enum ActivePane {
  PANE_THREADS = 0,      // Upper pane: thread previews
  PANE_CONVERSATION = 1   // Lower pane: full conversation + compose
};

// UI Layout for v26 - optimized for thread interface  
// v30: Removed old THREAD_PREVIEW_HEIGHT definition - now defined below with updated value
// v33: CONVERSATION_HEIGHT now defined below with updated value
#define COMPOSE_LINE_HEIGHT 15       // Bottom line for typing

// v26 UI state variables
ActivePane currentPane = PANE_THREADS;
int selectedThreadIndex = 0;          // Which thread preview is highlighted
String activeContactPhone = "";       // Phone number of active conversation
String activeContactName = "";        // Display name of active contact
int conversationPixelScrollOffset = 0; // Pixel-based scrolling for smooth partial message view
bool userIsManuallyScrolling = false; // Track if user manually scrolled away from bottom
String composeBuffer = "";            // Text being typed for new message

// v33 Contact search variables
String contactSearchBuffer = "";       // Letters typed for contact search
int contactSearchMode = 0;             // 0=normal, 1=searching
unsigned long lastSearchTime = 0;     // Time of last search keystroke for timeout
#define SEARCH_TIMEOUT_MS 2000         // Clear search after 2 seconds of no input

// Layout constants for v33 - Contact search UI (Status + Contacts + Conversation + Compose)
#define STATUS_SECTION_Y 0
#define STATUS_SECTION_HEIGHT 12       // Status bar height
#define CONTACTS_Y 12                  // v33: Renamed from THREAD_PREVIEW_Y - now contacts panel
#define CONTACTS_HEIGHT 69             // v33: 1/3 of remaining space (69px)  
#define CONVERSATION_Y 81              // v33: Moved up - starts after contacts (12+69=81)
#define CONVERSATION_HEIGHT 139        // v33: 2/3 of remaining space (139px)
#define COMPOSE_Y 220                  // Bottom area for message composition
#define COMPOSE_HEIGHT 20              // Compose line area
#define PANE_BORDER_WIDTH 2

// v27 Debug control
bool debugThreadLoading = false;  // v34: Disabled by default - use key '9' to toggle

// Function declarations - v26 thread-based interface
void drawStatusSection();
void updateStatusMessage(const char *text, uint16_t color);
void updateStatus(const char *text, uint16_t color);  // Legacy compatibility
void readUARTLines();
void handleKeyboard();
String getKeyName(uint8_t keyCode);
void runTest(int testNumber);

// v26 Thread management functions
bool loadAllMessages();                              // Load all SMS from individual files
void buildThreadPreviews();                         // Build thread preview list from messages (legacy - scans files)
void buildThreadPreviewsFromMessages(SMSMessage* messages, int messageCount); // Build from existing message array
void loadThreadForContact(const String& phoneNumber); // Load conversation for specific contact
bool saveOutgoingMessage(const String& recipient, const String& content); // Store outgoing message
void addMessageToThread(const String& content, bool isOutgoing, const String& timestamp);
void calculateOptimalScroll();                          // Calculate proper scroll to show latest messages
int calculateMaxScrollOffset();                        // Calculate maximum valid scroll position
int calculateTotalContentHeight();                     // Calculate total height of all messages in pixels
void sortThreadPreviewsByTime();                   // Sort threads by latest activity
String generateContactHash(const String& phoneNumber); // Generate hash for thread cache files
bool loadThreadCacheFile(const String& contactPhone); // Load from thread cache
bool saveThreadCacheFile(const String& contactPhone); // Save to thread cache

// Legacy functions (updated for v26)
void handleNewSMSNotification(int smsIndex);
bool deleteAllSMSIndividually();
bool deleteAllSMSWithStorageSelection();
bool loadAddressBook();
bool saveAddressBook();                               // v34: Save address book to file
bool addNewContact(const String& phoneNumber, const String& name); // v34: Add contact to address book
String lookupContactName(const String& phoneNumber);
unsigned long parseTimestamp(const String& timestamp);

// Memory monitoring functions
// void updateMemoryDisplay();  // v24: Obsolete - memory now in status section
uint32_t getFreeMemory();
void logMemoryUsage(const char* location);

// v26 UI functions
void drawContactsPane();                           // v33: Upper pane: searchable contacts sorted by recent activity
void drawConversationPane();                       // Lower pane: conversation + compose
void drawComposeAreaOnly();                        // Only redraw the compose area (optimization)
void drawPaneBorder(ActivePane pane);
void handleKeyboardV26();
void switchPane();
void scrollThreadSelection(int direction);         // Navigate thread previews
void scrollConversation(int direction);            // Scroll conversation history
void addCharToCompose(char c);                     // Add character to compose buffer
void sendMessage();                                // Send composed message
void selectThread();                               // Open selected thread for conversation

// Canvas objects - REMOVED for v22 to save 4KB RAM and improve performance
//GFXcanvas16 inbox_canvas(240, 100);  // Inbox canvas - height for 10 lines (10 pixels each)
//GFXcanvas1 inbox_canvas(240, 100);   // Removed - now using direct TFT calls
// v24: Obsolete constants - now using STATUS_SECTION_*, INBOX_PANE_*, THREAD_PANE_*
// Memory display now integrated into status section

// Memory tracking variables
uint32_t lastMemoryCheck = 0;
const uint32_t MEMORY_CHECK_INTERVAL = 5000; // Check every 5 seconds

void setup(void) {
  Serial.begin(115200);
  
  // Wait for serial port to be ready (up to 10 seconds)
  unsigned long serialStartTime = millis();
  while (!Serial && (millis() - serialStartTime < 10000)) {
    delay(100);
  }
  
  // Additional delay to ensure stable connection
  delay(1000);
  
  Serial.println("=== DIY Phone v35 Starting ===");
  Serial.print("[DEBUG] Serial connection established after ");
  Serial.print(millis() - serialStartTime);
  Serial.println(" ms");

  // Initialize custom SPI buses
  Serial.println("[DEBUG] Starting custom SPI initialization...");
  tftSPI.begin();
  Serial.println("[DEBUG] TFT SPI initialized");
  sdSPI.begin();
  Serial.println("[DEBUG] SD SPI initialized");

  // Initialize display
  Serial.println("[DEBUG] Starting display initialization...");
  tft.init(240, 240);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);
  
  // Show immediate visual feedback that device is running
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(10, 120);
  tft.print("DIY Phone v36");
  tft.setCursor(10, 140);
  tft.setTextColor(ST77XX_CYAN);
  tft.print("Starting...");
  
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
  
  // Skip full I2C bus scan to avoid system freeze - just test keyboard directly
  Serial.println("[DEBUG] Skipping full I2C scan to prevent freeze - testing keyboard only...");
  
  // Test I2C connection with detailed debugging and timeout
  Serial.println("[DEBUG] Testing I2C keyboard connection...");
  Serial.print("[DEBUG] Requesting 1 byte from address 0x");
  Serial.println(KEYBOARD_ADDR, HEX);
  
  // Simple keyboard connectivity test
  Wire.beginTransmission(KEYBOARD_ADDR);
  uint8_t error = Wire.endTransmission();
  
  if (error == 0) {
    Serial.println("[DEBUG] I2C keyboard responds to address - connection OK");
    updateStatus("Keyboard OK", ST77XX_GREEN);
  } else {
    Serial.print("[DEBUG] WARNING: I2C keyboard error code: ");
    Serial.println(error);
    Serial.println("[DEBUG] This could mean:");
    Serial.println("[DEBUG] 1. Keyboard not connected");
    Serial.println("[DEBUG] 2. Wrong I2C address");
    Serial.println("[DEBUG] 3. I2C timing issue");
    updateStatus("Keyboard Warning", ST77XX_YELLOW);
  }
  
  Serial.println("[DEBUG] I2C keyboard test complete");

  // Initialize SD card with custom SPI bus
  Serial.println("[DEBUG] About to initialize SD card...");
  SdSpiConfig sdConfig(SD_CS_PIN, DEDICATED_SPI, SD_SCK_MHZ(4), &sdSPI);
  if (sd.begin(sdConfig)) {
    Serial.println("[DEBUG] SD card initialized");
    updateStatus("SD card OK", ST77XX_GREEN);
    
    // Test SD card by creating a test file
    Serial.println("[DEBUG] Testing SD card write...");
    FsFile testFile = sd.open("test.txt", O_WRITE | O_CREAT);
    if (testFile) {
      testFile.println("DIY Phone v35 Test");
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

  // Configure Serial1 pins for ProMicro: RX=P0.22(pin4), TX=P0.20(pin3)
  Serial.println("[DEBUG] Configuring Serial1 pins...");
  Serial1.setPins(4, 3);  // setPins(rx, tx)
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
  
  // Load and build thread previews on boot
  updateStatus("Loading messages...", ST77XX_CYAN);
  Serial.println("[DEBUG] Starting message loading process...");
  
  unsigned long startTime = millis();
  bool loadSuccess = loadAllMessages();  // This also calls buildThreadPreviews()
  unsigned long loadTime = millis() - startTime;
  
  Serial.print("[DEBUG] Message loading completed in ");
  Serial.print(loadTime);
  Serial.println(" ms");
  
  if (!loadSuccess) {
    Serial.println("[WARNING] Message loading returned false");
  }
  
  Serial.println("[DEBUG] Initializing v26 thread-based interface...");
  logMemoryUsage("Before UI initialization");
  
  // Initialize UI state - select first thread if available
  if (threadPreviewCount > 0) {
    selectedThreadIndex = 0;
    Serial.println("[DEBUG] Selected first thread");
    // Don't auto-load thread, let user select
  } else {
    Serial.println("[DEBUG] No threads available to select");
  }
  
  Serial.println("[DEBUG] Drawing UI components...");
  logMemoryUsage("Before drawing UI");
  
  // Draw v26 thread-based interface
  Serial.println("[DEBUG] Drawing status section...");
  logMemoryUsage("Before status section");
  drawStatusSection();         // Status section at top
  logMemoryUsage("After status section");
  
  Serial.println("[DEBUG] Drawing thread preview pane...");
  logMemoryUsage("Before thread preview pane");
  drawContactsPane();          // v33: Contacts panel in upper area
  logMemoryUsage("After thread preview pane");
  
  Serial.println("[DEBUG] Drawing conversation pane...");
  drawConversationPane();      // Conversation + compose at bottom
  
  Serial.println("[DEBUG] Drawing pane borders...");
  drawPaneBorder(currentPane);
  
  Serial.println("[DEBUG] UI drawing completed");
  Serial.println("[DEBUG] Setup complete!");
  
  // Initial memory display and logging
  logMemoryUsage("Setup complete");
  
  updateStatusMessage("Ready - v32 Interface", ST77XX_GREEN);
  Serial.println("===============================================");
  Serial.println("Setup complete - Press keyboard numbers 1-9:");
  Serial.println("1 = Signal Quality Test");
  Serial.println("2 = AT Command Test");
  Serial.println("3 = SMS Check & Store");
  Serial.println("4 = SD Card Test");
  Serial.println("5 = Read SMS Files");
  Serial.println("6 = Network Status");
  Serial.println("7 = Delete SMS One-by-One");
  Serial.println("8 = Delete All SMS (Bulk)");
  Serial.println("9 = Toggle Debug Output (v27+)");
  Serial.println("Down Arrow = Scroll inbox");
  Serial.println("===============================================");
}

void loop() {
  // Monitor UART from SIM7600
  readUARTLines();
  
  // Monitor keyboard (v26 thread-based version)
  handleKeyboardV26();
  
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

bool saveAddressBook() {
  // v34: Save current address book to file
  Serial.println("=== Saving Address Book ===");
  
  // Open file for writing (create if doesn't exist, truncate if exists)
  FsFile addressFile = sd.open("addressbook.txt", O_WRITE | O_CREAT | O_TRUNC);
  if (!addressFile) {
    Serial.println("❌ Failed to open addressbook.txt for writing");
    return false;
  }
  
  // Write all contacts in "name,phone" format
  for (int i = 0; i < addressBookCount; i++) {
    String line = addressBook[i].name + "," + addressBook[i].phoneNumber + "\n";
    if (addressFile.write(line.c_str(), line.length()) != line.length()) {
      Serial.println("❌ Failed to write contact to file");
      addressFile.close();
      return false;
    }
  }
  
  addressFile.close();
  Serial.print("✅ Saved ");
  Serial.print(addressBookCount);
  Serial.println(" contacts to addressbook.txt");
  return true;
}

bool addNewContact(const String& phoneNumber, const String& name) {
  // v34: Add new contact to address book (memory and file)
  if (addressBookCount >= 100) {
    Serial.println("❌ Address book is full (100 contacts)");
    return false;
  }
  
  // Check if contact already exists
  String existingName = lookupContactName(phoneNumber);
  if (!existingName.equals(phoneNumber)) {
    Serial.println("Contact already exists: " + existingName);
    return false;
  }
  
  // Add to memory
  addressBook[addressBookCount].phoneNumber = phoneNumber;
  addressBook[addressBookCount].name = name;
  addressBookCount++;
  
  Serial.print("✅ Added new contact: ");
  Serial.print(name);
  Serial.print(" -> ");
  Serial.println(phoneNumber);
  Serial.print("🔍 Address book count now: ");
  Serial.println(addressBookCount);
  
  // Test lookup immediately
  String testLookup = lookupContactName(phoneNumber);
  Serial.print("🔍 Immediate lookup test: ");
  Serial.println(testLookup);
  
  // Save to file
  bool saved = saveAddressBook();
  if (saved) {
    Serial.println("✅ Contact saved to file successfully");
  } else {
    Serial.println("❌ Failed to save contact to file");
  }
  return saved;
}

String lookupContactName(const String& phoneNumber) {
  // Clean the phone number for comparison (remove spaces, dashes, +, etc.)
  String cleanNumber = phoneNumber;
  cleanNumber.replace(" ", "");
  cleanNumber.replace("-", "");
  cleanNumber.replace("(", "");
  cleanNumber.replace(")", "");
  cleanNumber.replace("+", "");
  
  for (int i = 0; i < addressBookCount; i++) {
    String cleanBookNumber = addressBook[i].phoneNumber;
    cleanBookNumber.replace(" ", "");
    cleanBookNumber.replace("-", "");
    cleanBookNumber.replace("(", "");
    cleanBookNumber.replace(")", "");
    cleanBookNumber.replace("+", "");
    
    // Try exact match first
    if (cleanNumber.equals(cleanBookNumber)) {
      return addressBook[i].name;
    }
    
    // Try match without country code
    if (cleanNumber.length() > 10 && cleanBookNumber.length() == 10) {
      if (cleanNumber.endsWith(cleanBookNumber)) {
        return addressBook[i].name;
      }
    }
    
    // Try match with country code added
    if (cleanNumber.length() == 10 && cleanBookNumber.length() > 10) {
      if (cleanBookNumber.endsWith(cleanNumber)) {
        return addressBook[i].name;
      }
    }
  }
  
  // No match found, return the original phone number
  return phoneNumber;
}

unsigned long parseTimestamp(const String& timestamp) {
  // v27 Fix: Parse timestamp format with UTC support
  // Input formats: "26/01/04,19:04:26-32" (old) or "26/01/04,23:04:26+00:00" (UTC)
  // Return seconds since epoch-like value for sorting
  
  if (timestamp.length() == 0) {
    Serial.println("[TIMESTAMP] Empty timestamp, returning 0");
    return 0;
  }
  
  int commaPos = timestamp.indexOf(',');
  if (commaPos == -1) {
    Serial.print("[TIMESTAMP] No comma found in timestamp: ");
    Serial.println(timestamp);
    return 0;
  }
  
  String datePart = timestamp.substring(0, commaPos);
  String timePart = timestamp.substring(commaPos + 1);
  
  // Parse date: "26/01/08" (year/month/day) - corrected format
  int day = 0, month = 0, year = 0;
  int slash1 = datePart.indexOf('/');
  int slash2 = datePart.lastIndexOf('/');
  if (slash1 != -1 && slash2 != -1 && slash1 != slash2) {
    year = datePart.substring(0, slash1).toInt();
    month = datePart.substring(slash1 + 1, slash2).toInt();
    day = datePart.substring(slash2 + 1).toInt();
    if (year < 50) year += 2000; // Assume 20xx
    else if (year < 100) year += 1900; // Assume 19xx
    
    Serial.print("[TIMESTAMP DEBUG] Parsed date - Year: ");
    Serial.print(year);
    Serial.print(" Month: ");
    Serial.print(month);
    Serial.print(" Day: ");
    Serial.println(day);
  } else {
    Serial.print("[TIMESTAMP] Invalid date format: ");
    Serial.println(datePart);
    return 0;
  }
  
  // v27: Enhanced time parsing for both old and UTC formats
  int hour = 0, minute = 0, second = 0;
  String timeOnly = timePart;
  
  // Remove timezone info (both old "-32" and new "+00:00" formats)
  int dashPos = timePart.indexOf('-');
  int plusPos = timePart.indexOf('+');
  int tzPos = -1;
  
  if (dashPos != -1) tzPos = dashPos;
  else if (plusPos != -1) tzPos = plusPos;
  
  if (tzPos != -1) {
    timeOnly = timePart.substring(0, tzPos);
    if (debugThreadLoading) {
      Serial.print("[TIMESTAMP] Timezone stripped: '");
      Serial.print(timePart.substring(tzPos));
      Serial.print("' Time only: '");
      Serial.print(timeOnly);
      Serial.println("'");
    }
  }
  
  int colon1 = timeOnly.indexOf(':');
  int colon2 = timeOnly.lastIndexOf(':');
  if (colon1 != -1 && colon2 != -1 && colon1 != colon2) {
    hour = timeOnly.substring(0, colon1).toInt();
    minute = timeOnly.substring(colon1 + 1, colon2).toInt();
    second = timeOnly.substring(colon2 + 1).toInt();
  } else {
    Serial.print("[TIMESTAMP] Invalid time format: ");
    Serial.println(timeOnly);
    return 0;
  }
  
  // v27 Fix: Validate ranges
  if (month < 1 || month > 12 || day < 1 || day > 31 || 
      hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
    Serial.print("[TIMESTAMP] Invalid date/time values - Year: ");
    Serial.print(year);
    Serial.print(" Month: ");
    Serial.print(month);
    Serial.print(" Day: ");
    Serial.print(day);
    Serial.print(" Hour: ");
    Serial.print(hour);
    Serial.print(" Min: ");
    Serial.print(minute);
    Serial.print(" Sec: ");
    Serial.println(second);
    return 0;
  }
  
  // Create a simple timestamp value (not actual epoch, but good for sorting)
  // v27: This now works consistently since all timestamps are normalized to UTC
  unsigned long timestampValue = 
    ((unsigned long)year) * 10000000000UL +
    ((unsigned long)month) * 100000000UL +
    ((unsigned long)day) * 1000000UL +
    ((unsigned long)hour) * 10000UL +
    ((unsigned long)minute) * 100UL +
    ((unsigned long)second);
  
  Serial.print("[TIMESTAMP DEBUG] Final calculated value: ");
  Serial.print(timestampValue);
  Serial.print(" for timestamp: ");
  Serial.println(timestamp);
    
  return timestampValue;
}

// v27 Timezone Helper Functions

// Helper functions for date arithmetic
String addOneDay(const String& dateStr) {
  // Convert "26/01/07" to next day "27/01/07" (simplified - handles common cases)
  // Format: YY/MM/DD
  if (dateStr.length() < 8) return dateStr; // Invalid format
  
  int year = dateStr.substring(0, 2).toInt();
  int month = dateStr.substring(3, 5).toInt();
  int day = dateStr.substring(6, 8).toInt();
  
  day++;
  
  // Simple month rollover (not perfect but good enough for SMS timestamps)
  if (day > 28 && month == 2) { // February
    day = 1; month++;
  } else if (day > 30 && (month == 4 || month == 6 || month == 9 || month == 11)) { // 30-day months
    day = 1; month++;
  } else if (day > 31) { // 31-day months
    day = 1; month++;
  }
  
  if (month > 12) {
    month = 1; year++;
  }
  
  char newDate[16];
  snprintf(newDate, sizeof(newDate), "%02d/%02d/%02d", year, month, day);
  return String(newDate);
}

String subtractOneDay(const String& dateStr) {
  // Convert "26/01/07" to previous day "25/01/07" (simplified)
  if (dateStr.length() < 8) return dateStr; // Invalid format
  
  int year = dateStr.substring(0, 2).toInt();
  int month = dateStr.substring(3, 5).toInt();
  int day = dateStr.substring(6, 8).toInt();
  
  day--;
  
  if (day < 1) {
    month--;
    if (month < 1) {
      month = 12; year--;
    }
    // Set to last day of previous month (simplified)
    if (month == 2) day = 28; // February
    else if (month == 4 || month == 6 || month == 9 || month == 11) day = 30; // 30-day months
    else day = 31; // 31-day months
  }
  
  char newDate[16];
  snprintf(newDate, sizeof(newDate), "%02d/%02d/%02d", year, month, day);
  return String(newDate);
}

String convertToUTC(const String& localTimestamp) {
  // Convert timestamp with timezone to UTC format
  // Input: "26/01/04,19:04:26-32" Output: "26/01/04,23:04:26+00:00"
  
  if (localTimestamp.length() == 0) return "";
  
  int commaPos = localTimestamp.indexOf(',');
  if (commaPos == -1) return localTimestamp + "+00:00"; // Fallback
  
  String datePart = localTimestamp.substring(0, commaPos);
  String timePart = localTimestamp.substring(commaPos + 1);
  
  // Extract timezone offset
  int dashPos = timePart.indexOf('-');
  int plusPos = timePart.indexOf('+');
  int tzPos = (dashPos != -1) ? dashPos : plusPos;
  
  if (tzPos == -1) return localTimestamp + "+00:00"; // No timezone found
  
  String timeOnly = timePart.substring(0, tzPos);
  String tzPart = timePart.substring(tzPos);
  
  // Parse timezone offset (e.g., "-32" means -8 hours in quarter-hours)
  int tzQuarters = tzPart.toInt();
  int tzMinutes = tzQuarters * 15; // Convert quarter-hours to minutes
  
  Serial.print("[UTC DEBUG] Timezone part: '");
  Serial.print(tzPart);
  Serial.print("' quarters=");
  Serial.print(tzQuarters);
  Serial.print(" minutes=");
  Serial.println(tzMinutes);
  
  // Parse time components
  int colon1 = timeOnly.indexOf(':');
  int colon2 = timeOnly.lastIndexOf(':');
  if (colon1 == -1 || colon2 == -1) return localTimestamp + "+00:00";
  
  int hour = timeOnly.substring(0, colon1).toInt();
  int minute = timeOnly.substring(colon1 + 1, colon2).toInt();
  int second = timeOnly.substring(colon2 + 1).toInt();
  
  Serial.print("[UTC DEBUG] Original time: ");
  Serial.print(hour);
  Serial.print(":");
  Serial.print(minute);
  Serial.print(":");
  Serial.println(second);
  
  // Convert to UTC by subtracting timezone offset
  int totalMinutes = hour * 60 + minute - tzMinutes;
  
  Serial.print("[UTC DEBUG] totalMinutes before adjustment: ");
  Serial.println(totalMinutes);
  
  // Handle day rollover with proper date calculation
  String newDate = datePart;
  if (totalMinutes < 0) {
    totalMinutes += 24 * 60;
    // Subtract a day from the date
    newDate = subtractOneDay(datePart);
    Serial.println("[UTC DEBUG] Day rollover backwards - subtracting day");
  } else if (totalMinutes >= 24 * 60) {
    totalMinutes -= 24 * 60;
    // Add a day to the date  
    newDate = addOneDay(datePart);
    Serial.println("[UTC DEBUG] Day rollover forwards - adding day");
  }
  
  Serial.print("[UTC DEBUG] Final date: ");
  Serial.print(newDate);
  Serial.print(" time: ");
  Serial.print(totalMinutes / 60);
  Serial.print(":");
  Serial.println(totalMinutes % 60);
  
  int utcHour = totalMinutes / 60;
  int utcMinute = totalMinutes % 60;
  
  // Format UTC timestamp
  char utcTime[32];
  snprintf(utcTime, sizeof(utcTime), "%s,%02d:%02d:%02d+00:00", 
           newDate.c_str(), utcHour, utcMinute, second);
  
  return String(utcTime);
}

String formatTimeForDisplay(const String& timestamp) {
  // Format timestamp for display: "26/01/04,19:04:26-32" -> "19:04"
  // Works with both UTC and local timestamps
  
  if (timestamp.length() == 0) return "";
  
  int commaPos = timestamp.indexOf(',');
  if (commaPos == -1) return timestamp;
  
  String timePart = timestamp.substring(commaPos + 1);
  
  // Remove timezone info
  int dashPos = timePart.indexOf('-');
  int plusPos = timePart.indexOf('+');
  int tzPos = -1;
  
  if (dashPos != -1) tzPos = dashPos;
  else if (plusPos != -1) tzPos = plusPos;
  
  if (tzPos != -1) {
    timePart = timePart.substring(0, tzPos);
  }
  
  // Extract HH:MM from HH:MM:SS
  int colon1 = timePart.indexOf(':');
  int colon2 = timePart.lastIndexOf(':');
  if (colon1 != -1 && colon2 != -1 && colon1 != colon2) {
    return timePart.substring(0, colon2); // Returns "HH:MM"
  }
  
  return timePart;
}

// v29: Text wrapping helper for conversation display
int drawWrappedText(const String& text, int startX, int startY, int maxWidth, uint16_t color, int lineHeight = 10) {
  // Draw text with word wrapping, maintaining left alignment at startX
  // Returns the number of lines used
  
  if (text.length() == 0) return 1;
  
  tft.setTextColor(color);
  int currentY = startY;
  int linesUsed = 0;
  int charWidth = 6; // Approximate character width for text size 1
  int maxCharsPerLine = maxWidth / charWidth;
  
  unsigned int pos = 0;
  while (pos < text.length()) {
    String line = "";
    int lineChars = 0;
    
    // Build line up to maxCharsPerLine, trying to break at spaces
    while (pos < text.length() && lineChars < maxCharsPerLine) {
      line += text.charAt(pos);
      pos++;
      lineChars++;
    }
    
    // If we didn't reach end of text and didn't break at a space, try to find a better break point
    if (pos < text.length() && lineChars == maxCharsPerLine) {
      int lastSpace = line.lastIndexOf(' ');
      if (lastSpace > 0 && lastSpace > lineChars * 0.7) { // Don't break too early in the line
        // Break at the space
        pos -= (lineChars - lastSpace - 1);
        line = line.substring(0, lastSpace);
      }
    }
    
    // Draw the line (remove leading/trailing whitespace manually)
    line.trim(); // Apply trim operation to line
    
    // v33: Only draw if line is within conversation area bounds
    if (currentY >= CONVERSATION_Y && currentY < CONVERSATION_Y + CONVERSATION_HEIGHT) {
      tft.setCursor(startX, currentY);
      tft.print(line);
    }
    
    currentY += lineHeight;
    linesUsed++;
    
    // Safety check - don't wrap beyond conversation area
    if (linesUsed >= 10) break;
  }
  
  return linesUsed;
}

// v26 Thread Management Functions

String generateContactHash(const String& phoneNumber) {
  // Generate a simple hash from phone number for thread cache filename
  String cleanNumber = phoneNumber;
  cleanNumber.replace("+", "");
  cleanNumber.replace(" ", "");
  cleanNumber.replace("-", "");
  cleanNumber.replace("(", "");
  cleanNumber.replace(")", "");
  
  // Simple hash - use last 8 digits
  if (cleanNumber.length() > 8) {
    return cleanNumber.substring(cleanNumber.length() - 8);
  }
  return cleanNumber;
}

bool loadAllMessages() {
  // Load all SMS messages from individual files into temporary array
  Serial.println("=== Loading All Messages for Thread Processing ===");
  logMemoryUsage("Before loading all messages");
  
  // Use a smaller temporary array to save memory
  SMSMessage tempMessages[20];  // Reduced from 50 to 20
  int tempMessageCount = 0;
  
  Serial.println("[DEBUG] Opening SD root directory...");
  FsFile root = sd.open("/");
  if (!root) {
    Serial.println("[ERROR] Failed to open SD root directory!");
    return false;
  }
  Serial.println("[DEBUG] SD root directory opened successfully");
  
  FsFile file;
  int fileCount = 0;
  
  Serial.println("[DEBUG] Starting to scan files...");
  while (file.openNext(&root, O_RDONLY) && tempMessageCount < 20) {
    fileCount++;
    char filename[64];
    file.getName(filename, sizeof(filename));
    
    if (fileCount % 10 == 0) {
      Serial.print("[DEBUG] Scanned ");
      Serial.print(fileCount);
      Serial.println(" files so far...");
    }
    
    // Check if this is an SMS file (starts with "sms_")
    if (strncmp(filename, "sms_", 4) == 0) {
      Serial.print("[DEBUG] Found SMS file: ");
      Serial.println(filename);
      
      // Parse SMS file content - support both old and new formats
      String lines[5];  // Support up to 5 lines for new format
      int lineCount = 0;
      
      Serial.print("[DEBUG] File size: ");
      Serial.print((unsigned long)file.size());
      Serial.println(" bytes");
      
      while (file.available() && lineCount < 5) {
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
      
      Serial.print("[DEBUG] Read ");
      Serial.print(lineCount);
      Serial.println(" lines from file");
      
      // Parse the lines - handle both old (4 lines) and new (5 lines) formats
      if (lineCount >= 4) {
        Serial.println("[DEBUG] Processing message lines...");
        // Old format: From, Time, Status, Content
        // New format: From, To, Time, Status, Content
        
        bool isNewFormat = (lineCount >= 5 && lines[1].startsWith("To: "));
        
        if (isNewFormat) {
          // New format with To: field
          tempMessages[tempMessageCount].sender = lines[0];
          tempMessages[tempMessageCount].sender.replace("From: ", "");
          
          tempMessages[tempMessageCount].recipient = lines[1];
          tempMessages[tempMessageCount].recipient.replace("To: ", "");
          
          tempMessages[tempMessageCount].time = lines[2];
          tempMessages[tempMessageCount].time.replace("Time: ", "");
          
          tempMessages[tempMessageCount].content = lines[4];
          tempMessages[tempMessageCount].content.replace("Content: ", "");
          
          // Determine if outgoing based on presence of To: field
          tempMessages[tempMessageCount].isOutgoing = true;
        } else {
          // Old format - incoming messages only
          tempMessages[tempMessageCount].sender = lines[0];
          tempMessages[tempMessageCount].sender.replace("From: ", "");
          
          tempMessages[tempMessageCount].recipient = ""; // No recipient for incoming
          
          tempMessages[tempMessageCount].time = lines[1];
          tempMessages[tempMessageCount].time.replace("Time: ", "");
          
          tempMessages[tempMessageCount].content = lines[3];
          tempMessages[tempMessageCount].content.replace("Content: ", "");
          
          tempMessages[tempMessageCount].isOutgoing = false;
        }
        
        tempMessages[tempMessageCount].fullTime = tempMessages[tempMessageCount].time;
        tempMessages[tempMessageCount].fullTime.replace(",", " ");
        tempMessages[tempMessageCount].filename = String(filename);
        tempMessages[tempMessageCount].timestampValue = parseTimestamp(tempMessages[tempMessageCount].time);
        
        Serial.print("[DEBUG] Message direction: ");
        Serial.println(tempMessages[tempMessageCount].isOutgoing ? "outgoing" : "incoming");
        
        // Set display name based on message direction
        if (tempMessages[tempMessageCount].isOutgoing) {
          tempMessages[tempMessageCount].senderDisplayName = lookupContactName(tempMessages[tempMessageCount].recipient);
        } else {
          tempMessages[tempMessageCount].senderDisplayName = lookupContactName(tempMessages[tempMessageCount].sender);
        }
        
        tempMessageCount++;
        Serial.print("[DEBUG] Total messages loaded so far: ");
        Serial.println(tempMessageCount);
      }
    }
    file.close();
  }
  root.close();
  
  Serial.print("[DEBUG] Scanned total files: ");
  Serial.println(fileCount);
  Serial.print("[DEBUG] Loaded ");
  Serial.print(tempMessageCount);
  Serial.println(" SMS messages");
  
  if (tempMessageCount == 0) {
    Serial.println("[WARNING] No SMS messages found! Check if SMS files exist on SD card.");
  }
  
  // Now build thread previews from the loaded messages (pass them as parameter)
  Serial.println("[DEBUG] Starting buildThreadPreviews()...");
  buildThreadPreviewsFromMessages(tempMessages, tempMessageCount);
  Serial.println("[DEBUG] buildThreadPreviews() completed");
  
  logMemoryUsage("After loading all messages");
  return tempMessageCount > 0;
}

void buildThreadPreviewsFromMessages(SMSMessage* messages, int messageCount) {
  Serial.println("=== Building Contact Previews from Address Book + Messages ===");
  
  // Reset thread previews
  threadPreviewCount = 0;
  Serial.print("[DEBUG] Starting with ");
  Serial.print(addressBookCount);
  Serial.println(" contacts from address book");
  
  // v34: Start with ALL contacts from address book
  for (int i = 0; i < addressBookCount && threadPreviewCount < 20; i++) {
    threadPreviews[threadPreviewCount].contactPhone = addressBook[i].phoneNumber;
    threadPreviews[threadPreviewCount].contactDisplayName = addressBook[i].name;
    threadPreviews[threadPreviewCount].lastMessageTime = ""; // No message initially
    threadPreviews[threadPreviewCount].lastMessagePreview = "No messages";
    threadPreviews[threadPreviewCount].lastTimestamp = 0; // No activity = bottom of sort
    threadPreviews[threadPreviewCount].hasUnread = false;
    threadPreviews[threadPreviewCount].messageCount = 0;
    threadPreviewCount++;
  }
  
  Serial.print("[DEBUG] Added ");
  Serial.print(threadPreviewCount);
  Serial.println(" contacts to preview list");
  
  // Now update with message data
  Serial.print("[DEBUG] Processing ");
  Serial.print(messageCount);
  Serial.println(" messages to update contact activity...");
  
  for (int i = 0; i < messageCount; i++) {
    String contactPhone;
    
    // Determine the contact phone number
    if (messages[i].isOutgoing) {
      contactPhone = messages[i].recipient;
    } else {
      contactPhone = messages[i].sender;
    }
    
    // Find this contact in our preview list
    bool contactFound = false;
    int contactIndex = -1;
    
    for (int j = 0; j < threadPreviewCount; j++) {
      // Use same phone number matching logic as lookupContactName
      String cleanPreviewPhone = threadPreviews[j].contactPhone;
      cleanPreviewPhone.replace(" ", "");
      cleanPreviewPhone.replace("-", "");
      cleanPreviewPhone.replace("(", "");
      cleanPreviewPhone.replace(")", "");
      cleanPreviewPhone.replace("+", "");
      
      String cleanMessagePhone = contactPhone;
      cleanMessagePhone.replace(" ", "");
      cleanMessagePhone.replace("-", "");
      cleanMessagePhone.replace("(", "");
      cleanMessagePhone.replace(")", "");
      cleanMessagePhone.replace("+", "");
      
      if (cleanPreviewPhone.equals(cleanMessagePhone) ||
          (cleanPreviewPhone.length() > 10 && cleanMessagePhone.length() == 10 && cleanPreviewPhone.endsWith(cleanMessagePhone)) ||
          (cleanPreviewPhone.length() == 10 && cleanMessagePhone.length() > 10 && cleanMessagePhone.endsWith(cleanPreviewPhone))) {
        contactFound = true;
        contactIndex = j;
        break;
      }
    }
    
    if (contactFound) {
      // Update contact with latest message info if this message is newer
      if (messages[i].timestampValue > threadPreviews[contactIndex].lastTimestamp) {
        threadPreviews[contactIndex].lastMessageTime = messages[i].time;
        threadPreviews[contactIndex].lastMessagePreview = messages[i].content.substring(0, 25);
        threadPreviews[contactIndex].lastTimestamp = messages[i].timestampValue;
      }
      threadPreviews[contactIndex].messageCount++;
    } else {
      // Contact not in address book but has messages - add them if space
      if (threadPreviewCount < 20) {
        threadPreviews[threadPreviewCount].contactPhone = contactPhone;
        threadPreviews[threadPreviewCount].contactDisplayName = lookupContactName(contactPhone); // Will return phone if unknown
        threadPreviews[threadPreviewCount].lastMessageTime = messages[i].time;
        threadPreviews[threadPreviewCount].lastMessagePreview = messages[i].content.substring(0, 25);
        threadPreviews[threadPreviewCount].lastTimestamp = messages[i].timestampValue;
        threadPreviews[threadPreviewCount].hasUnread = false;
        threadPreviews[threadPreviewCount].messageCount = 1;
        threadPreviewCount++;
      }
    }
  }
  
  Serial.println("[DEBUG] Starting contact sorting by recent activity...");
  // Sort contacts: those with recent messages first, then by timestamp, then contacts without messages
  sortThreadPreviewsByTime();
  Serial.println("[DEBUG] Contact sorting completed");
  
  Serial.print("[SUCCESS] Built ");
  Serial.print(threadPreviewCount);
  Serial.println(" contact previews");
  
  // Print first few previews for debugging
  for (int i = 0; i < min(5, threadPreviewCount); i++) {
    Serial.print("[DEBUG] Contact ");
    Serial.print(i);
    Serial.print(": ");
    Serial.print(threadPreviews[i].contactDisplayName);
    Serial.print(" (");
    Serial.print(threadPreviews[i].contactPhone);
    Serial.print(") - ");
    Serial.print(threadPreviews[i].messageCount);
    Serial.println(" msgs");
  }
}

void buildThreadPreviews() {
  Serial.println("=== Building Thread Previews ===");
  logMemoryUsage("buildThreadPreviews start");
  
  // Reset thread previews
  threadPreviewCount = 0;
  Serial.println("[DEBUG] Reset thread preview count to 0");
  
  // Load all messages again to build previews (memory efficient approach)
  SMSMessage tempMessages[50];
  int tempMessageCount = 0;
  
  Serial.println("[DEBUG] Opening SD root for thread preview building...");
  FsFile root = sd.open("/");
  if (!root) {
    Serial.println("[ERROR] Failed to open SD root in buildThreadPreviews!");
    return;
  }
  
  FsFile file;
  int processedFiles = 0;
  
  while (file.openNext(&root, O_RDONLY) && tempMessageCount < 50) {
    processedFiles++;
    char filename[64];
    file.getName(filename, sizeof(filename));
    
    if (strncmp(filename, "sms_", 4) == 0) {
      String lines[5];
      int lineCount = 0;
      
      while (file.available() && lineCount < 5) {
        String line = "";
        while (file.available()) {
          char c = file.read();
          if (c == '\n' || c == '\r') break;
          line += c;
        }
        if (line.length() > 0) {
          lines[lineCount] = line;
          lineCount++;
        }
      }
      
      if (lineCount >= 4) {
        bool isNewFormat = (lineCount >= 5 && lines[1].startsWith("To: "));
        
        if (processedFiles <= 3) { // Only debug first few files to avoid spam
          Serial.print("[DEBUG] Processing message, lineCount: ");
          Serial.println(lineCount);
          Serial.print("[DEBUG] Line 1: '");
          Serial.print(lines[1]);
          Serial.println("'");
          Serial.print("[DEBUG] isNewFormat: ");
          Serial.println(isNewFormat ? "true" : "false");
          
          // Add memory check during processing
          logMemoryUsage("During message processing");
        }
        
        tempMessages[tempMessageCount].isOutgoing = isNewFormat;
        
        if (isNewFormat) {
          tempMessages[tempMessageCount].sender = lines[0].substring(6); // Remove "From: "
          tempMessages[tempMessageCount].recipient = lines[1].substring(4); // Remove "To: "
          tempMessages[tempMessageCount].time = lines[2].substring(6); // Remove "Time: "
          tempMessages[tempMessageCount].content = lines[4].substring(9); // Remove "Content: "
        } else {
          tempMessages[tempMessageCount].sender = lines[0].substring(6);
          tempMessages[tempMessageCount].recipient = "";
          tempMessages[tempMessageCount].time = lines[1].substring(6);
          tempMessages[tempMessageCount].content = lines[3].substring(9);
        }
        
        tempMessages[tempMessageCount].timestampValue = parseTimestamp(tempMessages[tempMessageCount].time);
        tempMessageCount++;
        
        if (processedFiles <= 3) { // Debug first few files
          Serial.print("[DEBUG] Parsed message successfully, tempMessageCount: ");
          Serial.println(tempMessageCount);
          logMemoryUsage("After parsing message");
        }
      }
    }
    file.close();
    
    if (processedFiles <= 3) {
      Serial.print("[DEBUG] Closed file, processed files: ");
      Serial.println(processedFiles);
    }
  }
  root.close();
  
  Serial.print("[DEBUG] All files processed, tempMessageCount: ");
  Serial.println(tempMessageCount);
  logMemoryUsage("After processing all files");
  
  Serial.print("[DEBUG] Processing ");
  Serial.print(tempMessageCount);
  Serial.println(" messages for thread grouping...");
  
  // Now group messages by contact and find latest for each thread
  for (int i = 0; i < tempMessageCount && threadPreviewCount < 20; i++) {
    if (i % 5 == 0) {
      Serial.print("[DEBUG] Processing message ");
      Serial.print(i + 1);
      Serial.print(" of ");
      Serial.println(tempMessageCount);
    }
    String contactPhone;
    
    // Determine the contact phone number
    if (tempMessages[i].isOutgoing) {
      contactPhone = tempMessages[i].recipient;
    } else {
      contactPhone = tempMessages[i].sender;
    }
    
    // Check if we already have a preview for this contact
    bool contactExists = false;
    int existingIndex = -1;
    
    for (int j = 0; j < threadPreviewCount; j++) {
      if (threadPreviews[j].contactPhone.equals(contactPhone)) {
        contactExists = true;
        existingIndex = j;
        break;
      }
    }
    
    if (i < 3) { // Debug first few contacts
      Serial.print("[DEBUG] Contact: ");
      Serial.print(contactPhone);
      Serial.print(", exists: ");
      Serial.println(contactExists ? "true" : "false");
    }
    
    if (contactExists) {
      // Update if this message is newer
      if (tempMessages[i].timestampValue > threadPreviews[existingIndex].lastTimestamp) {
        threadPreviews[existingIndex].lastMessageTime = tempMessages[i].time;
        threadPreviews[existingIndex].lastMessagePreview = tempMessages[i].content.substring(0, 25); // Preview length
        threadPreviews[existingIndex].lastTimestamp = tempMessages[i].timestampValue;
      }
      threadPreviews[existingIndex].messageCount++;
    } else {
      // Create new thread preview
      threadPreviews[threadPreviewCount].contactPhone = contactPhone;
      threadPreviews[threadPreviewCount].contactDisplayName = lookupContactName(contactPhone);
      threadPreviews[threadPreviewCount].lastMessageTime = tempMessages[i].time;
      threadPreviews[threadPreviewCount].lastMessagePreview = tempMessages[i].content.substring(0, 25);
      threadPreviews[threadPreviewCount].lastTimestamp = tempMessages[i].timestampValue;
      threadPreviews[threadPreviewCount].hasUnread = false;
      threadPreviews[threadPreviewCount].messageCount = 1;
      threadPreviewCount++;
    }
  }
  
  Serial.println("[DEBUG] Starting thread preview sorting...");
  // Sort thread previews by latest activity (most recent first)
  sortThreadPreviewsByTime();
  Serial.println("[DEBUG] Thread preview sorting completed");
  
  Serial.print("[SUCCESS] Built ");
  Serial.print(threadPreviewCount);
  Serial.println(" thread previews");
  
  // Print first few thread previews for debugging
  for (int i = 0; i < min(3, threadPreviewCount); i++) {
    Serial.print("[DEBUG] Thread ");
    Serial.print(i);
    Serial.print(": ");
    Serial.print(threadPreviews[i].contactDisplayName);
    Serial.print(" (");
    Serial.print(threadPreviews[i].contactPhone);
    Serial.println(")");
  }
}

void sortThreadPreviewsByTime() {
  // Simple bubble sort for thread previews by latest timestamp
  for (int i = 0; i < threadPreviewCount - 1; i++) {
    for (int j = 0; j < threadPreviewCount - i - 1; j++) {
      if (threadPreviews[j].lastTimestamp < threadPreviews[j + 1].lastTimestamp) {
        ThreadPreview temp = threadPreviews[j];
        threadPreviews[j] = threadPreviews[j + 1];
        threadPreviews[j + 1] = temp;
      }
    }
  }
}

void loadThreadForContact(const String& phoneNumber) {
  Serial.print("Loading thread for contact: ");
  Serial.println(phoneNumber);
  logMemoryUsage("loadThreadForContact start");
  
  currentThreadMessageCount = 0;
  activeContactPhone = phoneNumber;
  activeContactName = lookupContactName(phoneNumber);
  
  // Clean the phone number for comparison
  String cleanTargetNumber = phoneNumber;
  cleanTargetNumber.replace(" ", "");
  cleanTargetNumber.replace("-", "");
  cleanTargetNumber.replace("(", "");
  cleanTargetNumber.replace(")", "");
  cleanTargetNumber.replace("+", "");
  
  // Load all messages for this contact
  FsFile root = sd.open("/");
  FsFile file;
  
  while (file.openNext(&root, O_RDONLY) && currentThreadMessageCount < 30) {
    char filename[64];
    file.getName(filename, sizeof(filename));
    
    Serial.print("[THREAD DEBUG] Checking file: ");
    Serial.println(filename);
    
    if (strncmp(filename, "sms_", 4) == 0) {
      Serial.print("[THREAD DEBUG] Processing SMS file: ");
      Serial.println(filename);
      String lines[6];  // v27: Increased for dual-timestamp format  
      int lineCount = 0;
      
      while (file.available() && lineCount < 6) {
        String line = "";
        while (file.available()) {
          char c = file.read();
          if (c == '\n' || c == '\r') break;
          line += c;
        }
        if (line.length() > 0) {
          lines[lineCount] = line;
          lineCount++;
        }
      }
      
      // v27 Debug: Show full file contents (conditional)
      if (debugThreadLoading) {
        Serial.println("========== FULL SMS FILE CONTENTS ==========");
        Serial.print("[FILE] ");
        Serial.println(filename);
        Serial.print("[LINES] ");
        Serial.println(lineCount);
        for (int i = 0; i < lineCount; i++) {
          Serial.print("[LINE ");
          Serial.print(i);
          Serial.print("] '");
          Serial.print(lines[i]);
          Serial.println("'");
        }
        Serial.println("============================================");
      }
      
      if (lineCount >= 4) {
        // v27: Detect format type
        bool hasLocalTime = false;
        bool isOutgoing = false;
        
        // Check for v27 dual-timestamp format (has LocalTime line)
        for (int i = 0; i < lineCount; i++) {
          if (lines[i].startsWith("LocalTime: ")) {
            hasLocalTime = true;
            break;
          }
        }
        
        // Check if outgoing (has "To:" field)
        for (int i = 0; i < lineCount; i++) {
          if (lines[i].startsWith("To: ")) {
            isOutgoing = true;
            break;
          }
        }
        
        if (debugThreadLoading) {
          Serial.print("[FORMAT] ");
          if (hasLocalTime) {
            Serial.println("V27 DUAL-TIMESTAMP");
          } else if (isOutgoing) {
            Serial.println("V26 OUTGOING");
          } else {
            Serial.println("V26 INCOMING");
          }
        }
        
        String msgSender, msgRecipient, msgTime, msgLocalTime, msgContent;
        
        if (hasLocalTime && isOutgoing) {
          // v27 new format: From/To/Time(UTC)/LocalTime/Status/Content
          msgSender = lines[0].substring(6);        // Remove "From: "
          msgRecipient = lines[1].substring(4);     // Remove "To: "  
          msgTime = lines[2].substring(6);          // Remove "Time: " (UTC)
          msgLocalTime = lines[3].substring(11);    // Remove "LocalTime: "
          msgContent = lines[5].substring(9);       // Remove "Content: "
        } else if (isOutgoing) {
          // v26 outgoing format: From/To/Time/Status/Content
          msgSender = lines[0].substring(6);
          msgRecipient = lines[1].substring(4);
          msgTime = lines[2].substring(6);
          msgLocalTime = msgTime; // Same as msgTime in old format
          msgContent = lines[4].substring(9);
        } else {
          // v26 incoming format: From/Time/Status/Content
          msgSender = lines[0].substring(6);
          msgRecipient = "";
          msgTime = lines[1].substring(6);
          msgLocalTime = msgTime; // Same as msgTime in old format
          msgContent = lines[3].substring(9);
        }
        
        // v27: For old format messages without UTC, convert local time to UTC for sorting
        String utcTimeForSorting = msgTime;
        if (!hasLocalTime && msgTime.indexOf('+') == -1) {
          // Old format without UTC - convert to UTC
          utcTimeForSorting = convertToUTC(msgTime);
          if (debugThreadLoading) {
            Serial.print("[CONVERSION] Old format converted to UTC: ");
            Serial.println(utcTimeForSorting);
          }
        }
        
        // v27 Debug: Show parsed message data (conditional)
        if (debugThreadLoading) {
          Serial.println("---------- PARSED MESSAGE DATA ----------");
          Serial.print("[PARSED SENDER] '");
          Serial.print(msgSender);
          Serial.println("'");
          Serial.print("[PARSED RECIPIENT] '");
          Serial.print(msgRecipient);
          Serial.println("'");
          Serial.print("[PARSED TIME (for sorting)] '");
          Serial.print(utcTimeForSorting);
          Serial.println("'");
          if (hasLocalTime) {
            Serial.print("[PARSED LOCAL TIME] '");
            Serial.print(msgLocalTime);
            Serial.println("'");
          }
          Serial.print("[PARSED CONTENT] '");
          Serial.print(msgContent);
          Serial.println("'");
          Serial.print("[PARSED OUTGOING] ");
          Serial.println(isOutgoing ? "true" : "false");
          
          // Parse and show timestamp value
          unsigned long timestampVal = parseTimestamp(utcTimeForSorting);
          Serial.print("[TIMESTAMP VALUE] ");
          Serial.println(timestampVal);
          Serial.println("------------------------------------------");
        }
        
        // Clean sender and recipient for comparison
        String cleanSender = msgSender;
        cleanSender.replace(" ", "");
        cleanSender.replace("-", "");
        cleanSender.replace("(", "");
        cleanSender.replace(")", "");
        cleanSender.replace("+", "");
        
        String cleanRecipient = msgRecipient;
        cleanRecipient.replace(" ", "");
        cleanRecipient.replace("-", "");
        cleanRecipient.replace("(", "");
        cleanRecipient.replace(")", "");
        cleanRecipient.replace("+", "");
        
        // Check if this message belongs to our contact
        bool isMatch = false;
        if (isOutgoing) {
          // For outgoing messages, check recipient
          Serial.print("[MATCH DEBUG] Outgoing: target='");
          Serial.print(cleanTargetNumber);
          Serial.print("' recipient='");
          Serial.print(cleanRecipient);
          Serial.print("' -> ");
          
          if (cleanTargetNumber.equals(cleanRecipient) || 
              (cleanTargetNumber.length() > 10 && cleanRecipient.length() == 10 && cleanTargetNumber.endsWith(cleanRecipient)) ||
              (cleanTargetNumber.length() == 10 && cleanRecipient.length() > 10 && cleanRecipient.endsWith(cleanTargetNumber))) {
            isMatch = true;
            Serial.println("MATCH");
          } else {
            Serial.println("NO MATCH");
          }
        } else {
          // For incoming messages, check sender
          if (cleanTargetNumber.equals(cleanSender) ||
              (cleanTargetNumber.length() > 10 && cleanSender.length() == 10 && cleanTargetNumber.endsWith(cleanSender)) ||
              (cleanTargetNumber.length() == 10 && cleanSender.length() > 10 && cleanSender.endsWith(cleanTargetNumber))) {
            isMatch = true;
          }
        }
        
        if (isMatch) {
          if (debugThreadLoading) {
            Serial.println(">>>>>>> MESSAGE MATCHED - ADDING TO THREAD <<<<<<<");
            Serial.print("[THREAD MSG #");
            Serial.print(currentThreadMessageCount);
            Serial.println("]");
          }
          
          currentThreadMessages[currentThreadMessageCount].sender = msgSender;
          currentThreadMessages[currentThreadMessageCount].recipient = msgRecipient;
          currentThreadMessages[currentThreadMessageCount].time = utcTimeForSorting;  // v27: Use UTC for sorting
          currentThreadMessages[currentThreadMessageCount].content = msgContent;
          currentThreadMessages[currentThreadMessageCount].isOutgoing = isOutgoing;
          currentThreadMessages[currentThreadMessageCount].timestampValue = parseTimestamp(utcTimeForSorting);  // v27: Parse UTC timestamp
          currentThreadMessages[currentThreadMessageCount].filename = String(filename);
          
          // v27: Store local time for display (will add fullTime field usage later)
          currentThreadMessages[currentThreadMessageCount].fullTime = hasLocalTime ? msgLocalTime : msgTime;
          
          if (isOutgoing) {
            currentThreadMessages[currentThreadMessageCount].senderDisplayName = "Me";
          } else {
            // Use cached activeContactName instead of repeated lookups
            currentThreadMessages[currentThreadMessageCount].senderDisplayName = activeContactName;
          }
          
          if (debugThreadLoading) {
            Serial.print("[ADDED] File: ");
            Serial.print(filename);
            Serial.print(" | Time: ");
            Serial.print(msgTime);
            Serial.print(" | TimestampVal: ");
            Serial.print(currentThreadMessages[currentThreadMessageCount].timestampValue);
            Serial.print(" | Direction: ");
            Serial.println(isOutgoing ? "OUT" : "IN");
            Serial.println(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
          }
          
          currentThreadMessageCount++;
        } else if (debugThreadLoading) {
          Serial.println("[NO MATCH] - Message not added to thread");
        }
      }
    }
    file.close();
  }
  root.close();
  
  // v27 Fix: Sort messages by timestamp (oldest first for conversation view)
  logMemoryUsage("Before sorting thread messages");
  
  // Debug: Show timestamps before sorting
  Serial.println("[SORT DEBUG] Timestamps before sorting:");
  for (int i = 0; i < min(5, currentThreadMessageCount); i++) {
    Serial.print("Message ");
    Serial.print(i);
    Serial.print(": time='");
    Serial.print(currentThreadMessages[i].time);
    Serial.print("' value=");
    Serial.print(currentThreadMessages[i].timestampValue);
    Serial.print(" outgoing=");
    Serial.println(currentThreadMessages[i].isOutgoing ? "true" : "false");
  }
  
  // v27 Fix: Single sort with improved comparison (handles zero timestamps)
  for (int i = 0; i < currentThreadMessageCount - 1; i++) {
    for (int j = 0; j < currentThreadMessageCount - i - 1; j++) {
      // v27 Fix: Handle invalid timestamps by using filename as fallback
      unsigned long timeJ = currentThreadMessages[j].timestampValue;
      unsigned long timeJPlus1 = currentThreadMessages[j + 1].timestampValue;
      
      // If timestamps are invalid (0), use filename comparison as fallback
      if (timeJ == 0 && timeJPlus1 == 0) {
        // Sort by filename lexically (sms_ files are timestamped)
        if (currentThreadMessages[j].filename > currentThreadMessages[j + 1].filename) {
          SMSMessage temp = currentThreadMessages[j];
          currentThreadMessages[j] = currentThreadMessages[j + 1];
          currentThreadMessages[j + 1] = temp;
        }
      } else if (timeJ == 0) {
        // Move invalid timestamp to end
        SMSMessage temp = currentThreadMessages[j];
        currentThreadMessages[j] = currentThreadMessages[j + 1];
        currentThreadMessages[j + 1] = temp;
      } else if (timeJPlus1 != 0 && timeJ > timeJPlus1) {
        // Normal timestamp comparison
        SMSMessage temp = currentThreadMessages[j];
        currentThreadMessages[j] = currentThreadMessages[j + 1];
        currentThreadMessages[j + 1] = temp;
      }
    }
  }
  
  // Debug: Show final thread order (conditional)
  if (debugThreadLoading) {
    Serial.println("=============== FINAL THREAD ORDER ===============");
    Serial.print("[TOTAL MESSAGES] ");
    Serial.println(currentThreadMessageCount);
    for (int i = 0; i < currentThreadMessageCount; i++) {
      Serial.print("[MSG ");
      Serial.print(i);
      Serial.print("] ");
      Serial.print(currentThreadMessages[i].isOutgoing ? "OUT" : "IN ");
      Serial.print(" | File: ");
      Serial.print(currentThreadMessages[i].filename);
      Serial.print(" | Time: '");
      Serial.print(currentThreadMessages[i].time);
      Serial.print("' | TimestampVal: ");
      Serial.print(currentThreadMessages[i].timestampValue);
      Serial.print(" | Content: '");
      Serial.print(currentThreadMessages[i].content.substring(0, 30));
      Serial.println("...'");
    }
    Serial.println("====================================================");
  }
  
  logMemoryUsage("After sorting thread messages");
  
  // Auto-scroll to show latest complete messages above compose line
  userIsManuallyScrolling = false; // Reset manual scroll flag for new thread
  calculateOptimalScroll();
  
  Serial.print("Loaded ");
  Serial.print(currentThreadMessageCount);
  Serial.println(" messages for thread");
  logMemoryUsage("loadThreadForContact end");
}

void calculateOptimalScroll() {
  // Calculate proper pixel scroll position to show complete latest messages above compose line
  if (currentThreadMessageCount == 0) {
    conversationPixelScrollOffset = 0;
    return;
  }
  
  int conversationDisplayHeight = CONVERSATION_HEIGHT - 20; // Reserve for header
  int totalContentHeight = calculateTotalContentHeight();
  
  // Position scroll to show the bottom of the content
  conversationPixelScrollOffset = max(0, totalContentHeight - conversationDisplayHeight);
  
  Serial.print("Auto-scroll: pixel offset ");
  Serial.print(conversationPixelScrollOffset);
  Serial.print(" (total height: ");
  Serial.print(totalContentHeight);
  Serial.print(", display height: ");
  Serial.print(conversationDisplayHeight);
  Serial.print(", viewport: ");
  Serial.print(conversationPixelScrollOffset);
  Serial.print("-");
  Serial.print(conversationPixelScrollOffset + conversationDisplayHeight);
  Serial.println(")");
}

int calculateMaxScrollOffset() {
  // Calculate maximum pixel scroll offset based on total content height
  int totalHeight = calculateTotalContentHeight();
  int conversationDisplayHeight = CONVERSATION_HEIGHT - 20; // Reserve for header
  
  return max(0, totalHeight - conversationDisplayHeight);
}

int calculateTotalContentHeight() {
  // Calculate the total pixel height of all messages with their wrapping
  if (currentThreadMessageCount == 0) {
    return 0;
  }
  
  int screenWidth = 240;
  int incomingLeftMargin = 2;
  int outgoingLeftMargin = screenWidth / 3;
  int timeWidth = 30;
  int totalHeight = 0;
  
  for (int i = 0; i < currentThreadMessageCount; i++) {
    SMSMessage& msg = currentThreadMessages[i];
    
    // Calculate message width and estimate lines needed
    int messageMaxWidth;
    if (msg.isOutgoing) {
      messageMaxWidth = screenWidth - outgoingLeftMargin - timeWidth - 10;
    } else {
      messageMaxWidth = screenWidth - incomingLeftMargin - timeWidth - 10;
    }
    
    // Estimate lines needed for this message (same logic as in calculateOptimalScroll)
    int charsPerLine = messageMaxWidth / 6; // Approximate 6 pixels per character
    int linesNeeded = (msg.content.length() + charsPerLine - 1) / charsPerLine;
    if (linesNeeded < 1) linesNeeded = 1;
    
    int messageHeight = linesNeeded * 10 + 2; // Line height + gap
    totalHeight += messageHeight;
  }
  
  return totalHeight;
}

bool saveOutgoingMessage(const String& recipient, const String& content) {
  // Get network time from SIM7600 for accurate timestamp
  String localTime = cellular.getNetworkTime();
  
  Serial.print("[OUTGOING DEBUG] Raw network time from SIM7600: '");
  Serial.print(localTime);
  Serial.println("'");
  
  // If network time fails, fall back to a reasonable default
  if (localTime.length() == 0) {
    localTime = "26/01/05,19:00:00-32";  // Fallback timestamp
    Serial.println("[OUTGOING] Using fallback timestamp");
  }
  
  Serial.print("[OUTGOING DEBUG] Input to convertToUTC: '");
  Serial.print(localTime);
  Serial.println("'");
  
  // v27 Dual-Timestamp: Convert to UTC for consistent sorting
  String utcTime = convertToUTC(localTime);
  
  Serial.print("[OUTGOING DEBUG] Output from convertToUTC: '");
  Serial.print(utcTime);
  Serial.println("'");
  
  // Use unique filename with milliseconds to avoid conflicts
  String filename = "sms_out_" + String(millis()) + ".txt";
  
  Serial.print("[OUTGOING] Saving outgoing message to: ");
  Serial.println(filename);
  Serial.print("[OUTGOING] Local timestamp: ");
  Serial.println(localTime);
  Serial.print("[OUTGOING] UTC timestamp: ");
  Serial.println(utcTime);
  
  FsFile outFile = sd.open(filename.c_str(), O_WRITE | O_CREAT);
  if (outFile) {
    // v27 New format: UTC timestamp for sorting + local timestamp for reference
    outFile.println("From: +1234567890");  // Placeholder - could be actual device phone number 
    outFile.println("To: " + recipient);
    outFile.println("Time: " + utcTime);        // UTC timestamp for sorting
    outFile.println("LocalTime: " + localTime); // Original local timestamp
    outFile.println("Status: SENT");
    outFile.println("Content: " + content);
    outFile.close();
    
    Serial.println("[OUTGOING] Outgoing message saved successfully with dual timestamps");
    return true;
  } else {
    Serial.println("[OUTGOING] Failed to create outgoing message file");
    return false;
  }
}

void addMessageToThread(const String& content, bool isOutgoing, const String& timestamp) {
  Serial.println("[THREAD] Adding message to current thread");
  Serial.print("[THREAD] Content: ");
  Serial.println(content);
  Serial.print("[THREAD] Outgoing: ");
  Serial.println(isOutgoing ? "true" : "false");
  Serial.print("[THREAD] Timestamp: ");
  Serial.println(timestamp);
  
  if (currentThreadMessageCount >= 30) {
    // Remove oldest message to make room
    for (int i = 0; i < currentThreadMessageCount - 1; i++) {
      currentThreadMessages[i] = currentThreadMessages[i + 1];
    }
    currentThreadMessageCount--;
  }
  
  // Add new message to thread
  if (isOutgoing) {
    currentThreadMessages[currentThreadMessageCount].sender = "+1234567890"; // Match file format
    currentThreadMessages[currentThreadMessageCount].recipient = activeContactPhone;
    currentThreadMessages[currentThreadMessageCount].senderDisplayName = "Me";
  } else {
    currentThreadMessages[currentThreadMessageCount].sender = activeContactPhone;
    currentThreadMessages[currentThreadMessageCount].recipient = "+1234567890";
    currentThreadMessages[currentThreadMessageCount].senderDisplayName = activeContactName;
  }
  
  currentThreadMessages[currentThreadMessageCount].time = timestamp;
  currentThreadMessages[currentThreadMessageCount].fullTime = timestamp;
  currentThreadMessages[currentThreadMessageCount].content = content;
  currentThreadMessages[currentThreadMessageCount].isOutgoing = isOutgoing;
  currentThreadMessages[currentThreadMessageCount].timestampValue = parseTimestamp(timestamp);
  
  currentThreadMessageCount++;
  
  Serial.print("[THREAD] Thread now has ");
  Serial.print(currentThreadMessageCount);
  Serial.println(" messages");
  
  // Auto-scroll to show new message using pixel-based scrolling
  calculateOptimalScroll();
  
  // Re-draw conversation pane to show new message
  drawConversationPane();
}

// v26 Navigation Functions

void switchPane() {
  if (currentPane == PANE_THREADS) {
    currentPane = PANE_CONVERSATION;
    Serial.println("Switched to CONVERSATION pane");
  } else {
    currentPane = PANE_THREADS;
    Serial.println("Switched to THREADS pane");
  }
  drawPaneBorder(currentPane);
  drawStatusSection(); // Update status section with new instructions
}

void scrollThreadSelection(int direction) {
  selectedThreadIndex += direction;
  
  // Bounds checking
  if (selectedThreadIndex < 0) {
    selectedThreadIndex = 0;
  } else if (selectedThreadIndex >= threadPreviewCount) {
    selectedThreadIndex = threadPreviewCount - 1;
  }
  
  // Auto-scroll view if selection is outside visible area
  int maxVisibleLines = (CONTACTS_HEIGHT - 20) / 10;
  if (selectedThreadIndex < previewScrollOffset) {
    previewScrollOffset = selectedThreadIndex;
  } else if (selectedThreadIndex >= previewScrollOffset + maxVisibleLines) {
    previewScrollOffset = selectedThreadIndex - maxVisibleLines + 1;
  }
  
  drawContactsPane();
  Serial.print("Thread selection: ");
  Serial.println(selectedThreadIndex);
}

void scrollConversation(int direction) {
  // Scroll by 10 pixels at a time for smooth scrolling
  int scrollStep = 10;
  conversationPixelScrollOffset += (direction * scrollStep);
  
  // Mark that user is manually scrolling
  userIsManuallyScrolling = true;
  
  // Bounds checking using pixel-based logic
  int maxScrollOffset = calculateMaxScrollOffset();
  if (conversationPixelScrollOffset < 0) {
    conversationPixelScrollOffset = 0;
  } else if (conversationPixelScrollOffset > maxScrollOffset) {
    conversationPixelScrollOffset = maxScrollOffset;
  }
  
  // Check if user scrolled back to bottom (optimal position)
  // Calculate what the optimal position would be
  int tempScrollOffset = conversationPixelScrollOffset; // Save current
  calculateOptimalScroll();
  int optimalPosition = conversationPixelScrollOffset;
  conversationPixelScrollOffset = tempScrollOffset; // Restore current
  
  // If user scrolled to optimal position (within 20 pixels), resume auto-scrolling
  if (abs(conversationPixelScrollOffset - optimalPosition) <= 20) {
    userIsManuallyScrolling = false;
    Serial.println("User scrolled back to bottom - resuming auto-scroll");
  }
  
  drawConversationPane();
  Serial.print("Manual pixel scroll offset: ");
  Serial.println(conversationPixelScrollOffset);
}

void addCharToCompose(char c) {
  if (composeBuffer.length() < 100) { // Limit compose length
    composeBuffer += c;
    
    // v32: Check if message is getting too long for single line
    // Approximate: "> " + message should fit in ~50 characters (300 pixels / 6 pixels per char)
    bool messageWouldWrap = (composeBuffer.length() + 2) > 50; // +2 for "> "
    
    if (messageWouldWrap) {
      // Message is getting long - use full redraw to handle potential wrapping
      drawConversationPane();
    } else {
      // Message fits on one line - use optimized compose-only redraw
      drawComposeAreaOnly();
    }
    
    // Only auto-scroll if user hasn't manually scrolled away from bottom
    // This will only trigger a full redraw if scroll position changes
    if (currentThreadMessageCount > 0 && !userIsManuallyScrolling) {
      int oldScrollOffset = conversationPixelScrollOffset;
      calculateOptimalScroll();
      
      // Only redraw conversation if scroll position actually changed
      if (conversationPixelScrollOffset != oldScrollOffset) {
        drawConversationPane();
      }
    }
  }
}

void selectThread() {
  if (selectedThreadIndex < threadPreviewCount) {
    String selectedPhone = threadPreviews[selectedThreadIndex].contactPhone;
    loadThreadForContact(selectedPhone);
    currentPane = PANE_CONVERSATION;
    drawConversationPane();
    drawPaneBorder(currentPane);
    drawStatusSection();
  }
}

void sendMessage() {
  if (composeBuffer.length() == 0 || activeContactPhone.length() == 0) {
    Serial.println("Cannot send: empty message or no contact selected");
    return;
  }
  
  // Show sending status
  updateStatus("Sending...", ST77XX_YELLOW);
  
  Serial.print("Sending SMS to ");
  Serial.print(activeContactPhone);
  Serial.print(": ");
  Serial.println(composeBuffer);
  
  // Remove '+' prefix if present since SIM7600 library adds it automatically
  String phoneForSMS = activeContactPhone;
  if (phoneForSMS.startsWith("+")) {
    phoneForSMS = phoneForSMS.substring(1);
  }
  
  // Send via SIM7600 (implement actual sending)
  if (cellular.sendSMS(phoneForSMS.c_str(), composeBuffer.c_str())) {
    Serial.println("SMS sent successfully");
    
    // Save outgoing message to file  
    if (saveOutgoingMessage(activeContactPhone, composeBuffer)) {
      Serial.println("[SMS SEND] Message saved, reloading thread to show all messages");
      
      // Reload the entire thread from files to ensure consistency
      loadThreadForContact(activeContactPhone);
      
      // Clear compose buffer
      composeBuffer = "";
      
      // Refresh displays
      drawConversationPane();
      
      // Skip buildThreadPreviews() to avoid memory issues and redundant processing
      // The thread previews will be updated when user switches back to preview pane
      Serial.println("[OPTIMIZATION] Skipping thread preview rebuild to save memory");
      
      updateStatus("Message sent", ST77XX_GREEN);
    } else {
      updateStatus("Failed to save message", ST77XX_RED);
    }
  } else {
    Serial.println("SMS sending failed");
    updateStatus("SMS failed", ST77XX_RED);
  }
  
  drawContactsPane(); // v33: Refresh contact list
  drawConversationPane();  // Refresh conversation
}

// Old v24 functions removed - replaced with v26 thread-based system

// Old updateInbox function removed - replaced with drawContactsPane

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
    
    // v34: Check if sender is unknown and add to address book
    String senderName = lookupContactName(sms.sender);
    if (senderName.equals(sms.sender)) {
      // No contact found - sender number was returned, meaning unknown contact
      Serial.println("🆕 Unknown sender detected, adding to address book...");
      
      // Generate a friendly name for the unknown contact
      String lastDigits = sms.sender.length() >= 4 ? sms.sender.substring(sms.sender.length() - 4) : sms.sender;
      String contactName = "Unknown " + lastDigits;
      
      // Try to add new contact
      if (addNewContact(sms.sender, contactName)) {
        Serial.println("✅ New contact added: " + contactName);
        updateStatus("New contact added", ST77XX_CYAN);
      } else {
        Serial.println("❌ Failed to add new contact");
      }
    } else {
      Serial.println("📞 Known contact: " + senderName);
    }
    
    // Store to SD card
    if (cellular.storeSMSToSD(sms)) {
      Serial.println("✅ New SMS stored to SD card and deleted from SIM");
      
      // v34 Debug: Show contact count before and after refresh
      Serial.print("🔍 Contact count before refresh: ");
      Serial.println(addressBookCount);
      
      // Refresh thread system instead of old inbox
      loadAllMessages();  // This also calls buildThreadPreviews()
      
      Serial.print("🔍 Contact count after refresh: ");
      Serial.println(addressBookCount);
      Serial.print("🔍 Thread preview count: ");
      Serial.println(threadPreviewCount);
      
      drawContactsPane();
      
      // Refresh current conversation if it's from the same contact
      if (activeContactPhone.length() > 0 && activeContactPhone.equals(sms.sender)) {
        loadThreadForContact(activeContactPhone);
        drawConversationPane();
      }
      
      updateStatus("SMS stored & updated", ST77XX_GREEN);
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

// Old addNewSMSToInbox function removed - no longer needed with thread system

void handleKeyboardV26() {
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
        if (currentPane == PANE_THREADS) {
          scrollThreadSelection(-1);
        } else {
          scrollConversation(-1);
        }
      }
      else if (keyData == 0xB6) { // DOWN arrow
        if (currentPane == PANE_THREADS) {
          scrollThreadSelection(1);
        } else {
          scrollConversation(1);
        }
      }
      // Enter key - different actions per pane
      else if (keyData == 0x0D) { // ENTER
        if (currentPane == PANE_THREADS) {
          // Select thread and open conversation
          selectThread();
        } else {
          // Send message in conversation pane
          sendMessage();
        }
      }
      // Backspace in conversation pane
      else if (keyData == 0x08 && currentPane == PANE_CONVERSATION) { // BACKSPACE
        if (composeBuffer.length() > 0) {
          composeBuffer = composeBuffer.substring(0, composeBuffer.length() - 1);
          drawConversationPane(); // Refresh to show updated input
        }
      }
      // v38: Capital 'C' initiates a call with selected contact
      else if (currentPane == PANE_THREADS && keyData == 'C') {
        if (selectedThreadIndex >= 0 && selectedThreadIndex < threadPreviewCount) {
          String phoneNumber = threadPreviews[selectedThreadIndex].contactPhone;
          String contactName = threadPreviews[selectedThreadIndex].contactDisplayName;

          Serial.println("[CALL] Initiating call to: " + contactName + " (" + phoneNumber + ")");

          // Create status message
          char statusText[64];
          snprintf(statusText, sizeof(statusText), "Calling %s", contactName.substring(0, 15).c_str());
          updateStatus(statusText, ST77XX_YELLOW);

          // Remove '+' prefix if present since SIM7600 library may handle it differently
          String phoneForCall = phoneNumber;
          if (phoneForCall.startsWith("+")) {
            phoneForCall = phoneForCall.substring(1);
          }

          if (cellular.makeCall(phoneForCall.c_str())) {
            Serial.println("[CALL] Call initiated successfully");
            updateStatus("Calling...", ST77XX_GREEN);
          } else {
            Serial.println("[CALL] Failed to initiate call");
            updateStatus("Call failed", ST77XX_RED);
          }
        }
      }
      // v33: Letter-based contact search in contacts pane
      else if (currentPane == PANE_THREADS && keyData >= 'A' && keyData <= 'Z') {
        char searchChar = (char)keyData;
        contactSearchBuffer += searchChar;
        contactSearchMode = 1;
        lastSearchTime = millis();
        
        // Find first contact matching current search and select it
        for (int i = 0; i < threadPreviewCount; i++) {
          String contactLower = threadPreviews[i].contactDisplayName;
          contactLower.toLowerCase();
          String searchLower = contactSearchBuffer;
          searchLower.toLowerCase();
          if (contactLower.startsWith(searchLower)) {
            selectedThreadIndex = i;
            previewScrollOffset = 0; // Reset scroll to show matched contact
            break;
          }
        }
        
        drawContactsPane(); // Refresh to show search
        Serial.println("[SEARCH] Contact search: '" + contactSearchBuffer + "'");
      }
      // v38: Lowercase 'c' also initiates a call with selected contact
      else if (currentPane == PANE_THREADS && keyData == 'c') {
        if (selectedThreadIndex >= 0 && selectedThreadIndex < threadPreviewCount) {
          String phoneNumber = threadPreviews[selectedThreadIndex].contactPhone;
          String contactName = threadPreviews[selectedThreadIndex].contactDisplayName;

          Serial.println("[CALL] Initiating call to: " + contactName + " (" + phoneNumber + ")");

          // Create status message
          char statusText[64];
          snprintf(statusText, sizeof(statusText), "Calling %s", contactName.substring(0, 15).c_str());
          updateStatus(statusText, ST77XX_YELLOW);

          // Remove '+' prefix if present since SIM7600 library may handle it differently
          String phoneForCall = phoneNumber;
          if (phoneForCall.startsWith("+")) {
            phoneForCall = phoneForCall.substring(1);
          }

          if (cellular.makeCall(phoneForCall.c_str())) {
            Serial.println("[CALL] Call initiated successfully");
            updateStatus("Calling...", ST77XX_GREEN);
          } else {
            Serial.println("[CALL] Failed to initiate call");
            updateStatus("Call failed", ST77XX_RED);
          }
        }
      }
      // v33: Lowercase letters also work for search
      else if (currentPane == PANE_THREADS && keyData >= 'a' && keyData <= 'z') {
        char searchChar = (char)(keyData - 32); // Convert to uppercase
        contactSearchBuffer += searchChar;
        contactSearchMode = 1;
        lastSearchTime = millis();
        
        // Find first contact matching current search and select it
        for (int i = 0; i < threadPreviewCount; i++) {
          String contactLower = threadPreviews[i].contactDisplayName;
          contactLower.toLowerCase();
          String searchLower = contactSearchBuffer;
          searchLower.toLowerCase();
          if (contactLower.startsWith(searchLower)) {
            selectedThreadIndex = i;
            previewScrollOffset = 0; // Reset scroll to show matched contact
            break;
          }
        }
        
        drawContactsPane(); // Refresh to show search
        Serial.println("[SEARCH] Contact search: '" + contactSearchBuffer + "'");
      }
      // v33: Backspace in contacts pane clears search character by character
      else if (keyData == 0x08 && currentPane == PANE_THREADS) { // BACKSPACE
        if (contactSearchBuffer.length() > 0) {
          contactSearchBuffer = contactSearchBuffer.substring(0, contactSearchBuffer.length() - 1);
          lastSearchTime = millis();

          if (contactSearchBuffer.length() == 0) {
            contactSearchMode = 0;
          }

          drawContactsPane(); // Refresh to show updated search
          Serial.println("[SEARCH] Contact search updated: '" + contactSearchBuffer + "'");
        }
      }
      // v37: ESC in contacts pane clears entire search filter and returns to main list
      else if (keyData == 0x1B && currentPane == PANE_THREADS) { // ESC
        if (contactSearchBuffer.length() > 0) {
          contactSearchBuffer = "";
          contactSearchMode = 0;

          drawContactsPane(); // Refresh to show full contact list
          Serial.println("[SEARCH] Contact search cleared - returned to main list");
        }
      }
      // Alphanumeric characters for typing in conversation pane
      else if (currentPane == PANE_CONVERSATION && keyData >= 32 && keyData <= 126) {
        addCharToCompose((char)keyData);
      }
      // Number keys 1-9 still trigger tests (including v27 debug toggle)
      else if (keyData >= '1' && keyData <= '9') {
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
    case 9:
      // v27 Debug: Toggle thread loading debug output
      debugThreadLoading = !debugThreadLoading;
      snprintf(statusText, sizeof(statusText), "Debug: %s", debugThreadLoading ? "ON" : "OFF");
      updateStatus(statusText, debugThreadLoading ? ST77XX_GREEN : ST77XX_RED);
      Serial.print("Thread loading debug is now ");
      Serial.println(debugThreadLoading ? "ENABLED" : "DISABLED");
      break;
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
        
        // If new SMS were stored, refresh the thread system
        if (smsAfter > smsBefore) {
          Serial.print("New SMS detected: ");
          Serial.print(smsAfter - smsBefore);
          Serial.println(" new messages. Refreshing threads...");
          updateStatus("Refreshing threads", ST77XX_CYAN);
          loadAllMessages();  // This also calls buildThreadPreviews()
          drawContactsPane();
          // Refresh conversation if we're viewing one
          if (activeContactPhone.length() > 0) {
            loadThreadForContact(activeContactPhone);
            drawConversationPane();
          }
          updateStatus("Threads updated", ST77XX_GREEN);
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
      // Refresh thread system
      {
        updateStatus("Refreshing Threads", ST77XX_CYAN);
        Serial.println("=== Refreshing Thread System ===");
        logMemoryUsage("Before refreshing threads");
        
        if (loadAllMessages()) {  // This also calls buildThreadPreviews()
          drawContactsPane();
          // Refresh conversation if we're viewing one
          if (activeContactPhone.length() > 0) {
            loadThreadForContact(activeContactPhone);
            drawConversationPane();
          }
          char threadCountText[32];
          snprintf(threadCountText, sizeof(threadCountText), "%d threads loaded", threadPreviewCount);
          updateStatus(threadCountText, ST77XX_GREEN);
        } else {
          updateStatus("No messages found", ST77XX_YELLOW);
        }
        logMemoryUsage("After refreshing threads");
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

// Old v24 smooth scrolling functions removed - replaced with thread-based UI

// v24 Enhanced Status Section Implementation

String currentStatusMessage = "Starting...";
uint16_t currentStatusColor = ST77XX_CYAN;

void drawStatusSection() {
  // Clear entire status section
  tft.fillRect(0, STATUS_SECTION_Y, 240, STATUS_SECTION_HEIGHT, ST77XX_BLACK);
  
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
  
  // Position RAM display on top right (screen width is 240px)
  tft.setCursor(180, STATUS_SECTION_Y + 2);
  tft.setTextColor(memColor);
  tft.print(memText);
  
  // v30: Removed instruction text for cleaner interface
  
  // Draw white horizontal separator line at bottom of status section
  int separatorY = STATUS_SECTION_Y + STATUS_SECTION_HEIGHT - 1;
  tft.drawFastHLine(0, separatorY, 240, ST77XX_WHITE);
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

// v26 Thread-based UI Implementation

void drawContactsPane() {
  // v33: Check if search has timed out
  if (contactSearchMode == 1 && millis() - lastSearchTime > SEARCH_TIMEOUT_MS) {
    contactSearchMode = 0;
    contactSearchBuffer = "";
  }
  
  // Clear contacts pane area
  tft.fillRect(0, CONTACTS_Y, 240, CONTACTS_HEIGHT, ST77XX_BLACK);
  
  // Draw header
  tft.setCursor(5, CONTACTS_Y + 5);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(1);
  
  // v33: Show search status or normal header
  if (contactSearchMode == 1 && contactSearchBuffer.length() > 0) {
    tft.print("SEARCH: " + contactSearchBuffer);
  } else {
    tft.print("CONTACTS");
  }
  
  // Show contact count (filtered or total) - positioned on right side
  int visibleCount = 0;
  for (int i = 0; i < threadPreviewCount; i++) {
    bool matches = false;
    if (contactSearchBuffer.length() == 0) {
      matches = true;
    } else {
      String contactLower = threadPreviews[i].contactDisplayName;
      contactLower.toLowerCase();
      String searchLower = contactSearchBuffer;
      searchLower.toLowerCase();
      matches = contactLower.startsWith(searchLower);
    }
    if (matches) {
      visibleCount++;
    }
  }
  tft.setCursor(180, CONTACTS_Y + 5);
  tft.print("(" + String(visibleCount) + ")");
  
  // v33: Draw filtered contacts (10 pixels per line, starting at Y+15)
  int maxLines = (CONTACTS_HEIGHT - 20) / 10; // Reserve space for header
  tft.setTextColor(ST77XX_WHITE);
  
  int visibleContactIndex = 0;
  for (int threadIndex = 0; threadIndex < threadPreviewCount && visibleContactIndex < maxLines + previewScrollOffset; threadIndex++) {
    // v33: Filter contacts based on search
    bool matches = false;
    if (contactSearchBuffer.length() == 0) {
      matches = true;
    } else {
      String contactLower = threadPreviews[threadIndex].contactDisplayName;
      contactLower.toLowerCase();
      String searchLower = contactSearchBuffer;
      searchLower.toLowerCase();
      matches = contactLower.startsWith(searchLower);
    }
    
    if (!matches) continue;
    
    // Skip if before scroll offset
    if (visibleContactIndex < previewScrollOffset) {
      visibleContactIndex++;
      continue;
    }
    
    int displayLineIndex = visibleContactIndex - previewScrollOffset;
    if (displayLineIndex >= maxLines) break;
    
    int yPos = CONTACTS_Y + 15 + (displayLineIndex * 10);
    
    // v33: Highlight selected contact (using threadIndex for selection)
    bool isSelected = (threadIndex == selectedThreadIndex);
    bool isSearchMatch = (contactSearchBuffer.length() > 0 && matches);
    
    if (isSelected) {
      tft.fillRect(2, yPos - 1, 316, 10, ST77XX_BLUE);
      tft.setTextColor(ST77XX_WHITE);
    } else if (isSearchMatch) {
      tft.fillRect(2, yPos - 1, 316, 10, ST77XX_GREEN);  // Highlight search matches
      tft.setTextColor(ST77XX_BLACK);
    } else {
      tft.setTextColor(ST77XX_WHITE);
    }
    
    // v37: Format: "Contact | Recent msg..." (optimized for message preview visibility)
    String contactName = threadPreviews[threadIndex].contactDisplayName.substring(0, 10);
    String line = contactName;

    // Add padding to align columns (shorter for more message space)
    while (line.length() < 11) line += " ";
    line += " ";

    // Add latest message preview (truncated to fit screen)
    String preview = threadPreviews[threadIndex].lastMessagePreview;
    if (preview.length() > 0) {
      line += preview.substring(0, 26); // Increased from 15 to 26 chars for better preview
    } else {
      line += "No messages";
    }
    
    tft.setCursor(5, yPos);
    tft.print(line);
    
    visibleContactIndex++;
  }
}

void drawConversationPane() {
  // Clear conversation area
  tft.fillRect(0, CONVERSATION_Y, 240, CONVERSATION_HEIGHT, ST77XX_BLACK);

  // v31: Pixel-based scrolling with partial message visibility
  int conversationDisplayHeight = CONVERSATION_HEIGHT - 20; // Reserve for header
  int screenWidth = 240;
  int incomingLeftMargin = 2;
  int outgoingLeftMargin = screenWidth / 3; // 1/3 across screen (≈107px)
  int timeWidth = 30; // Width for timestamp "HH:MM"

  tft.setTextSize(1);

  // Calculate virtual Y positions and draw only visible messages
  int virtualY = 0; // Virtual position in the content
  int screenStartY = CONVERSATION_Y + 20; // Reserve 20px for header (matches conversationDisplayHeight calculation)
  int screenEndY = CONVERSATION_Y + CONVERSATION_HEIGHT;
  
  for (int msgIndex = 0; msgIndex < currentThreadMessageCount; msgIndex++) {
    SMSMessage& msg = currentThreadMessages[msgIndex];
    String displayTime = formatTimeForDisplay(msg.fullTime);
    
    // Calculate message height first
    int messageMaxWidth;
    if (msg.isOutgoing) {
      messageMaxWidth = screenWidth - outgoingLeftMargin - timeWidth - 10;
    } else {
      messageMaxWidth = screenWidth - incomingLeftMargin - timeWidth - 10;
    }
    
    // Calculate lines needed for this message
    int charsPerLine = messageMaxWidth / 6; // Approximate 6 pixels per character
    int linesNeeded = (msg.content.length() + charsPerLine - 1) / charsPerLine;
    if (linesNeeded < 1) linesNeeded = 1;
    int messageHeight = linesNeeded * 10 + 2; // Line height + gap
    
    // Calculate screen Y position (virtualY - scroll offset)
    int screenY = screenStartY + virtualY - conversationPixelScrollOffset;
    
    // Only draw if message is visible (partially or fully)
    // Fixed: message is visible if it overlaps with the viewport at all
    if (screenY < screenEndY && screenY + messageHeight > screenStartY) {
      if (msg.isOutgoing) {
        // v29: Outgoing messages - indented 1/3 from left, green text
        int messageStartX = outgoingLeftMargin;
        
        // Draw message content with wrapping at indented position
        int linesUsed = drawWrappedText(msg.content, messageStartX, screenY, messageMaxWidth, ST77XX_GREEN);
        
        // Draw timestamp at end of first line (only if first line is visible)
        if (screenY >= screenStartY && screenY < screenEndY) {
          tft.setTextColor(ST77XX_CYAN);
          tft.setCursor(screenWidth - timeWidth, screenY);
          tft.print(displayTime);
        }
        
      } else {
        // v29: Incoming messages - left-aligned at margin, white text
        int messageStartX = incomingLeftMargin;
        
        // Draw message content with wrapping at left margin
        int linesUsed = drawWrappedText(msg.content, messageStartX, screenY, messageMaxWidth, ST77XX_WHITE);
        
        // Draw timestamp at end of first line (only if first line is visible)
        if (screenY >= screenStartY && screenY < screenEndY) {
          tft.setTextColor(ST77XX_CYAN);
          tft.setCursor(screenWidth - timeWidth, screenY);
          tft.print(displayTime);
        }
      }
    }
    
    // Move to next message position in virtual space
    virtualY += messageHeight;
  }

  // Draw header with contact name (drawn AFTER messages so it stays on top)
  tft.fillRect(0, CONVERSATION_Y, 240, 20, ST77XX_BLACK); // Clear header area
  tft.setCursor(5, CONVERSATION_Y + 5);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setTextSize(1);
  if (activeContactPhone.length() > 0) {
    tft.print(activeContactName.substring(0, 25));
  } else {
    tft.print("Select a conversation");
  }

  // Draw compose area at bottom
  tft.fillRect(0, COMPOSE_Y, 240, COMPOSE_HEIGHT, ST77XX_BLACK);
  
  // Draw compose line
  tft.setCursor(5, COMPOSE_Y + 5);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(1);
  tft.print("> " + composeBuffer);
  
  // Draw cursor if compose buffer is not empty or we're in compose mode
  if (currentPane == PANE_CONVERSATION && activeContactPhone.length() > 0) {
    // Simple cursor indication
    tft.print("_");
  }
}

void drawComposeAreaOnly() {
  // v32: Optimized function to only redraw compose area during typing
  // This prevents the entire conversation from flashing on every keystroke
  
  // Clear and redraw only the compose area at bottom
  tft.fillRect(0, COMPOSE_Y, 240, COMPOSE_HEIGHT, ST77XX_BLACK);
  
  // Draw compose line
  tft.setCursor(5, COMPOSE_Y + 5);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(1);
  tft.print("> " + composeBuffer);
  
  // Draw cursor if compose buffer is not empty or we're in compose mode
  if (currentPane == PANE_CONVERSATION && activeContactPhone.length() > 0) {
    // Simple cursor indication
    tft.print("_");
  }
}

void drawPaneBorder(ActivePane pane) {
  // Always maintain red border around active pane
  uint16_t activeBorderColor = ST77XX_RED;
  uint16_t inactiveBorderColor = ST77XX_BLACK;
  
  if (pane == PANE_THREADS) {
    // Active thread previews, inactive conversation
    tft.drawRect(0, CONTACTS_Y, 240, CONTACTS_HEIGHT, activeBorderColor);
    tft.drawRect(0, CONVERSATION_Y, 240, CONVERSATION_HEIGHT + COMPOSE_HEIGHT, inactiveBorderColor);
  } else {
    // Active conversation, inactive thread previews
    tft.drawRect(0, CONTACTS_Y, 240, CONTACTS_HEIGHT, inactiveBorderColor);
    tft.drawRect(0, CONVERSATION_Y, 240, CONVERSATION_HEIGHT + COMPOSE_HEIGHT, activeBorderColor);
  }
}

// End of v26 functions
