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

recipient_area = label.Label(terminalio.FONT, text="", color=0xFFFF00, scale=2,x=5, y=10)
outgoing_area = label.Label(terminalio.FONT, text="", color=0xFFFF00, scale=2,x=5, y=30)
incoming_title = label.Label(terminalio.FONT, text="", color=0xFFFF00, scale=1,x=5, y=50)
incoming_content = label.Label(terminalio.FONT, text="", color=0xFFFF00, scale=1,x=5, y=70)
incoming_content2 = label.Label(terminalio.FONT, text="", color=0xFFFF00, scale=1,x=5, y=90)
incoming_content3 = label.Label(terminalio.FONT, text="", color=0xFFFF00, scale=1,x=5, y=110)
status_area = label.Label(terminalio.FONT, text="", color=0xFFFF00, scale=2,x=5, y=130)

splash.append(recipient_area)
splash.append(outgoing_area)
splash.append(incoming_title)
splash.append(incoming_content)
splash.append(incoming_content2)
splash.append(incoming_content3)
splash.append(status_area)

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
# Add new keys for message navigation
# Using 'n' and 'p' for next/previous message
N_KEY = b'n'
P_KEY = b'p'
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

# Global variable to track which message we're viewing
current_message_index = 0

def display_stored_messages():
    global current_message_index
    sms_list = load_sms_from_sd()
    if not sms_list:
        incoming_title.text = "No stored messages"
        incoming_content.text = ""
        incoming_content2.text = ""
        incoming_content3.text = ""
        return
    
    # Ensure index is within bounds
    if current_message_index >= len(sms_list):
        current_message_index = len(sms_list) - 1
    elif current_message_index < 0:
        current_message_index = 0
    
    # Show current message
    current_sms = sms_list[current_message_index]
    incoming_title.text = f"{current_message_index + 1}/{len(sms_list)} {current_sms['sender']}"
    
    # Wrap long content across multiple lines
    content = current_sms['content']
    if len(content) <= 40:
        incoming_content.text = content
        incoming_content2.text = ""
        incoming_content3.text = ""
    elif len(content) <= 80:
        incoming_content.text = content[:40]
        incoming_content2.text = content[40:]
        incoming_content3.text = ""
    else:
        incoming_content.text = content[:40]
        incoming_content2.text = content[40:80]
        incoming_content3.text = content[80:120] + ("..." if len(content) > 120 else "")
    
    print(f"Displaying message {current_message_index + 1} of {len(sms_list)}")

def next_message():
    global current_message_index
    sms_list = load_sms_from_sd()
    if sms_list and current_message_index < len(sms_list) - 1:
        current_message_index += 1
        display_stored_messages()

def prev_message():
    global current_message_index
    if current_message_index > 0:
        current_message_index -= 1
        display_stored_messages()
    
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

# Load and display any stored messages on startup
display_stored_messages()

recipient_area.text="TO: "+str(recipient[0])
outgoing_area.text=">"

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
    
    if (b == LEFT):
        print("left!")
        #sendee_index=(sendee_index+1)%len(sendee_list)
        #sendee=sendee_list[sendee_index]
        #label0.text="TO: "+str(sendee)
        delete_all_messages()
        continue
    if (b == RIGHT):
        get_messages()
        display_stored_messages()
        continue
    if (b == ESC):
        audio_alert=False
        print("audio alert off")
        continue
    if (b == UP):
        display_stored_messages()
        continue
    if (b == DOWN):
        hangup()
        continue
    if (b == N_KEY):
        next_message()
        continue
    if (b == P_KEY):
        prev_message()
        continue
    if (b == TAB):
        print("tab!")
        recipient_index=(recipient_index+1)%len(address_book)
        recipient=address_book[recipient_index]
        recipient_area.text="TO: "+str(recipient[0])
        continue
        
    if (b == BACK) and len(instr)>0:
        print("DELETE")
        instr=instr[:-1]
        print("instr=",instr)
        outgoing_area.text='> '+instr
        #del instr[-1]
    
    c=b.decode()
    
    if (c != ESC and c != NUL and c !=LEFT and c!=BACK):
        if (c == CR):
            print('\nsending:',instr)
            status_area.text='::sending message::'
            time.sleep(1)
            send_message(recipient[1],instr.strip())
            status_area.text='::message sent::'
            instr=''
            outgoing_area.text='>'
            time.sleep(1)
            status_area.text=''
            #send_message(sendee[1],instr.strip())
            #messages.append("me: "+instr.strip())
            #get_messages()
            
        else:
            #print("back in!")
            print(c, end='')
            instr=instr+c
            outgoing_area.text='> '+instr


