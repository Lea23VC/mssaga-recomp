#!/bin/sh
# What the Linux release build (build-linux.sh) needs, into tools/linux/ (not kept in git): SDL3's source (built in
# the container) and FFmpeg (BtbN's LGPL shared build for Linux, the same as the Windows release; for the movies).
set -eu
cd "$(dirname "$0")"
mkdir -p linux/dl
cd linux
SDL=3.2.30
[ -s "dl/SDL3-$SDL.tar.gz" ] ||
    curl -fsSL -o "dl/SDL3-$SDL.tar.gz" "https://github.com/libsdl-org/SDL/releases/download/release-$SDL/SDL3-$SDL.tar.gz"
[ -d "SDL3-$SDL" ] || tar xzf "dl/SDL3-$SDL.tar.gz"
[ -s dl/ffmpeg-linux64.tar.xz ] ||
    curl -fsSL -o dl/ffmpeg-linux64.tar.xz \
        "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-n8.1-latest-linux64-lgpl-shared-8.1.tar.xz"
ls -d ffmpeg-*-linux64-lgpl-shared-* >/dev/null 2>&1 || tar xJf dl/ffmpeg-linux64.tar.xz
echo "tools/linux ready: SDL3 $SDL, $(ls -d ffmpeg-*-linux64-lgpl-shared-*)"
