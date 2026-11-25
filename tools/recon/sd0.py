#!/usr/bin/env python3
"""Decompress an SD0 file (the game's own LZ + run-length format, `.cmp` entries of AFSLIST0.AFS).

    sd0.py <in.cmp> <out.bin>

Written from the game's decoder PS_DecSlide (0x10D1D0 in SLUS_212.70). Header: "SD0\\0", u32 compressed file
size, u32 decompressed size; the stream follows at +12. A flag byte gives 8 items, low bit first: 0 = one
literal byte, 1 = a 2- or 3-byte token whose low nibble says what it is:
    n == 0: copy b2+16 bytes from dist = b1<<4 | b0>>4 back
    n == 1: repeat byte b1, (b0>>4)+3 times
    n == 2: (b1<<4 | b0>>4) + 18 literal bytes follow
    n >= 3: copy n bytes from dist = b1<<4 | b0>>4 back
"""
import struct
import sys


def sd0(data):
    assert data[:4] == b"SD0\0", data[:4]
    size = struct.unpack_from("<I", data, 8)[0]
    out = bytearray()
    p = 12
    while len(out) < size:
        flags = data[p]
        p += 1
        for _ in range(8):
            if len(out) >= size:
                break
            if not flags & 1:
                out.append(data[p])
                p += 1
            else:
                b0, b1 = data[p], data[p + 1]
                p += 2
                n, hi = b0 & 15, (b1 << 4) | (b0 >> 4)
                if n == 0:
                    cnt = data[p] + 16
                    p += 1
                elif n == 1:
                    out += bytes([b1]) * ((b0 >> 4) + 3)
                    cnt = 0
                elif n == 2:
                    out += data[p:p + hi + 18]
                    p += hi + 18
                    cnt = 0
                else:
                    cnt = n
                for _ in range(cnt):             # byte by byte: source and destination may overlap
                    out.append(out[-hi])
            flags >>= 1
    return bytes(out[:size]), p


if __name__ == "__main__":
    data = open(sys.argv[1], "rb").read()
    out, used = sd0(data)
    open(sys.argv[2], "wb").write(out)
    print(f"{len(out):#x} bytes out, {used:#x} of {len(data):#x} bytes read")
