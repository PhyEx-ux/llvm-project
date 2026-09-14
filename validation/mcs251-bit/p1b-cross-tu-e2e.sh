#!/usr/bin/env bash
# p1b-cross-tu-e2e.sh - P09 P-1b acceptance: two real C translation units,
# compiled independently by clang, lowered to ELF objects by llc, and linked
# by mcs251-lld with the approved S1 CRT (crt-irq.yaml: owned window clear +
# XINIT walker). This closes the P-1b loop the lit IR tests cannot: the
# bit-object records, the cross-TU slot identity, the initial values, and the
# BITADDR8 bit numbers written into the actual machine instructions.
#
# Assertions (P09-BIT-CODEGEN-DESIGN §2.1/§2.2, §7 P-1b criteria):
#   1. TU1's definition and TU2's extern resolve to exactly ONE bit slot
#      (_shared_flag), with the normalized init 1.
#   2. The two same-named internal statics (hidden_a) get two DISTINCT slots
#      (no cross-TU merge), carrying their own inits (1 and 0).
#   3. The aggregated backing byte has mask 0x07 value 0x03 (inits from both
#      TUs), owner crt-pool, init crt-clear+xinit.
#   4. The linked machine code addresses the exact map bit numbers:
#      producer = CPL <shared> + CLR <tu1 hidden>; consumer = SETB/CLR
#      <shared> pair (one write per path) + CPL <tu2 hidden>.
#   5. Swapping the TU order keeps the per-SYMBOL slot values identical (one
#      shared slot init 1, two distinct internal slots init 1/0); the
#      aggregated backing byte is position-dependent under the swapped
#      allocation, so it is recomputed from the swapped map and must match;
#      a same-order re-link is byte-identical (reproducibility).
#
# Tools (three build trees; override via env):
#   CLANG   /home/liu/build-mcs251-s1/bin/clang   (this worktree, P-1b)
#   LLC     /home/liu/build-mcs251/bin/llc        (P-1a backend)
#   LLD     /home/liu/build-mcs251-lld/bin/mcs251-lld
#   YAML2OBJ, READHEX via the lld build tree
#
# Usage: p1b-cross-tu-e2e.sh [--keep]
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "$0")" && pwd)
REPO=${REPO:-$(cd -- "$ROOT/../.." && pwd)}
OUT="$ROOT/build/p1b-cross-tu"

CLANG=${CLANG:-/home/liu/build-mcs251-s1/bin/clang}
LLC=${LLC:-/home/liu/build-mcs251/bin/llc}
LLD=${LLD:-/home/liu/build-mcs251-lld/bin/mcs251-lld}
YAML2OBJ=${YAML2OBJ:-/home/liu/build-mcs251-lld/bin/yaml2obj}
DUMPBYTES="$REPO/lld/test/MCS251/Inputs/dump-elf-bytes.py"
CONTRACT=${CONTRACT:-1,1,32,8,1}
CRT_YAML=${CRT_YAML:-$REPO/validation/mcs251-elf/runtime/crt-irq.yaml}

rm -rf -- "$OUT"
mkdir -p -- "$OUT"

# --- compile: two real C TUs + a tiny firmware entry, independently ---
# (clang -cc1 is the exact %clang_cc1 form of the lit tests; each TU gets no
# knowledge of the other.)
cat > "$OUT/fw.c" <<'EOF'
void producer(void);
int consumer(int);
int main(void) {
  producer();
  return consumer(1);
}
EOF
for tu in tu1 tu2; do
  "$CLANG" -cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil \
    -mcs251-memory-contract="$CONTRACT" \
    -emit-llvm -o "$OUT/$tu.ll" "$ROOT/p1b-$tu.c"
done
"$CLANG" -cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil \
  -mcs251-memory-contract="$CONTRACT" \
  -emit-llvm -o "$OUT/fw.ll" "$OUT/fw.c"

for tu in tu1 tu2 fw; do
  "$LLC" -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf \
    -mcs251-memory-contract="$CONTRACT" \
    "$OUT/$tu.ll" -o "$OUT/$tu.o"
done
if [ -n "${CRT_OBJ:-}" ]; then
  # Pre-built CRT object (e.g. gen-crt-v2.sh --identity v2): yaml2obj cannot
  # emit the v2 e_flags word, so v2-identity runs pass a finished object.
  cp "$CRT_OBJ" "$OUT/crt.o"
else
  "$YAML2OBJ" "$CRT_YAML" -o "$OUT/crt.o"
fi
echo "compile: tu1/tu2/fw clang -cc1 + llc ELF OK"

# --- link (canonical order); run from $OUT so the map names objects by
# their basenames (tu1.o:_hidden_a), independent of the absolute build path ---
link() { # out-prefix object-order...
  local pfx=$1; shift
  (cd "$OUT" && "$LLD" "$@" \
    --area-start=HOME=0xff0000 --area-start=BOOT=0xff0210 \
    --area-start=CSEG=0xff0320 --area-start=XINIT=0xff8000 \
    --map="$(basename "$pfx").map" -o "$(basename "$pfx").elf")
}
link "$OUT/a" crt.o fw.o tu1.o tu2.o
echo "link: crt + fw + tu1 + tu2 OK"

# --- 1/2/3: map identity and initial values ---
MAP="$OUT/a.map"
line_shared=$(grep -c '^BIT _shared_flag ' "$MAP")
line_h1=$(grep '^BIT tu1.o:_hidden_a ' "$MAP")
line_h2=$(grep '^BIT tu2.o:_hidden_a ' "$MAP")
line_byte=$(grep '^BITBYTE ' "$MAP")
[ "$line_shared" -eq 1 ] || { echo "FAIL: expected exactly one _shared_flag slot, got $line_shared" >&2; exit 1; }
grep -q '^BIT _shared_flag = 0x[0-9a-f]* byte 0x20 index [0-7] init 1$' "$MAP" || {
  echo "FAIL: _shared_flag slot/init wrong: $(grep '^BIT _shared_flag' "$MAP")" >&2; exit 1; }
b1=$(sed -n 's/^BIT tu1.o:_hidden_a = 0x\([0-9a-f]*\).*/\1/p' "$MAP")
b2=$(sed -n 's/^BIT tu2.o:_hidden_a = 0x\([0-9a-f]*\).*/\1/p' "$MAP")
[ -n "$b1" ] && [ -n "$b2" ] || { echo "FAIL: internal hidden_a slots missing" >&2; exit 1; }
[ "$b1" != "$b2" ] || { echo "FAIL: same-named internals merged to one slot $b1" >&2; exit 1; }
echo "$line_h1" | grep -q 'init 1$' || { echo "FAIL: tu1 hidden_a init wrong" >&2; exit 1; }
echo "$line_h2" | grep -q 'init 0$' || { echo "FAIL: tu2 hidden_a init wrong" >&2; exit 1; }
echo "$line_byte" | grep -q '^BITBYTE 0x20 mask 0x07 value 0x03 ' || {
  echo "FAIL: backing byte mask/value wrong: $line_byte" >&2; exit 1; }
echo "map: one shared slot (init 1), two distinct internal slots (init 1/0), byte 0x20 mask 07 value 03"

# --- 4: the machine code addresses the exact map bit numbers ---
# bit numbers from the map; opcodes: CPL=b2 SETB=d2 CLR=c2. The dumps are
# single lines of space-separated hex; matching with word boundaries (not
# fixed byte pairs) because the -O0 instruction stream around each bit
# instruction is variable-length (stack spills), so a fixed pairing would be
# misaligned from the real instruction boundaries.
shared_n=$(printf '%d' "0x$(sed -n 's/^BIT _shared_flag = 0x\([0-9a-f]*\).*/\1/p' "$MAP")")
h1_n=$(printf '%d' "0x$b1")
h2_n=$(printf '%d' "0x$b2")
tu1_text=$(sed -n 's/^tu1.o:.text 0x\([0-9a-f]*\) .*/\1/p' "$MAP")
tu2_text=$(sed -n 's/^tu2.o:.text 0x\([0-9a-f]*\) +0x\([0-9a-f]*\).*/\1 \2/p' "$MAP")
tu2_start=${tu2_text%% *}; tu2_size=${tu2_text##* }
tu1_start_dec=$((0x$tu1_text)); tu2_start_dec=$((0x$tu2_start)); tu2_end_dec=$((tu2_start_dec + 0x$tu2_size))

dump() { python3 "$DUMPBYTES" "$OUT/a.elf" "$1" "$2"; }
has_op() { # <bytes> <opcode> <bit-number> : opcode followed by the bit number
  printf '%s\n' "$1" | grep -qE "(^| )$2 $(printf '%02x' "$3")( |$)"
}

# producer: CPL shared ; CLR tu1-hidden  (its whole .text is 5 bytes here,
# but scan a bounded window from its start; exact instruction bytes asserted)
prod_window=$(dump "$tu1_start_dec" 5)
has_op "$prod_window" b2 "$shared_n" || {
  echo "FAIL: producer CPL shared_flag (bit $shared_n) not found in: $prod_window" >&2; exit 1; }
has_op "$prod_window" c2 "$h1_n" || {
  echo "FAIL: producer CLR tu1 hidden_a (bit $h1_n) not found in: $prod_window" >&2; exit 1; }

# consumer: SETB shared + CLR shared (the dynamic-write pair) ; CPL tu2-hidden
cons_bytes=$(dump "$tu2_start_dec" $((tu2_end_dec - tu2_start_dec)))
has_op "$cons_bytes" d2 "$shared_n" || {
  echo "FAIL: consumer SETB shared_flag (bit $shared_n) not found" >&2; exit 1; }
has_op "$cons_bytes" c2 "$shared_n" || {
  echo "FAIL: consumer CLR shared_flag (bit $shared_n) not found" >&2; exit 1; }
has_op "$cons_bytes" b2 "$h2_n" || {
  echo "FAIL: consumer CPL tu2 hidden_a (bit $h2_n) not found" >&2; exit 1; }
echo "code: CPL/set/clear instructions carry the exact map bit numbers (shared=$shared_n tu1hidden=$h1_n tu2hidden=$h2_n)"

# --- 5: order swap per-symbol value-invariance + same-order byte reproducibility
# The order-invariant facts are the per-symbol slot values: still exactly one
# shared slot with init 1, still two distinct internal slots carrying their
# own inits (tu1's 1, tu2's 0). The aggregated backing byte VALUE is
# position-dependent (the swapped order allocates different bit indexes), so
# it is recomputed from the swapped map's own slot list and compared to what
# lld wrote.
link "$OUT/b" crt.o fw.o tu2.o tu1.o
BMAP="$OUT/b.map"
[ "$(grep -c '^BIT _shared_flag ' "$BMAP")" -eq 1 ] || { echo "FAIL: swapped order lost the single shared slot" >&2; exit 1; }
grep -q '^BIT _shared_flag = 0x[0-9a-f]* byte 0x20 index [0-7] init 1$' "$BMAP" || {
  echo "FAIL: swapped order changed the shared slot value" >&2; exit 1; }
sb1=$(sed -n 's/^BIT tu1.o:_hidden_a = 0x\([0-9a-f]*\).*/\1/p' "$BMAP")
sb2=$(sed -n 's/^BIT tu2.o:_hidden_a = 0x\([0-9a-f]*\).*/\1/p' "$BMAP")
[ -n "$sb1" ] && [ -n "$sb2" ] && [ "$sb1" != "$sb2" ] || {
  echo "FAIL: swapped order merged/lost the internal slots" >&2; exit 1; }
grep -q "^BIT tu1.o:_hidden_a = 0x[0-9a-f]* byte 0x20 index [0-7] init 1\$" "$BMAP" || {
  echo "FAIL: swapped order changed tu1 hidden_a init" >&2; exit 1; }
grep -q "^BIT tu2.o:_hidden_a = 0x[0-9a-f]* byte 0x20 index [0-7] init 0\$" "$BMAP" || {
  echo "FAIL: swapped order changed tu2 hidden_a init" >&2; exit 1; }
bval=$(sed -n 's/^BITBYTE 0x20 mask 0x07 value 0x\([0-9a-f]*\) .*/\1/p' "$BMAP")
bexp=$(awk '/^BIT .* byte 0x20 index [0-7] init 1$/ {
  for (i = 1; i <= NF; i++) if ($i == "index") {
    w = 1; for (j = 0; j < $(i + 1); j++) w *= 2; Sum += w; break } }
  END { printf "%02x", Sum }' "$BMAP")
[ "$bval" = "$bexp" ] || {
  echo "FAIL: swapped backing byte value $bval != OR of its init-1 slots $bexp" >&2; exit 1; }
echo "swap: TU order exchanged -- per-symbol values unchanged (shared init 1, internals 1/0), byte value $bval matches the swapped slot layout"

link "$OUT/a2" crt.o fw.o tu1.o tu2.o
if cmp -s "$OUT/a.elf" "$OUT/a2.elf"; then
  echo "rerun: same-order relink is byte-identical"
else
  echo "FAIL: same-order relink is not byte-reproducible" >&2; exit 1
fi

echo "P1B-CROSS-TU: PASS"
case " ${1:-} " in *" --keep "*) echo "kept: $OUT";; *) rm -rf -- "$OUT";; esac
