#!/usr/bin/env bash
# run.sh - end-to-end E4 acceptance demo for the asm->ELF standard path.
#
#   liba.asm --sdas251--> liba.rel --sdrel2elf.py--> liba.o --\
#   libb.asm --sdas251--> libb.rel --sdrel2elf.py--> libb.o ---+--> mcs251-lld --> demo.elf
#   cunit.c  --clang/llc-->  cunit.o -------------------------/
#   cabi.c   --clang/llc-->  cabi.o  -------------------------/
#
# The link is non-IRQ (see README.md for why asm objects cannot carry ISR
# metadata), uses --keep-symbols for traceability, and is followed by
# check.py static assertions against the final image.  cabi.c is the
# independent ABI oracle: a compiler-produced C caller of c_add whose
# _c_add_PARM_2 slot stores check.py cross-checks against liba.asm's
# hand-written stores (see cabi.c and check.py section 8).
set -euo pipefail
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
A2ELF=$(cd -- "$HERE/.." && pwd)
OUT=${1:?usage: run.sh <output-dir>}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)

SDAS=${SDAS:-/home/liu/build-sdcc/bin/sdas251}
CLANG=${CLANG:-/home/liu/build-mcs251-s1/bin/clang}
LLC=${LLC:-/home/liu/build-mcs251-s1/bin/llc}
LLD=${LLD:-/home/liu/build-mcs251-lld/bin/lld}
CONVERT="$A2ELF/sdrel2elf.py"

echo "== assemble hand-written objects with sdas251 =="
"$SDAS" -los "$OUT/liba.rel" "$HERE/liba.asm"
"$SDAS" -los "$OUT/libb.rel" "$HERE/libb.asm"

echo "== convert .rel -> ELF32 ET_REL (sdrel2elf.py) =="
# --source-mode is a mandatory declaration: the XH3 header does not record
# the source/binary opcode map, so the converter cannot detect it and the
# caller must state that these modules use the default source encoding.
python3 "$CONVERT" "$OUT/liba.rel" -o "$OUT/liba.o" --source-mode source
python3 "$CONVERT" "$OUT/libb.rel" -o "$OUT/libb.o" --source-mode source \
  --dump > "$OUT/libb.dump"

echo "== compile the C objects =="
"$CLANG" --target=mcs251-unknown-none -std=c11 -O2 -Wall -Wextra -Werror \
  -Xclang -mcs251-memory-contract=1,1,32,8,1 -S -emit-llvm \
  "$HERE/cunit.c" -o "$OUT/cunit.ll"
"$LLC" -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 \
  -verify-machineinstrs -mcs251-object-format=elf -filetype=obj \
  "$OUT/cunit.ll" -o "$OUT/cunit.o"
# The independent ABI oracle (see cabi.c): compiled by the current
# toolchain, never hand-written.
"$CLANG" --target=mcs251-unknown-none -std=c11 -O2 -Wall -Wextra -Werror \
  -Xclang -mcs251-memory-contract=1,1,32,8,1 -S -emit-llvm \
  "$HERE/cabi.c" -o "$OUT/cabi.ll"
"$LLC" -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 \
  -verify-machineinstrs -mcs251-object-format=elf -filetype=obj \
  "$OUT/cabi.ll" -o "$OUT/cabi.o"

echo "== link everything with mcs251-lld =="
"$LLD" -flavor mcs251 "$OUT/liba.o" "$OUT/libb.o" "$OUT/cunit.o" \
  "$OUT/cabi.o" \
  --area-start=CSEG=0xff0000 --area-start=DSEG=0x30 --area-start=XSEG=0x12000 \
  --area-start=XINIT=0xff8000 --edata-end=0x0fff \
  --flash-base=0xff0000 --flash-size=0x10000 \
  --keep-symbols --map="$OUT/demo.map" -o "$OUT/demo.elf"

echo "== static assertions (check.py) =="
python3 "$HERE/check.py" "$OUT"

echo "E4 demo PASS (artifacts in $OUT)"
