#!/usr/bin/env bash
# gen-crt-elf.sh - build the pure ELF MCS251 crt object from the hand-authored
# YAML fixture (crt-selfstart.yaml) with the frozen yaml2obj, then smoke-check
# it with the frozen llvm-readobj.
#
# LLVM has no MCS251 AsmParser, so this YAML->yaml2obj route is the only way to
# produce a pure ELF crt without touching llvm/clang sources (SPEC.md 9.3).
#
# Usage (WSL Debian, clean PATH like the e4 harness):
#   /mnt/c/Prj/LLVM/MCS251/validation/mcs251-elf/runtime/gen-crt-elf.sh [out.o]
# Environment overrides: YAML2OBJ, READOBJ.
set -eu
DIR="$(cd "$(dirname "$0")" && pwd)"
YAML2OBJ="${YAML2OBJ:-/home/liu/build-mcs251/bin/yaml2obj}"
READOBJ="${READOBJ:-/home/liu/build-mcs251/bin/llvm-readobj}"
SRC="$DIR/crt-selfstart.yaml"
OUT="${1:-$DIR/crt.o}"

command -v "$YAML2OBJ" >/dev/null || { echo "yaml2obj missing: $YAML2OBJ" >&2; exit 1; }
command -v "$READOBJ" >/dev/null || { echo "llvm-readobj missing: $READOBJ" >&2; exit 1; }

"$YAML2OBJ" "$SRC" -o "$OUT"

# Acceptance smoke (full fingerprint: llvm-readobj --file-headers --sections
# --symbols --relocations "$OUT"):
"$READOBJ" --file-headers "$OUT" | grep -E "Type: Relocatable|Machine: EM_MCS251|EF_MCS251_ABI_V1" \
  || { echo "header fingerprint mismatch" >&2; exit 1; }
printf 'generated: %s (%s bytes)\n' "$OUT" "$(wc -c < "$OUT")"
