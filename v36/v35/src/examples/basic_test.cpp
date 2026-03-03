// Basic SIM7600 functionality test example
// Uncomment this file and comment main.cpp to run this test

/*
#include <Arduino.h>
#include "SIM7600.h"

SIM7600 cellular(&Serial1);

void setup() {
    Serial.begin(115200);
    Serial.println("SIM7600 Basic Test");
    
    if (cellular.begin(115200)) {
        Serial.println("✓ SIM7600 connected");
        
        // Test signal quality
        int signal = cellular.getSignalQuality();
        Serial.print("Signal Quality: ");
        Serial.print(signal);
        Serial.println("/31");
        
        // Enable caller ID
        if (cellular.enableCallerID()) {
            Serial.println("✓ Caller ID enabled");
        }
        
        // Set SMS text mode
        if (cellular.setSMSTextMode()) {
            Serial.println("✓ SMS text mode enabled");
        }
        
        Serial.println("\nSIM7600 ready for commands!");
        Serial.println("Available commands:");
        Serial.println("- Send 'CALL <number>' to make a call");
        Serial.println("- Send 'SMS <number> <message>' to send SMS");
        Serial.println("- Send 'SIGNAL' to check signal quality");
        Serial.println("- Send 'LIST' to list SMS messages");
        
    } else {
        Serial.println("✗ SIM7600 connection failed");
    }
}

void loop() {
    // Check for serial commands
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        
        if (command.startsWith("CALL ")) {
            String number = command.substring(5);
            Serial.print("Making call to: ");
            Serial.println(number);
            
            if (cellular.makeCall(number.c_str())) {
                Serial.println("✓ Call initiated");
            } else {
                Serial.println("✗ Call failed");
            }
            
        } else if (command.startsWith("SMS ")) {
            int spaceIndex = command.indexOf(' ', 4);
            if (spaceIndex != -1) {
                String number = command.substring(4, spaceIndex);
                String message = command.substring(spaceIndex + 1);
                
                Serial.print("Sending SMS to ");
                Serial.print(number);
                Serial.print(": ");
                Serial.println(message);
                
                if (cellular.sendSMS(number.c_str(), message.c_str())) {
                    Serial.println("✓ SMS sent successfully");
                } else {
                    Serial.println("✗ SMS send failed");
                }
            } else {
                Serial.println("Usage: SMS <number> <message>");
            }
            
        } else if (command == "SIGNAL") {
            int signal = cellular.getSignalQuality();
            Serial.print("Signal Quality: ");
            Serial.print(signal);
            Serial.println("/31");
            
        } else if (command == "LIST") {
            Serial.println("Listing SMS messages...");
            if (cellular.listAllSMS()) {
                String response = cellular.getATResponse();
                Serial.println(response);
            } else {
                Serial.println("✗ Failed to list SMS");
            }
            
        } else if (command == "HANGUP") {
            if (cellular.hangUp()) {
                Serial.println("✓ Call ended");
            } else {
                Serial.println("✗ Hangup failed");
            }
            
        } else {
            Serial.println("Unknown command. Available: CALL, SMS, SIGNAL, LIST, HANGUP");
        }
    }
    
    delay(100);
}
*/