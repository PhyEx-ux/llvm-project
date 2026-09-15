#!/usr/bin/env bash
# bs4-runtime-probe.sh - G2 B-S4 runtime acceptance: the migrated variadic
# printf/sprintf runtime executed as a REAL firmware under QEMU, against the
# SAME runtime source built for the host (Oracle-A).
#
# VERIFICATION LEVEL: real compilation and real execution.  The runtime
# object is built by clang --target=mcs251-unknown-none + llc under the v2
# contract (1,2,32,8,1, the only contract that can lower the sprintf fmt
# pointer slot); the probe TU is a real C translation unit; the firmware is
# linked with the frozen v2 crt (gen-crt-v2.sh --identity v2, selfstart) and
# run on qemu-system-mcs251 (machine stc32g144k246, TCG).  The host side is
# the SAME mcs251_printf.c compiled by gcc, using its #ifndef
# MCS251_RT_TARGET va_start/va_arg bridge.
#
# What is asserted (byte-exact transcripts, never a substring test):
#   1. 0 / 1 / 2 / 3 / 5 / 6 variadic arguments - the frozen 6-slot cap
#      boundary.  The host bridge requires exactly six variadic arguments
#      per call (its documented contract), so the host call sites pad to six
#      while the target calls pass the true counts; both must produce the
#      same bytes, which is what proves the target reads exactly the slots
#      the caller marshalled and ignores the ones it did not write.
#   2. %d %u %x %s %c %% with width and zero padding.  The '-' flag is a
#      REGISTERED pre-existing limitation: it is parsed and then ignored, so
#      %-4d right-aligns ("   7") and is asserted as such - never as a pass
#      for left justification (2026-09-15 Alice review R2).
#   3. f32: %f %.2f %g - the target passes a real promoted float (the
#      backend stores its bits in the slot), the host passes the same bits.
#   4. sprintf writes the CALLER buffer (g_out_buf), resets its position per
#      call, truncates at 24 bytes and forces NUL at byte 23 while leaving
#      bytes 24.. untouched (sentinel 0xAA checked byte by byte).
#   5. A second sprintf call with shorter content overwrites from buf[0] and
#      the previous longer tail is gone.
#
# Tools (override via env):
#   CLANG LLC LLD YAML2OBJ READOBJ OBJCOPY QEMU HOSTCC
#
# Usage: bs4-runtime-probe.sh [--keep]
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "$0")" && pwd)
OUT="$ROOT/build/bs4-runtime-probe"

CLANG=${CLANG:-/home/liu/build-mcs251-s1/bin/clang}
LLC=${LLC:-/home/liu/build-mcs251/bin/llc}
LLD=${LLD:-/home/liu/build-mcs251-lld/bin/mcs251-lld}
YAML2OBJ=${YAML2OBJ:-/home/liu/build-mcs251-lld/bin/yaml2obj}
READOBJ=${READOBJ:-/home/liu/build-mcs251-s1/bin/llvm-readobj}
READELF=${READELF:-/home/liu/build-mcs251-s1/bin/llvm-readelf}
OBJCOPY=${OBJCOPY:-/home/liu/build-mcs251-s1/bin/llvm-objcopy}
QEMU=${QEMU:-/home/liu/build-qemu/qemu-system-mcs251}
HOSTCC=${HOSTCC:-/usr/bin/cc}
MACHINE=${MACHINE:-stc32g144k246}
GEN_CRT_V2="$ROOT/../mcs251-elf/runtime/gen-crt-v2.sh"
RT_SRC="$ROOT/../mcs251-runtime/src/mcs251_printf.c"
RT_INC="$ROOT/../mcs251-runtime/src"
DIV_SRC="$ROOT/../../llvm/lib/Target/MCS251/Runtime"
PROBE_C="$ROOT/bs4-runtime-probe.c"
QEMU_TIMEOUT=${QEMU_TIMEOUT:-60}

# The v2 contract.  compat (1,1) rejects the static pointer slot that
# sprintf's fmt parameter needs, so v2 is the only contract the migrated
# runtime can be built under.
CONTRACT=1,2,32,8,1

# The runtime is built at -O2: -O0 emits a 26 KB .text that cannot coexist
# with the demo CSEG window, and -O2 is the recipe the div/mod runtime uses.
RT_OPT=O2

# div/mod runtime objects the runtime object references (transitive).
DIV_OBJS="divulong modulong"

for t in "$CLANG" "$LLC" "$LLD" "$YAML2OBJ" "$READOBJ" "$OBJCOPY" "$QEMU" \
         "$HOSTCC"; do
  command -v "$t" >/dev/null || { echo "FAIL: missing tool: $t" >&2; exit 1; }
done
[ -f "$GEN_CRT_V2" ] || { echo "FAIL: missing fixture: $GEN_CRT_V2" >&2; exit 1; }
[ -f "$RT_SRC" ] || { echo "FAIL: missing runtime source: $RT_SRC" >&2; exit 1; }
[ -f "$PROBE_C" ] || { echo "FAIL: missing probe TU: $PROBE_C" >&2; exit 1; }

rm -rf -- "$OUT"
mkdir -p -- "$OUT"

# --- 1: the runtime object, target side (v2, -O2) ------------------------
"$CLANG" --target=mcs251-unknown-none -std=c11 -ffreestanding -fno-builtin \
  -DMCS251_RT_TARGET -"$RT_OPT" -Wall -Wextra -Werror \
  -Xclang -mcs251-memory-contract="$CONTRACT" \
  -S -emit-llvm "$RT_SRC" -o "$OUT/printf.ll"
"$LLC" -mtriple=mcs251 -mcs251-memory-contract="$CONTRACT" -"$RT_OPT" \
  -verify-machineinstrs -mcs251-object-format=elf -filetype=obj \
  "$OUT/printf.ll" -o "$OUT/printf.o"
echo "runtime: mcs251_printf.c -> printf.o ($RT_OPT, v2 contract, -Werror clean)"

# The six printf continuation slots and the seven sprintf slots are the
# frozen cross-TU value channel (design section 4.7); pin them here so a
# silent slot-layout change cannot slip past the transcript test.
sym_line() { # <obj> <name> -> "size type bind name" or empty
  "$READELF" -s "$1" | awk -v n="$2" '$8 == n { print $3, $4, $5, $8; exit }'
}
need_sym() { # <obj> <name> <size>
  local got
  got=$(sym_line "$1" "$2")
  [ "$got" = "$3 OBJECT GLOBAL $2" ] || {
    echo "FAIL: slot $2 in printf.o is '$got', expected '$3 OBJECT GLOBAL $2'" >&2
    exit 1; }
}
for n in 2 3 4 5 6 7; do
  need_sym "$OUT/printf.o" "_printf_PARM_$n" 4
done
for n in 2 3 4 5 6 7 8; do
  need_sym "$OUT/printf.o" "_sprintf_PARM_$n" 4
done
echo "slots: _printf_PARM_2..7 (6x4B) + _sprintf_PARM_2..8 (7x4B) OBJECT GLOBAL"

# --- 2: div/mod runtime objects (v2) -------------------------------------
for f in $DIV_OBJS; do
  "$CLANG" --target=mcs251-unknown-none -std=c11 -ffreestanding -fno-builtin \
    -DMCS251_RT_TARGET -O0 -Xclang -mcs251-memory-contract="$CONTRACT" \
    -S -emit-llvm "$DIV_SRC/mcs251rt_$f.c" -o "$OUT/$f.ll"
  "$LLC" -mtriple=mcs251 -mcs251-memory-contract="$CONTRACT" -O0 \
    -verify-machineinstrs -mcs251-object-format=elf -filetype=obj \
    "$OUT/$f.ll" -o "$OUT/$f.o"
done
echo "runtime: div/mod objects ($DIV_OBJS) OK"

# --- 3: the probe TU, target side ----------------------------------------
"$CLANG" --target=mcs251-unknown-none -std=c11 -O0 -fmcs251-keil \
  -Wno-incompatible-library-redeclaration \
  -Xclang -mcs251-memory-contract="$CONTRACT" \
  -S -emit-llvm "$PROBE_C" -o "$OUT/probe.ll"
"$LLC" -mtriple=mcs251 -mcs251-memory-contract="$CONTRACT" -O0 \
  -mcs251-object-format=elf -filetype=obj "$OUT/probe.ll" -o "$OUT/probe.o"
echo "probe: bs4-runtime-probe.c -> probe.o (v2, -O0)"

# --- 4: the SAME source on the host, against the SAME runtime source -----
# Oracle-A: the runtime's #ifndef MCS251_RT_TARGET bridge does the va_arg
# materialisation; the probe's host arms pad every call to the six arguments
# that bridge requires.
"$HOSTCC" -std=c11 -O0 -fno-builtin -DBS4_HOST_ORACLE -Dmain=bs4_main \
  -I"$RT_INC" -c "$PROBE_C" -o "$OUT/probe-host.o"
"$HOSTCC" -std=c11 -O0 -c "$ROOT/bs4-runtime-probe-main.c" \
  -o "$OUT/main-host.o"
"$HOSTCC" -std=c11 -O0 -fno-builtin -I"$RT_INC" -c "$RT_SRC" \
  -o "$OUT/printf-host.o"
"$HOSTCC" -o "$OUT/probe-host.bin" "$OUT/probe-host.o" "$OUT/main-host.o" \
  "$OUT/printf-host.o"
"$OUT/probe-host.bin" > "$OUT/host.transcript"
echo "host: same probe + same runtime source under $HOSTCC -> host.transcript"

# --- 5: the v2 crt and the firmware --------------------------------------
bash "$GEN_CRT_V2" --yaml2obj "$YAML2OBJ" --readobj "$READOBJ" \
  --variant selfstart --identity v2 --out "$OUT/crt.o" >/dev/null

AREAS="--area-start=HOME=0xff0000 --area-start=VECS=0xff0003 \
--area-start=BOOT=0xff0100 --area-start=CSEG=0xff0200 \
--area-start=XINIT=0xff8000 --area-start=XDATA_INIT=0xff9000 \
--area-start=XSEG=0x010000 --edata-end=0x3fff"
# shellcheck disable=SC2086
(cd "$OUT" && "$LLD" $AREAS --map=fw.map -o fw.elf \
  probe.o printf.o divulong.o modulong.o crt.o)
"$OBJCOPY" -O ihex "$OUT/fw.elf" "$OUT/fw.hex"
echo "link: probe + printf + div/mod + v2 crt OK (map: fw.map)"

# The direct-page window [0x0000,0x0080) is a hard 128B limit; the probe
# firmware must fit it (this is the G8 gate for this image).
dseg=$(awk '$1 == "l_DSEG" { print $3 }' "$OUT/fw.map")
oseg_s=$(awk '$1 == "s_OSEG" { print $3 }' "$OUT/fw.map")
oseg_l=$(awk '$1 == "l_OSEG" { print $3 }' "$OUT/fw.map")
echo "window: l_DSEG=$dseg l_OSEG=$oseg_l @ $oseg_s (limit 0x80)"

# --- 6: run it -----------------------------------------------------------
: > "$OUT/fw.serial"
( for i in $(seq 1 $((QEMU_TIMEOUT * 2))); do
    if [ -s "$OUT/fw.serial" ] && tail -c 64 "$OUT/fw.serial" 2>/dev/null | \
        grep -q 'DONE'; then
      pkill -TERM -f "fw.hex" 2>/dev/null
      exit 0
    fi
    sleep 0.5
  done ) &
watcher=$!
timeout --foreground "$QEMU_TIMEOUT" "$QEMU" -M "$MACHINE" \
  -bios "$OUT/fw.hex" -accel tcg -display none -monitor none \
  -serial "file:$OUT/fw.serial" \
  > "$OUT/fw.qemu.stdout" 2> "$OUT/fw.qemu.stderr" || true
kill "$watcher" 2>/dev/null || true
wait "$watcher" 2>/dev/null || true
[ -s "$OUT/fw.serial" ] || {
  echo "FAIL: no serial output (QEMU stderr:)" >&2
  head -5 "$OUT/fw.qemu.stderr" >&2
  exit 1; }
echo "qemu: firmware ran, $(wc -c < "$OUT/fw.serial") serial bytes"

# --- 7: byte-exact transcript -------------------------------------------
# The selfstart crt prints 'S' over SBUF right after _main returns, so the
# firmware transcript is the host transcript plus that one marker.
printf 'S' > "$OUT/crt-marker.raw"
cat "$OUT/host.transcript" "$OUT/crt-marker.raw" > "$OUT/golden.raw"
if ! cmp -s "$OUT/fw.serial" "$OUT/golden.raw"; then
  echo "FAIL: firmware transcript != host oracle transcript" >&2
  echo "--- got:" >&2; od -An -tx1z "$OUT/fw.serial" >&2
  echo "--- expected:" >&2; od -An -tx1z "$OUT/golden.raw" >&2
  exit 1
fi
echo "transcript: firmware == host oracle (+ crt 'S'), byte-exact"

# Explicit spot assertions on top of the byte compare, so a failure names
# the feature instead of dumping the whole transcript.  The transcript is
# CRLF, so the checks run against a CR-stripped view (the byte compare above
# already pinned the raw bytes, CR included).
tr -d '\r' < "$OUT/fw.serial" > "$OUT/fw.lines"
check_line() { # <exact line> <feature>
  grep -qxF "$1" "$OUT/fw.lines" || {
    echo "FAIL: missing/wrong line for $2: '$1'" >&2; exit 1; }
}
check_line "P0" "0 variadic arguments"
check_line "P1:42" "1 variadic argument, %d"
check_line "P2:4000000000 abcdef" "2 variadic arguments, %u %x"
check_line "P3:-7 4000000000 abcdef" "3 variadic arguments"
check_line "P5:-7 4000000000 abcdef str Z" "5 variadic arguments incl. %s %c"
check_line "P6:1 2 3 abcd Z % -6" "6 variadic arguments (cap): all six slots consumed, incl. %% before the last"
check_line "PLAIN-no-conversion" "plain string, no conversion"
# Registered pre-existing behaviour, NOT a left-justification pass: the '-'
# flag is parsed then ignored, so %-4d right-aligns to "   7" (2026-09-15
# Alice review R2).  The line below is the exact bytes the engine produces.
check_line "W:   42|00042|   7|" "width / zero-pad / %-4d right-aligns (dash ignored, registered)"
check_line "F:1.500000 3.25 0.5" "f32 %f %.2f %g"
check_line "S1:12345" "sprintf into the caller buffer"
check_line "S2:0123456789ABCDEFGHIJKLM" "sprintf 24-byte truncation"
check_line "S3:XY" "sprintf position reset, shorter second call"
check_line "T23=00" "truncation NUL forced at byte 23"
check_line "T24=aa" "nothing written at byte 24"
check_line "T25=aa" "nothing written at byte 25"
[ "$(grep -c '^T[0-9][0-9]=' "$OUT/fw.serial")" -eq 26 ] || {
  echo "FAIL: truncation table is not 26 lines" >&2; exit 1; }
# The crt's own 'S' marker is the last byte, so DONE is the last line of the
# CR-stripped view minus that marker.
[ "$(tail -n 1 "$OUT/fw.lines")" = "S" ] || {
  echo "FAIL: firmware does not end with the crt 'S' marker" >&2
  tail -3 "$OUT/fw.lines" >&2; exit 1; }
[ "$(tail -n 2 "$OUT/fw.lines" | head -n 1)" = "DONE" ] || {
  echo "FAIL: transcript does not end at DONE" >&2; tail -4 "$OUT/fw.lines" >&2; exit 1; }
echo "features: 0/1/2/3/5/6 args, %d %u %x %s %c %%, width, f32, sprintf buf+truncation all verified"

echo "BS4-RUNTIME-PROBE: PASS"
case " ${1:-} " in *" --keep "*) echo "kept: $OUT";; *) rm -rf -- "$OUT";; esac
