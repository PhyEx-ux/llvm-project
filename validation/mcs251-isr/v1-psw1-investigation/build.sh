#!/usr/bin/env bash
# V1 PSW1 investigation fixture build.
# Usage: bash build.sh [new-empty-output-dir]
# Produces three images that share main.c and the assembly stages and differ
# ONLY in which Timer0 ISR is linked:
#   v1-baseline.hex  no ISR object at all (stages W and L only)
#   v1-minisr.hex    hand-written minimal ISR inside the asm module
#   v1-compiler.hex  the unmodified T10 compiler-generated ISR + helper
set -euo pipefail
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OUT=${1:-$(mktemp -d /tmp/v1-psw1.XXXXXX)}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
CLANG_EXTRA=""
if compgen -G "$OUT/*" >/dev/null; then printf 'output must be empty\n' >&2; exit 1; fi
CLANG=${CLANG:-/home/liu/build-mcs251-s1/bin/clang}
LLC=${LLC:-/home/liu/build-mcs251-s1/bin/llc}
if command -v /home/liu/build-mcs251-lld/bin/mcs251-lld >/dev/null 2>&1; then
  LLD=(${LLD:-/home/liu/build-mcs251-lld/bin/mcs251-lld})
else
  LLD=(${LLD:-/home/liu/build-mcs251-lld/bin/lld} -flavor mcs251)
fi
YAML2OBJ=${YAML2OBJ:-/home/liu/build-mcs251-s1/bin/yaml2obj}
OBJCOPY=${OBJCOPY:-/home/liu/build-mcs251-s1/bin/llvm-objcopy}
SDAS=${SDAS:-/home/liu/build-sdcc/bin/sdas251}
SDLD=${SDLD:-/home/liu/build-sdcc/bin/sdld}
CRT="$HERE/../../mcs251-elf/runtime/crt-irq.yaml"

link_one() { # arm obj1 [obj2 ...]
  local arm=$1; shift
  "${LLD[@]}" "$@" \
    --area-start=HOME=0xff0000 --area-start=BOOT=0xff0500 --area-start=CSEG=0xff0700 \
    --area-start=XINIT=0xff8000 --area-start=DSEG=0x30 --edata-end=0x0fff \
    --area-start=.mcs251.DATA.fixture=0x30 --area-start=.mcs251.DATA.result=0x100 \
    --area-start=.mcs251.DATA.teststack=0x580 \
    --flash-base=0xfe0000 --flash-size=0x20000 --map="$OUT/v1-$arm.map" \
    -o "$OUT/v1-$arm.elf"
  "$OBJCOPY" -O ihex "$OUT/v1-$arm.elf" "$OUT/v1-$arm.hex"
}

build_unit() { # name optlevel
  local unit=$1 opt=$2
  if "$CLANG" --target=mcs251-unknown-none -std=c11 $opt -Wall -Wextra -Werror $CLANG_EXTRA \
    -Xclang -mcs251-memory-contract=1,1,32,8,1 -S -emit-llvm \
    "$HERE/$unit.c" -o "$OUT/$unit.ll" 2>"$OUT/$unit-O2-failure.log" &&
    "$LLC" -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 \
      -verify-machineinstrs -mcs251-object-format=elf -filetype=obj \
      "$OUT/$unit.ll" -o "$OUT/$unit.o" 2>>"$OUT/$unit-O2-failure.log"; then
    :
  else
    printf '%s: -O2 failed (backend state), retrying with -O0\n' "$unit" >&2
    "$CLANG" --target=mcs251-unknown-none -std=c11 -O0 -Wall -Wextra -Werror \
      -Xclang -mcs251-memory-contract=1,1,32,8,1 -S -emit-llvm \
      "$HERE/$unit.c" -o "$OUT/$unit.ll"
    "$LLC" -O0 -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 \
      -verify-machineinstrs -mcs251-object-format=elf -filetype=obj \
      "$OUT/$unit.ll" -o "$OUT/$unit.o"
  fi
  if ! "$LLC" -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 \
    "$OUT/$unit.ll" -o "$OUT/$unit.asm" 2>/dev/null; then
    "$LLC" -O0 -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 \
      "$OUT/$unit.ll" -o "$OUT/$unit.asm"
  fi
}

# Baseline image: stages W+L only (no ISR object linked, no I stage calls).
build_unit main -O2
cp "$OUT/main.o" "$OUT/main-baseline.o"
# minisr/compiler images: main compiled with the I stage enabled.
CLANG_EXTRA="-DV1_ARM_INTERRUPT" build_unit main -O2
mv "$OUT/main.o" "$OUT/main-int.o"
# Baseline and compiler arms share one module (no _timer0 in it).
python3 "$HERE/gen-v1.py" "$OUT" "$SDAS" "$SDLD" "$YAML2OBJ" compiler
# Minisr arm gets its own module with the hand-written _timer0.
mkdir -p "$OUT/minisr-asm"
python3 "$HERE/gen-v1.py" "$OUT/minisr-asm" "$SDAS" "$SDLD" "$YAML2OBJ" minisr
"$YAML2OBJ" "$CRT" -o "$OUT/crt.o"
python3 "$HERE/../../mcs251-elf/runtime/check-crt-irq.py" "$OUT/crt.o"

# baseline: crt + main + asm module (no _timer0 symbol anywhere)
link_one baseline "$OUT/crt.o" "$OUT/v1psw1.o" "$OUT/main-baseline.o"
# minisr: asm module carries the hand-written _timer0
link_one minisr "$OUT/crt.o" "$OUT/minisr-asm/v1psw1.o" "$OUT/main-int.o"
# compiler: T10 ISR + helper objects provide _timer0
build_unit isr-compiler -O2
build_unit helper-compiler -O2
link_one compiler "$OUT/crt.o" "$OUT/v1psw1.o" "$OUT/main-int.o" \
  "$OUT/isr-compiler.o" "$OUT/helper-compiler.o"

python3 "$HERE/check.py" "$OUT" "$HERE" "$CLANG" "$LLC" "${LLD[@]}" "$YAML2OBJ" "$OBJCOPY" "$SDAS" "$SDLD"
printf 'Hardware images: %s/v1-baseline.hex %s/v1-minisr.hex %s/v1-compiler.hex\n' \
  "$OUT" "$OUT" "$OUT"
