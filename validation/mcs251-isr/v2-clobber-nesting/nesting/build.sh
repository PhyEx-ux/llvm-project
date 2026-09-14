#!/usr/bin/env bash
# V2 two-level nesting fixture build.  Usage: bash build.sh [new-empty-output-dir]
set -euo pipefail
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OUT=${1:-$(mktemp -d /tmp/v2-nest.XXXXXX)}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
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

build_unit() { # name optlevel
  local unit=$1 opt=$2
  "$CLANG" --target=mcs251-unknown-none -std=c11 $opt -Wall -Wextra -Werror \
    -Xclang -mcs251-memory-contract=1,1,32,8,1 -S -emit-llvm \
    "$HERE/$unit.c" -o "$OUT/$unit.ll"
  "$LLC" -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 \
    -verify-machineinstrs -mcs251-object-format=elf -filetype=obj \
    "$OUT/$unit.ll" -o "$OUT/$unit.o"
  "$LLC" -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 \
    "$OUT/$unit.ll" -o "$OUT/$unit.asm"
}

build_unit isr-high -O2
build_unit isr-low -O2
if ! build_unit main -O2 2>"$OUT/main-O2-failure.log"; then
  printf 'main.c: -O2 failed (backend state), retrying with -O0\n' >&2
  build_unit main -O0
  printf 'main.c built with -O0 (see main-O2-failure.log)\n'
fi
python3 "$HERE/gen-nest.py" "$OUT" "$SDAS" "$SDLD" "$YAML2OBJ"
CRT="$HERE/../../../mcs251-elf/runtime/crt-irq.yaml"
"$YAML2OBJ" "$CRT" -o "$OUT/crt.o"
python3 "$HERE/../../../mcs251-elf/runtime/check-crt-irq.py" "$OUT/crt.o"
"${LLD[@]}" "$OUT/crt.o" "$OUT/v2nest.o" "$OUT/main.o" "$OUT/isr-high.o" "$OUT/isr-low.o" \
  --area-start=HOME=0xff0000 --area-start=BOOT=0xff0500 --area-start=CSEG=0xff0700 \
  --area-start=XINIT=0xff8000 --area-start=DSEG=0x30 --edata-end=0x0fff \
  --area-start=.mcs251.DATA.fixture=0x30 --area-start=.mcs251.DATA.result=0x100 \
  --area-start=.mcs251.DATA.teststack=0x580 \
  --flash-base=0xfe0000 --flash-size=0x20000 --map="$OUT/v2-nest.map" \
  -o "$OUT/v2-nest.elf"
"$OBJCOPY" -O ihex "$OUT/v2-nest.elf" "$OUT/v2-nest.hex"
python3 "$HERE/check.py" "$OUT" "$HERE" "$CLANG" "$LLC" "${LLD[@]}" "$YAML2OBJ" "$OBJCOPY" "$SDAS" "$SDLD"
printf 'Hardware image: %s/v2-nest.hex\n' "$OUT"
