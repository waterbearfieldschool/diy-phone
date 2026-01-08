#!/bin/bash

pio run
cp .pio/build/adafruit_itsybitsy_nrf52840/firmware.hex bin/uf2conv/
cd bin/uf2conv
python3 uf2conv.py firmware.hex -f NRF52840 -o new_firmware.uf2

