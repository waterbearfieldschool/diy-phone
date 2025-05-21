import time
import board
import busio
import digitalio

uart = busio.UART(board.TX, board.RX, baudrate=115200,timeout=10)



while True:
    uart.write(bytes('AT+CSQ\r\n',"ascii"))
    #uart.write(bytes('AT\r\n',"ascii"))
    time.sleep(1)
    #data=uart.read(uart.in_waiting).decode()
    data=uart.read(uart.in_waiting)
    print(data)
    time.sleep(2)
