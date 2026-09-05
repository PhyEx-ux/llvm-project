#!/bin/bash
# selfstart-check: acceptance evidence for harness-llvm.ll +
# harness-llvm-cells.asm on top of crt-selfstart.asm (Step 4b).
# Four cases, exact-transcript verdict:
#   pass    -> BrwdPASS\n
#   fail8   -> BFAIL expected=0xA5 got=0x00\n            (then halt)
#   fail16  -> BFAIL expected=0x1357 got=0x0000\n        (then halt)
#   fail32  -> BFAIL expected=0x12345678 got=0x00000000\n (then halt)
# Env overrides: LLC, SDAS, MCS251_LD, QEMU, TIMEOUT (default 5).
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
FW=$(cd "$HERE/.." && pwd)
LLC=${LLC:-/home/liu/build-mcs251/bin/llc}
SDAS=${SDAS:-/home/liu/build-sdcc/bin/sdas251}
MCS251_LD=${MCS251_LD:-/mnt/c/Prj/LLVM/MCS251/validation/mcs251-ld/mcs251_ld.py}
QEMU=${QEMU:-/home/liu/build-qemu/qemu-system-mcs251}
TIMEOUT=${TIMEOUT:-5}
BUILD="$HERE/build"
mkdir -p "$BUILD"

# Shared modules (built once): startup, cells, harness.
"$SDAS" -plosgffw -o "$BUILD/crt-selfstart.rel" "$FW/crt-selfstart.asm" \
  || { echo "FAIL sdas crt-selfstart"; exit 1; }
"$SDAS" -plosgffw -o "$BUILD/harness-cells.rel" "$FW/harness-llvm-cells.asm" \
  || { echo "FAIL sdas cells"; exit 1; }
"$LLC" -mtriple=mcs251-unknown-none -filetype=obj \
  -o "$BUILD/harness-llvm.rel" "$FW/harness-llvm.ll" \
  || { echo "FAIL llc harness-llvm"; exit 1; }

rc=0
run_case() {
  name=$1; mainll=$2; expect=$3
  rm -f "$BUILD/$name.hex" "$BUILD/$name.out" "$BUILD/$name-main.rel" "$BUILD/$name.lk"
  "$LLC" -mtriple=mcs251-unknown-none -filetype=obj \
    -o "$BUILD/$name-main.rel" "$HERE/$mainll" \
    || { echo "FAIL llc $name"; rc=1; return; }
  sed -e "s|@OUTPUT_STEM@|$BUILD/$name|" \
      -e "s|^@CRT_REL@\$|$BUILD/crt-selfstart.rel|" \
      -e "s|^@MODULE_REL@\$|$BUILD/harness-cells.rel\n$BUILD/harness-llvm.rel\n$BUILD/$name-main.rel|" \
      "$FW/link-selfstart.lk" > "$BUILD/$name.lk"
  python3 "$MCS251_LD" --mcs251-abi -f "$BUILD/$name.lk" \
      > "$BUILD/$name.link.log" 2>&1 \
    || { echo "FAIL link $name"; cat "$BUILD/$name.link.log"; rc=1; return; }
  [ -f "$BUILD/$name.hex" ] || { echo "FAIL link $name: no .hex"; rc=1; return; }
  timeout "$TIMEOUT" "$QEMU" -M stc32g144k246 -bios "$BUILD/$name.hex" \
    -accel tcg -display none -monitor none -serial stdio \
    > "$BUILD/$name.out" 2>&1
  # serial text only; cut QEMU's stderr suffix (may share the last line)
  got=$(tr -d '\r' < "$BUILD/$name.out" | sed 's/qemu-system-mcs251:.*$//')
  want=$(tr -d '\r' < "$HERE/$expect")
  if [ "$got" = "$want" ]; then
    echo "PASS $name"
  else
    echo "FAIL $name"; echo "  want: $(printf %s "$want" | od -c | head -3)"
    echo "  got:  $(printf %s "$got" | od -c | head -3)"; rc=1
  fi
}

run_case pass   pass-main.ll   pass.expect
run_case fail8  fail8-main.ll  fail8.expect
run_case fail16 fail16-main.ll fail16.expect
run_case fail32 fail32-main.ll fail32.expect
echo "=="
[ "$rc" -eq 0 ] && echo "selfstart-check: ALL PASS" || echo "selfstart-check: FAILURES PRESENT"
exit "$rc"
