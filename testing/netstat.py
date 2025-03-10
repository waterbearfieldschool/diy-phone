import time
import board
import busio
import digitalio

uart = busio.UART(board.TX, board.RX, baudrate=115200,timeout=0)



while True:
    uart.write(bytes('AT+CSQ\r',"ascii"))
    #uart.write(bytes('AT\r',"ascii"))
    time.sleep(1)
    data=uart.read(uart.in_waiting).decode()
    print(data)
    time.sleep(2)
