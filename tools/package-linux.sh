#!/bin/sh
# The Linux release archive from build-linux/ (./build-linux.sh first): build/mssaga-recomp-<version>-linux-x64.tar.gz,
# with the folder's contents at the top (mssaga, lib/, README.txt, recomplauncher.json, licenses/) as in the Windows
# zip, without the build's objects. Nothing from the game is in build-linux/.
#
#   tools/package-linux.sh [version]      version: the git tag by default (v1.2.0), else the commit
set -eu
cd "$(dirname "$0")/.."
VERSION=${1:-$(git describe --tags --always)}
[ -f build-linux/mssaga ] && [ -f build-linux/recomplauncher.json ] || { echo "run ./build-linux.sh first"; exit 1; }
[ ! -d build-linux/disc ] || { echo "build-linux/disc holds game files: run ./build-linux.sh again"; exit 1; }
OUT=build/mssaga-recomp-$VERSION-linux-x64.tar.gz
tar -C build-linux --exclude=./obj --owner=0 --group=0 --sort=name -czf "$OUT" .
echo "$OUT"
