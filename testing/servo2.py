import time
import board
import pwmio
from adafruit_motor import servo

# create a PWMOut object on Pin A2.
pwm = pwmio.PWMOut(board.D12, duty_cycle=2 ** 15, frequency=50)

# Create a servo object, my_servo.
my_servo = servo.Servo(pwm)

while True:

    my_servo.angle = 0
    time.sleep(.5)
    my_servo.angle = 120
    time.sleep(.5)
    my_servo.angle=0
    time.sleep(2)
    
