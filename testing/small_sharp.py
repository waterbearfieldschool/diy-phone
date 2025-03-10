import board
import displayio
import framebufferio
import sharpdisplay
import busio
import digitalio   
import time
   
# Release the existing display, if any
displayio.release_displays()

bus = board.SPI()

chip_select_pin = board.D6
# Select JUST ONE of the following lines:
# For the 400x240 display (can only be operated at 2MHz)
framebuffer = sharpdisplay.SharpMemoryFramebuffer(bus, chip_select_pin, 400, 240)
# For the 144x168 display (can be operated at up to 8MHz)
#framebuffer = sharpdisplay.SharpMemoryFramebuffer(bus, chip_select_pin, width=144, height=168, baudrate=8000000)

display = framebufferio.FramebufferDisplay(framebuffer)



from adafruit_display_text.label import Label
from terminalio import FONT
label = Label(font=FONT, text="booting...", x=10, y=10, scale=2, line_spacing=1.2)
display.root_group = label


uart = busio.UART(board.TX, board.RX, baudrate=115200,timeout=0)

while True:

    # signal strength
    uart.write(bytes('AT+CSQ\r',"ascii"))
    #uart.write(bytes('AT\r',"ascii"))
    time.sleep(.2)
    data=uart.read(uart.in_waiting).decode()
    print(data)
    label.text=str(data)
    time.sleep(2)
    
    # query local number
    uart.write(bytes('AT+CNUM\r',"ascii"))
    time.sleep(.2)
    data=uart.read(uart.in_waiting).decode()
    print(data)
    label.text=str(data)
    time.sleep(2)
    
    # switch to speaker output
    uart.write(bytes('AT+CSDVC=1\r',"ascii"))
    time.sleep(.2)
    data=uart.read(uart.in_waiting).decode()
    print(data)
    label.text=str(data)
    time.sleep(2)
    
    # set hte volume to 2
    uart.write(bytes('AT+CLVL=2\r',"ascii"))
    time.sleep(.2)
    data=uart.read(uart.in_waiting).decode()
    print(data)
    label.text=str(data)
    time.sleep(2)
    
    # dial a specified number
    uart.write(bytes('ATD16512524765;\r',"ascii"))
    time.sleep(.2)
    data=uart.read(uart.in_waiting).decode()
    print(data)
    label.text=str(data)
    time.sleep(2)
    
