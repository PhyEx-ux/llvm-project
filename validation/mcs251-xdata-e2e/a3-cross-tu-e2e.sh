#!/usr/bin/env bash
# a3-cross-tu-e2e.sh - A2c cross-TU link/run chain (RUNTIME-AS-PTR-DESIGN-A.md
# §2.7, §3-A2c).
#
# The lit half of A2c (clang/test/CodeGen/mcs251-as4-cross-tu.c) proves that
# the caller TU and the definition TU, compiled separately, agree on address
# space, const and signature, and that AS4 -> AS0 use sites materialise as
# addrspacecast.  This script completes the second half the design asks for:
# a real link and run under the supported 32-bit ABI.
#
#   header   clang/test/CodeGen/Inputs/mcs251-as4-cross-tu.h
#   callee   clang/test/CodeGen/Inputs/mcs251-as4-cross-tu-callee.c (object TU)
#   caller   clang/test/CodeGen/mcs251-as4-cross-tu.c      (caller TU)
#   firmware clang/test/CodeGen/Inputs/mcs251-as4-cross-tu-fw.c (main)
#
# Each TU is compiled on its own; the objects are linked with mcs251-lld and
# run under QEMU.  A wrong conversion, a lost AS4 on a cross-TU signature or a
# wrong CODE table byte makes the firmware print A2C-FAIL.  The v1 contract is
# used because the firmware only needs the single-pointer-first-parameter ABI
# (follow-on pointer parameters are the v2 slice, explicitly out of scope
# here).
#
# Usage: a3-cross-tu-e2e.sh [--no-qemu]
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "$0")" && pwd)
REPO=${REPO:-$(cd -- "$ROOT/../.." && pwd)}
OUT="$ROOT/build/a3-cross-tu"
SRC="$REPO/clang/test/CodeGen"

CLANG=${CLANG:-/home/liu/build-mcs251-s1/bin/clang}
LLC=${LLC:-/home/liu/build-mcs251/bin/llc}
LLD=${LLD:-/home/liu/build-mcs251-lld/bin/lld}
READOBJ=${READOBJ:-/home/liu/build-mcs251-lld/bin/llvm-readobj}
OBJCOPY=${OBJCOPY:-/home/liu/build-mcs251-s1/bin/llvm-objcopy}
YAML2OBJ=${YAML2OBJ:-/home/liu/build-mcs251-lld/bin/yaml2obj}
QEMU=${QEMU:-/home/liu/build-qemu/qemu-system-mcs251}
MACHINE=stc32g144k246

CONTRACT=1,1,32,8,1
INC="$SRC/Inputs"
CRT_YAML="$REPO/validation/mcs251-elf/runtime/crt-selfstart.yaml"

AREA_ARGS=(
  --area-start=HOME=0xff0000 --area-start=VECS=0xff0003
  --area-start=BOOT=0xff0100 --area-start=CSEG=0xff0200
  --area-start=XINIT=0xff8000 --area-start=XDATA_INIT=0xff9000
  --area-start=XSEG=0x010000 --edata-end 0x3fff
)

rm -rf -- "$OUT"
mkdir -p -- "$OUT"

compile() { # name src opt
  local name=$1 src=$2 opt=$3
  "$CLANG" --target=mcs251-unknown-none -std=c11 "$opt" -Wall -Wextra \
    -Werror -I"$INC" -Xclang -mcs251-memory-contract="$CONTRACT" \
    -S -emit-llvm "$src" -o "$OUT/$name$opt.ll"
  "$LLC" -mtriple=mcs251 -mcs251-memory-contract="$CONTRACT" \
    -mcs251-object-format=elf -filetype=obj \
    "$OUT/$name$opt.ll" -o "$OUT/$name$opt.o"
}

for opt in -O0 -O2; do
  compile callee "$INC/mcs251-as4-cross-tu-callee.c" "$opt"
  compile caller "$SRC/mcs251-as4-cross-tu.c" "$opt"
  compile fw "$INC/mcs251-as4-cross-tu-fw.c" "$opt"
done
echo "seed/callee/caller/fw: separate clang+llc OK at -O0 and -O2"

# A2c signature agreement (same checker as the lit test, run over the -O2 IR
# too so the conversion is proven to survive optimization).  The checker now
# compares BOTH TUs against the frozen signature oracle (return type
# included), not merely against each other, and additionally requires the
# AST pointee-const write probes and the implicit-const/explicit-const TU
# equivalence, so the clang path and the inputs dir are mandatory.
for opt in -O0 -O2; do
  python3 "$INC/check-as4-cross-tu.py" --caller "$OUT/caller$opt.ll" \
    --callee "$OUT/callee$opt.ll" --clang "$CLANG" --inputs-dir "$INC" \
    --tmp-dir "$OUT/gen$opt"
done
python3 "$INC/check-as4-cross-tu.py" --self-test \
  --caller "$OUT/caller-O2.ll" --callee "$OUT/callee-O2.ll"
echo "signatures: both TUs match the frozen oracle (return type included),"
echo "            implicit-const == explicit-const, addrspacecast at use sites"

for opt in -O0 -O2; do
  if grep -qE "bitcast ptr addrspace\(4\)|ptrtoint ptr addrspace\(4\)" \
      "$OUT/caller$opt.ll"; then
    echo "FAIL: bitcast/ptrtoint used for the AS conversion at $opt" >&2
    exit 1
  fi
done

"$YAML2OBJ" "$CRT_YAML" -o "$OUT/crt.o"

link_and_run() { # opt
  local opt=$1
  "$LLD" -flavor mcs251 "${AREA_ARGS[@]}" \
    --map="$OUT/fw$opt.map" -o "$OUT/fw$opt.elf" \
    "$OUT/fw$opt.o" "$OUT/caller$opt.o" "$OUT/callee$opt.o" "$OUT/crt.o"
  "$OBJCOPY" -O ihex "$OUT/fw$opt.elf" "$OUT/fw$opt.hex"

  # The CODE table (10,20,...,80) must survive verbatim in the CODE window,
  # and the table symbol must be allocated there with its exact size: that is
  # the cross-TU object identity the A2c acceptance cares about.
  local map_line sym_line
  map_line=$(grep -E 'callee'"$opt"'\.o:\.text' "$OUT/fw$opt.map")
  sym_line=$("$READOBJ" --symbols "$OUT/callee$opt.o" | grep -A8 'Name: _shared_rom' || true)
  echo "$sym_line" | grep -q 'Section: .text' || {
    echo "FAIL: shared_rom is not in .text (CODE) in callee$opt.o" >&2
    exit 1
  }
  echo "$sym_line" | grep -q 'Size: 8' || {
    echo "FAIL: shared_rom has the wrong size in callee$opt.o" >&2
    exit 1
  }
  echo "$map_line" | grep -qE '0xff[0-9a-f]{4}' || {
    echo "FAIL: callee .text slice not allocated in the CODE window" >&2
    exit 1
  }
  python3 - "$OUT/fw$opt.elf" <<'PY'
import struct, sys
from pathlib import Path
data = Path(sys.argv[1]).read_bytes()
assert data[:4] == b"\x7fELF" and data[4] == 1 and data[5] == 2
(e_shoff,) = struct.unpack_from(">I", data, 32)
(e_shentsize, e_shnum) = struct.unpack_from(">HH", data, 46)
code = b""
for i in range(e_shnum):
    off = e_shoff + i * e_shentsize
    (_, typ, flags, addr, offset, size) = struct.unpack_from(">IIIIII", data, off)
    if typ == 1 and 0xFF0000 <= addr < 0xFF8000:
        code += data[offset:offset + size]
needle = bytes(range(10, 81, 10))
assert needle in code, "CODE table bytes missing from the CODE window"
print("PASS: cross-TU CODE table bytes verbatim in the CODE window")
PY
  echo "link+bytes($opt): OK"

  if [ "${1:-}" = "--no-qemu" ] || [ "${NO_QEMU:-0}" = "1" ]; then
    return 0
  fi
  local serial="$OUT/fw$opt.serial"
  : > "$serial"
  ( for _ in $(seq 1 120); do
      if [ -s "$serial" ] && grep -q "A2C-" "$serial"; then
        pkill -TERM -f "qemu-system-mcs251.*fw$opt" 2>/dev/null
        exit 0
      fi
      sleep 0.5
    done ) &
  local watcher=$!
  timeout --foreground 60 "$QEMU" -M "$MACHINE" -bios "$OUT/fw$opt.hex" \
    -accel tcg -display none -monitor none -serial "file:$serial" \
    > "$OUT/fw$opt.qemu.stdout" 2> "$OUT/fw$opt.qemu.stderr" || true
  kill "$watcher" 2>/dev/null || true
  wait "$watcher" 2>/dev/null || true
  echo "== QEMU($opt) transcript =="
  cat "$serial"
  grep -q '^A2C-PASS$' "$serial" || {
    echo "A2c cross-TU e2e($opt): FAIL (see $serial)" >&2
    exit 1
  }
  echo "A2c cross-TU e2e($opt): PASS"
}

link_and_run -O0
link_and_run -O2

echo "A2c cross-TU e2e complete"
