#!/bin/bash
# Batch check: every MCS251 .ll test through llc.
#   * llc failures are acceptable ONLY for the known diagnostic tests
#     (names containing "-error", e.g. loadstore-error-i32-load.ll and
#     global-data-error.ll); anything else is a regression and exits
#     non-zero.
#   * every successful output must be a clean ASxxxx module (no banned
#     ELF directives, .area CSEG (CODE) present).
#   * the acceptance-form spec grep runs over the whole products dir.
# LOCAL SCRATCH SCRIPT: hardcoded to this machine's WSL layout.
PRODUCTS=/tmp/mcs251-batch-products
rm -rf "$PRODUCTS"; mkdir -p "$PRODUCTS"
cd ~/build-mcs251 || exit 1
ok=0; banned=0; expected_fail=0; regressions=0
for f in /mnt/c/Prj/LLVM/MCS251/llvm/test/CodeGen/MCS251/*.ll; do
  base=$(basename "$f")
  if ! ./bin/llc -mtriple=mcs251 -o "$PRODUCTS/$base.s" "$f" 2>"$PRODUCTS/$base.err"; then
    case "$base" in
      *-error*.ll)
        expected_fail=$((expected_fail+1))
        ;;
      *)
        echo "REGRESSION: llc failed on $base:"
        tail -3 "$PRODUCTS/$base.err" | sed 's/^/    /'
        regressions=$((regressions+1))
        ;;
    esac
    continue
  fi
  if grep -Eq "^[[:space:]]*\.(text|section|p2align|align|type|size|ident|space|long|short|quad|zero|fill|file|weak|end)([[:space:]]|$)" "$PRODUCTS/$base.s"; then
    echo "BANNED directives in $base:"
    grep -En "^[[:space:]]*\.(text|section|p2align|align|type|size|ident|space|long|short|quad|zero|fill|file|weak|end)([[:space:]]|$)" "$PRODUCTS/$base.s" | sed 's/^/    /'
    banned=$((banned+1))
    continue
  fi
  if ! grep -q "^[[:space:]]*\.area CSEG (CODE)[[:space:]]*$" "$PRODUCTS/$base.s"; then
    echo "MISSING CSEG area in $base"
    banned=$((banned+1))
    continue
  fi
  ok=$((ok+1))
done
echo "clean_modules=$ok banned_or_missing=$banned expected_failures=$expected_fail regressions=$regressions"

# Acceptance spec grep, verbatim form, over all products:
if grep -E "^\.text|^\.section|^\.p2align|^\.type|^\.size|^\.ident" "$PRODUCTS"/*.s; then
  echo "SPEC-GREP FOUND (BAD)"
  exit 1
fi
echo "spec-grep empty over $PRODUCTS"

[ "$banned" -eq 0 ] && [ "$regressions" -eq 0 ] || exit 1
exit 0
