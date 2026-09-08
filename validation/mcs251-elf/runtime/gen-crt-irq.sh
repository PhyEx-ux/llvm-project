#!/usr/bin/env bash
# gen-crt-irq.sh - build the pure ELF MCS251 IRQ-mode crt object (T08, ISR
# campaign) from the hand-authored YAML fixture crt-irq.yaml with a
# caller-supplied frozen yaml2obj, then smoke-check it with a caller-supplied
# llvm-readobj. Full acceptance is check-crt-irq.py + lld/test/MCS251/
# isr-crt.test.
#
# LLVM has no MCS251 AsmParser, so the YAML -> yaml2obj route is the only way
# to produce a pure ELF crt without touching llvm/clang sources (SPEC.md 9.3).
# The old crt-selfstart assets stay frozen and untouched; this script never
# reads or rewrites them.
#
# No default tool paths and no default output: this generator refuses to run
# without explicit arguments and never writes into the repository on its own
# (T08 card step 12; cf. the old gen-crt-elf.sh defaulting to ./crt.o).
#
# Usage (WSL Debian):
#   gen-crt-irq.sh --yaml2obj PATH --readobj PATH --out OUT.o [--src YAML]
#
# Example:
#   /mnt/c/Prj/LLVM/MCS251/validation/mcs251-elf/runtime/gen-crt-irq.sh \
#     --yaml2obj /home/liu/build-mcs251-isr-link/bin/yaml2obj \
#     --readobj /home/liu/build-mcs251-isr-link/bin/llvm-readobj \
#     --out /tmp/crt-irq.o
set -eu
DIR="$(cd "$(dirname "$0")" && pwd)"
SRC="$DIR/crt-irq.yaml"
YAML2OBJ=""
READOBJ=""
OUT=""

usage() {
  echo "usage: gen-crt-irq.sh --yaml2obj PATH --readobj PATH --out OUT.o [--src YAML]" >&2
}

while [ $# -gt 0 ]; do
  case "$1" in
    --yaml2obj) [ $# -ge 2 ] || { usage; exit 2; }; YAML2OBJ="$2"; shift 2 ;;
    --readobj)  [ $# -ge 2 ] || { usage; exit 2; }; READOBJ="$2"; shift 2 ;;
    --out)      [ $# -ge 2 ] || { usage; exit 2; }; OUT="$2"; shift 2 ;;
    --src)      [ $# -ge 2 ] || { usage; exit 2; }; SRC="$2"; shift 2 ;;
    *) echo "gen-crt-irq.sh: unknown argument: $1" >&2; usage; exit 2 ;;
  esac
done

[ -n "$YAML2OBJ" ] && [ -n "$READOBJ" ] && [ -n "$OUT" ] || {
  echo "gen-crt-irq.sh: --yaml2obj, --readobj and --out are all required" >&2
  usage
  exit 2
}
command -v "$YAML2OBJ" >/dev/null || { echo "yaml2obj missing: $YAML2OBJ" >&2; exit 1; }
command -v "$READOBJ" >/dev/null || { echo "llvm-readobj missing: $READOBJ" >&2; exit 1; }
[ -f "$SRC" ] || { echo "fixture missing: $SRC" >&2; exit 1; }
OUT_DIR="$(dirname "$OUT")"
[ -d "$OUT_DIR" ] || {
  echo "gen-crt-irq.sh: output directory does not exist (not created silently): $OUT_DIR" >&2
  exit 1
}
[ ! -e "$OUT" ] || [ -f "$OUT" ] || { echo "output is not a regular file: $OUT" >&2; exit 1; }

"$YAML2OBJ" "$SRC" -o "$OUT"

# Acceptance smoke (full fingerprint: llvm-readobj --file-headers --sections
# --symbols --relocations "$OUT"). All three frozen header fingerprints are
# asserted individually - every one must match, not just any one of them:
#   "Type: Relocatable"   ET_REL object identity (SPEC v1 inputs are ET_REL)
#   "Machine: EM_MCS251"  MCS251 machine
#   "EF_MCS251_ABI_V1"    original v1 ABI flag (A3.1)
HDR="$("$READOBJ" --file-headers "$OUT")"
printf '%s\n' "$HDR" | grep -q "Type: Relocatable" \
  || { echo "header fingerprint mismatch: Type is not Relocatable (ET_REL)" >&2; exit 1; }
printf '%s\n' "$HDR" | grep -q "Machine: EM_MCS251" \
  || { echo "header fingerprint mismatch: Machine is not EM_MCS251" >&2; exit 1; }
printf '%s\n' "$HDR" | grep -q "EF_MCS251_ABI_V1" \
  || { echo "header fingerprint mismatch: EF_MCS251_ABI_V1 missing" >&2; exit 1; }
printf 'generated: %s (%s bytes)\n' "$OUT" "$(wc -c < "$OUT")"
