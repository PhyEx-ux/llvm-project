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
//
// G1-3a review B2: the per-slot registration also holds at the top of the
// 0-126 profile. A second PCH (pch-high.h) carries slot 126 (the profile
// maximum), slot 45 (completed in the consumer) and slot 31 (reclassified
// Legal); all three survive the round-trip with their canonical slot
// texts, and duplicate/redeclaration rules behave exactly as at slots 1/2.
// Illegal slots (127 out of profile, 13 Reserved) are rejected on BOTH
// sides of the PCH boundary -- the writer refuses to put them into the
// PCH at all.

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

// G1-3a review B2: the PCH carries the full 0-126 profile, so high-slot
// identities round-trip exactly like the slot 1/2 cases above. The second
// PCH defines slot 126 (the profile maximum) and slot 31 (reclassified
// Legal) and declares slot 45 for consumer completion; probed 2026-09-14,
// the consumer IR keeps all three with their canonical slot texts and the
// llvm.used membership.
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -mcs251-memory-contract=1,1,32,8,1 -Os -x c-header %t/pch-high.h -emit-pch -o %t/pch-high.pch
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -mcs251-memory-contract=1,1,32,8,1 -Os -include-pch %t/pch-high.pch -emit-llvm %t/consume-high.c -o - | FileCheck %s --check-prefix=HIGH

// A consumer definition at a high slot owned by the PCH is the same
// duplicate registration as the slot 1 DUP case: one diagnostic.
// RUN: not %clang_cc1 -triple mcs251-unknown-none -std=c11 -mcs251-memory-contract=1,1,32,8,1 -Os -include-pch %t/pch-high.pch -fsyntax-only %t/dup-high.c 2>&1 | FileCheck %s --check-prefix=DUP

// Re-declaring the PCH-defined high-slot ISR itself keeps the canonical
// identity, like redecl.c for slot 1.
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -mcs251-memory-contract=1,1,32,8,1 -Os -include-pch %t/pch-high.pch -fsyntax-only %t/redecl-high.c

// The PCH never launders an illegal slot: the writer itself rejects slot
// 127 (out of profile) and slot 13 (the Reserved transfer slot) at
// -emit-pch time, and with a valid PCH loaded a consumer declaration at
// either slot is rejected the same way (probed: identical single error).
// RUN: not %clang_cc1 -triple mcs251-unknown-none -std=c11 -mcs251-memory-contract=1,1,32,8,1 -Os -x c-header %t/bad-pch-127.h -emit-pch -o %t/bad127.pch 2>&1 | FileCheck %s --check-prefix=BADSLOT
// RUN: not %clang_cc1 -triple mcs251-unknown-none -std=c11 -mcs251-memory-contract=1,1,32,8,1 -Os -x c-header %t/bad-pch-13.h -emit-pch -o %t/bad13.pch 2>&1 | FileCheck %s --check-prefix=BADSLOT
// RUN: not %clang_cc1 -triple mcs251-unknown-none -std=c11 -mcs251-memory-contract=1,1,32,8,1 -Os -include-pch %t/pch.pch -fsyntax-only %t/bad-127.c 2>&1 | FileCheck %s --check-prefix=BADSLOT
// RUN: not %clang_cc1 -triple mcs251-unknown-none -std=c11 -mcs251-memory-contract=1,1,32,8,1 -Os -include-pch %t/pch.pch -fsyntax-only %t/bad-13.c 2>&1 | FileCheck %s --check-prefix=BADSLOT

// CONSUME: @llvm.used = appending global [2 x ptr] [ptr @decl, ptr @first]
// CONSUME: define internal mcs251_intrcc void @first()
// CONSUME: define dso_local mcs251_intrcc void @decl()

// The deserialized identities keep their slot texts (126 defined in the
// PCH, 45 completed in the consumer, 31 defined in the PCH).
// HIGH: @llvm.used = appending global [3 x ptr] [ptr @hi126, ptr @lo31, ptr @mid45], section "llvm.metadata"
// HIGH: define internal mcs251_intrcc void @hi126() #0 {
// HIGH: define internal mcs251_intrcc void @lo31() #1 {
// HIGH: define dso_local mcs251_intrcc void @mid45() #2 {
// HIGH: attributes #0 = { {{.*}}"mcs251-isr-vector"="126"
// HIGH: attributes #1 = { {{.*}}"mcs251-isr-vector"="31"
// HIGH: attributes #2 = { {{.*}}"mcs251-isr-vector"="45"

// DUP-COUNT-1: duplicate MCS251 interrupt vector
// DUP-NOT: duplicate MCS251 interrupt vector

// MODULE-DUP: duplicate MCS251 interrupt vector

// BADSLOT: MCS251 interrupt vector must be a legal slot in 0-126

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

//--- pch-high.h
static void hi126(void) __attribute__((interrupt(126)));
static void hi126(void) {}
void mid45(void) __attribute__((interrupt(45)));
static void lo31(void) __attribute__((interrupt(31)));
static void lo31(void) {}

//--- consume-high.c
void mid45(void) {}

//--- dup-high.c
void clash(void) __attribute__((interrupt(126)));
void clash(void) {}

//--- redecl-high.c
void hi126(void);

//--- bad-pch-127.h
void bad(void) __attribute__((interrupt(127)));

//--- bad-pch-13.h
void bad(void) __attribute__((interrupt(13)));

//--- bad-127.c
void bad(void) __attribute__((interrupt(127)));

//--- bad-13.c
void bad(void) __attribute__((interrupt(13)));
