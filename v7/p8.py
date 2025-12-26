# SPDX-FileCopyrightText: 2021 ladyada for Adafruit Industries
# SPDX-License-Identifier: MIT

"""
This test will initialize the display using displayio and draw a solid green
background, a smaller purple rectangle, and some yellow text.
"""
import time
import adafruit_sdcard
import board
import displayio
import digitalio
import storage
import terminalio
import busio
from adafruit_display_text import label
from fourwire import FourWire
from adafruit_bus_device.i2c_device import I2CDevice
from adafruit_st7789 import ST7789
import supervisor
supervisor.runtime.autoreload = False


address_book = [["Don (voip)",16512524765],["Don (iphone)",17813230341],["Liz",16174299144]]

recipient_index=0
recipient=address_book[recipient_index]

# Address book functions
def save_address_book():
    try:
        with open("/sd/addressbook.txt", "w") as f:
            for entry in address_book:
                f.write(f"{entry[0]},{entry[1]}\n")
        print("Address book saved")
    except Exception as e:
        print(f"Error saving address book: {e}")

def load_address_book():
    global address_book
    try:
        with open("/sd/addressbook.txt", "r") as f:
            address_book = []
            for line in f.readlines():
                if line.strip():
                    parts = line.strip().split(',')
                    if len(parts) == 2:
                        name = parts[0]
                        number = int(parts[1])
                        address_book.append([name, number])
        print(f"Loaded {len(address_book)} contacts")
    except Exception as e:
        print(f"Error loading address book: {e}, using defaults")
        # Create default address book file
        save_address_book()

def lookup_contact_name(phone_number):
    # Clean phone number for comparison
    clean_number = str(phone_number).replace('+', '').replace('-', '').replace(' ', '')
    
    for entry in address_book:
        contact_number = str(entry[1]).replace('+', '').replace('-', '').replace(' ', '')
        if clean_number.endswith(contact_number) or contact_number.endswith(clean_number):
            return entry[0]
    
    # Return formatted phone number if not found
    if clean_number.startswith('1') and len(clean_number) == 11:
        return f"+1 {clean_number[1:4]}-{clean_number[4:7]}-{clean_number[7:]}"
    elif len(clean_number) == 10:
        return f"{clean_number[:3]}-{clean_number[3:6]}-{clean_number[6:]}"
    else:
        return clean_number

# Removed search_contacts function - no longer needed

spi = busio.SPI(board.SCK, board.MOSI, board.MISO)
cs = digitalio.DigitalInOut(board.D10)

sdcard = adafruit_sdcard.SDCard(spi, cs)
vfs = storage.VfsFat(sdcard)
storage.mount(vfs, "/sd")
    
with open("/sd/test.txt", "w") as f:
    f.write("Hello world!\r\n")

with open("/sd/test.txt", "r") as f:
    print("Read line from file:")
    print(f.readline())
    
    
# Release any resources currently in use for the displays
displayio.release_displays()

#spi = board.SPI()
spi = busio.SPI(clock=board.A2, MOSI=board.A0, MISO=board.A1)
tft_cs = board.A3
tft_dc = board.A5

display_bus = FourWire(spi, command=tft_dc, chip_select=tft_cs, reset=board.D12)

display = ST7789(display_bus, width=320, height=240, rotation=90)

# Make the display context
splash = displayio.Group()
display.root_group = splash

#color_bitmap = displayio.Bitmap(320, 240, 1)
#color_palette = displayio.Palette(1)
#color_palette[0] = 0x00FF00  # Bright Green

#bg_sprite = displayio.TileGrid(color_bitmap, #pixel_shader=color_palette, x=0, y=0)
#splash.append(bg_sprite)

# Draw a smaller inner rectangle
#inner_bitmap = displayio.Bitmap(280, 200, 1)
#inner_palette = displayio.Palette(1)
#inner_palette[0] = 0xAA0088  # Purple
#inner_sprite = displayio.TileGrid(inner_bitmap, #pixel_shader=inner_palette, x=20, y=20)
#splash.append(inner_sprite)

# Draw a label
#text_group = displayio.Group(scale=1, x=57, y=120)
#text = "Hello World!"
#text_area = label.Label(terminalio.FONT, text=text, color=0xFFFF00)
#text_group.append(text_area)  # Subgroup for text scaling
#splash.append(text_group)

#recipient_area = label.Label(terminalio.FONT, text="", color=0xFFFF00, x=5, y=5)

# Inbox view elements
inbox_title = label.Label(terminalio.FONT, text="INBOX", color=0x00FF00, scale=2, x=5, y=10)
status_area = label.Label(terminalio.FONT, text="", color=0xFFFF00, scale=1, x=5, y=220)

# Message list lines (showing multiple messages in inbox view)
msg_line1 = label.Label(terminalio.FONT, text="", color=0xFFFF00, scale=1, x=5, y=35)
msg_line2 = label.Label(terminalio.FONT, text="", color=0xFFFF00, scale=1, x=5, y=50)  
msg_line3 = label.Label(terminalio.FONT, text="", color=0xFFFF00, scale=1, x=5, y=65)
msg_line4 = label.Label(terminalio.FONT, text="", color=0xFFFF00, scale=1, x=5, y=80)
msg_line5 = label.Label(terminalio.FONT, text="", color=0xFFFF00, scale=1, x=5, y=95)
msg_line6 = label.Label(terminalio.FONT, text="", color=0xFFFF00, scale=1, x=5, y=110)
msg_line7 = label.Label(terminalio.FONT, text="", color=0xFFFF00, scale=1, x=5, y=125)
msg_line8 = label.Label(terminalio.FONT, text="", color=0xFFFF00, scale=1, x=5, y=140)

# Detail view elements (hidden initially)
detail_from = label.Label(terminalio.FONT, text="", color=0x00FF00, scale=1, x=5, y=35)
detail_time = label.Label(terminalio.FONT, text="", color=0x888888, scale=1, x=5, y=50)
detail_content1 = label.Label(terminalio.FONT, text="", color=0xFFFF00, scale=1, x=5, y=70)
detail_content2 = label.Label(terminalio.FONT, text="", color=0xFFFF00, scale=1, x=5, y=85)
detail_content3 = label.Label(terminalio.FONT, text="", color=0xFFFF00, scale=1, x=5, y=100)
detail_content4 = label.Label(terminalio.FONT, text="", color=0xFFFF00, scale=1, x=5, y=115)
detail_content5 = label.Label(terminalio.FONT, text="", color=0xFFFF00, scale=1, x=5, y=130)
detail_actions = label.Label(terminalio.FONT, text="", color=0x00FF00, scale=1, x=5, y=155)

# Compose view elements  
compose_title = label.Label(terminalio.FONT, text="", color=0x00FF00, scale=1, x=5, y=35)
compose_content = label.Label(terminalio.FONT, text="", color=0xFFFF00, scale=1, x=5, y=55)

msg_lines = [msg_line1, msg_line2, msg_line3, msg_line4, msg_line5, msg_line6, msg_line7, msg_line8]
detail_lines = [detail_from, detail_time, detail_content1, detail_content2, detail_content3, detail_content4, detail_content5, detail_actions]
compose_lines = [compose_title, compose_content]

# Add all elements to splash
splash.append(inbox_title)
splash.append(status_area)

for line in msg_lines:
    splash.append(line)
for line in detail_lines: 
    splash.append(line)
for line in compose_lines:
    splash.append(line)

i2c = board.I2C()

#while not i2c.try_lock():
#    pass
    
#i2c_devices=i2c.scan()
#print(i2c_devices)

#cardkb=95

kb = I2CDevice(i2c, 0x5F)

from adafruit_ticks import ticks_ms, ticks_add, ticks_diff, ticks_less

uart = busio.UART(board.D2, board.A4, baudrate=115200,timeout=0,receiver_buffer_size=5024)

ESC = chr(27)
NUL = '\x00'
CR = "\r"
LF = "\n"
TAB = bytearray(b'\x09')
LEFT = bytearray(b'\xB4')
RIGHT = bytearray(b'\xB7')
DOWN = bytearray(b'\xB6')
UP = bytearray(b'\xB5')
BACK = bytearray(b'\x08')
# Add new keys for inbox navigation
ENTER_KEY = bytearray(b'\r')  # For selecting message
B_KEY = b'b'  # For back
R_KEY = b'r'  # For reply
C_KEY = b'c'  # For compose
N_KEY = b'n'  # For new number entry / get new messages
D_KEY = b'd'  # For delete all from SIM
DEL_KEY = bytearray(b'\x7f')  # For delete
c = ''
b = bytearray(1)

instr = ''
radio_instr = ''

def parse_sms_message(line):
    if not line.startswith('+CMGL:'):
        return None
    
    try:
        # Parse: +CMGL: index,"status","sender","","timestamp"
        parts = line.split(',')
        
        index = parts[0].split(':')[1].strip()
        status = parts[1].strip().strip('"')
        sender = parts[2].strip().strip('"') 
        
        # Reconstruct timestamp from parts 4 and 5 (it got split by comma)
        timestamp_part1 = parts[4].strip().strip('"')  # "25/12/25"
        timestamp_part2 = parts[5].strip().strip('"')  # "16:25:06-32"
        timestamp = timestamp_part1 + ',' + timestamp_part2
        
        # Convert timestamp to filename-safe format: YYMMDD_HHMMSS
        # From: "25/12/25,16:25:06-32" to: "251225_162506"
        date_part, time_part = timestamp.split(',')
        time_clean = time_part.split('-')[0]
        file_timestamp = date_part.replace('/', '') + '_' + time_clean.replace(':', '')
        
        result = {
            'index': index,
            'status': status, 
            'sender': sender,
            'timestamp': timestamp,
            'file_id': file_timestamp
        }
        return result
    except Exception as e:
        print(f"Error parsing SMS: {e}")
        return None

def store_sms_to_sd(sms_data, content):
    try:
        filename = f"/sd/sms_{sms_data['file_id']}.txt"
        with open(filename, "w") as f:
            f.write(f"From: {sms_data['sender']}\n")
            f.write(f"Time: {sms_data['timestamp']}\n") 
            f.write(f"Status: {sms_data['status']}\n")
            f.write(f"Content: {content}\n")
        print(f"SMS stored: {filename}")
        return True
    except Exception as e:
        print(f"Error storing SMS: {e}")
        return False

def load_sms_from_sd(force_reload=False):
    global cached_sms_list, sms_cache_valid
    
    # Return cached data if valid and not forcing reload
    if sms_cache_valid and not force_reload:
        return cached_sms_list
    
    sms_list = []
    print("Loading SMS from SD card...")
    
    # Try different approaches to find SMS files
    try:
        import os
        files = os.listdir("/sd")
        print(f"Found {len(files)} files on SD card")
    except:
        print("os.listdir not available, trying manual scan...")
        # Manual file discovery for CircuitPython
        files = []
        # Try common timestamp patterns based on recent dates
        base_patterns = ["251225", "251226", "251227", "251228", "251229", "251230", "251231"]
        time_patterns = []
        for hour in range(24):
            for minute in range(0, 60, 5):  # Check every 5 minutes
                time_patterns.append(f"{hour:02d}{minute:02d}")
        
        for date in base_patterns:
            for time in time_patterns[:50]:  # Limit to avoid too many checks
                filename = f"sms_{date}_{time}*.txt"
                try:
                    # Try to open the file to see if it exists
                    test_name = f"sms_{date}_{time}00.txt"
                    with open(f"/sd/{test_name}", "r") as f:
                        files.append(test_name)
                        print(f"Found SMS file: {test_name}")
                except:
                    continue
                    
    # Process found files
    for filename in files:
        if filename.startswith("sms_") and filename.endswith(".txt"):
            try:
                filepath = f"/sd/{filename}"
                print(f"Trying to load: {filepath}")
                with open(filepath, "r") as f:
                    lines = f.readlines()
                    if len(lines) >= 4:
                        sms_data = {
                            'filename': filename,
                            'sender': lines[0].strip().replace('From: ', ''),
                            'time': lines[1].strip().replace('Time: ', ''),
                            'status': lines[2].strip().replace('Status: ', ''),
                            'content': lines[3].strip().replace('Content: ', '')
                        }
                        sms_list.append(sms_data)
                        print(f"Successfully loaded: {filename}")
                    else:
                        print(f"Invalid SMS file format: {filename}")
            except Exception as file_error:
                print(f"Error reading {filename}: {file_error}")
                
    # Sort by filename (which includes timestamp)
    sms_list.sort(key=lambda x: x['filename'])
    print(f"Total SMS loaded: {len(sms_list)}")
    
    if len(sms_list) == 0:
        print("No SMS files found. Check if messages are being stored correctly.")
    
    # Update cache
    cached_sms_list = sms_list
    sms_cache_valid = True
    
    return sms_list

def invalidate_sms_cache():
    global sms_cache_valid
    sms_cache_valid = False

# Global buffer for accumulating SMS notifications
sms_notification_buffer = ""

# Global UART buffer for complete line reading
uart_line_buffer = ""

def read_uart_lines():
    """Read UART data and return complete lines, buffering incomplete data"""
    global uart_line_buffer
    
    if uart.in_waiting > 0:
        # Read all available data
        new_data = uart.read(uart.in_waiting)
        if new_data:
            try:
                # Add to buffer
                uart_line_buffer += new_data.decode()
                
                # Extract complete lines
                lines = []
                while '\r\n' in uart_line_buffer:
                    line, uart_line_buffer = uart_line_buffer.split('\r\n', 1)
                    if line:  # Don't add empty lines
                        lines.append(line)
                
                return lines
                
            except Exception as e:
                print(f"Error reading UART: {e}")
                return []
    
    return []

def handle_incoming_sms_notification(data):
    """Handle +CMTI notification for new incoming SMS"""
    global sms_notification_buffer
    
    try:
        # Decode without keyword arguments (CircuitPython compatibility)
        data_str = data.decode()
        print(f"SMS notification received: {data_str}")
        
        # Accumulate data in buffer since notifications can be split
        sms_notification_buffer += data_str
        print(f"Buffer now contains: '{sms_notification_buffer}'")
        
        # Check if we have a complete CMTI notification 
        # Look for the reassembled +CMTI pattern with comma and line ending
        if ('CMTI:' in sms_notification_buffer or '+CMTI:' in sms_notification_buffer) and ',' in sms_notification_buffer and '\r\n' in sms_notification_buffer:
            print("Found complete CMTI notification")
            
            # Try to find and reconstruct the CMTI line
            buffer_clean = sms_notification_buffer.replace('\r\n', ' ').replace('\r', ' ').replace('\n', ' ')
            
            # Look for the CMTI pattern - might be split as +C MTI: or +CMTI:
            if 'MTI:' in buffer_clean:
                # Find the MTI: part and reconstruct
                parts = buffer_clean.split()
                cmti_line = None
                
                for i, part in enumerate(parts):
                    if 'MTI:' in part:
                        # Try to reconstruct +CMTI: line
                        if i > 0 and parts[i-1].endswith('+C'):
                            # Case: ['+C', 'MTI:', '"SM",9']
                            cmti_line = '+CMTI: ' + ' '.join(parts[i+1:])
                        elif part.startswith('+') or part.startswith('MTI:'):
                            # Case: ['MTI:', '"SM",9'] or ['+MTI:', '"SM",9']
                            if part.startswith('MTI:'):
                                cmti_line = '+C' + part + ' ' + ' '.join(parts[i+1:])
                            else:
                                cmti_line = part + ' ' + ' '.join(parts[i+1:])
                        break
                
                if cmti_line and ',' in cmti_line:
                    print(f"Reconstructed CMTI: {cmti_line}")
                    process_cmti_line(cmti_line)
                    sms_notification_buffer = ""  # Clear after processing
                else:
                    print("Could not reconstruct valid CMTI line")
            else:
                print("No MTI pattern found in buffer")
                
    except Exception as e:
        print(f"Error handling SMS notification: {e}")
        print(f"Buffer was: '{sms_notification_buffer}'")

def process_cmti_line(line):
    """Process a complete +CMTI line"""
    try:
        # Parse: +CMTI: "SM",6
        if ':' in line and ',' in line:
            # Split by comma to get the index
            parts = line.split(',')
            if len(parts) >= 2:
                try:
                    # Extract number from second part, removing quotes and whitespace
                    index_part = parts[1].strip().strip('"').strip()
                    message_index = int(index_part)
                    print(f"New SMS at index {message_index}")
                    
                    # Auto-retrieve the new message
                    retrieve_new_message(message_index)
                    
                except ValueError as ve:
                    print(f"Could not parse message index from '{parts[1]}': {ve}")
                    # Fallback: get all messages
                    auto_get_all_messages()
            else:
                print(f"Unexpected CMTI format: {line}")
                auto_get_all_messages()
        else:
            print(f"Invalid CMTI line format: {line}")
            
    except Exception as e:
        print(f"Error processing CMTI line: {e}")

def retrieve_new_message(message_index):
    """Retrieve a specific message by index"""
    try:
        print(f"Retrieving message {message_index}...")
        
        # Set text mode
        uart.write(bytes('AT+CMGF=1\r',"ascii"))
        time.sleep(.2)
        uart.read(uart.in_waiting)  # Clear response
        
        # Read the specific message
        uart.write(bytes(f'AT+CMGR={message_index}\r',"ascii"))
        time.sleep(.5)
        data = uart.read(uart.in_waiting).decode()
        print(f"Message {message_index} response: {data}")
        
        # Parse and store the message if valid
        lines = data.split('\r\n')
        current_sms = None
        
        for i, line in enumerate(lines):
            if line.startswith('+CMGR:'):
                # Parse message header: +CMGR: "REC UNREAD","+16512524765","","25/12/26,10:30:15-32"
                current_sms = parse_cmgr_message(line)
                print(f"Parsed message header: {current_sms}")
            elif current_sms and line.strip() and not line.startswith('AT') and not line.startswith('OK'):
                # This is the message content
                if store_sms_to_sd(current_sms, line.strip()):
                    print(f"Stored new message: {line.strip()}")
                    invalidate_sms_cache()  # Refresh cache
                    
                    # Update display if in inbox view
                    if current_view == "inbox":
                        refresh_inbox_highlighting()
                        
                break
                
    except Exception as e:
        print(f"Error retrieving message {message_index}: {e}")
        # Fallback: get all messages
        auto_get_all_messages()

def parse_cmgr_message(line):
    """Parse +CMGR response format"""
    try:
        print(f"Parsing CMGR line: {line}")
        # Parse: +CMGR: "REC UNREAD","+16512524765","","25/12/25,17:48:42-32"
        parts = line.split(',')
        print(f"CMGR parts: {parts}")
        
        if len(parts) >= 4:
            status = parts[0].split(':')[1].strip().strip('"')
            sender = parts[1].strip().strip('"')
            # Skip parts[2] (usually empty "")
            timestamp = parts[3].strip().strip('"')
            
            print(f"Extracted - status: {status}, sender: {sender}, timestamp: {timestamp}")
            
            # Convert timestamp to filename-safe format
            if ',' in timestamp:
                date_part, time_part = timestamp.split(',')
                time_clean = time_part.split('-')[0]
                file_timestamp = date_part.replace('/', '') + '_' + time_clean.replace(':', '')
            else:
                # Fallback if timestamp format is unexpected
                import time as time_mod
                file_timestamp = str(int(time_mod.time()))
                
            result = {
                'index': 'auto',
                'status': status,
                'sender': sender,
                'timestamp': timestamp,
                'file_id': file_timestamp
            }
            print(f"Created CMGR result: {result}")
            return result
        else:
            print(f"Insufficient CMGR parts: {len(parts)}")
            return None
            
    except Exception as e:
        print(f"Error parsing CMGR message: {e}")
        print(f"Line was: {line}")
        return None

def auto_get_all_messages():
    """Automatically get all messages (fallback for notification parsing errors)"""
    print("Auto-retrieving all messages...")
    get_messages()
    if current_view == "inbox":
        display_inbox()  # Force full refresh

def delete_all_messages():
    uart.write(bytes('AT+CMGF=1\r',"ascii"))
    time.sleep(.2)
    uart.write(bytes('AT+CMGDA="DEL ALL"\r',"ascii"))
    time.sleep(.5)
    data=uart.read(uart.in_waiting).decode()
    print("Delete result:", data)

def network_status():
    uart.write(bytes('AT+CSQ\r',"ascii"))
    time.sleep(.2)
    data=uart.read(uart.in_waiting).decode()
    print(data)

def get_messages():
    status_area.text='(checking messages...)'
    print("checking messages")
    uart.write(bytes('AT+CMGF=1\r',"ascii"))
    time.sleep(.2)
    data=uart.read(uart.in_waiting)
    print(data)
    
    uart.write(bytes('AT+CMGL=\"ALL\"\r',"ascii"))
    time.sleep(.3)
    data=uart.read(uart.in_waiting).decode()
    print(data)
    
    # Parse and store new SMS messages
    lines = data.split('\r\n')
    current_sms = None
    messages_stored = 0
    
    for i, line in enumerate(lines):
        if line.startswith('+CMGL:'):
            current_sms = parse_sms_message(line)
        elif current_sms and line.strip() and not line.startswith('AT') and not line.startswith('OK'):
            # This is the message content
            if store_sms_to_sd(current_sms, line.strip()):
                messages_stored += 1
            current_sms = None
    
    print(f"Total messages stored: {messages_stored}")
    if messages_stored > 0:
        invalidate_sms_cache()  # Force reload of SMS list
    status_area.text=''

# Global variables for inbox navigation  
current_view = "inbox"  # "inbox", "detail", "compose", "select_recipient"
selected_message_index = 0
inbox_scroll_offset = 0
compose_message = ""
selected_recipient_index = 0
manual_number_entry = ""
recipient_mode = "contacts"  # "contacts" or "manual"

# Cache for SMS list to avoid repeated SD card reads
cached_sms_list = []
sms_cache_valid = False

def hide_all_views():
    # Hide inbox view
    for line in msg_lines:
        line.text = ""
    
    # Hide detail view  
    for line in detail_lines:
        line.text = ""
        
    # Hide compose view
    for line in compose_lines:
        line.text = ""

def format_sender(sender):
    # Use contact name if available, otherwise format phone number
    contact_name = lookup_contact_name(sender)
    if len(contact_name) > 10:  # Truncate long names
        return contact_name[:8] + ".."
    return contact_name

def format_timestamp(timestamp):
    # Convert timestamp to "MM/DD HH:MM" format
    try:
        print(f"Formatting timestamp: '{timestamp}'")
        
        # Check if timestamp has time component
        if ',' in timestamp:
            # Full timestamp: "25/12/25,16:25:06-32"
            date_part, time_part = timestamp.split(',')
            time_clean = time_part.split('-')[0][:5]  # HH:MM
        else:
            # Date only: "25/12/25"
            date_part = timestamp
            time_clean = "00:00"  # Default time
            
        # Parse date parts
        date_components = date_part.split('/')
        if len(date_components) == 3:
            # Try different date formats
            if len(date_components[0]) == 2 and len(date_components[2]) == 2:
                # Could be YY/MM/DD or DD/MM/YY
                # Check if first component looks like year (20-30) or day (01-31)
                try:
                    first_num = int(date_components[0])
                    if first_num >= 20 and first_num <= 30:
                        # Likely YY/MM/DD format: "25/12/25" -> year=25, month=12, day=25
                        year, month, day = date_components
                    else:
                        # Likely DD/MM/YY format: "26/12/25" -> day=26, month=12, year=25  
                        day, month, year = date_components
                    
                    return f"{month}/{day} {time_clean}"
                except ValueError:
                    # If parsing numbers fails, use fallback
                    return f"{date_components[1]}/{date_components[0]} {time_clean}"
            else:
                # Fallback: assume middle is month, last is day
                return f"{date_components[1]}/{date_components[2]} {time_clean}"
        else:
            return timestamp[:8]  # Fallback
            
    except Exception as e:
        print(f"Error formatting timestamp '{timestamp}': {e}")
        return timestamp[:8] if len(timestamp) >= 8 else timestamp  # Fallback

def format_content_preview(content, max_length=15):
    # Truncate content for inbox preview (shorter to fit 3 columns)
    if len(content) > max_length:
        return content[:max_length] + "..."
    return content

def display_inbox():
    global selected_message_index, inbox_scroll_offset
    hide_all_views()
    
    print("Displaying inbox...")
    sms_list = load_sms_from_sd()
    print(f"SMS list has {len(sms_list)} messages")
    
    if not sms_list:
        msg_lines[0].text = "No messages"
        print("No messages to display")
        return
    
    # Ensure selected index is valid
    if selected_message_index >= len(sms_list):
        selected_message_index = len(sms_list) - 1
    elif selected_message_index < 0:
        selected_message_index = 0
    
    # Adjust scroll offset to keep selected message visible
    if selected_message_index < inbox_scroll_offset:
        inbox_scroll_offset = selected_message_index
    elif selected_message_index >= inbox_scroll_offset + len(msg_lines):
        inbox_scroll_offset = selected_message_index - len(msg_lines) + 1
    
    # Display messages in 3-column format: "From | Time | Content"
    for i, line in enumerate(msg_lines):
        msg_idx = inbox_scroll_offset + i
        if msg_idx < len(sms_list):
            sms = sms_list[msg_idx]
            sender = format_sender(sms['sender'])
            timestamp = format_timestamp(sms['time'])
            content = format_content_preview(sms['content'])
            
            # Format: "NAME     TIME     CONTENT..."
            # Use fixed width columns: 10 chars name, 11 chars time, rest content
            sender_col = (sender[:10] + " " * 10)[:10]  # Pad and truncate to 10 chars
            time_col = (timestamp[:11] + " " * 11)[:11]  # Pad and truncate to 11 chars
            
            # Highlight selected message
            if msg_idx == selected_message_index:
                line.text = f">{sender_col} {time_col} {content}"
                line.color = 0x00FF00  # Green for selected
            else:
                line.text = f" {sender_col} {time_col} {content}"
                line.color = 0xFFFF00  # Yellow for normal
        else:
            line.text = ""
    
    status_area.text = f"MSG {selected_message_index + 1}/{len(sms_list)} - UP/DN:nav RIGHT/ENTER:view C:compose N:new D:del"

def refresh_inbox_highlighting():
    global selected_message_index, inbox_scroll_offset
    
    # Use cached data, don't reload from SD
    sms_list = load_sms_from_sd()
    
    if not sms_list:
        return
    
    # Ensure selected index is valid
    if selected_message_index >= len(sms_list):
        selected_message_index = len(sms_list) - 1
    elif selected_message_index < 0:
        selected_message_index = 0
    
    # Adjust scroll offset to keep selected message visible
    if selected_message_index < inbox_scroll_offset:
        inbox_scroll_offset = selected_message_index
    elif selected_message_index >= inbox_scroll_offset + len(msg_lines):
        inbox_scroll_offset = selected_message_index - len(msg_lines) + 1
    
    # Only update highlighting, don't rebuild content
    for i, line in enumerate(msg_lines):
        msg_idx = inbox_scroll_offset + i
        if msg_idx < len(sms_list):
            sms = sms_list[msg_idx]
            sender = format_sender(sms['sender'])
            timestamp = format_timestamp(sms['time'])
            content = format_content_preview(sms['content'])
            
            # Format: "NAME     TIME     CONTENT..."
            # Use fixed width columns: 10 chars name, 11 chars time, rest content
            sender_col = (sender[:10] + " " * 10)[:10]  # Pad and truncate to 10 chars
            time_col = (timestamp[:11] + " " * 11)[:11]  # Pad and truncate to 11 chars
            
            # Highlight selected message
            if msg_idx == selected_message_index:
                line.text = f">{sender_col} {time_col} {content}"
                line.color = 0x00FF00  # Green for selected
            else:
                line.text = f" {sender_col} {time_col} {content}"
                line.color = 0xFFFF00  # Yellow for normal
        else:
            line.text = ""
    
    status_area.text = f"MSG {selected_message_index + 1}/{len(sms_list)} - UP/DN:nav RIGHT/ENTER:view C:compose N:new D:del"

def scroll_inbox_up():
    global selected_message_index
    if selected_message_index > 0:
        selected_message_index -= 1
        refresh_inbox_highlighting()

def scroll_inbox_down():
    global selected_message_index
    sms_list = load_sms_from_sd()  # Uses cache
    if sms_list and selected_message_index < len(sms_list) - 1:
        selected_message_index += 1
        refresh_inbox_highlighting()

def display_message_detail():
    global current_view, selected_message_index
    current_view = "detail"
    hide_all_views()
    
    sms_list = load_sms_from_sd()
    if not sms_list or selected_message_index >= len(sms_list):
        detail_lines[0].text = "Message not found"
        return
    
    sms = sms_list[selected_message_index]
    
    # Display message details
    detail_from.text = f"From: {sms['sender']}"
    detail_time.text = f"Time: {sms['time']}"
    
    # Wrap content across multiple lines (45 chars per line)
    content = sms['content']
    lines = []
    while content:
        if len(content) <= 45:
            lines.append(content)
            break
        else:
            # Find good break point
            break_point = content.rfind(' ', 0, 45)
            if break_point == -1:
                break_point = 45
            lines.append(content[:break_point])
            content = content[break_point:].strip()
    
    # Display content lines
    content_labels = [detail_content1, detail_content2, detail_content3, detail_content4, detail_content5]
    for i, line_label in enumerate(content_labels):
        if i < len(lines):
            line_label.text = lines[i]
        else:
            line_label.text = ""
    
    detail_actions.text = "R:reply B:back DEL:delete"
    status_area.text = f"MESSAGE DETAIL {selected_message_index + 1}/{len(sms_list)}"

def display_recipient_selection():
    global current_view, selected_recipient_index, recipient_mode, manual_number_entry
    current_view = "select_recipient"
    hide_all_views()
    
    if recipient_mode == "contacts":
        # Show address book contacts
        compose_title.text = "Select Contact:"
        
        contact_text = ""
        for i, contact in enumerate(address_book[:5]):  # Show up to 5 contacts
            if i == selected_recipient_index:
                contact_text += f"> {contact[0]} ({contact[1]})\n"
            else:
                contact_text += f"  {contact[0]} ({contact[1]})\n"
        
        compose_content.text = contact_text
        status_area.text = "TAB:next ENTER:select N:new# ESC:cancel"
        
    else:  # manual mode
        compose_title.text = "Enter Number:"
        compose_content.text = f"> {manual_number_entry}"
        status_area.text = "Type number, ENTER:select, ESC:cancel"

def display_compose(reply_to=None):
    global current_view, compose_message
    current_view = "compose" 
    hide_all_views()
    
    if reply_to:
        # Check if reply_to is a contact name or phone number
        if any(char.isalpha() for char in reply_to):
            # It's likely a contact name, use as-is
            compose_title.text = f"To: {reply_to}"
        else:
            # It's a phone number, format it
            compose_title.text = f"To: {format_sender(reply_to)}"
        
        compose_content.text = f"> {compose_message}"
        status_area.text = "Type message, ENTER:send ESC:cancel"
    else:
        # Start with recipient selection for new messages
        display_recipient_selection()

def back_to_inbox():
    global current_view
    current_view = "inbox"
    display_inbox()

def start_reply():
    global compose_message
    sms_list = load_sms_from_sd()
    if sms_list and selected_message_index < len(sms_list):
        sms = sms_list[selected_message_index]
        compose_message = ""
        display_compose(reply_to=sms['sender'])

def start_new_compose():
    global compose_message, selected_recipient_index, recipient_mode, manual_number_entry
    compose_message = ""
    selected_recipient_index = 0
    recipient_mode = "contacts"
    manual_number_entry = ""
    display_compose()

def select_recipient():
    global current_view, compose_message, recipient_mode, manual_number_entry
    
    if recipient_mode == "contacts":
        if selected_recipient_index < len(address_book):
            selected_contact = address_book[selected_recipient_index]
            compose_message = ""
            # Properly transition to compose mode with selected contact
            display_compose(reply_to=selected_contact[0])
    else:  # manual mode
        if manual_number_entry.strip():
            compose_message = ""
            display_compose(reply_to=manual_number_entry.strip())

def cycle_recipient():
    global selected_recipient_index
    if recipient_mode == "contacts":
        selected_recipient_index = (selected_recipient_index + 1) % len(address_book)
        display_recipient_selection()

def switch_to_manual_entry():
    global recipient_mode, manual_number_entry, selected_recipient_index
    recipient_mode = "manual"
    manual_number_entry = ""
    selected_recipient_index = 0
    display_recipient_selection()

def send_reply():
    global compose_message, current_view
    if current_view == "compose":
        # Extract recipient number from compose_title
        title = compose_title.text
        if "To: " in title:
            recipient_name = title.replace("To: ", "")
            
            # Find the number for this contact
            recipient_number = None
            if "Reply to:" in title:
                # For replies, get from original message
                sms_list = load_sms_from_sd()
                if sms_list and selected_message_index < len(sms_list):
                    sms = sms_list[selected_message_index]
                    recipient_number = sms['sender'].replace('+', '')
            else:
                # Check if it's a number (manual entry) or contact name
                if recipient_name.isdigit() or ('+' in recipient_name):
                    # It's a phone number
                    recipient_number = recipient_name.replace('+', '').replace('-', '').replace(' ', '')
                else:
                    # For new messages, find in address book
                    for contact in address_book:
                        if contact[0] == recipient_name:
                            recipient_number = str(contact[1])
                            break
            
            if recipient_number:
                status_area.text = "Sending..."
                send_message(recipient_number, compose_message)
                compose_message = ""
                back_to_inbox()
            else:
                status_area.text = "Error: No number found"

def delete_current_message():
    global selected_message_index
    sms_list = load_sms_from_sd()
    if sms_list and selected_message_index < len(sms_list):
        sms = sms_list[selected_message_index]
        filename = f"/sd/{sms['filename']}"
        try:
            # Try different ways to delete the file
            try:
                import os
                os.remove(filename)
                print(f"Deleted {filename}")
            except:
                # Alternative: overwrite file with empty content
                with open(filename, "w") as f:
                    f.write("")
                print(f"Cleared {filename}")
                
            invalidate_sms_cache()  # Force reload of SMS list
            
            # Adjust selection after delete
            if selected_message_index >= len(sms_list) - 1:
                selected_message_index = max(0, len(sms_list) - 2)
            back_to_inbox()
        except Exception as e:
            print(f"Error deleting: {e}")
            status_area.text = "Delete failed"
    
def send_message(recipient,message):
    #ta.text='(sending message...)'
    print("sending message")
    uart.write(bytes('AT+CFUN=1\r',"ascii"))
    time.sleep(.2)
    data=uart.read(uart.in_waiting)
    print(data)
    #messages.append(data.strip())
    uart.write(bytes('AT+CMGF=1\r',"ascii"))
    time.sleep(.2)
    data=uart.read(uart.in_waiting)
    #messages.append(data.strip())
    print(data)
    uart.write(bytes('AT+CMGS=\"+'+str(recipient)+'\"\r',"ascii"))
    time.sleep(.5)
    data=uart.read(uart.in_waiting)
    #messages.append(data.strip())
    print(data)
    uart.write(bytes(message+'\x1a',"ascii"))
    time.sleep(2)
    data=uart.read(uart.in_waiting)
    #print("send result:",data)
    send_result=data.decode().split('\r\n')
    print("send_result=",send_result)

def make_call(recipient):
    
    uart.write(bytes('AT+CNUM\r',"ascii")) # switch to headphones
    time.sleep(.2)
    data=uart.read(uart.in_waiting).decode()
    print(data)
    
    uart.write(bytes('AT+CSDVC=1\r',"ascii")) # switch to headphones
    time.sleep(.2)
    data=uart.read(uart.in_waiting).decode()
    print(data)
    
    uart.write(bytes('AT+CLVL=?\r',"ascii")) # query volume range
    time.sleep(.2)
    data=uart.read(uart.in_waiting).decode()
    print(data)
    
    uart.write(bytes('AT+CLVL=5\r',"ascii")) # set volume to 2
    time.sleep(.2)
    data=uart.read(uart.in_waiting).decode()
    print(data)
    
    uart.write(bytes('ATD+'+str(recipient)+';\r',"ascii")) # set volume to 2
    time.sleep(2)
    data=uart.read(uart.in_waiting).decode()
    print(data)
    

# display startup msgs
status_area.text="::starting up::"
time.sleep(2)
status_area.text=""

# Load address book and start in inbox view
load_address_book()
current_view = "inbox"
display_inbox()

while True:

    # Read complete lines from UART
    uart_lines = read_uart_lines()
    for line in uart_lines:
        print(f"UART line: {line}")
        # Check for incoming SMS notification
        if '+CMTI:' in line:
            print(f"Found SMS notification: {line}")
            # Process the complete CMTI line directly
            process_cmti_line(line)
    #network_status()
    #time.sleep(5)
    #get_messages()

    #recipient=address_book[0]
    #send_message(recipient[1],"hello")
    #print("calling ",recipient[0])
    #make_call(recipient[1])
    #print("sent")
    #time.sleep(60)
    
    #while not i2c.try_lock():
    #    pass
    #i2c.readfrom_into(cardkb,b)
    #i2c.unlock()
    
    with kb:
        kb.readinto(b)
    
    # Handle navigation based on current view
    if current_view == "inbox":
        if (b == UP):
            scroll_inbox_up()
            continue
        if (b == DOWN):
            scroll_inbox_down()
            continue
        if (b == ENTER_KEY) or (b == CR) or (b == RIGHT):
            display_message_detail()
            continue
        if (b == N_KEY):
            get_messages()
            display_inbox()  # Force full refresh after getting new messages
            continue
        if (b == D_KEY):
            delete_all_messages()
            continue
        if (b == C_KEY):
            start_new_compose()
            continue
            
    elif current_view == "detail":
        if (b == B_KEY) or (b == BACK) or (b == LEFT) or (b == ESC):
            back_to_inbox()
            continue
        if (b == R_KEY):
            start_reply()
            continue
        if (b == DEL_KEY):
            delete_current_message()
            continue
            
    elif current_view == "select_recipient":
        if (b == ESC):
            back_to_inbox()
            continue
        if (b == ENTER_KEY) or (b == CR):
            select_recipient()
            continue
        if (b == TAB):
            cycle_recipient()
            continue
        if (b == N_KEY):
            switch_to_manual_entry()
            continue
        if (b == BACK) and recipient_mode == "manual" and len(manual_number_entry) > 0:
            manual_number_entry = manual_number_entry[:-1]
            display_recipient_selection()
            continue
            
    elif current_view == "compose":
        if (b == ESC):
            back_to_inbox()
            continue
        if (b == ENTER_KEY) or (b == CR):
            send_reply()
            continue
        if (b == BACK) and len(compose_message) > 0:
            compose_message = compose_message[:-1]
            display_compose()
            continue
        
    # Handle text input for manual number entry and compose modes
    if current_view == "select_recipient" and recipient_mode == "manual":
        c = b.decode()
        if (c != ESC and c != NUL and c != '\r' and c != '\x08' and c != '\t' and c != 'n'):
            manual_number_entry += c
            display_recipient_selection()
            continue
            
    elif current_view == "compose":
        c = b.decode()
        if (c != ESC and c != NUL and c != '\r' and c != '\x08'):
            compose_message += c
            display_compose()
            continue


