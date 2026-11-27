// ELF is now the default object format and emits DWARF .debug_* sections:
// `-g`, `-g0 -g`, `-gline-tables-only`, and `-gmodules` succeed by default.
// Legacy ASxxxx REL (`-mllvm -mcs251-object-format=rel`) and plain ASxxxx
// assembly (`-S`) still have no debug carrier and fail closed.
//
// RUN: %clang --target=mcs251-unknown-none -c -g %s -o %t.elf.o && test -s %t.elf.o
// RUN: not %clang --target=mcs251-unknown-none -mllvm -mcs251-object-format=rel -fsyntax-only -g %s 2>&1 | FileCheck %s --check-prefix=GDEFAULT --implicit-check-not='PLEASE submit' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH'
// RUN: not %clang --target=mcs251-unknown-none -mllvm -mcs251-object-format=rel -fsyntax-only -g0 -g %s 2>&1 | FileCheck %s --check-prefix=G0G --implicit-check-not='PLEASE submit' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH'
// RUN: not %clang --target=mcs251-unknown-none -mllvm -mcs251-object-format=rel -fsyntax-only -gline-tables-only %s 2>&1 | FileCheck %s --check-prefix=GLINES --implicit-check-not='PLEASE submit' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH'
// RUN: not %clang --target=mcs251-unknown-none -S -g %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=GDEFAULT --implicit-check-not='PLEASE submit' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH'
//
// The accepted cases are run without `not`, so a regression that turns them
// into failures breaks the test.

// RUN: %clang --target=mcs251-unknown-none -mllvm -mcs251-object-format=rel -fsyntax-only -g0 %s
// RUN: %clang --target=mcs251-unknown-none -mllvm -mcs251-object-format=rel -fsyntax-only -g -g0 %s
// RUN: %clang --target=mcs251-unknown-none -mllvm -mcs251-object-format=rel -fsyntax-only %s
// Internal location users are NOT user debug requests: remarks and
// -fstack-usage must keep compiling in both ELF and REL modes.
// RUN: %clang --target=mcs251-unknown-none -mllvm -mcs251-object-format=rel -c -o %t.remarks.o -O1 -Rpass=inline %s && test -s %t.remarks.o
// RUN: %clang --target=mcs251-unknown-none -mllvm -mcs251-object-format=rel -c -o %t.stackusage.o -fstack-usage %s && test -s %t.stackusage.o
// RUN: %clang --target=mcs251-unknown-none -mllvm -mcs251-object-format=rel -c -o %t.g0.o -g0 %s && test -s %t.g0.o
// RUN: %clang --target=mcs251-unknown-none -mllvm -mcs251-object-format=rel -c -o %t.g0g.o -g -g0 %s && test -s %t.g0g.o
// RUN: %clang --target=mcs251-unknown-none -mllvm -mcs251-object-format=rel -c -o %t.glineg0.o -gline-tables-only -g0 %s && test -s %t.glineg0.o
// RUN: not %clang --target=mcs251-unknown-none -mllvm -mcs251-object-format=rel -c -o %t.gmodules.o -gmodules %s 2>&1 | FileCheck %s --check-prefix=GMOD --implicit-check-not='PLEASE submit' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH'
// RUN: not %clang --target=mcs251-unknown-none -mllvm -mcs251-object-format=rel -c -o %t.gmodulesg0.o -gmodules -g0 %s 2>&1 | FileCheck %s --check-prefix=GMOD --implicit-check-not='PLEASE submit' --implicit-check-not='Stack dump' --implicit-check-not='PLEASE ATTACH'

int main(void) { return 0; }

// GDEFAULT: error: debug information is not supported for target 'mcs251'
// GDEFAULT-NOT: -g0
// GDEFAULT: build with -g0
// G0G: error: debug information is not supported for target 'mcs251'
// GLINES: error: debug information is not supported for target 'mcs251'
// GMOD: error: debug information is not supported for target 'mcs251'
