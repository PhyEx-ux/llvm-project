// RUN: split-file %s %t
// RUN: bash %t/check.sh %clang %t
// RUN: FileCheck %s --check-prefix=WEAK < %t/weak.O0.err
// RUN: FileCheck %s --check-prefix=WEAK < %t/weak.Oz.err
// RUN: FileCheck %s --check-prefix=I64 --implicit-check-not='PLEASE submit' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH' --implicit-check-not='crash backtrace' < %t/i64.O0.err
// RUN: FileCheck %s --check-prefix=I64 --implicit-check-not='PLEASE submit' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH' --implicit-check-not='crash backtrace' < %t/i64.O2.err
// RUN: FileCheck %s --check-prefix=BITINT --implicit-check-not='PLEASE submit' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH' --implicit-check-not='crash backtrace' < %t/bitint.O0.err
// RUN: FileCheck %s --check-prefix=BITINT --implicit-check-not='PLEASE submit' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH' --implicit-check-not='crash backtrace' < %t/bitint.O2.err
// RUN: FileCheck %s --check-prefix=INTERNAL < %t/internal.err
//
// WP4: the clang C path must fail with EXACTLY status 1, a complete and clean
// stderr (no bug-report request, no stack dump, no crash-reproducer block,
// no "frontend command failed" crash summary) and no leftover output file --
// for the source-structure rejections (A8 weak definition), backend arithmetic
// rejections (B1 runtime i64), and a backend usage-fatal regression (_BitInt(2)
// parameter). The latter still crosses the in-process CrashRecoveryContext via
// Process::Exit, but must not be treated as a crash. A plain `not` only asserts
// "nonzero", so
// the assertions live in check.sh, which returns 0 only if every case has
// exact status 1 (or 0 for the positive control), no artifact, and no crash
// text in the captured stderr.

// WEAK-COUNT-1: error:
// WEAK: weak function definitions are not supported on MCS251
// WEAK-NOT: PLEASE submit
// WEAK-NOT: Stack dump
// WEAK-NOT: PLEASE ATTACH
// I64-COUNT-1: error:
// I64: MCS251 contract violation: i64 integer arithmetic is not yet implemented
// I64-NOT: PLEASE submit
// I64-NOT: Stack dump
// I64-NOT: PLEASE ATTACH
// BITINT-COUNT-1: fatal error: error in backend: MCS251: arguments must be unsplit i8/i16/i32 scalars
// BITINT-NOT: PLEASE submit
// BITINT-NOT: Stack dump
// BITINT-NOT: PLEASE ATTACH
// INTERNAL: fatal error: error in backend: #pragma clang __debug llvm_fatal_error
// INTERNAL: PLEASE submit a bug report
// INTERNAL: Stack dump:

//--- check.sh
#!/usr/bin/env bash
set -u
CLANG="$1"
T="$2"
rc=0

crash_free() { # $1 = stderr file
  ! grep -Eq 'PLEASE submit|Stack dump|PLEASE ATTACH|crash backtrace|frontend command failed' "$1"
}

expect_reject() { # $1 = label, $2 = src, $3 = clang args..., log = $T/$1.err
  local label="$1" src="$2"; shift 2
  local log="$T/$label.err"
  local obj="$T/$label.o"
  rm -f "$obj"
  "$CLANG" --target=mcs251-unknown-none "$@" -c "$src" -o "$obj" 2>"$log"
  local code=$?
  if [ "$code" -ne 1 ]; then
    echo "FAIL $label: expected status 1, got $code"
    rc=1
  fi
  if [ -e "$obj" ]; then
    echo "FAIL $label: output artifact left behind"
    rc=1
  fi
  if ! crash_free "$log"; then
    echo "FAIL $label: crash diagnostic text present"
    rc=1
  fi
}

for opt in O0 O1 O2 Os Oz; do
  expect_reject "weak.$opt" "$T/weak.c" "-$opt"
done
for opt in O0 O2; do
  expect_reject "i64.$opt" "$T/i64.c" "-$opt"
  expect_reject "bitint.$opt" "$T/bitint.c" "-$opt" "-ffreestanding" \
    "-mllvm" "-mcs251-object-format=elf"
done

# An internal fatal error must retain the crash diagnostics. This also guards
# the distinction from reportFatalUsageError in the in-process driver path.
rm -f "$T/internal.o"
"$CLANG" --target=mcs251-unknown-none -O0 -c "$T/internal.c" \
  -o "$T/internal.o" 2>"$T/internal.err"
code=$?
if [ "$code" -ne 1 ]; then
  echo "FAIL internal: expected driver status 1, got $code"
  rc=1
fi
if [ -e "$T/internal.o" ]; then
  echo "FAIL internal: output artifact left behind"
  rc=1
fi
if ! grep -q 'exit code 70' "$T/internal.err" || \
   ! grep -q 'PLEASE submit a bug report' "$T/internal.err" || \
   ! grep -q 'Stack dump:' "$T/internal.err"; then
  echo "FAIL internal: expected crash diagnostic was not present"
  rc=1
fi

# Positive control: a supported module compiles (the target's clang object
# output is ASxxxx REL text) and the artifact is produced.
"$CLANG" --target=mcs251-unknown-none -O0 -c "$T/ok.c" -o "$T/ok.o" 2>"$T/ok.err"
code=$?
if [ "$code" -ne 0 ] || [ ! -s "$T/ok.o" ]; then
  echo "FAIL ok: positive control did not produce an object (status $code)"
  rc=1
fi

exit $rc

//--- weak.c
__attribute__((weak)) int wf(void) { return 1; }

//--- i64.c
volatile unsigned long long a = 2, b = 3;
volatile unsigned long long sink;
void f(void) { sink = a * b; }

//--- bitint.c
int f(_BitInt(2) x) { return x; }

//--- internal.c
#pragma clang __debug llvm_fatal_error
int f(void) { return 0; }

//--- ok.c
extern volatile unsigned char port;
void g(void) { port = 1; }
