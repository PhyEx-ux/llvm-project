#!/bin/bash
# Cross-validate llc -filetype=obj against sdas251 over the lit corpus
# (Phase 13a).  For every positive .ll test: llc -filetype=asm | sdas251 is
# the reference .rel; llc -filetype=obj must match it semantically
# (compare-rel.py).  Error tests must fail loudly on BOTH paths.
LLC=~/build-mcs251/bin/llc
AS=/home/liu/build-sdcc/bin/sdas251
CMP="python3 /mnt/c/Prj/LLVM/MCS251/validation/mcs251-smoke/llvm-driver/compare-rel.py"
TDIR=/mnt/c/Prj/LLVM/MCS251/llvm/test/CodeGen/MCS251
pass=0; fail=0; errsame=0
for f in "$TDIR"/*.ll; do
  b=$(basename "$f" .ll)
  [ "$b" = "call-mir" ] && { echo "SKIP   $b (MIR input)"; continue; }
  if grep -q "not llc\|not --crash" "$f"; then
    if "$LLC" -mtriple=mcs251 -filetype=asm -o /dev/null "$f" 2>/dev/null; then
      echo "BAD    $b: error test unexpectedly succeeded (asm)"; fail=$((fail+1)); continue
    fi
    if "$LLC" -mtriple=mcs251 -filetype=obj -o /dev/null "$f" 2>/dev/null; then
      echo "BAD    $b: error test unexpectedly succeeded (obj)"; fail=$((fail+1)); continue
    fi
    errsame=$((errsame+1)); echo "ERR-OK $b (both paths reject)"; continue
  fi
  if ! "$LLC" -mtriple=mcs251-unknown-none -filetype=asm -o "$b.asm" "$f" 2>"$b.err"; then
    echo "BAD    $b: asm path failed"; head -3 "$b.err"; fail=$((fail+1)); continue
  fi
  if ! "$AS" -plosgffw -o "$b-sdas.rel" "$b.asm" >/dev/null 2>&1; then
    echo "BAD    $b: sdas251 failed on llc asm"; fail=$((fail+1)); continue
  fi
  if ! "$LLC" -mtriple=mcs251-unknown-none -filetype=obj -o "$b-llvm.rel" "$f" 2>"$b.obj.err"; then
    echo "BAD    $b: obj path failed"; head -3 "$b.obj.err"; fail=$((fail+1)); continue
  fi
  if $CMP "$b-sdas.rel" "$b-llvm.rel" >/dev/null; then
    pass=$((pass+1)); echo "MATCH  $b"
  else
    echo "DIFF   $b"; $CMP "$b-sdas.rel" "$b-llvm.rel" | head -3; fail=$((fail+1))
  fi
done
echo "== match=$pass fail=$fail err-consistent=$errsame =="
