; ISR campaign T06: `.mcs251.isr` object-record emission (A3.2/A3.4/A3.5),
; the exact A2.2 keepalive exemption in the v1 object gate (T06 step 8), and
; the final machine boundary (RETI-only ISR exits, no target pseudos).
;
; Structure note (for review): the frozen card RUN template targets %s, but
; this file must also carry the rework-round-1 coverage and the step-9
; negative modules, which cannot share one LLVM module with the positive one.
; Following the T01 mcs251-isr-invalid.ll precedent, the file is a split-file
; container; the positive section's RUN lines below are the frozen template
; with only %s -> %t/object.ll and the 2026-09-09 ruling's unified form
; (explicit -mcs251-memory-contract=1,2,32,8,1, addrspace(4) ISR definitions,
; and the standard addrspacecast keepalive root).
;
; Rework R1: a minimal ISR reserves no REG_BANK_0/DSEG/XINIT storage (the
; appending-linkage keepalive root is excluded from the storage scan via the
; exact structural verification, never by name).
; Rework R2: the keepalive exemption is limited to modules that actually
; define interrupt entries. An ordinary module's llvm.used keeps the
; original rejections (V2 gate hard-reject; V1 global-data emission
; hard-reject), while legal mixed members inside ISR modules keep working.
; Boundary coverage: ISR->RETI-only, ordinary->no-RETI, unexpanded target
; pseudo hard errors, and a generic pseudo (KILL) that must not trip the
; check. The MIR sections carry a ;MIRHEADER placeholder instead of the
; YAML "--- |" document marker because split-file treats "^--- <name>" as a
; part separator; the RUN lines restore the marker with sed before llc.

; RUN: split-file %s %t
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/object.ll -o %t.o
; RUN: llvm-readobj --file-headers --sections --symbols --relocations %t.o | FileCheck %s
; RUN: llvm-objcopy --dump-section=.mcs251.isr=%t.meta %t.o
; RUN: %python -c "import pathlib,struct,sys; b=pathlib.Path(sys.argv[1]).read_bytes(); assert len(b)==48; a=[struct.unpack('>HHBBBBHHIII',b[i:i+24]) for i in (0,24)]; assert a==[(1,24,1,1,1,1,1,1,0,0,0),(1,24,2,1,1,1,1,1,0,0,0)],a" %t.meta
; RUN: llvm-readobj --symbols %t.o | FileCheck %s --check-prefix=SYM
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj %t/object.ll -o %t.rel 2>&1 | FileCheck %s --check-prefix=REL

; Rework R1: the minimal ISR object must not reserve REG_BANK_0 storage and
; must not carry DSEG/XINIT data sections (the keepalive root is metadata).
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/minimal.ll -o %t/min.o
; RUN: llvm-readobj --sections %t/min.o | FileCheck %s --check-prefix=MINIMAL --implicit-check-not=.mcs251.REG_BANK_0 --implicit-check-not=.mcs251.dseg --implicit-check-not=.mcs251.xinit

; Rework R2: ordinary llvm.used modules keep their original behavior. The V2
; standard AS4-cast root over ordinary functions is hard-rejected by the v1
; object gate; the V1 AS0 root is hard-rejected by the global-data emission
; path. Both are crashes on the pre-T06 toolchain (probe: exit -6).
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/ordinary-used-v2.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=ESCAPE
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -filetype=obj -mcs251-object-format=elf %t/ordinary-used-v1.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=V1USED

; Rework R2: legal mixed members inside ISR modules keep working -- an ISR
; member next to an ordinary AS4 function member, the AS4-direct root form,
; and multi-space no-op cast chains.
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/mixed-functions.ll -o %t/mixed.o
; RUN: llvm-readobj --sections %t/mixed.o | FileCheck %s --check-prefix=MIXED
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/direct.ll -o /dev/null
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/two-hop.ll -o /dev/null
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/three-hop.ll -o /dev/null

; Shared-constant dual path: the same cast constant inside the verified
; keepalive root and inside an ordinary escaped global must still be
; rejected, independent of the global ordering in the module.
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf %t/as4-escape.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=ESCAPE
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf %t/shared-dual.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=ESCAPE
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf %t/shared-aggregate.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=ESCAPE
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf %t/shared-aggregate-reverse.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=ESCAPE
; With an ISR definition, sharing the entire keepalive aggregate also escapes
; the ISR itself, so the structural verifier must reject both global orders.
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf %t/shared-isr-aggregate.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=NOTUSED
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf %t/shared-isr-aggregate-reverse.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=NOTUSED
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf %t/mixed-member.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=ESCAPE
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf %t/compiler-used.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=NOTUSED
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf %t/bad-root.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=NOTUSED

; An ordinary module requires no ISR metadata at all (A3.3).
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf %t/ordinary.ll -o %t/ord.o
; RUN: llvm-readobj --sections %t/ord.o | FileCheck %s --check-prefix=ORD

; Final machine boundary (T06 card steps 8/9), driven through MIR with the
; emitter as the pipeline start so no expansion pass can fix the function
; before the check (same harness as the review probes).
; RUN: sed -e 's/^;MIRHEADER$/--- |/' %t/isr-eret.mir > %t/isr-eret.gen.mir
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf -start-before=mcs251-asm-printer %t/isr-eret.gen.mir -o /dev/null 2>&1 | FileCheck %s --check-prefix=ISRERET
; RUN: sed -e 's/^;MIRHEADER$/--- |/' %t/ordinary-reti.mir > %t/ordinary-reti.gen.mir
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf -start-before=mcs251-asm-printer %t/ordinary-reti.gen.mir -o /dev/null 2>&1 | FileCheck %s --check-prefix=ORDRETI
; RUN: sed -e 's/^;MIRHEADER$/--- |/' %t/target-pseudo.mir > %t/target-pseudo.gen.mir
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf -start-before=mcs251-asm-printer %t/target-pseudo.gen.mir -o /dev/null 2>&1 | FileCheck %s --check-prefix=TPSEUDO
; RUN: sed -e 's/^;MIRHEADER$/--- |/' %t/generic-pseudo.mir > %t/generic-pseudo.gen.mir
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf -start-before=mcs251-asm-printer %t/generic-pseudo.gen.mir -o %t/gp.o
; RUN: llvm-objcopy --dump-section=.text=%t/gp.bin %t/gp.o
; RUN: %python -c "import pathlib,sys; b=pathlib.Path(sys.argv[1]).read_bytes(); assert b.hex() == 'c0d0ca0bca1bca2bca3bca4bca5bca6bca7bcaebdaebda7bda6bda5bda4bda3bda2bda1bda0bd0d032', b.hex()" %t/gp.bin

; W3b (PM ruling 2026-09-13 #2): the object RUN uses the v2 contract, so the
; ISR module publishes the v2 identity (0x102 + .mcs251.attributes, no v1
; note) instead of the W3 downgrade to v1; the .mcs251.isr records and
; relocations are untouched.
; CHECK: Flags [ (0x102)
; CHECK-NOT: .note.mcs251.abi
; CHECK: Name: .mcs251.isr
; CHECK: Type: SHT_PROGBITS
; CHECK: AddressAlignment: 4
; The v2 identity carrier closes the section table (emitEndOfAsmFile).
; CHECK: Name: .mcs251.attributes
; CHECK: Type: Unknown (0x70000003)
; CHECK: R_MCS251_ISR_REF _irq 0x0
; CHECK: R_MCS251_ISR_REF _irq 0x0
; REL: MCS251 ISR requires ELF object output

; The type-9 relocations must reference the named internal function symbol,
; never a folded STT_SECTION reference.
; SYM: Name: _irq
; SYM: Type: Function (0x2)

; ESCAPE: outside the registered A4 v2 object identity
; V1USED: defined global data requires
; NOTUSED: non-registration use
; ISRERET: must return with RETI
; ORDRETI: RETI is only valid inside an interrupt service routine
; TPSEUDO: unexpanded target pseudo instruction 'ADJCALLSTACKDOWN'
; MINIMAL: Name: .mcs251.isr
; MIXED: Name: .mcs251.isr
; MIXED: Size: 48
; ORD-NOT: .mcs251.isr

;--- object.ll
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="1" }

;--- minimal.ll
; Rework R1 probe: an ISR module whose only global is the verified keepalive
; root must produce exactly the code, ABI note and .mcs251.isr sections --
; no REG_BANK_0 reservation, no DSEG/XINIT data.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="1" }

;--- ordinary-used-v2.ll
; Rework R2 probe (V2): a standard AS4-cast llvm.used root over ordinary
; functions, with no ISR definition in the module. The keepalive exemption
; does not apply; the original v1-object-gate rejection must fire.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @plain to ptr)], section "llvm.metadata"
define void @plain() addrspace(4) { ret void }

;--- ordinary-used-v1.ll
; Rework R2 probe (V1): an ordinary AS0 llvm.used root under the v1 layout.
; The keepalive routing does not apply; the original global-data-emission
; rejection must fire.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr @plain], section "llvm.metadata"
define void @plain() { ret void }

;--- as4-escape.ll
; An ordinary AS4 code pointer escaping through an ordinary global is not a
; keepalive item; the v1 object gate still rejects it.
target triple = "mcs251-unknown-none"
@leak = global ptr addrspacecast (ptr addrspace(4) @codefn to ptr)
define void @codefn() {
  ret void
}

;--- shared-dual.ll
; The @codefn cast constant is shared between the verified keepalive root and
; an ordinary escaped global. The step-8 exemption is per member path; the
; second path must still fail.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [2 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr), ptr addrspacecast (ptr addrspace(4) @codefn to ptr)], section "llvm.metadata"
@leak = global ptr addrspacecast (ptr addrspace(4) @codefn to ptr)
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
define void @codefn() {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="1" }

;--- shared-aggregate.ll
; Shared aggregate constant feeding the keepalive root and an ordinary
; escaped global (used container first). Not a keepalive item; rejected.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @plain to ptr)], section "llvm.metadata"
@leak = global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @plain to ptr)]
define void @plain() addrspace(4) { ret void }

;--- shared-aggregate-reverse.ll
; Same dual path with the reversed global ordering; the verdict must not
; depend on the order.
target triple = "mcs251-unknown-none"
@leak = global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @plain to ptr)]
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @plain to ptr)], section "llvm.metadata"
define void @plain() addrspace(4) { ret void }

;--- shared-isr-aggregate.ll
; The identical aggregate reaches both the verified root and ordinary storage.
; Unlike the ordinary baseline above, this module has an ISR definition.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [2 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr), ptr addrspacecast (ptr addrspace(4) @plain to ptr)], section "llvm.metadata"
@leak = global [2 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr), ptr addrspacecast (ptr addrspace(4) @plain to ptr)]
define internal mcs251_intrcc void @irq() addrspace(4) #0 { ret void }
define void @plain() addrspace(4) { ret void }
attributes #0 = { noinline "mcs251-isr-vector"="1" }

;--- shared-isr-aggregate-reverse.ll
; Reverse the terminals of the same shared constant use graph.
target triple = "mcs251-unknown-none"
@leak = global [2 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr), ptr addrspacecast (ptr addrspace(4) @plain to ptr)]
@llvm.used = appending global [2 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr), ptr addrspacecast (ptr addrspace(4) @plain to ptr)], section "llvm.metadata"
define internal mcs251_intrcc void @irq() addrspace(4) #0 { ret void }
define void @plain() addrspace(4) { ret void }
attributes #0 = { noinline "mcs251-isr-vector"="1" }

;--- compiler-used.ll
; llvm.compiler.used is not a registration root (A2.2 rule 5).
target triple = "mcs251-unknown-none"
@llvm.compiler.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="1" }

;--- bad-root.ll
; A malformed llvm.used container (missing the "llvm.metadata" section) is
; not a registration root.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)]
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="1" }

;--- mixed-member.ll
; One verified ISR member plus one ordinary AS4 data member: the container is
; never exempted as a whole table.
target triple = "mcs251-unknown-none"
@as4data = addrspace(4) global i8 0
@llvm.used = appending global [2 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr), ptr addrspacecast (ptr addrspace(4) @as4data to ptr)], section "llvm.metadata"
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="1" }

;--- mixed-functions.ll
; Legal mixed members: an ISR member next to an ordinary AS4 function member
; stays exempted inside an ISR module (one ISR -> exactly 48 bytes of
; records).
target triple = "mcs251-unknown-none"
@llvm.used = appending global [2 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr), ptr addrspacecast (ptr addrspace(4) @plain to ptr)], section "llvm.metadata"
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="1" }
define void @plain() addrspace(4) { ret void }

;--- direct.ll
; The A2.2 "AS4 direct" root form (no container conversion) is a verified
; keepalive shape.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr addrspace(4)] [ptr addrspace(4) @irq], section "llvm.metadata"
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="1" }

;--- two-hop.ll
; A no-op pointer-cast chain through an intermediate address space stays a
; verified keepalive member shape.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(1) addrspacecast (ptr addrspace(4) @irq to ptr addrspace(1)) to ptr)], section "llvm.metadata"
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="1" }

;--- three-hop.ll
; Same as two-hop.ll with one more intermediate address space.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(2) addrspacecast (ptr addrspace(1) addrspacecast (ptr addrspace(4) @irq to ptr addrspace(1)) to ptr addrspace(2)) to ptr)], section "llvm.metadata"
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="1" }

;--- ordinary.ll
; An ordinary module without any keepalive root.
target triple = "mcs251-unknown-none"
define void @plain() {
  ret void
}

;--- isr-eret.mir
;MIRHEADER
  target triple = "mcs251-unknown-none"
  @llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
  define mcs251_intrcc void @irq() addrspace(4) #0 { ret void }
  attributes #0 = { noinline "mcs251-isr-vector"="1" }
...
---
name: irq
tracksRegLiveness: false
body: |
  bb.0:
    ISR_PUSH_PSW implicit-def $dr60, implicit $psw, implicit $dr60
    ISR_PUSH_DR0 implicit-def $dr60, implicit $dr0, implicit $dr60
    ISR_PUSH_DR4 implicit-def $dr60, implicit $dr4, implicit $dr60
    ISR_PUSH_DR8 implicit-def $dr60, implicit $dr8, implicit $a, implicit $b, implicit $dr60
    ISR_PUSH_DR12 implicit-def $dr60, implicit $dr12, implicit $dr60
    ISR_PUSH_DR16 implicit-def $dr60, implicit $dr16, implicit $dr60
    ISR_PUSH_DR20 implicit-def $dr60, implicit $dr20, implicit $dr60
    ISR_PUSH_DR24 implicit-def $dr60, implicit $dr24, implicit $dr60
    ISR_PUSH_DR28 implicit-def $dr60, implicit $dr28, implicit $dr60
    ISR_PUSH_DPX implicit-def $dr60, implicit $dr56, implicit $dpl, implicit $dph, implicit $dptr, implicit $dpxl, implicit $dr60
    ISR_POP_DPX implicit-def $dr56, implicit-def $dpl, implicit-def $dph, implicit-def $dptr, implicit-def $dpxl, implicit-def $dr60, implicit $dr60
    ISR_POP_DR28 implicit-def $dr28, implicit-def $dr60, implicit $dr60
    ISR_POP_DR24 implicit-def $dr24, implicit-def $dr60, implicit $dr60
    ISR_POP_DR20 implicit-def $dr20, implicit-def $dr60, implicit $dr60
    ISR_POP_DR16 implicit-def $dr16, implicit-def $dr60, implicit $dr60
    ISR_POP_DR12 implicit-def $dr12, implicit-def $dr60, implicit $dr60
    ISR_POP_DR8 implicit-def $dr8, implicit-def $a, implicit-def $b, implicit-def $dr60, implicit $dr60
    ISR_POP_DR4 implicit-def $dr4, implicit-def $dr60, implicit $dr60
    ISR_POP_DR0 implicit-def $dr0, implicit-def $dr60, implicit $dr60
    ISR_POP_PSW implicit-def $psw, implicit-def $dr60, implicit $dr60
    ERET
...

;--- ordinary-reti.mir
;MIRHEADER
  target triple = "mcs251-unknown-none"
  define void @irq() addrspace(4) { ret void }
...
---
name: irq
tracksRegLiveness: false
body: |
  bb.0:
    ISR_PUSH_PSW implicit-def $dr60, implicit $psw, implicit $dr60
    ISR_PUSH_DR0 implicit-def $dr60, implicit $dr0, implicit $dr60
    ISR_PUSH_DR4 implicit-def $dr60, implicit $dr4, implicit $dr60
    ISR_PUSH_DR8 implicit-def $dr60, implicit $dr8, implicit $a, implicit $b, implicit $dr60
    ISR_PUSH_DR12 implicit-def $dr60, implicit $dr12, implicit $dr60
    ISR_PUSH_DR16 implicit-def $dr60, implicit $dr16, implicit $dr60
    ISR_PUSH_DR20 implicit-def $dr60, implicit $dr20, implicit $dr60
    ISR_PUSH_DR24 implicit-def $dr60, implicit $dr24, implicit $dr60
    ISR_PUSH_DR28 implicit-def $dr60, implicit $dr28, implicit $dr60
    ISR_PUSH_DPX implicit-def $dr60, implicit $dr56, implicit $dpl, implicit $dph, implicit $dptr, implicit $dpxl, implicit $dr60
    ISR_POP_DPX implicit-def $dr56, implicit-def $dpl, implicit-def $dph, implicit-def $dptr, implicit-def $dpxl, implicit-def $dr60, implicit $dr60
    ISR_POP_DR28 implicit-def $dr28, implicit-def $dr60, implicit $dr60
    ISR_POP_DR24 implicit-def $dr24, implicit-def $dr60, implicit $dr60
    ISR_POP_DR20 implicit-def $dr20, implicit-def $dr60, implicit $dr60
    ISR_POP_DR16 implicit-def $dr16, implicit-def $dr60, implicit $dr60
    ISR_POP_DR12 implicit-def $dr12, implicit-def $dr60, implicit $dr60
    ISR_POP_DR8 implicit-def $dr8, implicit-def $a, implicit-def $b, implicit-def $dr60, implicit $dr60
    ISR_POP_DR4 implicit-def $dr4, implicit-def $dr60, implicit $dr60
    ISR_POP_DR0 implicit-def $dr0, implicit-def $dr60, implicit $dr60
    ISR_POP_PSW implicit-def $psw, implicit-def $dr60, implicit $dr60
    RETI implicit-def $dr60, implicit-def $psw, implicit $dr60
...

;--- target-pseudo.mir
;MIRHEADER
  target triple = "mcs251-unknown-none"
  @llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
  define mcs251_intrcc void @irq() addrspace(4) #0 { ret void }
  attributes #0 = { noinline "mcs251-isr-vector"="1" }
...
---
name: irq
tracksRegLiveness: false
body: |
  bb.0:
    ISR_PUSH_PSW implicit-def $dr60, implicit $psw, implicit $dr60
    ISR_PUSH_DR0 implicit-def $dr60, implicit $dr0, implicit $dr60
    ISR_PUSH_DR4 implicit-def $dr60, implicit $dr4, implicit $dr60
    ISR_PUSH_DR8 implicit-def $dr60, implicit $dr8, implicit $a, implicit $b, implicit $dr60
    ISR_PUSH_DR12 implicit-def $dr60, implicit $dr12, implicit $dr60
    ISR_PUSH_DR16 implicit-def $dr60, implicit $dr16, implicit $dr60
    ISR_PUSH_DR20 implicit-def $dr60, implicit $dr20, implicit $dr60
    ISR_PUSH_DR24 implicit-def $dr60, implicit $dr24, implicit $dr60
    ISR_PUSH_DR28 implicit-def $dr60, implicit $dr28, implicit $dr60
    ISR_PUSH_DPX implicit-def $dr60, implicit $dr56, implicit $dpl, implicit $dph, implicit $dptr, implicit $dpxl, implicit $dr60
    ISR_POP_DPX implicit-def $dr56, implicit-def $dpl, implicit-def $dph, implicit-def $dptr, implicit-def $dpxl, implicit-def $dr60, implicit $dr60
    ISR_POP_DR28 implicit-def $dr28, implicit-def $dr60, implicit $dr60
    ISR_POP_DR24 implicit-def $dr24, implicit-def $dr60, implicit $dr60
    ISR_POP_DR20 implicit-def $dr20, implicit-def $dr60, implicit $dr60
    ISR_POP_DR16 implicit-def $dr16, implicit-def $dr60, implicit $dr60
    ISR_POP_DR12 implicit-def $dr12, implicit-def $dr60, implicit $dr60
    ISR_POP_DR8 implicit-def $dr8, implicit-def $a, implicit-def $b, implicit-def $dr60, implicit $dr60
    ISR_POP_DR4 implicit-def $dr4, implicit-def $dr60, implicit $dr60
    ISR_POP_DR0 implicit-def $dr0, implicit-def $dr60, implicit $dr60
    ISR_POP_PSW implicit-def $psw, implicit-def $dr60, implicit $dr60
    ADJCALLSTACKDOWN 0, 0
    ADJCALLSTACKUP 0, 0
    RETI implicit-def $dr60, implicit-def $psw, implicit $dr60
...

;--- generic-pseudo.mir
;MIRHEADER
  target triple = "mcs251-unknown-none"
  @llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
  define mcs251_intrcc void @irq() addrspace(4) #0 { ret void }
  attributes #0 = { noinline "mcs251-isr-vector"="1" }
...
---
name: irq
tracksRegLiveness: false
body: |
  bb.0:
    ISR_PUSH_PSW implicit-def $dr60, implicit $psw, implicit $dr60
    ISR_PUSH_DR0 implicit-def $dr60, implicit $dr0, implicit $dr60
    ISR_PUSH_DR4 implicit-def $dr60, implicit $dr4, implicit $dr60
    ISR_PUSH_DR8 implicit-def $dr60, implicit $dr8, implicit $a, implicit $b, implicit $dr60
    ISR_PUSH_DR12 implicit-def $dr60, implicit $dr12, implicit $dr60
    ISR_PUSH_DR16 implicit-def $dr60, implicit $dr16, implicit $dr60
    ISR_PUSH_DR20 implicit-def $dr60, implicit $dr20, implicit $dr60
    ISR_PUSH_DR24 implicit-def $dr60, implicit $dr24, implicit $dr60
    ISR_PUSH_DR28 implicit-def $dr60, implicit $dr28, implicit $dr60
    ISR_PUSH_DPX implicit-def $dr60, implicit $dr56, implicit $dpl, implicit $dph, implicit $dptr, implicit $dpxl, implicit $dr60
    ISR_POP_DPX implicit-def $dr56, implicit-def $dpl, implicit-def $dph, implicit-def $dptr, implicit-def $dpxl, implicit-def $dr60, implicit $dr60
    ISR_POP_DR28 implicit-def $dr28, implicit-def $dr60, implicit $dr60
    ISR_POP_DR24 implicit-def $dr24, implicit-def $dr60, implicit $dr60
    ISR_POP_DR20 implicit-def $dr20, implicit-def $dr60, implicit $dr60
    ISR_POP_DR16 implicit-def $dr16, implicit-def $dr60, implicit $dr60
    ISR_POP_DR12 implicit-def $dr12, implicit-def $dr60, implicit $dr60
    ISR_POP_DR8 implicit-def $dr8, implicit-def $a, implicit-def $b, implicit-def $dr60, implicit $dr60
    ISR_POP_DR4 implicit-def $dr4, implicit-def $dr60, implicit $dr60
    ISR_POP_DR0 implicit-def $dr0, implicit-def $dr60, implicit $dr60
    ISR_POP_PSW implicit-def $psw, implicit-def $dr60, implicit $dr60
    KILL implicit $r0
    RETI implicit-def $dr60, implicit-def $psw, implicit $dr60
...
