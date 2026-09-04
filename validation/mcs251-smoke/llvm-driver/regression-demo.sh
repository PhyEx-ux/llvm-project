#!/bin/bash
# One-shot: prove batch-check.sh exits non-zero on an unexpected llc failure.
# Creates a scratch broken test, runs batch-check, records the exit code.
T=/mnt/c/Prj/LLVM/MCS251/llvm/test/CodeGen/MCS251/zz-scratch-regression-demo.ll
printf '; fake regression probe: i64 store is not supported\n' > "$T"
printf 'define void @boomboom() {\n  store i64 1, ptr null\n  ret void\n}\n' >> "$T"
bash /mnt/c/Prj/LLVM/MCS251/validation/mcs251-smoke/llvm-driver/batch-check.sh \
  > /tmp/batch-demo.log 2>&1
rc=$?
rm -f "$T"
echo "batch-check exit code: $rc"
grep -E "REGRESSION|regressions=" /tmp/batch-demo.log
exit $rc
