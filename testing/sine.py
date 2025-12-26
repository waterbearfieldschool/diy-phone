#import analogbufio
import array
import audiocore
import audiopwmio
import board
import math
import time

# Generate one period of sine wave.
length = 8000 // 440
sine_wave = array.array("h", [0] * length)
for i in range(length):
    sine_wave[i] = int(math.sin(math.pi * 2 * i / length) * (2 ** 15))
pwm = audiopwmio.PWMAudioOut(left_channel=board.D12, right_channel=board.D13)

# Play single-buffered
sample = audiocore.RawSample(sine_wave)
pwm.play(sample, loop=True)
time.sleep(3)
# changing the wave has no effect
for i in range(length):
     sine_wave[i] = int(math.sin(math.pi * 4 * i / length) * (2 ** 15))
time.sleep(3)
pwm.stop()
time.sleep(1)

# Play double-buffered
sample = audiocore.RawSample(sine_wave, single_buffer=False)
pwm.play(sample, loop=True)
time.sleep(3)
# changing the wave takes effect almost immediately
for i in range(length):
    sine_wave[i] = int(math.sin(math.pi * 2 * i / length) * (2 ** 15))
time.sleep(3)
pwm.stop()
pwm.deinit()
