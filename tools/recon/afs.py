#!/usr/bin/env python3
"""List or extract entries of AFSLIST0.AFS straight from the disc image (2048-byte sectors).

    afs.py <game.iso>                     summary: entry count, extensions, first bytes
    afs.py <game.iso> <outdir> name...    copy the named entries (e.g. title_dll.cmp) to <outdir>
Output stays outside git (it is game data).
"""
import collections
import os
import struct
import sys

S = 2048


def main():
    f = open(sys.argv[1], "rb")
    rd = lambda lsn, n: (f.seek(lsn * S), f.read(n))[1]
    root = rd(16, S)[156:190]
    d = rd(struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0])
    files, i = {}, 0
    while i < len(d):
        if d[i] == 0:
            i = (i // S + 1) * S
            continue
        nl = d[i + 32]
        files[d[i + 33:i + 33 + nl].decode(errors="replace")] = struct.unpack_from("<I", d, i + 2)[0]
        i += d[i]
    base = files["AFSLIST0.AFS;1"] * S
    f.seek(base)
    magic, n = struct.unpack("<4sI", f.read(8))
    assert magic == b"AFS\0"
    ents = [struct.unpack("<II", f.read(8)) for _ in range(n)]
    nt_off, _ = struct.unpack("<II", f.read(8))
    f.seek(base + nt_off)
    nt = f.read(n * 48)
    names = [nt[j * 48:j * 48 + 32].split(b"\0")[0].decode(errors="replace") for j in range(n)]
    if len(sys.argv) > 2:
        os.makedirs(sys.argv[2], exist_ok=True)
        for j, nm in enumerate(names):
            if nm in sys.argv[3:]:
                f.seek(base + ents[j][0])
                open(os.path.join(sys.argv[2], nm), "wb").write(f.read(ents[j][1]))
        return
    ext = collections.Counter(nm.rsplit(".", 1)[-1].lower() for nm in names)
    mag = collections.Counter()
    for o, s in ents:
        f.seek(base + o)
        mag[f.read(4)] += 1
    print(f"{n} entries, {sum(s for _, s in ents)} bytes; name table at {nt_off:#x}")
    print("extensions:", ext.most_common())
    print("first 4 bytes:", mag.most_common(8))
    print("first 16 names:", names[:16])


main()
