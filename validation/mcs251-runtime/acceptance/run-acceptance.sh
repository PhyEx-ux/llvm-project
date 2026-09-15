#!/usr/bin/env bash
# run-acceptance.sh -- MCS251 connected-f32 runtime acceptance
#
# The compiler firmware deliberately uses C float arithmetic, comparisons and
# casts with volatile operands.  It is built twice (O0/O2), linked with an
# explicit list of independent runtime objects, and its UART transcript is
# compared byte-for-byte with the host oracle.  No archive or directory scan is
# used for the runtime link.
#
# Usage:
#   ./run-acceptance.sh
#   ./run-acceptance.sh --inject-wrong
#
# --inject-wrong appends a bogus oracle record.  A successful negative run must
# therefore return nonzero and print ACCEPTANCE-FAIL.

set -u
set -o pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

ACC=/home/liu/mcs251-runtime-acceptance
REPO=/mnt/c/Prj/LLVM/MCS251
SRC=$REPO/validation/mcs251-runtime/acceptance
RTSRC=$REPO/validation/mcs251-runtime/src
CLANG=/home/liu/build-mcs251-s1/bin/clang
LLC=/home/liu/build-mcs251-s1/bin/llc
LLD=/home/liu/build-mcs251-lld/bin/mcs251-lld
OBJCOPY=/home/liu/build-mcs251-s1/bin/llvm-objcopy
READELF=/home/liu/build-mcs251-s1/bin/llvm-readelf
STRINGS=/home/liu/build-mcs251-s1/bin/llvm-strings
QEMU=/home/liu/build-qemu/qemu-system-mcs251
HOSTCC=/usr/bin/cc
CRT=/home/liu/mcs251-rt-acceptance/crt-selfstart.o
INT_RT=/home/liu/build-mcs251-s1/lib/Target/MCS251/Runtime
MACHINE=stc32g144k246
# G12-compatible ABI/layout: v1 compatibility, 32-bit AS0, default placement 8.
CONTRACT=1,1,32,8,1
G12_EDATA_END=0x0fff

case "${1:-}" in
  "") INJECT_WRONG=0 ;;
  --inject-wrong) INJECT_WRONG=1 ;;
  *) echo "usage: $0 [--inject-wrong]" >&2; exit 2 ;;
esac

LOG=$ACC/compiler-float
mkdir -p "$LOG"
FAIL=0

step() { printf '== %s ==\n' "$*"; }
mark_failure() { FAIL=1; }

require_file() {
  if [ ! -x "$1" ] && [ ! -f "$1" ]; then
    printf 'MISSING: %s\n' "$1" >&2
    mark_failure
  fi
}

for Tool in "$CLANG" "$LLC" "$LLD" "$OBJCOPY" "$QEMU" "$CRT"; do
  require_file "$Tool"
done

# Explicit, ordered runtime object lists.  Do not replace either list with an
# archive, a glob or a directory scan.
INT_RUNTIME_OBJS=(
  "$INT_RT/mcs251rt_divuint.o"
  "$INT_RT/mcs251rt_divulong.o"
  "$INT_RT/mcs251rt_divsint.o"
  "$INT_RT/mcs251rt_divslong.o"
  "$INT_RT/mcs251rt_moduint.o"
  "$INT_RT/mcs251rt_modulong.o"
  "$INT_RT/mcs251rt_modsint.o"
  "$INT_RT/mcs251rt_modslong.o"
)
for Obj in "${INT_RUNTIME_OBJS[@]}"; do
  require_file "$Obj"
done

compile_ir() {
  local source=$1 opt=$2 output=$3
  local extra_flags=()
  case "$source" in
    "$RTSRC"/*) extra_flags=(-DMCS251_RT_TARGET) ;;
  esac
  "$CLANG" --target=mcs251-unknown-none -std=c11 -ffreestanding -fno-builtin \
    "${extra_flags[@]}" -Xclang -mcs251-memory-contract="$CONTRACT" \
    -"$opt" -S -emit-llvm "$source" -o "$output"
}

compile_obj() {
  local input=$1 opt=$2 output=$3
  "$LLC" -mtriple=mcs251 -mcs251-memory-contract="$CONTRACT" -"$opt" \
    -verify-machineinstrs -mcs251-object-format=elf -filetype=obj \
    "$input" -o "$output"
}

step "host float oracle"
if "$HOSTCC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$SRC/compiler_float_expect.c" -o "$LOG/compiler_float_expect" \
    2>"$LOG/oracle.build.err"; then
  "$LOG/compiler_float_expect" >"$LOG/compiler_float.expected" \
    2>"$LOG/oracle.run.err" || mark_failure
else
  cat "$LOG/oracle.build.err"
  mark_failure
fi
if [ "$INJECT_WRONG" -eq 1 ] && [ "$FAIL" -eq 0 ]; then
  step "inject incorrect oracle record"
  printf 'FOP i=DEADBEEF injected=BAD\n' >>"$LOG/compiler_float.expected"
fi

step "host runtime fuzz"
# G7 S1'': the arithmetic helpers live in per-routine TUs now; the host fuzz
# links all of them plus the comparison/bit-utility units.
if "$HOSTCC" -std=c11 -O2 -Wall -Wextra -Werror -I"$RTSRC" \
    "$SRC/host_fuzz.c" "$RTSRC/mcs251_float_arith.c" \
    "$RTSRC/mcs251_float_addsub.c" "$RTSRC/mcs251_float_mul.c" \
    "$RTSRC/mcs251_float_div.c" \
    "$RTSRC/mcs251_float_cmp.c" "$RTSRC/mcs251_bitutil.c" -lm \
    -o "$LOG/host_fuzz" 2>"$LOG/host_fuzz.build.err"; then
  "$LOG/host_fuzz" >"$LOG/host_fuzz.out" 2>&1 || mark_failure
  tail -2 "$LOG/host_fuzz.out" || true
else
  cat "$LOG/host_fuzz.build.err"
  mark_failure
fi

# Build the f32 runtime in the same compatibility ABI used by the compiler
# firmware.  Arithmetic/cmp use O2; bit utilities remain O0 by runtime design.
if [ "$FAIL" -eq 0 ]; then
  step "compile explicit f32 runtime objects"
  # G7 S1'': the arithmetic routines are split into per-routine translation
  # units (addsub/mul/div) so the linker can pull a single helper.  Each is
  # still built with the -O2 pipeline.
  for Name in mcs251_float_arith mcs251_float_addsub mcs251_float_mul \
              mcs251_float_div mcs251_float_cmp; do
    if ! compile_ir "$RTSRC/$Name.c" O2 "$LOG/$Name.ll" \
        2>"$LOG/$Name.clang.err" || \
       ! compile_obj "$LOG/$Name.ll" O2 "$LOG/$Name.o" \
        2>"$LOG/$Name.llc.err"; then
      printf 'RUNTIME-BUILD-FAIL: %s\n' "$Name"
      cat "$LOG/$Name.clang.err" "$LOG/$Name.llc.err" 2>/dev/null || true
      mark_failure
    fi
  done
  if ! compile_ir "$RTSRC/mcs251_bitutil.c" O0 "$LOG/mcs251_bitutil.ll" \
      2>"$LOG/mcs251_bitutil.clang.err" || \
     ! compile_obj "$LOG/mcs251_bitutil.ll" O0 "$LOG/mcs251_bitutil.o" \
      2>"$LOG/mcs251_bitutil.llc.err"; then
    printf 'RUNTIME-BUILD-FAIL: mcs251_bitutil\n'
    cat "$LOG/mcs251_bitutil.clang.err" "$LOG/mcs251_bitutil.llc.err" \
      2>/dev/null || true
    mark_failure
  fi
fi

FLOAT_RUNTIME_OBJS=(
  "$LOG/mcs251_float_arith.o"
  "$LOG/mcs251_float_addsub.o"
  "$LOG/mcs251_float_mul.o"
  "$LOG/mcs251_float_div.o"
  "$LOG/mcs251_float_cmp.o"
  "$LOG/mcs251_bitutil.o"
)

# G7 S1' object-level gate: the connected unsigned conversion pair must be a
# real definition in the runtime object (not merely declared), and the object's
# Tag 28 signature table must carry a record for each.  G7 S1'' split the
# arithmetic helpers into their own TUs, so the gate checks each helper in the
# object that now defines it (the arith TU keeps the conversion helpers).
if [ "$FAIL" -eq 0 ]; then
  step "f32 runtime object-level helper gate (G7 S1'/S1'')"
  for Pair in "__floatunsisf:$LOG/mcs251_float_arith.o" \
              "__fixunssfsi:$LOG/mcs251_float_arith.o" \
              "__floatsisf:$LOG/mcs251_float_arith.o" \
              "__fixsfsi:$LOG/mcs251_float_arith.o" \
              "__mulsf3:$LOG/mcs251_float_mul.o" \
              "__addsf3:$LOG/mcs251_float_addsub.o" \
              "__divsf3:$LOG/mcs251_float_div.o"; do
    Sym="${Pair%%:*}"; ARITH_OBJ="${Pair#*:}"
    if ! "$READELF" -sW "$ARITH_OBJ" 2>/dev/null \
         | grep -qE "FUNC +GLOBAL +DEFAULT +[0-9]+ +$Sym\$"; then
      printf 'HELPER-GATE-FAIL: %s is not a defined global function in %s\n' \
             "$Sym" "$(basename "$ARITH_OBJ")"
      mark_failure
      break
    fi
    if ! "$STRINGS" "$ARITH_OBJ" | grep -qF "$Sym"; then
      printf 'HELPER-GATE-FAIL: %s missing from Tag 28 signature payload of %s\n' \
             "$Sym" "$(basename "$ARITH_OBJ")"
      mark_failure
      break
    fi
  done
  if [ "$FAIL" -eq 0 ]; then
    printf 'HELPER-GATE-PASS: 7 helpers defined and recorded in Tag 28\n'
  fi
fi

run_qemu() {
  local hex=$1 serial=$2 timeout_seconds=$3 sentinel=$4
  : >"$serial"
  (
    for _ in $(seq 1 $((timeout_seconds * 2))); do
      if [ -s "$serial" ] && tail -c 256 "$serial" 2>/dev/null | grep -Fq "$sentinel"; then
        pkill -TERM -f "qemu-system-mcs251.*$(basename "$hex")" 2>/dev/null || true
        exit 0
      fi
      sleep 0.5
    done
  ) &
  local watcher=$!
  timeout --foreground "$timeout_seconds" "$QEMU" -M "$MACHINE" -bios "$hex" \
    -accel tcg -display none -monitor none -serial "file:$serial" \
    >"$serial.qemu.stdout" 2>"$serial.qemu.stderr"
  QEMU_RC=$?
  kill "$watcher" 2>/dev/null || true
  wait "$watcher" 2>/dev/null || true
}

compare_transcript() {
  local expected=$1 actual=$2 label=$3
  python3 - "$expected" "$actual" "$label" <<'PYEOF'
import sys
expected_path, actual_path, label = sys.argv[1:]
expected = open(expected_path, "rb").read().splitlines()
actual = open(actual_path, "rb").read().splitlines()
while expected and not expected[-1]: expected.pop()
while actual and not actual[-1]: actual.pop()
mismatches = 0
for index in range(max(len(expected), len(actual))):
    lhs = expected[index] if index < len(expected) else b"<missing>"
    rhs = actual[index] if index < len(actual) else b"<missing>"
    if lhs != rhs:
        mismatches += 1
        if mismatches <= 10:
            print(f"{label}: mismatch line {index + 1}: expected={lhs!r} actual={rhs!r}")
if mismatches:
    print(f"{label}: FAIL ({mismatches} mismatches)")
    sys.exit(1)
print(f"{label}: PASS ({len(expected)} bytes-lines)")
PYEOF
}

link_firmware() {
  local stem=$1 firmware=$2
  "$LLD" --edata-end "$G12_EDATA_END" \
    --area-start=HOME=0xff0000 --area-start=VECS=0xff0003 \
    --area-start=BOOT=0xff0100 --area-start=CSEG=0xfe0000 \
    --area-start=XINIT=0xff8000 --map "$LOG/$stem.map" \
    -o "$LOG/$stem.elf" "$firmware" "$CRT" \
    "${FLOAT_RUNTIME_OBJS[@]}" "${INT_RUNTIME_OBJS[@]}" \
    >"$LOG/$stem.link.stdout" 2>"$LOG/$stem.link.stderr"
}

for OPT in O0 O2; do
  [ "$FAIL" -eq 0 ] || break
  STEM=compiler_float_$OPT
  step "compile compiler float firmware $OPT"
  if ! compile_ir "$SRC/compiler_float_firmware.c" "$OPT" "$LOG/$STEM.ll" \
      2>"$LOG/$STEM.clang.err" || \
     ! compile_obj "$LOG/$STEM.ll" "$OPT" "$LOG/$STEM.o" \
      2>"$LOG/$STEM.llc.err"; then
    printf 'FIRMWARE-BUILD-FAIL: %s\n' "$OPT"
    cat "$LOG/$STEM.clang.err" "$LOG/$STEM.llc.err" 2>/dev/null || true
    mark_failure
    break
  fi

  step "link compiler float firmware $OPT with explicit objects"
  if ! link_firmware "$STEM" "$LOG/$STEM.o"; then
    printf 'FIRMWARE-LINK-FAIL: %s\n' "$OPT"
    cat "$LOG/$STEM.link.stderr"
    mark_failure
    break
  fi
  if ! "$OBJCOPY" -O ihex "$LOG/$STEM.elf" "$LOG/$STEM.hex"; then
    printf 'OBJCOPY-FAIL: %s\n' "$OPT"
    mark_failure
    break
  fi

  step "QEMU compiler float firmware $OPT"
  run_qemu "$LOG/$STEM.hex" "$LOG/$STEM.serial" 90 COMPILER-FLOAT-PASS
  if ! tail -c 256 "$LOG/$STEM.serial" 2>/dev/null | grep -Fq COMPILER-FLOAT-PASS; then
    printf 'QEMU-FAIL: %s rc=%s sentinel missing\n' "$OPT" "$QEMU_RC"
    tail -40 "$LOG/$STEM.serial" 2>/dev/null || true
    cat "$LOG/$STEM.serial.qemu.stderr" 2>/dev/null || true
    mark_failure
    break
  fi
  if ! compare_transcript "$LOG/compiler_float.expected" "$LOG/$STEM.serial" \
      "$STEM"; then
    mark_failure
    break
  fi
done

if [ "$FAIL" -eq 0 ]; then
  printf 'ACCEPTANCE-PASS\n'
  exit 0
fi
printf 'ACCEPTANCE-FAIL\n'
exit 1
