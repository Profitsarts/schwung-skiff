#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Host tests:
#   1. Skiff's own regression suite on the vendored sources: every engine x factory preset rendered and hashed
#      against Skiff's tests/golden.txt (the sound is Skiff's, bit for bit), plus its voice / health checks;
#   2. the plugin as Schwung drives it (tests/test_host.c), its JSON checked by python3.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
echo "==> Skiff regression suite (vendor/skiff/tests/regress.c)"
cc -O2 -w -Ivendor/skiff/gen -Ivendor/skiff/firmware/src -o build/regress vendor/skiff/tests/regress.c -lm
build/regress vendor/skiff/tests/golden.txt vendor/skiff/tests/cpu_baseline.txt | tail -n 1
echo "==> plugin"
CC=cc OUT=build/host scripts/compile.sh
cc -O2 -std=gnu11 -Wall -Wextra -Werror -Isrc -Isrc/dsp build/host/copy*.o build/host/cores.o build/host/plugin.o \
    tests/test_host.c -o build/test_host -lm
build/test_host build
for f in build/ui_hierarchy.json build/chain_params.json; do
    python3 -m json.tool "$f" > /dev/null || { echo "invalid JSON: $f"; exit 1; }
done
echo "==> JSON ok"
