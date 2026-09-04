#!/bin/bash
# Phase 13a end-to-end for the object path: llc -filetype=obj writes the
# ASxxxx .rel directly (sdas251 is NOT in the production path), then the
# usual sdcc-driver link and QEMU serial check.  Self-contained: builds the
# reference sides (crt0/harness) with run_smoke.py --sdcc-only first.
#
# Usage: run-e2e-obj.sh [build-dir]
#
# Tool paths follow the local machine (see run_smoke.py --toolchain docs).
set -e
SDCCBIN=${SDCCBIN:-/home/liu/build-sdcc/bin}
QEMU=${QEMU:-/home/liu/build-qemu/qemu-system-mcs251}
LLC=${LLC:-$HOME/build-mcs251/bin/llc}
SMOKE=$(cd "$(dirname "$0")/.." && pwd)
W=${1:-/tmp/mcs251-p13a-e2e}
rm -rf "$W"; mkdir -p "$W"

# Reference sides via the smoke harness (SDCC only ever builds these).
python3 "$SMOKE/run_smoke.py" --sdcc-only --toolchain "${SDCCBIN%/bin}" \
  --qemu "$QEMU" --build-dir "$W"

# The probe goes .ll -> .rel with no assembler involved.
"$LLC" -mtriple=mcs251-unknown-none -filetype=obj -o "$W/probe-llvm.rel" \
  "$SMOKE/probe.ll"

cd "$W"
# sdld for mcs251 needs -r (it is what enables the 24-bit ROM accounting in
# lkmain.c), and the -r listing pass wants a .lst beside every .rel; the obj
# path has none (no assembler, no listing), so provide an empty one.
: > probe-llvm.lst
export PATH="$SDCCBIN:$PATH"
sdcc -mmcs251 --nostdlib --no-xinit-opt --code-loc 0xff0000 \
  "-Wl-b GSINIT0=0xfc2800" -o smoke-llvm.hex crt0.rel harness.rel probe-llvm.rel
ls -la smoke-llvm.hex

set +e
timeout 3 "$QEMU" -M stc32g144k246 -bios smoke-llvm.hex -accel tcg \
  -icount shift=0,align=off,sleep=off -display none -monitor none \
  -serial stdio < /dev/null | tee qemu-serial.raw
if grep -qa "BPASS" qemu-serial.raw; then
  echo "E2E-RESULT: BPASS (llc -filetype=obj, no sdas251)"
else
  echo "E2E-RESULT: NO BPASS"
  exit 1
fi
