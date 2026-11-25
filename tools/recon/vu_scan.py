#!/usr/bin/env python3
"""Find VIF MPG packets (VU microcode uploads) in the loaded image and group them into programs.
Usage: vu_scan.py <SLUS_212.70>"""
import sys, struct
raw = open(sys.argv[1],'rb').read()
OFF, BASE, END = 0x1000, 0x100000, 0x100000+0x2c962c
n = (END-BASE)//4
w = struct.unpack_from(f'<{n}I', raw, OFF)
hits = []
for i in range(n):
    x = w[i]
    if (x >> 24) & 0x7F == 0x4A:
        num = (x >> 16) & 0xFF or 256
        addr = x & 0xFFFF
        # MPG data must be 64-bit aligned: the MPG word sits at an odd word index (data starts at next word, 8-aligned)
        if (i % 2) == 1 and addr < 0x800 and BASE + i*4 >= 0x285300:
            hits.append((BASE+i*4, num, addr))
print(len(hits), "MPG-looking words in .data and after")
for a, num, addr in hits: print(f"  {a:08X} MPG num={num:3} ({num*8:#06x} bytes) to VU addr {addr:#05x} (byte {addr*8:#06x})")
print("---- chains")
by = {a: (num, addr) for a, num, addr in hits}
used = set(); progs = []
for a, num, addr in hits:
    if a in used: continue
    chain = [(a, num, addr)]; used.add(a)
    cur = (a, num, addr)
    while True:
        end = cur[0] + 4 + cur[1]*8
        nxt = next((b for b in range(end, end+20, 4) if b in by and by[b][1] == cur[2]+cur[1]), None)
        if nxt is None: break
        cur = (nxt,)+by[nxt]; chain.append(cur); used.add(nxt)
    progs.append(chain)
good = [c for c in progs if len(c) > 1 or c[0][2] in (0,) ]
for c in good:
    tot = sum(x[1] for x in c)*8
    print(f"  {c[0][0]:08X} {len(c):2} packets  VU {c[0][2]*8:#06x}-{(c[-1][2]+c[-1][1])*8:#06x} ({tot:#x} bytes)")
print(len(good), "candidate programs;", sum(len(c) for c in good), "packets")
