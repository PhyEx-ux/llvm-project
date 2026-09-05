#!/usr/bin/env bash
set -euo pipefail
CASE=$(cd "$(dirname "$0")" && pwd)
ROOT=/mnt/c/Prj/LLVM/MCS251
FW="$ROOT/validation/mcs251-firmware"
LD="$ROOT/validation/mcs251-ld/mcs251_ld.py"
BIN=/home/liu/mcs251-demo-test-t2/bin-frozen
SDCCBIN=/home/liu/build-sdcc/bin
W=/home/liu/mcs251-demo-test-t2/build/t2-sfr-uart-tx
rm -rf "$W"
mkdir -p "$W"

cpp -P -undef -nostdinc "$CASE/harness.c" > "$W/harness.i"
"$SDCCBIN/sdcc" -mmcs251 --c1mode -o "$W/harness.asm" < "$W/harness.i"
"$SDCCBIN/sdas251" -plosgffw -o "$W/harness.rel" "$W/harness.asm"
"$SDCCBIN/sdas251" -plosgffw -o "$W/crt0.rel" "$FW/crt0.asm"
# Emit and inspect assembly before entering the production object path.  The
# fixed <=0xff pointers must become direct SFR operands, not indirect data.
"$BIN/llc" -mtriple=mcs251-unknown-none -filetype=asm -o "$W/sfr-uart-tx.asm" "$CASE/sfr-uart-tx.ll"
for addr in 98 99; do
  if ! grep -Eiq "(^|[^0-9a-f])0x?$addr([^0-9a-f]|$)" "$W/sfr-uart-tx.asm"; then
    echo "sfr-uart-tx: missing direct SFR operand $addr" >&2
    exit 1
  fi
done
"$BIN/llc" -mtriple=mcs251-unknown-none -filetype=obj -o "$W/sfr-uart-tx.rel" "$CASE/sfr-uart-tx.ll"
: > "$W/sfr-uart-tx.lst"
python3 - "$W/sfr-uart-tx.rel" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1])
if not p.exists() or p.stat().st_size == 0:
    raise SystemExit("missing LLVM object")
PY

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
  "$W/crt0.rel" "$W/harness.rel" "$W/sfr-uart-tx.rel" \
  '-e' > "$W/image.lk"
python3 "$LD" --mcs251-abi -f "$W/image.lk" > "$W/link.log" 2>&1
if [ -f "$W/image.ihx" ]; then cp "$W/image.ihx" "$W/image.hex"; fi
if [ ! -f "$W/image.hex" ]; then
  echo "sfr-uart-tx: linker did not produce image.hex" >&2
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
print(got.decode(errors='replace'), end='')
PY
if [ "$QRC" -ne 124 ]; then
  echo "sfr-uart-tx: expected timeout 124, got $QRC" >&2
  exit 1
fi
printf 'sfr-uart-tx: PASS\n'
