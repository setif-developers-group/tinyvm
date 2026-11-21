#!/usr/bin/env bash
# Assemble and run a TinyVM program.
#   scripts/run.sh examples/fibonacci.asm [vmrun options, e.g. -t -r]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[ $# -ge 1 ] || { echo "usage: $0 program.asm [vmrun options]" >&2; exit 2; }
SRC="$1"; shift

[ -x "$ROOT/build/vmrun" ] || "$ROOT/scripts/build.sh" c

BIN="$ROOT/build/$(basename "${SRC%.*}").bin"
python3 "$ROOT/tools/asm.py" "$SRC" -o "$BIN" 2>/dev/null
"$ROOT/build/vmrun" "$@" "$BIN"
