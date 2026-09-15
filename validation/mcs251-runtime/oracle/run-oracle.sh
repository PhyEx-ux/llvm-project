#!/usr/bin/env bash
# run-oracle.sh -- MCS251 G7 S1'' bit-exact verification driver (controlled asset)
#
# Builds the independent integer-rational oracle against the current runtime
# sources and requires ZERO mismatches for add/sub/mul/div.  Also runs a
# negative control: the same oracle against a sabotaged copy of the mul unit
# with the underflow guard-merge removed; that run MUST fail, proving the
# harness is sensitive to the class of defect Alice found.
#
# Usage:
#   ./run-oracle.sh [random_pairs] [seed]
#   ./run-oracle.sh --negative-control
#
# Exit: 0 = all required runs passed; 1 = failure.

set -u
set -o pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/../src
CC=${CC:-cc}
N=${1:-1000000}
if [ "${1:-}" = "--negative-control" ]; then N=200000; fi
SEED=${2:-0xC0FFEE}

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

RUNTIME_SRCS=(
  "$SRC/mcs251_float_mul.c"
  "$SRC/mcs251_float_addsub.c"
  "$SRC/mcs251_float_div.c"
  "$SRC/mcs251_bitutil.c"
)

step() { printf '== %s ==\n' "$*"; }
fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }

step "build oracle (independent integer-rational) against current runtime"
"$CC" -O2 -std=c11 -Wall -Wextra -Werror -I"$SRC" \
  "$HERE/mcs251_float_oracle.c" "${RUNTIME_SRCS[@]}" -o "$TMP/float_oracle" \
  || fail "oracle build"

step "bit-exact run: random_pairs=$N seed=$SEED"
"$TMP/float_oracle" "$N" "$SEED" | tee "$TMP/oracle.out"
grep -q 'PASS (zero-diff)' "$TMP/oracle.out" \
  || fail "runtime vs independent oracle: mismatches found"
grep -q 'cross mismatches=0' "$TMP/oracle.out" \
  || fail "oracle self cross-check vs host IEEE-754 failed"

step "negative control: sabotage underflow guard-merge in mul, expect FAIL"
SAB=$TMP/mul_sabotaged.c
sed 's/sst = (uint16_t)(sst | st | g);/sst = (uint16_t)(sst | st);/' \
  "$SRC/mcs251_float_mul.c" > "$SAB"
if ! diff -q "$SRC/mcs251_float_mul.c" "$SAB" >/dev/null; then :; else
  fail "negative control could not be constructed (guard-merge line not found)"
fi
"$CC" -O2 -std=c11 -I"$SRC" \
  "$HERE/mcs251_float_oracle.c" "$SAB" \
  "$SRC/mcs251_float_addsub.c" "$SRC/mcs251_float_div.c" "$SRC/mcs251_bitutil.c" \
  -o "$TMP/oracle_sabotaged" || fail "sabotaged oracle build"
if "$TMP/oracle_sabotaged" "$N" "$SEED" >"$TMP/sab.out" 2>&1; then
  cat "$TMP/sab.out"
  fail "negative control PASSED but must FAIL (harness is not sensitive)"
fi
printf 'negative control correctly FAILED (%s)\n' \
  "$(grep -c '^  DIFF' "$TMP/sab.out" 2>/dev/null || echo '>0') diffs shown"
grep -m1 'mul checks' "$TMP/sab.out" || true

step "all required runs passed"
