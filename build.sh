#!/bin/sh
# Translate SLUS_212.70 to C and compile it (milestone 1). Needs disc/SLUS_212.70 (see disc/README.md).
#
#   ./build.sh            symbols -> splat -> recompiler -> gcc, everything into build/ and asm/ (both ignored)
#
# Steps: tools/symbols.py writes build/symbol_addrs.txt from the executable's SN symbol table; splat splits
# the ELF with config/slus_212.70.yaml into asm/; tools/recomp/recomp.py writes build/recomp/*.c; gcc
# compiles them into build/obj/. Linking needs the runtime (milestone 2).
set -eu
cd "$(dirname "$0")"

ELF=disc/SLUS_212.70
[ -f "$ELF" ] || { echo "missing $ELF (see disc/README.md)"; exit 1; }
if ! (cd disc && sha1sum -c ../config/SLUS_212.70.sha1 >/dev/null 2>&1); then
    echo "$ELF is not the USA executable this config is for (config/SLUS_212.70.sha1)"; exit 1
fi

if [ ! -x .venv/bin/python ] || ! .venv/bin/python -c 'import splat, rabbitizer' 2>/dev/null; then
    python3 -m venv .venv
    .venv/bin/pip install -q 'splat64[mips]' spimdisasm rabbitizer ninja-syntax pyyaml pyelftools
fi
PY=.venv/bin/python
mkdir -p build/obj

$PY -I tools/symbols.py "$ELF" build/symbol_addrs.txt
rm -rf asm
$PY -m splat split config/slus_212.70.yaml > build/splat.log 2>&1 || { tail -20 build/splat.log; exit 1; }
$PY tools/recomp/recomp.py "$ELF" build/recomp asm/text.s

CFLAGS="-O1 -g -w -fno-strict-aliasing -ffp-contract=off -march=x86-64-v2 -Iport/runtime -Ibuild/recomp"
ls build/recomp/*.c | xargs -P"$(nproc)" -I{} sh -c \
    'o=build/obj/$(basename {} .c).o; gcc -c '"$CFLAGS"' {} -o $o || { echo "FAILED: {}"; exit 255; }'
echo "compiled $(ls build/obj/*.o | wc -l) files into build/obj/"
