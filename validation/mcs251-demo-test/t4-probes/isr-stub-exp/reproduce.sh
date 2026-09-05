#!/bin/bash
# Reproduce the ISR stub form experiments (transcripts in *.transcript.txt).
# Experiment-stage chain: sdas251 + sdcc-driver link (oracle chain), frozen
# or env-selected QEMU.  The DELIVERED regression uses the production chain
# (llc -> mcs251_ld.py); see ../../t4/timer0-irq-llvm/.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
BUILD="$HERE/build"
SDAS=${SDAS:-/home/liu/build-sdcc/bin/sdas251}
SDCC=${SDCC:-/home/liu/build-sdcc/bin/sdcc}
LLC=${LLC:-/home/liu/build-mcs251/bin/llc}
QEMU=${QEMU:-/home/liu/build-qemu/qemu-system-mcs251}
mkdir -p "$BUILD"

"$LLC" -mtriple=mcs251-unknown-none -filetype=obj \
  -o "$BUILD/isr_body.rel" "$HERE/isr_body.ll" || { echo "FAIL llc"; exit 1; }

run_exp() {
  n=$1; shift
  rm -f "$BUILD/$n.rel" "$BUILD/$n.hex" "$BUILD/$n.ihx" "$BUILD/$n.out"
  "$SDAS" -plosgffw -o "$BUILD/$n.rel" "$HERE/$n.asm" || { echo "ASSEM-FAIL $n"; return 1; }
  ( cd "$BUILD" && "$SDCC" -mmcs251 --nostdlib --no-xinit-opt \
      "-Wl-b HOME=0xff0000" "-Wl-b VEC=0xff000b" "-Wl-b PROBE=0xff0100" \
      "-Wl-b CSEG=0xff0200" \
      -o "$n.hex" "$n.rel" "$@" ) > "$BUILD/$n.linklog" 2>&1
  [ -f "$BUILD/$n.hex" ] || { echo "LINK-FAIL $n"; cat "$BUILD/$n.linklog"; return 1; }
  timeout 5 "$QEMU" -M stc32g144k246 -bios "$BUILD/$n.hex" -accel tcg \
    -display none -monitor none -serial stdio > "$BUILD/$n.out" 2>&1
  printf "%-8s " "$n"
  tr -d '\r' < "$BUILD/$n.out" | tr '\n' '|'
  echo
}

run_exp exp_a  isr_body.rel   # lcall + LLVM(eret) : expect vAI then silence
run_exp exp_a2                # lcall + asm(ret)   : expect PASS (control)
run_exp exp_b  isr_body.rel   # ecall + LLVM(eret) : expect PASS
run_exp exp_c  isr_body.rel   # stub+ecall+LLVM    : expect PASS
