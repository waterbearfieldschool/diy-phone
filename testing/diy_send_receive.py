import time
import board
import busio
import displayio
import framebufferio
import sharpdisplay
import busio
import digitalio
import supervisor

from __future__ import annotations

sendee_list = [["Don",16512524765],["Emilie",16463278220],["David",15304929688],["Liz",16174299144]]

sendee_index=0

sendee=sendee_list[sendee_index]

check_message_interval = 10 # in seconds

messages = []

displayio.release_displays()

spi = busio.SPI(board.D11, MOSI=board.D12)
chip_select_pin = board.D7

framebuffer = sharpdisplay.SharpMemoryFramebuffer(spi, chip_select_pin, width=400, height=240)

display = framebufferio.FramebufferDisplay(framebuffer)

from adafruit_display_text.label import Label
from terminalio import FONT

label0 = Label(font=FONT, text="TO: "+str(sendee), x=0, y=5, scale=1, line_spacing=1.2)
label1 = Label(font=FONT, text="> ", x=0, y=20, scale=1, line_spacing=1.2)
label3 = Label(font=FONT, text="-------messages-------", x=0, y=35, scale=1, line_spacing=1.2)
label2 = Label(font=FONT, text="(no messages)", x=0, y=50, scale=1, line_spacing=1.2)


#display.root_group = label

text_group = displayio.Group()
text_group.append(label0)
text_group.append(label1)
text_group.append(label2)
text_group.append(label3)

display.root_group=text_group
#display.show(text_group)

i2c = busio.I2C(board.SCL, board.SDA)

while not i2c.try_lock():
    pass
 
#cardkb = i2c.scan()[0]  # should return 95
i2c_devices = i2c.scan()
print(i2c_devices)
cardkb=i2c_devices[0]
if cardkb != 95:
    print("!!! Check I2C config: " + str(i2c))
    print("!!! CardKB not found. I2C device", cardkb,
          "found instead.")
    exit(1)
 
ESC = chr(27)
NUL = '\x00'
CR = "\r"
LF = "\n"
LEFT = bytearray(b'\xB4')
RIGHT = bytearray(b'\xB7')
DOWN = bytearray(b'\xB6')
UP = bytearray(b'\xB5')
c = ''
b = bytearray(1)

instr = ''
radio_instr = ''
uart = busio.UART(board.TX, board.RX, baudrate=115200,timeout=0,receiver_buffer_size=5024)

msg_display_index=0

message_lines = 12



def show_messages(highlight):
    # highlight is the last message
    end_message=highlight
    start_message = (highlight-message_lines)%len(messages)
    print("highlight:",highlight)
    print("bounds:",start_message,end_message)
    outstr=''
    i = end_message
    count=0
    while (count < message_lines) and (count < len(messages)):
        outstr="<"+str(i)+"> "+messages[i]+'\n'+outstr
        i=(i-1)%len(messages)
        count=count+1
    label2.text=outstr

def delete_all_message():
    uart.write(bytes('AT+CMGD=,2\r',"ascii"))
    time.sleep(.1)
    data=uart.read(uart.in_waiting)
    
def delete_messages(limit,prune,res):

    # note -- if you want to delete all read messages, "AT+CMGD=,2"
    num_msg = len(res)
    if num_msg > limit:
    
        messages.clear()
        for i in range(0,prune):
            r = res[i]
            header=r[0].split(',')
            msg_id = header[0].split(":")[1].strip()
            print("msg_id=",msg_id)
            uart.write(bytes('AT+CMGD='+msg_id+'\r',"ascii"))
            time.sleep(.1)
            data=uart.read(uart.in_waiting)
            print(data)
            
    #if len(res)>10:
        
    #cmgls = [row[0] for row in res]
    #print(cmgls)


def get_messages():
    label1.text='> (checking messages...)'
    time.sleep(1)
    uart.write(bytes('AT+CMGF=1\r',"ascii"))
    time.sleep(1)
    data=uart.read(uart.in_waiting)
    uart.write(bytes('AT+CMGL=\"ALL\"\r',"ascii"))
    #uart.write(bytes('AT+CMGL=\"STO SENT\"\r',"ascii"))
    #uart.write(bytes('AT+CMGL=\"REC UNREAD\"\r',"ascii"))
    time.sleep(1)
    data=uart.read(uart.in_waiting).decode()
    #print(data)
    ls=data.split('\r\n')
    ls.pop(0) # remove the modem reply at beginning
    ls = ls[:-3] # remove the modem reply at end
    if len(ls)>0:
        #print(ls)
        res = [ls[i:i+2] for i in range(0,len(ls),2)]
        print(res)
        
        # prune if too many messages
        delete_messages(25,10,res)
        
        messages.clear()
        for r in res:
            header=r[0].split(',')
            sim_num=header[0].split(':')[1]
            sender=header[2][2:]
            sender=sender[:-1].strip()
            
            
            # check to see if number is in address book
            numbers = [row[1] for row in sendee_list]
            i=0
            result_index=-1
            for number in numbers:
                print(sender,number)
                if (int(sender)==int(number)):
                    result_index=i
                    print("match!")
            if (result_index>-1):
                sender_name=sendee_list[result_index][0]
            else:
                sender_name=sender
                    
                
            
            msg = r[1]
            #print(sim_num," -- ",msg)
            messages.append(sender_name+"> "+msg)
            #messages.append(sender+"> "+msg)
            msg_display_index=len(messages)-1
            #
            label1.text='> '+instr
        show_messages(msg_display_index)
    else:
        label1.text='> (no new messages)'
        print("no new messages")
        time.sleep(1)
        label1.text='> '+instr
    
    
        
def send_message(number,message):
    label1.text='> (checking messages...)'
    uart.write(bytes('AT+CFUN=1\r',"ascii"))
    time.sleep(1)
    data=uart.read(64)
    print(data)
    #messages.append(data.strip())
    uart.write(bytes('AT+CMGF=1\r',"ascii"))
    time.sleep(1)
    data=uart.read(64)
    #messages.append(data.strip())
    print(data)
    uart.write(bytes('AT+CMGS=\"+'+str(number)+'\"\r',"ascii"))
    time.sleep(1)
    data=uart.read(64)
    #messages.append(data.strip())
    print(data)
    uart.write(bytes(message+'\x1a',"ascii"))
    time.sleep(1)
    data=uart.read(64)
    #messages.append(data.strip())
    print(data)
    label1.text='> (message sent!)'
    time.sleep(1)
    label1.text='> '

starttime = time.monotonic()

while True:


    #if (time.monotonic() - starttime > check_message_interval):
    #    print("check messages")
    #    get_messages()
    #    starttime=time.monotonic()
        
    
    i2c.readfrom_into(cardkb,b)
    if (b == LEFT):
        print("left!")
        sendee_index=(sendee_index+1)%len(sendee_list)
        sendee=sendee_list[sendee_index]
        label0.text="TO: "+str(sendee)
        continue
    if (b == RIGHT):
        print("right!")
        get_messages()
        continue
    if (b == UP):
        print("up!")
        if(len(messages)>message_lines):
            msg_display_index=(msg_display_index-1)%len(messages)
            if(msg_display_index<(message_lines-1)):
                msg_display_index=message_lines-1
            print("index=",msg_display_index)
            show_messages(msg_display_index)
            #label2.text="<"+str(msg_display_index)+"> "+messages[msg_display_index]
        continue
    if (b == DOWN):
        print("down!")
        if(len(messages)>message_lines):
            #msg_display_index=(msg_display_index+1)%len(messages)
            msg_display_index=msg_display_index+1
            if(msg_display_index>len(messages)-1):
                msg_display_index=len(messages)-1
            print("index=",msg_display_index)
            show_messages(msg_display_index)
            #label2.text="<"+str(msg_display_index)+"> "+messages[msg_display_index]
        continue
    
    c=b.decode()
    if (c != ESC and c != NUL and c !=LEFT):
        if (c == CR):
            print('\nsending:',instr)
            label1.text='> '
            send_message(sendee[1],instr.strip())
            messages.append("me: "+instr.strip())
            msg_display_index=len(messages)-1
            show_messages(msg_display_index)
            instr=''
        else:
            print(c, end='')
            instr=instr+c
            label1.text='> '+instr
 
# be nice, clean up
i2c.unlock()
