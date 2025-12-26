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

def load_sms_from_sd():
    try:
        import os
        sms_list = []
        for filename in os.listdir("/sd"):
            if filename.startswith("sms_") and filename.endswith(".txt"):
                with open(f"/sd/{filename}", "r") as f:
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
        # Sort by filename (which includes timestamp)
        sms_list.sort(key=lambda x: x['filename'])
        return sms_list
    except Exception as e:
        print(f"Error loading SMS: {e}")
        return []

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
    status_area.text=''

# Global variables for inbox navigation
current_view = "inbox"  # "inbox", "detail", "compose"
selected_message_index = 0
inbox_scroll_offset = 0
compose_message = ""

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
    # Shorten long phone numbers for display
    if len(sender) > 12:
        return sender[-10:]  # Show last 10 digits
    return sender

def format_content_preview(content, max_length=25):
    # Truncate content for inbox preview
    if len(content) > max_length:
        return content[:max_length] + "..."
    return content

def display_inbox():
    global selected_message_index, inbox_scroll_offset
    hide_all_views()
    
    sms_list = load_sms_from_sd()
    if not sms_list:
        msg_lines[0].text = "No messages"
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
    
    # Display messages in inbox format: "From: Content"
    for i, line in enumerate(msg_lines):
        msg_idx = inbox_scroll_offset + i
        if msg_idx < len(sms_list):
            sms = sms_list[msg_idx]
            sender = format_sender(sms['sender'])
            content = format_content_preview(sms['content'])
            
            # Highlight selected message
            if msg_idx == selected_message_index:
                line.text = f"> {sender}: {content}"
                line.color = 0x00FF00  # Green for selected
            else:
                line.text = f"  {sender}: {content}"
                line.color = 0xFFFF00  # Yellow for normal
        else:
            line.text = ""
    
    status_area.text = f"MSG {selected_message_index + 1}/{len(sms_list)} - UP/DN:nav ENTER:view"

def scroll_inbox_up():
    global selected_message_index
    if selected_message_index > 0:
        selected_message_index -= 1
        display_inbox()

def scroll_inbox_down():
    global selected_message_index
    sms_list = load_sms_from_sd()
    if sms_list and selected_message_index < len(sms_list) - 1:
        selected_message_index += 1
        display_inbox()

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

def display_compose(reply_to=None):
    global current_view, compose_message
    current_view = "compose" 
    hide_all_views()
    
    if reply_to:
        compose_title.text = f"Reply to: {format_sender(reply_to)}"
    else:
        compose_title.text = "New message"
    
    compose_content.text = f"> {compose_message}"
    status_area.text = "Type message, ENTER:send ESC:cancel"

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

def send_reply():
    global compose_message, current_view
    sms_list = load_sms_from_sd()
    if sms_list and selected_message_index < len(sms_list):
        sms = sms_list[selected_message_index]
        recipient = sms['sender'].replace('+', '')  # Remove + for sending
        status_area.text = "Sending..."
        send_message(recipient, compose_message)
        compose_message = ""
        back_to_inbox()

def delete_current_message():
    global selected_message_index
    sms_list = load_sms_from_sd()
    if sms_list and selected_message_index < len(sms_list):
        sms = sms_list[selected_message_index]
        filename = f"/sd/{sms['filename']}"
        try:
            import os
            os.remove(filename)
            print(f"Deleted {filename}")
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

# Start in inbox view
current_view = "inbox"
display_inbox()

while True:

    data=uart.read(uart.in_waiting)
    if len(data)>0:
        print(data)
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
        if (b == ENTER_KEY) or (b == CR):
            display_message_detail()
            continue
        if (b == RIGHT):
            get_messages()
            display_inbox()
            continue
        if (b == LEFT):
            delete_all_messages()
            continue
            
    elif current_view == "detail":
        if (b == B_KEY) or (b == BACK):
            back_to_inbox()
            continue
        if (b == R_KEY):
            start_reply()
            continue
        if (b == DEL_KEY):
            delete_current_message()
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
        
    # Handle text input for compose mode
    if current_view == "compose":
        c = b.decode()
        if (c != ESC and c != NUL and c != '\r' and c != '\x08'):
            compose_message += c
            display_compose()
            continue


