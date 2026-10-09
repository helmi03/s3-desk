"""Send console commands to the board and print what comes back.

usage: devcmd.py [--port P] [--wait S] [--reset] "cmd1" "cmd2" ...
"""
import argparse
import sys
import time

import serial


def open_port(port):
    s = serial.Serial(port, 115200, timeout=0.1)
    return s


def reset(s):
    s.dtr = False
    s.rts = True
    time.sleep(0.1)
    s.rts = False


def read_for(s, seconds, until=None):
    end = time.time() + seconds
    buf = b""
    while time.time() < end:
        buf += s.read(65536)
        if until and until.encode() in buf:
            break
    return buf.decode("utf-8", "replace")


def run(s, cmd, wait):
    s.write((cmd + "\r\n").encode())
    s.flush()
    return read_for(s, wait)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/cu.usbmodem101")
    ap.add_argument("--wait", type=float, default=1.5)
    ap.add_argument("--reset", action="store_true")
    ap.add_argument("--boot-wait", type=float, default=8)
    ap.add_argument("cmds", nargs="*")
    a = ap.parse_args()
    s = open_port(a.port)
    if a.reset:
        reset(s)
        sys.stdout.write(read_for(s, a.boot_wait))
    else:
        s.read(65536)
    for c in a.cmds:
        sys.stdout.write(run(s, c, a.wait))
    sys.stdout.flush()


if __name__ == "__main__":
    main()
