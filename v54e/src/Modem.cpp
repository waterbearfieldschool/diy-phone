#include "Modem.h"

#include "Log.h"

namespace {

// Unsolicited result codes we act on, plus the ones we merely want kept out of
// command replies.
const char *const NOTIFICATION_PREFIXES[] = {
    "RING", "+CLIP:", "+CMTI:", "NO CARRIER", "+CEND:",
    "VOICE CALL:", "MISSED_CALL:", "NO ANSWER", "BUSY",
};

// Splits an AT reply's comma-separated parameter list, honouring quotes.
int splitParams(const String &params, String *out, int max) {
  int count = 0;
  unsigned pos = 0;

  while (pos < params.length() && count < max) {
    if (params[pos] == '"') {
      const int close = params.indexOf('"', ++pos);
      if (close < 0) break;
      out[count++] = params.substring(pos, close);
      pos = close + 1;
      while (pos < params.length() && (params[pos] == ',' || params[pos] == ' ')) pos++;
    } else {
      int comma = params.indexOf(',', pos);
      if (comma < 0) comma = params.length();
      out[count++] = params.substring(pos, comma);
      pos = comma + 1;
    }
  }
  return count;
}

}  // namespace

bool Modem::begin(uint32_t baud, uint8_t rxPin, uint8_t txPin) {
  serial_->setPins(rxPin, txPin);
  serial_->begin(baud);
  delay(1000);
  drain();

  // Echo off, so command replies do not start with the command itself.
  command("ATE0", nullptr, 2000);
  drain();

  return command("AT");
}

bool Modem::isResponding() { return command("AT"); }

// ------------------------------------------------------------------ transport

void Modem::drain() {
  while (serial_->available()) serial_->read();
  rx_ = "";
}

void Modem::writeLine(const char *text) {
  LOGD("modem", "TX %s", text);
  serial_->print(text);
  serial_->print('\r');  // the SIM7600 wants a bare CR
}

bool Modem::nextLine(String &out, uint32_t timeoutMs) {
  const uint32_t start = millis();
  for (;;) {
    while (serial_->available()) {
      const char c = serial_->read();
      if (c == '\r' || c == '\n') {
        if (rx_.length() > 0) {
          out = rx_;
          rx_ = "";
          LOGD("modem", "RX %s", out.c_str());
          return true;
        }
      } else if (rx_.length() < MAX_LINE_CHARS) {
        rx_ += c;
      } else {
        // A line this long means the modem is streaming something we cannot
        // interpret. Drop it rather than let the buffer grow without bound.
        LOGE("modem", "oversized line discarded");
        rx_ = "";
      }
    }
    if (millis() - start >= timeoutMs) return false;
    delay(1);
  }
}

bool Modem::waitForPrompt(uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    while (serial_->available()) {
      if (serial_->read() == '>') return true;
    }
    delay(1);
  }
  LOGE("modem", "no '>' prompt");
  return false;
}

bool Modem::isNotification(const String &line) {
  for (const char *prefix : NOTIFICATION_PREFIXES) {
    if (line.startsWith(prefix)) return true;
  }
  return false;
}

void Modem::enqueue(const String &line) {
  const uint8_t next = (uint8_t)((queueTail_ + 1) % QUEUE_SIZE);
  if (next == queueHead_) {
    LOGE("modem", "notification queue full, dropping: %s", line.c_str());
    return;
  }
  queue_[queueTail_] = line;
  queueTail_ = next;
}

void Modem::poll() {
  // Outside a command, everything arriving is unsolicited.
  String line;
  while (nextLine(line, 0)) {
    if (isNotification(line)) enqueue(line);
    else LOGD("modem", "ignored: %s", line.c_str());
  }

  while (queueHead_ != queueTail_) {
    const String pending = queue_[queueHead_];
    queueHead_ = (uint8_t)((queueHead_ + 1) % QUEUE_SIZE);
    if (handler_) handler_(pending);
  }
}

bool Modem::command(const char *cmd, String *response, uint32_t timeoutMs) {
  writeLine(cmd);

  const uint32_t start = millis();
  String line;
  while (millis() - start < timeoutMs) {
    if (!nextLine(line, 20)) continue;

    if (isNotification(line)) {
      enqueue(line);  // handled later, from poll()
      continue;
    }
    if (line == "OK") return true;
    if (line == "ERROR" || line.startsWith("+CME ERROR") || line.startsWith("+CMS ERROR")) {
      LOGE("modem", "%s -> %s", cmd, line.c_str());
      return false;
    }
    if (line == cmd) continue;  // echo, if ATE0 did not take

    if (response) {
      if (response->length() > 0) *response += '\n';
      *response += line;
    }
  }

  LOGE("modem", "%s timed out", cmd);
  return false;
}

// ------------------------------------------------------------------------ SMS

bool Modem::textMode() { return command("AT+CMGF=1"); }

bool Modem::sendSms(const String &number, const String &body) {
  if (!textMode()) return false;

  // The modem wants the number in international form.
  const String dial = number.startsWith("+") ? number : "+" + number;

  char cmd[48];
  snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", dial.c_str());
  writeLine(cmd);

  if (!waitForPrompt(3000)) return false;

  serial_->print(body);
  serial_->write(0x1A);  // Ctrl-Z ends the message

  const uint32_t start = millis();
  String line;
  while (millis() - start < 30000) {  // sending can take many seconds
    if (!nextLine(line, 50)) continue;
    if (isNotification(line)) { enqueue(line); continue; }
    if (line == "OK") return true;
    if (line == "ERROR" || line.startsWith("+CMS ERROR")) {
      LOGE("modem", "send failed: %s", line.c_str());
      return false;
    }
  }
  LOGE("modem", "send timed out");
  return false;
}

bool Modem::readAndDeleteSms(uint8_t index, Sms &out) {
  if (!textMode()) return false;

  char cmd[24];
  snprintf(cmd, sizeof(cmd), "AT+CMGRD=%u", index);

  String reply;
  if (!command(cmd, &reply, 5000)) return false;

  out = parseSmsReply(reply);
  return out.body.length() > 0;
}

bool Modem::deleteSms(uint8_t index) {
  char cmd[24];
  snprintf(cmd, sizeof(cmd), "AT+CMGD=%u", index);
  return command(cmd, nullptr, 3000);
}

bool Modem::deleteAllSms() {
  if (!textMode()) return false;
  if (!command("AT+CPMS=\"SM\",\"SM\",\"SM\"", nullptr, 3000)) return false;
  return command("AT+CMGD=4", nullptr, 15000);  // 4 = delete all
}

bool Modem::smsSlots(int &used, int &total) {
  if (!textMode()) return false;

  String reply;
  if (!command("AT+CPMS?", &reply, 3000)) return false;

  // +CPMS: "SM",2,50,"SM",2,50,"SM",2,50
  const int colon = reply.indexOf(':');
  if (colon < 0) return false;

  String params[4];
  if (splitParams(reply.substring(colon + 1), params, 4) < 3) return false;

  used = params[1].toInt();
  total = params[2].toInt();
  return total > 0;
}

Modem::Sms Modem::parseSmsReply(const String &reply) {
  Sms sms;

  int header = reply.indexOf("+CMGRD:");
  if (header < 0) header = reply.indexOf("+CMGR:");
  if (header < 0) {
    LOGE("modem", "no +CMGR header in reply");
    return sms;
  }

  int headerEnd = reply.indexOf('\n', header);
  if (headerEnd < 0) headerEnd = reply.length();

  const int colon = reply.indexOf(':', header);
  String params[5];
  const int count = splitParams(reply.substring(colon + 1, headerEnd), params, 5);
  if (count < 4) {
    LOGE("modem", "unexpected +CMGR parameters (%d)", count);
    return sms;
  }

  // +CMGRD: "REC READ","+16175551234","","26/01/04,19:04:26-32"
  sms.status = params[0];
  sms.sender = params[1];
  sms.atTime = params[3];

  // Everything after the header line is the message body.
  if (headerEnd < (int)reply.length()) {
    sms.body = reply.substring(headerEnd + 1);
    sms.body.trim();
  }
  return sms;
}

// ---------------------------------------------------------------------- voice

bool Modem::call(const String &number) {
  // Route 3 = loudspeaker: the DFRobot board's amplified speaker jack hangs
  // off this output (1 is the unamplified handset earpiece channel).
  setAudioRoute(3);
  const String dial = number.startsWith("+") ? number : "+" + number;

  char cmd[32];
  snprintf(cmd, sizeof(cmd), "ATD%s;", dial.c_str());
  return command(cmd, nullptr, 5000);
}

bool Modem::answer() { return command("ATA", nullptr, 5000); }
bool Modem::hangUp() { return command("AT+CHUP", nullptr, 3000); }
bool Modem::enableCallerId() { return command("AT+CLIP=1"); }

bool Modem::setSpeakerVolume(uint8_t level) {
  char cmd[24];
  snprintf(cmd, sizeof(cmd), "AT+CLVL=%u", level);
  return command(cmd);
}

bool Modem::setMicGain(uint8_t level) {
  char cmd[24];
  snprintf(cmd, sizeof(cmd), "AT+CMICGAIN=%u", level);
  return command(cmd);
}

bool Modem::setAudioRoute(uint8_t route) {
  char cmd[24];
  snprintf(cmd, sizeof(cmd), "AT+CSDVC=%u", route);
  return command(cmd);
}

// -------------------------------------------------------------------- network

int Modem::signalQuality() {
  String reply;
  if (!command("AT+CSQ", &reply, 3000)) return -1;

  const int at = reply.indexOf("+CSQ:");
  if (at < 0) return -1;

  const int comma = reply.indexOf(',', at);
  if (comma < 0) return -1;

  return reply.substring(at + 5, comma).toInt();
}

bool Modem::networkStatus() {
  String reply;
  if (!command("AT+COPS?", &reply, 5000)) return false;
  LOGI("modem", "operator: %s", reply.c_str());
  return true;
}

String Modem::networkTime() {
  String reply;
  if (!command("AT+CCLK?", &reply, 3000)) return String();

  // +CCLK: "26/01/04,19:04:26-32"
  const int open = reply.indexOf('"');
  if (open < 0) return String();
  const int close = reply.indexOf('"', open + 1);
  if (close < 0) return String();

  return reply.substring(open + 1, close);
}
