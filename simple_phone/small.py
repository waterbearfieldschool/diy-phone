import time
import board
import busio
import displayio
import framebufferio
import sharpdisplay
import digitalio
import gc
from adafruit_display_text.label import Label
from terminalio import FONT

# Memory optimization: Reduced contact list and constants
CONTACTS = [["Don", 16512524765], ["David", 15304929688], ["me", 17819189114]]
MY_NUMBER = 17819189114
MAX_MESSAGES = 3  # Reduced from 6
MAX_MSG_LEN = 30  # Reduced from 45
CHECK_INTERVAL = 30  # Reduced from 60

# Simple button setup
btn = digitalio.DigitalInOut(board.D9)
btn.direction = digitalio.Direction.INPUT
btn.pull = digitalio.Pull.UP

# Display setup
displayio.release_displays()
spi = board.SPI()
display = framebufferio.FramebufferDisplay(
    sharpdisplay.SharpMemoryFramebuffer(spi, board.D10, width=400, height=240)
)

# Minimal UI labels
lbl_to = Label(FONT, text="TO: " + CONTACTS[0][0], x=0, y=10)
lbl_input = Label(FONT, text="> ", x=0, y=25)
lbl_msgs = Label(FONT, text="(no messages)", x=0, y=45)
lbl_status = Label(FONT, text="Ready", x=0, y=220)

ui = displayio.Group()
ui.append(lbl_to)
ui.append(lbl_input)
ui.append(lbl_msgs)
ui.append(lbl_status)
display.root_group = ui

# UART for SIM7600
uart = busio.UART(board.TX, board.RX, baudrate=115200, timeout=0, receiver_buffer_size=1024)

# Memory-efficient message storage (tuples instead of lists)
messages = []  # [(time_str, from_name, msg_text)]
contact_idx = 0
input_text = ""
last_check = 0

def gc_collect():
    """Force garbage collection and show memory"""
    gc.collect()
    print(f"Free memory: {gc.mem_free()}")

def send_at(cmd, wait=0.2):
    """Send AT command and return response"""
    uart.write(bytes(cmd + '\r', "ascii"))
    time.sleep(wait)
    return uart.read(uart.in_waiting)

def get_time():
    """Get network time - simplified"""
    try:
        data = send_at("AT+CCLK?").decode()
        time_part = data.split('"')[1].split(',')[1][:5]  # Just HH:MM
        return time_part
    except:
        return "??:??"

def check_messages():
    """Check for new messages - memory optimized"""
    global messages
    lbl_status.text = "Checking..."
    
    try:
        send_at("AT+CMGF=1")
        data = send_at("AT+CMGL=\"ALL\"", 0.5).decode()
        lines = [l for l in data.split('\r\n') if l.strip()]
        
        # Clear old messages to save memory
        messages.clear()
        
        # Parse messages (simplified)
        i = 1
        while i < len(lines) - 2:
            if "+CMGL:" in lines[i]:
                try:
                    header = lines[i].split(',')
                    sender = header[2].strip('"')
                    msg_text = lines[i+1][:MAX_MSG_LEN]  # Truncate long messages
                    
                    # Find sender name
                    sender_name = sender
                    for contact in CONTACTS:
                        if str(contact[1]) in sender:
                            sender_name = contact[0]
                            break
                    
                    time_str = get_time()
                    messages.append((time_str, sender_name, msg_text))
                    
                    # Keep only recent messages
                    if len(messages) > MAX_MESSAGES:
                        messages.pop(0)
                        
                except:
                    pass
                i += 2
            else:
                i += 1
                
        # Update display
        if messages:
            msg_display = ""
            for msg in messages[-3:]:  # Show last 3
                msg_display += f"{msg[0]} {msg[1]}: {msg[2]}\n"
            lbl_msgs.text = msg_display.strip()
        else:
            lbl_msgs.text = "(no messages)"
            
        lbl_status.text = f"Found {len(messages)} msgs"
        
    except Exception as e:
        lbl_status.text = "Check failed"
        print(f"Error: {e}")
    
    gc_collect()

def send_message(text):
    """Send SMS - simplified"""
    global messages
    contact = CONTACTS[contact_idx]
    lbl_status.text = "Sending..."
    
    try:
        send_at("AT+CMGF=1")
        send_at(f'AT+CMGS="+{contact[1]}"', 0.5)
        response = send_at(text + '\x1a', 2).decode()
        
        if "+CMGS:" in response:
            # Add to local messages
            time_str = get_time()
            messages.append((time_str, "Me", text[:MAX_MSG_LEN]))
            if len(messages) > MAX_MESSAGES:
                messages.pop(0)
            lbl_status.text = "Sent!"
        else:
            lbl_status.text = "Send failed"
            
    except Exception as e:
        lbl_status.text = "Send error"
        print(f"Send error: {e}")
    
    gc_collect()

def handle_input(char):
    """Handle keyboard input - simplified"""
    global input_text, contact_idx
    
    if char == '\r':  # Enter - send message
        if input_text.strip():
            send_message(input_text.strip())
            input_text = ""
            lbl_input.text = "> "
    elif char == '\x08':  # Backspace
        if input_text:
            input_text = input_text[:-1]
            lbl_input.text = "> " + input_text
    elif char == '\t':  # Tab - next contact
        contact_idx = (contact_idx + 1) % len(CONTACTS)
        lbl_to.text = "TO: " + CONTACTS[contact_idx][0]
    elif len(char) == 1 and ord(char) >= 32:  # Printable char
        if len(input_text) < 50:  # Limit input length
            input_text += char
            lbl_input.text = "> " + input_text

# Initialize
lbl_status.text = "Booting..."
time.sleep(1)
send_at("AT")  # Wake up modem
check_messages()
last_check = time.monotonic()
gc_collect()

# Main loop
while True:
    # Check button (delete all messages)
    if not btn.value:
        lbl_status.text = "Deleting..."
        send_at("AT+CMGD=,4", 1)  # Delete all
        messages.clear()
        lbl_msgs.text = "(no messages)"
        lbl_status.text = "Deleted"
        time.sleep(1)
    
    # Handle UART data
    data = uart.read(uart.in_waiting)
    if data:
        try:
            text = data.decode()
            if "RING" in text:
                lbl_status.text = "INCOMING CALL"
            elif "+CMT:" in text or "SM" in text:
                lbl_status.text = "NEW SMS"
                check_messages()
        except:
            pass
    
    # Periodic message check
    if time.monotonic() - last_check > CHECK_INTERVAL:
        check_messages()
        last_check = time.monotonic()
    
    # Handle any keyboard input (simplified - would need actual keyboard code)
    # This is a placeholder for I2C keyboard integration
    
    time.sleep(0.1)  # Small delay to prevent overwhelming