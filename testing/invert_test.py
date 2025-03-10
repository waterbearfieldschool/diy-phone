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
#bus=busio.SPI(board.D11, MOSI=board.D12, MISO=board.MISO)
chip_select_pin = board.D10
# Select JUST ONE of the following lines:
# For the 400x240 display (can only be operated at 2MHz)
framebuffer = sharpdisplay.SharpMemoryFramebuffer(bus, chip_select_pin, 400, 240)
# For the 144x168 display (can be operated at up to 8MHz)
#framebuffer = sharpdisplay.SharpMemoryFramebuffer(bus, chip_select_pin, width=144, height=168, baudrate=8000000)

display = framebufferio.FramebufferDisplay(framebuffer)

black_color=0x000000
white_color=0xFFFFFF

from adafruit_display_text.label import Label
from terminalio import FONT
label = Label(font=FONT, text="booting...", x=10, y=10, scale=1, color=black_color, background_color=white_color,line_spacing=1.2)
display.root_group = label

while True:
    time.sleep(1)



