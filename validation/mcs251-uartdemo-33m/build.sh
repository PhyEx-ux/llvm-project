#!/usr/bin/env bash
# Build mcs251-uartdemo-33m for STC32G12K128 (IRC=33.1776MHz, UART1=115200).
#
# Proven real-hw PASS chain (g12-xfrrw-build.sh / g12-hexdemo):
# clang(compat contract) -> llc(ELF) -> mcs251-lld(explicit ROM window gate)
# -> llvm-objcopy -> Intel HEX.  sdas251 is deliberately NOT used anywhere.
# Deltas vs the proven chain script (both required, see comments at the link
# step): --flash-base/--flash-size actually enable the ROM window gate, and
# --keep-symbols emits FUNC lines the encoding gate needs.
# The build finishes with check-encoding.py, a hard gate asserting 251
# SOURCE-mode encodings and the STC32G T2 register layout (T2H@0xD6/T2L@0xD7).
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "$0")" && pwd)
REPO="$ROOT/../.."
OUT="$ROOT/build"
NAME=uartdemo-33m

CLANG=/home/liu/build-mcs251-s1/bin/clang
LLC=/home/liu/build-mcs251-s1/bin/llc
LLD=/home/liu/build-mcs251-lld/bin/lld
OBJCOPY=/home/liu/build-mcs251-s1/bin/llvm-objcopy
YAML2OBJ=/home/liu/build-mcs251-s1/bin/yaml2obj
CONTRACT=1,1,32,8,1

rm -rf -- "$OUT"
mkdir -p -- "$OUT"

# 1) C -> LLVM IR (compat memory contract, proven flags)
"$CLANG" --target=mcs251-unknown-none -std=c11 -O2 -Wall -Wextra -Werror \
  -I"$REPO/validation/mcs251-porting/generated" \
  -Xclang -mcs251-memory-contract="$CONTRACT" \
  -S -emit-llvm "$ROOT/src/uartdemo.c" -o "$OUT/$NAME.ll"

# 2) IR -> ELF object (251 source-mode machine code)
"$LLC" -mtriple=mcs251-unknown-none -verify-machineinstrs \
  -mcs251-memory-contract="$CONTRACT" -mcs251-object-format=elf -filetype=obj \
  "$OUT/$NAME.ll" -o "$OUT/$NAME.o"

# 3) frozen crt (selfstart BOOT + XINIT walker + default vectors)
"$YAML2OBJ" "$REPO/validation/mcs251-elf/runtime/crt-selfstart.yaml" \
  -o "$OUT/crt-selfstart.o"

# 4) division runtime objects (proven set/pattern from real-hw PASS
#    g12-divrt: mcs251rt_divulong.o + mcs251rt_modulong.o; these define
#    __divulong/__modulong and their _PARM_2 slots). Firmware's decimal
#    counter uses u32 div/rem -> libcalls.
RT=/home/liu/c23demo-work/rt
RT_DIV="$RT/mcs251rt_divulong.o"
RT_MOD="$RT/mcs251rt_modulong.o"

# 5) link (proven area starts).  The ROM window gate is OPT-IN in
#    lld/MCS251: without --flash-base AND --flash-size the linker applies NO
#    gate at all (Driver.cpp only sets Core.FlashGate when both are given;
#    LinkerCore.cpp checkFlashGate() returns early otherwise).  The proven
#    g12-xfrrw-build.sh did not pass them, so its "ROM gate" claim was empty.
#    Window values come from the ISR linker-test invocations
#    (validation/mcs251-isr: compile-matrix.py and
#    proposals/ISR-TASK-BREAKDOWN.md both link with
#    --flash-base=0xff0000 --flash-size=0x10000): 0xff0000..0xffffff covers
#    every area start used here (HOME 0xff0000, VECS 0xff0003, BOOT 0xff0100,
#    CSEG 0xff0200, XINIT 0xff8000).
#    --keep-symbols makes the map carry "FUNC addr +len name" lines; the
#    check-encoding.py gate decodes each function by exact instruction
#    boundaries (does not affect the firmware bytes / the .hex).
"$LLD" -flavor mcs251 --edata-end 0x0fff --keep-symbols \
  --flash-base=0xff0000 --flash-size=0x10000 \
  --area-start=HOME=0xff0000 --area-start=VECS=0xff0003 \
  --area-start=BOOT=0xff0100 --area-start=CSEG=0xff0200 \
  --area-start=XINIT=0xff8000 --map="$OUT/$NAME.map" \
  -o "$OUT/$NAME.elf" "$OUT/$NAME.o" "$RT_DIV" "$RT_MOD" "$OUT/crt-selfstart.o"

# 6) ELF -> Intel HEX for STC-ISP
"$OBJCOPY" -O ihex "$OUT/$NAME.elf" "$OUT/$NAME.hex"

cp -- "$OUT/$NAME.hex" "$ROOT/$NAME.hex"
cp -- "$OUT/$NAME.map" "$ROOT/$NAME.map"
printf 'built: %s and %s\n' "$ROOT/$NAME.hex" "$ROOT/$NAME.map"

# 7) HARD GATE: strict Intel HEX integrity, 251 source-mode instruction
#    boundary scan over BOOT + every FUNC symbol, T2@D6/D7 layout assertions
python3 "$ROOT/check-encoding.py" "$ROOT/$NAME.hex" "$ROOT/$NAME.map"
