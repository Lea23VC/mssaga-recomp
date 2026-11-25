#!/usr/bin/env python3
"""Put screenshots side by side at half size, to look at a test run at once (no PIL needed).

    tools/sheet.py out.png build/shot_2800.ppm build/shot_3200.ppm ...
"""
import struct
import sys
import zlib


def main():
    imgs = []
    for path in sys.argv[2:]:
        data = open(path, "rb").read()
        parts = data.split(b"\n", 3)
        w, h = map(int, parts[1].split())
        imgs.append((w, parts[3]))
    w, h = imgs[0][0] // 2, (len(imgs[0][1]) // (imgs[0][0] * 3)) // 2
    rows = []
    for y in range(h):
        row = bytearray()
        for iw, px in imgs:
            line = px[2 * y * iw * 3:(2 * y + 1) * iw * 3]
            row += b"".join(line[x * 6:x * 6 + 3] for x in range(w))
        rows.append(b"\0" + bytes(row))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w * len(imgs), h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(b"".join(rows), 6)) + chunk(b"IEND", b"")
    open(sys.argv[1], "wb").write(png)


main()
