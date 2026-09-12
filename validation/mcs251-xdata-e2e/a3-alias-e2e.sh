#!/usr/bin/env bash
# a3-alias-e2e.sh - A3 alias acceptance chain (RUNTIME-AS-PTR-DESIGN-A.md
# §3-A3 "验收补充一").
#
# Two halves, deliberately separate:
#
#   A. OPTIMIZER HALF: lib/IR probes in
#      llvm/test/CodeGen/MCS251/code-addrspacecast-alias.ll are run through
#      real optimization pipelines (`opt -passes=gvn`,
#      `opt -passes=instcombine,early-cse,gvn,simplifycfg`, `default<O2>`)
#      and the *results* are asserted: a store through the converted view and
#      a read of the same byte through the other view must be forwarded
#      (may-alias), while an unrelated object must not be forwarded.  The lit
#      test already does this; this script re-runs it standalone so the same
#      evidence is available outside lit, and additionally runs `llc -O0/-O2`
#      over it.
#
#   B. QEMU HALF: src/a3-alias-fw.c compiles the same constructs as a real
#      firmware, links with mcs251-lld and runs under QEMU at -O0/-O2 with
#      byte-exact OK<n> reports.
#
# Usage: a3-alias-e2e.sh [--no-qemu]
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "$0")" && pwd)
REPO=${REPO:-$(cd -- "$ROOT/../.." && pwd)}
OUT="$ROOT/build/a3-alias"
SRC="$ROOT/src"

CLANG=${CLANG:-/home/liu/build-mcs251-s1/bin/clang}
LLC=${LLC:-/home/liu/build-mcs251/bin/llc}
OPT=${OPT:-/home/liu/build-mcs251/bin/opt}
FILECHECK=${FILECHECK:-/home/liu/build-mcs251/bin/FileCheck}
LLD=${LLD:-/home/liu/build-mcs251-lld/bin/lld}
OBJCOPY=${OBJCOPY:-/home/liu/build-mcs251-s1/bin/llvm-objcopy}
YAML2OBJ=${YAML2OBJ:-/home/liu/build-mcs251-lld/bin/yaml2obj}
QEMU=${QEMU:-/home/liu/build-qemu/qemu-system-mcs251}
MACHINE=stc32g144k246

CONTRACT=1,1,32,8,1
DIALECT_INC="$REPO/validation/mcs251-dialect/include"
CRT_YAML="$REPO/validation/mcs251-elf/runtime/crt-selfstart.yaml"
ALIAS_LL="$REPO/llvm/test/CodeGen/MCS251/code-addrspacecast-alias.ll"

AREA_ARGS=(
  --area-start=HOME=0xff0000 --area-start=VECS=0xff0003
  --area-start=BOOT=0xff0100 --area-start=CSEG=0xff0200
  --area-start=XINIT=0xff8000 --area-start=XDATA_INIT=0xff9000
  --area-start=XSEG=0x010000 --edata-end 0x3fff
)

rm -rf -- "$OUT"
mkdir -p -- "$OUT"

echo "== A. optimizer pipelines over the IR probes =="
"$OPT" -passes=gvn -S "$ALIAS_LL" -o "$OUT/gvn.ll"
"$FILECHECK" "$ALIAS_LL" --check-prefix=GVN < "$OUT/gvn.ll"
echo "gvn: AS4/AS0 aliases forwarded, unrelated object kept"
"$OPT" -passes='instcombine,early-cse,gvn,simplifycfg' -S "$ALIAS_LL" \
  -o "$OUT/pipe.ll"
"$FILECHECK" "$ALIAS_LL" --check-prefix=PIPE < "$OUT/pipe.ll"
echo "pipeline: same results after canonicalization"
"$OPT" -passes='default<O2>' -S "$ALIAS_LL" -o "$OUT/o2.ll"
"$FILECHECK" "$ALIAS_LL" --check-prefix=O2 < "$OUT/o2.ll"
echo "default<O2>: same results with the full module pipeline"

# The DAG/assembly half of the same probes.
"$LLC" -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 \
  -verify-machineinstrs "$ALIAS_LL" -o "$OUT/alias-O0.s"
"$FILECHECK" "$ALIAS_LL" --check-prefix=O0 < "$OUT/alias-O0.s"
"$LLC" -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O2 \
  -verify-machineinstrs "$ALIAS_LL" -o "$OUT/alias-O2.s"
"$FILECHECK" "$ALIAS_LL" --check-prefix=ASM < "$OUT/alias-O2.s"
echo "llc -O0/-O2: DR read/write sequences for both views"

echo "== B. QEMU pairing firmware =="
"$YAML2OBJ" "$CRT_YAML" -o "$OUT/crt.o"

for opt in -O0 -O2; do
  "$CLANG" --target=mcs251-unknown-none -std=c11 "$opt" -fmcs251-keil \
    -Wall -Wextra -Werror -I"$SRC" -I"$DIALECT_INC" \
    -Xclang -mcs251-memory-contract="$CONTRACT" \
    -S -emit-llvm "$SRC/a3-alias-fw.c" -o "$OUT/fw$opt.ll"
  # The conversion must be addrspacecast, never a bitcast/ptrtoint launder.
  if grep -qE "bitcast ptr addrspace\(4\)|ptrtoint ptr addrspace\(4\)" \
      "$OUT/fw$opt.ll"; then
    echo "FAIL: bitcast/ptrtoint launder in fw$opt.ll" >&2
    exit 1
  fi
  grep -q "addrspacecast" "$OUT/fw$opt.ll" || {
    echo "FAIL: no addrspacecast in fw$opt.ll" >&2
    exit 1
  }
  "$LLC" -mtriple=mcs251 -mcs251-memory-contract="$CONTRACT" \
    -mcs251-object-format=elf -filetype=obj \
    "$OUT/fw$opt.ll" -o "$OUT/fw$opt.o"
  "$LLD" -flavor mcs251 "${AREA_ARGS[@]}" \
    --map="$OUT/fw$opt.map" -o "$OUT/fw$opt.elf" \
    "$OUT/fw$opt.o" "$OUT/crt.o"
  "$OBJCOPY" -O ihex "$OUT/fw$opt.elf" "$OUT/fw$opt.hex"
  echo "fw$opt: clang+llc+lld+ihex OK"
done

if [ "${1:-}" = "--no-qemu" ]; then
  echo "QEMU skipped (--no-qemu); optimizer half verified"
  exit 0
fi

run_qemu() { # opt
  local opt=$1 serial="$OUT/fw$opt.serial"
  : > "$serial"
  ( for _ in $(seq 1 120); do
      if [ -s "$serial" ] && grep -q "A3-ALIAS-" "$serial"; then
        pkill -TERM -f "qemu-system-mcs251.*fw$opt" 2>/dev/null
        exit 0
      fi
      sleep 0.5
    done ) &
  local watcher=$!
  timeout --foreground 60 "$QEMU" -M "$MACHINE" -bios "$OUT/fw$opt.hex" \
    -accel tcg -display none -monitor none -serial "file:$serial" \
    > "$OUT/fw$opt.qemu.stdout" 2> "$OUT/fw$opt.qemu.stderr" || true
  kill "$watcher" 2>/dev/null || true
  wait "$watcher" 2>/dev/null || true
  echo "== QEMU($opt) transcript =="
  cat "$serial"
  grep -q '^A3-ALIAS-PASS$' "$serial" || {
    echo "A3 alias e2e($opt): FAIL (see $serial)" >&2
    exit 1
  }
  echo "A3 alias e2e($opt): PASS"
}

run_qemu -O0
run_qemu -O2
echo "A3 alias e2e complete"
