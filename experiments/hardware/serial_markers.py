"""Open the USB diagnostic channel without changing reset control lines."""
import serial


def open_console():
    port=serial.Serial(port=None,baudrate=115200,timeout=.2)
    port.dtr=False
    port.rts=False
    port.port='COM3'
    port.open()
    return port
