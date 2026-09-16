; P09 identity-fix instance 1 (ISR x bit gate): the A4 v2 capability scan's
; ISR-keepalive exemption classifies each llvm.used member by its terminal
; GlobalValue instead of demanding that every member be a program-address-
; space function.  Two REGISTERED member kinds may share the llvm.used root
; of a module that defines interrupt entries:
;   (i)  the program-AS function   -- the A2.2 ISR keepalive (T06), and
;   (ii) the marked bit-object GV  -- the BT12 bit-record keepalive (P09).
; clang emits exactly this mixed container for the corpus idiom "ISR sets a
; persistent bit flag, the main loop polls it"; before the fix that shape
; died in classifyModule with "module uses an ABI capability outside the
; registered A4 v2 object identity" (154 rollback sites / 42 demos).
;
; The member SHAPE rules are unchanged and fail-closed: a single-operand
; no-op cast chain (bitcast/addrspacecast, any number of hops) ending at the
; terminal.  Ordinary AS4/AS3 data members, unmarked ordinary globals and a
; function outside the program address space keep the original rejection.
;
; Positive matrix here (the Alice probe set, section 1 of the review):
;   mixed          ISR cast + bit handle in ONE llvm.used        (was: fatal)
;   mixed-multi    2 bit handles + ISR cast + ordinary AS4 fn     (was: fatal)
;   bit-cast       bit handle behind a no-op cast chain           (was: fatal)
;   isr-only       ISR cast alone                                 (regression)
;   bit-only       all-bit llvm.used, no ISR                      (regression)
;   multi-bit      multi-handle all-bit llvm.used, no ISR         (p1 shape)
;   extern-isr     extern bit reference + ISR (no bit member)     (regression)
; The same-run probe set (isr_bit / isr_only / isr_auto / isr_ext / ext_def /
; p1, built from C with the clang driver) is the end-to-end companion of this
; file: isr_auto's auto bit lowers to a private alloca (container = isr-only),
; isr_ext is extern-isr, ext_def is bit-only and p1 is multi-bit.
; Negative matrix (the rejection face must NOT widen):
;   bad-data       ISR cast + unmarked ordinary AS0 data member
;   bad-as4data    ISR cast + AS4 data member (mirrors isr-object.ll
;                  mixed-member.ll with a different member flavor)
;   bad-func-as    ISR cast + function outside the program AS
;   bad-chain      ISR cast + malformed cast chain (a non-no-op op in a
;                  single-operand member position); the generic IR verifier
;                  rejects it first, so -disable-verify is used to reach the
;                  target gate, exactly like the isr-object.ll convention

; RUN: split-file %s %t
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/mixed.ll -o %t/mixed.o
; RUN: llvm-readobj --file-headers --sections --section-data --symbols --relocations %t/mixed.o | FileCheck %s --check-prefix=MIXED --implicit-check-not=REG_BANK --implicit-check-not=.mcs251.dseg --implicit-check-not=.mcs251.xinit
; RUN: llvm-objcopy --dump-section=.mcs251.isr=%t/mixed.isr %t/mixed.o
; RUN: %python -c "import pathlib,struct,sys; b=pathlib.Path(sys.argv[1]).read_bytes(); assert len(b)==48; a=[struct.unpack('>HHBBBBHHIII',b[i:i+24]) for i in (0,24)]; assert a==[(2,24,1,1,1,1,1,1,0,0,0),(2,24,2,1,1,1,1,1,0,0,0)],a" %t/mixed.isr

; The mixed container survives the codegen optimiser pipeline as one root
; (llc's own O2 pipeline; llvm.used members are GlobalOpt/GlobalDCE roots by
; construction -- that is the keepalive's job).
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O2 -filetype=obj -mcs251-object-format=elf %t/mixed.ll -o %t/mixed-o2.o
; RUN: llvm-readobj --sections --relocations %t/mixed-o2.o | FileCheck %s --check-prefix=MIXED-O2

; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/mixed-multi.ll -o %t/mm.o
; RUN: llvm-readobj --sections --relocations %t/mm.o | FileCheck %s --check-prefix=MM
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/bit-cast.ll -o %t/bc.o
; RUN: llvm-readobj --sections --relocations %t/bc.o | FileCheck %s --check-prefix=BC
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/isr-only.ll -o %t/io.o
; RUN: llvm-readobj --sections %t/io.o | FileCheck %s --check-prefix=IO
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/bit-only.ll -o %t/bo.o
; RUN: llvm-readobj --sections --relocations %t/bo.o | FileCheck %s --check-prefix=BO
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/multi-bit.ll -o %t/mb.o
; RUN: llvm-readobj --sections --relocations %t/mb.o | FileCheck %s --check-prefix=MB
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/extern-isr.ll -o %t/ei.o
; RUN: llvm-readobj --sections --symbols %t/ei.o | FileCheck %s --check-prefix=EI --implicit-check-not=.mcs251.bit

; The rejection face: an unmarked data member (AS0 or AS4) or a function
; outside the program address space still fails the capability walk
; fail-closed, exactly like the pre-fix all-function rule.
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/bad-data.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=UNREGISTERED
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/bad-as4data.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=UNREGISTERED
; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/bad-func-as.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=UNREGISTERED
; The malformed-chain member never reaches the generic verifier's acceptance;
; it fails the target capability walk with the same fatal.
; RUN: not --crash llc -disable-verify -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/bad-chain.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=UNREGISTERED
; UNREGISTERED: LLVM ERROR: MCS251: module uses an ABI capability outside the registered A4 v2 object identity

; The v2 identity itself is unchanged by the fix: the mixed module is a
; regular v2 object (EFlagsV2 0x102, .mcs251.attributes present, no v1 note).
; The record sections land in readobj order: .mcs251.bit, then .mcs251.isr,
; then .mcs251.attributes.
;MIXED: Flags [ (0x102)
; One bit-object definition -> one 8-byte kind-1 record, init 0, caps 1.
;MIXED: Name: .mcs251.bit
;MIXED: Size: 8
;MIXED: 0000: 01010001 00000000
; One ISR definition -> ENTRY+REGISTER, 48 bytes.
;MIXED: Name: .mcs251.isr
;MIXED: Size: 48
;MIXED: Name: .mcs251.attributes
;MIXED-NOT: .note.mcs251.abi
; The per-use symbolic bit addresses come first (.rela.text), then the
; bit-record association (.rela.mcs251.bit) and the ISR table references
; (.rela.mcs251.isr), in readobj's order (relocations precede symbols).
;MIXED: 0x15 R_MCS251_BITADDR8 _flag 0x0
;MIXED: 0x2C R_MCS251_BITADDR8 _flag 0x0
;MIXED: 0x4 R_MCS251_BIT_REF _flag 0x0
;MIXED: 0xC R_MCS251_ISR_REF _t0isr 0x0
;MIXED: 0x24 R_MCS251_ISR_REF _t0isr 0x0
; The defining symbol is the record identity, not a byte object.
;MIXED: Name: _flag
;MIXED: Value: 0x0
;MIXED: Size: 1
;MIXED: Type: Object
;MIXED: Section: .mcs251.bit

;MIXED-O2: Name: .mcs251.bit
;MIXED-O2: Name: .mcs251.isr
;MIXED-O2: R_MCS251_BITADDR8 _flag
;MIXED-O2: R_MCS251_BIT_REF _flag
;MIXED-O2: R_MCS251_ISR_REF _t0isr

; Three-way mix: two bit records (16 bytes) + the 48-byte ISR table, and
; BITADDR8 uses of both handles.
;MM: Name: .mcs251.bit
;MM: Size: 16
;MM: Name: .mcs251.isr
;MM: R_MCS251_BITADDR8 _flag 0x0
;MM: R_MCS251_BITADDR8 _done 0x0
;MM: 0x4 R_MCS251_BIT_REF _flag 0x0
;MM: 0xC R_MCS251_BIT_REF _done 0x0
;MM: R_MCS251_ISR_REF _t0isr 0x0

; A bit handle behind a no-op cast chain is the same registered member shape
; (the terminal classification mirrors isMCS251BitObjectKeepaliveRoot).
;BC: Name: .mcs251.bit
;BC: Size: 8
;BC: Name: .mcs251.isr
;BC: Size: 48
;BC: R_MCS251_BITADDR8 _flag 0x0
;BC: 0x4 R_MCS251_BIT_REF _flag 0x0
;BC: R_MCS251_ISR_REF _t0isr 0x0

;IO: Name: .mcs251.isr
;IO: Size: 48

;BO: Name: .mcs251.bit
;BO: Size: 8
;BO: R_MCS251_BITADDR8 _flag 0x0
;BO: 0x4 R_MCS251_BIT_REF _flag 0x0

; The multi-handle all-bit shape (the p1 probe): five handles, three
; intrinsic kinds, no ISR anywhere in the module.
;MB: Name: .mcs251.bit
;MB: Size: 40
;MB: R_MCS251_BIT_REF _g_flag 0x0
;MB: 0xC R_MCS251_BIT_REF _use.ls 0x0
;MB: 0x14 R_MCS251_BIT_REF _g_a 0x0
;MB: 0x1C R_MCS251_BIT_REF _g_b 0x0
;MB: 0x24 R_MCS251_BIT_REF _s_flag 0x0

; The extern-bit + ISR shape: the extern declaration carries no record of its
; own (no .mcs251.bit section), only BITADDR8 uses.
;EI: Name: .mcs251.isr
;EI: Size: 48
;EI: Name: _ext_flag
;EI: Section: Undefined

;--- mixed.ll
; The corpus idiom in its minimal form: the ISR's addrspacecast keepalive and
; the persistent bit handle share one llvm.used root.
target triple = "mcs251-unknown-none"
@flag = global i8 0 #0
@llvm.used = appending global [2 x ptr] [ptr @flag, ptr addrspacecast (ptr addrspace(4) @t0isr to ptr)], section "llvm.metadata"

define mcs251_intrcc void @t0isr() addrspace(4) #1 {
  call addrspace(4) void @llvm.mcs251.bit.obj.set(ptr @flag)
  ret void
}
declare void @llvm.mcs251.bit.obj.set(ptr) addrspace(4)

define void @main() addrspace(4) {
  %v = call addrspace(4) i1 @llvm.mcs251.bit.obj.read(ptr @flag)
  br i1 %v, label %clr, label %out
clr:
  call addrspace(4) void @llvm.mcs251.bit.obj.clear(ptr @flag)
  br label %out
out:
  ret void
}
declare i1 @llvm.mcs251.bit.obj.read(ptr) addrspace(4)
declare void @llvm.mcs251.bit.obj.clear(ptr) addrspace(4)

attributes #0 = { "mcs251-bit-object" }
attributes #1 = { noinline "mcs251-isr-vector"="1" }

!mcs251.signatures = !{!10000, !10001}
!10000 = !{!"_main", i32 1, i32 0}
!10001 = !{!"_t0isr", i32 1, i32 0}

;--- mixed-multi.ll
; Two bit handles, the ISR cast and an ordinary AS4 function member in one
; container: every member is of a registered kind.
target triple = "mcs251-unknown-none"
@flag = global i8 0 #0
@done = global i8 1 #0
@llvm.used = appending global [4 x ptr] [ptr @flag, ptr @done, ptr addrspacecast (ptr addrspace(4) @t0isr to ptr), ptr addrspacecast (ptr addrspace(4) @plain to ptr)], section "llvm.metadata"

define mcs251_intrcc void @t0isr() addrspace(4) #1 {
  call addrspace(4) void @llvm.mcs251.bit.obj.set(ptr @flag)
  ret void
}
declare void @llvm.mcs251.bit.obj.set(ptr) addrspace(4)
define void @plain() addrspace(4) {
  ret void
}
define void @main() addrspace(4) {
  %v = call addrspace(4) i1 @llvm.mcs251.bit.obj.read(ptr @done)
  br i1 %v, label %clr, label %out
clr:
  call addrspace(4) void @llvm.mcs251.bit.obj.clear(ptr @done)
  br label %out
out:
  ret void
}
declare i1 @llvm.mcs251.bit.obj.read(ptr) addrspace(4)
declare void @llvm.mcs251.bit.obj.clear(ptr) addrspace(4)

attributes #0 = { "mcs251-bit-object" }
attributes #1 = { noinline "mcs251-isr-vector"="1" }

!mcs251.signatures = !{!10000, !10001, !10002}
!10000 = !{!"_main", i32 1, i32 0}
!10001 = !{!"_t0isr", i32 1, i32 0}
!10002 = !{!"_plain", i32 1, i32 0}

;--- bit-cast.ll
; The bit handle behind a two-hop no-op cast chain: the member shape rule
; (single-operand casts, any depth) is unchanged; only the terminal matters.
target triple = "mcs251-unknown-none"
@flag = global i8 0 #0
@llvm.used = appending global [2 x ptr] [ptr addrspacecast (ptr addrspace(1) addrspacecast (ptr @flag to ptr addrspace(1)) to ptr), ptr addrspacecast (ptr addrspace(4) @t0isr to ptr)], section "llvm.metadata"

define mcs251_intrcc void @t0isr() addrspace(4) #1 {
  call addrspace(4) void @llvm.mcs251.bit.obj.set(ptr @flag)
  ret void
}
declare void @llvm.mcs251.bit.obj.set(ptr) addrspace(4)

define void @main() addrspace(4) {
  %v = call addrspace(4) i1 @llvm.mcs251.bit.obj.read(ptr @flag)
  br i1 %v, label %clr, label %out
clr:
  call addrspace(4) void @llvm.mcs251.bit.obj.clear(ptr @flag)
  br label %out
out:
  ret void
}
declare i1 @llvm.mcs251.bit.obj.read(ptr) addrspace(4)
declare void @llvm.mcs251.bit.obj.clear(ptr) addrspace(4)

attributes #0 = { "mcs251-bit-object" }
attributes #1 = { noinline "mcs251-isr-vector"="1" }

!mcs251.signatures = !{!10000, !10001}
!10000 = !{!"_main", i32 1, i32 0}
!10001 = !{!"_t0isr", i32 1, i32 0}

;--- isr-only.ll
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @t0isr to ptr)], section "llvm.metadata"

define mcs251_intrcc void @t0isr() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="1" }

!mcs251.signatures = !{!10001}
!10001 = !{!"_t0isr", i32 1, i32 0}

;--- bit-only.ll
; The all-bit keepalive of a NON-ISR module keeps its existing treatment
; (ordinary walk; the container is registration data, not storage).
target triple = "mcs251-unknown-none"
@flag = global i8 0 #0
@llvm.used = appending global [1 x ptr] [ptr @flag], section "llvm.metadata"

define void @main() addrspace(4) {
  %v = call addrspace(4) i1 @llvm.mcs251.bit.obj.read(ptr @flag)
  br i1 %v, label %clr, label %out
clr:
  call addrspace(4) void @llvm.mcs251.bit.obj.clear(ptr @flag)
  br label %out
out:
  ret void
}
declare i1 @llvm.mcs251.bit.obj.read(ptr) addrspace(4)
declare void @llvm.mcs251.bit.obj.clear(ptr) addrspace(4)

attributes #0 = { "mcs251-bit-object" }

!mcs251.signatures = !{!10000}
!10000 = !{!"_main", i32 1, i32 0}

;--- multi-bit.ll
; The non-ISR multi-handle keepalive (p1 probe shape): five marked handles
; behind one llvm.used, three intrinsic kinds, one extern declaration that is
; no member at all.  No ISR definitions -> the ordinary walk, unchanged.
target triple = "mcs251-unknown-none"
@g_flag = global i8 0 #0
@use.ls = internal global i8 0 #0
@g_a = global i8 0 #0
@g_b = global i8 0 #0
@g_ext = external global i8 #0
@s_flag = internal global i8 0 #0
@llvm.used = appending global [5 x ptr] [ptr @g_flag, ptr @use.ls, ptr @g_a, ptr @g_b, ptr @s_flag], section "llvm.metadata"

define i32 @use() addrspace(4) {
  %v = call addrspace(4) i1 @llvm.mcs251.bit.obj.read(ptr @g_flag)
  br i1 %v, label %set, label %skip
set:
  call addrspace(4) void @llvm.mcs251.bit.obj.set(ptr @g_a)
  br label %skip
skip:
  call addrspace(4) void @llvm.mcs251.bit.obj.clear(ptr @g_b)
  call addrspace(4) void @llvm.mcs251.bit.obj.clear(ptr @g_ext)
  call addrspace(4) void @llvm.mcs251.bit.obj.toggle(ptr @s_flag)
  %r = call addrspace(4) i1 @llvm.mcs251.bit.obj.read(ptr @g_flag)
  %z = zext i1 %r to i32
  ret i32 %z
}
declare i1 @llvm.mcs251.bit.obj.read(ptr) addrspace(4)
declare void @llvm.mcs251.bit.obj.set(ptr) addrspace(4)
declare void @llvm.mcs251.bit.obj.clear(ptr) addrspace(4)
declare void @llvm.mcs251.bit.obj.toggle(ptr) addrspace(4)

attributes #0 = { "mcs251-bit-object" }

!mcs251.signatures = !{!10000}
!10000 = !{!"_use", i32 1, i32 0}

;--- extern-isr.ll
; The extern bit reference (no definition in this TU) plus an ISR: the
; extern declaration is not a container member at all.
target triple = "mcs251-unknown-none"
@ext_flag = external global i8 #0
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @t0isr to ptr)], section "llvm.metadata"

define mcs251_intrcc void @t0isr() addrspace(4) #1 {
  call addrspace(4) void @llvm.mcs251.bit.obj.set(ptr @ext_flag)
  ret void
}
declare void @llvm.mcs251.bit.obj.set(ptr) addrspace(4)

define void @main() addrspace(4) {
  %v = call addrspace(4) i1 @llvm.mcs251.bit.obj.read(ptr @ext_flag)
  br i1 %v, label %out, label %clr
clr:
  call addrspace(4) void @llvm.mcs251.bit.obj.clear(ptr @ext_flag)
  br label %out
out:
  ret void
}
declare i1 @llvm.mcs251.bit.obj.read(ptr) addrspace(4)
declare void @llvm.mcs251.bit.obj.clear(ptr) addrspace(4)

attributes #0 = { "mcs251-bit-object" }
attributes #1 = { noinline "mcs251-isr-vector"="1" }

!mcs251.signatures = !{!10000, !10001}
!10000 = !{!"_main", i32 1, i32 0}
!10001 = !{!"_t0isr", i32 1, i32 0}

;--- bad-data.ll
; An unmarked ordinary AS0 data member is NOT a registered member kind: the
; container keeps the original whole-table rejection.
target triple = "mcs251-unknown-none"
@data = global i8 7
@llvm.used = appending global [2 x ptr] [ptr @data, ptr addrspacecast (ptr addrspace(4) @t0isr to ptr)], section "llvm.metadata"

define mcs251_intrcc void @t0isr() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="1" }

!mcs251.signatures = !{!10001}
!10001 = !{!"_t0isr", i32 1, i32 0}

;--- bad-as4data.ll
; An AS4 data member (the classic escape flavor, mirroring isr-object.ll
; mixed-member.ll): still rejected.
target triple = "mcs251-unknown-none"
@as4data = addrspace(4) global i8 0
@llvm.used = appending global [2 x ptr] [ptr addrspacecast (ptr addrspace(4) @as4data to ptr), ptr addrspacecast (ptr addrspace(4) @t0isr to ptr)], section "llvm.metadata"

define mcs251_intrcc void @t0isr() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="1" }

!mcs251.signatures = !{!10001}
!10001 = !{!"_t0isr", i32 1, i32 0}

;--- bad-func-as.ll
; A function member outside the program address space keeps the original
; function-member rule (program AS only).  The function is defined in AS1
; (a D.5 data address space, not the program AS 4) and referenced through
; the standard cast chain -- the member shape is fine, the terminal is not.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [2 x ptr] [ptr addrspacecast (ptr addrspace(1) @as1fn to ptr), ptr addrspacecast (ptr addrspace(4) @t0isr to ptr)], section "llvm.metadata"

define void @as1fn() addrspace(1) {
  ret void
}
define mcs251_intrcc void @t0isr() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="1" }

!mcs251.signatures = !{!10001, !10002}
!10001 = !{!"_t0isr", i32 1, i32 0}
!10002 = !{!"_as1fn", i32 1, i32 0}

;--- bad-chain.ll
; A malformed member: the single-operand chain contains a non-no-op op
; (inttoptr of a constant) and never reaches a GlobalValue terminal.  The
; member shape rule is unchanged -- this is not a verified keepalive item.
; The generic IR verifier ("invalid llvm.used member") rejects the file
; first, so the RUN line uses -disable-verify to isolate the target gate.
target triple = "mcs251-unknown-none"
@llvm.used = appending global [2 x ptr] [ptr inttoptr (i32 4660 to ptr), ptr addrspacecast (ptr addrspace(4) @t0isr to ptr)], section "llvm.metadata"

define mcs251_intrcc void @t0isr() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="1" }

!mcs251.signatures = !{!10001}
!10001 = !{!"_t0isr", i32 1, i32 0}
