#include "SIM7600.h"
#include <SdFat.h>

extern SdFat sd;

SIM7600::SIM7600(HardwareSerial *serial) {
    _hwSerial = serial;
    _swSerial = nullptr;
    _useHwSerial = true;
}

SIM7600::SIM7600(SoftwareSerial *serial) {
    _hwSerial = nullptr;
    _swSerial = serial;
    _useHwSerial = false;
}

bool SIM7600::begin(unsigned long baud) {
    if (_useHwSerial) {
        _hwSerial->begin(baud);
    } else {
        _swSerial->begin(baud);
    }
    
    delay(1000);
    flushInput();
    
    // First disable echo
    Serial.println("Disabling AT echo...");
    sendATCommand("ATE0", 2000);  // ATE0 = disable echo
    
    delay(500);
    flushInput();
    
    // Test connectivity with AT command
    return sendATCommand("AT");
}

bool SIM7600::isConnected() {
    return sendATCommand("AT");
}

// SMS Functions
bool SIM7600::setSMSTextMode() {
    return sendATCommand("AT+CMGF=1");
}

bool SIM7600::sendSMS(const char* phoneNumber, const char* message) {
    if (!setSMSTextMode()) return false;
    
    char command[64];
    snprintf(command, sizeof(command), "AT+CMGS=\"+%s\"", phoneNumber);
    
    println(command);
    delay(500);
    
    if (!waitForResponse(">", 1000)) return false;
    
    print(message);
    print("\x1A"); // Ctrl+Z to send
    delay(2000);
    
    return waitForResponse("OK", 5000);
}

bool SIM7600::deleteSMS(uint8_t index) {
    if (!setSMSTextMode()) return false;
    
    char command[32];
    snprintf(command, sizeof(command), "AT+CMGD=%d", index);
    return sendATCommand(command, 2000);
}

bool SIM7600::deleteAllSMS() {
    if (!setSMSTextMode()) return false;
    return sendATCommand("AT+CMGDA=\"DEL ALL\"", 5000);
}

String SIM7600::readSMS(uint8_t index) {
    if (!setSMSTextMode()) return "";
    
    char command[32];
    snprintf(command, sizeof(command), "AT+CMGR=%d", index);
    
    if (!sendATCommand(command, 1000)) return "";
    
    return getATResponse();
}

String SIM7600::readSMSRaw(uint8_t index) {
    if (!setSMSTextMode()) return "";
    
    char command[32];
    snprintf(command, sizeof(command), "AT+CMGR=%d", index);
    
    flushInput();
    println(command);
    
    // Get multiline response without expecting OK - use longer timeout
    return getMultiLineResponse(5000);
}

String SIM7600::readAndDeleteSMS(uint8_t index) {
    if (!setSMSTextMode()) return "";
    
    char command[32];
    snprintf(command, sizeof(command), "AT+CMGRD=%d", index);
    
    flushInput();
    println(command);
    
    // Get multiline response - CMGRD reads and deletes the SMS
    String response = getMultiLineResponse(5000);
    
    Serial.print("[SIM7600] readAndDeleteSMS response length: ");
    Serial.println(response.length());
    Serial.print("[SIM7600] readAndDeleteSMS response: '");
    Serial.print(response);
    Serial.println("'");
    
    // Debug: show first 20 bytes as hex
    Serial.print("[SIM7600] First 20 bytes as hex: ");
    for (int i = 0; i < min(20, (int)response.length()); i++) {
        if (response[i] < 16) Serial.print("0");
        Serial.print(response[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
    
    return response;
}

void SIM7600::checkSMSStorage() {
    if (!setSMSTextMode()) return;
    
    Serial.println("=== Checking SMS Storage ===");
    
    // Check SMS storage status with AT+CPMS?
    flushInput();
    println("AT+CPMS?");
    
    String cpmsResponse = getATResponse();
    Serial.print("CPMS Response received, parsing for message count...");
    
    // Parse response like: +CPMS: "SM",2,50,"SM",2,50,"SM",2,50
    int firstComma = cpmsResponse.indexOf(',');
    if (firstComma != -1) {
        int secondComma = cpmsResponse.indexOf(',', firstComma + 1);
        if (secondComma != -1) {
            String countStr = cpmsResponse.substring(firstComma + 1, secondComma);
            int messageCount = countStr.toInt();
            Serial.print("Found ");
            Serial.print(messageCount);
            Serial.println(" messages");
            
            // Now retrieve each message individually
            for (int i = 1; i <= messageCount; i++) { // Read all messages
                Serial.print("=== Retrieving message ");
                Serial.print(i);
                Serial.println(" ===");
                
                flushInput();
                char cmd[32];
                snprintf(cmd, sizeof(cmd), "AT+CMGR=%d", i);
                println(cmd);
                
                String msgResponse = getMultiLineResponse(3000);
                // The response is already printed by getMultiLineResponse
                
                delay(200); // Small delay between messages
            }
        }
    }
    
    Serial.println("=== SMS Storage Check Complete ===");
}

void SIM7600::checkAndStoreSMS() {
    if (!setSMSTextMode()) return;
    
    Serial.println("=== Checking and Storing SMS ===");
    
    // Check SMS storage status with AT+CPMS?
    flushInput();
    println("AT+CPMS?");
    
    String cpmsResponse = getATResponse();
    Serial.print("CPMS Response received, parsing for message count...");
    
    // Parse response like: +CPMS: "SM",2,50,"SM",2,50,"SM",2,50
    int firstComma = cpmsResponse.indexOf(',');
    if (firstComma != -1) {
        int secondComma = cpmsResponse.indexOf(',', firstComma + 1);
        if (secondComma != -1) {
            String countStr = cpmsResponse.substring(firstComma + 1, secondComma);
            int messageCount = countStr.toInt();
            Serial.print("Found ");
            Serial.print(messageCount);
            Serial.println(" messages");
            
            // Now retrieve and store each message individually
            for (int i = 1; i <= messageCount; i++) { // Read all messages
                Serial.print("=== Processing message ");
                Serial.print(i);
                Serial.println(" ===");
                
                flushInput();
                char cmd[32];
                snprintf(cmd, sizeof(cmd), "AT+CMGR=%d", i);
                println(cmd);
                
                String msgResponse = getMultiLineResponse(3000);
                
                // Parse the CMGR response
                SMSMessage sms = parseCMGRResponse(msgResponse);
                if (sms.content.length() > 0) {
                    Serial.print("📧 Parsed SMS - From: ");
                    Serial.print(sms.sender);
                    Serial.print(" Time: ");
                    Serial.print(sms.timestamp);
                    Serial.print(" Content: ");
                    Serial.println(sms.content.substring(0, 30) + "...");
                    Serial.print("📁 Generated filename: sms_");
                    Serial.print(sms.fileId);
                    Serial.println(".txt");
                    
                    // Try to store to SD card
                    if (storeSMSToSD(sms)) {
                        Serial.println("✅ SMS stored to SD card successfully");
                    } else {
                        Serial.println("❌ Failed to store SMS or already exists");
                    }
                } else {
                    Serial.println("⚠️ Failed to parse SMS message");
                }
                
                delay(200); // Small delay between messages
            }
        }
    }
    
    Serial.println("=== SMS Storage Complete ===");
}

bool SIM7600::listAllSMS() {
    checkAndStoreSMS();
    return true; // Always return true since checkAndStoreSMS handles everything
}

// Parse CMGR/CMGRD response format: +CMGR: "REC READ","+16512524765","","25/12/25,17:48:42-32"
SIM7600::SMSMessage SIM7600::parseCMGRResponse(const String& response) {
    SMSMessage sms;
    
    Serial.print("[SMS PARSE] Raw response length: ");
    Serial.println(response.length());
    Serial.println("[SMS PARSE] Raw response:");
    Serial.println(response);
    Serial.print("[SMS PARSE] First 10 chars as hex: ");
    for (int i = 0; i < min(10, (int)response.length()); i++) {
        if (response[i] < 16) Serial.print("0");
        Serial.print(response[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
    
    // Find the +CMGR or +CMGRD line
    int cmgrStart = response.indexOf("+CMGR:");
    if (cmgrStart == -1) {
        cmgrStart = response.indexOf("+CMGRD:");
    }
    if (cmgrStart == -1) {
        Serial.print("[SMS PARSE] ERROR: No +CMGR or +CMGRD found. Looking for literal strings...");
        // Debug: search for any occurrence of "CMGR"
        int cmgrAny = response.indexOf("CMGR");
        Serial.print(" Found 'CMGR' at position: ");
        Serial.println(cmgrAny);
        return sms;
    }
    
    // Extract the line
    int lineEnd = response.indexOf('\n', cmgrStart);
    if (lineEnd == -1) lineEnd = response.length();
    
    String cmgrLine = response.substring(cmgrStart, lineEnd);
    
    // Parse the CMGR line: +CMGR: "status","sender","","timestamp"
    int colonPos = cmgrLine.indexOf(':');
    if (colonPos == -1) return sms;
    
    String params = cmgrLine.substring(colonPos + 1);
    params.trim();
    
    // Split by commas, handling quoted strings
    int currentPos = 0;
    String parts[5];
    int partIndex = 0;
    
    while (currentPos < (int)params.length() && partIndex < 5) {
        if (params[currentPos] == '"') {
            currentPos++; // Skip opening quote
            int closeQuote = params.indexOf('"', currentPos);
            if (closeQuote != -1) {
                parts[partIndex] = params.substring(currentPos, closeQuote);
                currentPos = closeQuote + 1;
                // Skip comma and space
                while (currentPos < (int)params.length() && (params[currentPos] == ',' || params[currentPos] == ' ')) {
                    currentPos++;
                }
            } else {
                break;
            }
        } else {
            int nextComma = params.indexOf(',', currentPos);
            if (nextComma == -1) nextComma = params.length();
            parts[partIndex] = params.substring(currentPos, nextComma);
            currentPos = nextComma + 1;
        }
        partIndex++;
    }
    
    Serial.print("[SMS PARSE] Parsed parts count: ");
    Serial.println(partIndex);
    for (int i = 0; i < partIndex; i++) {
        Serial.print("[SMS PARSE] Part ");
        Serial.print(i);
        Serial.print(": '");
        Serial.print(parts[i]);
        Serial.println("'");
    }
    
    if (partIndex >= 4) {
        sms.status = parts[0];
        sms.sender = parts[1];
        sms.timestamp = parts[3]; // Skip parts[2] which is usually empty
        
        Serial.print("[SMS PARSE] Status: ");
        Serial.println(sms.status);
        Serial.print("[SMS PARSE] Sender: ");
        Serial.println(sms.sender);
        Serial.print("[SMS PARSE] Timestamp: ");
        Serial.println(sms.timestamp);
        
        // Find content after the CMGR line
        int contentStart = response.indexOf('\n', cmgrStart);
        if (contentStart != -1) {
            contentStart++; // Skip newline
            int contentEnd = response.indexOf("\r\n\r\nOK", contentStart);
            if (contentEnd == -1) {
                contentEnd = response.indexOf("\r\nOK", contentStart);
            }
            if (contentEnd == -1) {
                // If no OK found, use the end of the response
                contentEnd = response.length();
                // Remove any trailing whitespace/control characters
                while (contentEnd > contentStart && 
                       (response[contentEnd-1] == '\r' || 
                        response[contentEnd-1] == '\n' || 
                        response[contentEnd-1] == ' ')) {
                    contentEnd--;
                }
            }
            if (contentEnd > contentStart) {
                sms.content = response.substring(contentStart, contentEnd);
                sms.content.trim();
            }
            
            Serial.print("[SMS PARSE] Content start: ");
            Serial.print(contentStart);
            Serial.print(", end: ");
            Serial.println(contentEnd);
            Serial.print("[SMS PARSE] Content: '");
            Serial.print(sms.content);
            Serial.println("'");
        }
        
        // Generate file ID from timestamp
        sms.fileId = formatTimestampForFile(sms.timestamp);
    }
    
    return sms;
}

// Convert timestamp "25/12/25,17:48:42-32" to "251225_174842"
String SIM7600::formatTimestampForFile(const String& timestamp) {
    String fileId = "";
    
    int commaPos = timestamp.indexOf(',');
    if (commaPos != -1) {
        String datePart = timestamp.substring(0, commaPos);
        String timePart = timestamp.substring(commaPos + 1);
        
        // Remove slashes from date: "25/12/25" -> "251225"
        datePart.replace("/", "");
        
        // Remove colons and timezone from time: "17:48:42-32" -> "174842"
        int dashPos = timePart.indexOf('-');
        if (dashPos != -1) {
            timePart = timePart.substring(0, dashPos);
        }
        timePart.replace(":", "");
        
        fileId = datePart + "_" + timePart;
    }
    
    return fileId;
}

// Store SMS to SD card in p26.py format
bool SIM7600::storeSMSToSD(const SMSMessage& sms) {
    if (sms.fileId.length() == 0 || sms.content.length() == 0) {
        return false;
    }
    
    String filename = "sms_" + sms.fileId + ".txt";
    
    // Convert to char array for SD library
    char filenameChar[64];
    filename.toCharArray(filenameChar, sizeof(filenameChar));
    
    Serial.print("🔍 Checking if file exists: ");
    Serial.println(filename);
    
    // Check if file already exists
    if (sd.exists(filenameChar)) {
        Serial.print("📄 File already exists on SD card: ");
        Serial.println(filename);
        return false; // Don't overwrite existing files
    }
    
    Serial.print("💾 Creating new file on SD card: ");
    Serial.println(filename);
    
    // Create and write the file
    FsFile file = sd.open(filenameChar, O_WRITE | O_CREAT);
    if (file) {
        file.print("From: ");
        file.println(sms.sender);
        file.print("Time: ");
        file.println(sms.timestamp);
        file.print("Status: ");
        file.println(sms.status);
        file.print("Content: ");
        file.println(sms.content);
        file.close();
        
        Serial.print("✅ Successfully wrote SMS to SD card: ");
        Serial.println(filename);
        Serial.println("📝 File contents written:");
        Serial.print("  From: ");
        Serial.println(sms.sender);
        Serial.print("  Time: ");
        Serial.println(sms.timestamp);
        Serial.print("  Content: ");
        Serial.println(sms.content.substring(0, 50) + "...");
        return true;
    } else {
        Serial.print("❌ Failed to create file on SD card: ");
        Serial.println(filename);
        return false;
    }
}

// Call Functions
bool SIM7600::makeCall(const char* phoneNumber) {
    // Set audio route to headphones
    sendATCommand("AT+CSDVC=1");
    delay(200);
    
    // Set volume
    sendATCommand("AT+CLVL=5");
    delay(200);
    
    char command[64];
    snprintf(command, sizeof(command), "ATD+%s;", phoneNumber);
    return sendATCommand(command, 2000);
}

bool SIM7600::answerCall() {
    return sendATCommand("ATA");
}

bool SIM7600::hangUp() {
    return sendATCommand("AT+CHUP");
}

bool SIM7600::enableCallerID() {
    return sendATCommand("AT+CLIP=1");
}

bool SIM7600::setAudioRoute(uint8_t route) {
    char command[32];
    snprintf(command, sizeof(command), "AT+CSDVC=%d", route);
    return sendATCommand(command);
}

bool SIM7600::setVolume(uint8_t level) {
    char command[32];
    snprintf(command, sizeof(command), "AT+CLVL=%d", level);
    return sendATCommand(command);
}

// Network Functions
int SIM7600::getSignalQuality() {
    if (!sendATCommand("AT+CSQ")) return -1;
    
    String response = getATResponse();
    int start = response.indexOf("+CSQ: ") + 6;
    if (start < 6) return -1;
    
    int end = response.indexOf(",", start);
    if (end == -1) return -1;
    
    return response.substring(start, end).toInt();
}

bool SIM7600::getNetworkStatus() {
    return sendATCommand("AT+COPS?");
}

String SIM7600::getNetworkTime() {
    flushInput();
    println("AT+CCLK?");
    
    String response = getATResponse();
    
    // Parse response: +CCLK: "26/01/05,19:30:45-32"
    int startQuote = response.indexOf('"');
    if (startQuote != -1) {
        int endQuote = response.indexOf('"', startQuote + 1);
        if (endQuote != -1) {
            String networkTime = response.substring(startQuote + 1, endQuote);
            Serial.print("[NETWORK TIME] Retrieved: ");
            Serial.println(networkTime);
            return networkTime;
        }
    }
    
    Serial.println("[NETWORK TIME] Failed to get network time");
    return "";
}

// Low-level AT command interface
bool SIM7600::sendATCommand(const char* command, unsigned long timeout) {
    flushInput();
    println(command);
    return waitForResponse("OK", timeout);
}

String SIM7600::getATResponse() {
    String response = "";
    unsigned long startTime = millis();
    bool firstChar = true;
    
    Serial.print("[SIM7600 RESPONSE] ");
    
    while (millis() - startTime < 1000) {
        if (available()) {
            if (firstChar) {
                Serial.print("\"");
                firstChar = false;
            }
            char c = read();
            response += c;
            
            // Show readable characters
            if (c == '\r') {
                Serial.print("\\r");
            } else if (c == '\n') {
                Serial.print("\\n");
            } else if (c >= 32 && c <= 126) {
                Serial.print(c);
            } else {
                Serial.print("[0x");
                if (c < 16) Serial.print("0");
                Serial.print(c, HEX);
                Serial.print("]");
            }
            
            if (response.endsWith("\r\nOK\r\n") || response.endsWith("\r\nERROR\r\n")) {
                Serial.println("\" - Complete response received");
                break;
            }
        }
        delay(1);
    }
    
    if (!firstChar) {
        Serial.println("\" - Response timeout or incomplete");
    } else {
        Serial.println("No response data received");
    }
    
    return response;
}

String SIM7600::getMultiLineResponse(unsigned long timeout) {
    String response = "";
    unsigned long startTime = millis();
    bool firstChar = true;
    unsigned long lastCharTime = millis();
    
    Serial.print("[SIM7600 MULTILINE] ");
    
    while (millis() - startTime < timeout) {
        if (available()) {
            if (firstChar) {
                Serial.print("\"");
                firstChar = false;
            }
            char c = read();
            response += c;
            lastCharTime = millis();
            
            // Show readable characters
            if (c == '\r') {
                Serial.print("\\r");
            } else if (c == '\n') {
                Serial.print("\\n");
            } else if (c >= 32 && c <= 126) {
                Serial.print(c);
            } else {
                Serial.print("[0x");
                if (c < 16) Serial.print("0");
                Serial.print(c, HEX);
                Serial.print("]");
            }
            
            // Check if we've received a complete response (ends with \r\nOK\r\n)
            if (response.endsWith("\r\nOK\r\n")) {
                Serial.println("\" - Complete multiline response received");
                return response;
            }
        } else {
            // If no characters for 1000ms and we have some data, consider it complete
            if (!firstChar && (millis() - lastCharTime > 1000)) {
                Serial.println("\" - Multiline response complete (no more data)");
                return response;
            }
        }
        delay(1);
    }
    
    if (!firstChar) {
        Serial.println("\" - Multiline response timeout");
    } else {
        Serial.println("No multiline response received");
    }
    
    return response;
}

void SIM7600::debugRawResponse(const char* command, unsigned long timeout) {
    Serial.print("=== RAW DEBUG: Sending command: ");
    Serial.println(command);
    
    flushInput();
    println(command);
    
    Serial.println("=== RAW DEBUG: Received response (raw):");
    Serial.print("RAW: \"");
    
    String rawResponse = "";
    unsigned long startTime = millis();
    int byteCount = 0;
    
    while (millis() - startTime < timeout) {
        if (available()) {
            uint8_t b = read();
            rawResponse += (char)b;
            
            // Convert to readable ASCII
            if (b == '\r') {
                Serial.print("\\r");
            } else if (b == '\n') {
                Serial.print("\\n");
            } else if (b >= 32 && b <= 126) {
                Serial.print((char)b);
            } else {
                Serial.print("[0x");
                if (b < 16) Serial.print("0");
                Serial.print(b, HEX);
                Serial.print("]");
            }
            
            byteCount++;
            startTime = millis(); // Reset timeout on each byte
        }
        delay(1);
    }
    
    Serial.println("\"");
    
    // Now try to filter out doubled characters
    Serial.println("=== RAW DEBUG: Filtered response:");
    Serial.print("FILTERED: \"");
    String filtered = "";
    
    for (int i = 0; i < (int)rawResponse.length(); i++) {
        char c = rawResponse[i];
        // Skip if this character is the same as the previous one (simple de-duplication)
        if (i == 0 || c != rawResponse[i-1]) {
            filtered += c;
            if (c == '\r') {
                Serial.print("\\r");
            } else if (c == '\n') {
                Serial.print("\\n");
            } else if (c >= 32 && c <= 126) {
                Serial.print(c);
            } else {
                Serial.print("[0x");
                if (c < 16) Serial.print("0");
                Serial.print((int)c, HEX);
                Serial.print("]");
            }
        }
    }
    
    Serial.println("\"");
    Serial.print("=== RAW DEBUG: Total bytes received: ");
    Serial.println(byteCount);
    Serial.println("=== RAW DEBUG: End");
}

void SIM7600::flushInput() {
    while (available()) {
        read();
        delay(1);
    }
}

// Helper functions
bool SIM7600::waitForResponse(const char* expected, unsigned long timeout) {
    String response = "";
    unsigned long startTime = millis();
    unsigned long lastCharTime = millis();
    bool firstChar = true;
    
    Serial.print("[SIM7600 RX] ");
    
    while (millis() - startTime < timeout) {
        if (available()) {
            if (firstChar) {
                Serial.print("\"");
                firstChar = false;
            }
            char c = read();
            response += c;
            lastCharTime = millis();
            
            // Show readable characters
            if (c == '\r') {
                Serial.print("\\r");
            } else if (c == '\n') {
                Serial.print("\\n");
            } else if (c >= 32 && c <= 126) {
                Serial.print(c);
            } else {
                Serial.print("[0x");
                if (c < 16) Serial.print("0");
                Serial.print(c, HEX);
                Serial.print("]");
            }
        } else {
            // If no new characters for 200ms and we have some data, consider response complete
            if (!firstChar && (millis() - lastCharTime > 200)) {
                break;
            }
        }
        delay(1);
    }
    
    if (!firstChar) {
        Serial.println("\"");
        Serial.print("[SIM7600 RX] Complete response: ");
        Serial.print(response.length());
        Serial.print(" bytes, contains '");
        Serial.print(expected);
        Serial.print("': ");
        Serial.println(response.indexOf(expected) != -1 ? "YES" : "NO");
        return response.indexOf(expected) != -1;
    } else {
        Serial.println("No response received - timeout");
        return false;
    }
}

String SIM7600::readLine(unsigned long timeout) {
    String line = "";
    unsigned long startTime = millis();
    
    while (millis() - startTime < timeout) {
        if (available()) {
            char c = read();
            if (c == '\r') continue;
            if (c == '\n') break;
            line += c;
        }
        delay(1);
    }
    
    return line;
}

void SIM7600::println(const char* str) {
    Serial.print("[SIM7600 TX] \"");
    Serial.print(str);
    Serial.println("\\r\"");
    
    if (_useHwSerial) {
        _hwSerial->print(str);
        _hwSerial->print('\r');  // Send only \r, not \r\n
    } else {
        _swSerial->print(str);
        _swSerial->print('\r');  // Send only \r, not \r\n
    }
}

void SIM7600::print(const char* str) {
    Serial.print("[SIM7600 TX] \"");
    Serial.print(str);
    Serial.println("\"");
    
    if (_useHwSerial) {
        _hwSerial->print(str);
    } else {
        _swSerial->print(str);
    }
}

int SIM7600::available() {
    if (_useHwSerial) {
        return _hwSerial->available();
    } else {
        return _swSerial->available();
    }
}

char SIM7600::read() {
    if (_useHwSerial) {
        return _hwSerial->read();
    } else {
        return _swSerial->read();
    }
}