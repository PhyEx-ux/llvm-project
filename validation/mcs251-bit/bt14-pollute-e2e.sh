#!/usr/bin/env bash
# bt14-pollute-e2e.sh - BT14 acceptance: neighbour-preserving bit
# initialization under POLLUTED RAM, end to end in QEMU.
#
# The bit window 0x20..0x2F is polluted with A5/5A by QEMU's -device loader
# BEFORE the CPU starts (reset does not clear RAM in this machine model), so
# QEMU's default zero memory can never masquerade as initialization
# evidence.  The firmware (bt14-pollute-fw.c/tu1/tu2) then runs under the
# bit-aware CRT (crt-bit.yaml + lld-synthesized .mcs251.bittable) and dumps
# the raw window plus the bit-instruction views; this script compares every
# byte against (pollution & ~mask) | value computed FROM THE LINK MAP, never
# from hand-written constants.  A byte-exact match proves:
#   - owned bits hold their declared values, zeros included (they were
#     cleared through the mask, not by luck of zero RAM: byte 0x20 was
#     polluted 0xA5 and comes back 0x25);
#   - neighbouring bits in a partial-mask byte keep the polluted value
#     (byte 0x21: 0x5A -> 0x5B, bits 2..7 untouched);
#   - bytes without a table record are never accessed (0x22..0x2F come back
#     exactly as polluted - if the CRT still cleared the window S1-style,
#     these would read 0x00);
#   - the bit-instruction views agree with the byte view (L/P/X/F lines);
#   - the ROM table was actually read and applied (non-zero owned values
#     under pollution are unreachable any other way).
#
# Scope notes (honest limits of this e2e):
#   - FX (numeric sbit 0x50, byte 0x2A) is inlined by the C frontend into
#     absolute bit-address instructions and carries no kind-2 .mcs251.bit
#     record, so the "fixed byte keeps pollution" case here means "a byte
#     only ever touched by a fixed sbit" (no record, no access).  The
#     kind-2 owner-fixed map case is lit-level (bit-profile.test fixed.yaml).
#   - The walker itself is validated by execution here; crt-bit-walker.asm
#     documents the 2026-09-16 register-plan defect this run caught.
#
# Tools (override via env): CLANG LLC LLD YAML2OBJ READOBJ-s1 OBJCOPY QEMU.
# Usage: bt14-pollute-e2e.sh [--keep]
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "$0")" && pwd)
REPO=$(cd -- "$ROOT/../.." && pwd)
OUT="$ROOT/build/bt14-pollute"

CLANG=${CLANG:-/home/liu/build-mcs251-s1/bin/clang}
LLC=${LLC:-/home/liu/build-mcs251/bin/llc}
LLD=${LLD:-/home/liu/build-mcs251-lld/bin/mcs251-lld}
YAML2OBJ=${YAML2OBJ:-/home/liu/build-mcs251-lld/bin/yaml2obj}
OBJCOPY=${OBJCOPY:-/home/liu/build-mcs251-s1/bin/llvm-objcopy}
QEMU=${QEMU:-/home/liu/build-qemu/qemu-system-mcs251}
MACHINE=${MACHINE:-stc32g144k246}
CONTRACT=1,1,32,8,1
CRT_YAML="$REPO/validation/mcs251-elf/runtime/crt-bit.yaml"
DUMPBYTES="$REPO/lld/test/MCS251/Inputs/dump-elf-bytes.py"
QEMU_TIMEOUT=${QEMU_TIMEOUT:-30}

KEEP=0
for a in "$@"; do case "$a" in --keep) KEEP=1 ;; esac; done

for t in "$CLANG" "$LLC" "$LLD" "$YAML2OBJ" "$OBJCOPY" "$QEMU"; do
  command -v "$t" >/dev/null || { echo "FAIL: missing tool: $t" >&2; exit 1; }
done

rm -rf -- "$OUT"
mkdir -p -- "$OUT"
cd "$OUT"

# --- compile: three real C TUs, independently -----------------------------
for tu in fw tu1 tu2; do
  "$CLANG" -cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil \
    -mcs251-memory-contract="$CONTRACT" \
    -emit-llvm -o "$tu.ll" "$ROOT/bt14-pollute-$tu.c"
  "$LLC" -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf \
    -mcs251-memory-contract="$CONTRACT" "$tu.ll" -o "$tu.o"
done
"$YAML2OBJ" "$CRT_YAML" -o crtbit.o
echo "compile: fw/tu1/tu2 (clang -cc1 + llc ELF) + crt-bit.yaml OK"

# --- link: bit-aware CRT + BITINIT area ------------------------------------
"$LLD" crtbit.o fw.o tu1.o tu2.o \
  --area-start=HOME=0xff0000 --area-start=BOOT=0xff0500 \
  --area-start=CSEG=0xff0700 --area-start=XINIT=0xff8000 \
  --area-start=BITINIT=0xff8200 \
  --map=pollute.map -o pollute.elf
echo "link: crt-bit + fw + tu1 + tu2 OK"

# --- map-level assertions (inputs of the runtime oracle) ------------------
grep -q '^BITBYTE 0x20 mask 0xff value 0x25 owner crt-pool .mcs251.BSEG_BYTES init bit-rmw$' pollute.map || {
  echo "FAIL: full-mask byte row wrong: $(grep '^BITBYTE 0x20' pollute.map)" >&2; exit 1; }
grep -q '^BITBYTE 0x21 mask 0x03 value 0x03 owner crt-pool .mcs251.BSEG_BYTES init bit-rmw$' pollute.map || {
  echo "FAIL: partial/cross-TU byte row wrong: $(grep '^BITBYTE 0x21' pollute.map)" >&2; exit 1; }
# cross-TU aggregation: p1 (tu1) and q1 (tu2) share byte 0x21
grep -q '^BIT _p1 = 0x08 byte 0x21 index 0 init 1$' pollute.map
grep -q '^BIT _q1 = 0x09 byte 0x21 index 1 init 1$' pollute.map
# no other byte of the window is owned/recorded
[ "$(grep -c '^BITBYTE ' pollute.map)" -eq 2 ] || {
  echo "FAIL: unexpected BITBYTE rows:" >&2; grep '^BITBYTE ' pollute.map >&2; exit 1; }
[ "$(sed -n 's/^l_BITINIT = 0x//p' pollute.map)" = "0006" ] || {
  echo "FAIL: l_BITINIT != 6 (two 3-byte records)" >&2; exit 1; }
[ "$(sed -n 's/^l_XINIT = 0x//p' pollute.map)" = "0000" ] || {
  echo "FAIL: bit bytes must not synthesize XINIT records (l_XINIT != 0)" >&2; exit 1; }
echo "map: byte 0x20 {ff,25} + byte 0x21 {03,03} bit-rmw, 2 records, l_XINIT=0"

# --- the ROM table bytes, recomputed from the map --------------------------
# one 3-byte record {addr, ~mask, value&mask} per recorded byte, ascending
tab_expect=$(awk '
  /^BITBYTE / { addr = strtonum($2); mask = strtonum($4); val = strtonum($6);
                printf "%02x %02x %02x ", addr, and(compl(mask), 255), val }' pollute.map)
tab_got=$(python3 "$DUMPBYTES" pollute.elf 0xff8200 6 | tr -s ' ' | sed 's/ $//')
tab_expect=$(printf '%s' "$tab_expect" | tr -s ' ' | sed 's/ $//')
[ "$tab_expect" = "$tab_got" ] || {
  echo "FAIL: BITINIT table bytes '$tab_got' != map-derived '$tab_expect'" >&2; exit 1; }
echo "table: $tab_got == {addr,~mask,value} from the map (ROM content non-zero, applied at runtime)"

# --- pollution image: 16 bytes A5/5A at IRAM 0x20, as ihex -----------------
# (binary loader files are rejected on this machine: ram_size is 0; the ihex
#  path of -device loader writes by record address at reset, before boot.)
python3 - <<'EOF'
data = bytes([0xA5, 0x5A] * 8)
def rec(addr, typ, payload):
    line = bytes([len(payload), (addr >> 8) & 0xFF, addr & 0xFF, typ]) + payload
    return ':' + line.hex().upper() + '%02X' % ((-sum(line)) & 0xFF)
open('pollution.hex', 'w').write('\n'.join([
    rec(0x0020, 0x00, data), rec(0x0000, 0x01, b'')]) + '\n')
EOF
pollution=$(python3 -c "print(bytes([0xA5,0x5A]*8).hex(' '))")
echo "pollution: 16B A5/5A at 0x20 via -device loader (ihex; reset keeps RAM)"

# --- run in QEMU ------------------------------------------------------------
"$OBJCOPY" -O ihex pollute.elf pollute.hex
: > serial.out
# QEMU runs as this script's own background child.  The watcher below signals
# exactly that PID when the firmware's PASS marker appears (a global
# `pkill -f` could terminate another concurrently running pollute test), and
# it enforces the QEMU_TIMEOUT bound itself: after the loop it TERMs, then
# KILLs, only this instance.
"$QEMU" -M "$MACHINE" \
  -bios pollute.hex -device loader,file=pollution.hex \
  -accel tcg -display none -monitor none \
  -serial file:serial.out >/dev/null 2>&1 &
qemu_pid=$!
( for i in $(seq 1 $((QEMU_TIMEOUT * 2))); do
    sleep 0.5
    kill -0 "$qemu_pid" 2>/dev/null || exit 0
    if grep -q 'BT14-POLLUTE-PASS' serial.out 2>/dev/null; then
      kill -TERM "$qemu_pid" 2>/dev/null || true
      exit 0
    fi
  done
  kill -TERM "$qemu_pid" 2>/dev/null || true
  sleep 1
  kill -KILL "$qemu_pid" 2>/dev/null || true ) &
watcher=$!
wait "$qemu_pid" 2>/dev/null || true
kill "$watcher" 2>/dev/null || true
wait "$watcher" 2>/dev/null || true
grep -q 'BT14-POLLUTE-PASS' serial.out || {
  echo "FAIL: firmware did not complete:" >&2; cat serial.out >&2; exit 1; }

# --- host oracle: expected transcript FROM THE MAP + pollution -------------
python3 - "$pollution" <<'EOF' > expected.txt
import sys
pollution = bytes.fromhex(sys.argv[1].replace(' ', ''))
# mask/value per byte from the link map (the same rows asserted above)
rows = {}
for ln in open('pollute.map'):
    p = ln.split()
    if p and p[0] == 'BITBYTE' and 'bit-rmw' in ln:
        rows[int(p[1], 16)] = (int(p[3], 16), int(p[5], 16))
out = []
for i in range(16):
    addr = 0x20 + i
    b = pollution[i]
    if addr in rows:
        mask, val = rows[addr]
        b = (b & ~mask & 0xFF) | (val & mask)
    out.append('B%02x=%02x' % (addr, b))
full = rows[0x20][1]; part = rows[0x21][1]
# X: the cross-read bit a0; F: bit 0 of the fixed-sbit byte 0x2A (no record:
# pure pollution).  a0's declared init is PARSED from its BIT row so the
# oracle genuinely derives it from the map: a missing row, absent init field
# or a non-numeric value is a hard error (the old hardcoded default of 1
# could never disagree with a missing/zeroed entry).
a0_init = None
for ln in open('pollute.map'):
    p = ln.split()
    if len(p) >= 5 and p[0] == 'BIT' and p[1] == '_a0':
        i = p.index('init')
        a0_init = int(p[i + 1], 10)
if a0_init is None or a0_init not in (0, 1):
    sys.stderr.write('oracle: no parseable BIT _a0 init row in pollute.map\n')
    sys.exit(1)
fx_byte = pollution[0x2A - 0x20]
out += ['L=%02x' % full, 'P=%02x' % part,
        'X=%02x' % a0_init, 'F=%02x' % (fx_byte & 1),
        'BT14-POLLUTE-PASS']
sys.stdout.write('\n'.join(out) + '\n')
EOF
if cmp -s serial.out expected.txt; then
  echo "transcript: byte-exact match with the map-derived oracle"
else
  echo "FAIL: transcript != map-derived oracle:" >&2
  diff expected.txt serial.out >&2 || true
  exit 1
fi

# --- rejection-power self-test: flipping ONE value digit in ONE oracle line
# --- must be rejected by the same byte-exact comparison.  The corruption is
# --- length-preserving (same line count, same line lengths, same layout) so
# --- rejection proves value sensitivity, not a length/garbage artifact.
python3 - <<'EOF'
import re
src = open('expected.txt').read()
lines = src.split('\n')
for i, ln in enumerate(lines):
    m = re.fullmatch(r'B([0-9a-f]{2})=([0-9a-f]{2})', ln)
    if m:
        v = int(m.group(2), 16) ^ 0x01  # one-bit flip, same two hex digits
        lines[i] = 'B%s=%02x' % (m.group(1), v)
        open('inject.changed', 'w').write('%s -> %s\n' % (ln, lines[i]))
        break
else:
    raise SystemExit('injector: no B<xx>=<hh> line in expected.txt')
out = '\n'.join(lines)
if out == src:
    raise SystemExit('injector: flip produced an identical file')
open('inject.tmp', 'w').write(out)
EOF
[ "$(wc -c < inject.tmp)" -eq "$(wc -c < expected.txt)" ] || {
  echo "FAIL: injector changed the oracle length" >&2; exit 1; }
if cmp -s serial.out inject.tmp; then
  echo "FAIL: a corrupted oracle was accepted (checker has no rejection power)" >&2
  exit 1
fi
echo "injection self-test: single-value flip rejected ($(cat inject.changed))"

echo "BT14-POLLUTE-E2E: PASS (bit window initialized under polluted RAM;"
echo "  neighbours preserved; no-record bytes untouched; bit view == byte view)"
case "$KEEP" in 1) echo "kept: $OUT";; *) rm -rf -- "$OUT";; esac
