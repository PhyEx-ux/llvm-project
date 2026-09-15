#!/usr/bin/env bash
# vararg-crosstu-e2e.sh - G2 B-S3 acceptance: the P-4 variadic interaction
# matrix over REAL clang products plus the cross-TU variadic value chain
# executed under QEMU (G2-VARIADIC-DESIGN-draft.md R3, section 4.7 matrix +
# section 6 B-S3 gates 1/2/3/5).
#
# VERIFICATION LEVEL: real compilation and real execution. Every object is
# produced by clang -cc1 + llc under the v2 contract (1,2,32,8,1), so each
# carries .mcs251.attributes with a genuine Tag 28 record; the firmware runs
# on qemu-system-mcs251 (machine stc32g144k246, TCG) and the assertions read
# the serial transcript. No yaml stand-in is used anywhere except the ONE
# case no toolchain can produce anymore: a v2 object without Tag 28, built
# by stripping the record out of a real object's yaml (pre-P-4 shape).
#
# What is asserted:
#   1. SLOT CONTRACT: the definition TU (vararg-crosstu-callee.c) emits the
#      six 4-byte continuation slots _vchain_PARM_2.._PARM_7 as OBJECT
#      globals; the caller TU references exactly those names undefined.
#      Cross-TU slot pairing is pure symbol identity (design 4.4: no
#      contract needs to be transmitted - P-4 param_count compares it).
#   2. SIGNATURE RECORDS: the definition TU publishes role 9 (definition +
#      variadic bit3), the caller TU role 10 (declaration + bit3), both
#      param_count 1 - pinned straight from the emitted IR metadata.
#   3. P-4 MATRIX (section 4.7; hard errors verified verbatim, exit != 0,
#      and NO output file written):
#        P1  variadic def x variadic caller               -> links;
#        P2  variadic def (count 1) x fixed 7-param decl
#            (count 7)                                    -> "parameter count
#            conflict ... : 1 vs 7" (the count arm fires before bit3);
#        P3a fixed 7-param definition (the pre-migration runtime printf
#            shape: bit3=0, count=7) x variadic caller     -> "parameter
#            count conflict ... : 7 vs 1" (the "forgot to rebuild the
#            runtime" proof, fail-closed);
#        P3b v2 object with no Tag 28 at all              -> "required
#            function_signatures(28) is missing" (the pre-P-4 v2-object
#            rejection; variadic changes nothing about it);
#        P4  fixed long mix(long,char,long) def x same-proto caller -> links;
#        P5  fixed def count 3 x fixed decl count 2       -> "parameter
#            count conflict ... : 3 vs 2";
#        P6  count and bitmap identical, ONLY bit3 differs (two
#            declaration-only TUs)                        -> "disagree on
#            variadic-ness (bit3 differs)".
#   4. >CAP: a caller TU passing 7 variadic arguments is rejected by Sema
#      with the frozen cap diagnostic and produces no IR/object at all
#      (B-S1 message A; nothing reaches llc or lld).
#   5. VALUE CHAIN: two-TU firmware (v2 crt + caller + callee) and a fused
#      single-TU build of the same program produce BYTE-IDENTICAL UART
#      transcripts, equal to the golden
#        0000005A 000003E8 00000007 00011170 00000079 00000000 PASS\n S
#      ('Z'/1000/7/70000 are the promoted scalar slots, 00000079 is a
#      pointer formed in the caller TU and dereferenced inside the callee
#      TU through va_arg(ap, char*), 'S' is the CRT's post-main marker).
#
# Tools (override via env):
#   CLANG     /home/liu/build-mcs251-s1/bin/clang   (this worktree, B-S2)
#   LLC       /home/liu/build-mcs251/bin/llc
#   LLD       /home/liu/build-mcs251-lld/bin/mcs251-lld
#   OBJ2YAML, YAML2OBJ, READELF, READOBJ, OBJCOPY, QEMU
#
# Usage: vararg-crosstu-e2e.sh [--keep]
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "$0")" && pwd)
OUT="$ROOT/build/vararg-crosstu"

CLANG=${CLANG:-/home/liu/build-mcs251-s1/bin/clang}
LLC=${LLC:-/home/liu/build-mcs251/bin/llc}
LLD=${LLD:-/home/liu/build-mcs251-lld/bin/mcs251-lld}
OBJ2YAML=${OBJ2YAML:-/home/liu/build-mcs251-lld/bin/obj2yaml}
YAML2OBJ=${YAML2OBJ:-/home/liu/build-mcs251-lld/bin/yaml2obj}
READELF=${READELF:-/home/liu/build-mcs251-s1/bin/llvm-readelf}
READOBJ=${READOBJ:-/home/liu/build-mcs251-s1/bin/llvm-readobj}
OBJCOPY=${OBJCOPY:-/home/liu/build-mcs251-s1/bin/llvm-objcopy}
QEMU=${QEMU:-/home/liu/build-qemu/qemu-system-mcs251}
MACHINE=${MACHINE:-stc32g144k246}
GEN_CRT_V2="$ROOT/../mcs251-elf/runtime/gen-crt-v2.sh"
QEMU_TIMEOUT=${QEMU_TIMEOUT:-60}

# The v2 contract: Tag 28 carriers. AS0 32-bit, XSmall placement, exactly
# the registered profile the P-4 signature protocol rides on.
CONTRACT=1,2,32,8,1

for t in "$CLANG" "$LLC" "$LLD" "$OBJ2YAML" "$YAML2OBJ" "$READELF" \
         "$READOBJ" "$OBJCOPY" "$QEMU"; do
  command -v "$t" >/dev/null || { echo "FAIL: missing tool: $t" >&2; exit 1; }
done
[ -f "$GEN_CRT_V2" ] || { echo "FAIL: missing fixture: $GEN_CRT_V2" >&2; exit 1; }
for f in "$ROOT/vararg-crosstu-callee.c" "$ROOT/vararg-crosstu-caller.c"; do
  [ -f "$f" ] || { echo "FAIL: missing TU: $f" >&2; exit 1; }
done

rm -rf -- "$OUT"
mkdir -p -- "$OUT"

compile_tu() { # <src> <out.o>
  local src=$1 obj=$2
  "$CLANG" -cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil \
    -mcs251-memory-contract="$CONTRACT" -emit-llvm \
    -o "${obj%.o}.ll" "$src"
  "$LLC" -mtriple=mcs251 -mcs251-memory-contract="$CONTRACT" \
    -mcs251-object-format=elf -filetype=obj "${obj%.o}.ll" -o "$obj"
}

# --- 1/2: the two value-chain TUs, independently compiled ---------------
compile_tu "$ROOT/vararg-crosstu-callee.c" "$OUT/callee.o"
compile_tu "$ROOT/vararg-crosstu-caller.c" "$OUT/caller.o"
echo "compile: callee/caller (clang -cc1 + llc, v2 contract) OK"

# signature records straight from the emitted IR: role 9 vs role 10
grep -Eq '^![0-9]+ = !\{!"_vchain", i32 9, i32 0, i32 0\}$' "$OUT/callee.ll" || {
  echo "FAIL: definition TU does not record role 9 (definition+bit3) for _vchain" >&2
  exit 1; }
grep -Eq '^![0-9]+ = !\{!"_vchain", i32 10, i32 0, i32 0\}$' "$OUT/caller.ll" || {
  echo "FAIL: caller TU does not record role 10 (declaration+bit3) for _vchain" >&2
  exit 1; }
echo "records: _vchain role 9 (def+bit3) in callee TU, role 10 (decl+bit3) in caller TU, param_count 1 both"

# slot contract: callee defines the six 4-byte continuation slots, caller
# references exactly those names undefined (cross-TU slot identity)
sym_line() { # <obj> <name> -> "size type bind name" or empty
  "$READELF" -s "$1" | awk -v n="$2" '$8 == n { print $3, $4, $5, $8; exit }'
}
for n in 2 3 4 5 6 7; do
  got=$(sym_line "$OUT/callee.o" "_vchain_PARM_$n")
  [ "$got" = "4 OBJECT GLOBAL _vchain_PARM_$n" ] || {
    echo "FAIL: callee slot _vchain_PARM_$n is '$got', expected '4 OBJECT GLOBAL'" >&2
    exit 1; }
  got=$(sym_line "$OUT/caller.o" "_vchain_PARM_$n")
  [ "$got" = "0 NOTYPE GLOBAL _vchain_PARM_$n" ] || {
    echo "FAIL: caller reference to _vchain_PARM_$n is '$got', expected undefined" >&2
    exit 1; }
done
echo "slots: callee defines _vchain_PARM_2..7 (4B OBJECT GLOBAL each); caller references them undefined"

# --- 3: the P-4 matrix over real clang products --------------------------
# All links use the full frozen area recipe: the caller TU owns initialized
# data (g), so CSEG alone is not enough for the positive cases, and the
# negative cases must fail on the SIGNATURE gate, never on a layout gate.
AREAS="--area-start=HOME=0xff0000 --area-start=VECS=0xff0003 --area-start=BOOT=0xff0100 --area-start=CSEG=0xff0200 --area-start=XINIT=0xff8000 --area-start=XDATA_INIT=0xff9000 --area-start=XSEG=0x010000 --edata-end=0x3fff"
link_ok() { # <tag> <obj...> : must link and produce an output
  local tag=$1; shift
  local out="$OUT/$tag.elf"
  (cd "$OUT" && "$LLD" "$@" $AREAS \
      --map="$tag.map" -o "$tag.elf") || {
    echo "FAIL[$tag]: link unexpectedly failed" >&2; exit 1; }
  [ -f "$out" ] || { echo "FAIL[$tag]: no output file" >&2; exit 1; }
}
link_reject() { # <tag> <verbatim-line> <obj...> : hard error + no output
  local tag=$1 expect=$2; shift 2
  local out="$OUT/$tag.elf" log="$OUT/$tag.log"
  if (cd "$OUT" && "$LLD" "$@" $AREAS -o "$tag.elf") \
      >"$log" 2>&1; then
    echo "FAIL[$tag]: link unexpectedly succeeded" >&2; exit 1
  fi
  grep -Fqx "$expect" "$log" || {
    echo "FAIL[$tag]: diagnostic mismatch." >&2
    echo "  expected: $expect" >&2
    echo "  got     :" >&2; sed 's/^/    /' "$log" >&2
    exit 1; }
  [ ! -e "$out" ] || {
    echo "FAIL[$tag]: output file written despite the hard error" >&2; exit 1; }
}

cat > "$OUT/p2fixed.c" <<'EOF'
int vchain(int, int, int, int, int, int, int);
int main(void) { return vchain(1, 2, 3, 4, 5, 6, 7); }
EOF
cat > "$OUT/p3old.c" <<'EOF'
/* pre-migration runtime printf shape: fixed fmt+6 parameters, bit3=0,
 * param_count 7 - the Tag 28 role/count an old runtime object carries
 * when it was not rebuilt (B-S4 migrates printf to count 1, bit3 1) */
int vchain(int a, int b, int c, int d, int e, int f, int g) { return a + g; }
EOF
cat > "$OUT/p4def.c" <<'EOF'
long mix(long a, char b, long c) { return a + b + c; }
EOF
cat > "$OUT/p4call.c" <<'EOF'
long mix(long, char, long);
int main(void) { return mix(1, 2, 3); }
EOF
cat > "$OUT/p5call.c" <<'EOF'
long mix(long, char);
int main(void) { return mix(1, 2); }
EOF
cat > "$OUT/p6a.c" <<'EOF'
void f(int, ...);
EOF
cat > "$OUT/p6b.c" <<'EOF'
void f(int);
EOF
for tu in p2fixed p3old p4def p4call p5call p6a p6b; do
  compile_tu "$OUT/$tu.c" "$OUT/$tu.o"
done
echo "compile: matrix TUs OK"

# P3b fixture: strip the Tag 28 record out of the real caller object's yaml
# (the pre-P-4 v2 shape; llc itself refuses to emit a v2 object without the
# !mcs251.signatures metadata, so the strip is the only faithful way to
# build one).  The strip rebuilds both envelope size fields (VendorSize =
# sh_size - 1, ScopeSize = VendorSize - 11).
"$OBJ2YAML" "$OUT/caller.o" -o "$OUT/caller.yaml"
cp "$OUT/caller.yaml" "$OUT/caller-nosig.yaml"
python3 - "$OUT/caller-nosig.yaml" <<'PYEOF'
import pathlib, re, struct, sys
t = pathlib.Path(sys.argv[1])
text = t.read_text()
m = re.search(r'(- Name:\s*\.mcs251\.attributes\n(?:.*\n)*?\s*Content:\s*)([0-9A-F]+)', text)
b = bytes.fromhex(m.group(2))
assert b[0] == 0x41 and b[5:12] == b'MCS251\0' and b[12] == 0x01

def uleb(bb, i):
    v = s = 0
    while True:
        x = bb[i]; i += 1; v |= (x & 0x7f) << s; s += 7
        if not (x & 0x80):
            return v, i

i, out, seen = 17, bytearray(b[:17]), False
while i < len(b):
    tag, i = uleb(b, i); typ = b[i]; i += 1
    ln, i = uleb(b, i); val = b[i:i + ln]; i += ln
    if tag == 28:
        seen = True
        continue
    assert tag < 128 and ln < 128, (tag, ln)
    out += bytes([tag, typ, ln]) + val
assert seen, "no Tag 28 record found"
out[1:5] = struct.pack('>I', len(out) - 1)     # VendorSize = sh_size - 1
out[13:17] = struct.pack('>I', len(out) - 12)  # ScopeSize = VendorSize - 11
t.write_text(text[:m.start(2)] + out.hex().upper() + text[m.end(2):])
print("Tag 28 stripped from caller-nosig.yaml")
PYEOF
"$YAML2OBJ" "$OUT/caller-nosig.yaml" -o "$OUT/caller-nosig.o"
python3 - "$OUT/caller-nosig.o" <<'PYEOF'
import pathlib, sys
p = pathlib.Path(sys.argv[1]); b = bytearray(p.read_bytes())
b[36:40] = bytes.fromhex("00000102")  # yaml2obj emits flags 0; restore v2
p.write_bytes(bytes(b))
PYEOF

# P1: variadic x variadic - links.  A successful link already proves the
# signature comparison passed AND every external resolved: errorUndefined()
# runs before any output file exists, and the caller's relocations to
# _vchain and _vchain_PARM_2..7 were applied (the object-level slot pairing
# is pinned above; the value path is pinned by the QEMU run below).
link_ok p1 callee.o caller.o
echo "P1: variadic def x variadic caller LINKS (real clang products)"

# P2: variadic definition x fixed 7-param declaration
link_reject p2 \
  "mcs251 linker: error: p2fixed.o: MCS251 signatures: records '_vchain' have a parameter count conflict (callee.o vs p2fixed.o): 1 vs 7" \
  callee.o p2fixed.o
echo "P2: rejected, parameter count conflict 1 vs 7 (count arm precedes bit3)"

# P3a: fixed 7-param definition (old runtime shape) x variadic caller
link_reject p3a \
  "mcs251 linker: error: caller.o: MCS251 signatures: records '_vchain' have a parameter count conflict (p3old.o vs caller.o): 7 vs 1" \
  p3old.o caller.o
echo "P3a: old-runtime stand-in x variadic caller rejected, count conflict 7 vs 1"

# P3b: v2 object with no Tag 28 at all
link_reject p3b \
  "mcs251 linker: error: caller-nosig.o: MCS251 attributes: required function_signatures(28) is missing" \
  callee.o caller-nosig.o
echo "P3b: v2 object without Tag 28 rejected by the existing pre-P-4 gate"

# P4: fixed signatures agreeing on count and bitmap
link_ok p4 p4def.o p4call.o
echo "P4: fixed mix(long,char,long) def x same-proto caller LINKS"

# P5: fixed count disagreement
link_reject p5 \
  "mcs251 linker: error: p5call.o: MCS251 signatures: records '_mix' have a parameter count conflict (p4def.o vs p5call.o): 3 vs 2" \
  p4def.o p5call.o
echo "P5: rejected, parameter count conflict 3 vs 2"

# P6: identical count and bitmap, ONLY role bit3 differs
link_reject p6 \
  "mcs251 linker: error: p6b.o: MCS251 signatures: records '_f' disagree on variadic-ness (bit3 differs) (p6a.o vs p6b.o)" \
  p6a.o p6b.o
echo "P6: rejected, disagree on variadic-ness (bit3 differs) with counts equal"

# --- 4: >cap caller TU dies at Sema, no IR, no object --------------------
cat > "$OUT/cap7.c" <<'EOF'
int vchain(int n, ...);
int main(void) { return vchain(6, 1, 2, 3, 4, 5, 6, 7); }
EOF
if "$CLANG" -cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil \
     -mcs251-memory-contract="$CONTRACT" -emit-llvm \
     -o "$OUT/cap7.ll" "$OUT/cap7.c" > "$OUT/cap7.log" 2>&1; then
  echo "FAIL[cap7]: over-cap variadic call compiled" >&2; exit 1
fi
grep -Fq "MCS251 variadic call exceeds the fixed 6-slot variadic ABI cap (7 variadic arguments given)" \
  "$OUT/cap7.log" || {
    echo "FAIL[cap7]: frozen cap diagnostic missing:" >&2; cat "$OUT/cap7.log" >&2; exit 1; }
[ ! -e "$OUT/cap7.ll" ] || {
  echo "FAIL[cap7]: compiler produced IR despite the hard error" >&2; exit 1; }
echo "cap7: >cap variadic caller rejected at Sema (message A), no IR/object produced"

# --- 5: the cross-TU value chain under QEMU ------------------------------
bash "$GEN_CRT_V2" --yaml2obj "$YAML2OBJ" --readobj "$READOBJ" \
  --variant selfstart --identity v2 --out "$OUT/crt.o" >/dev/null

# the fused single-TU build of the very same program (fused.c includes both
# TUs; -O0 keeps the call, so the value path is identical to the two-TU one)
cat > "$OUT/fused.c" <<'EOF'
#include "vararg-crosstu-callee.c"
#include "vararg-crosstu-caller.c"
EOF
"$CLANG" -cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil \
  -mcs251-memory-contract="$CONTRACT" -I "$ROOT" -emit-llvm \
  -o "$OUT/fused.ll" "$OUT/fused.c"
"$LLC" -mtriple=mcs251 -mcs251-memory-contract="$CONTRACT" \
  -mcs251-object-format=elf -filetype=obj "$OUT/fused.ll" -o "$OUT/fused.o"
echo "compile: fused single-TU variant OK"

link_fw() { # <tag> <obj...>
  local tag=$1; shift
  (cd "$OUT" && "$LLD" "$@" $AREAS \
    --map="fw-$tag.map" -o "fw-$tag.elf")
  "$OBJCOPY" -O ihex "$OUT/fw-$tag.elf" "$OUT/fw-$tag.hex"
}
link_fw 2tu crt.o caller.o callee.o
link_fw 1tu crt.o fused.o
grep -q '^callee.o:.text' "$OUT/fw-2tu.map" || {
  echo "FAIL[fw-2tu]: callee .text missing from the firmware map" >&2; exit 1; }
grep -q '^caller.o:.text' "$OUT/fw-2tu.map" || {
  echo "FAIL[fw-2tu]: caller .text missing from the firmware map" >&2; exit 1; }
echo "link: 2-TU and 1-TU firmwares (v2 crt, Tag 28 in every object) OK"

run_qemu() { # <tag>
  local tag=$1 serial="$OUT/fw-$tag.serial"
  : > "$serial"
  ( for i in $(seq 1 $((QEMU_TIMEOUT * 2))); do
      if [ -s "$serial" ] && tail -c 256 "$serial" 2>/dev/null | \
          grep -q 'PASS'; then
        pkill -TERM -f "fw-$tag.hex" 2>/dev/null
        exit 0
      fi
      sleep 0.5
    done ) &
  local watcher=$!
  timeout --foreground "$QEMU_TIMEOUT" "$QEMU" -M "$MACHINE" \
    -bios "$OUT/fw-$tag.hex" -accel tcg -display none -monitor none \
    -serial "file:$serial" \
    > "$serial.qemu.stdout" 2> "$serial.qemu.stderr" || true
  kill "$watcher" 2>/dev/null || true
  wait "$watcher" 2>/dev/null || true
  [ -s "$serial" ] || {
    echo "FAIL[$tag]: no serial output (QEMU stderr:)" >&2
    head -5 "$serial.qemu.stderr" >&2
    exit 1; }
}

printf '0000005A000003E800000007000111700000007900000000PASS\nS' > "$OUT/golden.raw"
for tag in 2tu 1tu; do
  run_qemu "$tag"
  if ! cmp -s "$OUT/fw-$tag.serial" "$OUT/golden.raw"; then
    echo "FAIL[$tag]: transcript is not the golden:" >&2
    od -An -tx1z "$OUT/fw-$tag.serial" >&2
    echo "expected:" >&2
    od -An -tx1z "$OUT/golden.raw" >&2
    exit 1
  fi
done
echo "qemu: 2-TU transcript == 1-TU transcript == golden (6 slot values incl. cross-TU pointer deref 79)"

echo "VARARG-CROSSTU: PASS"
case " ${1:-} " in *" --keep "*) echo "kept: $OUT";; *) rm -rf -- "$OUT";; esac
