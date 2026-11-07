#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Compiles Skiff Synth's objects with the compiler in $CC into $OUT (both required):
#   SKC_COPIES copies of src/dsp/skiff_core.c (-DSKC_ID=n), each with every symbol made local except its own
#   skc<n>_* entry points (and copy 0's shared skiff_* tables), so the copies' statics and Skiff's few global
#   symbols never meet; then the copy table and the plugin.
# Used by scripts/test.sh (host cc) and scripts/build.sh (aarch64).
set -euo pipefail
cd "$(dirname "$0")/.."
: "${CC:?}" "${OUT:?}"
OBJCOPY="${OBJCOPY:-objcopy}"
COPIES=8
CFLAGS="${CFLAGS:--O2} -std=gnu11 -fPIC -fvisibility=hidden -Isrc -Isrc/dsp -Ivendor/skiff/gen -Ivendor/skiff/firmware/src"
mkdir -p "$OUT"
pids=()
for n in $(seq 0 $((COPIES - 1))); do
    (
        # Skiff's own sources: built as Skiff builds them for its host tests (-w: its code, not ours)
        # shellcheck disable=SC2086
        $CC $CFLAGS -w -DSKC_ID="$n" -c src/dsp/skiff_core.c -o "$OUT/copy$n.raw.o"
        "$OBJCOPY" -w --keep-global-symbol="skc${n}_*" --keep-global-symbol='skiff_*' \
            "$OUT/copy$n.raw.o" "$OUT/copy$n.o"
        rm -f "$OUT/copy$n.raw.o"
    ) &
    pids+=($!)
done
for p in "${pids[@]}"; do wait "$p"; done
# shellcheck disable=SC2086
$CC $CFLAGS -Wall -Wextra -Werror -c src/dsp/skiff_cores.c -o "$OUT/cores.o"
# shellcheck disable=SC2086
$CC $CFLAGS -Wall -Wextra -Werror -c src/dsp/skiff_plugin.c -o "$OUT/plugin.o"
