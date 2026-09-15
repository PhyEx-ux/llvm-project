#!/usr/bin/env bash
# signature-zeroparam-e2e.sh - zero-parameter protocol (PM ruling 2026-09-15,
# review revision) writer->reader end-to-end regression.
#
# Background: the first implementation of the ruling forced ParamCount to 0 on
# every K&R record, which made the bit2 gate dead -- a K&R `int f(x) int x;`
# definition was written as a prototyped zero-parameter record and linked
# cleanly against another TU's `int f(void)` (the "bad.elf" hole Alice
# found).  The revision records bit2 verbatim and lets a K&R record carry its
# REAL parameter list; the reader keeps the both-sides-param_count-0 bit2
# tolerance, which is what keeps the demo-76 `void main()` definition
# linkable against the CRT's `void main(void)` declaration.
#
# VERIFICATION LEVEL: real toolchain.  Every object here is produced by
# clang + llc under the v2 contract (1,2,32,8,1) so each carries a genuine
# Tag 28; the link verdicts come from mcs251-lld itself.  The five frozen
# shapes, plus the both-K&R control:
#
#   form 1  demo-76 shape: K&R `void main()` definition (role 5, count 0)
#           vs a prototyped `void main(void)` declaration (role 2, count 0)
#                                                                  -> links;
#   form 2  K&R `int f(x) int x;` definition (role 5, count 1) vs
#           `int f(void)` declaration (role 2, count 0)   -> REJECTED
#           ("disagree on prototype-ness"; this is the bad.elf regression);
#   form 3  prototyped zero-parameter definition + declaration   -> links;
#   form 4  prototyped one-parameter definition + declaration    -> links;
#   form 5  K&R zero-parameter definition vs prototyped
#           `void k(void)` declaration (bit2 differs, both count 0) -> links;
#   form 6  control: K&R definition vs K&R declaration, both bit2=1
#           (compare (ret, call_abi) only)                        -> links.
#
# Review round 3 additions (compareRecords is NOT transitive, so the linker
# must compare ALL same-name record pairs):
#   triple  the three legal _f records A (`int f();`, role 6, count 0),
#           B (K&R definition, role 5, count 1) and C (`int f(void);`,
#           role 2, count 0) -- A-B and A-C are compatible, B-C conflicts,
#           and A does not exempt the B-C pair.  All SIX command-line
#           permutations of the three objects are rejected;
#   ctrl-1  A + C + a prototyped zero-param definition (role 1, count 0):
#           every pair compatible -> links;
#   ctrl-2  K&R def (5,0) + K&R decl (6,0) + prototyped decl (2,0) of _k:
#           every pair compatible -> links;
#   mutation  the round-2 review experiment: growing the zero-param `_main`
#           record to count=1 must break the IR pin AND flip the link
#           verdict against the CRT declaration to REJECT.
#
# The IR metadata of every shape is pinned before linking (complete-node
# match, operand list exact), so a writer regression cannot hide behind the
# reader.
#
# Tools (override via env):
#   CLANG /home/liu/build-mcs251-s1/bin/clang
#   LLC   /home/liu/build-mcs251-s1/bin/llc
#   LLD   /home/liu/build-mcs251-lld/bin/mcs251-lld
#
# Usage: signature-zeroparam-e2e.sh [--keep]
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "$0")" && pwd)
OUT="$ROOT/build/signature-zeroparam"

CLANG=${CLANG:-/home/liu/build-mcs251-s1/bin/clang}
LLC=${LLC:-/home/liu/build-mcs251-s1/bin/llc}
LLD=${LLD:-/home/liu/build-mcs251-lld/bin/mcs251-lld}

CONTRACT=1,2,32,8,1

for t in "$CLANG" "$LLC" "$LLD"; do
  command -v "$t" >/dev/null || { echo "FAIL: missing tool: $t" >&2; exit 1; }
done

KEEP=0
[ "${1:-}" = "--keep" ] && KEEP=1
if [ "$KEEP" = 0 ]; then
  # cd out first: rm -rf of the process cwd can be refused.  The rmdir cleans
  # the (now empty) shared "build" parent this script created via mkdir -p.
  trap 'cd / && { rm -rf -- "$OUT" && rmdir -- "$ROOT/build"; } 2>/dev/null' EXIT
fi
rm -rf -- "$OUT"
mkdir -p -- "$OUT"
cd -- "$OUT"

write_tu() { # <stem> <content...>
  local stem=$1; shift
  cat > "$stem.c"
  echo "$stem.c"
}

compile_tu() { # <stem>
  local stem=$1
  "$CLANG" --target=mcs251-unknown-none -std=c11 -O0 -fmcs251-keil \
    -Xclang -mcs251-memory-contract="$CONTRACT" \
    -S -emit-llvm "$stem.c" -o "$stem.ll" 2>/dev/null
  "$LLC" -mtriple=mcs251 -mcs251-memory-contract="$CONTRACT" \
    -mcs251-object-format=elf -filetype=obj "$stem.ll" -o "$stem.o"
}

# The five shapes + the control pair.  Every TU that references a K&R
# function keeps its reference in an unused-at-O0 wrapper so the declaration
# is recorded, exactly like a CRT or caller TU would.
write_tu main-kr <<'EOF'
/* form 1: the demo-76 shape -- `void main()` is a K&R no-prototype
   zero-parameter definition (role 5, param_count 0). */
void main() { }
EOF

write_tu crt-standin <<'EOF'
/* form 1 counterpart: the prototyped `_main` declaration the CRT carries
   (role 2, param_count 0). */
void main(void);
void _crt_entry(void) { main(); }
EOF

write_tu f-kr <<'EOF'
/* form 2: a K&R definition with a REAL parameter list -- must be recorded
   (bit2=1, param_count 1), never as a prototyped zero-parameter record. */
int f(x) int x; { return x; }
EOF

write_tu f-void-caller <<'EOF'
/* form 2 counterpart: the prototyped declaration that must NOT silently
   match the K&R definition (the bad.elf regression). */
int f(void);
int g(void) { return f(); }
EOF

write_tu f-proto <<'EOF'
/* form 3: prototyped zero-parameter definition (role 1, count 0). */
void p(void) { }
EOF

write_tu f-proto-caller <<'EOF'
/* form 3 counterpart. */
void p(void);
int g2(void) { p(); return 0; }
EOF

write_tu h-proto <<'EOF'
/* form 4: prototyped one-parameter definition (role 1, count 1). */
int h(int a) { return a + 1; }
EOF

write_tu h-proto-caller <<'EOF'
/* form 4 counterpart. */
int h(int);
int g4(void) { return h(2); }
EOF

write_tu knr0-def <<'EOF'
/* form 5: K&R zero-parameter definition (role 5, count 0). */
void k() { }
EOF

write_tu knr0-proto-caller <<'EOF'
/* form 5 counterpart: prototyped declaration; bit2 differs but both sides
   have param_count 0, so the reader tolerance applies. */
void k(void);
int g5(void) { k(); return 0; }
EOF

write_tu f-kr-decl-caller <<'EOF'
/* form 6 control: K&R declaration (role 6, count 0); both sides bit2=1 so
   only (ret, call_abi) compare.  This TU is also record A of the
   three-object permutation matrix below. */
int f();
int g3(void) { return f(1); }
EOF

write_tu tri-proto-def <<'EOF'
/* ctrl-1 third record: prototyped zero-parameter DEFINITION of _f
   (role 1, count 0).  A + C + this object is all-pairs compatible. */
int f(void) { return 0; }
EOF

write_tu knr0-kr-decl <<'EOF'
/* ctrl-2 middle record: old-style K&R declaration of _k (role 6, count 0).
   K&R def (5,0) + this (6,0) + the prototyped decl (2,0): every pair
   compatible. */
void k();
int g7(void) { k(); return 0; }
EOF

for s in main-kr crt-standin f-kr f-void-caller f-proto f-proto-caller \
         h-proto h-proto-caller knr0-def knr0-proto-caller f-kr-decl-caller \
         tri-proto-def knr0-kr-decl; do
  compile_tu "$s"
done
echo "compile: 13 TUs (clang + llc, v2 contract) OK"

# --- pin the IR metadata: role/ret/param operands per shape --------------
# node layout: !{!"<symbol>", i32 role, i32 ret, i32 param0, ...}
# The match is the COMPLETE node (anchored at the closing brace) and must
# hit exactly once: a prefix-only pattern would keep matching after the
# record grows operands (the round-2 review hole -- a zero-param record
# mutated to count=1 still carried the old prefix).
pin_match() { # <stem> <symbol> <expected operand tail> -> status
  local stem=$1 sym=$2 want=$3
  [ "$(grep -Ec "^![0-9]+ = !\{!\"${sym}\", ${want}\}$" "$stem.ll" || true)" = 1 ]
}
pin() { # <stem> <symbol> <expected operand tail>
  local stem=$1 sym=$2 want=$3
  pin_match "$stem" "$sym" "$want" || {
    echo "FAIL: $stem.ll must carry exactly one complete node !{$sym, $want}" >&2
    exit 1
  }
}
pin main-kr          _main   'i32 5, i32 0'
pin crt-standin      _main   'i32 2, i32 0'
pin f-kr             _f      'i32 5, i32 0, i32 0'
pin f-void-caller    _f      'i32 2, i32 0'
pin f-proto          _p      'i32 1, i32 0'
pin f-proto-caller   _p      'i32 2, i32 0'
pin h-proto          _h      'i32 1, i32 0, i32 0'
pin h-proto-caller   _h      'i32 2, i32 0, i32 0'
pin knr0-def         _k      'i32 5, i32 0'
pin knr0-proto-caller _k     'i32 2, i32 0'
pin f-kr-decl-caller _f      'i32 6, i32 0'
pin tri-proto-def    _f      'i32 1, i32 0'
pin knr0-kr-decl     _k      'i32 6, i32 0'
echo "records: all 13 IR metadata shapes pinned (complete nodes, exact operand lists)"

link() { # <expect: OK|REJECT> <label> <objs...>
  local expect=$1 label=$2; shift 2
  rm -f out.elf
  if "$LLD" "$@" --area-start=CSEG=0xff0200 -o out.elf 2>err.txt; then
    got=OK
  else
    got=REJECT
  fi
  if [ "$got" != "$expect" ]; then
    echo "FAIL: $label linked=$got, expected $expect" >&2
    sed -n 1,4p err.txt >&2
    exit 1
  fi
  if [ "$expect" = REJECT ]; then
    grep -q "disagree on prototype-ness" err.txt || {
      echo "FAIL: $label rejected with the wrong diagnostic:" >&2
      sed -n 1,4p err.txt >&2
      exit 1; }
    [ ! -e out.elf ] || { echo "FAIL: $label rejected but wrote out.elf" >&2; exit 1; }
  fi
  echo "link: $label -> $got"
}

# --- the frozen matrix ----------------------------------------------------
link OK     "form 1 demo-76: K&R void main() def vs prototyped CRT decl" \
  main-kr.o crt-standin.o
link REJECT "form 2 bad.elf regression: K&R f(x) def vs int f(void) decl" \
  f-kr.o f-void-caller.o
link OK     "form 3: prototyped zero-param def vs decl" \
  f-proto.o f-proto-caller.o
link OK     "form 4: prototyped one-param def vs decl" \
  h-proto.o h-proto-caller.o
link OK     "form 5: K&R zero-param def vs prototyped decl" \
  knr0-def.o knr0-proto-caller.o
link OK     "form 6 control: K&R def vs K&R decl (both bit2=1)" \
  f-kr.o f-kr-decl-caller.o

# --- three-object permutation matrix (review round 3, P1): A = K&R decl
# --- (6,0), B = K&R def (5,1), C = prototyped decl (2,0).  A-B and A-C are
# --- compatible but B-C conflicts, and A does not exempt the B-C pair: B
# --- and C are simultaneously in the link in every order, so all six
# --- permutations must be rejected (the pre-fix linker only compared each
# --- record against the FIRST record and let A/B/C, A/C/B through).
link REJECT "triple perm 1 A/B/C: K&R decl, K&R def, proto decl" \
  f-kr-decl-caller.o f-kr.o f-void-caller.o
link REJECT "triple perm 2 A/C/B: K&R decl, proto decl, K&R def" \
  f-kr-decl-caller.o f-void-caller.o f-kr.o
link REJECT "triple perm 3 B/A/C: K&R def, K&R decl, proto decl" \
  f-kr.o f-kr-decl-caller.o f-void-caller.o
link REJECT "triple perm 4 B/C/A: K&R def, proto decl, K&R decl" \
  f-kr.o f-void-caller.o f-kr-decl-caller.o
link REJECT "triple perm 5 C/A/B: proto decl, K&R decl, K&R def" \
  f-void-caller.o f-kr-decl-caller.o f-kr.o
link REJECT "triple perm 6 C/B/A: proto decl, K&R def, K&R decl" \
  f-void-caller.o f-kr.o f-kr-decl-caller.o

# --- all-pairs-compatible controls: the pairwise sweep must not reject
# --- triples whose every pair is compatible -------------------------------
link OK     "ctrl-1 A+C+D: decl(6,0)+decl(2,0)+def(1,0), all pairs exempt/compatible" \
  f-kr-decl-caller.o f-void-caller.o tri-proto-def.o
link OK     "ctrl-2 def(5,0)+decl(6,0)+decl(2,0) of _k, all pairs compatible" \
  knr0-def.o knr0-kr-decl.o knr0-proto-caller.o

# --- mutation negative (review round 2): growing the zero-param `_main`
# --- record to count=1 must (a) break the complete-node IR pin and
# --- (b) flip the link verdict against the CRT declaration to REJECT,
# --- which is exactly the defense the pin summarizes ----------------------
cp main-kr.ll main-kr-mut.ll
sed -i 's/!"_main", i32 5, i32 0}/!"_main", i32 5, i32 0, i32 0}/' main-kr-mut.ll
if pin_match main-kr-mut _main 'i32 5, i32 0'; then
  echo "FAIL: mutated count=1 record still matches the zero-param pin" >&2
  exit 1
fi
echo "mutation: count=1 record no longer matches the zero-param pin (as required)"
"$LLC" -mtriple=mcs251 -mcs251-memory-contract="$CONTRACT" \
  -mcs251-object-format=elf -filetype=obj main-kr-mut.ll -o main-kr-mut.o
link REJECT "mutation e2e: zero-param _main record grown to count=1 vs CRT decl" \
  main-kr-mut.o crt-standin.o

echo "PASS: zero-parameter protocol writer->reader e2e (15 link verdicts, 13 pinned records, 1 pin mutation)"
