#!/usr/bin/env python3
"""Find function starts in .text (jal targets, code after `jr ra`), how each saves s0-s7 (sq/sd), syscall stubs,
`jr ra` outside .text. Usage: funcs.py <SLUS_212.70> <out.tsv>"""
import sys, struct, collections
raw = open(sys.argv[1],'rb').read()
OFF, BASE, TEND, IMGEND = 0x1000, 0x100000, 0x285290, 0x100000+0x2c962c
n = (IMGEND-BASE)//4
w = struct.unpack_from(f'<{n}I', raw, OFF)
W = lambda a: w[(a-BASE)//4]
nt = (TEND-BASE)//4
SREGS = {16,17,18,19,20,21,22,23,30}
starts = {0x100008}
for i in range(nt):
    x = w[i]
    if x >> 26 == 3:
        t = (x & 0x3FFFFFF) << 2
        if BASE <= t < TEND: starts.add(t)
    if x == 0x03E00008 and i+2 < nt:
        j = i+2
        while j < nt and w[j] == 0: j += 1
        if j < nt: starts.add(BASE+j*4)
starts = sorted(starts)
lab = collections.Counter(); align = collections.Counter(); size = {}
rows = []
for k, s in enumerate(starts):
    e = starts[k+1] if k+1 < len(starts) else TEND
    body = w[(s-BASE)//4:(e-BASE)//4]
    sq = sum(1 for x in body if x>>26 == 0x1F and (x>>21)&31 == 29 and (x>>16)&31 in SREGS)
    sd = sum(1 for x in body if x>>26 == 0x3F and (x>>21)&31 == 29 and (x>>16)&31 in SREGS)
    l = "sq" if sq > sd else "sd" if sd > sq else "-"
    rows.append((s, e, l)); lab[l] += 1
    align[16 if s % 16 == 0 else 8 if s % 8 == 0 else 4] += 1
print(len(starts), "function starts in .text (jal targets + after jr ra)")
print("s-reg save style:", dict(lab)); print("start alignment:", dict(align))
# runs
runs = []
for s, e, l in rows:
    if l == "-": continue
    if runs and runs[-1][2] == l: runs[-1][1] = e; runs[-1][3] += 1
    else: runs.append([s, e, l, 1])
print("runs of >= 10 functions with the same save style:")
for s, e, l, c in runs:
    if c >= 10: print(f"  {s:08X}-{e:08X} {l} {c}")
# syscalls: addiu v1,zero,N ; syscall
sc = collections.Counter(); scaddr = collections.defaultdict(list)
for i in range(1, n):
    if w[i] == 0x0000000C:
        p = w[i-1]
        if p >> 16 == 0x2403: num = p & 0xFFFF; num = num-0x10000 if num & 0x8000 else num
        else: num = None
        a = BASE+i*4
        sc[num] += 1; scaddr[num].append(a)
print("syscall stubs:", len(scaddr), "numbers")
print(" ", " ".join(f"{k:#x}@{scaddr[k][0]:X}" if k is not None else f"?@{scaddr[k][0]:X}" for k in sorted(scaddr, key=lambda k: (k is None, abs(k) if k else 0, k or 0))))
# code-like words outside .text
jr = [BASE+i*4 for i in range(nt, n) if w[i] == 0x03E00008]
print("jr $ra outside .text:", len(jr), [hex(a) for a in jr[:20]])
# jal targets outside .text
out = collections.Counter()
for i in range(nt):
    x = w[i]
    if x >> 26 == 3:
        t = (x & 0x3FFFFFF) << 2
        if not (BASE <= t < TEND): out[t] += 1
print("jal targets outside .text:", {hex(k): v for k, v in sorted(out.items())})
# jalr count, jr non-ra
print("jalr:", sum(1 for x in w[:nt] if x & 0xFC1F07FF == 0x00000009), " jr reg!=ra:", sum(1 for x in w[:nt] if x & 0xFC1FFFFF == 0x00000008 and (x>>21)&31 != 31))
open(sys.argv[2], 'w').write("".join(f"{s:08X}\t{e-s:#x}\t{l}\n" for s, e, l in rows))
