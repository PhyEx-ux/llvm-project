#!/usr/bin/env bash
set -euo pipefail
CASE=$(cd "$(dirname "$0")" && pwd)
ROOT=/mnt/c/Prj/LLVM/MCS251
FW="$ROOT/validation/mcs251-firmware"
LD="$ROOT/validation/mcs251-ld/mcs251_ld.py"
BIN=/home/liu/mcs251-demo-test-t2/bin-frozen
SDCCBIN=/home/liu/build-sdcc/bin
W=/home/liu/mcs251-demo-test-t2/build/t2-display-table
rm -rf "$W"
mkdir -p "$W"

cpp -P -undef -nostdinc "$CASE/harness.c" > "$W/harness.i"
"$SDCCBIN/sdcc" -mmcs251 --c1mode -o "$W/harness.asm" < "$W/harness.i"
"$SDCCBIN/sdas251" -plosgffw -o "$W/harness.rel" "$W/harness.asm"
"$SDCCBIN/sdas251" -plosgffw -o "$W/crt0.rel" "$FW/crt0.asm"

# Inspect the generated assembly first. The table/bit scan must coexist with
# a direct SBUF operand for the mixed algorithm+SFR shape.
"$BIN/llc" -mtriple=mcs251-unknown-none -filetype=asm -o "$W/display-table.asm" "$CASE/display-table.ll"
if ! grep -Eiq '(^|[^0-9a-f])0x?99([^0-9a-f]|$)' "$W/display-table.asm"; then
  echo "display-table: missing direct SBUF operand 0x99" >&2
  exit 1
fi
if ! grep -Eiq 't_display|0x4f|79' "$W/display-table.asm"; then
  echo "display-table: missing table/lookup evidence in assembly" >&2
  exit 1
fi
"$BIN/llc" -mtriple=mcs251-unknown-none -filetype=obj -o "$W/display-table.rel" "$CASE/display-table.ll"
: > "$W/display-table.lst"

printf '%s\n' \
  '-muwx' \
  "-i $W/image.ihx" \
  '-M' '-I 0x0100' \
  '-b HOME = 0xff0000' \
  '-b XSEG = 0x10000' \
  '-b PSEG = 0x10000' \
  '-b ISEG = 0x0000' \
  '-b BSEG = 0x0000' \
  '-A stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1' \
  '-b GSINIT0 = 0xfc2800' \
  "$W/crt0.rel" "$W/harness.rel" "$W/display-table.rel" \
  '-e' > "$W/image.lk"
python3 "$LD" --mcs251-abi -f "$W/image.lk" > "$W/link.log" 2>&1
if [ -f "$W/image.ihx" ]; then cp "$W/image.ihx" "$W/image.hex"; fi
if [ ! -f "$W/image.hex" ]; then
  echo "display-table: linker did not produce image.hex" >&2
  exit 1
fi
set +e
timeout 8 "$BIN/qemu-system-mcs251" -M stc32g144k246 -bios "$W/image.hex" -accel tcg \
  -icount shift=0,align=off,sleep=off -display none -monitor none -serial stdio \
  < /dev/null > "$W/serial.raw" 2> "$W/qemu.stderr"
QRC=$?
set -e
python3 - "$CASE/expected.serial" "$W/serial.raw" <<'PY'
from pathlib import Path
import sys
want = Path(sys.argv[1]).read_bytes().replace(b'\r\n', b'\n')
got = Path(sys.argv[2]).read_bytes().replace(b'\r\n', b'\n')
if got != want:
    print("serial mismatch")
    print("expected:", repr(want))
    print("got:     ", repr(got))
    raise SystemExit(1)
print(repr(got))
PY
if [ "$QRC" -ne 124 ]; then
  echo "display-table: expected timeout 124, got $QRC" >&2
  exit 1
fi
printf 'display-table: PASS\n'
