#!/bin/bash
# selfstart-smoke: acceptance evidence for crt-selfstart.asm +
# link-selfstart.lk.  Builds two images against the UNMODIFIED library
# assets and checks exact transcripts:
#   smoke1: main.ll  -> "MS"  (reset, stack init, ecall/eret round trip)
#   smoke2: main2.ll -> "MS!" (same, then default-vector net catches TF0)
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

"$SDAS" -plosgffw -o "$BUILD/crt-selfstart.rel" "$FW/crt-selfstart.asm" \
  || { echo "FAIL sdas251 crt-selfstart"; exit 1; }

rc=0
run_case() {
  name=$1; ll=$2; expect=$3
  rm -f "$BUILD/$name.hex" "$BUILD/$name.out" "$BUILD/$name-main.rel" \
        "$BUILD/$name.lk"
  "$LLC" -mtriple=mcs251-unknown-none -filetype=obj \
    -o "$BUILD/$name-main.rel" "$HERE/$ll" || { echo "FAIL llc $name"; rc=1; return; }
  sed -e "s|@OUTPUT_STEM@|$BUILD/$name|" \
      -e "s|^@CRT_REL@\$|$BUILD/crt-selfstart.rel|" \
      -e "s|^@MODULE_REL@\$|$BUILD/$name-main.rel|" \
      "$FW/link-selfstart.lk" > "$BUILD/$name.lk"
  python3 "$MCS251_LD" --mcs251-abi -f "$BUILD/$name.lk" \
      > "$BUILD/$name.link.log" 2>&1 \
    || { echo "FAIL link $name"; cat "$BUILD/$name.link.log"; rc=1; return; }
  [ -f "$BUILD/$name.hex" ] || { echo "FAIL link $name: no .hex"; rc=1; return; }
  timeout "$TIMEOUT" "$QEMU" -M stc32g144k246 -bios "$BUILD/$name.hex" \
    -accel tcg -display none -monitor none -serial stdio \
    > "$BUILD/$name.out" 2>&1
  # serial text only: strip CR, cut QEMU's stderr suffix (it can share the
  # last line with serial output when the firmware prints no trailing \n).
  got=$(tr -d '\r' < "$BUILD/$name.out" | sed 's/qemu-system-mcs251:.*$//' | grep -v '^$')
  want=$(tr -d '\r' < "$HERE/$expect")
  if [ "$got" = "$want" ]; then
    echo "PASS $name transcript=$got"
  else
    echo "FAIL $name expected=$want got=$got"; rc=1
  fi
}

run_case smoke1 main.ll  serial1.expect
run_case smoke2 main2.ll serial2.expect
echo "=="
[ "$rc" -eq 0 ] && echo "selfstart-smoke: ALL PASS" || echo "selfstart-smoke: FAILURES PRESENT"
exit "$rc"
