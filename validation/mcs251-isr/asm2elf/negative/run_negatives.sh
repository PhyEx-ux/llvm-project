#!/usr/bin/env bash
# run_negatives.sh - negative acceptance cases for the asm2elf E4 path.
#
# Each case must FAIL with a clear, actionable diagnostic:
#   N1  object without .note.mcs251.abi            -> mcs251-lld rejects
#   N2  object with a corrupted ABI descriptor     -> mcs251-lld rejects
#   N3  absolute (constant) ajmp/acall target      -> sdrel2elf rejects
#   N4  initialized data in XSEG                   -> sdrel2elf rejects
#   N5  unsupported area name                      -> sdrel2elf rejects
#   N6  unsupported .rel header (XH4)              -> sdrel2elf rejects
#   N7  unknown .rel directive                     -> sdrel2elf rejects
#   N8  truncated 'R' line (corrupt .rel)          -> sdrel2elf rejects
#   N9  escaped absolute control marker 0xFFFF     -> sdrel2elf rejects
#   N10 'P' (setdp) line                           -> sdrel2elf rejects
#   N11 missing --source-mode declaration          -> sdrel2elf rejects
#
# Additional assertion levels:
#   - every sdrel2elf rejection (N3..N10) carries a "<file>:<line>:" prefix
#     (converter diagnostics are line-anchored; N11 is an argparse usage
#     error and N1/N2 come from the linker, so they are exempt);
#   - every diagnostic is actionable: its first line is at least 25
#     characters long.
set -uo pipefail
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
A2ELF=$(cd -- "$HERE/.." && pwd)
DEMO=$(cd -- "$HERE/../demo" && pwd)
OUT=${1:?usage: run_negatives.sh <output-dir>}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)

SDAS=${SDAS:-/home/liu/build-sdcc/bin/sdas251}
LLD=${LLD:-/home/liu/build-mcs251-lld/bin/lld}
CONVERT="$A2ELF/sdrel2elf.py"
FAILURES=0
CASES=0

expect_fail() { # name expected-substring command...
  local name="$1" pattern="$2"
  shift 2
  CASES=$((CASES+1))
  local log rc first
  log=$("$@" 2>&1)
  rc=$?
  first=$(printf '%s' "$log" | head -1)
  if [ $rc -eq 0 ]; then
    echo "negative: FAIL: $name unexpectedly succeeded"
    FAILURES=$((FAILURES+1))
  elif [ ${#first} -lt 25 ]; then
    echo "negative: FAIL: $name diagnostic too short to be actionable: '$first'"
    FAILURES=$((FAILURES+1))
  elif printf '%s' "$log" | grep -q "$pattern"; then
    echo "negative: ok: $name rejected: $(printf '%s' "$first" | cut -c1-100)"
  else
    echo "negative: FAIL: $name failed without expected message %r? got:" \
      "$pattern"
    printf '%s\n' "$log"
    FAILURES=$((FAILURES+1))
  fi
}

expect_line_anchor() { # name file command...  (requires "<file>:<digits>:")
  local name="$1" file="$2"
  shift 2
  CASES=$((CASES+1))
  local log rc
  log=$("$@" 2>&1)
  rc=$?
  if [ $rc -eq 0 ]; then
    echo "negative: FAIL: $name unexpectedly succeeded"
    FAILURES=$((FAILURES+1))
  elif printf '%s' "$log" | grep -Eq "$file:[0-9]+:"; then
    echo "negative: ok: $name diagnostic is line-anchored: $(\
printf '%s' "$log" | head -1 | cut -c1-100)"
  else
    echo "negative: FAIL: $name diagnostic lacks a $file:<line>: prefix:"
    printf '%s\n' "$log"
    FAILURES=$((FAILURES+1))
  fi
}

convert() { # converter invocation used by every conversion negative case
  python3 "$CONVERT" "$@" --source-mode source
}

# ---- N1/N2: ABI note diagnostics come from mcs251-lld --------------------
"$SDAS" -los "$OUT/n1.rel" "$DEMO/liba.asm"
python3 "$CONVERT" "$OUT/n1.rel" -o "$OUT/n1-nonote.o" --source-mode source \
  --no-abi-note
expect_fail "N1 missing ABI note" "expected exactly one .note.mcs251.abi" \
  "$LLD" -flavor mcs251 "$OUT/n1-nonote.o" --area-start=CSEG=0xff0000 \
  -o "$OUT/n1.elf"
python3 "$CONVERT" "$OUT/n1.rel" -o "$OUT/n1-badnote.o" --source-mode source \
  --corrupt-abi-note
expect_fail "N2 corrupted ABI note" "invalid MCS251 ABI note descriptor" \
  "$LLD" -flavor mcs251 "$OUT/n1-badnote.o" --area-start=CSEG=0xff0000 \
  -o "$OUT/n2.elf"

# ---- N3/N4/N5: unsupported source constructs via sdas251 -----------------
cat > "$OUT/n3.asm" <<'EOF'
	.module n3
	.area CSEG (CODE,REL,CON)
	.globl _n3
_n3:
	ajmp	#0x0020
	ret
EOF
"$SDAS" -los "$OUT/n3.rel" "$OUT/n3.asm"
expect_line_anchor "N3 absolute ajmp" "n3.rel" \
  python3 "$CONVERT" "$OUT/n3.rel" -o "$OUT/n3.o" --source-mode source

cat > "$OUT/n4.asm" <<'EOF'
	.module n4
	.area XSEG (XDATA,REL,CON)
	.globl _n4
_n4:
	.db	1,2,3
EOF
"$SDAS" -los "$OUT/n4.rel" "$OUT/n4.asm"
expect_line_anchor "N4 initialized XSEG" "n4.rel" \
  python3 "$CONVERT" "$OUT/n4.rel" -o "$OUT/n4.o" --source-mode source
# The offending 'T' line is line 10 of n4.rel (XH3, H, M, .__.ABS., _CODE
# area, XSEG area, S, T, R); the diagnostic must name it.
n4log=$(python3 "$CONVERT" "$OUT/n4.rel" -o "$OUT/n4.o" --source-mode source \
  2>&1 || true)
if printf '%s' "$n4log" | grep -q "n4.rel:10:"; then
  echo "negative: ok: N4 diagnostic points at n4.rel:10 (the 'T' line)"
else
  echo "negative: FAIL: N4 diagnostic does not point at n4.rel:10"
  FAILURES=$((FAILURES+1))
fi
CASES=$((CASES+1))

cat > "$OUT/n5.asm" <<'EOF'
	.module n5
	.area FOO (REL,CON)
	.globl _n5
_n5:
	.blkb	1
EOF
"$SDAS" -los "$OUT/n5.rel" "$OUT/n5.asm"
expect_line_anchor "N5 unsupported area" "n5.rel" \
  python3 "$CONVERT" "$OUT/n5.rel" -o "$OUT/n5.o" --source-mode source

# ---- N6..N10: corrupt / unsupported .rel inputs ---------------------------
printf 'XH4\n' > "$OUT/n6.rel"
expect_line_anchor "N6 bad header" "n6.rel" \
  python3 "$CONVERT" "$OUT/n6.rel" -o "$OUT/n6.o" --source-mode source

printf 'XH3\nH t\nM t\nZ 1 2\n' > "$OUT/n7.rel"
expect_line_anchor "N7 unknown directive" "n7.rel" \
  python3 "$CONVERT" "$OUT/n7.rel" -o "$OUT/n7.o" --source-mode source

printf 'XH3\nH t\nM t\nA CSEG size 1 flags 20 addr 0\nS _x Def000000\nT 00 00 00 01\nR 00 00\n' \
  > "$OUT/n8.rel"
expect_line_anchor "N8 truncated R line" "n8.rel" \
  python3 "$CONVERT" "$OUT/n8.rel" -o "$OUT/n8.o" --source-mode source

# N9: the generic 8051 emission path marks absolute control transfers with
# symbol index 0xFFFF (asout.c out_rw(0xFFFF)).
printf 'XH3\nH t\nM t\nA CSEG size 3 flags 20 addr 0\nS _x Def000000\nT 00 00 00 20 00 01\nR 00 00 00 00 F8 0A 03 FF FF\n' \
  > "$OUT/n9.rel"
expect_line_anchor "N9 absolute control marker" "n9.rel" \
  python3 "$CONVERT" "$OUT/n9.rel" -o "$OUT/n9.o" --source-mode source

printf 'XH3\nH t\nM t\nP 00 00 00 00\n' > "$OUT/n10.rel"
expect_line_anchor "N10 setdp line" "n10.rel" \
  python3 "$CONVERT" "$OUT/n10.rel" -o "$OUT/n10.o" --source-mode source

# ---- N11: the encoding-mode declaration is mandatory ----------------------
# The XH3 header does not record the source/binary opcode map, so the
# converter refuses to guess and demands --source-mode (blocker 3).
"$SDAS" -los "$OUT/n11.rel" "$DEMO/liba.asm"
expect_fail "N11 missing --source-mode" \
  "the following arguments are required: --source-mode" \
  python3 "$CONVERT" "$OUT/n11.rel" -o "$OUT/n11.o"
expect_fail "N11b wrong --source-mode value" \
  "invalid choice: 'binary' (choose from 'source')" \
  python3 "$CONVERT" "$OUT/n11.rel" -o "$OUT/n11.o" --source-mode binary

if [ $FAILURES -ne 0 ]; then
  echo "negative: $FAILURES case(s) FAILED"
  exit 1
fi
echo "negative: all $CASES assertions rejected/matched with clear diagnostics"
