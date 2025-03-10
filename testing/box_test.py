import board
import displayio
import framebufferio
import sharpdisplay
import busio
import digitalio   
import time
import terminalio

from adafruit_display_text import label
   
WIDTH = 128
HEIGHT = 32  # Change to 64 if needed
BORDER = 5

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
#label = Label(font=FONT, text="booting...", x=10, y=10, scale=1, color=black_color, background_color=white_color,line_spacing=1.2)
#display.root_group = label

# Make the display context
splash = displayio.Group()
display.root_group = splash

color_bitmap = displayio.Bitmap(WIDTH, HEIGHT, 1)
color_palette = displayio.Palette(1)
color_palette[0] = 0xFFFFFF  # White

bg_sprite = displayio.TileGrid(color_bitmap, pixel_shader=color_palette, x=0, y=0)
splash.append(bg_sprite)

# Draw a smaller inner rectangle
inner_bitmap = displayio.Bitmap(WIDTH - BORDER * 2, HEIGHT - BORDER * 2, 1)
inner_palette = displayio.Palette(1)
inner_palette[0] = 0x000000  # Black
inner_sprite = displayio.TileGrid(inner_bitmap, pixel_shader=inner_palette, x=BORDER, y=BORDER)
splash.append(inner_sprite)

# Draw a label
text = "Hello World!"
text_area = label.Label(terminalio.FONT, text=text, color=0xFFFFFF, x=28, y=HEIGHT // 2 - 1)
splash.append(text_area)

while True:
    pass
    



