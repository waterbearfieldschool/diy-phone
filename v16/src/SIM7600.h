#ifndef SIM7600_H
#define SIM7600_H

#include <Arduino.h>
#include <SoftwareSerial.h>

class SIM7600 {
public:
    SIM7600(HardwareSerial *serial);
    SIM7600(SoftwareSerial *serial);
    
    bool begin(unsigned long baud = 115200);
    bool isConnected();
    
    // SMS Functions
    bool setSMSTextMode();
    bool sendSMS(const char* phoneNumber, const char* message);
    bool deleteSMS(uint8_t index);
    bool deleteAllSMS();
    String readSMS(uint8_t index);
    int getNumSMS();
    bool listAllSMS();
    void checkSMSStorage();
    void checkAndStoreSMS();
    
    // SMS Storage Functions
    struct SMSMessage {
        String index;
        String status;
        String sender;
        String timestamp;
        String content;
        String fileId;
    };
    
    SMSMessage parseCMGRResponse(const String& response);
    String formatTimestampForFile(const String& timestamp);
    bool storeSMSToSD(const SMSMessage& sms);
    
    // Call Functions
    bool makeCall(const char* phoneNumber);
    bool answerCall();
    bool hangUp();
    bool enableCallerID();
    bool setAudioRoute(uint8_t route = 1);
    bool setVolume(uint8_t level = 5);
    
    // Network Functions
    int getSignalQuality();
    bool getNetworkStatus();
    
    // Low-level AT command interface
    bool sendATCommand(const char* command, unsigned long timeout = 1000);
    String getATResponse();
    String getMultiLineResponse(unsigned long timeout = 5000);
    void debugRawResponse(const char* command, unsigned long timeout = 3000);
    void flushInput();
    
private:
    HardwareSerial *_hwSerial;
    SoftwareSerial *_swSerial;
    bool _useHwSerial;
    
    // Helper functions
    bool waitForResponse(const char* expected, unsigned long timeout = 1000);
    String readLine(unsigned long timeout = 1000);
    void println(const char* str);
    void print(const char* str);
    int available();
    char read();
};

#endif