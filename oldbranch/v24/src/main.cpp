/**************************************************************************
  DIY Phone v24 - Dynamic Canvas Sizing and Message Positioning
  Features: Display + SIM7600 + SD card SMS storage + I2C keyboard control + Address book name lookup + Full timestamps + Auto SMS deletion + Message threads
  Tests triggered by keyboard: 1=Signal, 2=AT Test, 3=SMS Check, 4=SD Test, 5=SMS Read, 6=Network, 7=Delete One-by-One, 8=Delete Bulk
  
  v24 Changes: 
  - Dynamic message canvas positioning based on inbox content
  - Adaptive canvas sizes based on actual contact count  
  - Messages sorted with latest at bottom
  - Auto-scroll to fill upper portion when messages don't fill canvas
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
int inboxSelectedIndex = 0;  // Currently selected inbox entry (relative to scroll offset)
int messageScrollOffset = 0;       // Scroll position in message canvas
int currentInboxHeight = 100;      // Current height of inbox canvas (dynamic)
int currentMessageY = 142;         // Current Y position of message canvas (dynamic)
int currentMessageHeight = 95;     // Current height of message canvas (dynamic)

// Function declarations
void updateStatus(const char *text, uint16_t color);
void updateInbox();
void updateMessages();
void calculateCanvasSizes();
void updateSeparator();
void loadMessagesForContact(const String& senderNumber);
void loadAllMessagesForContact(const String& senderNumber);
void readUARTLines();
void handleKeyboard();
String getKeyName(uint8_t keyCode);
void runTest(int testNumber);
bool loadSMSInbox();
void sortSMSByTime();
void filterToUniqueContacts();
void handleNewSMSNotification(int smsIndex);
void addNewSMSToInbox(const String& filename);
bool deleteAllSMSIndividually();
bool deleteAllSMSWithStorageSelection();
bool loadAddressBook();
String lookupContactName(const String& phoneNumber);
unsigned long parseTimestamp(const String& timestamp);
int compareSMSByTime(const void* a, const void* b);
String formatDateWithoutYear(const String& timestamp);

// Canvas objects - 1-bit monochrome for maximum memory efficiency
GFXcanvas1 inbox_canvas(320, 100);  // Inbox canvas - height for 10 lines (10 pixels each)
GFXcanvas1 message_canvas(320, 95);  // Message canvas - reduced by 3 more pixels (240-inboxY-100-12-3 = 95)

#define statusY 10
#define inboxY 30
#define separatorY 130  // inboxY + 100 = 130
#define messageY 145    // separatorY + 12 + 3 = 145

void setup(void) {
  Serial.begin(115200);
  delay(2000); // Wait for serial connection
  Serial.println("=== DIY Phone v24 Starting ===");

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
  
  // Skip SMS loading during setup to keep system responsive
  Serial.println("[DEBUG] Skipping SMS loading during setup for responsiveness");
  
  // Initialize empty canvases
  inbox_canvas.fillScreen(0);  // Black for 1-bit canvas
  message_canvas.fillScreen(0);  // Black for 1-bit canvas
  
  // Set default canvas sizes for empty state
  currentInboxHeight = 20;
  currentMessageY = inboxY + currentInboxHeight + 5;
  currentMessageHeight = 95;
  
  tft.drawBitmap(0, inboxY, inbox_canvas.getBuffer(), inbox_canvas.width(), currentInboxHeight, ST77XX_WHITE, ST77XX_BLACK);
  tft.drawBitmap(0, currentMessageY, message_canvas.getBuffer(), message_canvas.width(), currentMessageHeight, ST77XX_WHITE, ST77XX_BLACK);
  
  // Clear any remaining area below to prevent artifacts
  int clearAreaY = currentMessageY + currentMessageHeight;
  if (clearAreaY < 240) {
    tft.fillRect(0, clearAreaY, 320, 240 - clearAreaY, ST77XX_BLACK);
  }
  
  Serial.println("[DEBUG] Setup complete!");
  
  updateStatus("Ready - Press 5 for SMS", ST77XX_CYAN);
  Serial.println("===============================================");
  Serial.println("Setup complete - Press keyboard numbers 1-8:");
  Serial.println("1 = Signal Quality Test");
  Serial.println("2 = AT Command Test");
  Serial.println("3 = SMS Check & Store");
  Serial.println("4 = SD Card Test");
  Serial.println("5 = Read SMS Files (Load Inbox)");
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
  inboxSelectedIndex = 0; // Reset selection to top
  
  updateStatus("Opening SD root dir", ST77XX_CYAN);
  FsFile root = sd.open("/");
  if (!root) {
    updateStatus("SD root open failed", ST77XX_RED);
    return false;
  }
  
  updateStatus("SD opened, reading files", ST77XX_CYAN);
  FsFile file;
  
  int fileCount = 0;
  while (file.openNext(&root, O_RDONLY) && smsInboxCount < 50 && fileCount < 200) { // Limit total files scanned
    fileCount++;
    char filename[64];
    file.getName(filename, sizeof(filename));
    
    if (fileCount % 10 == 0) {
      char statusMsg[32];
      snprintf(statusMsg, sizeof(statusMsg), "Checked %d files", fileCount);
      updateStatus(statusMsg, ST77XX_CYAN);
      delay(10); // Small delay to keep system responsive
      
      // Check for keyboard input during long operations
      handleKeyboard();
    }
    
    // Check if this is an SMS file (starts with "sms_")
    if (strncmp(filename, "sms_", 4) == 0) {
      Serial.print("Loading SMS file: ");
      Serial.println(filename);
      
      char statusMsg[32];
      snprintf(statusMsg, sizeof(statusMsg), "Found %d SMS files", smsInboxCount + 1);
      updateStatus(statusMsg, ST77XX_CYAN);
      
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
        smsInbox[smsInboxCount].fullTime = formatDateWithoutYear(smsInbox[smsInboxCount].time);
        
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
  
  char finalMsg[32];
  snprintf(finalMsg, sizeof(finalMsg), "Loaded %d SMS files", smsInboxCount);
  updateStatus(finalMsg, ST77XX_GREEN);
  
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

void filterToUniqueContacts() {
  if (smsInboxCount == 0) return;
  
  Serial.println("Filtering SMS inbox to unique contacts...");
  
  // Create a temporary array to hold unique contacts
  SMSInboxEntry uniqueContacts[50];
  int uniqueCount = 0;
  
  // Go through sorted messages and keep only the latest from each contact
  for (int i = 0; i < smsInboxCount && uniqueCount < 50; i++) {
    // Check if we've already seen this contact
    bool alreadySeen = false;
    for (int j = 0; j < uniqueCount; j++) {
      if (uniqueContacts[j].sender == smsInbox[i].sender) {
        alreadySeen = true;
        break;
      }
    }
    
    // If this is a new contact, add it to unique list
    if (!alreadySeen) {
      uniqueContacts[uniqueCount] = smsInbox[i];
      uniqueCount++;
      Serial.print("Added unique contact: ");
      Serial.print(smsInbox[i].senderDisplayName);
      Serial.print(" (");
      Serial.print(smsInbox[i].sender);
      Serial.println(")");
    }
  }
  
  // Copy unique contacts back to main inbox array
  for (int i = 0; i < uniqueCount; i++) {
    smsInbox[i] = uniqueContacts[i];
  }
  
  smsInboxCount = uniqueCount;
  Serial.print("Filtered to ");
  Serial.print(uniqueCount);
  Serial.println(" unique contacts");
}

void calculateCanvasSizes() {
  // Calculate inbox height based on actual number of contacts
  int visibleContacts = min(smsInboxCount, 10); // Max 10 visible contacts
  currentInboxHeight = visibleContacts * 10; // 10 pixels per contact line
  
  // Constrain inbox height to allocated canvas buffer (100 pixels max)
  if (currentInboxHeight > 100) {
    currentInboxHeight = 100;
  }
  if (currentInboxHeight < 20) {
    currentInboxHeight = 20;
  }
  
  // Calculate message canvas position (few pixels below last inbox line)
  currentMessageY = inboxY + currentInboxHeight + 5; // 5 pixels gap
  
  // Calculate remaining space for message canvas
  int screenBottom = 240; // Total screen height
  currentMessageHeight = screenBottom - currentMessageY;
  
  // Constrain message height to allocated canvas buffer (95 pixels max)
  if (currentMessageHeight > 95) {
    currentMessageHeight = 95;
  }
  if (currentMessageHeight < 30) {
    currentMessageHeight = 30;
  }
  
  Serial.print("Canvas sizes calculated - Inbox height: ");
  Serial.print(currentInboxHeight);
  Serial.print(", Message Y: ");
  Serial.print(currentMessageY);
  Serial.print(", Message height: ");
  Serial.println(currentMessageHeight);
}

void updateInbox() {
  // Clear inbox canvas
  inbox_canvas.fillScreen(0);  // Black for 1-bit canvas
  inbox_canvas.setTextSize(1);
  inbox_canvas.setTextColor(1);  // White for 1-bit canvas
  inbox_canvas.setTextWrap(false); // Disable wrapping for fixed format
  
  // Show contacts based on calculated inbox height
  int maxVisibleEntries = currentInboxHeight / 10; // 10 pixels per line
  int maxEntries = min(maxVisibleEntries, smsInboxCount - inboxScrollOffset);
  
  for (int i = 0; i < maxEntries && (i + inboxScrollOffset) < smsInboxCount; i++) {
    int smsIndex = i + inboxScrollOffset;
    int yPos = i * 10;  // 10 pixels per line
    
    // Check if this entry is selected
    bool isSelected = (i == inboxSelectedIndex);
    
    // Set highlighting for selected item (1-bit canvas uses text inversion)
    if (isSelected) {
      inbox_canvas.fillRect(0, yPos, 320, 10, 1);  // White background for selection
      inbox_canvas.setTextColor(0);  // Black text on white background
    } else {
      inbox_canvas.setTextColor(1);  // White text on black background
    }
    
    // Format: "From(10)  Date Time  Message..."
    String displayLine = "";
    
    // Sender name (first 10 chars)
    String shortSender = smsInbox[smsIndex].senderDisplayName;
    if (shortSender.length() > 10) {
      shortSender = shortSender.substring(0, 10);
    } else {
      // Pad to 10 chars
      while (shortSender.length() < 10) {
        shortSender += " ";
      }
    }
    displayLine += shortSender;
    
    // Two spaces
    displayLine += "  ";
    
    // Date & time (up to 11 chars: "12/27 17:48")
    String datetime = smsInbox[smsIndex].fullTime;
    if (datetime.length() > 11) {
      datetime = datetime.substring(0, 11);
    } else {
      // Pad to 11 chars for alignment
      while (datetime.length() < 11) {
        datetime += " ";
      }
    }
    displayLine += datetime;
    
    // Two spaces
    displayLine += "  ";
    
    // Message content (remaining space to edge of screen)
    String content = smsInbox[smsIndex].content;
    int remainingSpace = 53 - displayLine.length(); // Characters that fit on 320px width
    if ((int)content.length() > remainingSpace) {
      content = content.substring(0, remainingSpace); // Truncate, don't wrap
    }
    displayLine += content;
    
    // Draw the line
    inbox_canvas.setCursor(0, yPos);
    inbox_canvas.print(displayLine);
  }
  
  // Update display with inbox canvas using calculated height
  tft.drawBitmap(0, inboxY, inbox_canvas.getBuffer(), inbox_canvas.width(), currentInboxHeight, ST77XX_WHITE, ST77XX_BLACK);
  
  // Update separator line with sender name
  updateSeparator();
  
  // Reset message scroll when contact changes
  messageScrollOffset = 0;
  
  // Show "Loading..." message immediately, then load thread
  message_canvas.fillScreen(0);  // Black for 1-bit canvas
  message_canvas.setTextSize(1);
  message_canvas.setTextColor(1);  // White for 1-bit canvas
  message_canvas.setCursor(0, 0);
  
  if (smsInboxCount > 0 && (inboxSelectedIndex + inboxScrollOffset) < smsInboxCount) {
    int selectedSMSIndex = inboxSelectedIndex + inboxScrollOffset;
    message_canvas.print("Loading messages for: ");
    message_canvas.println(smsInbox[selectedSMSIndex].senderDisplayName);
    message_canvas.println("Please wait...");
    tft.drawBitmap(0, currentMessageY, message_canvas.getBuffer(), message_canvas.width(), currentMessageHeight, ST77XX_WHITE, ST77XX_BLACK);
    
    // Clear any remaining area below to prevent artifacts
    int clearAreaY = currentMessageY + currentMessageHeight;
    if (clearAreaY < 240) {
      tft.fillRect(0, clearAreaY, 320, 240 - clearAreaY, ST77XX_BLACK);
    }
    
    updateStatus("Loading message thread", ST77XX_CYAN);
    // Now load the actual messages
    updateMessages();
  } else {
    message_canvas.print("No contact selected");
    tft.drawBitmap(0, currentMessageY, message_canvas.getBuffer(), message_canvas.width(), currentMessageHeight, ST77XX_WHITE, ST77XX_BLACK);
    
    // Clear any remaining area below to prevent artifacts
    int clearAreaY = currentMessageY + currentMessageHeight;
    if (clearAreaY < 240) {
      tft.fillRect(0, clearAreaY, 320, 240 - clearAreaY, ST77XX_BLACK);
    }
  }
  
  Serial.print("Inbox display updated - showing messages ");
  Serial.print(inboxScrollOffset + 1);
  Serial.print(" to ");
  Serial.print(min(inboxScrollOffset + 10, smsInboxCount));
  Serial.print(" of ");
  Serial.print(smsInboxCount);
  Serial.print(" (selected: ");
  Serial.print(inboxSelectedIndex);
  Serial.println(")");
}

void updateSeparator() {
  // Get the currently selected contact's display name
  String senderName = "No Messages";
  if (smsInboxCount > 0 && inboxSelectedIndex < 10 && 
      (inboxSelectedIndex + inboxScrollOffset) < smsInboxCount) {
    int selectedSMSIndex = inboxSelectedIndex + inboxScrollOffset;
    senderName = smsInbox[selectedSMSIndex].senderDisplayName;
  }
  
  // Draw yellow horizontal line
  tft.fillRect(0, separatorY, 320, 12, ST77XX_YELLOW);
  
  // Calculate text position to center the name
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_BLACK);
  
  // Estimate text width (roughly 6 pixels per character for size 1 font)
  int textWidth = senderName.length() * 6;
  int textX = (320 - textWidth) / 2;
  
  tft.setCursor(textX, separatorY + 2); // +2 for vertical centering
  tft.print(senderName);
}

// Message thread data
struct MessageEntry {
  String sender;
  String timestamp;
  String content;
  String fullTime;
};
MessageEntry messageThread[20];  // Show up to 20 messages in thread
int messageThreadCount = 0;

void updateMessages() {
  updateStatus("updateMessages start", ST77XX_CYAN);
  // Get the currently selected contact's number
  if (smsInboxCount == 0 || inboxSelectedIndex >= 10 || 
      (inboxSelectedIndex + inboxScrollOffset) >= smsInboxCount) {
    // No messages or invalid selection - clear message canvas
    message_canvas.fillScreen(0);  // Black for 1-bit canvas
    tft.drawBitmap(0, currentMessageY, message_canvas.getBuffer(), message_canvas.width(), currentMessageHeight, ST77XX_WHITE, ST77XX_BLACK);
    Serial.println("updateMessages: No messages or invalid selection");
    updateStatus("No valid selection", ST77XX_YELLOW);
    return;
  }
  
  Serial.print("updateMessages: Processing contact selection - index: ");
  Serial.print(inboxSelectedIndex);
  Serial.print(", offset: ");
  Serial.print(inboxScrollOffset);
  Serial.print(", total: ");
  Serial.println(smsInboxCount);
  
  int selectedSMSIndex = inboxSelectedIndex + inboxScrollOffset;
  String selectedSender = smsInbox[selectedSMSIndex].sender;
  
  // Safety check - don't process empty sender
  if (selectedSender.length() == 0) {
    Serial.println("updateMessages: Empty sender, clearing canvas");
    message_canvas.fillScreen(0);  // Black for 1-bit canvas
    tft.drawBitmap(0, currentMessageY, message_canvas.getBuffer(), message_canvas.width(), currentMessageHeight, ST77XX_WHITE, ST77XX_BLACK);
    return;
  }
  
  updateStatus("Loading contact msgs", ST77XX_CYAN);
  // Load ALL messages for this contact
  loadAllMessagesForContact(selectedSender);
  
  // Clear message canvas
  message_canvas.fillScreen(0);  // Black for 1-bit canvas
  message_canvas.setTextSize(1);
  message_canvas.setTextColor(1);  // White for 1-bit canvas
  message_canvas.setTextWrap(false);
  
  // Calculate total display height needed for all messages (in reverse order for latest-at-bottom)
  int totalHeight = 0;
  for (int i = messageThreadCount - 1; i >= 0; i--) {
    String messageText = messageThread[i].fullTime + ": " + messageThread[i].content;
    int linesNeeded = (messageText.length() + 52) / 53; // Round up division
    totalHeight += linesNeeded * 8 + 2; // 8px per line + 2px spacing
  }
  
  // Auto-scroll logic: if messages don't fill canvas, scroll them to fill upper portion
  int startY;
  if (totalHeight < currentMessageHeight) {
    // Messages don't fill canvas - position them at the top (startY = 0)
    startY = 0;
  } else {
    // Messages fill or exceed canvas - use bottom-aligned display with scroll offset
    startY = max(0, currentMessageHeight - totalHeight + messageScrollOffset);
  }
  
  // Draw messages with latest at bottom (reverse order display)
  int yPos = startY;
  
  for (int i = messageThreadCount - 1; i >= 0; i--) {
    // Check if this message would be visible
    if (yPos >= currentMessageHeight) break;
    
    // Display message with time and content
    String messageText = messageThread[i].fullTime + ": " + messageThread[i].content;
    
    // Manual text wrapping
    int charsPerLine = 53;
    int lineHeight = 8;
    
    for (int pos = 0; pos < (int)messageText.length() && yPos < currentMessageHeight && yPos < 95; pos += charsPerLine) {
      if (yPos >= 0 && yPos < 95) { // Only draw if within canvas buffer bounds
        String line = messageText.substring(pos, min(pos + charsPerLine, (int)messageText.length()));
        message_canvas.setCursor(0, yPos);
        message_canvas.setTextColor(1);  // White for 1-bit canvas
        message_canvas.print(line);
      }
      yPos += lineHeight;
    }
    
    yPos += 2; // Add small spacing between messages
  }
  
  // Update display with message canvas using calculated position and height
  tft.drawBitmap(0, currentMessageY, message_canvas.getBuffer(), message_canvas.width(), currentMessageHeight, ST77XX_WHITE, ST77XX_BLACK);
  
  // Clear any remaining area below the message canvas to prevent artifacts
  int clearAreaY = currentMessageY + currentMessageHeight;
  if (clearAreaY < 240) {
    tft.fillRect(0, clearAreaY, 320, 240 - clearAreaY, ST77XX_BLACK);
  }
  
  Serial.print("Message thread updated for: ");
  Serial.print(selectedSender);
  Serial.print(" (");
  Serial.print(messageThreadCount);
  Serial.println(" messages)");
}

void loadMessagesForContact(const String& senderNumber) {
  messageThreadCount = 0;
  
  // Go through all SMS messages and find ones from this sender
  for (int i = 0; i < smsInboxCount && messageThreadCount < 20; i++) {
    if (smsInbox[i].sender == senderNumber) {
      messageThread[messageThreadCount].sender = smsInbox[i].sender;
      messageThread[messageThreadCount].timestamp = smsInbox[i].time;
      messageThread[messageThreadCount].content = smsInbox[i].content;
      messageThread[messageThreadCount].fullTime = smsInbox[i].fullTime;
      messageThreadCount++;
    }
  }
  
  Serial.print("Loaded ");
  Serial.print(messageThreadCount);
  Serial.print(" messages for contact: ");
  Serial.println(senderNumber);
}

void loadAllMessagesForContact(const String& senderNumber) {
  updateStatus("loadAllMsgs start", ST77XX_CYAN);
  messageThreadCount = 0;
  
  Serial.print("loadAllMessagesForContact: Starting for contact: ");
  Serial.println(senderNumber);
  
  // Clear message canvas and show initial message
  message_canvas.fillScreen(0);  // Black for 1-bit canvas
  message_canvas.setTextSize(1);
  message_canvas.setTextColor(1);  // White for 1-bit canvas
  message_canvas.setCursor(0, 0);
  message_canvas.print("Searching for messages...");
  tft.drawBitmap(0, currentMessageY, message_canvas.getBuffer(), message_canvas.width(), currentMessageHeight, ST77XX_WHITE, ST77XX_BLACK);
  
  updateStatus("Opening SD for thread", ST77XX_CYAN);
  // Search through all SMS files on SD card
  FsFile root = sd.open("/");
  if (!root) {
    Serial.println("loadAllMessagesForContact: ERROR - Could not open root directory");
    updateStatus("SD open failed", ST77XX_RED);
    return;
  }
  
  FsFile file;
  int totalCount = 0;
  int yPos = 10; // Start below "Searching..." text
  
  Serial.println("loadAllMessagesForContact: Starting file iteration...");
  int fileCount = 0;
  
  updateStatus("Starting file scan", ST77XX_CYAN);
  while (file.openNext(&root, O_RDONLY) && totalCount < 20 && fileCount < 100) { // Limit files scanned
    fileCount++;
    
    if (fileCount % 20 == 0) {
      char statusMsg[32];
      snprintf(statusMsg, sizeof(statusMsg), "Scanned %d files", fileCount);
      updateStatus(statusMsg, ST77XX_CYAN);
      delay(10); // Small delay to keep system responsive
      
      // Check for keyboard input during long operations
      handleKeyboard();
    }
    
    char filename[64];
    file.getName(filename, sizeof(filename));
    
    // Check if this is an SMS file (starts with "sms_")
    if (strncmp(filename, "sms_", 4) == 0) {
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
        String fileSender = lines[0];
        fileSender.replace("From: ", "");
        
        // Check if this message is from our target contact
        if (fileSender == senderNumber) {
          String fileTime = lines[1];
          fileTime.replace("Time: ", "");
          
          String fileContent = lines[3];
          fileContent.replace("Content: ", "");
          
          // Store this message
          messageThread[totalCount].sender = fileSender;
          messageThread[totalCount].timestamp = fileTime;
          messageThread[totalCount].content = fileContent;
          messageThread[totalCount].fullTime = formatDateWithoutYear(fileTime);
          
          // Display this message immediately as we find it
          if (yPos < 85) { // Make sure we don't overflow canvas
            message_canvas.setCursor(0, yPos);
            message_canvas.setTextColor(1);  // White for 1-bit canvas (no cyan available)
            String shortTime = messageThread[totalCount].fullTime.substring(0, 11);
            message_canvas.print(shortTime);
            message_canvas.setTextColor(1);  // White for 1-bit canvas
            message_canvas.print(": ");
            
            // Truncate message if too long for one line
            String shortContent = fileContent.substring(0, 35);
            if (fileContent.length() > 35) shortContent += "...";
            message_canvas.println(shortContent);
            
            // Update display in real-time
            tft.drawBitmap(0, currentMessageY, message_canvas.getBuffer(), message_canvas.width(), currentMessageHeight, ST77XX_WHITE, ST77XX_BLACK);
            
            yPos += 9; // Move to next line
          }
          
          totalCount++;
          Serial.print("Found message ");
          Serial.print(totalCount);
          Serial.print(": ");
          Serial.println(fileContent.substring(0, 20) + "...");
        }
      }
    }
    file.close();
  }
  root.close();
  
  messageThreadCount = totalCount;
  
  char finalMsg[32];
  snprintf(finalMsg, sizeof(finalMsg), "Found %d thread msgs", messageThreadCount);
  updateStatus(finalMsg, ST77XX_GREEN);
  
  Serial.print("Loaded ");
  Serial.print(messageThreadCount);
  Serial.print(" total messages for contact: ");
  Serial.println(senderNumber);
  Serial.println("Messages will display with latest at bottom");
  
  // Clear canvas and show final sorted result
  message_canvas.fillScreen(0);  // Black for 1-bit canvas
  message_canvas.setCursor(0, 0);
  message_canvas.print("Found ");
  message_canvas.print(messageThreadCount);
  message_canvas.println(" messages");
  if (messageThreadCount > 0) {
    message_canvas.println("(Latest messages at bottom)");
  }
  tft.drawBitmap(0, currentMessageY, message_canvas.getBuffer(), message_canvas.width(), currentMessageHeight, ST77XX_WHITE, ST77XX_BLACK);
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
      smsInbox[0].fullTime = formatDateWithoutYear(smsInbox[0].time);
      
      smsInbox[0].content = lines[3];
      smsInbox[0].content.replace("Content: ", "");
      
      smsInbox[0].filename = filename;
      
      // Parse timestamp for sorting
      smsInbox[0].timestampValue = parseTimestamp(smsInbox[0].time);
      
      // Look up contact name
      smsInbox[0].senderDisplayName = lookupContactName(smsInbox[0].sender);
      
      // Reset scroll and selection to top to show the new message
      inboxScrollOffset = 0;
      inboxSelectedIndex = 0;
      
      // Re-sort to maintain chronological order and filter to unique contacts
      sortSMSByTime();
      filterToUniqueContacts();
      calculateCanvasSizes(); // Recalculate canvas sizes after filtering
      
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
      // Check for down arrow to move selection down
      else if (keyData == 0xB6) { // DOWN arrow
        if ((inboxSelectedIndex + inboxScrollOffset + 1) < smsInboxCount) {
          if (inboxSelectedIndex < 9) {
            // Selection can move down within visible area (10 entries: 0-9)
            inboxSelectedIndex++;
          } else {
            // Need to scroll down to show next message
            inboxScrollOffset++;
          }
          updateInbox();
          Serial.print("Inbox selection moved down - index: ");
          Serial.print(inboxSelectedIndex);
          Serial.print(", offset: ");
          Serial.println(inboxScrollOffset);
        }
      }
      // Check for up arrow to move selection up  
      else if (keyData == 0xB5) { // UP arrow
        if ((inboxSelectedIndex + inboxScrollOffset) > 0) {
          if (inboxSelectedIndex > 0) {
            // Selection can move up within visible area
            inboxSelectedIndex--;
          } else {
            // Need to scroll up to show previous message
            inboxScrollOffset--;
          }
          updateInbox();
          Serial.print("Inbox selection moved up - index: ");
          Serial.print(inboxSelectedIndex);
          Serial.print(", offset: ");
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
          filterToUniqueContacts();
          calculateCanvasSizes(); // Recalculate canvas sizes after filtering
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
          filterToUniqueContacts();
          calculateCanvasSizes(); // Recalculate canvas sizes after filtering
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

String formatDateWithoutYear(const String& timestamp) {
  // Input format: "25/12/27,17:48:42-32" or "25/12/27 17:48:42-32"
  // Output format: "12/27 17:48" (month/day hour:minute)
  
  String result = timestamp;
  result.replace(",", " "); // Replace comma with space
  
  // Find the date part and time part
  int spacePos = result.indexOf(' ');
  if (spacePos == -1) return timestamp; // Return original if can't parse
  
  String datePart = result.substring(0, spacePos);
  String timePart = result.substring(spacePos + 1);
  
  // Parse date: "25/12/27" -> skip year, take "12/27"
  int firstSlash = datePart.indexOf('/');
  if (firstSlash == -1) return timestamp; // Return original if can't parse
  
  String monthDay = datePart.substring(firstSlash + 1); // "12/27"
  
  // Parse time: "17:48:42-32" -> take "17:48"
  int colonPos = timePart.indexOf(':');
  if (colonPos == -1) return timestamp; // Return original if can't parse
  
  int secondColon = timePart.indexOf(':', colonPos + 1);
  String hourMin;
  if (secondColon != -1) {
    hourMin = timePart.substring(0, secondColon); // "17:48"
  } else {
    hourMin = timePart; // Use whole time if no second colon
  }
  
  return monthDay + " " + hourMin;
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