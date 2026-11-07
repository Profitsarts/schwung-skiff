#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Builds Skiff Synth for the Move (aarch64) and packages dist/skiff-module.tar.gz.
# Uses a local aarch64-linux-gnu-gcc when there is one, the native cc on an aarch64 machine (CI runs on GitHub's
# arm64 runners, so it needs no apt), else the Docker image in scripts/Dockerfile.
# The tarball's top-level directory MUST be the module id: Schwung extracts it into modules/sound_generators/.
set -euo pipefail
cd "$(dirname "$0")/.."

ID=skiff
IMAGE=schwung-skiff-builder

link() {   # $1 = compiler
    "$1" -shared -fPIC build/move/copy*.o build/move/cores.o build/move/plugin.o -o dsp.so -lm
}

rm -rf build/move
if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    echo "==> compiling with local aarch64-linux-gnu-gcc"
    CC=aarch64-linux-gnu-gcc OBJCOPY=aarch64-linux-gnu-objcopy OUT=build/move scripts/compile.sh
    link aarch64-linux-gnu-gcc
elif [ "$(uname -m)" = aarch64 ] && command -v cc >/dev/null 2>&1; then
    echo "==> compiling with the native cc (aarch64 host)"
    CC=cc OUT=build/move scripts/compile.sh
    link cc
else
    docker build -q -t "$IMAGE" -f scripts/Dockerfile scripts/ >/dev/null
    echo "==> compiling with aarch64-linux-gnu-gcc (Docker)"
    docker run --rm -v "$PWD":/build -w /build "$IMAGE" bash -c \
        'CC=aarch64-linux-gnu-gcc OBJCOPY=aarch64-linux-gnu-objcopy OUT=build/move scripts/compile.sh &&
         aarch64-linux-gnu-gcc -shared -fPIC build/move/copy*.o build/move/cores.o build/move/plugin.o -o dsp.so -lm'
fi

echo "==> packaging dist/"
rm -rf dist && mkdir -p "dist/$ID"
mv dsp.so "dist/$ID/dsp.so"
cp src/module.json LICENSE "dist/$ID/"
cp vendor/skiff/LICENSING.md "dist/$ID/LICENSING-skiff.md"
cp -r vendor/skiff/LICENSES "dist/$ID/LICENSES"
( cd dist && tar -czf "$ID-module.tar.gz" "$ID/" )
echo "==> done: dist/$ID-module.tar.gz"
