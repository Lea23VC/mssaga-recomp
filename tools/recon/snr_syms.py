#!/usr/bin/env python3
"""Dump the SN Systems SNR1 symbol table of the main executable (.sndata): imports, then
<address> <info> <name> for every exported symbol into <out.txt> (keep it out of git).
Usage: snr_syms.py <SLUS_212.70> <out.txt>"""
import sys, struct, collections, bisect
raw = open(sys.argv[1],'rb').read(); O = lambda a: 0x1000+a-0x100000
mg, rel, nrel, sym, nsym = struct.unpack_from('<4sIIII', raw, O(0x385080))
def cs(a): o = O(a); return raw[o:raw.index(b'\0', o)].decode()
syms = []
for i in range(nsym):
    np, val, info = struct.unpack_from('<III', raw, O(sym)+i*12)
    syms.append((val, cs(np), info))
rels = []
for i in range(nrel):
    a, ti, _ = struct.unpack_from('<III', raw, O(rel)+i*12)
    rels.append((a, ti & 0xFF, syms[ti >> 8][1] if (ti>>8) < nsym else ti>>8))
print("relocs (imports):", nrel); [print(f"  {a:08X} type {t} -> {s}") for a, t, s in rels]
info = collections.Counter((x[2] & 0xFF) for x in syms)
print("symbols:", nsym, "low byte of info:", info.most_common(8))
reg = [(0x100000,0x285290,'.text'),(0x285300,0x385080,'.data'),(0x385080,0x3B3380,'.sndata'),(0x3B3380,0x3C8880,'.rodata'),(0x3C8880,0x3C9680,'.lit4/.sdata'),(0x3C9680,0x726A00,'.sbss/.bss')]
c = collections.Counter()
for v, n, _ in syms:
    c[next((r[2] for r in reg if r[0] <= v < r[1]), 'other:'+hex(v))] += 1
print("by section:", dict(c))
with open(sys.argv[2], 'w') as f:
    for v, n, i in sorted(syms): f.write(f"{v:08X} {i:08X} {n}\n")
