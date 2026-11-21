#!/usr/bin/env bash
# Build the engine, then run every available test suite.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
status=0

"$ROOT/scripts/build.sh" c

echo; echo "==> Python: unit tests (assembler, VM, example programs)"
(cd "$ROOT/tests" && python3 -m unittest discover -v -s . 2>&1 | tail -n 15) || status=1

echo; echo "==> JavaScript: web VM tests"
if command -v node >/dev/null 2>&1; then
  node "$ROOT/web/test.js" || status=1
else
  echo "node not found - skipping"
fi

echo; echo "==> CLI smoke test (C runner vs. expected output)"
for asm in "$ROOT"/examples/*.asm; do
  exp="${asm%.asm}.out"
  [ -f "$exp" ] || continue
  if diff -q <("$ROOT/scripts/run.sh" "$asm") "$exp" >/dev/null; then
    echo "  ok   $(basename "$asm")"
  else
    echo "  FAIL $(basename "$asm")"; status=1
  fi
done

[ $status -eq 0 ] && echo -e "\nAll checks passed." || echo -e "\nSome checks failed."
exit $status
