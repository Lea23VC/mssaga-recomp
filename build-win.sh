#!/bin/sh
# The Windows port (build-win/mssaga.exe), cross-compiled with MinGW-w64 from WSL/Linux.
#
#   ./build.sh          first: it writes the translated C (build/recomp, build/dll/c) and the shaders (build/shaders)
#   tools/win_deps.sh   once: SDL3 for MinGW into tools/win/
#   ./build-win.sh      -> build-win/mssaga.exe, SDL3.dll, disc/SLUS_212.70 (a copy), mssaga.bat
#
# Needs x86_64-w64-mingw32-gcc-posix (sudo apt install mingw-w64). build-win/ and tools/win/ stay out of git:
# build-win/disc/ holds a copy of the executable from your disc. Run mssaga.bat from Windows: it passes the ISO's
# Windows path (edit it if yours is elsewhere); mssaga.exe alone asks for the disc image once and remembers it.
# Under Windows the GPU renderer uses the real graphics card.
set -eu
cd "$(dirname "$0")"

CC=x86_64-w64-mingw32-gcc-posix
SDL=tools/win/SDL3-3.2.30/x86_64-w64-mingw32
command -v $CC >/dev/null || { echo "missing $CC: sudo apt install mingw-w64"; exit 1; }
[ -d "$SDL" ] || { echo "missing $SDL: run tools/win_deps.sh"; exit 1; }
[ -f build/recomp/functab.c ] && [ -f build/dll/c/dlls.c ] && [ -f build/shaders/shaders.h ] || {
    echo "run ./build.sh first (it writes the translated C and the shaders)"; exit 1; }

FLAGS="-fno-strict-aliasing -ffp-contract=off -march=x86-64-v2 -D_FILE_OFFSET_BITS=64"
INC="-Iport/runtime -Iport/kernel -Iport/iop -Iport/gs -Ibuild/recomp -Ibuild/dll/c -Ibuild/shaders -I$SDL/include"
mkdir -p build-win/obj build-win/disc

# the translated code: -O1, no warnings (generated); the port: -O2
compile() {   # opt, files...
    opt=$1; shift
    for src in "$@"; do echo "$src"; done | xargs -P"$(nproc)" -I{} sh -c \
        'o=build-win/obj/$(echo {} | tr / _ | sed "s/\.c$/.o/"); [ "$o" -nt {} ] || '"$CC"' -c '"$opt $FLAGS $INC"' -w {} -o "$o" || { echo "FAILED: {}"; exit 255; }'
}
compile -O1 build/recomp/*.c build/dll/c/*.c
compile -O2 port/*.c port/runtime/*.c port/kernel/*.c port/iop/*.c port/hle/*.c port/gs/*.c

# console program for now (the log shows in its window); -mwindows would hide it
$CC -static-libgcc -o build-win/mssaga.exe build-win/obj/*.o -L$SDL/lib -lSDL3 \
    -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic -lm
cp "$SDL/bin/SDL3.dll" build-win/
install -m 644 disc/SLUS_212.70 build-win/disc/SLUS_212.70   # a writable copy (the original is read-only)
cat > build-win/mssaga.bat <<'EOF'
@echo off
rem MS Saga: A New Dawn, native port. Optional: mssaga.exe alone asks for the disc image once and remembers it.
rem pushd, not cd: cd can't enter a \\wsl.localhost\... folder (pushd maps it to a drive letter)
pushd "%~dp0"
mssaga.exe --iso "G:\Sistema\Usuarios\Leandro\Descargas\MS Saga - A New Dawn (USA)\MS Saga - A New Dawn (USA).iso" %*
popd
pause
EOF
echo "built build-win/mssaga.exe (run build-win\\mssaga.bat from Windows)"
