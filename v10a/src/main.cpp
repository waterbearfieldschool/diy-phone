/**************************************************************************
  SIM7600 Cellular Phone Controller
  Converted from CircuitPython p26.py to C++
  Features: SMS, Voice Calls, Display UI, SD Storage, I2C Keyboard
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

// SD card pin definitions  
#define SD_CS         10

// UART for SIM7600 (D2=TX, A4=RX)
#define SIM_TX_PIN    2
#define SIM_RX_PIN    A4

// I2C keyboard address
#define KEYBOARD_ADDR 0x5F

// Display and storage objects
Adafruit_ST7789 tft = Adafruit_ST7789(&customSPI, TFT_CS, TFT_DC, TFT_RST);
SdFat SD;
File dataFile;

// UART buffer for SIM7600 communication
String uartLineBuffer = "";
String smsNotificationBuffer = "";

// Address book structure
struct Contact {
  String name;
  String number;
};

Contact addressBook[10];
int addressBookSize = 0;
int recipientIndex = 0;

// UI State Management
enum ViewState {
  VIEW_INBOX,
  VIEW_DETAIL, 
  VIEW_COMPOSE,
  VIEW_CALL,
  VIEW_THREAD,
  VIEW_INCOMING_CALL
};

ViewState currentView = VIEW_INBOX;
int selectedMessageIndex = 0;
int inboxScrollOffset = 0;
String composeMessage = "";
int selectedRecipientIndex = 0;
String manualNumberEntry = "";
bool recipientMode = false; // false=contacts, true=manual

// Call state
bool callInProgress = false;
String callStatus = "";
String callContactName = "";
unsigned long callStartTime = 0;
bool incomingCallActive = false;
String incomingCallerNumber = "";

// Message storage
struct SMSMessage {
  String sender;
  String timestamp;
  String content;
  String filename;
};

SMSMessage messagesList[20];
int messagesCount = 0;

// Display text buffers (simulating CircuitPython labels)
String displayLines[8];
String statusText = "";
String infoText = "";
String titleText = "INBOX";

// Keyboard input
uint8_t keyboardData = 0;
char lastKey = 0;

// Key definitions
#define KEY_UP     0xB5
#define KEY_DOWN   0xB6
#define KEY_LEFT   0xB4
#define KEY_RIGHT  0xB7
#define KEY_ENTER  0x0D
#define KEY_ESC    0x1B
#define KEY_BACK   0x08
#define KEY_SPACE  0x20
#define KEY_TAB    0x09

// Function declarations
void initializeSystem();
void loadAddressBook();
void saveAddressBook();
void setupDisplay();
void setupSDCard();
void setupUART();
void setupI2C();
String getKeyName(uint8_t keyCode);
String getViewName(ViewState view);

void handleKeyboard();
void processKeyInput(char key);
void readUARTLines();
void processUARTLine(String line);
void handleSMSNotification(String line);
void parseIncomingCall(String line);

void displayInbox();
void displayMessageDetail();
void displayCompose();
void displayCallScreen();
void displayThreadView();
void displayIncomingCall();
void updateDisplay();
void clearDisplay();

void scrollInboxUp();
void scrollInboxDown();
void selectMessage();
void startReply();
void startNewCompose();
void sendMessage();
void makeCall();
void answerCall();
void rejectCall();

void getMessages();
void parseAndStoreSMS(String data);
void storeSMSToSD(String sender, String timestamp, String content);
void loadSMSFromSD();
String formatSender(String sender);
String formatTimestamp(String timestamp);

void sendATCommand(String command);
String readATResponse(int timeout = 1000);
void initializeSIM();

void setup() {
  Serial.begin(115200);
  Serial.println("Starting SIM7600 Phone Controller...");
  
  initializeSystem();
  loadAddressBook();
  setupDisplay();
  setupSDCard();
  setupUART();
  setupI2C();
  
  // Initialize SIM7600
  delay(2000);
  initializeSIM();
  
  // Load existing messages and display inbox
  loadSMSFromSD();
  displayInbox();
  
  Serial.println("System ready!");
}

void loop() {
  // Read and process UART data from SIM7600
  readUARTLines();
  
  // Handle keyboard input
  handleKeyboard();
  
  // Update display if needed
  updateDisplay();
  
  // Handle call timing if in progress
  if (callInProgress && callStatus == "connected") {
    // Update call duration every second
    if (millis() - callStartTime >= 1000) {
      callStartTime = millis();
      if (currentView == VIEW_CALL) {
        displayCallScreen(); // Refresh display with updated duration
      }
    }
  }
  
  delay(50); // Small delay for stability
}

void initializeSystem() {
  // Initialize address book with default contacts
  addressBook[0] = {"Don (voip)", "16512524765"};
  addressBook[1] = {"Don (iphone)", "17813230341"};
  addressBook[2] = {"Liz", "16174299144"};
  addressBookSize = 3;
}

void setupDisplay() {
  Serial.println("[DEBUG] Initializing display...");
  
  // Initialize custom SPI bus
  customSPI.begin();
  Serial.println("[DEBUG] Custom SPI bus initialized");
  
  // Initialize display
  tft.init(320, 240);  // 320x240 display with rotation
  tft.setRotation(1);  // Landscape mode
  tft.fillScreen(ST77XX_BLACK);
  Serial.println("[DEBUG] Display initialized (320x240, landscape)");
  
  // Display startup message
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(2);
  tft.setCursor(10, 100);
  tft.println("SIM7600 Phone");
  tft.setCursor(10, 130);
  tft.println("Starting up...");
  
  Serial.println("[DEBUG] Display startup message shown");
  delay(1000);
}

void setupSDCard() {
  Serial.println("[DEBUG] Initializing SD card...");
  
  if (!SD.begin(SD_CS)) {
    Serial.println("[DEBUG] ERROR: SD card initialization failed!");
    // Show error on display
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_RED);
    tft.setCursor(10, 100);
    tft.println("SD Card Error!");
    delay(2000);
    return;
  }
  
  Serial.println("[DEBUG] SD card initialized successfully");
  
  // Test SD card with a simple write/read
  dataFile = SD.open("test.txt", FILE_WRITE);
  if (dataFile) {
    dataFile.println("Hello world!");
    dataFile.close();
    Serial.println("[DEBUG] SD card test write successful");
  } else {
    Serial.println("[DEBUG] WARNING: SD card test write failed");
  }
}

void setupUART() {
  // Initialize UART for SIM7600 communication
  Serial.println("[DEBUG] Initializing UART for SIM7600...");
  
  // Configure Serial1 to use specific pins for SIM7600
  // TX = D2, RX = A4
  Serial1.setPins(A4, 2);  // setPins(rx, tx)
  Serial1.begin(115200);
  
  Serial.println("[DEBUG] UART initialized at 115200 baud (TX=D2, RX=A4)");
}

void setupI2C() {
  Serial.println("[DEBUG] Initializing I2C for keyboard...");
  Wire.begin();
  Serial.println("[DEBUG] I2C initialized for keyboard at address 0x5F");
  
  // Test I2C connection to keyboard
  Serial.println("[DEBUG] Testing I2C connection to keyboard...");
  Wire.requestFrom(KEYBOARD_ADDR, 1);
  if (Wire.available()) {
    uint8_t testData = Wire.read();
    Serial.println("[DEBUG] I2C keyboard test successful, received: 0x" + String(testData, HEX));
  } else {
    Serial.println("[DEBUG] WARNING: No response from I2C keyboard at address 0x5F");
  }
}

void initializeSIM() {
  Serial.println("[DEBUG] Initializing SIM7600...");
  
  // Basic AT commands to initialize SIM7600
  Serial.println("[DEBUG] Sending AT command...");
  sendATCommand("AT");
  delay(500);
  String response1 = readATResponse(1000);
  Serial.println("[DEBUG] AT Response: " + response1);
  
  // Set text mode for SMS
  Serial.println("[DEBUG] Setting SMS text mode...");
  sendATCommand("AT+CMGF=1");
  delay(500);
  String response2 = readATResponse(1000);
  Serial.println("[DEBUG] CMGF Response: " + response2);
  
  // Enable caller ID
  Serial.println("[DEBUG] Enabling caller ID...");
  sendATCommand("AT+CLIP=1");
  delay(500);
  String response3 = readATResponse(1000);
  Serial.println("[DEBUG] CLIP Response: " + response3);
  
  // Set audio to headphones
  Serial.println("[DEBUG] Setting audio to headphones...");
  sendATCommand("AT+CSDVC=1");
  delay(500);
  String response4 = readATResponse(1000);
  Serial.println("[DEBUG] CSDVC Response: " + response4);
  
  Serial.println("[DEBUG] SIM7600 initialization complete");
}

void handleKeyboard() {
  // Read from I2C keyboard
  Wire.requestFrom(KEYBOARD_ADDR, 1);
  
  if (Wire.available()) {
    keyboardData = Wire.read();
    
    if (keyboardData != 0) {
      lastKey = keyboardData;
      
      // Detailed key analysis (only for actual key presses)
      String keyName = getKeyName(keyboardData);
      char printableChar = (keyboardData >= 32 && keyboardData <= 126) ? (char)keyboardData : '?';
      
      Serial.println("[DEBUG] Key pressed: 0x" + String(keyboardData, HEX) + " (" + keyName + ") char: '" + String(printableChar) + "'");
      Serial.println("[DEBUG] Current view: " + String(currentView) + ", calling processKeyInput...");
      
      processKeyInput(lastKey);
    }
    // Removed spam for keyboardData == 0 (no key pressed)
  } else {
    // Only print occasionally to avoid spam
    static unsigned long lastNoDataPrint = 0;
    if (millis() - lastNoDataPrint > 30000) {  // Reduced frequency to every 30 seconds
      Serial.println("[DEBUG] No data available from I2C keyboard");
      lastNoDataPrint = millis();
    }
  }
}

void processKeyInput(char key) {
  String viewName = getViewName(currentView);
  Serial.println("[DEBUG] Processing key input in view: " + viewName + " (" + String(currentView) + ")");
  
  switch (currentView) {
    case VIEW_INBOX:
      Serial.println("[DEBUG] In inbox view, processing key...");
      if (key == KEY_UP) {
        Serial.println("[DEBUG] UP key - scrolling inbox up");
        scrollInboxUp();
      } else if (key == KEY_DOWN) {
        Serial.println("[DEBUG] DOWN key - scrolling inbox down");
        scrollInboxDown();
      } else if (key == KEY_ENTER || key == KEY_RIGHT) {
        Serial.println("[DEBUG] ENTER/RIGHT key - opening thread view");
        displayThreadView();
      } else if (key == 'n' || key == 'N') {
        Serial.println("[DEBUG] N key - getting new messages");
        getMessages();
        displayInbox();
      } else if (key == 'c' || key == 'C') {
        Serial.println("[DEBUG] C key - starting new compose");
        startNewCompose();
      } else if (key == KEY_SPACE) {
        Serial.println("[DEBUG] SPACE key - opening call screen");
        displayCallScreen();
      } else if (key >= '0' && key <= '9') {
        Serial.println("[DEBUG] Digit key " + String(key) + " - starting direct dial");
        manualNumberEntry = String(key);
        displayCallScreen();
      } else {
        Serial.println("[DEBUG] Unhandled key in inbox view: 0x" + String(key, HEX));
      }
      break;
      
    case VIEW_DETAIL:
      Serial.println("[DEBUG] In detail view, processing key...");
      if (key == 'b' || key == 'B' || key == KEY_ESC) {
        Serial.println("[DEBUG] Back/ESC key - returning to inbox");
        displayInbox();
      } else if (key == 'r' || key == 'R') {
        Serial.println("[DEBUG] R key - starting reply");
        startReply();
      } else {
        Serial.println("[DEBUG] Unhandled key in detail view: 0x" + String(key, HEX));
      }
      break;
      
    case VIEW_COMPOSE:
      Serial.println("[DEBUG] In compose view, processing key...");
      if (key == KEY_ESC) {
        Serial.println("[DEBUG] ESC key - canceling compose, returning to inbox");
        displayInbox();
      } else if (key == KEY_ENTER) {
        Serial.println("[DEBUG] ENTER key - sending message");
        sendMessage();
      } else if (key == KEY_BACK && composeMessage.length() > 0) {
        Serial.println("[DEBUG] BACK key - deleting last character");
        composeMessage.remove(composeMessage.length() - 1);
        displayCompose();
      } else if (key >= 32 && key <= 126) { // Printable characters
        Serial.println("[DEBUG] Adding character '" + String((char)key) + "' to message");
        composeMessage += key;
        displayCompose();
      } else {
        Serial.println("[DEBUG] Unhandled key in compose view: 0x" + String(key, HEX));
      }
      break;
      
    case VIEW_CALL:
      Serial.println("[DEBUG] In call view, processing key...");
      if (key == KEY_ESC) {
        if (callInProgress) {
          Serial.println("[DEBUG] ESC key - hanging up call");
          sendATCommand("AT+CHUP");
          callInProgress = false;
        } else {
          Serial.println("[DEBUG] ESC key - canceling call, returning to inbox");
        }
        displayInbox();
      } else if (key == KEY_ENTER) {
        Serial.println("[DEBUG] ENTER key - making call");
        makeCall();
      } else {
        Serial.println("[DEBUG] Unhandled key in call view: 0x" + String(key, HEX));
      }
      break;
      
    case VIEW_INCOMING_CALL:
      Serial.println("[DEBUG] In incoming call view, processing key...");
      if (key == KEY_ENTER) {
        Serial.println("[DEBUG] ENTER key - answering call");
        answerCall();
      } else if (key == KEY_ESC) {
        Serial.println("[DEBUG] ESC key - rejecting call");
        rejectCall();
      } else {
        Serial.println("[DEBUG] Unhandled key in incoming call view: 0x" + String(key, HEX));
      }
      break;
      
    case VIEW_THREAD:
      Serial.println("[DEBUG] In thread view, processing key...");
      if (key == 'b' || key == 'B' || key == KEY_ESC) {
        Serial.println("[DEBUG] Back/ESC key - returning to inbox");
        displayInbox();
      } else {
        Serial.println("[DEBUG] Unhandled key in thread view: 0x" + String(key, HEX));
      }
      break;
  }
  
  Serial.println("[DEBUG] Key processing complete");
}

void readUARTLines() {
  while (Serial1.available()) {
    char c = Serial1.read();
    
    if (c == '\r') {
      // Process complete line on carriage return
      String line = uartLineBuffer;
      line.trim();
      if (line.length() > 0) {
        processUARTLine(line);
      }
      uartLineBuffer = "";
    } else if (c != '\n') {  // Ignore line feed characters
      // Add character to buffer (ignore \n)
      uartLineBuffer += c;
    }
  }
}

void processUARTLine(String line) {
  Serial.println("[DEBUG] UART RX: " + line);
  
  // Check for incoming SMS notification
  if (line.indexOf("+CMTI:") >= 0) {
    Serial.println("[DEBUG] Detected SMS notification: " + line);
    handleSMSNotification(line);
  }
  
  // Check for incoming call
  if (line.indexOf("RING") >= 0) {
    Serial.println("[DEBUG] Detected incoming call: " + line);
    parseIncomingCall(line);
  }
  
  // Check for call status
  if (line.indexOf("VOICE CALL: BEGIN") >= 0) {
    Serial.println("[DEBUG] Call connected");
    callStatus = "connected";
    callStartTime = millis();
  } else if (line.indexOf("VOICE CALL: END") >= 0 || line.indexOf("NO CARRIER") >= 0) {
    Serial.println("[DEBUG] Call ended");
    callStatus = "ended";
    callInProgress = false;
    displayInbox();
  }
}

void displayInbox() {
  Serial.println("[DEBUG] Displaying inbox view");
  Serial.println("[DEBUG] messagesCount = " + String(messagesCount));
  Serial.println("[DEBUG] selectedMessageIndex = " + String(selectedMessageIndex));
  Serial.println("[DEBUG] inboxScrollOffset = " + String(inboxScrollOffset));
  
  currentView = VIEW_INBOX;
  clearDisplay();
  
  // Title at the top
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(2);
  tft.setCursor(5, 5);
  tft.println("INBOX");
  
  // Display message list
  tft.setTextSize(1);
  if (messagesCount == 0) {
    // Show "No messages" if empty
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(5, 40);
    tft.println("No messages");
    tft.setCursor(5, 60);
    tft.println("Press 'n' to check for new messages");
  } else {
    // Display messages
    for (int i = 0; i < 8 && (inboxScrollOffset + i) < messagesCount; i++) {
      int msgIdx = inboxScrollOffset + i;
      tft.setCursor(5, 40 + (i * 20));
      
      if (msgIdx == selectedMessageIndex) {
        tft.setTextColor(ST77XX_BLACK);
        tft.fillRect(0, 40 + (i * 20) - 2, 320, 18, ST77XX_GREEN);
      } else {
        tft.setTextColor(ST77XX_YELLOW);
      }
      
      String sender = formatSender(messagesList[msgIdx].sender);
      String timestamp = formatTimestamp(messagesList[msgIdx].timestamp);
      String preview = messagesList[msgIdx].content.substring(0, 15);
      
      tft.println(sender + " " + timestamp + " " + preview);
    }
  }
  
  // Status line at the bottom
  if (messagesCount > 0) {
    statusText = "MSG " + String(selectedMessageIndex + 1) + "/" + String(messagesCount);
  } else {
    statusText = "No messages";
  }
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(5, 200);  // Moved higher from 220
  tft.println(statusText);
  
  // Info line at the very bottom
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(5, 220);
  tft.println("N:refresh C:compose SPACE:call");
}

void scrollInboxUp() {
  if (selectedMessageIndex > 0) {
    selectedMessageIndex--;
    if (selectedMessageIndex < inboxScrollOffset) {
      inboxScrollOffset = selectedMessageIndex;
    }
    displayInbox();
  }
}

void scrollInboxDown() {
  if (selectedMessageIndex < messagesCount - 1) {
    selectedMessageIndex++;
    if (selectedMessageIndex >= inboxScrollOffset + 8) {
      inboxScrollOffset = selectedMessageIndex - 7;
    }
    displayInbox();
  }
}

void getMessages() {
  Serial.println("[DEBUG] Starting message retrieval...");
  statusText = "(checking messages...)";
  updateDisplay();
  
  Serial.println("[DEBUG] Setting SMS text mode for message retrieval...");
  sendATCommand("AT+CMGF=1");
  delay(200);
  
  Serial.println("[DEBUG] Requesting all SMS messages...");
  sendATCommand("AT+CMGL=\"ALL\"");
  delay(300);
  
  Serial.println("[DEBUG] Reading SMS response...");
  String response = readATResponse(2000);
  Serial.println("[DEBUG] SMS response length: " + String(response.length()));
  
  parseAndStoreSMS(response);
  
  Serial.println("[DEBUG] Message retrieval complete. Found " + String(messagesCount) + " messages");
  statusText = "";
}

void parseAndStoreSMS(String data) {
  Serial.println("[DEBUG] Parsing SMS data...");
  Serial.println("[DEBUG] Raw SMS data: " + data);
  
  // Parse SMS messages from AT+CMGL response
  int startPos = 0;
  messagesCount = 0;
  
  while ((unsigned int)startPos < data.length() && messagesCount < 20) {
    int cmglPos = data.indexOf("+CMGL:", startPos);
    if (cmglPos == -1) {
      Serial.println("[DEBUG] No more +CMGL entries found");
      break;
    }
    
    Serial.println("[DEBUG] Found +CMGL at position " + String(cmglPos));
    
    int lineEnd = data.indexOf("\r\n", cmglPos);
    if (lineEnd == -1) {
      // Try just \r in case \n is missing
      lineEnd = data.indexOf("\r", cmglPos);
      if (lineEnd == -1) break;
    }
    
    String headerLine = data.substring(cmglPos, lineEnd);
    Serial.println("[DEBUG] Header line: " + headerLine);
    
    // Find the message content (next non-empty line)
    int contentStart = lineEnd + (data.charAt(lineEnd + 1) == '\n' ? 2 : 1);
    int contentEnd = data.indexOf("\r\n", contentStart);
    if (contentEnd == -1) {
      contentEnd = data.indexOf("\r", contentStart);
      if (contentEnd == -1) contentEnd = data.length();
    }
    
    String content = data.substring(contentStart, contentEnd);
    content.trim();
    Serial.println("[DEBUG] Message content: " + content);
    
    if (content.length() > 0) {
      // Parse header: +CMGL: index,"status","sender","","timestamp"
      // Simple parsing for now
      int firstQuote = headerLine.indexOf('"');
      if (firstQuote >= 0) {
        int secondQuote = headerLine.indexOf('"', firstQuote + 1);
        int thirdQuote = headerLine.indexOf('"', secondQuote + 1);
        int fourthQuote = headerLine.indexOf('"', thirdQuote + 1);
        
        if (fourthQuote >= 0) {
          String sender = headerLine.substring(thirdQuote + 1, fourthQuote);
          Serial.println("[DEBUG] Parsed sender: " + sender);
          
          messagesList[messagesCount].sender = sender;
          messagesList[messagesCount].content = content;
          messagesList[messagesCount].timestamp = "today";
          messagesList[messagesCount].filename = "msg_" + String(messagesCount);
          
          Serial.println("[DEBUG] Stored message " + String(messagesCount) + " from " + sender + ": " + content);
          
          // Store to SD card
          storeSMSToSD(sender, "today", content);
          
          messagesCount++;
        } else {
          Serial.println("[DEBUG] Could not parse sender from header line");
        }
      } else {
        Serial.println("[DEBUG] No quotes found in header line");
      }
    } else {
      Serial.println("[DEBUG] Empty message content, skipping");
    }
    
    startPos = contentEnd + (data.substring(contentEnd, contentEnd + 2) == "\r\n" ? 2 : 1);
  }
  
  Serial.println("[DEBUG] SMS parsing complete. Parsed " + String(messagesCount) + " messages");
}

void storeSMSToSD(String sender, String timestamp, String content) {
  String filename = "sms_" + String(millis()) + ".txt";
  Serial.println("[DEBUG] Storing SMS to SD card: " + filename);
  
  dataFile = SD.open(filename.c_str(), FILE_WRITE);
  if (dataFile) {
    dataFile.println("From: " + sender);
    dataFile.println("Time: " + timestamp);
    dataFile.println("Content: " + content);
    dataFile.close();
    Serial.println("[DEBUG] SMS stored successfully to SD card");
  } else {
    Serial.println("[DEBUG] ERROR: Failed to open SD card file for writing");
  }
}

void loadSMSFromSD() {
  Serial.println("[DEBUG] Loading SMS from SD card...");
  // Load existing SMS files from SD card
  messagesCount = 0;
  // Implementation would scan SD card for sms_*.txt files
  // For now, add a test message for debugging
  
  messagesList[0].sender = "+16512524765";
  messagesList[0].content = "Test message from SD card load";
  messagesList[0].timestamp = "12/26 10:30";
  messagesList[0].filename = "test_msg.txt";
  messagesCount = 1;
  
  Serial.println("[DEBUG] Loaded " + String(messagesCount) + " test messages from SD");
}

String formatSender(String sender) {
  // Look up in address book first
  for (int i = 0; i < addressBookSize; i++) {
    if (addressBook[i].number == sender) {
      return addressBook[i].name.substring(0, 10);
    }
  }
  
  // Format phone number if not found
  if (sender.length() > 10) {
    return sender.substring(sender.length() - 10);
  }
  return sender;
}

String formatTimestamp(String timestamp) {
  // Simple timestamp formatting
  return timestamp.substring(0, 8);
}

void sendATCommand(String command) {
  Serial1.print(command);
  Serial1.print("\r");
  Serial.println("[DEBUG] UART TX: " + command + "\\r");
}

String readATResponse(int timeout) {
  String response = "";
  String lineBuffer = "";
  unsigned long startTime = millis();
  
  Serial.println("[DEBUG] Waiting for AT response (timeout: " + String(timeout) + "ms)...");
  
  while (millis() - startTime < (unsigned long)timeout) {
    if (Serial1.available()) {
      char c = Serial1.read();
      
      if (c == '\r') {
        // Complete line received
        if (lineBuffer.length() > 0) {
          if (response.length() > 0) response += "\r\n";
          response += lineBuffer;
          lineBuffer = "";
          
          // Check for common AT response endings
          if (response.endsWith("OK") || response.endsWith("ERROR") || 
              response.indexOf("+CME ERROR:") >= 0 || response.indexOf("+CMS ERROR:") >= 0) {
            break;
          }
        }
      } else if (c != '\n') {  // Ignore line feed
        lineBuffer += c;
      }
    } else {
      delay(10);
    }
  }
  
  // Add any remaining buffer content
  if (lineBuffer.length() > 0) {
    if (response.length() > 0) response += "\r\n";
    response += lineBuffer;
  }
  
  if (response.length() > 0) {
    Serial.println("[DEBUG] AT Response received (" + String(response.length()) + " chars):");
    Serial.println(response);
  } else {
    Serial.println("[DEBUG] AT Response timeout - no data received");
  }
  
  return response;
}

void startNewCompose() {
  currentView = VIEW_COMPOSE;
  composeMessage = "";
  displayCompose();
}

void displayCompose() {
  clearDisplay();
  
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(1);
  tft.setCursor(5, 5);
  tft.println("COMPOSE MESSAGE");
  
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(5, 30);
  tft.println("To: " + addressBook[selectedRecipientIndex].name);
  
  tft.setCursor(5, 50);
  tft.println("Message:");
  
  tft.setCursor(5, 70);
  tft.println(composeMessage + "_");
  
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(5, 220);
  tft.println("ENTER:send ESC:cancel BACK:delete");
}

void sendMessage() {
  if (composeMessage.length() > 0) {
    String recipient = addressBook[selectedRecipientIndex].number;
    
    Serial.println("[DEBUG] Sending SMS to: " + recipient);
    Serial.println("[DEBUG] Message content: " + composeMessage);
    
    statusText = "Sending...";
    updateDisplay();
    
    Serial.println("[DEBUG] Setting SMS text mode for sending...");
    sendATCommand("AT+CMGF=1");
    delay(200);
    String response1 = readATResponse(1000);
    
    Serial.println("[DEBUG] Initiating SMS send command...");
    sendATCommand("AT+CMGS=\"+" + recipient + "\"");
    delay(500);
    String response2 = readATResponse(1000);
    Serial.println("[DEBUG] CMGS Response: " + response2);
    
    Serial.println("[DEBUG] Sending message content and Ctrl+Z...");
    Serial1.print(composeMessage);
    Serial1.write(0x1A); // Ctrl+Z to send
    
    delay(2000);
    String response3 = readATResponse(3000);
    Serial.println("[DEBUG] SMS Send Response: " + response3);
    
    composeMessage = "";
    Serial.println("[DEBUG] SMS send complete, returning to inbox");
    displayInbox();
  } else {
    Serial.println("[DEBUG] No message content to send");
  }
}

void displayCallScreen() {
  currentView = VIEW_CALL;
  clearDisplay();
  
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(2);
  tft.setCursor(5, 5);
  tft.println("CALL");
  
  // Show address book for selection
  tft.setTextSize(1);
  for (int i = 0; i < addressBookSize && i < 8; i++) {
    tft.setCursor(5, 40 + (i * 20));
    if (i == selectedRecipientIndex) {
      tft.setTextColor(ST77XX_BLACK);
      tft.fillRect(0, 40 + (i * 20) - 2, 320, 18, ST77XX_GREEN);
    } else {
      tft.setTextColor(ST77XX_YELLOW);
    }
    tft.println(addressBook[i].name + " (" + addressBook[i].number + ")");
  }
  
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(5, 220);
  tft.println("ENTER:call ESC:back UP/DN:select");
}

void makeCall() {
  String number = addressBook[selectedRecipientIndex].number;
  callContactName = addressBook[selectedRecipientIndex].name;
  
  Serial.println("[DEBUG] Making call to: " + callContactName + " (" + number + ")");
  
  statusText = "Calling " + callContactName + "...";
  updateDisplay();
  
  // Set audio to headphones
  Serial.println("[DEBUG] Setting audio to headphones...");
  sendATCommand("AT+CSDVC=1");
  delay(200);
  String response1 = readATResponse(1000);
  Serial.println("[DEBUG] Audio Response: " + response1);
  
  // Set volume
  Serial.println("[DEBUG] Setting call volume...");
  sendATCommand("AT+CLVL=5");
  delay(200);
  String response2 = readATResponse(1000);
  Serial.println("[DEBUG] Volume Response: " + response2);
  
  // Make the call
  Serial.println("[DEBUG] Initiating call...");
  sendATCommand("ATD+" + number + ";");
  
  callInProgress = true;
  callStatus = "dialing";
  callStartTime = millis();
  Serial.println("[DEBUG] Call initiated, status: dialing");
}

void clearDisplay() {
  tft.fillScreen(ST77XX_BLACK);
}

void updateDisplay() {
  // Update status line if needed
  if (statusText.length() > 0) {
    tft.fillRect(0, 210, 320, 30, ST77XX_BLACK);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(5, 220);
    tft.println(statusText);
  }
}

// Helper functions for debugging
String getKeyName(uint8_t keyCode) {
  switch (keyCode) {
    case KEY_UP: return "UP";
    case KEY_DOWN: return "DOWN";
    case KEY_LEFT: return "LEFT";
    case KEY_RIGHT: return "RIGHT";
    case KEY_ENTER: return "ENTER";
    case KEY_ESC: return "ESC";
    case KEY_BACK: return "BACKSPACE";
    case KEY_SPACE: return "SPACE";
    case KEY_TAB: return "TAB";
    default:
      if (keyCode >= 32 && keyCode <= 126) {
        return "'" + String((char)keyCode) + "'";
      } else {
        return "UNKNOWN";
      }
  }
}

String getViewName(ViewState view) {
  switch (view) {
    case VIEW_INBOX: return "INBOX";
    case VIEW_DETAIL: return "DETAIL";
    case VIEW_COMPOSE: return "COMPOSE";
    case VIEW_CALL: return "CALL";
    case VIEW_THREAD: return "THREAD";
    case VIEW_INCOMING_CALL: return "INCOMING_CALL";
    default: return "UNKNOWN_VIEW";
  }
}

// Additional stub functions for features not yet implemented
void loadAddressBook() {}
void saveAddressBook() {}
void displayMessageDetail() {}
void displayThreadView() {}
void displayIncomingCall() {}
void startReply() {}
void handleSMSNotification(String line) {}
void parseIncomingCall(String line) {}
void answerCall() {}
void rejectCall() {}
