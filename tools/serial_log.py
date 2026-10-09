import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbmodem101"
seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 10
s = serial.Serial(port, 115200, timeout=0.2)
s.dtr = False
s.rts = True
time.sleep(0.1)
s.rts = False
end = time.time() + seconds
buf = b""
while time.time() < end:
    buf += s.read(4096)
sys.stdout.write(buf.decode("utf-8", "replace"))
