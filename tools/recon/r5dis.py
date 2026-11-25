#!/usr/bin/env python3
"""Disassemble R5900 code from the ELF: r5dis.py <SLUS_212.70> <hex address> <count>. Needs rabbitizer (.venv)."""
import sys, struct, rabbitizer as R
R.config.pseudos_enablePseudos = False
raw = open(sys.argv[1],'rb').read()
OFF, BASE = 0x1000, 0x100000
a0, n = int(sys.argv[2],16), int(sys.argv[3])
for i in range(n):
    a = a0 + 4*i
    w = struct.unpack_from('<I', raw, OFF + a - BASE)[0]
    ins = R.Instruction(w, vram=a, category=R.InstrCategory.R5900)
    print(f"{a:08X}: {w:08X}  {ins.disassemble()}")
