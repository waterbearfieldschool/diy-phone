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

text="startup..."

ta = label.Label(terminalio.FONT, text=text, color=0xFFFF00, x=5, y=5)
splash.append(ta)

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
 
def check_sim():
    uart.write(bytes('AT+CPIN?\r',"ascii"))
    time.sleep(.5)
    data=uart.read(uart.in_waiting)
    print("SIM insert:", data)
    #return b'0,1' in data or b'0,5' in data
       
def check_network_registration():
    uart.write(bytes('AT+CREG?\r',"ascii"))
    time.sleep(.5)
    data=uart.read(uart.in_waiting)
    print("Network registration:", data)
    return b'0,1' in data or b'0,5' in data

def send_message(recipient,message):
    ta.text='(sending message...)'
    print("sending message")
    
    # Check network registration first
    if not check_network_registration():
        print("Not registered on network, waiting...")
        ta.text='(waiting for network...)'
        time.sleep(5)
        if not check_network_registration():
            print("Network registration failed")
            ta.text='(network error)'
            return False
    
    uart.write(bytes('AT+CFUN=1\r',"ascii"))
    time.sleep(.5)
    data=uart.read(uart.in_waiting)
    print(data)
    
    uart.write(bytes('AT+CMGF=1\r',"ascii"))
    time.sleep(.5)
    data=uart.read(uart.in_waiting)
    print(data)
    if b'ERROR' in data:
        print("SMS mode setting failed")
        ta.text='(SMS error)'
        return False
    
    uart.write(bytes('AT+CMGS=\"+'+str(recipient)+'\"\r',"ascii"))
    time.sleep(.5)
    data=uart.read(uart.in_waiting)
    print(data)
    if b'ERROR' in data:
        print("SMS send command failed")
        ta.text='(send error)'
        return False
    
    uart.write(bytes(message+'\x1a',"ascii"))
    time.sleep(3)
    data=uart.read(uart.in_waiting)
    send_result=data.decode().split('\r\n')
    print("send_result=",send_result)
    
    if 'OK' in str(data):
        ta.text='(message sent!)'
        return True
    else:
        ta.text='(send failed)'
        return False
    

while True:

    check_sim()
    
    network_status()
    #get_messages()

    
    recipient=address_book[0]
    send_message(recipient[1],"hello")
    time.sleep(2)
    
    
