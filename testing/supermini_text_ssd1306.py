import time
import board
import busio
import digitalio
import supervisor
import terminalio
import displayio
import adafruit_displayio_ssd1306
from adafruit_display_text import label

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

stats_area = label.Label(terminalio.FONT, text="", color=0xFFFF00, x=5, y=5)
time_area = label.Label(terminalio.FONT, text="", color=0xFFFF00, x=5, y=15)
temp_batt_area = label.Label(terminalio.FONT, text="", color=0xFFFF00, x=5, y=25)
probe_area = label.Label(terminalio.FONT, text="", color=0xFFFF00, x=5, y=35)
satellite_area = label.Label(terminalio.FONT, text="", color=0xFFFF00, x=5, y=45)

splash.append(stats_area)
splash.append(time_area)
splash.append(temp_batt_area)
splash.append(probe_area)
splash.append(satellite_area)
        
stats_area.text="startup ..."


from adafruit_ticks import ticks_ms, ticks_add, ticks_diff, ticks_less

uart = busio.UART(board.P0_22, board.P0_20, baudrate=115200,timeout=10)

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
    

while True:

    network_status()
    time.sleep(5)
    #get_messages()

    
    #recipient=address_book[0]
    #send_message(recipient[1],"hello")
    #print("sent")
    #time.sleep(5)
    
    
