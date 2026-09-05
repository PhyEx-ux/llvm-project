#!/bin/bash
# build-selfstart.sh - Step 4 self-start image builder (v2).
# Usage: build-selfstart.sh <image-name> <c-source> [extra .rel files...]
# Environment (frozen tools):
#   clang  /home/liu/mcs251-alice4/v1/bin-frozen/0260312e/clang
#   llc    /home/liu/mcs251-alice4/v1/bin-frozen/43739468/llc   (md5 43739468...)
#   sdas251 /home/liu/build-sdcc/bin/sdas251   (crt/data asm only - the single
#             remaining SDCC-tool dependency, see STEP4-SELFSTART-DESIGN.md)
#   ld     validation/mcs251-ld/mcs251_ld.py --mcs251-abi
#   qemu   /home/liu/mcs251-clang/bin-frozen/6b9edfd0/qemu-system-mcs251
set -u
CLANG=/home/liu/mcs251-alice4/v1/bin-frozen/0260312e/clang
LLC=/home/liu/mcs251-alice4/v1/bin-frozen/43739468/llc
SDAS=/home/liu/build-sdcc/bin/sdas251
LD=/mnt/c/Prj/LLVM/MCS251/validation/mcs251-ld/mcs251_ld.py
QEMU=/home/liu/mcs251-clang/bin-frozen/6b9edfd0/qemu-system-mcs251
HERE=$(cd "$(dirname "$0")" && pwd)          # dir of this script
IMG=$1; SRC=$2; shift 2
CRT=$HERE/crt-selfstart.asm
LKTMPL=$HERE/link-selfstart.lk

"$CLANG" --target=mcs251-unknown-none -O1 -S -emit-llvm "$SRC" -o "$IMG.ll" \
  || { echo CLANG-FAIL; exit 1; }
"$LLC" -mtriple=mcs251-unknown-none -filetype=obj -o "$IMG.rel" "$IMG.ll" \
  || { echo LLC-FAIL; exit 1; }
"$SDAS" -plosgffw -o "$IMG.crt.rel" "$CRT" || { echo SDAS-FAIL; exit 1; }
MODS="$PWD/$IMG.crt.rel $PWD/$IMG.rel"
for x in "$@"; do MODS="$MODS $PWD/$x"; done
sed -e "s|@OUTPUT_STEM@|$PWD/$IMG|" -e "s|@CRT_REL@|$PWD/$IMG.crt.rel|" \
    -e "s|@MODULE_REL@|$PWD/$IMG.rel|" "$LKTMPL" \
  | grep -v '^;' | sed '/^$/d' > "$IMG.lk"
# extra module lines (e.g. -b XSEG, additional .rel) are appended before -e
# from an optional $IMG.lk.extra file
if [ -f "$IMG.lk.extra" ]; then
  awk -v extra="$IMG.lk.extra" '
    /^-e$/ { while ((getline l < extra) > 0) print l } { print }' \
    "$IMG.lk" > "$IMG.lk.tmp" && mv "$IMG.lk.tmp" "$IMG.lk"
fi
python3 "$LD" --mcs251-abi -f "$IMG.lk" > "$IMG.ld.log" 2>&1
rc=$?
[ $rc -eq 0 ] && [ -f "$IMG.hex" ] || { echo LINK-FAIL; cat "$IMG.ld.log"; exit 1; }
md5sum "$IMG.hex" | tee "$IMG.hex.md5"
timeout 5 "$QEMU" -M stc32g144k246 -bios "$IMG.hex" -accel tcg \
  -display none -monitor none -serial stdio > "$IMG.qemu.out" 2>&1
echo "qemu-rc=$? (124 = expected spin/timeout)"
echo "== transcript $IMG =="
cat "$IMG.qemu.out"
