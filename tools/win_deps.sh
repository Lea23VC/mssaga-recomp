#!/bin/sh
# What the Windows build (build-win.sh) needs, into tools/win/ (not kept in git): SDL3's MinGW development release
# and FFmpeg (BtbN's LGPL shared build, as the reference uses: its DLLs go next to mssaga.exe; for the movies).
set -eu
cd "$(dirname "$0")"
mkdir -p win/dl
cd win
SDL=3.2.30
[ -s "dl/SDL3-devel-$SDL-mingw.tar.gz" ] ||
    curl -fsSL -o "dl/SDL3-devel-$SDL-mingw.tar.gz" \
        "https://github.com/libsdl-org/SDL/releases/download/release-$SDL/SDL3-devel-$SDL-mingw.tar.gz"
[ -d "SDL3-$SDL" ] || tar xzf "dl/SDL3-devel-$SDL-mingw.tar.gz"
[ -s dl/ffmpeg-win64.zip ] ||
    curl -fsSL -o dl/ffmpeg-win64.zip \
        "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-n8.1-latest-win64-lgpl-shared-8.1.zip"
ls -d ffmpeg-*-win64-lgpl-shared-* >/dev/null 2>&1 || unzip -q dl/ffmpeg-win64.zip
echo "tools/win ready: SDL3 $SDL, $(ls -d ffmpeg-*-win64-lgpl-shared-*)"
