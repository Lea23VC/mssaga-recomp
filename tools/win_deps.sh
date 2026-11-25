#!/bin/sh
# What the Windows build (build-win.sh) needs, into tools/win/ (not kept in git): SDL3's MinGW development release.
# The reference (SaintSeiyaRecomp tools/win_deps.sh) also gets FFmpeg and FreeType; this port doesn't use them yet.
set -eu
cd "$(dirname "$0")"
mkdir -p win/dl
cd win
SDL=3.2.30
[ -s "dl/SDL3-devel-$SDL-mingw.tar.gz" ] ||
    curl -fsSL -o "dl/SDL3-devel-$SDL-mingw.tar.gz" \
        "https://github.com/libsdl-org/SDL/releases/download/release-$SDL/SDL3-devel-$SDL-mingw.tar.gz"
[ -d "SDL3-$SDL" ] || tar xzf "dl/SDL3-devel-$SDL-mingw.tar.gz"
echo "tools/win/SDL3-$SDL ready"
