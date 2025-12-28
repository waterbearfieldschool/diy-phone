#include "SIM7600.h"

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
            for (int i = 1; i <= messageCount && i <= 10; i++) { // Limit to 10 messages
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

bool SIM7600::listAllSMS() {
    checkSMSStorage();
    return true; // Always return true since checkSMSStorage handles everything
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
            // If no characters for 500ms and we have some data, consider it complete
            if (!firstChar && (millis() - lastCharTime > 500)) {
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
    
    for (int i = 0; i < rawResponse.length(); i++) {
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