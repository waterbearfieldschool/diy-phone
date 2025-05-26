import time
import board
import busio
import digitalio
import supervisor
import terminalio
import displayio
import adafruit_displayio_ssd1306
from adafruit_display_text import label
from adafruit_debouncer import Debouncer
import pwmio

buzzer = pwmio.PWMOut(board.D7, variable_frequency=True)
buzzer.frequency = 440

OFF = 0
ON = 2**15

supervisor.runtime.autoreload = False

hangup_pin = digitalio.DigitalInOut(board.D2)
hangup_pin.direction = digitalio.Direction.INPUT
hangup_pin.pull = digitalio.Pull.UP
hangup_switch = Debouncer(hangup_pin)


answer_pin = digitalio.DigitalInOut(board.A5)
answer_pin.direction = digitalio.Direction.INPUT
answer_pin.pull = digitalio.Pull.UP
answer_switch = Debouncer(answer_pin)

call_don_pin = digitalio.DigitalInOut(board.A3)
call_don_pin.direction = digitalio.Direction.INPUT
call_don_pin.pull = digitalio.Pull.UP
call_don_switch = Debouncer(call_don_pin)

call_liz_pin = digitalio.DigitalInOut(board.A2)
call_liz_pin.direction = digitalio.Direction.INPUT
call_liz_pin.pull = digitalio.Pull.UP
call_liz_switch = Debouncer(call_liz_pin)

call_emilie_pin = digitalio.DigitalInOut(board.A1)
call_emilie_pin.direction = digitalio.Direction.INPUT
call_emilie_pin.pull = digitalio.Pull.UP
call_emilie_switch = Debouncer(call_emilie_pin)

call_mom_pin = digitalio.DigitalInOut(board.A0)
call_mom_pin.direction = digitalio.Direction.INPUT
call_mom_pin.pull = digitalio.Pull.UP
call_mom_switch = Debouncer(call_mom_pin)


try:
    from i2cdisplaybus import I2CDisplayBus
except ImportError:
    from displayio import I2CDisplay as I2CDisplayBus

displayio.release_displays()
i2c = board.I2C()

i2c = board.I2C()  # uses board.SCL and board.SDA
# i2c = board.STEMMA_I2C()  # For using the built-in STEMMA QT connector on a microcontroller
display_bus = I2CDisplayBus(i2c, device_address=0x3C)
display = adafruit_displayio_ssd1306.SSD1306(display_bus, width=128, height=64)

# Make the display context
splash = displayio.Group()
display.root_group = splash


# Draw a label

text="startup..."

ta = label.Label(terminalio.FONT, text=text, color=0xFFFF00, x=5, y=5)
splash.append(ta)

from adafruit_ticks import ticks_ms, ticks_add, ticks_diff, ticks_less

uart = busio.UART(board.TX, board.RX, baudrate=115200,timeout=10)

def send_command(command, timeout=3000):
    command += '\r\n'
    uart.write(command)
    start_time = ticks_ms()
    response = []
    while ticks_diff(ticks_ms(), start_time) < timeout:
        if uart.in_waiting:
            print(uart.read(uart.in_waiting))
            #response.append(uart.read().decode())
    #return ''.join(response)
        
def do_and_print(command):
    print("sending: "+command)
    uart.write(bytes(command+'\r\n',"ascii"))
    time.sleep(.1)
    data=uart.read(uart.in_waiting)
    print("reply: ",data)
    time.sleep(.1)


    #uart.write(bytes('AT+CSQ\r\n',"ascii"))
    #uart.write(bytes('AT\r\n',"ascii"))
    #time.sleep(1)
    #data=uart.read(uart.in_waiting).decode()
    #data=uart.read(uart.in_waiting)
    

#do_and_print('AT+CMICGAIN=?') 
#do_and_print('AT+CSDVC=2') # speaker output
#do_and_print('AT+CLVL=5') # output volume
#do_and_print('AT+CMICGAIN=5') # gain



#recipient=16512524765
#do_and_print('ATD+'+str(recipient)+';') # call recipient
#command='ATD+'+str(recipient)+';'
#send_command(command, timeout=3000)

#uart.write(bytes(command+'\r\n',"ascii"))
#while(True):        
#    data=uart.read(uart.in_waiting)
#    print("reply: ",data)
#    time.sleep(.1)

do_and_print('AT+CSDVC=3') # use voice 
do_and_print('AT+CLVL=3') # output volume
do_and_print('AT+CMICGAIN=8') # gain    

ta.text="Ready."

while True:

    answer_switch.update()
    hangup_switch.update()
    call_mom_switch.update()
    call_emilie_switch.update()
    call_liz_switch.update()
    call_don_switch.update()
    
    if call_don_switch.fell:
        recipient=['Don',17813230341]
        print("Calling "+recipient[0])
        ta.text="Calling "+recipient[0]
        do_and_print('AT+CSDVC=3') # use voice 
        do_and_print('AT+CLVL=2') # output volume
        do_and_print('AT+CMICGAIN=8') # gain 
        do_and_print('ATD+'+str(recipient[1])+';')
        
    if call_mom_switch.fell:
        recipient=['Mom',17815979083]
        print("Calling "+recipient[0])
        ta.text="Calling "+recipient[0]
        do_and_print('AT+CSDVC=3') # use voice 
        do_and_print('AT+CLVL=2') # output volume
        do_and_print('AT+CMICGAIN=8') # gain 
        do_and_print('ATD+'+str(recipient[1])+';')
        
    if call_emilie_switch.fell:
        recipient=['Emilie',16463278220]
        print("Calling "+recipient[0])
        ta.text="Calling "+recipient[0]
        do_and_print('AT+CSDVC=3') # use voice 
        do_and_print('AT+CLVL=2') # output volume
        do_and_print('AT+CMICGAIN=8') # gain 
        do_and_print('ATD+'+str(recipient[1])+';')
        
    if call_liz_switch.fell:
        recipient=['Liz',16174299144]
        print("Calling "+recipient[0])
        ta.text="Calling "+recipient[0]
        do_and_print('AT+CSDVC=3') # use voice 
        do_and_print('AT+CLVL=2') # output volume
        do_and_print('AT+CMICGAIN=8') # gain 
        do_and_print('ATD+'+str(recipient[1])+';')
    
        
    if answer_switch.fell:
        print("Answering...")
        ta.text="ANSWERING..."
        do_and_print('AT+CSDVC=3') # use voice 
        do_and_print('AT+CLVL=2') # output volume
        do_and_print('AT+CMICGAIN=8') # gain 
        do_and_print('ATA') # answering
        buzzer.duty_cycle=OFF
        #echo suppresssion
        #do_and_print('AT+CECH=1000') # gain

    if hangup_switch.fell:
        print('hanging up...')
        ta.text="HANGING UP"
        do_and_print('AT+CHUP')
        time.sleep(2)
        ta.text="Ready."

    
    data=uart.read(uart.in_waiting)
    if len(data)>0:
        print(data)
        #try:
        data=data.decode()
        cleaned = data.replace("\r","")
        cleaned = cleaned.replace("\n","")
        print(cleaned)
        if(data.strip()=='RING'):
            #label7.text="INCOMING CALL"
            print("INCOMING CALL")
            ta.text="INCOMING CALL"
            buzzer.duty_cycle=ON
            #time.sleep(2)
            
            
        #except:
        #    print("couldn't decode uart")
