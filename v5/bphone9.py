# SPDX-FileCopyrightText: 2021 ladyada for Adafruit Industries
# SPDX-License-Identifier: MIT

"""
This test will initialize the display using displayio and draw a solid green
background, a smaller purple rectangle, and some yellow text.
"""

import board
import displayio
import terminalio
import busio
from adafruit_display_text import label
from fourwire import FourWire

from adafruit_st7789 import ST7789

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

color_bitmap = displayio.Bitmap(320, 240, 1)
color_palette = displayio.Palette(1)
color_palette[0] = 0x00FF00  # Bright Green

bg_sprite = displayio.TileGrid(color_bitmap, pixel_shader=color_palette, x=0, y=0)
splash.append(bg_sprite)

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

recipient_area = label.Label(terminalio.FONT, text="", color=0xFFFF00, x=5, y=5)
outgoing_area = label.Label(terminalio.FONT, text="", color=0xFFFF00, x=5, y=15)
incoming_title = label.Label(terminalio.FONT, text="", color=0xFFFF00, x=5, y=25)
incoming_content = label.Label(terminalio.FONT, text="", color=0xFFFF00, x=5, y=35)
status_area = label.Label(terminalio.FONT, text="", color=0xFFFF00, x=5, y=50)

splash.append(recipient_area)
splash.append(outgoing_area)
splash.append(incoming_title)
splash.append(incoming_content)
splash.append(status_area)




while True:
    pass

