import time
import board
import busio
import digitalio
import supervisor
import terminalio
import displayio
import adafruit_displayio_ssd1306
from adafruit_display_text import label
#from adafruit_display_text.label import Label
from adafruit_bus_device.i2c_device import I2CDevice
import supervisor
supervisor.runtime.autoreload = False

address_book = [["Don",16512524765],["Emilie",16463278220],["David",15304929688],["Liz",16174299144],["me",17819189114]]

try:
    from i2cdisplaybus import I2CDisplayBus
except ImportError:
    from displayio import I2CDisplay as I2CDisplayBus

displayio.release_displays()
i2c = busio.I2C(scl=board.P0_11, sda=board.P1_04)
display_bus = I2CDisplayBus(i2c, device_address=0x3C)
display = adafruit_displayio_ssd1306.SSD1306(display_bus, width=128, height=64)

splash = displayio.Group()
display.root_group = splash

stats_area = label.Label(terminalio.FONT, text="startup...", color=0xFFFF00, x=5, y=5)
time_area = label.Label(terminalio.FONT, text="goof1", color=0xFFFF00, x=5, y=15)
temp_batt_area = label.Label(terminalio.FONT, text="goof2", color=0xFFFF00, x=5, y=25)
probe_area = label.Label(terminalio.FONT, text="goof3", color=0xFFFF00, x=5, y=35)
satellite_area = label.Label(terminalio.FONT, text="goof4", color=0xFFFF00, x=5, y=45)

splash.append(stats_area)
splash.append(time_area)
splash.append(temp_batt_area)
splash.append(probe_area)
splash.append(satellite_area)

time.sleep(5)

#while not i2c.try_lock():
#    pass
 
#i2c_devices = i2c.scan()
#print(i2c_devices)
#cardkb=i2c_devices[1]
#if cardkb != 95:
#    print("!!! Check I2C config: " + str(i2c))
#    print("!!! CardKB not found. I2C device", cardkb,
#          "found instead.")

cardkb=95

kb = I2CDevice(i2c, 0x5F)

from adafruit_ticks import ticks_ms, ticks_add, ticks_diff, ticks_less

uart = busio.UART(board.P0_22, board.P0_20, baudrate=115200,timeout=10)

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
c = ''
b = bytearray(1)

instr = ''
radio_instr = ''

def network_status():
    uart.write(bytes('AT+CSQ\r',"ascii"))
    time.sleep(.2)
    data=uart.read(uart.in_waiting).decode()
    print(data)

def get_messages():
    ta.text='(checking messages...)'
    print("checking messages")
    uart.write(bytes('AT+CMGF=1\r',"ascii"))
    time.sleep(.2)
    data=uart.read(uart.in_waiting)
    print(data)
    uart.write(bytes('AT+CMGL=\"ALL\"\r',"ascii"))
    time.sleep(.2)
    data=uart.read(uart.in_waiting).decode()
    print(data)
    
def send_message(recipient,message):
    ta.text='(sending message...)'
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
    


while True:

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
        #get_network_status()
        #get_network_time()
        delete_all_messages()
        continue
    if (b == RIGHT):
        print("right!")
        get_network_status()
        get_network_time()
        get_messages()
        #make_call(sendee[1])
        continue
    if (b == ESC):
        audio_alert=False
        print("audio alert off")
        continue
    if (b == UP):
        #answer_call()
        make_call(sendee[1])
        continue
    if (b == DOWN):
        hangup()
        continue
    if (b == TAB):
        print("tab!")
        sendee_index=(sendee_index+1)%len(sendee_list)
        sendee=sendee_list[sendee_index]
        label0.text="TO: "+str(sendee)
        continue
        
    if (b == BACK) and len(instr)>0:
        print("DELETE")
        instr=instr[:-1]
        print("instr=",instr)
        label1.text='> '+instr
        #del instr[-1]
    
    c=b.decode()
    
    if (c != ESC and c != NUL and c !=LEFT and c!=BACK):
        if (c == CR):
            print('\nsending:',instr)
            stats_area.text='> (sending message ...)'
            time.sleep(3)
            stats_area.text='> '
            #send_message(sendee[1],instr.strip())
            #messages.append("me: "+instr.strip())
            #get_messages()
            instr=''
            time_area.text='> '
        else:
            #print("back in!")
            print(c, end='')
            instr=instr+c
            time_area.text='> '+instr
