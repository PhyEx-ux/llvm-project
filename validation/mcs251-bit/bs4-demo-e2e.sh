#!/usr/bin/env bash
# bs4-demo-e2e.sh - G2 B-S4 acceptance: the six G2 demos (13,48,50,59,60,68)
# through the v2 variadic chain, with real QEMU transcripts for 13 and 68.
#
# This script deliberately REPLICATES the drive.py recipe instead of calling
# it: drive.py belongs to the G5/other-instance domain and must not be
# modified.  Everything here is read-only with respect to the demo corpus
# (mcs251-demos-rewritten/src is never written); all products land under
# validation/mcs251-bit/build/bs4-demo-e2e/.
#
# The v2 chain (contract 1,2,32,8,1):
#   - demo TUs compiled by clang -cc1 --target=mcs251-unknown-none -O0
#     -fmcs251-keil + the three compat headers, then llc -> ELF;
#   - the migrated variadic printf runtime at -O2 (-O0's 26 KB .text cannot
#     share the demo CSEG window);
#   - the div/mod runtime objects the program actually references;
#   - the nop helper for TUs that call _nop_();
#   - the frozen v2 CRT (gen-crt-v2.sh --identity v2): irq variant when the
#     program defines an interrupt entry, selfstart otherwise.
#
# FINDING-S4b (see IMPL-G2S4-PROGRESS.md): the corpus's SFR header is the v1
# (AS0) flavor, whose SBUF macro lowers to a @dr RAM access under v2 and
# never reaches SFR 0x99 - so a v2 build of demo 13/68 runs but is silent on
# the UART.  This script therefore builds BOTH flavors for the two demos that
# actually print at run time:
#   flavor "v1hdr": the corpus header as shipped (documents the silence);
#   flavor "as6":   the AS6 header overlay (the registered remedy for a v2
#                   layout chain), which is the flavor that can produce a
#                   real transcript.
# The silence is a G3/T2 attribution gap, not a B-S4 regression: the printf
# path itself is exercised by the same binaries and is proven by
# bs4-runtime-probe.sh.
#
# What is asserted:
#   1. T0: every demo TU compiles to IR under v2 (13,48,50,59,60,68).
#   2. T1: the v2 chain links; 48 and 59 pass, 50 and 60 must fail with the
#      G8 direct-page window diagnostic (existing gap, recorded not hidden).
#   3. 41 call sites: the emitted IR's `call ... @printf/@sprintf` count per
#      demo equals the design section 2.1 table (13:1 48:4 50:8 59:15 60:11
#      68:2).  Dead `#else` branches and commented-out calls are excluded by
#      construction because the count reads IR, not source text.
#   4. T2: demo 13's AS6-flavor firmware prints its banner and demo 68's
#      prints the Sleep/wakeup cycle; the v1-flavor builds of the same
#      sources produce no such bytes (the AS0/v2 silence, asserted as a
#      documented outcome, never as a pass for the banner).
#
# SCOPE OF THE T2 PASS - READ THIS BEFORE QUOTING THE RESULT (2026-09-15 Alice
# review R4).  Every byte-level T2 PASS in this script is scoped to the header
# version it was produced with, namely the AS6 overlay
# (validation/mcs251-porting/generated/stc32g144k246-as6.h, passed as
# -I<overlay> so it shadows the corpus's stc32g144k246-v1.h).  The AS6 rewrite
# is a legitimate and NECESSARY v2 change, not a fabrication: per
# validation/mcs251-porting/README.md:51-58 (Alice's 2026-09-08 ruling) a v2
# layout chain must express SFR access explicitly with
# __attribute__((address_space(6))), because an AS0 absolute address is a @dr
# RAM access under v2 and never reaches SFR 0x99.  The corpus header as shipped
# is the v1/AS0 flavor; under the v2 chain this script uses, its SBUF macro is
# silently a RAM write.  Therefore:
#   - "T2 PASS" here means: the AS6-flavored build of the demo prints the
#     expected bytes - and nothing about the corpus's own (v1/AS0) header,
#     which is asserted separately as the registered 0-byte FINDING-S4b
#     outcome;
#   - it does NOT mean the demo corpus passes unmodified under v2.
# The two flavors are always built and reported side by side so the scope
# cannot be read past.
#
# Usage: bs4-demo-e2e.sh [--keep] [--quick]
#   --quick  skip the QEMU runs (T0/T1/counts only)
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "$0")" && pwd)
REPO=$(cd -- "$ROOT/../.." && pwd)
DEMOS=/home/liu/LLVM_STC32/mcs251-demos-rewritten
SRCROOT="$DEMOS/src"
OUT="$ROOT/build/bs4-demo-e2e"

CLANG=${CLANG:-/home/liu/build-mcs251-s1/bin/clang}
LLC=${LLC:-/home/liu/build-mcs251/bin/llc}
LLD=${LLD:-/home/liu/build-mcs251-lld/bin/mcs251-lld}
YAML2OBJ=${YAML2OBJ:-/home/liu/build-mcs251-lld/bin/yaml2obj}
READOBJ=${READOBJ:-/home/liu/build-mcs251-s1/bin/llvm-readobj}
READELF=${READELF:-/home/liu/build-mcs251-s1/bin/llvm-readelf}
OBJCOPY=${OBJCOPY:-/home/liu/build-mcs251-s1/bin/llvm-objcopy}
QEMU=${QEMU:-/home/liu/build-qemu/qemu-system-mcs251}
MACHINE=${MACHINE:-stc32g144k246}
GEN_CRT_V2="$REPO/validation/mcs251-elf/runtime/gen-crt-v2.sh"
RT_SRC="$REPO/validation/mcs251-runtime/src/mcs251_printf.c"
DIV_SRC="$REPO/llvm/lib/Target/MCS251/Runtime"
DIALECT="$REPO/validation/mcs251-dialect/include"
PORTING_GEN="$REPO/validation/mcs251-porting/generated"
PORTING_INC="$REPO/validation/mcs251-porting/include"
NOP_SRC="$DEMOS/tools/nop-helper.c"

CONTRACT=1,2,32,8,1
QEMU_TIMEOUT=${QEMU_TIMEOUT:-45}

AREAS_IRQ="--area-start=HOME=0xff0000 --area-start=VECS=0xff0003 \
--area-start=BOOT=0xff0500 --area-start=CSEG=0xff0700 \
--area-start=XINIT=0xff8000 --area-start=XDATA_INIT=0xff9000 \
--area-start=XSEG=0x010000 --edata-end=0x3fff"
AREAS_SELF="--area-start=HOME=0xff0000 --area-start=VECS=0xff0003 \
--area-start=BOOT=0xff0100 --area-start=CSEG=0xff0200 \
--area-start=XINIT=0xff8000 --area-start=XDATA_INIT=0xff9000 \
--area-start=XSEG=0x010000 --edata-end=0x3fff"

QUICK=0
for a in "$@"; do case "$a" in
  --quick) QUICK=1 ;;
  --keep) KEEP=1 ;;
esac; done
KEEP=${KEEP:-0}

for t in "$CLANG" "$LLC" "$LLD" "$YAML2OBJ" "$READOBJ" "$READELF" "$OBJCOPY" "$QEMU"; do
  command -v "$t" >/dev/null || { echo "FAIL: missing tool: $t" >&2; exit 1; }
done

rm -rf -- "$OUT"
mkdir -p -- "$OUT/rt" "$OUT/crt" "$OUT/overlay"

# --- runtime + crt + div/mod objects (v2) --------------------------------
build_runtime() {
  "$CLANG" --target=mcs251-unknown-none -std=c11 -ffreestanding -fno-builtin \
    -DMCS251_RT_TARGET -O2 -Xclang -mcs251-memory-contract="$CONTRACT" \
    -S -emit-llvm "$RT_SRC" -o "$OUT/rt/printf.ll"
  "$LLC" -mtriple=mcs251 -mcs251-memory-contract="$CONTRACT" -O2 \
    -verify-machineinstrs -mcs251-object-format=elf -filetype=obj \
    "$OUT/rt/printf.ll" -o "$OUT/rt/printf.o"
  local f
  for f in divulong modulong; do
    "$CLANG" --target=mcs251-unknown-none -std=c11 -ffreestanding \
      -fno-builtin -DMCS251_RT_TARGET -O0 \
      -Xclang -mcs251-memory-contract="$CONTRACT" \
      -S -emit-llvm "$DIV_SRC/mcs251rt_$f.c" -o "$OUT/rt/$f.ll"
    "$LLC" -mtriple=mcs251 -mcs251-memory-contract="$CONTRACT" -O0 \
      -verify-machineinstrs -mcs251-object-format=elf -filetype=obj \
      "$OUT/rt/$f.ll" -o "$OUT/rt/$f.o"
  done
  "$CLANG" --target=mcs251-unknown-none -std=c11 -O0 -Wall -Werror \
    -Xclang -mcs251-memory-contract="$CONTRACT" -S -emit-llvm \
    "$NOP_SRC" -o "$OUT/rt/nop.ll"
  "$LLC" -mtriple=mcs251 -mcs251-memory-contract="$CONTRACT" -O0 \
    -mcs251-object-format=elf -filetype=obj "$OUT/rt/nop.ll" -o "$OUT/rt/nop.o"
  bash "$GEN_CRT_V2" --yaml2obj "$YAML2OBJ" --readobj "$READOBJ" \
    --variant irq --identity v2 --out "$OUT/crt/crt-irq-v2.o" >/dev/null
  bash "$GEN_CRT_V2" --yaml2obj "$YAML2OBJ" --readobj "$READOBJ" \
    --variant selfstart --identity v2 --out "$OUT/crt/crt-selfstart-v2.o" >/dev/null
}
build_runtime
echo "runtime: printf(-O2) + div/mod + nop + v2 crt (irq, selfstart) OK"

# --- FINDING-S4b: AS6 overlay header -------------------------------------
# The corpus includes the header by the -v1.h name; the overlay directory is
# passed FIRST so its AS6-flavored copy wins.  The corpus is not touched.
cp "$PORTING_GEN/stc32g144k246-as6.h" "$OUT/overlay/stc32g144k246-v1.h"

# --- per-demo program groups (mirrors drive.py PROGRAM_GROUPS for 59) ----
demo_sources() { # <demo> -> newline-separated .c paths
  find "$SRCROOT/$1" -name '*.c' | sort
}

D13="13-8个串口同时使用收发测试程序"
D48="48-LIN2总线从机收发测试"
D50="50-LIN1-LIN2同时做从机通信"
D59="59-DMA-UART串口与存储器数据自动收发"
D60="60-DMA-I2C与存储器数据自动收发"
D68="68-普通IO口中断-休眠唤醒"

# compile one TU; $4 is the include overlay ("" for the corpus header)
compile_tu() { # <src> <outdir> <tag> <overlay>
  local src=$1 dir=$2 tag=$3 overlay=$4
  local base; base=$(basename "$src" .c)
  local dd; dd=$(dirname "$src")
  mkdir -p "$dir"
  local incs=()
  [ -n "$overlay" ] && incs=(-I"$overlay")
  local nop=""
  grep -q '_nop_' "$src" && nop="-DMCS251_PORTING_ELF_NOP_HELPER"
  # shellcheck disable=SC2086
  "$CLANG" --target=mcs251-unknown-none -std=c11 -O0 -fmcs251-keil \
    -w $nop "${incs[@]}" -I"$dd" -I"$DIALECT" -I"$PORTING_GEN" -I"$PORTING_INC" \
    -Xclang -mcs251-memory-contract="$CONTRACT" \
    -S -emit-llvm "$src" -o "$dir/$base.ll"
  "$LLC" -mtriple=mcs251 -mcs251-memory-contract="$CONTRACT" -O0 \
    -mcs251-object-format=elf -filetype=obj "$dir/$base.ll" -o "$dir/$base.o"
  echo "$dir/$base.ll"
}

# --- T0 for all six demos, into per-group directories --------------------
# A demo's program group owns its own directory (mirrors drive.py
# PROGRAM_GROUPS): demo 59's eight parallel UART programs must not share a
# build directory, or their duplicate _main would be a harness artifact.
group_dir() { # <demo> <src> <flavor> -> build dir
  local demo=$1 src=$2 flavor=$3
  local tag=${demo%%-*}
  if [ "$demo" = "$D59" ]; then
    echo "$OUT/$flavor/$tag/$(basename "$(dirname "$src")")"
  else
    echo "$OUT/$flavor/$tag"
  fi
}

declare -A T0
for demo in "$D13" "$D48" "$D50" "$D59" "$D60" "$D68"; do
  tag=${demo%%-*}
  n=0
  while IFS= read -r src; do
    [ -n "$src" ] || continue
    compile_tu "$src" "$(group_dir "$demo" "$src" t0)" "$tag" "" >/dev/null
    n=$((n + 1))
  done < <(demo_sources "$demo")
  T0[$demo]=$n
  echo "T0[$tag]: $n TU compiled to IR + ELF (v2)"
done
echo "T0: all six demos pass (contract $CONTRACT)"

# --- call-site count: IR-active vs the design section 2.1 table ----------
# FINDING-S4c: the design's 41 is a TEXTUAL count.  Demo 59's eight parallel
# programs each carry a `printf("DMA buffer full.\r\n", i--)` inside the
# `#else` of an `#if 1` - seven dead sites (UART1.c has no such branch).
# The IR therefore holds only 34 active `call ... @printf/@sprintf` sites.
# Both numbers are pinned here: 34 is what the compiler must produce, and
# 41 is reproduced by adding the 7 dead-branch sites back.
IR_WANT=(["13"]=1 ["48"]=4 ["50"]=8 ["59"]=8 ["60"]=11 ["68"]=2)
DEAD_59=7
declare -A GOT
for demo in "$D13" "$D48" "$D50" "$D59" "$D60" "$D68"; do
  tag=${demo%%-*}
  c=$(find "$OUT/t0/$tag" -name '*.ll' -exec grep -h -E \
        'call +addrspace\(4\) +i32 +\(ptr, +\.\.\.\) +@(printf|sprintf)\(' {} + \
        2>/dev/null | wc -l || true)
  GOT[$tag]=$c
done
echo "call sites (IR active): 13=${GOT[13]} 48=${GOT[48]} 50=${GOT[50]} 59=${GOT[59]} 60=${GOT[60]} 68=${GOT[68]}"
for tag in 13 48 50 59 60 68; do
  [ "${GOT[$tag]}" -eq "${IR_WANT[$tag]}" ] || {
    echo "FAIL: demo $tag IR call-site count ${GOT[$tag]} != expected ${IR_WANT[$tag]}" >&2
    exit 1; }
done
total=$(( GOT[13] + GOT[48] + GOT[50] + GOT[59] + GOT[60] + GOT[68] ))
[ "$total" -eq 34 ] || { echo "FAIL: total IR call sites $total != 34" >&2; exit 1; }
echo "call sites: 1+4+8+8+11+2 = $total active in IR"

# textual reconciliation to the design's 41: demo 59's dead `#else` sites
# are counted from the corpus sources (the group build dirs hold products,
# not sources), so the reconciliation is evidence-backed, not asserted.
dead=0
while IFS= read -r src; do
  c=$(awk '/^#if 1/{live=1} /^#else/{if(live)dead=1} /^#endif/{live=0;dead=0}
           dead && /printf\("DMA buffer full/{n++} END{print n+0}' "$src")
  dead=$(( dead + c ))
done < <(demo_sources "$D59")
[ "$dead" -eq "$DEAD_59" ] || {
  echo "FAIL: demo 59 dead-branch call sites $dead != $DEAD_59" >&2; exit 1; }
textual=$(( total + dead ))
[ "$textual" -eq 41 ] || {
  echo "FAIL: textual call sites $textual != design 41" >&2; exit 1; }
echo "call sites: $textual == design section 2.1 (41) = $total IR-active + $dead dead-branch sites in 59 (FINDING-S4c)"

# --- T1: link each program group ----------------------------------------
link_group() { # <tag> <groupdir> <objs...>
  local tag=$1 dir=$2; shift 2
  (cd "$dir" && "$LLD" -flavor mcs251 $AREAS_IRQ --map=link.map \
    -o demo.elf "$@")
}
link_v2() { # <tag> <groupdir>
  local tag=$1 dir=$2
  local objs=()
  local o
  for o in "$dir"/*.o; do objs+=("$o"); done
  objs+=("$OUT/rt/printf.o" "$OUT/rt/divulong.o" "$OUT/rt/modulong.o")
  [ -f "$dir/nop-needed" ] && objs+=("$OUT/rt/nop.o")
  link_group "$tag" "$dir" "${objs[@]}" "$OUT/crt/crt-irq-v2.o"
}

# nop-helper objects are only linked when a TU actually references the symbol
need_nop() { # <groupdir>
  grep -qh 'mcs251_porting_elf_nop' "$1"/*.ll 2>/dev/null
}

T1_13=ok; T1_68=ok
link_ok=0; link_gap=0

# nop-helper objects are only linked when a TU actually references the symbol
need_nop() { # <groupdir>
  grep -qh 'mcs251_porting_elf_nop' "$1"/*.ll 2>/dev/null
}

link_one() { # <demo-tag> <groupdir> <label>
  local tag=$1 gdir=$2 label=$3
  need_nop "$gdir" && touch "$gdir/nop-needed" || rm -f "$gdir/nop-needed"
  if link_v2 "$tag" "$gdir" >"$gdir/link.log" 2>&1; then
    echo "  T1[$label]: pass ($(awk '$1=="l_DSEG"{print $3}' "$gdir/link.map"))"
    return 0
  fi
  echo "  T1[$label]: GAP/fail"
  sed 's/^/    /' "$gdir/link.log" | head -3
  return 1
}

echo "T1:"
for demo in "$D13" "$D48" "$D68"; do
  tag=${demo%%-*}
  link_one "$tag" "$OUT/t0/$tag" "$tag" || { echo "FAIL: demo $tag T1" >&2; exit 1; }
done
for g in "$OUT/t0/59"/*/; do
  link_one 59 "$g" "59/$(basename "$g")" || { echo "FAIL: 59 group $g" >&2; exit 1; }
done

# 50 and 60 are the recorded G8 window gaps: their T1 must FAIL with the
# window diagnostic, and the script asserts the exact failure mode instead
# of pretending they link.
for spec in "$D50:50" "$D60:60"; do
  demo=${spec%%:*}; tag=${spec##*:}
  mkdir -p "$OUT/t0/$tag"
  if link_v2 "$tag" "$OUT/t0/$tag" >"$OUT/t0/$tag/link.log" 2>&1; then
    echo "FAIL: demo $tag unexpectedly linked (G8 window gap should persist)" >&2
    exit 1
  fi
  grep -q 'window \[0x0000,0x0080)' "$OUT/t0/$tag/link.log" || {
    echo "FAIL: demo $tag failed for a reason other than the G8 window:" >&2
    sed 's/^/  /' "$OUT/t0/$tag/link.log" >&2; exit 1; }
  grep -o 'size [0-9]* bytes, align 1, symbols [^;]*' "$OUT/t0/$tag/link.log" | head -1 \
    | sed "s/^/  T1[$tag]: G8 window gap: /"
done
echo "T1: 48/59/13/68 link; 50/60 fail on the recorded G8 direct-page window (unchanged)"

[ "$QUICK" -eq 1 ] && { echo "BS4-DEMO-E2E: T0/T1/counts PASS (QEMU skipped)"; exit 0; }

# --- T2: real transcripts for 13 and 68, both header flavors -------------
run_qemu() { # <hex> <serial> <needle>
  local hex=$1 serial=$2 needle=$3
  : > "$serial"
  ( for i in $(seq 1 $((QEMU_TIMEOUT * 2))); do
      if [ -s "$serial" ] && grep -q "$needle" "$serial" 2>/dev/null; then
        pkill -TERM -f "$(basename "$hex")" 2>/dev/null
        exit 0
      fi
      sleep 0.5
    done ) &
  local w=$!
  timeout --foreground "$QEMU_TIMEOUT" "$QEMU" -M "$MACHINE" \
    -bios "$hex" -accel tcg -display none -monitor none \
    -serial "file:$serial" >"$serial.qemu.out" 2>"$serial.qemu.err" || true
  kill "$w" 2>/dev/null || true
  wait "$w" 2>/dev/null || true
}

# build the AS6-flavor 13 and 68 groups (overlay first on the include path)
for demo in "$D13" "$D68"; do
  tag=${demo%%-*}
  rm -rf "$OUT/t2/$tag-as6"
  while IFS= read -r src; do
    compile_tu "$src" "$OUT/t2/$tag-as6" "$tag" "$OUT/overlay" >/dev/null
  done < <(demo_sources "$demo")
  need_nop "$OUT/t2/$tag-as6" && touch "$OUT/t2/$tag-as6/nop-needed" || true
  link_v2 "$tag" "$OUT/t2/$tag-as6" >"$OUT/t2/$tag-as6/link.log" 2>&1 || {
    echo "FAIL: AS6-flavor demo $tag did not link:" >&2
    sed 's/^/  /' "$OUT/t2/$tag-as6/link.log" >&2; exit 1; }
  "$OBJCOPY" -O ihex "$OUT/t2/$tag-as6/demo.elf" "$OUT/t2/$tag-as6/demo.hex"
done
echo "T2 build: AS6-flavor firmware for 13 and 68 OK"

# demo 13: banner appears only with the AS6 flavor.  The crt's post-main 'S'
# marker never prints here because main() never returns (the demo's whole
# purpose is the infinite RX polling loop), so the transcript is the banner
# and nothing else.
run_qemu "$OUT/t2/13-as6/demo.hex" "$OUT/t2/13-as6/demo.serial" 'Test Programme'
grep -q 'STC32G UART Test Programme!' "$OUT/t2/13-as6/demo.serial" || {
  echo "FAIL: demo 13 (AS6) did not print the banner:" >&2
  od -An -tx1z "$OUT/t2/13-as6/demo.serial" >&2; exit 1; }
printf 'STC32G UART Test Programme!\r\n' > "$OUT/t2/13-as6/golden.raw"
cmp -s "$OUT/t2/13-as6/demo.serial" "$OUT/t2/13-as6/golden.raw" || {
  echo "FAIL: demo 13 (AS6) transcript != the golden banner" >&2
  od -An -tx1z "$OUT/t2/13-as6/demo.serial" >&2; exit 1; }
echo "T2[13]: [AS6 header scope] banner byte-exact: 'STC32G UART Test Programme!\\r\\n' (main loops, no crt S)"

# the corpus (v1/AS0) header flavor of the SAME sources is silent: the
# FINDING-S4b outcome, asserted explicitly so it can never masquerade as a
# successful demo run.
rm -rf "$OUT/t2/13-v1hdr"
while IFS= read -r src; do
  compile_tu "$src" "$OUT/t2/13-v1hdr" 13 "" >/dev/null
done < <(demo_sources "$D13")
link_v2 13 "$OUT/t2/13-v1hdr" >"$OUT/t2/13-v1hdr/link.log" 2>&1 || {
  echo "FAIL: v1-flavor demo 13 did not link" >&2; exit 1; }
"$OBJCOPY" -O ihex "$OUT/t2/13-v1hdr/demo.elf" "$OUT/t2/13-v1hdr/demo.hex"
# the negative runs expect NO bytes, so a shorter window suffices: demo 13
# prints immediately after its UART setup, demo 68 after 2 s of delay.
QEMU_TIMEOUT=20 run_qemu "$OUT/t2/13-v1hdr/demo.hex" "$OUT/t2/13-v1hdr/demo.serial" 'Test Programme'
if [ -s "$OUT/t2/13-v1hdr/demo.serial" ]; then
  echo "FAIL: v1-flavor demo 13 emitted bytes - FINDING-S4b no longer holds:" >&2
  od -An -tx1z "$OUT/t2/13-v1hdr/demo.serial" >&2; exit 1
fi
echo "T2[13-v1hdr]: 0 serial bytes - FINDING-S4b reproduced (AS0 SBUF never reaches the SFR); NOT a T2 pass"

# demo 68: Sleep/wakeup cycle appears only with the AS6 flavor.  The window
# is long: the program delays 100 ms * 20 before its first printf.  The
# transcript is periodic (PCON.PD does not halt this target).
#
# 2026-09-15 Alice review R3: the previous assertion (substring greps + equal
# Sleep/wakeup counts) was too loose - injecting garbage bytes or rewriting
# CRLF to LF still passed.  The transcript is now required to be an exact
# repetition of ONE byte-exact Sleep/wakeup pair: extra bytes, wrong order and
# half pairs are all rejected.  The watcher waits for the PAIR-COMPLETING
# 'MCU wakeup' line and kills there, which lands in the ~2 s idle gap before
# the next Sleep (the loop does delay_ms(100) * 20), so the run ends on a pair
# boundary and the exact-multiple test is deterministic rather than racy.
PAIR=$'MCU Sleep.\r\nMCU wakeup from P00.\r\n'
PAIR_LEN=$(printf '%s' "$PAIR" | wc -c)

# check_68_transcript <file> -> 0 iff the file is exactly N >= 1 copies of PAIR
check_68_transcript() { # <file>
  local f=$1 size n i
  size=$(wc -c < "$f" 2>/dev/null || echo 0)
  [ "$size" -ge "$PAIR_LEN" ] || return 1
  [ $(( size % PAIR_LEN )) -eq 0 ] || return 1
  n=$(( size / PAIR_LEN ))
  : > "$OUT/t2/68-as6/expected.raw"
  for (( i = 0; i < n; i++ )); do
    printf '%s' "$PAIR" >> "$OUT/t2/68-as6/expected.raw"
  done
  cmp -s "$f" "$OUT/t2/68-as6/expected.raw"
}

QEMU_TIMEOUT=150 run_qemu "$OUT/t2/68-as6/demo.hex" "$OUT/t2/68-as6/demo.serial" 'MCU wakeup'
if ! check_68_transcript "$OUT/t2/68-as6/demo.serial"; then
  echo "FAIL: demo 68 (AS6) transcript is not an exact repetition of the" >&2
  echo "      Sleep/wakeup pair ($PAIR_LEN bytes: 'MCU Sleep.\\r\\nMCU wakeup from P00.\\r\\n')" >&2
  echo "      size=$(wc -c < "$OUT/t2/68-as6/demo.serial") bytes:" >&2
  od -An -tx1z "$OUT/t2/68-as6/demo.serial" >&2; exit 1
fi
n_pairs=$(( $(wc -c < "$OUT/t2/68-as6/demo.serial") / PAIR_LEN ))

# The assertion above is only worth what its rejection power is worth, so the
# two false-PASS injections Alice used are replayed here against the very same
# checker and MUST be rejected.  This runs on every invocation, so the check
# can never silently rot back into a substring test.
inject_garbage="$OUT/t2/68-as6/inject-garbage.raw"
inject_lf="$OUT/t2/68-as6/inject-lf.raw"
{ printf 'GARBAGE\001\002'; cat "$OUT/t2/68-as6/demo.serial"; printf 'JUNK'; } > "$inject_garbage"
tr -d '\r' < "$OUT/t2/68-as6/demo.serial" > "$inject_lf"
if check_68_transcript "$inject_garbage"; then
  echo "FAIL: injected garbage bytes were accepted by the demo 68 assertion" >&2; exit 1; fi
if check_68_transcript "$inject_lf"; then
  echo "FAIL: a CRLF->LF rewritten transcript was accepted by the demo 68 assertion" >&2; exit 1; fi
# half a pair (one byte short) must also be rejected
head -c $(( PAIR_LEN - 1 )) "$OUT/t2/68-as6/demo.serial" > "$OUT/t2/68-as6/inject-half.raw"
if check_68_transcript "$OUT/t2/68-as6/inject-half.raw"; then
  echo "FAIL: a truncated (half-pair) transcript was accepted by the demo 68 assertion" >&2; exit 1; fi
echo "T2[68]: $n_pairs exact Sleep/wakeup pair(s), transcript == ($PAIR_LEN B pair)^$n_pairs; injection self-test (garbage / CRLF->LF / half pair) all rejected"

rm -rf "$OUT/t2/68-v1hdr"
while IFS= read -r src; do
  compile_tu "$src" "$OUT/t2/68-v1hdr" 68 "" >/dev/null
done < <(demo_sources "$D68")
need_nop "$OUT/t2/68-v1hdr" && touch "$OUT/t2/68-v1hdr/nop-needed" || true
link_v2 68 "$OUT/t2/68-v1hdr" >"$OUT/t2/68-v1hdr/link.log" 2>&1 || {
  echo "FAIL: v1-flavor demo 68 did not link" >&2; exit 1; }
"$OBJCOPY" -O ihex "$OUT/t2/68-v1hdr/demo.elf" "$OUT/t2/68-v1hdr/demo.hex"
QEMU_TIMEOUT=40 run_qemu "$OUT/t2/68-v1hdr/demo.hex" "$OUT/t2/68-v1hdr/demo.serial" 'MCU Sleep'
if [ -s "$OUT/t2/68-v1hdr/demo.serial" ]; then
  echo "FAIL: v1-flavor demo 68 emitted bytes - FINDING-S4b no longer holds:" >&2
  od -An -tx1z "$OUT/t2/68-v1hdr/demo.serial" >&2; exit 1
fi
echo "T2[68-v1hdr]: 0 serial bytes - FINDING-S4b reproduced (AS0 SBUF never reaches the SFR); NOT a T2 pass"

echo "BS4-DEMO-E2E: PASS (T2 byte-level results are scoped to the AS6 header overlay;"
echo "  the corpus v1/AS0 header flavor is the registered 0-byte FINDING-S4b outcome, not a pass)"
case "$KEEP" in 1) echo "kept: $OUT";; *) rm -rf -- "$OUT";; esac
