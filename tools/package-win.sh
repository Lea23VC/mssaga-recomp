#!/bin/sh
# The Windows release zip from build-win/ (./build-win.sh first): build/mssaga-recomp-<version>-windows-x64.zip,
# with the folder's contents at the top (mssaga.exe, its DLLs, README.txt, recomplauncher.json, licenses/) and
# without the build's objects. Nothing from the game is in build-win/.
#
#   tools/package-win.sh [version]      version: the git tag by default (v1.2.0), else the commit
set -eu
cd "$(dirname "$0")/.."
VERSION=${1:-$(git describe --tags --always)}
[ -f build-win/mssaga.exe ] && [ -f build-win/recomplauncher.json ] || { echo "run ./build-win.sh first"; exit 1; }
[ ! -d build-win/disc ] || { echo "build-win/disc holds game files: run ./build-win.sh again"; exit 1; }
OUT=build/mssaga-recomp-$VERSION-windows-x64.zip
python3 -I - "$OUT" <<'PY'
import os, sys, zipfile
with zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED) as z:
    for root, dirs, files in os.walk("build-win"):
        dirs[:] = sorted(d for d in dirs if d != "obj")
        for f in sorted(files):
            p = os.path.join(root, f)
            z.write(p, os.path.relpath(p, "build-win"))
PY
echo "$OUT"
