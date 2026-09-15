#!/usr/bin/env bash
# g3-demo61-four-tier.sh - G3 acceptance: demo 61 target program group
# (TFT320240-I8080) through the four tiers, on the v2 contract
# (1,2,32,8,1), per G3-STATIC-PTR-DESIGN-draft.md sections 5/5.1/6.2 and
# the Alice-approved completion criterion "61 target program group
# four-tier green".
#
# VERIFICATION LEVEL: real compilation + real execution.  The program is
# compiled by clang (v2 contract, static-pointer slot parameters), lowered
# to an ELF object by llc (identity e_flags 0x102 + .mcs251.attributes
# carrier), linked by mcs251-lld against the v2 IRQ CRT (gen-crt-v2.sh
# --identity v2, byte-checked by the generator's own smoke assertions) and
# EXECUTED under qemu-system-mcs251 (stc32g144k246, TCG).  No yaml
# stand-in for any firmware object.
#
# Tiers asserted (never merged):
#   T1 clang  LCM_Test.c compiles under the v2 contract, clean of errors.
#   T2 llc    the object carries the v2 identity (e_flags 0x102, exactly one
#             .mcs251.attributes, no v1 note) and the static-pointer slot
#             contract of the demo's signatures: _Show_Str_PARM_2..7 are
#             2/2/2/4/1/1-byte OBJECT GLOBAL slots with the 5th-parameter
#             pointer slot _Show_Str_PARM_5 exactly 4 bytes (AS0 generic
#             pointer), per design section 3.1.
#   T3 lld    the link with crt-irq-v2.o + the v2 div/mod runtime succeeds;
#             the map shows the slot regions allocated (.mcs251.DSEG.7
#             hosting _Show_Str_PARM_5's region for LCM_Test.o) and the
#             vector table synthesizing IRQ 58/59 to _LCM_DMA_Interrupt /
#             _LCM_Interrupt; the E2 reentrancy diagnostic emits NOTHING
#             (the static-slot functions are foreground-only).
#   T4 QEMU   the image boots through the CRT into _main at the map's text
#             base and makes forward progress into the LCM peripheral poll
#             loop (documented model boundary: QEMU has no LCM model, so
#             the firmware parks on the first LCMIFSTA poll).  The serial
#             transcript is EMPTY - this program contains no UART code at
#             all, so silence is the expected outcome, never a failure.
#             Both the main-entry execution and the poll loop are asserted
#             from the execution trace (-d in_asm: a TB is translated on
#             first execution), not from the image alone.
#
# Secondary (same mechanism, not the gated target): the LCD1602-M6800
# program group is compiled and linked the same way and its map is checked
# for the IRQ 58 handler line, recorded honestly in the summary.
#
# Tools (override via env):
#   CLANG    /home/liu/build-mcs251-s1/bin/clang   (v2 contract support)
#   LLC      /home/liu/build-mcs251/bin/llc
#   LLD      /home/liu/build-mcs251-lld/bin/mcs251-lld
#   YAML2OBJ /home/liu/build-mcs251-lld/bin/yaml2obj
#   READOBJ/READELF/OBJCOPY  the s1 tree (read-only)
#   QEMU     /home/liu/build-qemu/qemu-system-mcs251
#
# Usage: g3-demo61-four-tier.sh [--keep]
#   Output directory defaults to /tmp (G3 review fix, Alice R5: earlier
#   rounds wrote into the main tree's validation/mcs251-isr/build/; the
#   script must not write into the MCS251 tree).  Override via G3_OUT.
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "$0")" && pwd)
OUT="${G3_OUT:-/tmp/g3-demo61-four-tier}"
DEMO="/home/liu/LLVM_STC32/mcs251-demos-rewritten/src/61-DMA-LCM液晶屏接口测试"
TFT_DIR="$DEMO/TFT320240显示程序-硬件I8080并行接口+DMA刷新"
TFT_SRC="$TFT_DIR/LCM_Test.c"
LCD_DIR="$DEMO/LCD1602显示程序-硬件M6800并行接口+DMA刷新"
LCD_SRC="$LCD_DIR/LCD1602-LCM-DMA.c"
REPO="/home/liu/LLVM_STC32/MCS251"

CLANG=${CLANG:-/home/liu/build-mcs251-s1/bin/clang}
LLC=${LLC:-/home/liu/build-mcs251/bin/llc}
LLD=${LLD:-/home/liu/build-mcs251-lld/bin/mcs251-lld}
YAML2OBJ=${YAML2OBJ:-/home/liu/build-mcs251-lld/bin/yaml2obj}
READOBJ=${READOBJ:-/home/liu/build-mcs251-s1/bin/llvm-readobj}
READELF=${READELF:-/home/liu/build-mcs251-s1/bin/llvm-readelf}
OBJCOPY=${OBJCOPY:-/home/liu/build-mcs251-s1/bin/llvm-objcopy}
QEMU=${QEMU:-/home/liu/build-qemu/qemu-system-mcs251}
MACHINE=${MACHINE:-stc32g144k246}
GEN_CRT_V2="$REPO/validation/mcs251-elf/runtime/gen-crt-v2.sh"
RT_SRC="$REPO/llvm/lib/Target/MCS251/Runtime"
QEMU_TIMEOUT=${QEMU_TIMEOUT:-12}

# The v2 contract - the only contract under which static pointer parameters
# are lowered (design section 2.1: the ISel gate keys on ProgramAS==4).
CONTRACT=1,2,32,8,1

INC="-I$TFT_DIR -I$REPO/validation/mcs251-dialect/include \
-I$REPO/validation/mcs251-porting/generated -I$REPO/validation/mcs251-porting/include"

AREAS="--area-start=HOME=0xff0000 --area-start=VECS=0xff0003 \
--area-start=BOOT=0xff0500 --area-start=CSEG=0xff0700 \
--area-start=XINIT=0xff8000 --area-start=XDATA_INIT=0xff9000 \
--area-start=XSEG=0x010000 --edata-end=0x3fff"

for t in "$CLANG" "$LLC" "$LLD" "$YAML2OBJ" "$READOBJ" "$READELF" \
         "$OBJCOPY" "$QEMU"; do
  command -v "$t" >/dev/null || { echo "FAIL: missing tool: $t" >&2; exit 1; }
done
[ -f "$GEN_CRT_V2" ] || { echo "FAIL: missing $GEN_CRT_V2" >&2; exit 1; }
[ -f "$TFT_SRC" ] || { echo "FAIL: missing $TFT_SRC" >&2; exit 1; }
[ -f "$LCD_SRC" ] || { echo "FAIL: missing $LCD_SRC" >&2; exit 1; }

rm -rf -- "$OUT"
mkdir -p -- "$OUT"

# --- T1: clang (v2 contract) ---------------------------------------------
# shellcheck disable=SC2086
"$CLANG" --target=mcs251-unknown-none -std=c11 -O0 -fmcs251-keil \
  -Wall -Wextra -Wno-implicit-int-conversion -Wno-shorten-64-to-32 \
  -Wno-unused-but-set-parameter -Wno-unused-variable -Wno-uninitialized \
  -Wno-incompatible-pointer-types -Wno-int-conversion \
  -Wno-constant-conversion $INC \
  -Xclang -mcs251-memory-contract="$CONTRACT" \
  -S -emit-llvm "$TFT_SRC" -o "$OUT/LCM_Test.ll" > "$OUT/t1.log" 2>&1 || {
  echo "FAIL[T1]: clang rejected LCM_Test.c under $CONTRACT" >&2
  grep -E "error:|fatal" "$OUT/t1.log" | head -5 >&2; exit 1; }
if grep -qE "error:|fatal error" "$OUT/t1.log"; then
  echo "FAIL[T1]: clang reported errors despite rc=0" >&2
  grep -E "error:|fatal" "$OUT/t1.log" | head -5 >&2; exit 1
fi
echo "T1 clang: LCM_Test.c lowered to IR under contract $CONTRACT"

# --- T2: llc object identity + slot contract ------------------------------
"$LLC" -mtriple=mcs251 -mcs251-memory-contract="$CONTRACT" \
  -mcs251-object-format=elf -filetype=obj "$OUT/LCM_Test.ll" \
  -o "$OUT/LCM_Test.o" > "$OUT/t2.log" 2>&1 || {
  echo "FAIL[T2]: llc rejected the object" >&2; head -5 "$OUT/t2.log" >&2
  exit 1; }

HDR="$("$READELF" -h "$OUT/LCM_Test.o")"
printf '%s\n' "$HDR" | grep -q "Flags:                             0x102" || {
  echo "FAIL[T2]: e_flags is not the v2 word 0x102:" >&2
  printf '%s\n' "$HDR" | grep Flags >&2; exit 1; }
SECS="$("$READOBJ" --sections "$OUT/LCM_Test.o")"
CARRIERS="$(printf '%s\n' "$SECS" | grep -c "Name: .mcs251.attributes" || true)"
NOTES="$(printf '%s\n' "$SECS" | grep -c "Name: .note.mcs251.abi" || true)"
[ "$CARRIERS" = "1" ] || { echo "FAIL[T2]: expected exactly one .mcs251.attributes, saw $CARRIERS" >&2; exit 1; }
[ "$NOTES" = "0" ] || { echo "FAIL[T2]: object carries a v1 .note.mcs251.abi" >&2; exit 1; }

# _Show_Str slot contract: sizes 2,2,2,4,1,1; the 4-byte one is the 5th
# source parameter's AS0 generic pointer slot (design section 3.1).
sym_line() { # <obj> <name> -> "size OBJECT GLOBAL section name"
  "$READELF" -s "$1" | awk -v n="$2" '$8 == n { print $3, $4, $5, $7, $8; exit }'
}
for pair in "2 2" "3 2" "4 2" "5 4" "6 1" "7 1"; do
  n=${pair%% *}; sz=${pair##* }
  got=$(sym_line "$OUT/LCM_Test.o" "_Show_Str_PARM_$n")
  [ "$got" = "$sz OBJECT GLOBAL 7 _Show_Str_PARM_$n" ] || {
    echo "FAIL[T2]: _Show_Str_PARM_$n is '$got', expected '$sz OBJECT GLOBAL 7'" >&2
    exit 1; }
done
echo "T2 llc: e_flags 0x102, one carrier, no v1 note; _Show_Str_PARM_2..7 = 2/2/2/4/1/1B slots (5th param ptr = 4B AS0)"

# --- T3: lld with the v2 IRQ CRT and the v2 runtime -----------------------
bash "$GEN_CRT_V2" --yaml2obj "$YAML2OBJ" --readobj "$READOBJ" \
  --variant irq --identity v2 --out "$OUT/crt-irq-v2.o" >/dev/null
mkdir -p "$OUT/rt"
for f in divuint divulong divsint divslong moduint modulong modsint modslong; do
  "$CLANG" --target=mcs251-unknown-none -std=c11 -ffreestanding \
    -fno-builtin -DMCS251_RT_TARGET -O0 \
    -Xclang -mcs251-memory-contract="$CONTRACT" \
    -S -emit-llvm "$RT_SRC/mcs251rt_$f.c" -o "$OUT/rt/$f.ll" 2>/dev/null
  "$LLC" -mtriple=mcs251 -mcs251-memory-contract="$CONTRACT" -O0 \
    -verify-machineinstrs -mcs251-object-format=elf -filetype=obj \
    "$OUT/rt/$f.ll" -o "$OUT/rt/$f.o" 2>/dev/null
done
# shellcheck disable=SC2086
"$LLD" -flavor mcs251 $AREAS --map="$OUT/demo.map" -o "$OUT/demo.elf" \
  "$OUT/LCM_Test.o" "$OUT/rt"/*.o "$OUT/crt-irq-v2.o" \
  > "$OUT/t3.log" 2> "$OUT/t3.err" || {
  echo "FAIL[T3]: link failed" >&2; head -5 "$OUT/t3.log" "$OUT/t3.err" >&2
  exit 1; }
[ -s "$OUT/demo.elf" ] || { echo "FAIL[T3]: no output image" >&2; exit 1; }

grep -q "LCM_Test.o:.mcs251.DSEG.7 0x0008" "$OUT/demo.map" || {
  echo "FAIL[T3]: map does not place LCM_Test.o's slot region DSEG.7" >&2
  grep DSEG "$OUT/demo.map" >&2; exit 1; }
grep -q "IRQ 59 0xff01db ISR _LCM_Interrupt" "$OUT/demo.map" || {
  echo "FAIL[T3]: map does not synthesize IRQ 59 -> _LCM_Interrupt" >&2; exit 1; }
grep -q "IRQ 58 0xff01d3 ISR _LCM_DMA_Interrupt" "$OUT/demo.map" || {
  echo "FAIL[T3]: map does not synthesize IRQ 58 -> _LCM_DMA_Interrupt" >&2; exit 1; }
if grep -q "reentrancy" "$OUT/t3.err"; then
  echo "FAIL[T3]: E2 reentrancy diagnostic fired (slot functions are foreground-only; review)" >&2
  grep "reentrancy" "$OUT/t3.err" >&2; exit 1
fi
echo "T3 lld: v2 CRT link OK; DSEG.7 slot region placed; IRQ 58/59 synthetic ISR rows; E2 silent"

# --- T4: QEMU - boot through CRT into _main, then the LCM model boundary --
"$OBJCOPY" -O ihex "$OUT/demo.elf" "$OUT/demo.hex"
: > "$OUT/demo.serial"
set +e
timeout --foreground "$QEMU_TIMEOUT" "$QEMU" -M "$MACHINE" \
  -bios "$OUT/demo.hex" -accel tcg -display none -monitor none \
  -serial "file:$OUT/demo.serial" -d in_asm -D "$OUT/trace.log" \
  > "$OUT/qemu.out" 2> "$OUT/qemu.err"
RC=$?
set -e
[ "$RC" = "124" ] || {
  echo "FAIL[T4]: QEMU exited early with rc=$RC (crash or init failure)" >&2
  head -5 "$OUT/qemu.err" >&2; exit 1; }
[ -s "$OUT/trace.log" ] || { echo "FAIL[T4]: empty execution trace" >&2; exit 1; }

# main entry = text base (map s_CSEG); _main is the first function in the
# demo's .text, so its entry equals the section base.
MAIN_PC=$(awk '/^s_CSEG = / { print $3 }' "$OUT/demo.map")
[ -n "$MAIN_PC" ] || { echo "FAIL[T4]: no s_CSEG in map" >&2; exit 1; }
TRACE_PC="0x00$(printf '%06x' "$MAIN_PC")"
grep -q "^${TRACE_PC}:" "$OUT/trace.log" || {
  echo "FAIL[T4]: main entry ($TRACE_PC) never executed" >&2
  echo "  (executed range sample:)" >&2
  grep -o "^0x[0-9a-f]*:" "$OUT/trace.log" | sort -u | head -5 >&2
  exit 1; }
# LCMIFSTA (0x7efe53) poll: the first unmodelled-peripheral boundary.  The
# compiler materializes the literal 0x7efe53 as `mov dr0,#0xfe53` +
# `movh dr0,#0x007e` immediately before the `mov r0,@dr0` poll read; a TB is
# translated on first execution, so the pair's presence proves the poll ran.
grep -q "mov dr0,#0xfe53" "$OUT/trace.log" || {
  echo "FAIL[T4]: never reached the LCMIFSTA poll (0x7efe53) - unexpected path" >&2
  exit 1; }
grep -q "movh dr0,#0x007e" "$OUT/trace.log" || {
  echo "FAIL[T4]: LCMIFSTA high half (0x007e) never materialized" >&2
  exit 1; }
[ -s "$OUT/demo.serial" ] && {
  echo "FAIL[T4]: unexpected serial output (program has no UART code):" >&2
  head -3 "$OUT/demo.serial" >&2; exit 1; }
echo "T4 qemu: CRT->main executed at $MAIN_PC; parked on the LCMIFSTA poll (no LCM model - documented boundary); serial empty (no UART in program), no crash"

# --- secondary: LCD1602-M6800 group (same mechanism, recorded, not gated) -
"$CLANG" --target=mcs251-unknown-none -std=c11 -O0 -fmcs251-keil \
  -Wall -Wextra -Wno-implicit-int-conversion -Wno-shorten-64-to-32 \
  -Wno-unused-but-set-parameter -Wno-unused-variable -Wno-uninitialized \
  -Wno-incompatible-pointer-types -Wno-int-conversion \
  -Wno-constant-conversion -I"$LCD_DIR" \
  -I$REPO/validation/mcs251-dialect/include \
  -I$REPO/validation/mcs251-porting/generated \
  -I$REPO/validation/mcs251-porting/include \
  -Xclang -mcs251-memory-contract="$CONTRACT" \
  -S -emit-llvm "$LCD_SRC" -o "$OUT/LCD1602.ll" > "$OUT/lcd.clang.log" 2>&1
"$LLC" -mtriple=mcs251 -mcs251-memory-contract="$CONTRACT" \
  -mcs251-object-format=elf -filetype=obj "$OUT/LCD1602.ll" \
  -o "$OUT/LCD1602.o" > "$OUT/lcd.llc.log" 2>&1
# shellcheck disable=SC2086
"$LLD" -flavor mcs251 $AREAS --map="$OUT/lcd1602.map" \
  -o "$OUT/lcd1602.elf" "$OUT/LCD1602.o" "$OUT/rt"/*.o "$OUT/crt-irq-v2.o" \
  > "$OUT/lcd.link.log" 2>&1
grep -q "IRQ 58 .* ISR _LCMIF_DMA_ISR" "$OUT/lcd1602.map" || {
  echo "FAIL[sec]: LCD1602 map lacks the IRQ 58 ISR row" >&2; exit 1; }
echo "secondary: LCD1602-M6800 group T0/T1 OK (IRQ 58 ISR row present)"

echo "G3-DEMO61-FOUR-TIER: PASS (target group TFT320240-I8080; raw artifacts in $OUT)"
