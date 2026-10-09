"""On-device acceptance test over the USB console.

Run with: uvx --with pyserial --with pillow python tools/smoke.py [--port P]
Every check prints PASS or FAIL; the exit code is the number of failures.
"""
import argparse
import base64
import datetime
import json
import os
import re
import sys
import time

import serial

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MYT = datetime.timezone(datetime.timedelta(hours=8))
BAD = re.compile(r"Guru Meditation|abort\(\)|Draw bitmap failed|Failed to acquire LVGL lock|AUDIO FAILED|assert failed")
MONTHS = {"Jan": 1, "Feb": 2, "Mac": 3, "Apr": 4, "Mei": 5, "Jun": 6, "Jul": 7, "Ogos": 8, "Sep": 9, "Okt": 10,
          "Nov": 11, "Dis": 12}

failures = 0


def check(ok, what, detail=""):
    global failures
    print(("PASS " if ok else "FAIL ") + what + (f"  ({detail})" if detail and not ok else ""))
    failures += 0 if ok else 1
    return ok


class Board:
    def __init__(self, port):
        self.s = serial.Serial(port, 115200, timeout=0.1)
        self.log = ""

    def read(self, seconds, until=None):
        end = time.time() + seconds
        got = ""
        while time.time() < end:
            got += self.s.read(65536).decode("utf-8", "replace")
            if until and re.search(until, got):
                break
        self.log += got
        return got

    def cmd(self, line, seconds=2.0, until=None):
        self.s.write((line + "\r\n").encode())
        return self.read(seconds, until)

    def reset(self):
        self.s.dtr = False
        self.s.rts = True
        time.sleep(0.1)
        self.s.rts = False

    def status(self):
        out = self.cmd("status", 3, r"STATUS .*\n")
        m = re.search(r"STATUS (.*)", out)
        return dict(kv.split("=", 1) for kv in m.group(1).split()) if m else {}

    def set_time_now(self):
        now = datetime.datetime.now(MYT)
        self.cmd("time " + now.strftime("%Y-%m-%dT%H:%M:%S"), 2)
        return now


def official_times(date):
    path = os.path.join(ROOT, "fixtures", "esolat", "SGR03-year.json")
    for row in json.load(open(path))["prayerTime"]:
        d, mon, y = row["date"].split("-")
        if (int(y), MONTHS[mon], int(d)) == (date.year, date.month, date.day):
            return ",".join(row[k][:5] for k in ["imsak", "fajr", "syuruk", "dhuhr", "asr", "maghrib", "isha"])
    return None


def boot(b):
    b.reset()
    out = b.read(20, r"app: zone .*\n")
    out += b.read(4)
    check("app: zone" in out, "boots to the app loop", out[-300:])
    check(not BAD.search(out), "no crash, lock or display errors during boot", (BAD.search(out) or [""])[0])
    return out


def snapshot(b, path):
    out = b.cmd("snap", 25, r"SNAP END")
    m = re.search(r"SNAP (\d+) (\d+) (\d+)\r?\n(.*?)SNAP END", out, re.S)
    if not check(m is not None, "screen dump received"):
        return
    w, h, stride = int(m.group(1)), int(m.group(2)), int(m.group(3))
    raw = base64.b64decode("".join(m.group(4).split()))
    if not check(len(raw) == stride * h, "screen dump is complete", f"{len(raw)} of {stride * h} bytes"):
        return
    from PIL import Image

    img = Image.new("RGB", (w, h))
    px = img.load()
    lit = 0
    for y in range(h):
        row = raw[y * stride:(y + 1) * stride]
        for x in range(w):
            v = row[2 * x] | (row[2 * x + 1] << 8)
            r, g, bl = (v >> 11) << 3, ((v >> 5) & 0x3F) << 2, (v & 0x1F) << 3
            px[x, y] = (r, g, bl)
            lit += (r + g + bl) > 60
    img.save(path)
    check(lit > w * h // 100, f"screen has content ({lit} lit pixels), saved {os.path.relpath(path, ROOT)}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/cu.usbmodem101")
    a = ap.parse_args()
    b = Board(a.port)

    boot(b)
    now = b.set_time_now()
    st = b.status()
    check(st.get("valid") == "1", "clock valid after setting time", st)
    check(st.get("have_today") == "1", "today's times available", st)
    expect = official_times(now.date())
    check(expect is None or st.get("times") == expect, "today's times match the official e-solat data",
          f"device {st.get('times')} vs official {expect}")

    out = b.cmd("tone", 4)
    check("codec open" in out or "playing /spiffs/tone.mp3" in out, "test tone starts through the codec", out[-200:])
    check(not BAD.search(out), "no audio errors")
    b.cmd("stop", 1)

    out = b.cmd("fake 1", 2)
    prayer = (re.search(r"FAKE (\w+) at minute \d+ \(armed\)", out) or [None, None])[1]
    if check(prayer is not None, "fake prayer armed one minute ahead", out[-200:]):
        out = b.read(80, r"ADHAN start \w+ .*\n")
        check(f"ADHAN start {prayer}" in out and "playing" in out, f"Adhan starts for {prayer}", out[-300:])
        check(b.status().get("playing") == "1", "status reports Adhan playing")
        out = b.cmd("key", 3)
        check("ADHAN stop" in out, "GPIO18 key path stops the Adhan", out[-200:])
        time.sleep(1.6)
        check(b.status().get("playing") == "0", "status reports Adhan stopped")

        boot(b)
        b.set_time_now()
        out = b.cmd("fake 0", 2)
        out += b.read(8)
        check("ADHAN start" not in out, f"no replay of {prayer} after reboot in the same prayer slot", out[-300:])
        mark = b.status().get("mark", "")
        check(mark == f"{now.date().isoformat()}/{prayer}", "last-Adhan mark survived the reboot", mark)

    snapshot(b, os.path.join(ROOT, "sim", "shots", "device-screen.png"))
    check(not BAD.search(b.log), "no crash, lock, display or audio errors in the whole run", (BAD.search(b.log) or [""])[0])
    with open(os.path.join(ROOT, "sim", "shots", "smoke-serial.log"), "w") as f:
        f.write(b.log)
    print(f"\n{failures} failure(s)")
    sys.exit(failures)


if __name__ == "__main__":
    main()
