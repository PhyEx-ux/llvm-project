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
; RUN: %python -c "import pathlib,struct,sys; b=pathlib.Path(sys.argv[1]).read_bytes(); assert len(b)==48; a=[struct.unpack('>HHBBBBHHIII',b[i:i+24]) for i in (0,24)]; assert a==[(2,24,1,1,1,1,1,1,0,0,0),(2,24,2,1,1,1,1,1,0,0,0)],a" %t.meta
; RUN: llvm-readobj --symbols %t.o | FileCheck %s --check-prefix=SYM
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj %t/object.ll -o %t.rel 2>&1 | FileCheck %s --check-prefix=REL

; G1-1: high-slot persistence. Slot 126 (the new upper bound) and slot 31
; (reclassified Legal) pass ContractCheck and the AsmPrinter object boundary
; and persist as four records in definition order (ENTRY+REGISTER each).
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/high.ll -o %t/high.o
; RUN: llvm-objcopy --dump-section=.mcs251.isr=%t/high.meta %t/high.o
; RUN: %python -c "import pathlib,struct,sys; b=pathlib.Path(sys.argv[1]).read_bytes(); assert len(b)==96; a=[struct.unpack('>HHBBBBHHIII',b[i:i+24]) for i in range(0,96,24)]; assert a==[(2,24,1,1,1,1,126,1,0,0,0),(2,24,2,1,1,1,126,1,0,0,0),(2,24,1,1,1,1,31,1,0,0,0),(2,24,2,1,1,1,31,1,0,0,0)],a" %t/high.meta

; G1-3: a six-entry module covering both digit widths and the reclassified
; slots (0, 8, 31, 45, 46, 126), defined in deliberately scrambled order.
; The twelve records persist in DEFINITION order (ENTRY+REGISTER per
; function), not in sorted slot order; the six bodies are byte-identical
; 41-byte units and each ends on its own RETI (final byte 0x32), with the
; function symbols laid out in the same definition order.
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/multi.ll -o %t/multi.o
; RUN: llvm-objcopy --dump-section=.mcs251.isr=%t/multi.meta %t/multi.o
; RUN: %python -c "import pathlib,struct,sys; b=pathlib.Path(sys.argv[1]).read_bytes(); assert len(b)==288; want=[(2,24,k,1,1,1,s,1,0,0,0) for s in (126,0,45,8,46,31) for k in (1,2)]; a=[struct.unpack('>HHBBBBHHIII',b[i:i+24]) for i in range(0,288,24)]; assert a==want,a" %t/multi.meta
; RUN: llvm-objcopy --dump-section=.text=%t/multi.bin %t/multi.o
; RUN: %python -c "import pathlib,sys; b=pathlib.Path(sys.argv[1]).read_bytes(); u=b[:41]; assert len(b)==246 and b==u*6 and u[-1]==0x32 and u.hex()=='c0d0ca0bca1bca2bca3bca4bca5bca6bca7bcaebdaebda7bda6bda5bda4bda3bda2bda1bda0bd0d032',(len(b),u.hex())" %t/multi.bin
; RUN: llvm-readobj --symbols %t/multi.o | FileCheck %s --check-prefix=MULTISYM
; MULTISYM: Name: _f126
; MULTISYM: Name: _f0
; MULTISYM: Name: _f45
; MULTISYM: Name: _f8
; MULTISYM: Name: _f46
; MULTISYM: Name: _f31

; G1-1: each checking layer rejects an out-of-profile slot ON ITS OWN. The
; upstream layers are skipped explicitly (-disable-verify skips the IR
; Verifier; -start-before=mcs251-asm-printer with a MIR input starts past
; ContractCheck), so removing either layer's own slot re-check makes the
; corresponding RUN pass an invalid slot through and fail.
; RUN: not llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/cc-oob.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=CCOOB
; RUN: sed -e 's/^;MIRHEADER$/--- |/' %t/ap-oob.mir > %t/ap-oob.gen.mir
; RUN: not llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf -start-before=mcs251-asm-printer %t/ap-oob.gen.mir -o /dev/null 2>&1 | FileCheck %s --check-prefix=APOOB
;
; G1-3a review B2: the same two harnesses carry the completed slot matrix.
; In-profile Reserved 81 (HeaderOnly), 100 (NoSource) and 13
; (LegacySpecial), System 14, and every non-canonical text form (the empty
; string, the leading zero "0126" -- probed 2026-09-14: refused, not
; silently re-read as 126 --, "+45", "-0", " 45", "45 ", "4x5", "abc" and
; a 40-digit overflow string) are each fed through ContractCheck isolation
; and AsmPrinter isolation. Every form was probed to reach BOTH layer
; re-checks with that layer's own message (the ContractCheck wrapper /
; the bare AsmPrinter text below): none is caught earlier by the .ll or
; .mir attribute parsers, so none of these RUNs depends on the disabled
; Verifier.
; RUN: not llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/cc-oob-res81.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=CCOOB
; RUN: not llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/cc-oob-res100.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=CCOOB
; RUN: not llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/cc-oob-res13.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=CCOOB
; RUN: not llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/cc-oob-sys14.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=CCOOB
; RUN: not llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/cc-oob-empty.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=CCOOB
; RUN: not llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/cc-oob-leadzero.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=CCOOB
; RUN: not llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/cc-oob-plus45.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=CCOOB
; RUN: not llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/cc-oob-minus0.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=CCOOB
; RUN: not llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/cc-oob-leadspace.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=CCOOB
; RUN: not llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/cc-oob-trailspace.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=CCOOB
; RUN: not llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/cc-oob-junk4x5.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=CCOOB
; RUN: not llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/cc-oob-alpha.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=CCOOB
; RUN: not llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/cc-oob-overflow40.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=CCOOB
; RUN: sed -e 's/^;MIRHEADER$/--- |/' %t/ap-oob-res81.mir > %t/ap-oob-res81.gen.mir
; RUN: not llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf -start-before=mcs251-asm-printer %t/ap-oob-res81.gen.mir -o /dev/null 2>&1 | FileCheck %s --check-prefix=APOOB
; RUN: sed -e 's/^;MIRHEADER$/--- |/' %t/ap-oob-res100.mir > %t/ap-oob-res100.gen.mir
; RUN: not llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf -start-before=mcs251-asm-printer %t/ap-oob-res100.gen.mir -o /dev/null 2>&1 | FileCheck %s --check-prefix=APOOB
; RUN: sed -e 's/^;MIRHEADER$/--- |/' %t/ap-oob-res13.mir > %t/ap-oob-res13.gen.mir
; RUN: not llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf -start-before=mcs251-asm-printer %t/ap-oob-res13.gen.mir -o /dev/null 2>&1 | FileCheck %s --check-prefix=APOOB
; RUN: sed -e 's/^;MIRHEADER$/--- |/' %t/ap-oob-sys14.mir > %t/ap-oob-sys14.gen.mir
; RUN: not llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf -start-before=mcs251-asm-printer %t/ap-oob-sys14.gen.mir -o /dev/null 2>&1 | FileCheck %s --check-prefix=APOOB
; RUN: sed -e 's/^;MIRHEADER$/--- |/' %t/ap-oob-empty.mir > %t/ap-oob-empty.gen.mir
; RUN: not llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf -start-before=mcs251-asm-printer %t/ap-oob-empty.gen.mir -o /dev/null 2>&1 | FileCheck %s --check-prefix=APOOB
; RUN: sed -e 's/^;MIRHEADER$/--- |/' %t/ap-oob-leadzero.mir > %t/ap-oob-leadzero.gen.mir
; RUN: not llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf -start-before=mcs251-asm-printer %t/ap-oob-leadzero.gen.mir -o /dev/null 2>&1 | FileCheck %s --check-prefix=APOOB
; RUN: sed -e 's/^;MIRHEADER$/--- |/' %t/ap-oob-plus45.mir > %t/ap-oob-plus45.gen.mir
; RUN: not llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf -start-before=mcs251-asm-printer %t/ap-oob-plus45.gen.mir -o /dev/null 2>&1 | FileCheck %s --check-prefix=APOOB
; RUN: sed -e 's/^;MIRHEADER$/--- |/' %t/ap-oob-minus0.mir > %t/ap-oob-minus0.gen.mir
; RUN: not llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf -start-before=mcs251-asm-printer %t/ap-oob-minus0.gen.mir -o /dev/null 2>&1 | FileCheck %s --check-prefix=APOOB
; RUN: sed -e 's/^;MIRHEADER$/--- |/' %t/ap-oob-leadspace.mir > %t/ap-oob-leadspace.gen.mir
; RUN: not llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf -start-before=mcs251-asm-printer %t/ap-oob-leadspace.gen.mir -o /dev/null 2>&1 | FileCheck %s --check-prefix=APOOB
; RUN: sed -e 's/^;MIRHEADER$/--- |/' %t/ap-oob-trailspace.mir > %t/ap-oob-trailspace.gen.mir
; RUN: not llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf -start-before=mcs251-asm-printer %t/ap-oob-trailspace.gen.mir -o /dev/null 2>&1 | FileCheck %s --check-prefix=APOOB
; RUN: sed -e 's/^;MIRHEADER$/--- |/' %t/ap-oob-junk4x5.mir > %t/ap-oob-junk4x5.gen.mir
; RUN: not llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf -start-before=mcs251-asm-printer %t/ap-oob-junk4x5.gen.mir -o /dev/null 2>&1 | FileCheck %s --check-prefix=APOOB
; RUN: sed -e 's/^;MIRHEADER$/--- |/' %t/ap-oob-alpha.mir > %t/ap-oob-alpha.gen.mir
; RUN: not llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf -start-before=mcs251-asm-printer %t/ap-oob-alpha.gen.mir -o /dev/null 2>&1 | FileCheck %s --check-prefix=APOOB
; RUN: sed -e 's/^;MIRHEADER$/--- |/' %t/ap-oob-overflow40.mir > %t/ap-oob-overflow40.gen.mir
; RUN: not llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf -start-before=mcs251-asm-printer %t/ap-oob-overflow40.gen.mir -o /dev/null 2>&1 | FileCheck %s --check-prefix=APOOB

; Rework R1: the minimal ISR object must not reserve REG_BANK_0 storage and
; must not carry DSEG/XINIT data sections (the keepalive root is metadata).
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/minimal.ll -o %t/min.o
; RUN: llvm-readobj --sections %t/min.o | FileCheck %s --check-prefix=MINIMAL --implicit-check-not=.mcs251.REG_BANK_0 --implicit-check-not=.mcs251.dseg --implicit-check-not=.mcs251.xinit

; Rework R2: ordinary llvm.used modules keep their original behavior. The V2
; standard AS4-cast root over ordinary functions is hard-rejected by the v1
; object gate; the V1 AS0 root is hard-rejected by the global-data emission
; path. Both are crashes on the pre-T06 toolchain (probe: exit -6).
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/ordinary-used-v2.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=ESCAPE
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -filetype=obj -mcs251-object-format=elf %t/ordinary-used-v1.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=V1USED

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
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf %t/as4-escape.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=ESCAPE
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf %t/shared-dual.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=ESCAPE
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf %t/shared-aggregate.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=ESCAPE
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf %t/shared-aggregate-reverse.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=ESCAPE
; With an ISR definition, sharing the entire keepalive aggregate also escapes
; the ISR itself, so the structural verifier must reject both global orders.
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf %t/shared-isr-aggregate.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=NOTUSED
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf %t/shared-isr-aggregate-reverse.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=NOTUSED
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf %t/mixed-member.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=ESCAPE
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf %t/compiler-used.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=NOTUSED
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf %t/bad-root.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=NOTUSED

; An ordinary module requires no ISR metadata at all (A3.3).
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf %t/ordinary.ll -o %t/ord.o
; RUN: llvm-readobj --sections %t/ord.o | FileCheck %s --check-prefix=ORD

; Final machine boundary (T06 card steps 8/9), driven through MIR with the
; emitter as the pipeline start so no expansion pass can fix the function
; before the check (same harness as the review probes).
; RUN: sed -e 's/^;MIRHEADER$/--- |/' %t/isr-eret.mir > %t/isr-eret.gen.mir
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf -start-before=mcs251-asm-printer %t/isr-eret.gen.mir -o /dev/null 2>&1 | FileCheck %s --check-prefix=ISRERET
; RUN: sed -e 's/^;MIRHEADER$/--- |/' %t/ordinary-reti.mir > %t/ordinary-reti.gen.mir
; RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -filetype=obj -mcs251-object-format=elf -start-before=mcs251-asm-printer %t/ordinary-reti.gen.mir -o /dev/null 2>&1 | FileCheck %s --check-prefix=ORDRETI
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
; CCOOB: MCS251 contract violation: MCS251 ISR: vector is not a legal slot in profile 0-126
; APOOB: MCS251 ISR: vector is not a legal slot in profile 0-126
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

!mcs251.signatures = !{}

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

!mcs251.signatures = !{}

;--- ordinary-used-v2.ll
; Rework R2 probe (V2): a standard AS4-cast llvm.used root over ordinary
; functions, with no ISR definition in the module. The keepalive exemption
; does not apply; the original v1-object-gate rejection must fire.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @plain to ptr)], section "llvm.metadata"
define void @plain() addrspace(4) { ret void }


!mcs251.signatures = !{!10000}
!10000 = !{!"_plain", i32 1, i32 0}
;--- ordinary-used-v1.ll
; Rework R2 probe (V1): an ordinary AS0 llvm.used root under the v1 layout.
; The keepalive routing does not apply; the original global-data-emission
; rejection must fire.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr @plain], section "llvm.metadata"
define void @plain() { ret void }


!mcs251.signatures = !{!10000}
!10000 = !{!"_plain", i32 1, i32 0}
;--- as4-escape.ll
; An ordinary AS4 code pointer escaping through an ordinary global is not a
; keepalive item; the v1 object gate still rejects it.
target triple = "mcs251-unknown-none"
@leak = global ptr addrspacecast (ptr addrspace(4) @codefn to ptr)
define void @codefn() {
  ret void
}


!mcs251.signatures = !{!10000}
!10000 = !{!"_codefn", i32 1, i32 0}
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


!mcs251.signatures = !{!10000}
!10000 = !{!"_codefn", i32 1, i32 0}
;--- shared-aggregate.ll
; Shared aggregate constant feeding the keepalive root and an ordinary
; escaped global (used container first). Not a keepalive item; rejected.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @plain to ptr)], section "llvm.metadata"
@leak = global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @plain to ptr)]
define void @plain() addrspace(4) { ret void }


!mcs251.signatures = !{!10000}
!10000 = !{!"_plain", i32 1, i32 0}
;--- shared-aggregate-reverse.ll
; Same dual path with the reversed global ordering; the verdict must not
; depend on the order.
target triple = "mcs251-unknown-none"
@leak = global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @plain to ptr)]
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @plain to ptr)], section "llvm.metadata"
define void @plain() addrspace(4) { ret void }


!mcs251.signatures = !{!10000}
!10000 = !{!"_plain", i32 1, i32 0}
;--- shared-isr-aggregate.ll
; The identical aggregate reaches both the verified root and ordinary storage.
; Unlike the ordinary baseline above, this module has an ISR definition.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [2 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr), ptr addrspacecast (ptr addrspace(4) @plain to ptr)], section "llvm.metadata"
@leak = global [2 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr), ptr addrspacecast (ptr addrspace(4) @plain to ptr)]
define internal mcs251_intrcc void @irq() addrspace(4) #0 { ret void }
define void @plain() addrspace(4) { ret void }
attributes #0 = { noinline "mcs251-isr-vector"="1" }


!mcs251.signatures = !{!10000}
!10000 = !{!"_plain", i32 1, i32 0}
;--- shared-isr-aggregate-reverse.ll
; Reverse the terminals of the same shared constant use graph.
target triple = "mcs251-unknown-none"
@leak = global [2 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr), ptr addrspacecast (ptr addrspace(4) @plain to ptr)]
@llvm.used = appending global [2 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr), ptr addrspacecast (ptr addrspace(4) @plain to ptr)], section "llvm.metadata"
define internal mcs251_intrcc void @irq() addrspace(4) #0 { ret void }
define void @plain() addrspace(4) { ret void }
attributes #0 = { noinline "mcs251-isr-vector"="1" }


!mcs251.signatures = !{!10000}
!10000 = !{!"_plain", i32 1, i32 0}
;--- compiler-used.ll
; llvm.compiler.used is not a registration root (A2.2 rule 5).
target triple = "mcs251-unknown-none"
@llvm.compiler.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="1" }

!mcs251.signatures = !{}

;--- bad-root.ll
; A malformed llvm.used container (missing the "llvm.metadata" section) is
; not a registration root.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)]
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="1" }

!mcs251.signatures = !{}

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

!mcs251.signatures = !{}

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


!mcs251.signatures = !{!10000}
!10000 = !{!"_plain", i32 1, i32 0}
;--- direct.ll
; The A2.2 "AS4 direct" root form (no container conversion) is a verified
; keepalive shape.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr addrspace(4)] [ptr addrspace(4) @irq], section "llvm.metadata"
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="1" }

!mcs251.signatures = !{}

;--- two-hop.ll
; A no-op pointer-cast chain through an intermediate address space stays a
; verified keepalive member shape.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(1) addrspacecast (ptr addrspace(4) @irq to ptr addrspace(1)) to ptr)], section "llvm.metadata"
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="1" }

!mcs251.signatures = !{}

;--- three-hop.ll
; Same as two-hop.ll with one more intermediate address space.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(2) addrspacecast (ptr addrspace(1) addrspacecast (ptr addrspace(4) @irq to ptr addrspace(1)) to ptr addrspace(2)) to ptr)], section "llvm.metadata"
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="1" }

!mcs251.signatures = !{}

;--- ordinary.ll
; An ordinary module without any keepalive root.
target triple = "mcs251-unknown-none"
define void @plain() {
  ret void
}


!mcs251.signatures = !{!10000}
!10000 = !{!"_plain", i32 1, i32 0}
;--- isr-eret.mir
;MIRHEADER
  target triple = "mcs251-unknown-none"
  @llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
  define mcs251_intrcc void @irq() addrspace(4) #0 { ret void }
  attributes #0 = { noinline "mcs251-isr-vector"="1" }

  !mcs251.signatures = !{!10000}
  !10000 = !{!"_irq", i32 1, i32 0}
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

  !mcs251.signatures = !{!10000}
  !10000 = !{!"_irq", i32 1, i32 0}
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

  !mcs251.signatures = !{!10000}
  !10000 = !{!"_irq", i32 1, i32 0}
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

  !mcs251.signatures = !{!10000}
  !10000 = !{!"_irq", i32 1, i32 0}
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

;--- high.ll
target triple = "mcs251-unknown-none"
@llvm.used = appending global [2 x ptr] [ptr addrspacecast (ptr addrspace(4) @dma to ptr), ptr addrspacecast (ptr addrspace(4) @t5 to ptr)], section "llvm.metadata"
define internal mcs251_intrcc void @dma() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="126" }
define internal mcs251_intrcc void @t5() addrspace(4) #1 {
  ret void
}
attributes #1 = { noinline "mcs251-isr-vector"="31" }
!mcs251.signatures = !{}

;--- cc-oob.ll
; ContractCheck-isolated rejection: the IR Verifier is disabled, so the
; module-level contract check is the only layer that sees slot 127.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="127" }
!mcs251.signatures = !{}

;--- ap-oob.mir
;MIRHEADER
  target triple = "mcs251-unknown-none"
  @llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
  define internal mcs251_intrcc void @irq() addrspace(4) #0 {
    ret void
  }
  attributes #0 = { noinline "mcs251-isr-vector"="127" }
  !mcs251.signatures = !{!10000}
  !10000 = !{!"_irq", i32 1, i32 0}
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

;--- cc-oob-res81.ll
; ContractCheck-isolated rejection of in-profile Reserved slot, HeaderOnly
; evidence (header macro, no manual row): the IR Verifier is disabled, so the
; module-level contract check is the only layer that sees the attribute text.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="81" }
!mcs251.signatures = !{}

;--- ap-oob-res81.mir
;MIRHEADER
  target triple = "mcs251-unknown-none"
  @llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
  define internal mcs251_intrcc void @irq() addrspace(4) #0 {
    ret void
  }
  attributes #0 = { noinline "mcs251-isr-vector"="81" }
  !mcs251.signatures = !{!10000}
  !10000 = !{!"_irq", i32 1, i32 0}
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

;--- cc-oob-res100.ll
; ContractCheck-isolated rejection of in-profile Reserved slot, NoSource
; evidence (neither header nor manual row): the IR Verifier is disabled, so the
; module-level contract check is the only layer that sees the attribute text.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="100" }
!mcs251.signatures = !{}

;--- ap-oob-res100.mir
;MIRHEADER
  target triple = "mcs251-unknown-none"
  @llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
  define internal mcs251_intrcc void @irq() addrspace(4) #0 {
    ret void
  }
  attributes #0 = { noinline "mcs251-isr-vector"="100" }
  !mcs251.signatures = !{!10000}
  !10000 = !{!"_irq", i32 1, i32 0}
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

;--- cc-oob-res13.ll
; ContractCheck-isolated rejection of in-profile Reserved slot, LegacySpecial
; evidence (the transfer slot): the IR Verifier is disabled, so the module-level
; contract check is the only layer that sees the attribute text.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="13" }
!mcs251.signatures = !{}

;--- ap-oob-res13.mir
;MIRHEADER
  target triple = "mcs251-unknown-none"
  @llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
  define internal mcs251_intrcc void @irq() addrspace(4) #0 {
    ret void
  }
  attributes #0 = { noinline "mcs251-isr-vector"="13" }
  !mcs251.signatures = !{!10000}
  !10000 = !{!"_irq", i32 1, i32 0}
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

;--- cc-oob-sys14.ll
; ContractCheck-isolated rejection of in-profile System slot: the IR Verifier is
; disabled, so the module-level contract check is the only layer that sees the
; attribute text.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="14" }
!mcs251.signatures = !{}

;--- ap-oob-sys14.mir
;MIRHEADER
  target triple = "mcs251-unknown-none"
  @llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
  define internal mcs251_intrcc void @irq() addrspace(4) #0 {
    ret void
  }
  attributes #0 = { noinline "mcs251-isr-vector"="14" }
  !mcs251.signatures = !{!10000}
  !10000 = !{!"_irq", i32 1, i32 0}
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

;--- cc-oob-empty.ll
; ContractCheck-isolated rejection of the empty attribute value: the IR Verifier
; is disabled, so the module-level contract check is the only layer that sees
; the attribute text.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="" }
!mcs251.signatures = !{}

;--- ap-oob-empty.mir
;MIRHEADER
  target triple = "mcs251-unknown-none"
  @llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
  define internal mcs251_intrcc void @irq() addrspace(4) #0 {
    ret void
  }
  attributes #0 = { noinline "mcs251-isr-vector"="" }
  !mcs251.signatures = !{!10000}
  !10000 = !{!"_irq", i32 1, i32 0}
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

;--- cc-oob-leadzero.ll
; ContractCheck-isolated rejection of a leading zero (probed: refused, not re-
; read as 126): the IR Verifier is disabled, so the module-level contract check
; is the only layer that sees the attribute text.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="0126" }
!mcs251.signatures = !{}

;--- ap-oob-leadzero.mir
;MIRHEADER
  target triple = "mcs251-unknown-none"
  @llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
  define internal mcs251_intrcc void @irq() addrspace(4) #0 {
    ret void
  }
  attributes #0 = { noinline "mcs251-isr-vector"="0126" }
  !mcs251.signatures = !{!10000}
  !10000 = !{!"_irq", i32 1, i32 0}
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

;--- cc-oob-plus45.ll
; ContractCheck-isolated rejection of an explicit plus sign: the IR Verifier is
; disabled, so the module-level contract check is the only layer that sees the
; attribute text.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="+45" }
!mcs251.signatures = !{}

;--- ap-oob-plus45.mir
;MIRHEADER
  target triple = "mcs251-unknown-none"
  @llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
  define internal mcs251_intrcc void @irq() addrspace(4) #0 {
    ret void
  }
  attributes #0 = { noinline "mcs251-isr-vector"="+45" }
  !mcs251.signatures = !{!10000}
  !10000 = !{!"_irq", i32 1, i32 0}
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

;--- cc-oob-minus0.ll
; ContractCheck-isolated rejection of a signed zero: the IR Verifier is
; disabled, so the module-level contract check is the only layer that sees the
; attribute text.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="-0" }
!mcs251.signatures = !{}

;--- ap-oob-minus0.mir
;MIRHEADER
  target triple = "mcs251-unknown-none"
  @llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
  define internal mcs251_intrcc void @irq() addrspace(4) #0 {
    ret void
  }
  attributes #0 = { noinline "mcs251-isr-vector"="-0" }
  !mcs251.signatures = !{!10000}
  !10000 = !{!"_irq", i32 1, i32 0}
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

;--- cc-oob-leadspace.ll
; ContractCheck-isolated rejection of leading whitespace: the IR Verifier is
; disabled, so the module-level contract check is the only layer that sees the
; attribute text.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"=" 45" }
!mcs251.signatures = !{}

;--- ap-oob-leadspace.mir
;MIRHEADER
  target triple = "mcs251-unknown-none"
  @llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
  define internal mcs251_intrcc void @irq() addrspace(4) #0 {
    ret void
  }
  attributes #0 = { noinline "mcs251-isr-vector"=" 45" }
  !mcs251.signatures = !{!10000}
  !10000 = !{!"_irq", i32 1, i32 0}
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

;--- cc-oob-trailspace.ll
; ContractCheck-isolated rejection of trailing whitespace: the IR Verifier is
; disabled, so the module-level contract check is the only layer that sees the
; attribute text.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="45 " }
!mcs251.signatures = !{}

;--- ap-oob-trailspace.mir
;MIRHEADER
  target triple = "mcs251-unknown-none"
  @llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
  define internal mcs251_intrcc void @irq() addrspace(4) #0 {
    ret void
  }
  attributes #0 = { noinline "mcs251-isr-vector"="45 " }
  !mcs251.signatures = !{!10000}
  !10000 = !{!"_irq", i32 1, i32 0}
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

;--- cc-oob-junk4x5.ll
; ContractCheck-isolated rejection of an embedded non-digit: the IR Verifier is
; disabled, so the module-level contract check is the only layer that sees the
; attribute text.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="4x5" }
!mcs251.signatures = !{}

;--- ap-oob-junk4x5.mir
;MIRHEADER
  target triple = "mcs251-unknown-none"
  @llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
  define internal mcs251_intrcc void @irq() addrspace(4) #0 {
    ret void
  }
  attributes #0 = { noinline "mcs251-isr-vector"="4x5" }
  !mcs251.signatures = !{!10000}
  !10000 = !{!"_irq", i32 1, i32 0}
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

;--- cc-oob-alpha.ll
; ContractCheck-isolated rejection of letters only: the IR Verifier is disabled,
; so the module-level contract check is the only layer that sees the attribute
; text.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="abc" }
!mcs251.signatures = !{}

;--- ap-oob-alpha.mir
;MIRHEADER
  target triple = "mcs251-unknown-none"
  @llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
  define internal mcs251_intrcc void @irq() addrspace(4) #0 {
    ret void
  }
  attributes #0 = { noinline "mcs251-isr-vector"="abc" }
  !mcs251.signatures = !{!10000}
  !10000 = !{!"_irq", i32 1, i32 0}
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

;--- cc-oob-overflow40.ll
; ContractCheck-isolated rejection of a 40-digit value past every field width:
; the IR Verifier is disabled, so the module-level contract check is the only
; layer that sees the attribute text.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
define internal mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="9999999999999999999999999999999999999999" }
!mcs251.signatures = !{}

;--- ap-oob-overflow40.mir
;MIRHEADER
  target triple = "mcs251-unknown-none"
  @llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
  define internal mcs251_intrcc void @irq() addrspace(4) #0 {
    ret void
  }
  attributes #0 = { noinline "mcs251-isr-vector"="9999999999999999999999999999999999999999" }
  !mcs251.signatures = !{!10000}
  !10000 = !{!"_irq", i32 1, i32 0}
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

;--- multi.ll
; G1-3 six-entry module: the profile maximum, the legacy low slots and the
; three reclassified slots, scrambled so record order must follow the
; definition order rather than any sorted-slot assumption.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [6 x ptr] [ptr addrspacecast (ptr addrspace(4) @f126 to ptr), ptr addrspacecast (ptr addrspace(4) @f0 to ptr), ptr addrspacecast (ptr addrspace(4) @f45 to ptr), ptr addrspacecast (ptr addrspace(4) @f8 to ptr), ptr addrspacecast (ptr addrspace(4) @f46 to ptr), ptr addrspacecast (ptr addrspace(4) @f31 to ptr)], section "llvm.metadata"
define internal mcs251_intrcc void @f126() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="126" }
define internal mcs251_intrcc void @f0() addrspace(4) #1 {
  ret void
}
attributes #1 = { noinline "mcs251-isr-vector"="0" }
define internal mcs251_intrcc void @f45() addrspace(4) #2 {
  ret void
}
attributes #2 = { noinline "mcs251-isr-vector"="45" }
define internal mcs251_intrcc void @f8() addrspace(4) #3 {
  ret void
}
attributes #3 = { noinline "mcs251-isr-vector"="8" }
define internal mcs251_intrcc void @f46() addrspace(4) #4 {
  ret void
}
attributes #4 = { noinline "mcs251-isr-vector"="46" }
define internal mcs251_intrcc void @f31() addrspace(4) #5 {
  ret void
}
attributes #5 = { noinline "mcs251-isr-vector"="31" }
!mcs251.signatures = !{}
