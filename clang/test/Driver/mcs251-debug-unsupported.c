// RUN: not %clang --target=mcs251-unknown-none -fsyntax-only -g %s 2>&1 | FileCheck %s --check-prefix=GDEFAULT --implicit-check-not='PLEASE submit' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH'
// RUN: not %clang --target=mcs251-unknown-none -fsyntax-only -g0 -g %s 2>&1 | FileCheck %s --check-prefix=G0G --implicit-check-not='PLEASE submit' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH'
// RUN: not %clang --target=mcs251-unknown-none -fsyntax-only -gline-tables-only %s 2>&1 | FileCheck %s --check-prefix=GLINES --implicit-check-not='PLEASE submit' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH'
//
// WP4 C1: an EFFECTIVE debug-information request is a hard error for MCS251.
// The target emits no source-level debug info (the object writers have zero
// debug sections), so `clang -g` used to return success while silently
// delivering an object without debug info -- exactly the wrong success
// signal. The check runs after the driver has reduced every -g* option to
// the final DebugInfoKind, so `-g -g0` (final: none) keeps compiling while
// `-g0 -g` (final: full) fails.
//
// The accepted cases are run without `not`, so a regression that turns them
// into failures breaks the test.

// RUN: %clang --target=mcs251-unknown-none -fsyntax-only -g0 %s
// RUN: %clang --target=mcs251-unknown-none -fsyntax-only -g -g0 %s
// RUN: %clang --target=mcs251-unknown-none -fsyntax-only %s
// Internal location users are NOT user debug requests: remarks and
// -fstack-usage must keep compiling (their source locations are tracked
// internally, no debug sections are produced). These run through the FULL
// CodeGen path (-c to an object), not -fsyntax-only: the distinction only
// exists once the backend decides what to emit, so a -fsyntax-only run would
// not exercise the rule at all. Each artifact is checked non-empty, so a
// silently-skipped backend cannot pass.
// RUN: %clang --target=mcs251-unknown-none -c -o %t.remarks.o -O1 -Rpass=inline %s && test -s %t.remarks.o
// RUN: %clang --target=mcs251-unknown-none -c -o %t.stackusage.o -fstack-usage %s && test -s %t.stackusage.o
// RUN: %clang --target=mcs251-unknown-none -c -o %t.g0.o -g0 %s && test -s %t.g0.o
// RUN: %clang --target=mcs251-unknown-none -c -o %t.g0g.o -g -g0 %s && test -s %t.g0g.o
//
// The rule follows the FINAL EFFECTIVE debug kind, not the presence of a -g
// spelling anywhere on the command line. Two families of case pin that:
//
//  * a later `-g0` clears an earlier debug request (-g0, -g -g0,
//    -gdwarf-5 -g0, -gline-tables-only -g0) -> accepted;
//  * `-gmodules` is NOT a no-op: the driver re-enables constructor debug for
//    it AFTER the -g0 reduction (ToolChains/Clang.cpp: `Args.hasFlag(...OPT_
//    gmodules...)` sets DebugInfoKind = DebugInfoConstructor unless the last
//    option is -gline-tables-only/-gline-directives-only), so `-gmodules -g0`
//    still requests debug information and is rejected. Reading the command
//    line for "-g0 is present" would get this wrong.
// RUN: %clang --target=mcs251-unknown-none -c -o %t.glineg0.o -gline-tables-only -g0 %s && test -s %t.glineg0.o
// RUN: not %clang --target=mcs251-unknown-none -c -o %t.gmodules.o -gmodules %s 2>&1 | FileCheck %s --check-prefix=GMOD --implicit-check-not='PLEASE submit' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH'
// RUN: not %clang --target=mcs251-unknown-none -c -o %t.gmodulesg0.o -gmodules -g0 %s 2>&1 | FileCheck %s --check-prefix=GMOD --implicit-check-not='PLEASE submit' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH'

int main(void) { return 0; }

// GDEFAULT: error: debug information is not supported for target 'mcs251'
// GDEFAULT-NOT: -g0
// GDEFAULT: build with -g0
// G0G: error: debug information is not supported for target 'mcs251'
// GLINES: error: debug information is not supported for target 'mcs251'
// GMOD: error: debug information is not supported for target 'mcs251'
