import time
import board
import busio
import digitalio
import supervisor
supervisor.runtime.autoreload = False

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
    

    
do_and_print('AT+CSDVC=3') # speaker output
do_and_print('AT+CLVL=5') # output volume
do_and_print('AT+CMICGAIN=8') # gain

recipient=16512524765
#do_and_print('ATD+'+str(recipient)+';') # call recipient
command='ATD+'+str(recipient)+';'
#send_command(command, timeout=3000)

#uart.write(bytes(command+'\r\n',"ascii"))
#while(True):        
#    data=uart.read(uart.in_waiting)
#    print("reply: ",data)
#    time.sleep(.1)

while True:
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
            time.sleep(1)
            print("Answering...")
            do_and_print('ATA') # answering
            
        #except:
        #    print("couldn't decode uart")
