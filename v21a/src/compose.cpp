void enterComposeMode() {
  composeMode = true;
  composingRecipient = true;
  composeRecipient = "";
  composeMessage = "";
  
  Serial.println("Entering compose mode");
  updateComposeScreen();
}

void exitComposeMode() {
  composeMode = false;
  composingRecipient = true;
  composeRecipient = "";
  composeMessage = "";
  
  Serial.println("Exiting compose mode");
  
  // Clear screen and restore inbox
  tft.fillScreen(ST77XX_BLACK);
  updateStatus("Back to Inbox", ST77XX_GREEN);
  updateInbox();
}

void updateComposeScreen() {
  // Clear screen
  tft.fillScreen(ST77XX_BLACK);
  
  // Header
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(0, 10);
  tft.print("Compose SMS - ESC to cancel");
  
  // To field
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(0, 30);
  tft.print("To: ");
  tft.setTextColor(composingRecipient ? ST77XX_YELLOW : ST77XX_WHITE);
  tft.print(composeRecipient);
  if (composingRecipient) {
    tft.print("_"); // Cursor
  }
  
  // Message field  
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(0, 60);
  tft.print("Message:");
  if (composingRecipient) {
    tft.setTextColor(ST77XX_GRAY);
    tft.setCursor(0, 80);
    tft.print("(Press ENTER after typing recipient)");
  } else {
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(0, 80);
    
    // Word wrap the message
    String displayMessage = composeMessage;
    if (!composingRecipient) {
      displayMessage += "_"; // Cursor
    }
    
    int x = 0, y = 80;
    int maxCharsPerLine = 53; // Approximate characters per line
    
    for (int i = 0; i < displayMessage.length(); i++) {
      if (x >= maxCharsPerLine || displayMessage[i] == '\n') {
        y += 10;
        x = 0;
        if (y > 200) break; // Stop if we run out of screen space
        tft.setCursor(0, y);
      }
      if (displayMessage[i] != '\n') {
        tft.print(displayMessage[i]);
        x++;
      }
    }
    
    // Instructions
    tft.setTextColor(ST77XX_GRAY);
    tft.setCursor(0, 220);
    tft.print("ENTER to send");
  }
}

void sendComposedMessage() {
  if (composeRecipient.length() == 0 || composeMessage.length() == 0) {
    updateStatus("Missing recipient or message!", ST77XX_RED);
    return;
  }
  
  updateStatus("Sending SMS...", ST77XX_YELLOW);
  Serial.print("Sending SMS to: ");
  Serial.print(composeRecipient);
  Serial.print(" Message: ");
  Serial.println(composeMessage);
  
  // Send the SMS
  bool success = cellular.sendSMS(composeRecipient.c_str(), composeMessage.c_str());
  
  if (success) {
    updateStatus("SMS sent successfully!", ST77XX_GREEN);
    Serial.println("SMS sent successfully");
  } else {
    updateStatus("Failed to send SMS", ST77XX_RED);
    Serial.println("Failed to send SMS");
  }
  
  delay(2000); // Show result for 2 seconds
  exitComposeMode();
}