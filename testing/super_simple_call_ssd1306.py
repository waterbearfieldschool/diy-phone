import time
import board
import busio
import digitalio
import supervisor
import terminalio
import displayio
import adafruit_displayio_ssd1306
from adafruit_display_text import label


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

while True:

    uart.write(bytes('AT+CSQ\r',"ascii"))
    time.sleep(.2)
    data=uart.read(uart.in_waiting).decode()
    print(data)
    
    time.sleep(2)
    
