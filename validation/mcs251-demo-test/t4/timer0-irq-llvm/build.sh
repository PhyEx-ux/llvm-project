#!/bin/bash
# t4/timer0-irq-llvm: Timer0 overflow IRQ with an ordinary LLVM-compiled
# function as the ISR body, entered through the hardened asm vector stub
# (see ../../t4-probes/ISR-STUB-VERDICT.md).
#
# Production chain only:  llc -filetype=obj -> mcs251_ld.py --mcs251-abi
# -> .hex -> QEMU.  No SDCC driver involvement in the link.
#
# Verdict (serial transcript is the only oracle):
#   line 1 must match serial.expect ('X' = wildcard, unused here),
#   a line "PASS" must exist, "FAIL" must not appear.  The harness spins
#   after PASS/FAIL; the timeout kill (rc 124) is expected.
#
# Env overrides: LLC, SDAS, MCS251_LD, QEMU, TIMEOUT (seconds, default 5).
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
LLC=${LLC:-/home/liu/build-mcs251/bin/llc}
SDAS=${SDAS:-/home/liu/build-sdcc/bin/sdas251}
MCS251_LD=${MCS251_LD:-$HERE/../../../mcs251-ld/mcs251_ld.py}
QEMU=${QEMU:-/home/liu/build-qemu/qemu-system-mcs251}
TIMEOUT=${TIMEOUT:-5}
BUILD="$HERE/build"
mkdir -p "$BUILD"
cd "$HERE"

rm -f "$BUILD/vector-stub.rel" "$BUILD/isr-body.rel" \
      "$BUILD/timer0-irq-llvm.hex" "$BUILD/timer0-irq-llvm.ihx" \
      "$BUILD/serial.out" "$BUILD/link.log"

"$LLC" -mtriple=mcs251-unknown-none -filetype=obj \
  -o "$BUILD/isr-body.rel" "$HERE/isr-body.ll" || { echo "FAIL llc"; exit 1; }

"$SDAS" -plosgffw -o "$BUILD/vector-stub.rel" "$HERE/vector-stub.asm" \
  || { echo "FAIL sdas251"; exit 1; }

python3 "$MCS251_LD" --mcs251-abi -f link.lk > "$BUILD/link.log" 2>&1 \
  || { echo "FAIL link (see build/link.log)"; cat "$BUILD/link.log"; exit 1; }
[ -f "$BUILD/timer0-irq-llvm.hex" ] || {
  echo "FAIL link: no .hex (see build/link.log)"; cat "$BUILD/link.log"; exit 1; }

timeout "$TIMEOUT" "$QEMU" -M stc32g144k246 -bios "$BUILD/timer0-irq-llvm.hex" \
  -accel tcg -display none -monitor none -serial stdio \
  > "$BUILD/serial.out" 2>&1

python3 - "$BUILD/serial.out" "$HERE/serial.expect" <<'PYEOF'
import re, sys
out_path, exp_path = sys.argv[1], sys.argv[2]
raw = open(out_path, "rb").read().decode("utf-8", "replace")
lines = [l for l in raw.replace("\r", "").split("\n")
         if l and not l.startswith("qemu-system-mcs251")]
text = "\n".join(lines)
print("serial: " + (lines[0] if lines else "<empty>"))
if "FAIL" in text:
    print("FAIL transcript-contains-FAIL"); sys.exit(1)
if "PASS" not in lines:
    print("FAIL no-PASS-line"); sys.exit(1)
pat = [l.rstrip("\n") for l in open(exp_path) if l.strip()][0]
regex = "^" + "".join("." if c == "X" else re.escape(c) for c in pat) + "$"
if not lines or not re.match(regex, lines[0]):
    print("FAIL evidence-mismatch pattern=%s got=%s"
          % (pat, lines[0] if lines else "<empty>")); sys.exit(1)
print("PASS"); sys.exit(0)
PYEOF
