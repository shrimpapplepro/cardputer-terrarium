#!/usr/bin/env python3
"""Talk to the terrarium over USB-Serial/JTAG.
  ser.py 's'            send one command, print the reply
  ser.py -w 8           just listen for 8 s
  ser.py --shot out.png screenshot the panel (sends P, decodes RGB332)
Must run under PlatformIO's python (has pyserial): ~/.platformio/penv/bin/python
Set TERRARIUM_PORT to override the auto-detected port.
"""
import glob, os, sys, time, zlib, struct, serial

def find_port():
    """TERRARIUM_PORT wins; otherwise the first USB-Serial/JTAG-looking device."""
    if os.environ.get("TERRARIUM_PORT"):
        return os.environ["TERRARIUM_PORT"]
    for pat in ("/dev/cu.usbmodem*", "/dev/ttyACM*"):
        hits = sorted(glob.glob(pat))
        if hits:
            return hits[0]
    sys.exit("no serial port found; plug the Cardputer in or set TERRARIUM_PORT")

PORT = find_port()

def open_port():
    s = serial.Serial()
    s.port = PORT; s.baudrate = 115200; s.timeout = 0.2
    s.dtr = True; s.rts = False   # HWCDC only talks with DTR asserted; RTS low = no reset
    s.open()
    return s

def read_for(s, secs, stop=None):
    end = time.time() + secs; buf = b""
    while time.time() < end:
        d = s.read(4096)
        if d:
            buf += d; end = max(end, time.time() + 0.6) if not stop else end
            if stop and stop in buf: break
    return buf.decode("utf-8", "replace")

def png(path, w, h, rgb, scale=4):
    if scale > 1:
        rgb = b"".join(b"".join(rgb[(y*w+x)*3:(y*w+x)*3+3]*scale for x in range(w)) * scale for y in range(h))
        w, h = w*scale, h*scale
    raw = b"".join(b"\x00" + rgb[y*w*3:(y+1)*w*3] for y in range(h))
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                           chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))

def main():
    a = sys.argv[1:]
    s = open_port()
    if a[:1] == ["-w"]:
        print(read_for(s, float(a[1])), end=""); return
    if a[:1] == ["--shot"]:
        s.reset_input_buffer(); s.write(b"P\n")
        out = read_for(s, 10, stop=b"FBEND")
        lines = out.splitlines(); i = lines.index(next(l for l in lines if l.startswith("FBSTART")))
        rows = [l.strip() for l in lines[i+1:i+136]]
        assert len(rows) == 135 and all(len(r) == 960 for r in rows), "short framebuffer dump"
        rgb = bytearray()
        for r in rows:
            for x in range(240):
                v = int(r[x*4:x*4+4], 16)  # RGB565, big-endian
                r5, g6, b5 = v >> 11, (v >> 5) & 63, v & 31
                rgb += bytes((r5*255//31, g6*255//63, b5*255//31))
        png(a[1], 240, 135, bytes(rgb)); print("wrote", a[1]); return
    s.reset_input_buffer()
    s.write((" ".join(a) + "\n").encode())
    print(read_for(s, 3), end="")

main()
