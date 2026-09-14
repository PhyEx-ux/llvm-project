#!/usr/bin/env bash
# dpl-sentinel-cross-tu.sh - P09 P-3 acceptance: DPL high-7-bits-zero
# sentinel across two real C translation units (P09-BIT-CODEGEN-DESIGN
# section 4.3, lines 324-333).
#
# VERIFICATION LEVEL: real execution. Two independently compiled C TUs
# (dpl-sentinel-caller.c / dpl-sentinel-callee.c, all entries noinline) are
# lowered to ELF objects, linked with the frozen crt-selfstart fixture, and
# EXECUTED under qemu-system-mcs251 (machine stc32g144k246, TCG); the
# assertions read the firmware's serial transcript. No MIR-level substitute
# is used anywhere.
#
# What is asserted (byte-exact, never a bit0 test):
#   1. ARG path: caller dirts the full DPL byte (SFR 0x82, direct
#      addressing) with FE/A5/FF, then passes 0/1/2/-1/INT_MIN as the first
#      (bit) source parameter. The callee captures the FULL DPL byte as its
#      first body statement - before any use of the parameter value - and it
#      must be exactly 00 (for 0) or 01 (everything else). The entry
#      private-copy reads DPL but never writes it, so the captured byte is
#      exactly what the caller deposited at the ecall.
#   2. RETURN path: the callee dirts the full DPL byte itself right before
#      returning a dynamic int; the caller captures the FULL byte
#      immediately after a result-discarded call (no bool decode between
#      the call and the capture) and must find exactly 00/01; the used
#      call's decoded value is checked as well.
#   3. LATER SLOTS: the raw 1-byte static slots _PARM_2/_PARM_3/_PARM_4 are
#      dirted directly (ordinary byte stores through extern symbols renamed
#      to the backend's own slot assembler names) with DISTINCT bytes per
#      slot, and each must be overwritten with exactly its own expected
#      00/01 - a slot store smeared across byte boundaries cannot pass.
#      The callee also observes the raw slot bytes at the top of its body
#      (the entry decode reads but never writes the slots). Adjacent bytes
#      around a slot keep sentinel values across the call, and the mixed
#      signature (bit, int, bit) proves ORIGINAL source-position numbering:
#      the position-3 bit rides _PARM_3 while the position-2 int is not a
#      1-byte bit slot (symbol sizes pinned by readelf).
#   4. ORDINARY ABI golden: plain i8/i16/i32 calls and returns keep their
#      exact values in the same firmware (section 4.3 line 331).
#
# Prohibition compliance (section 4.3 line 333):
#   - the sentinels are never initialised with the bit machinery under
#     test: the TUs contain NO bit-object intrinsics, which the script
#     proves by grepping the emitted IR for llvm.mcs251.bit;
#   - reading DPL bit0 is never a pass criterion: every check compares the
#     FULL byte against exactly 00/01, in the firmware AND again on the
#     host against the printed transcript.
#
# Optimisation coverage (section 4.3 line 331): four firmware builds
# caller x callee in {O0, O2}^2 (the O0<->O2 crossings plus both-same
# baselines); every build must produce the identical byte-exact transcript.
#
# Tools (override via env):
#   CLANG     /home/liu/build-mcs251-s1/bin/clang   (this worktree, P-2/P-3)
#   LLC       /home/liu/build-mcs251/bin/llc        (P-1a backend)
#   YAML2OBJ, LLD via the lld build tree
#   QEMU      /home/liu/build-qemu/qemu-system-mcs251
#
# Usage: dpl-sentinel-cross-tu.sh [--keep]
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "$0")" && pwd)
OUT="$ROOT/build/dpl-sentinel"

CLANG=${CLANG:-/home/liu/build-mcs251-s1/bin/clang}
LLC=${LLC:-/home/liu/build-mcs251/bin/llc}
YAML2OBJ=${YAML2OBJ:-/home/liu/build-mcs251-lld/bin/yaml2obj}
LLD=${LLD:-/home/liu/build-mcs251-lld/bin/mcs251-lld}
OBJCOPY=${OBJCOPY:-/home/liu/build-mcs251-s1/bin/llvm-objcopy}
READELF=${READELF:-/home/liu/build-mcs251-s1/bin/llvm-readelf}
QEMU=${QEMU:-/home/liu/build-qemu/qemu-system-mcs251}
MACHINE=${MACHINE:-stc32g144k246}
# compat memory contract: 256B data window -> fixed-address accesses in
# 0x80..0xFF lower to DIRECT addressing, i.e. the SFR path (same recipe as
# the QEMU-proven validation/mcs251-uartdemo-33m chain).
CONTRACT=1,1,32,8,1
CRT_YAML="$ROOT/../mcs251-elf/runtime/crt-selfstart.yaml"
QEMU_TIMEOUT=${QEMU_TIMEOUT:-60}

for t in "$CLANG" "$LLC" "$YAML2OBJ" "$LLD" "$OBJCOPY" "$READELF" "$QEMU"; do
  command -v "$t" >/dev/null || { echo "FAIL: missing tool: $t" >&2; exit 1; }
done
[ -f "$CRT_YAML" ] || { echo "FAIL: missing CRT fixture: $CRT_YAML" >&2; exit 1; }
[ -f "$ROOT/dpl-sentinel-caller.c" ] || { echo "FAIL: missing caller TU" >&2; exit 1; }
[ -f "$ROOT/dpl-sentinel-callee.c" ] || { echo "FAIL: missing callee TU" >&2; exit 1; }

rm -rf -- "$OUT"
mkdir -p -- "$OUT"

# --- compile both TUs at both optimisation levels (independent) ---
compile_tu() { # <src> <out.o> <O0|O2>
  local src=$1 obj=$2 o=$3
  "$CLANG" -cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil \
    -mcs251-memory-contract="$CONTRACT" "-$o" -emit-llvm \
    -o "${obj%.o}.ll" "$src"
  "$LLC" -mtriple=mcs251 -mcs251-memory-contract="$CONTRACT" \
    -mcs251-object-format=elf -filetype=obj "${obj%.o}.ll" -o "$obj"
}
for o in O0 O2; do
  compile_tu "$ROOT/dpl-sentinel-caller.c" "$OUT/caller-$o.o" "$o"
  compile_tu "$ROOT/dpl-sentinel-callee.c" "$OUT/callee-$o.o" "$o"
done
"$YAML2OBJ" "$CRT_YAML" -o "$OUT/crt.o"
echo "compile: caller/callee x {O0,O2} + crt OK"

# --- oracle independence: no bit-object intrinsics anywhere in the IR ---
for f in "$OUT"/caller-*.ll "$OUT"/callee-*.ll; do
  if grep -q 'llvm\.mcs251\.bit' "$f"; then
    echo "FAIL: bit intrinsic found in $f - the sentinel oracle must not use the bit machinery under test" >&2
    exit 1
  fi
done
echo "oracle: no llvm.mcs251.bit intrinsic in any TU IR (prohibition 1)"

# --- slot symbol contract: ORIGINAL source positions, 1-byte bit slots ---
# _slot_callee_PARM_2/3/4 and _solo_callee_PARM_2 are 1-byte OBJECT globals;
# _mixed_callee_PARM_3 is the position-3 bit's 1-byte slot (a renumbering
# bug that allocated slots by bit ordinal would place it at _PARM_2, whose
# size is the position-2 int's 4 bytes, not 1).
sym_line() { # <obj> <name> -> " <size> OBJECT GLOBAL ... <name>" or empty
  "$READELF" -s "$1" | awk -v n="$2" '$8 == n { print $3, $4, $5, $8; exit }'
}
need_sym() { # <obj> <name> <size>
  local got
  got=$(sym_line "$1" "$2")
  [ "$got" = "$3 OBJECT GLOBAL $2" ] || {
    echo "FAIL: symbol $2 in $(basename "$1") is '$got', expected '$3 OBJECT GLOBAL $2'" >&2
    exit 1
  }
}
for o in O0 O2; do
  need_sym "$OUT/callee-$o.o" _slot_callee_PARM_2 1
  need_sym "$OUT/callee-$o.o" _slot_callee_PARM_3 1
  need_sym "$OUT/callee-$o.o" _slot_callee_PARM_4 1
  need_sym "$OUT/callee-$o.o" _solo_callee_PARM_2 1
  need_sym "$OUT/callee-$o.o" _mixed_callee_PARM_3 1
done
echo "symbols: bit slots are 1-byte globals at ORIGINAL source positions (_PARM_2/3/4, mixed _PARM_3)"

# --- link a firmware per caller x callee optimisation pair ---
link_fw() { # <tag> (tag is <callerO>-<calleeO>)
  local tag=$1 co=${1%%-*} ce=${1##*-}
  (cd "$OUT" && "$LLD" --edata-end 0x0fff \
    --area-start=HOME=0xff0000 --area-start=VECS=0xff0003 \
    --area-start=BOOT=0xff0100 --area-start=CSEG=0xff0200 \
    --area-start=XINIT=0xff8000 --map="fw-$tag.map" -o "fw-$tag.elf" \
    crt.o "caller-$co.o" "callee-$ce.o")
  "$OBJCOPY" -O ihex "$OUT/fw-$tag.elf" "$OUT/fw-$tag.hex"
}
link_fw O0-O0
link_fw O0-O2
link_fw O2-O0
link_fw O2-O2
echo "link: 4 firmwares (caller x callee in {O0,O2}^2) OK"

# --- run one firmware under QEMU; the sentinel watcher recovers QEMU ---
run_qemu() { # <tag>
  local tag=$1
  local serial="$OUT/fw-$tag.serial"
  : > "$serial"
  ( for i in $(seq 1 $((QEMU_TIMEOUT * 2))); do
      if [ -s "$serial" ] && tail -c 256 "$serial" 2>/dev/null | \
          grep -q 'DPL-SENTINEL-\(PASS\|FAIL\)'; then
        pkill -TERM -f "fw-$tag.hex" 2>/dev/null
        exit 0
      fi
      sleep 0.5
    done ) &
  local watcher=$!
  timeout --foreground "$QEMU_TIMEOUT" "$QEMU" -M "$MACHINE" \
    -bios "$OUT/fw-$tag.hex" -accel tcg -display none -monitor none \
    -serial "file:$serial" \
    > "$serial.qemu.stdout" 2> "$serial.qemu.stderr" || true
  kill "$watcher" 2>/dev/null || true
  wait "$watcher" 2>/dev/null || true
  [ -s "$serial" ] || {
    echo "FAIL[$tag]: no serial output (QEMU stderr:)" >&2
    head -5 "$serial.qemu.stderr" >&2
    exit 1
  }
}

# --- byte-exact transcript verification (prohibition 2: full bytes) ---
check_serial() { # <tag>
  local tag=$1
  local serial="$OUT/fw-$tag.serial"
  local p v b
  if grep -q '^!' "$serial"; then
    echo "FAIL[$tag]: firmware reported full-byte mismatches:" >&2
    grep '^!' "$serial" >&2
    exit 1
  fi
  # every expected byte line, exactly (A/R/S/2/3/4/M tables)
  for p in FE A5 FF; do
    for v in 0 1 2 3 4; do
      if [ "$v" = 0 ]; then b=00; else b=01; fi
      for pref in A R S 2 M; do
        grep -qx "$pref:$p:$v=$b" "$serial" || {
          echo "FAIL[$tag]: missing/bad line '$pref:$p:$v=$b'" >&2
          exit 1
        }
      done
      grep -qx "3:$p:$v=00" "$serial" || {
        echo "FAIL[$tag]: missing/bad line '3:$p:$v=00'" >&2; exit 1; }
      grep -qx "4:$p:$v=01" "$serial" || {
        echo "FAIL[$tag]: missing/bad line '4:$p:$v=01'" >&2; exit 1; }
    done
  done
  # callee-side raw slot observations of the final combo (FF, INT_MIN)
  grep -qx 'C:2=01' "$serial" || { echo "FAIL[$tag]: missing/bad C:2" >&2; exit 1; }
  grep -qx 'C:3=00' "$serial" || { echo "FAIL[$tag]: missing/bad C:3" >&2; exit 1; }
  grep -qx 'C:4=01' "$serial" || { echo "FAIL[$tag]: missing/bad C:4" >&2; exit 1; }
  # adjacent-byte sentinels around the solo slot
  for p in FE A5 FF; do
    grep -qx "N:$p:lo=5A,hi=A5" "$serial" || {
      echo "FAIL[$tag]: missing/bad neighbor line 'N:$p'" >&2; exit 1; }
  done
  # ordinary ABI golden values
  local i=0 g
  for g in 0B 30 55 7A 9F C4; do
    grep -qx "G:u8:$i=$g" "$serial" || {
      echo "FAIL[$tag]: missing/bad golden u8 line 'G:u8:$i=$g'" >&2; exit 1; }
    i=$((i + 1))
  done
  grep -qx 'G:i32=23456789' "$serial" || {
    echo "FAIL[$tag]: missing/bad G:i32" >&2; exit 1; }
  grep -qx 'G:i16=68AC' "$serial" || {
    echo "FAIL[$tag]: missing/bad G:i16" >&2; exit 1; }
  # complete, untruncated transcript with the sentinel as the last line
  [ "$(grep -c '^A:' "$serial")" -eq 15 ] || { echo "FAIL[$tag]: A-line count != 15" >&2; exit 1; }
  [ "$(grep -c '^R:' "$serial")" -eq 15 ] || { echo "FAIL[$tag]: R-line count != 15" >&2; exit 1; }
  [ "$(grep -c '^2:' "$serial")" -eq 15 ] || { echo "FAIL[$tag]: slot-2 line count != 15" >&2; exit 1; }
  [ "$(grep -c '^3:' "$serial")" -eq 15 ] || { echo "FAIL[$tag]: slot-3 line count != 15" >&2; exit 1; }
  [ "$(grep -c '^4:' "$serial")" -eq 15 ] || { echo "FAIL[$tag]: slot-4 line count != 15" >&2; exit 1; }
  [ "$(grep -c '^S:' "$serial")" -eq 15 ] || { echo "FAIL[$tag]: solo-slot line count != 15" >&2; exit 1; }
  [ "$(grep -c '^M:' "$serial")" -eq 15 ] || { echo "FAIL[$tag]: M-line count != 15" >&2; exit 1; }
  [ "$(grep -c '^N:' "$serial")" -eq 3 ] || { echo "FAIL[$tag]: N-line count != 3" >&2; exit 1; }
  [ "$(tail -n 1 "$serial")" = "DPL-SENTINEL-PASS" ] || {
    echo "FAIL[$tag]: last line is not DPL-SENTINEL-PASS:" >&2
    tail -3 "$serial" >&2
    exit 1
  }
  echo "run[$tag]: 15 arg + 15 ret + 60 slot + 15 mixed + 3 neighbor + 8 golden lines byte-exact, sentinel PASS"
}

for combo in O0-O0 O0-O2 O2-O0 O2-O2; do
  run_qemu "$combo"
  check_serial "$combo"
done

echo "DPL-SENTINEL-CROSS-TU: PASS"
case " ${1:-} " in *" --keep "*) echo "kept: $OUT";; *) rm -rf -- "$OUT";; esac
