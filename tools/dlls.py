#!/usr/bin/env python3
"""Extract the game's DLLs from the disc image and decompress them, for the recompiler.

    tools/dlls.py disc/game.iso build/dll/bin

The 11 `*_dll.cmp` entries of AFSLIST0.AFS (docs/recon.md) are SD0-compressed SN Systems DLLs; each is written
decompressed as <out>/<name>.bin (title_dll.cmp -> title.bin). Game data: the output stays in build/.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "recon"))
from sd0 import sd0  # noqa: E402

S = 2048


def afs_entries(f):
    rd = lambda lsn, n: (f.seek(lsn * S), f.read(n))[1]
    root = rd(16, S)[156:190]
    d = rd(struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0])
    i = 0
    while i < len(d):
        if d[i] == 0:
            i = (i // S + 1) * S
            continue
        nl = d[i + 32]
        if d[i + 33:i + 33 + nl] == b"AFSLIST0.AFS;1":
            base = struct.unpack_from("<I", d, i + 2)[0] * S
            break
        i += d[i]
    else:
        raise SystemExit("AFSLIST0.AFS not on the disc")
    f.seek(base)
    magic, n = struct.unpack("<4sI", f.read(8))
    assert magic == b"AFS\0"
    ents = [struct.unpack("<II", f.read(8)) for _ in range(n)]
    nt_off, _ = struct.unpack("<II", f.read(8))
    f.seek(base + nt_off)
    nt = f.read(n * 48)
    for j, (off, size) in enumerate(ents):
        yield nt[j * 48:j * 48 + 32].split(b"\0")[0].decode(errors="replace"), base + off, size


def main():
    iso, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    with open(iso, "rb") as f:
        for name, at, size in afs_entries(f):
            if not name.endswith("_dll.cmp"):
                continue
            f.seek(at)
            data, _ = sd0(f.read(size))
            path = os.path.join(out, name[:-len("_dll.cmp")] + ".bin")
            open(path, "wb").write(data)
            print(f"{name:16} {size:#9x} -> {len(data):#9x} {path}")


main()
