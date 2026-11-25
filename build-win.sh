#!/bin/sh
# The Windows port (build-win/mssaga.exe), cross-compiled with MinGW-w64 from WSL/Linux.
#
#   ./build.sh          first: it writes the translated C (build/recomp, build/dll/c) and the shaders (build/shaders)
#   tools/win_deps.sh   once: SDL3 for MinGW into tools/win/
#   ./build-win.sh      -> build-win/mssaga.exe (with its icon), SDL3.dll, FFmpeg's DLLs, README.txt
#                          (docs/windows-README.txt), recomplauncher.json, licenses/: nothing from the game, it can
#                          be shared. tools/package-win.sh zips it for a release.
#
# Needs MinGW-w64 with POSIX threads (CachyOS/Arch: pacman -S mingw-w64-gcc; Ubuntu: apt install mingw-w64). build-win/ and tools/win/ stay out of git:
# mssaga.exe asks for the disc image once and remembers it, and copies the game's executable out of it; it has no console window (its log: %APPDATA%\mssaga\mssaga.log, or the
# command prompt it was started from).
# Under Windows the GPU renderer uses the real graphics card.
set -eu
cd "$(dirname "$0")"

CC=x86_64-w64-mingw32-gcc-posix                 # Ubuntu's name; Arch's plain one is POSIX threads already
command -v $CC >/dev/null || CC=x86_64-w64-mingw32-gcc
SDL=tools/win/SDL3-3.2.30/x86_64-w64-mingw32
command -v $CC >/dev/null || { echo "missing MinGW-w64: pacman -S mingw-w64-gcc (or apt install mingw-w64)"; exit 1; }
[ -d "$SDL" ] || { echo "missing $SDL: run tools/win_deps.sh"; exit 1; }
[ -f build/recomp/functab.c ] && [ -f build/dll/c/dlls.c ] && [ -f build/shaders/shaders.h ] && [ -f build/vu/vu_builtin.c ] || {
    echo "run ./build.sh first (it writes the translated C and the shaders)"; exit 1; }

FLAGS="-fno-strict-aliasing -ffp-contract=off -march=x86-64-v2 -D_FILE_OFFSET_BITS=64 -DMSSAGA_SDL3"   # sound through SDL
INC="-Iport/runtime -Iport/kernel -Iport/iop -Iport/gs -Ibuild/recomp -Ibuild/dll/c -Ibuild/shaders -I$SDL/include"
# FFmpeg plays the movies (port/hle/movie.c); without it they are skipped
FF=$(ls -d tools/win/ffmpeg-*-win64-lgpl-shared-* 2>/dev/null | head -1)
FF_LIBS=""
if [ -n "$FF" ]; then
    FLAGS="$FLAGS -DMSSAGA_FFMPEG"
    INC="$INC -I$FF/include"
    FF_LIBS="-L$FF/lib -lavformat -lavcodec -lavutil -lswscale -lswresample"
else
    echo "no FFmpeg in tools/win (tools/win_deps.sh): the movies are skipped"
fi
mkdir -p build-win/obj

# the translated code: -O1, no warnings (generated); the port: -O2
compile() {   # opt, files...
    opt=$1; shift
    for src in "$@"; do echo "$src"; done | xargs -P"$(nproc)" -I{} sh -c \
        'o=build-win/obj/$(echo {} | tr / _ | sed "s/\.c$/.o/"); [ "$o" -nt {} ] || '"$CC"' -c '"$opt $FLAGS $INC"' -w {} -o "$o" || { echo "FAILED: {}"; exit 255; }'
}
compile -O1 build/recomp/*.c build/dll/c/*.c
compile -O2 build/vu/*.c                       # the game's VU1 programs (build.sh writes them; none at run time here)
compile -O2 port/*.c port/runtime/*.c port/kernel/*.c port/iop/*.c port/hle/*.c port/gs/*.c port/iop/lle/*.c

# the icon (tools/make_icon.py draws it), as a Windows resource
python3 -I tools/make_icon.py build-win/obj/mssaga.ico
printf '1 ICON "mssaga.ico"\n' > build-win/obj/mssaga.rc
(cd build-win/obj && x86_64-w64-mingw32-windres mssaga.rc -O coff -o mssaga_rc.res)

# a window program (no console of its own)
$CC -static-libgcc -mwindows -o build-win/mssaga.exe build-win/obj/*.o build-win/obj/mssaga_rc.res -L$SDL/lib -lSDL3 $FF_LIBS \
    -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic -lm
cp "$SDL/bin/SDL3.dll" build-win/
[ -z "$FF" ] || for l in avformat avcodec avutil swscale swresample; do cp "$FF"/bin/$l-*.dll build-win/; done
rm -rf build-win/disc                         # nothing from the game: mssaga.exe takes SLUS_212.70 from the ISO
rm -f build-win/mssaga.bat                     # (older builds: it held one person's ISO path)
cp docs/windows-README.txt build-win/README.txt
cp recomplauncher.json build-win/              # RecompLauncher reads it from the top of the release zip
mkdir -p build-win/licenses
cp licenses/SaintSeiyaRecomp-MIT.txt build-win/licenses/
cp tools/win/SDL3-3.2.30/LICENSE.txt build-win/licenses/SDL3-zlib.txt
[ -z "$FF" ] || cp "$FF"/LICENSE.txt build-win/licenses/FFmpeg-LGPL.txt
echo "built build-win/mssaga.exe"
