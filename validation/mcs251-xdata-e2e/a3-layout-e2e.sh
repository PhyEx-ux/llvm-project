#!/usr/bin/env bash
# a3-layout-e2e.sh - R4 layout/dataflow acceptance chain
# (RUNTIME-AS-PTR-DESIGN-A.md §3-A3 "验收补充二" §2.2).
#
#   A. LAYOUT HALF: Inputs/check-as4-layout-dataflow.py queries the target's
#      own datalayout per memory contract (p0/p4 size and index width, plus
#      the 16-bit Tiny/XTiny refusal controls), requires the documented round
#      trip to fold to an identity after a real optimization pipeline, rejects
#      bitcast/ptrtoint laundering, and counts DR lanes for the 4-byte load
#      through both views.
#   B. QEMU HALF: src/a3-layout-fw.c compares the RELOCATED addresses and
#      bytes of a CODE table seen through AS4 and through the converted AS0
#      pointer, at -O0 and -O2.
#
# Usage: a3-layout-e2e.sh [--no-qemu]
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "$0")" && pwd)
REPO=${REPO:-$(cd -- "$ROOT/../.." && pwd)}
OUT="$ROOT/build/a3-layout"
SRC="$ROOT/src"
TEST_LL="$REPO/llvm/test/CodeGen/MCS251/code-addrspacecast-datalayout.ll"
CHECKER="$REPO/llvm/test/CodeGen/MCS251/Inputs/check-as4-layout-dataflow.py"

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

AREA_ARGS=(
  --area-start=HOME=0xff0000 --area-start=VECS=0xff0003
  --area-start=BOOT=0xff0100 --area-start=CSEG=0xff0200
  --area-start=XINIT=0xff8000 --area-start=XDATA_INIT=0xff9000
  --area-start=XSEG=0x010000 --edata-end 0x3fff
)

rm -rf -- "$OUT"
mkdir -p -- "$OUT"

echo "== A. target layout queries + dataflow =="
python3 "$CHECKER" --llc "$LLC" --opt "$OPT" --test-ll "$TEST_LL"
python3 "$CHECKER" --llc "$LLC" --opt "$OPT" --test-ll "$TEST_LL" --self-test

"$OPT" -passes=instcombine,gvn,simplifycfg -S "$TEST_LL" -o "$OUT/opt.ll"
"$FILECHECK" "$TEST_LL" --check-prefix=OPT < "$OUT/opt.ll"
"$LLC" -mtriple=mcs251 -mcs251-memory-contract="$CONTRACT" -O0 \
  -verify-machineinstrs "$TEST_LL" -o "$OUT/test.s"
"$FILECHECK" "$TEST_LL" --check-prefix=MOD < "$OUT/test.s"
echo "lit half: layout table, identity fold, lane counts all pass"

echo "== B. QEMU relocated-address/byte pairing =="
"$YAML2OBJ" "$CRT_YAML" -o "$OUT/crt.o"

# The CODE table lives in its OWN TU (src/a3-layout-tab.c): with the table
# and the comparison in one TU, -O2 resolves the address and constant-folds
# the whole check (Alice review R9-2), so the "five OKs" measured nothing at
# run time.  The accessor `lay_base()` is the only handle the firmware TU
# has, which keeps the converted address dynamic at -O2.
for opt in -O0 -O2; do
  "$CLANG" --target=mcs251-unknown-none -std=c11 "$opt" -fmcs251-keil \
    -Wall -Wextra -Werror -I"$SRC" -I"$DIALECT_INC" \
    -Xclang -mcs251-memory-contract="$CONTRACT" \
    -S -emit-llvm "$SRC/a3-layout-tab.c" -o "$OUT/tab$opt.ll"
  "$LLC" -mtriple=mcs251 -mcs251-memory-contract="$CONTRACT" \
    -mcs251-object-format=elf -filetype=obj \
    "$OUT/tab$opt.ll" -o "$OUT/tab$opt.o"
  "$CLANG" --target=mcs251-unknown-none -std=c11 "$opt" -fmcs251-keil \
    -Wall -Wextra -Werror -I"$SRC" -I"$DIALECT_INC" \
    -Xclang -mcs251-memory-contract="$CONTRACT" \
    -S -emit-llvm "$SRC/a3-layout-fw.c" -o "$OUT/fw$opt.ll"
  # The conversion must be addrspacecast, never a bitcast.  `ptrtoint` is
  # allowed HERE because the firmware uses it to *measure* the numeric
  # address on both sides (that is the R4 acceptance), but a ptrtoint
  # immediately feeding inttoptr would be an integer laundered conversion and
  # is rejected explicitly.
  if grep -qE "bitcast ptr addrspace\(4\)|bitcast ptr .* to ptr addrspace\(4\)" \
      "$OUT/fw$opt.ll"; then
    echo "FAIL: bitcast used for the AS conversion at $opt" >&2
    exit 1
  fi
  if grep -qE "inttoptr i32 %[A-Za-z0-9._]+ to ptr addrspace\(4\)" \
      "$OUT/fw$opt.ll"; then
    echo "FAIL: integer-laundered AS0 -> AS4 conversion at $opt" >&2
    exit 1
  fi
  # O2 must keep a DYNAMIC observation: a real addrspacecast instruction and
  # real loads of the converted pointer, not a folded relocation comparison
  # (Alice review R9-2).  At -O0 this is trivially the case; at -O2 it is the
  # whole point of the separate table TU.
  if ! grep -qE "addrspacecast ptr addrspace\(4\) %" "$OUT/fw$opt.ll"; then
    echo "FAIL: no dynamic AS4 -> AS0 addrspacecast in fw$opt.ll; the O2" \
         "evidence would be a constant fold, not a data-flow observation" >&2
    exit 1
  fi
  if ! grep -qE "load i(8|16|32), ptr %" "$OUT/fw$opt.ll"; then
    echo "FAIL: no AS0 load of a dynamic pointer in fw$opt.ll at $opt" >&2
    exit 1
  fi
  "$LLC" -mtriple=mcs251 -mcs251-memory-contract="$CONTRACT" \
    -mcs251-object-format=elf -filetype=obj \
    "$OUT/fw$opt.ll" -o "$OUT/fw$opt.o"
  "$LLD" -flavor mcs251 "${AREA_ARGS[@]}" \
    --map="$OUT/fw$opt.map" -o "$OUT/fw$opt.elf" \
    "$OUT/fw$opt.o" "$OUT/tab$opt.o" "$OUT/crt.o"
  "$OBJCOPY" -O ihex "$OUT/fw$opt.elf" "$OUT/fw$opt.hex"
  echo "fw$opt: clang+llc+lld+ihex OK (dynamic ADDRESPACECAST + AS0 load retained)"
done

if [ "${1:-}" = "--no-qemu" ]; then
  echo "QEMU skipped (--no-qemu)"
  exit 0
fi

run_qemu() { # opt
  local opt=$1 serial="$OUT/fw$opt.serial"
  : > "$serial"
  ( for _ in $(seq 1 120); do
      if [ -s "$serial" ] && grep -q "A3-LAYOUT-" "$serial"; then
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
  grep -q '^A3-LAYOUT-PASS$' "$serial" || {
    echo "A3 layout e2e($opt): FAIL (see $serial)" >&2
    exit 1
  }
  echo "A3 layout e2e($opt): PASS"
}

run_qemu -O0
run_qemu -O2
echo "A3 layout e2e complete"
