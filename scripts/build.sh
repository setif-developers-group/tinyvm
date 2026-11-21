#!/usr/bin/env bash
# Build TinyVM components.
#   scripts/build.sh [all|c|cpp|web|gui|clean]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
CC="${CC:-gcc}"
CXX="${CXX:-g++}"
OPT="${OPT:--O2}"
WARN="-Wall -Wextra -pedantic"

PIC="-fPIC"; DLL_DEF=""
case "$(uname -s)" in
  Darwin)               LIB_NAME="libvm.dylib" ;;
  MINGW*|MSYS*|CYGWIN*) LIB_NAME="vm.dll"; PIC=""; DLL_DEF="-DVM_BUILD_DLL" ;;
  *)                    LIB_NAME="libvm.so" ;;
esac

log() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
need() { command -v "$1" >/dev/null 2>&1 || { echo "error: '$1' not found (needed for: $2)" >&2; exit 1; }; }

build_c() {
  need "$CC" "C engine"
  mkdir -p "$BUILD"
  log "C: vmrun (CLI runner)"
  "$CC" -std=c11 $WARN $OPT "$ROOT/src/c/main.c" "$ROOT/src/c/vm.c" -I"$ROOT/src/c" -o "$BUILD/vmrun"
  log "C: $LIB_NAME (shared library for Python / C# / FFI)"
  "$CC" -std=c11 $WARN $OPT $PIC $DLL_DEF -shared "$ROOT/src/c/vm.c" -I"$ROOT/src/c" -o "$BUILD/$LIB_NAME"
}

build_cpp() {
  need "$CXX" "C++ monitor"
  need "$CC" "C engine"
  mkdir -p "$BUILD"
  log "C++: vmtool (interactive monitor)"
  "$CC" -std=c11 $WARN $OPT -c "$ROOT/src/c/vm.c" -I"$ROOT/src/c" -o "$BUILD/vm.o"
  "$CXX" -std=c++17 $WARN $OPT "$ROOT/src/cpp/vmtool.cpp" "$BUILD/vm.o" -I"$ROOT/src/c" -o "$BUILD/vmtool"
}

build_web() {
  log "JavaScript: bundling examples for the web demo"
  "$ROOT/scripts/gen_web_examples.sh"
}

build_gui() {
  need dotnet "C# dashboard (requires the .NET 8 SDK on Windows)"
  log "C#: VmDashboard (WinForms)"
  [ -f "$BUILD/vm.dll" ] || { echo "warning: build/vm.dll missing - build the C library with MinGW first" >&2; }
  dotnet build "$ROOT/gui/VmDashboard" -c Release
}

clean() { log "cleaning"; rm -rf "$BUILD" "$ROOT"/gui/VmDashboard/{bin,obj}; }

case "${1:-all}" in
  c)     build_c ;;
  cpp)   build_cpp ;;
  web)   build_web ;;
  gui)   build_gui ;;
  clean) clean ;;
  all)   build_c; build_cpp; build_web
         if command -v dotnet >/dev/null 2>&1 && [[ "$(uname -s)" == MINGW* || "$(uname -s)" == MSYS* ]]; then build_gui; fi
         log "done -> $BUILD" ;;
  *)     echo "usage: $0 [all|c|cpp|web|gui|clean]" >&2; exit 2 ;;
esac
