#!/bin/sh
# The Linux release (build-linux/mssaga), built in a container (tools/linux/Dockerfile: Ubuntu 22.04, glibc 2.35)
# so it runs on most distros, not only on the one it was built on. ./build.sh is the build for this machine.
#
#   ./build.sh            first: it writes the translated C (build/recomp, build/dll/c) and the shaders (build/shaders)
#   tools/linux_deps.sh   once: SDL3's source and FFmpeg into tools/linux/
#   ./build-linux.sh      -> build-linux/: mssaga, lib/ (SDL3, FFmpeg), README.txt (docs/linux-README.txt),
#                            recomplauncher.json, licenses/: nothing from the game. tools/package-linux.sh makes the
#                            release's .tar.gz.
#
# Needs docker (or podman: MSSAGA_DOCKER=podman). The program finds its libraries in lib/ next to it (rpath $ORIGIN/lib).
set -eu
cd "$(dirname "$0")"
SDLV=3.4.20
FF=$(ls -d tools/linux/ffmpeg-*-linux64-lgpl-shared-* 2>/dev/null | head -1)
[ -d "tools/linux/SDL3-$SDLV" ] && [ -n "$FF" ] || { echo "missing tools/linux: run tools/linux_deps.sh"; exit 1; }
[ -f build/recomp/functab.c ] && [ -f build/dll/c/dlls.c ] && [ -f build/shaders/shaders.h ] && [ -f build/vu/vu_builtin.c ] || {
    echo "run ./build.sh first (it writes the translated C and the shaders)"; exit 1; }

if [ -z "${MSSAGA_IN_BUILDER:-}" ]; then         # on the host: into the container
    D=${MSSAGA_DOCKER:-docker}
    $D build -q -t mssaga-linux-builder - < tools/linux/Dockerfile >/dev/null
    exec $D run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -w /src -e HOME=/tmp \
        --entrypoint sh mssaga-linux-builder -c "MSSAGA_IN_BUILDER=1 sh ./build-linux.sh"
fi

# SDL3, once (tools/linux/sdl/: its build and install)
SDL=tools/linux/sdl/install
if [ ! -f "$SDL/lib/libSDL3.so.0" ]; then
    cmake -S "tools/linux/SDL3-$SDLV" -B tools/linux/sdl/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$PWD/$SDL" -DCMAKE_INSTALL_LIBDIR=lib -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF \
        -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF >/dev/null
    cmake --build tools/linux/sdl/build >/dev/null
    cmake --install tools/linux/sdl/build >/dev/null
fi

FLAGS="-fno-strict-aliasing -ffp-contract=off -march=x86-64-v2 -D_FILE_OFFSET_BITS=64 -DMSSAGA_SDL3 -DMSSAGA_FFMPEG -DMSSAGA_LINUX_RELEASE"
INC="-Iport/runtime -Iport/kernel -Iport/iop -Iport/gs -Ibuild/recomp -Ibuild/dll/c -Ibuild/shaders -I$SDL/include -I$FF/include"
mkdir -p build-linux/obj build-linux/lib

# the translated code: -O1, no warnings (generated); the port: -O2
compile() {   # opt, files...
    opt=$1; shift
    for src in "$@"; do echo "$src"; done | xargs -P"$(nproc)" -I{} sh -c \
        'o=build-linux/obj/$(echo {} | tr / _ | sed "s/\.c$/.o/"); [ "$o" -nt {} ] || gcc -c '"$opt $FLAGS $INC"' -w {} -o "$o" || { echo "FAILED: {}"; exit 255; }'
}
compile -O1 build/recomp/*.c build/dll/c/*.c
compile -O2 build/vu/*.c
compile -O2 port/*.c port/runtime/*.c port/kernel/*.c port/iop/*.c port/hle/*.c port/gs/*.c port/iop/lle/*.c
for src in port/gs/*.cpp port/third_party/imgui/*.cpp; do echo "$src"; done | xargs -P"$(nproc)" -I{} sh -c \
    'o=build-linux/obj/$(echo {} | tr / _ | sed "s/\.cpp$/.o/"); [ "$o" -nt {} ] || g++ -c -O2 -std=c++17 -w -I'"$SDL"'/include -Iport/third_party/imgui {} -o "$o" || { echo "FAILED: {}"; exit 255; }'

g++ -rdynamic -static-libgcc -static-libstdc++ -o build-linux/mssaga build-linux/obj/*.o \
    -L$SDL/lib -lSDL3 -L$FF/lib -lavformat -lavcodec -lavutil -lswscale -lswresample \
    -Wl,-rpath,'$ORIGIN/lib' -Wl,--disable-new-dtags -lpthread -lm -ldl
strip --strip-debug build-linux/mssaga
cp -L "$SDL/lib/libSDL3.so.0" build-linux/lib/
for l in avformat avcodec avutil swscale swresample; do cp -L "$FF"/lib/lib$l.so.[0-9]* build-linux/lib/ 2>/dev/null || true; done
# only the sonames the program asks for (lib*.so.62), not the full-version copies
for f in build-linux/lib/*.so.*.*; do [ -e "$f" ] && rm -f "$f"; done
for f in build-linux/lib/*.so.*; do patchelf --set-rpath '$ORIGIN' "$f"; done
rm -rf build-linux/disc                          # nothing from the game: mssaga takes SLUS_212.70 from the ISO
cp docs/linux-README.txt build-linux/README.txt
cp recomplauncher.json build-linux/
mkdir -p build-linux/licenses
cp licenses/*.txt build-linux/licenses/
cp "tools/linux/SDL3-$SDLV/LICENSE.txt" build-linux/licenses/SDL3-zlib.txt
cp "$FF"/LICENSE.txt build-linux/licenses/FFmpeg-LGPL.txt
echo "built build-linux/mssaga"
