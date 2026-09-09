// R5 regressions: ISR definitions deserialized from a PCH participate in the
// same per-slot registration as definitions completed in the consumer TU.
// The registry is instance state of the Sema (R5-C): registration happens
// only when a real definition completes, keyed by canonical declaration, so
// pure declarations never register and repeated body/AST visits of the same
// canonical function are idempotent.
//
// Large-TU behavior (R5-A): each seeding walk deduplicates by canonical
// declaration BEFORE consulting getDefinition(), so a redeclaration chain
// of any length is walked once per scan and getDefinition() runs at most
// once per chain. The first registration seeds the table with a single
// O(N) walk over the TU-level declarations; after that, each completed
// definition registers in O(1) expected.
//
// Module imports (R5-B): ASTReader re-sets the TU's external lexical
// storage on TU_UPDATE_LEXICAL; the registry detects that in O(1) on the
// next registration and re-seeds once, so late-imported definitions are
// registered too.

// RUN: rm -rf %t
// RUN: mkdir -p %t
// RUN: split-file %s %t
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -mcs251-memory-contract=1,1,32,8,1 -Os -x c-header %t/pch.h -emit-pch -o %t/pch.pch

// Consumer defines decl (slot 2, only declared in the PCH): accepted, and
// the PCH-resident definition first (slot 1) is kept alive.
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -mcs251-memory-contract=1,1,32,8,1 -Os -include-pch %t/pch.pch -emit-llvm %t/consume.c -o - | FileCheck %s --check-prefix=CONSUME

// A consumer definition at a slot already owned by a PCH definition is a
// duplicate registration: rejected in Sema with exactly one diagnostic.
// RUN: not %clang_cc1 -triple mcs251-unknown-none -std=c11 -mcs251-memory-contract=1,1,32,8,1 -Os -include-pch %t/pch.pch -fsyntax-only %t/dup.c 2>&1 | FileCheck %s --check-prefix=DUP

// A consumer pure declaration at a PCH-owned slot is not a registration and
// is accepted.
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -mcs251-memory-contract=1,1,32,8,1 -Os -include-pch %t/pch.pch -fsyntax-only %t/pure.c

// Re-declaring the PCH-defined ISR itself (same canonical entity) is also
// accepted; the registry keys on the canonical declaration.
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -mcs251-memory-contract=1,1,32,8,1 -Os -include-pch %t/pch.pch -fsyntax-only %t/redecl.c

// R5-B module counterexample: the module is imported only after the
// bootstrap happened; TU_UPDATE_LEXICAL re-sets the TU's external lexical
// storage, the registry re-seeds on the next registration, and a consumer
// definition at the module-owned slot is rejected.
// RUN: echo 'module Late { header "late.h" }' > %t/module.modulemap
// RUN: echo 'void late(void) __attribute__((interrupt(2)));' > %t/late.h
// RUN: echo 'void late(void) {}' >> %t/late.h
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -mcs251-memory-contract=1,1,32,8,1 -fmodules -emit-module -fmodule-name=Late %t/module.modulemap -o %t/Late.pcm
// RUN: echo 'void first(void) __attribute__((interrupt(1)));' > %t/modmain.c
// RUN: echo 'void first(void) {}' >> %t/modmain.c
// RUN: echo '#pragma'" clang"' module import Late' >> %t/modmain.c
// RUN: echo 'void second(void) __attribute__((interrupt(2)));' >> %t/modmain.c
// RUN: echo 'void second(void) {}' >> %t/modmain.c
// RUN: not %clang_cc1 -triple mcs251-unknown-none -std=c11 -mcs251-memory-contract=1,1,32,8,1 -fmodules -fmodule-file=Late=%t/Late.pcm -fsyntax-only %t/modmain.c 2>&1 | FileCheck %s --check-prefix=MODULE-DUP

// CONSUME: @llvm.used = appending global [2 x ptr] [ptr @decl, ptr @first]
// CONSUME: define internal mcs251_intrcc void @first()
// CONSUME: define dso_local mcs251_intrcc void @decl()

// DUP-COUNT-1: duplicate MCS251 interrupt vector
// DUP-NOT: duplicate MCS251 interrupt vector

// MODULE-DUP: duplicate MCS251 interrupt vector

//--- pch.h
static void first(void) __attribute__((interrupt(1)));
static void first(void) {}
void decl(void) __attribute__((interrupt(2)));

//--- consume.c
void decl(void) {}

//--- dup.c
void second(void) __attribute__((interrupt(1)));
void second(void) {}

//--- pure.c
void third(void) __attribute__((interrupt(1)));

//--- redecl.c
void first(void);
