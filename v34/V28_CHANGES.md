# DIY Phone v28 - Enhanced Thread Display

## Changes from v27

### New Conversation Display Format
Messages now display in format: **`sender: message time`**

### Examples:
- **Outgoing messages**: `me: Hello there! 14:23`
- **Incoming from contact**: `Alice: How are you? 14:22`  
- **Incoming from unknown**: `1234: Who is this? 14:21`

### Smart Sender Display Logic:
1. **Outgoing messages**: Always show `me:`
2. **Incoming with known contact**: Show contact name (max 8 chars)
3. **Incoming from unknown**: Show last 4 digits of phone number

### Technical Improvements:
- **Smart width calculation**: Adjusts message length based on sender name length
- **Color coding maintained**: 
  - Green = outgoing messages
  - White = incoming messages  
  - Cyan = timestamps
- **Space optimization**: Efficiently uses all available screen width

### Backward Compatibility:
- All v27 timezone features preserved
- Same debug functionality (press '9' to toggle)
- Compatible with all existing SMS file formats

## Memory Usage:
- **RAM**: 9.6% (23,972 bytes)
- **Flash**: 16.6% (135,068 bytes)

## Key Benefits:
✅ **Clear sender identification** for each message  
✅ **Consistent formatting** across all conversations  
✅ **Optimal space usage** with smart text wrapping  
✅ **Maintains all timezone-aware sorting** from v27  
✅ **Easy visual scanning** of conversation flow