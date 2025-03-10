import time
import board
import busio
import displayio
import framebufferio
import sharpdisplay
import busio
import digitalio
import gc
#import supervisor
from audiocore import RawSample
import array
import math
from adafruit_datetime import datetime, timedelta
from __future__ import annotations

sendee_list = [["Don",16512524765],["Emilie",16463278220],["David",15304929688],["Liz",16174299144],["me",17819189114]]

sendee_index=0

sendee=sendee_list[sendee_index]

check_message_interval = 60 # in seconds

history_length = 6 #number of messages to show
#messages_in = []
#messages_out = []

max_msg_width = 45

try:
    from audioio import AudioOut
except ImportError:
    try:
        from audiopwmio import PWMAudioOut as AudioOut
    except ImportError:
        pass  
        
tone_volume = 1 # Increase this to increase the volume of the tone.
frequency = 2500  # Set this to the Hz of the tone you want to generate.
length = 8000 // frequency
sine_wave = array.array("H", [0] * length)
for i in range(length):
    sine_wave[i] = int((1 + math.sin(math.pi * 2 * i / length)) * tone_volume * (2 ** 15 - 1))

audio = AudioOut(board.A0)
sine_wave_sample = RawSample(sine_wave)

displayio.release_displays()

#spi = busio.SPI(board.D11, MOSI=board.D12)
spi = board.SPI()
chip_select_pin = board.D10

framebuffer = sharpdisplay.SharpMemoryFramebuffer(spi, chip_select_pin, width=400, height=240)
#framebuffer = sharpdisplay.SharpMemoryFramebuffer(spi, chip_select_pin, width=144, height=168)

display = framebufferio.FramebufferDisplay(framebuffer)

MY_NUMBER = 17819189114
from adafruit_display_text.label import Label
from terminalio import FONT

label0 = Label(font=FONT, text="TO: "+str(sendee), x=0, y=5, scale=1, line_spacing=1.2)
label1 = Label(font=FONT, text="> ", x=0, y=20, scale=1, line_spacing=1.2)
label3 = Label(font=FONT, text="-------------- messages --------------", x=0, y=35, scale=1, line_spacing=1.2)
label2 = Label(font=FONT, text="(no messages)", x=0, y=50, scale=1, line_spacing=1.2)
label4 = Label(font=FONT, text="::::", x=275, y=5, scale=1, line_spacing=1.2)
label5 = Label(font=FONT, text="[time]", x=275, y=20, scale=1, line_spacing=1.2)
label6 = Label(font=FONT, text="-------------- system --------------", x=0, y=210, scale=1, line_spacing=1.2)
label7 = Label(font=FONT, text="[ no errors ]", x=0, y=225, scale=1, line_spacing=1.2)

#display.root_group = label

text_group = displayio.Group()
text_group.append(label0)
text_group.append(label1)
text_group.append(label2)
text_group.append(label3)
text_group.append(label4)
text_group.append(label5)
text_group.append(label6)
text_group.append(label7)

display.root_group=text_group
#display.show(text_group)

#i2c = busio.I2C(board.SCL, board.SDA)

#while not i2c.try_lock():
#    pass
 
#cardkb = i2c.scan()[0]  # should return 95
#i2c_devices = i2c.scan()
#print(i2c_devices)
#cardkb=i2c_devices[0]
#if cardkb != 95:
#    print("!!! Check I2C config: " + str(i2c))
#    print("!!! CardKB not found. I2C device", cardkb,
#          "found instead.")
#    exit(1)
 
ESC = chr(27)
NUL = '\x00'
CR = "\r"
LF = "\n"
TAB = bytearray(b'\x09')
LEFT = bytearray(b'\xB4')
RIGHT = bytearray(b'\xB7')
DOWN = bytearray(b'\xB6')
UP = bytearray(b'\xB5')
BACK = bytearray(b'\x08')
c = ''
b = bytearray(1)

instr = ''
radio_instr = ''
uart = busio.UART(board.TX, board.RX, baudrate=115200,timeout=0,receiver_buffer_size=5024)

msg_display_index=0

message_lines = 12

#messages=[]
messages_out=[]
messages_in=[]

inbox_size = 0

audio_alert = False

def show_messages():
    gc.collect()
    start_mem = gc.mem_free()
    
    #clear messages
    #messages.clear()
   
    #combine lists
    messages=messages_in+messages_out
    
    print("sort")
    messages.sort(key=lambda x: x[1])
            
    print("prune")
    del messages[:-history_length]
            
    # highlight is the last message
    #messages_in=messages_in[-3:]
    #messages_out=messages_out[-3:]
    #full_list=messages_in+messages_out
    #print("full_list=",full_list)
    
    #messages_in.clear()
    #messages_out.clear()
    
    #messages=sorted(messages,key=lambda x: x[1],reverse=True)
    
    #sorted_messages=sorted(messages,key=lambda x: x[1])
    #pruned_messages=messages[-5:]
    
    #print(pruned_messages)
    #print("sorted list:",shorter_messages)
    #print("\n\nlen(shorter_messages)=",len(shorter_messages))
    
    #highlight=len(messages)-1
    outstr=''
    if len(messages)>0:
        for i in range(0,len(messages)):
            dt=messages[i][1]
            timestring=f'{dt.hour:02}:{dt.minute:02}'
            #messages.append([sendtime.timestamp,sendtime,MY_NUMBER,number,message])
            if(messages[i][2]==MY_NUMBER): # message is from me; so, show recipient
                outstr=outstr+timestring+" | Me -> "+str(messages[i][3])+" : "+messages[i][4]+'\n'
            else: # message is from someone else; i'm the recipient
                outstr=outstr+timestring+" | "+str(messages[i][2])+" : "+messages[i][4]+'\n'
            #outstr=pruned_messages[i][1]+" | "+pruned_messages[i][2]+" > "+pruned_messages[i][3]+'\n'+outstr
            #outstr="<"+str(i)+"> "+pruned_messages[i]+'\n'+outstr
        label2.text=outstr
        
    gc.collect()
    end_mem = gc.mem_free()
    print( "Point 2 Available memory: {} bytes".format(end_mem) )
    print( "Code section 1-2 used {} bytes".format(start_mem - end_mem) )
    
def delete_all_messages():
    label7.text="deleting all stored sms messages..."
    print("deleting all stored sms messages")
    uart.write(bytes('AT+CMGD=,2\r',"ascii"))
    time.sleep(.2)
    data=uart.read(uart.in_waiting)
    label7.text="all stored sms messages deleted"
    #time.sleep(1)
    #label7.text=""
    #messages_in.clear()
    #messages_out.clear()
    
def delete_messages(limit,prune,res):

    # note -- if you want to delete all read messages, "AT+CMGD=,2"
    num_msg = len(res)
    if num_msg > limit:
    
        #messages_in.clear()
        #messages_out.clear()
        for i in range(0,prune):
            r = res[i]
            header=r[0].split(',')
            msg_id = header[0].split(":")[1].strip()
            print("msg_id=",msg_id)
            uart.write(bytes('AT+CMGD='+msg_id+'\r',"ascii"))
            time.sleep(.2)
            data=uart.read(uart.in_waiting)
            print(data)
            
    #if len(res)>10:
        
    #cmgls = [row[0] for row in res]
    #print(cmgls)

def get_network_time():
    try:
        uart.write(bytes('AT+CTZU=1\r',"ascii"))
        time.sleep(.2)
        data=uart.read(uart.in_waiting)
        print(data)
        uart.write(bytes('AT+CREG=1\r',"ascii"))
        time.sleep(.2)
        data=uart.read(uart.in_waiting)
        print(data)
        uart.write(bytes('AT+CCLK?\r',"ascii"))
        time.sleep(.2)
        data=uart.read(uart.in_waiting).decode()
        print(data)
        time_info=data.split('\r\n')[1].split(" ")[1].split(",")
        print("time_info",time_info)
        #[1].split(":").split(",")
        
        datelong=time_info[0][1:].strip()
        timelong=time_info[1][:-1].strip()
        ds=datelong.split("/")
        #datestamp=datestamp[1]+"/"+datestamp[2]
        ts=timelong.split(":")
                
        # datetime(year, month, day, hour, minute, second, microsecond)
        nettime = datetime(int(ds[0]), int(ds[1]), int(ds[2]), int(ts[0]), int(ts[1]), int(ts[2].split("-")[0]), 0)
        
        #print(f'length = {length:03}')
        label5.text=f'{ts[0]:02}:{ts[1]:02}'
        #label5.text=(str(ts[0])+":"+str(ts[1]))
        
        # looks like network time is early by an hour
        #nettime=nettime+timedelta(hours=1)
        
        print("net time:",nettime.isoformat())
        #print("timestamp",time_info)
        
        
        return(nettime)
    except:
        print("couldn't get network time")
        label7.text="get_network_time() failed"
        #time.sleep(1)
        #label7.text=""
    
    
def get_network_status():
    try:
        uart.write(bytes('AT+CSQ\r',"ascii"))
        time.sleep(.2)
        data=uart.read(uart.in_waiting).decode()
        print(data)
        params=data.split('\r\n')
        sig = params[1].split(':')[1].strip()
        uart.write(bytes('AT+CREG?\r',"ascii"))
        time.sleep(.2)
        data=uart.read(uart.in_waiting).decode()
        params=data.split('\r\n')
        reg = params[1].split(':')[1].strip()
        
        label4.text=":s["+sig+"]::r["+reg+"]:"
        print("sig=",sig)
        print("reg=",reg)
        check_message_interval=30

    except:
        print("failed to get network status")
        label7.text="get_network_status() failed"
        check_message_interval = 5
        #time.sleep(1)
        #label7.text=""
    
def get_messages():

    #audio_alert = False
    
    #inbox_size_original = inbox_size
    gc.collect()
    messages_in.clear()
    
    label7.text='(checking messages...)'
    print("checking messages")
    time.sleep(.2)
    uart.write(bytes('AT+CMGF=1\r',"ascii"))
    time.sleep(.2)
    data=uart.read(uart.in_waiting)
    print(data)
    uart.write(bytes('AT+CMGL=\"ALL\"\r',"ascii"))
    #uart.write(bytes('AT+CMGL=\"STO SENT\"\r',"ascii"))
    #uart.write(bytes('AT+CMGL=\"REC UNREAD\"\r',"ascii"))
    time.sleep(.2)
    data=uart.read(uart.in_waiting).decode()
    print(data)
    ls=data.split('\r\n')
    ls.pop(0) # remove the modem reply at beginning
    ls = ls[:-3] # remove the modem reply at end
    if len(ls)>0:
        #print(ls)
        res = [ls[i:i+2] for i in range(0,len(ls),2)]
        print(res)
        
        # prune if too many messages
        delete_messages(25,10,res)
        
        
        
        #messages_in.clear()
        #message_num=0
        for r in res:
            header=r[0].split(',')
            sim_num=header[0].split(':')[1]
            sender=header[2][2:]
            sender=sender[:-1].strip()
            
            datelong=header[4][1:].strip()
            timelong=header[5][:-1].strip()
            ds=datelong.split("/")
            #datestamp=datestamp[1]+"/"+datestamp[2]
            ts=timelong.split(":")
            #timestamp=timestamp[0]
            
            # datetime(year, month, day, hour, minute, second, microsecond)
            sendtime = datetime(int(ds[0]), int(ds[1]), int(ds[2]), int(ts[0]), int(ts[1]), int(ts[2].split("-")[0]), 0)
            
            # looks like our messages get stamped 3 hours earlier; so, shift
            sendtime=sendtime+timedelta(hours=3)

            #print("datestamp=",datestamp)
            #print("timestamp=",timestamp)
            #time=header[
            
            
            
            # check to see if number is in address book
            numbers = [row[1] for row in sendee_list]
            i=0
            result_index=-1
            for number in numbers:
                #print(sender,number)
                if (int(sender)==int(number)):
                    result_index=i
                    #print("match!")
                i=i+1
            if (result_index>-1):
                #print("result_index=",result_index)
                sender_name=sendee_list[result_index][0]
            else:
                sender_name=sender
                    
                
            #print("timestamp=",sendtime.timestamp())
            
            msg = r[1]
            #print(sim_num," -- ",msg)
            
            
            print("append")
            #add line breaks
            print("msg=",msg)
            print("len(msg)=",len(msg))

            print("chunks:")
            res=[msg[y-max_msg_width:y] for y in range(max_msg_width, len(msg)+max_msg_width,max_msg_width)]
            print(res)
            
            msg=""
            msg=res[0]
            i=1
            while i < len(res):
                msg=msg+"\n "+res[i]
                i=i+1
                
            messages_in.append(["0",sendtime,sender_name,sender,msg])
            
            #print("messages:",messages)
            
            
            
            #messages=messages[-5:]
            
            #print("messages sorted:",messages)
            #sorted_messages=sorted(messages,key=lambda x: x[1])
            #pruned_messages=messages[-5:]   
            #play_alert(.1)
            #audio_alert=True
            #print("audio_alert=",audio_alert)
            #messages.append([sendtime.timestamp,sendtime.isoformat(),sender_name,msg])
            #messages.append(sendtime.isoformat() +" | "+sender_name+" > "+msg)
            #messages.append(sender+"> "+msg)
            #msg_display_index=len(messages_in)+len(messages_out)-1
            #
            #label1.text='> '+instr
        label7.text=''
        
        inbox_size=len(messages_in)
        
        #if (inbox_size > inbox_size_original):
            #audio_alert=True
        
        show_messages()
    else:
        #audio_alert=False
        label7.text=''
        print("no new messages")

def play_alert(seconds):
    audio.play(sine_wave_sample, loop=True)
    time.sleep(seconds)
    audio.stop()
       
def hangup():
    try:
        uart.write(bytes('AT+CHUP\r',"ascii")) # hang up
        time.sleep(.2)
        data=uart.read(uart.in_waiting).decode()
        print(data)
        print("hanging up")
        label7.text="hanging up"
        #time.sleep(1)
        #label7.text=""
    except:
        print("couldn't hang up")
        label7.text="hangup() failed"
        #time.sleep(1)
        #label7.text=""
    
def answer_call():
    try:
        uart.write(bytes('ATA\r',"ascii")) # switch to headphones
        time.sleep(.2)
        data=uart.read(uart.in_waiting).decode()
        print(data)
    except:
        print("couldn't answer call")
        label7.text="answer_call() failed"
        #time.sleep(1)
        #label7.text=""
          
def make_call(recipient):
    
    label7.text="calling: "+recipient
    try:
    
        uart.write(bytes('AT+CNUM\r',"ascii")) # switch to headphones
        time.sleep(.2)
        data=uart.read(uart.in_waiting).decode()
        print(data)
        
        uart.write(bytes('AT+CSDVC=1\r',"ascii")) # switch to headphones
        time.sleep(.2)
        data=uart.read(uart.in_waiting).decode()
        print(data)
        
        uart.write(bytes('AT+CLVL=?\r',"ascii")) # query volume range
        time.sleep(.2)
        data=uart.read(uart.in_waiting).decode()
        print(data)
        
        uart.write(bytes('AT+CLVL=5\r',"ascii")) # set volume to 2
        time.sleep(.2)
        data=uart.read(uart.in_waiting).decode()
        print(data)
        
        uart.write(bytes('ATD+'+str(recipient)+';\r',"ascii")) # set volume to 2
        time.sleep(.2)
        data=uart.read(uart.in_waiting).decode()
        print(data)

    except:
        print("failed to make call")
        label7.text="make_call() failed"
        #time.sleep(1)
        #label7.text=""
         
        
def send_message(recipient,message):
    #label1.text='> (checking messages...)'
    
    label7.text='> sending message ...'
    
    uart.write(bytes('AT+CFUN=1\r',"ascii"))
    time.sleep(.2)
    data=uart.read(uart.in_waiting)
    print(data)
    #messages.append(data.strip())
    uart.write(bytes('AT+CMGF=1\r',"ascii"))
    time.sleep(.2)
    data=uart.read(uart.in_waiting)
    #messages.append(data.strip())
    print(data)
    uart.write(bytes('AT+CMGS=\"+'+str(recipient)+'\"\r',"ascii"))
    time.sleep(.5)
    data=uart.read(uart.in_waiting)
    #messages.append(data.strip())
    print(data)
    uart.write(bytes(message+'\x1a',"ascii"))
    time.sleep(2)
    data=uart.read(uart.in_waiting)
    #print("send result:",data)
    send_result=data.decode().split('\r\n')
    print("send_result=",send_result)
    print("len(send_result)=",len(send_result))
    
    if (len(send_result)==6):
        status=send_result[2].split(':')[0]
        print("status=",status)
        
        print("network time...")
        sendtime=get_network_time()
        
        
        # check to see if number is in address book
        numbers = [row[1] for row in sendee_list]
        i=0
        result_index=-1
        for number in numbers:
            #print(recipient,number)
            if (int(recipient)==int(number)):
                result_index=i
                #print("match!")
            i=i+1
        if (result_index>-1):
            #print("result_index=",result_index)
            recipient_name=sendee_list[result_index][0]
        else:
            recipient_name=recipient
            
        
        
        messages_out.append(["0",sendtime,MY_NUMBER,recipient_name,message])
        
        #print("sort")
        #messages_out.sort(key=lambda x: x[1])
            
        print("prune")
        del messages_out[:-(history_length-1)]
        
        #msg_display_index=len(messages_in)+len(messages_out)-1
        show_messages()
        label7.text='> (message sent!)'
        #time.sleep(1)
        label1.text="> "
    
    else:
        print("send failed")
        label7.text='send failed.'
        uart.write(bytes('AT\r',"ascii"))
        data=uart.read(uart.in_waiting)
        print(data)
        time.sleep(1)
        label7.text=''
    
    #messages.append(data.strip())
    
    # now store message in memory
    #number="17819189114"
    #uart.write(bytes('AT+CMGW=\"+'+str(number)+'\"\r',"ascii"))
    #time.sleep(1)
    #data=uart.read(uart.in_waiting)
    #uart.write(bytes(message+'\x1a',"ascii"))
    #time.sleep(1)
    #data=uart.read(uart.in_waiting)
    

starttime = time.monotonic()
label7.text="booting up ..."
time.sleep(2)
label7.text="Ready."
get_network_status()
get_network_time()
get_messages()
#delete_all_message()


while True:

    data=uart.read(uart.in_waiting)
    #print("data=",data)decode()
    if len(data)>0:
        print(data)
        try:
            data=data.decode()
            cleaned = data.replace("\r","")
            cleaned = cleaned.replace("\n","")
            print(cleaned)
            if(data.strip()=='RING'):
                label7.text="INCOMING CALL"
                print("INCOMING CALL")
            if("SM" in cleaned):
                label7.text="NEW SMS"
                print("NEW SMS")
                #print(data.split(":"))
                #print(data.split("\r\n"))
                #print(data.strip().split(":"))
                play_alert(.1)
                get_messages()
        except:
            print("couldn't decode uart")
            #label7.text="couldn't decode uart"
        
    
    if (time.monotonic() - starttime > check_message_interval):
        label7.text=""
        #print("check messages")
        #get_messages()
        get_network_status()
        get_network_time()
        get_messages()
        #print("audio_alert=",audio_alert)
        #if(audio_alert):
            #play_alert(.1)
        starttime=time.monotonic()
        
    
    #i2c.readfrom_into(cardkb,b)
    if (b == LEFT):
        print("left!")
        #sendee_index=(sendee_index+1)%len(sendee_list)
        #sendee=sendee_list[sendee_index]
        #label0.text="TO: "+str(sendee)
        #get_network_status()
        #get_network_time()
        delete_all_messages()
        continue
    if (b == RIGHT):
        print("right!")
        get_network_status()
        get_network_time()
        get_messages()
        #make_call(sendee[1])
        continue
    if (b == ESC):
        audio_alert=False
        print("audio alert off")
        continue
    if (b == UP):
        #answer_call()
        make_call(sendee[1])
        continue
    if (b == DOWN):
        hangup()
        continue
    if (b == TAB):
        print("tab!")
        sendee_index=(sendee_index+1)%len(sendee_list)
        sendee=sendee_list[sendee_index]
        label0.text="TO: "+str(sendee)
        continue
        
    if (b == BACK) and len(instr)>0:
        print("DELETE")
        instr=instr[:-1]
        print("instr=",instr)
        label1.text='> '+instr
        #del instr[-1]
    
    c=b.decode()
    
    if (c != ESC and c != NUL and c !=LEFT and c!=BACK):
        if (c == CR):
            print('\nsending:',instr)
            label7.text='> (sending message ...)'
            send_message(sendee[1],instr.strip())
            #messages.append("me: "+instr.strip())
            #get_messages()
            instr=''
        else:
            #print("back in!")
            print(c, end='')
            instr=instr+c
            label1.text='> '+instr
 
# be nice, clean up
i2c.unlock()
