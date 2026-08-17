// Modem.h -- SIM7600 cellular modem over a hardware UART.
//
// This class is the sole owner of the modem UART. Unsolicited notifications
// (RING, +CMTI, ...) can arrive at any moment, including while an AT command is
// waiting for its reply, so every line read anywhere is classified: replies go
// to the caller, notifications go to a queue that poll() drains from the main
// loop. Dispatching from the queue rather than inline means a handler is free to
// issue its own AT commands without reentering a half-finished one.
#pragma once

#include <Arduino.h>

class Modem {
 public:
  struct Sms {
    String status;
    String sender;
    String atTime;  // raw modem timestamp, "YY/MM/DD,hh:mm:ss±zz"
    String body;
  };

  using NotificationHandler = void (*)(const String &line);

  // Uart rather than HardwareSerial: pin remapping is an nRF52 extension.
  explicit Modem(Uart *serial) : serial_(serial) {}

  bool begin(uint32_t baud, uint8_t rxPin, uint8_t txPin);
  bool isResponding();

  // Drains the UART and dispatches queued notifications. Call every loop.
  void poll();
  void onNotification(NotificationHandler handler) { handler_ = handler; }

  // --- SMS
  bool textMode();
  bool sendSms(const String &number, const String &body);
  // Reads slot `index` and deletes it from the SIM in one command (AT+CMGRD).
  bool readAndDeleteSms(uint8_t index, Sms &out);
  bool deleteSms(uint8_t index);
  bool deleteAllSms();               // bulk, after selecting SIM storage
  bool smsSlots(int &used, int &total);

  // --- Voice
  bool call(const String &number);
  bool answer();
  bool hangUp();
  bool enableCallerId();
  bool setSpeakerVolume(uint8_t level);  // AT+CLVL, 0-5
  bool setMicGain(uint8_t level);        // AT+CMICGAIN, 0-8
  bool setAudioRoute(uint8_t route);     // AT+CSDVC

  // --- Network
  int signalQuality();                   // 0-31, or -1
  bool networkStatus();
  String networkTime();                  // AT+CCLK?, empty on failure

  // Sends a command and waits for OK. Any informational lines are appended to
  // `response` when it is non-null.
  bool command(const char *cmd, String *response = nullptr, uint32_t timeoutMs = 2000);

  // Parses a +CMGR / +CMGRD reply body.
  static Sms parseSmsReply(const String &reply);

 private:
  bool nextLine(String &out, uint32_t timeoutMs);
  bool waitForPrompt(uint32_t timeoutMs);
  void writeLine(const char *text);
  void drain();
  static bool isNotification(const String &line);
  void enqueue(const String &line);

  static constexpr uint8_t QUEUE_SIZE = 8;
  static constexpr uint16_t MAX_LINE_CHARS = 512;

  Uart *serial_;
  NotificationHandler handler_ = nullptr;
  String rx_;                      // partial line being assembled
  String queue_[QUEUE_SIZE];
  uint8_t queueHead_ = 0;
  uint8_t queueTail_ = 0;
};
