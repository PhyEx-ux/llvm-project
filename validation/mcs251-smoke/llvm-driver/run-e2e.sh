#!/bin/bash
# Phase 12 Step 1 end-to-end: llc -> sdas251 -> sdld -> QEMU.
#
# LOCAL SCRATCH REPRODUCTION SCRIPT: paths are hardcoded to this machine's
# WSL layout (/home/liu/build-sdcc, /home/liu/build-qemu, ~/build-mcs251)
# and to Moka's Phase 12 artifacts under /tmp/mcs251-p12.  It is kept as the
# record of the acceptance run, not as a portable test.
set -e
cd /tmp/mcs251-p12-impl
AS=/home/liu/build-sdcc/bin/sdas251
LD=/home/liu/build-sdcc/bin/sdld
QEMU=/home/liu/build-qemu/qemu-system-mcs251
LLC=~/build-mcs251/bin/llc
SIG='stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1'

cp /tmp/mcs251-p12/build/crt0.rel /tmp/mcs251-p12/build/crt0.lst \
   /tmp/mcs251-p12/build/harness.rel /tmp/mcs251-p12/build/harness.lst \
   /tmp/mcs251-p12/build/external-provider.rel \
   /tmp/mcs251-p12/build/external-provider.lst .

"$LLC" -mtriple=mcs251-unknown-none -filetype=asm -o probe-llvm.asm probe.ll
cat probe-llvm.asm

"$AS" -plosgffw -o probe-llvm.rel probe-llvm.asm

{
  echo -muwx
  echo '-i probe.hex'
  echo -M
  echo '-I 0x0100'
  echo '-b HOME = 0xff0000'
  echo '-b XSEG = 0x10000'
  echo '-b PSEG = 0x10000'
  echo '-b ISEG = 0x0000'
  echo '-b BSEG = 0x0000'
  echo "-A $SIG"
  echo '-b GSINIT0=0xfc2800'
  echo crt0.rel
  echo harness.rel
  echo probe-llvm.rel
  echo external-provider.rel
  echo ''
  echo -e
} > probe.lk

"$LD" --mcs251-abi -r -nf probe.lk
# sdld names Intel-Hex output .ihx; this QEMU machine's loader keys on .hex.
ln -sf probe.ihx probe-qemu.hex

set +e
timeout 3 "$QEMU" -M stc32g144k246 -bios probe-qemu.hex -accel tcg \
  -icount shift=0,align=off,sleep=off -display none -monitor none \
  -serial stdio < /dev/null | tee qemu-serial.raw
# QEMU keeps looping in the harness, so the timeout above is the normal exit
# path; success is decided by the serial transcript alone.
serial_rc=${PIPESTATUS[0]}
set -e
if ! tr -d '\r' < qemu-serial.raw | grep -q 'PASS'; then
  echo "FAIL: QEMU serial transcript has no PASS:" >&2
  cat qemu-serial.raw >&2
  exit 1
fi
echo "serial transcript contains PASS (qemu pipeline rc=$serial_rc)"
