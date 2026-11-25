#!/usr/bin/env python3
"""Draw the Windows program icon (a generic dawn over a horizon, nothing from the game) as an .ico with PNG images.

    make_icon.py out.ico
"""
import math
import struct
import sys
import zlib


def draw(n):
    px = []
    r_corner = n * 0.18
    sun_cx, sun_cy, sun_r = n * 0.5, n * 0.62, n * 0.26
    horizon = n * 0.62
    for y in range(n):
        row = []
        for x in range(n):
            fx, fy = x + 0.5, y + 0.5
            # rounded square mask (anti-aliased)
            dx = max(r_corner - fx, 0, fx - (n - r_corner))
            dy = max(r_corner - fy, 0, fy - (n - r_corner))
            d = math.hypot(dx, dy) - r_corner
            a = max(0.0, min(1.0, 0.5 - d))
            # sky: deep blue to violet; below the horizon: dark ground
            t = fy / n
            if fy < horizon:
                col = (20 + 60 * t, 30 + 30 * t, 70 + 50 * t)
                sd = math.hypot(fx - sun_cx, fy - sun_cy) - sun_r
                if sd < 0.5:                                   # the sun
                    k = max(0.0, min(1.0, 0.5 - sd))
                    sun = (255, 150 + 80 * (1 - (sun_cy - fy) / sun_r), 60)
                    col = tuple(c * (1 - k) + s * k for c, s in zip(col, sun))
                elif sd < n * 0.12:                            # its glow
                    k = (1 - sd / (n * 0.12)) * 0.45
                    col = tuple(c * (1 - k) + s * k for c, s in zip(col, (255, 140, 60)))
            else:
                col = (12, 16, 34)
                if fy < horizon + max(1.0, n / 48):            # the horizon line
                    col = (255, 190, 110)
            row.append((int(col[0]), int(col[1]), int(col[2]), int(a * 255)))
        px.append(row)
    return px


def png(px):
    n = len(px)
    raw = b"".join(b"\0" + bytes(v for p in row for v in p) for row in px)
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", n, n, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    sizes = [256, 64, 48, 32, 16]
    images = [png(draw(n)) for n in sizes]
    out = struct.pack("<HHH", 0, 1, len(sizes))
    off = 6 + 16 * len(sizes)
    for n, img in zip(sizes, images):
        out += struct.pack("<BBBBHHII", n % 256, n % 256, 0, 0, 1, 32, len(img), off)
        off += len(img)
    open(sys.argv[1], "wb").write(out + b"".join(images))


main()
