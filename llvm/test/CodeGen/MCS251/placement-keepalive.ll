; G11-B: the keepalive member classification (design rev 7 §3.2 N5, the
; "keepalive root processing" ruling; the §5 frozen matrix completed per
; review R4 2026-09-16) -- classifyMCS251KeepaliveMember decides
; ISR / Bit / Placement / Bind / Unmarked, and the classification is the
; single point consumed by the identity census, the storage-reservation
; scan, the global emission gate and (by not being reached) the Reject
; chain.
;
; The frozen matrix:
;   * the four-cell: AS0 object / AS3 object / AS4 CODE object (no
;     EXECINSTR) / function, EACH x {place, place+retain} -- all eight
;     members of ONE llvm.used root of an ordinary (non-ISR) module;
;   * the four mixed containers: ISR+Bit, ISR+Placement, Bit+Placement,
;     ISR+Bit+Placement;
;   * each shape's negative: the same container with ONE Unmarked member
;     added is rejected fail-closed at the capability census;
;   * the un-engaged shape keeps its historical position: an ordinary
;     (unplaced) AS4 cast root in a non-ISR module is rejected by the
;     ordinary walk, exactly as before G11 (probe 2[A2] invariant).
; Only per-member structural classification ever grants an exemption --
; never a container name or section name.
;
; RUN: split-file %s %t
;
; The four-cell: eight placed entities (four entity classes x retain
; on/off) in one root. Retain rides the section flags iff NOTE flags.bit0=1
; for every entity class, including the AS4 CODE object.
; RUN: llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/four-cell.ll -o %t/four-cell.o
; RUN: llvm-readobj --sections %t/four-cell.o | FileCheck %s --check-prefix=FOURCELL
; FOURCELL: Name: .mcu.fixed.fk
; FOURCELL: Flags [ (0x200006)
; FOURCELL: Name: .mcu.fixed.fp
; FOURCELL: Flags [ (0x6)
; FOURCELL: Name: .mcu.fixed.dk
; FOURCELL: Flags [ (0x200003)
; FOURCELL: Name: .mcu.fixed.dp
; FOURCELL: Flags [ (0x3)
; FOURCELL: Name: .mcu.fixed.xk
; FOURCELL: Flags [ (0x200003)
; FOURCELL: Name: .mcu.fixed.xp
; FOURCELL: Flags [ (0x3)
; FOURCELL: Name: .mcu.fixed.ck
; FOURCELL: Flags [ (0x200002)
; FOURCELL: Name: .mcu.fixed.cp
; FOURCELL: Flags [ (0x2)
; RUN: %python %S/Inputs/check-placement-note.py %t/four-cell.o \
; RUN:   fk 2 1 0 0xFC5100 39 1 1 \
; RUN:   fp 2 1 0 0xFC5110 39 1 0 \
; RUN:   dk 0 0 0 0x40     4 1 1 \
; RUN:   dp 0 0 0 0x44     4 1 0 \
; RUN:   xk 1 0 0 0x10000  4 1 1 \
; RUN:   xp 1 0 0 0x10004  4 1 0 \
; RUN:   ck 2 0 0 0xFC2000 4 1 1 \
; RUN:   cp 2 0 0 0xFC2010 4 1 0
; The whole root survives llc's own O2 pipeline with every contract intact.
; RUN: llc -mtriple=mcs251 -O2 -filetype=obj -mcs251-object-format=elf %t/four-cell.ll -o %t/four-cell-o2.o
; RUN: %python %S/Inputs/check-placement-note.py %t/four-cell-o2.o \
; RUN:   fk 2 1 0 0xFC5100 23 1 1 \
; RUN:   fp 2 1 0 0xFC5110 23 1 0 \
; RUN:   dk 0 0 0 0x40     4 1 1 \
; RUN:   dp 0 0 0 0x44     4 1 0 \
; RUN:   xk 1 0 0 0x10000  4 1 1 \
; RUN:   xp 1 0 0 0x10004  4 1 0 \
; RUN:   ck 2 0 0 0xFC2000 4 1 1 \
; RUN:   cp 2 0 0 0xFC2010 4 1 0
;
; ISR + Bit: the T06 ISR keepalive and a BT12 bit placeholder share the
; llvm.used of an ISR module.
; RUN: llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/isr-bit.ll -o %t/isr-bit.o
; RUN: llvm-readobj --sections %t/isr-bit.o | FileCheck %s --check-prefix=ISRBIT
; ISRBIT: Name: .mcs251.bit
; ISRBIT: Name: .mcs251.isr
;
; ISR + Placement: an ISR root also carrying a placed entity.
; RUN: llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/isr-place.ll -o %t/isr-place.o
; RUN: llvm-readobj --sections %t/isr-place.o | FileCheck %s --check-prefix=ISRPLACE
; ISRPLACE: Name: .mcs251.isr
; ISRPLACE: Name: .mcu.fixed.xk
;
; Bit + Placement: a non-ISR module's root mixing a bit placeholder and a
; placed entity.
; RUN: llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/bit-place.ll -o %t/bit-place.o
; RUN: llvm-readobj --sections %t/bit-place.o | FileCheck %s --check-prefix=BITPLACE
; BITPLACE: Name: .mcs251.bit
; BITPLACE: Name: .mcu.fixed.xk
;
; ISR + Bit + Placement: the three registered kinds in one root.
; RUN: llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/isr-bit-place.ll -o %t/isr-bit-place.o
; RUN: llvm-readobj --sections %t/isr-bit-place.o | FileCheck %s --check-prefix=ISRBITPLACE
; ISRBITPLACE: Name: .mcs251.bit
; ISRBITPLACE: Name: .mcs251.isr
; ISRBITPLACE: Name: .mcu.fixed.xk
;
; Negatives: each mixed container with ONE Unmarked member -- the census
; rejects the whole container fail-closed (old :619-equivalent verdict).
; RUN: not llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/bad-isr-bit.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=BAD
; RUN: not llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/bad-isr-place.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=BAD
; RUN: not llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/bad-bit-place.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=BAD
; RUN: not llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/bad-isr-bit-place.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=BAD
; RUN: not llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/bad-four-cell.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=BAD
; BAD: MCS251: module uses an ABI capability outside the registered A4 v2 object identity
;
; Negative: the un-engaged shape keeps its historical position -- an
; ordinary (unplaced) AS4 cast root in a NON-ISR module is rejected by the
; ordinary walk, exactly as before G11 (probe 2[A2] invariant).
; RUN: not llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/bad-plain-as4.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=BAD2
; BAD2: MCS251: module uses an ABI capability outside the registered A4 v2 object identity

;--- four-cell.ll
target triple = "mcs251-unknown-none"
@dk = global i32 3, align 1 #0
@dp = global i32 4, align 1 #1
@xk = addrspace(3) global i32 5, align 1 #2
@xp = addrspace(3) global i32 6, align 1 #3
@ck = addrspace(4) constant i32 7, align 1 #4
@cp = addrspace(4) constant i32 8, align 1 #5
@llvm.used = appending global [8 x ptr] [
  ptr @dk, ptr @dp,
  ptr addrspacecast (ptr addrspace(3) @xk to ptr), ptr addrspacecast (ptr addrspace(3) @xp to ptr),
  ptr addrspacecast (ptr addrspace(4) @ck to ptr), ptr addrspacecast (ptr addrspace(4) @cp to ptr),
  ptr addrspacecast (ptr addrspace(4) @fk to ptr), ptr addrspacecast (ptr addrspace(4) @fp to ptr)], section "llvm.metadata"

define i32 @fk(i32 %a) addrspace(4) #6 {
  ret i32 %a
}

define i32 @fp(i32 %a) addrspace(4) #7 {
  ret i32 %a
}

!mcs251.signatures = !{!10000, !10001}
!10000 = !{!"_fk", i32 1, i32 0, i32 0}
!10001 = !{!"_fp", i32 1, i32 0, i32 0}

attributes #0 = { "mcs251-place"="0x40,data,object,owned,1" "mcs251-stable-symbol"="dk" }
attributes #1 = { "mcs251-place"="0x44,data,object,owned,0" "mcs251-stable-symbol"="dp" }
attributes #2 = { "mcs251-place"="0x10000,xdata,object,owned,1" "mcs251-stable-symbol"="xk" }
attributes #3 = { "mcs251-place"="0x10004,xdata,object,owned,0" "mcs251-stable-symbol"="xp" }
attributes #4 = { "mcs251-place"="0xFC2000,code,object,owned,1" "mcs251-stable-symbol"="ck" }
attributes #5 = { "mcs251-place"="0xFC2010,code,object,owned,0" "mcs251-stable-symbol"="cp" }
attributes #6 = { "mcs251-place"="0xFC5100,code,function,owned,1" "mcs251-stable-symbol"="fk" }
attributes #7 = { "mcs251-place"="0xFC5110,code,function,owned,0" "mcs251-stable-symbol"="fp" }

;--- isr-bit.ll
target triple = "mcs251-unknown-none"
@bit = global i8 0, align 1 #0
@llvm.used = appending global [2 x ptr] [ptr @bit, ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"

define mcs251_intrcc void @irq() addrspace(4) #1 {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_irq", i32 1, i32 0}

attributes #0 = { "mcs251-bit-object" }
attributes #1 = { noinline "mcs251-isr-vector"="1" }

;--- isr-place.ll
target triple = "mcs251-unknown-none"
@xk = addrspace(3) global i16 1, align 1 #0
@llvm.used = appending global [2 x ptr] [ptr addrspacecast (ptr addrspace(3) @xk to ptr), ptr addrspacecast (ptr addrspace(4) @t0isr to ptr)], section "llvm.metadata"

define mcs251_intrcc void @t0isr() addrspace(4) #1 {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_t0isr", i32 1, i32 0}

attributes #0 = { "mcs251-place"="0x10010,xdata,object,owned,0" "mcs251-stable-symbol"="xk" }
attributes #1 = { noinline "mcs251-isr-vector"="1" }

;--- bit-place.ll
target triple = "mcs251-unknown-none"
@bit = global i8 0, align 1 #0
@xk = addrspace(3) global i16 1, align 1 #1
@llvm.used = appending global [2 x ptr] [ptr @bit, ptr addrspacecast (ptr addrspace(3) @xk to ptr)], section "llvm.metadata"

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}

attributes #0 = { "mcs251-bit-object" }
attributes #1 = { "mcs251-place"="0x10010,xdata,object,owned,0" "mcs251-stable-symbol"="xk" }

;--- isr-bit-place.ll
target triple = "mcs251-unknown-none"
@bit = global i8 0, align 1 #0
@xk = addrspace(3) global i16 1, align 1 #1
@llvm.used = appending global [3 x ptr] [ptr @bit, ptr addrspacecast (ptr addrspace(3) @xk to ptr), ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"

define mcs251_intrcc void @irq() addrspace(4) #2 {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_irq", i32 1, i32 0}

attributes #0 = { "mcs251-bit-object" }
attributes #1 = { "mcs251-place"="0x10010,xdata,object,owned,0" "mcs251-stable-symbol"="xk" }
attributes #2 = { noinline "mcs251-isr-vector"="1" }

;--- bad-isr-bit.ll
target triple = "mcs251-unknown-none"
@bit = global i8 0, align 1 #0
@ordinary = global i8 5
@llvm.used = appending global [3 x ptr] [ptr @bit, ptr @ordinary, ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"

define mcs251_intrcc void @irq() addrspace(4) #1 {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_irq", i32 1, i32 0}

attributes #0 = { "mcs251-bit-object" }
attributes #1 = { noinline "mcs251-isr-vector"="1" }

;--- bad-isr-place.ll
target triple = "mcs251-unknown-none"
@xk = addrspace(3) global i16 1, align 1 #0
@ordinary = global i8 5
@llvm.used = appending global [3 x ptr] [ptr addrspacecast (ptr addrspace(3) @xk to ptr), ptr @ordinary, ptr addrspacecast (ptr addrspace(4) @t0isr to ptr)], section "llvm.metadata"

define mcs251_intrcc void @t0isr() addrspace(4) #1 {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_t0isr", i32 1, i32 0}

attributes #0 = { "mcs251-place"="0x10010,xdata,object,owned,0" "mcs251-stable-symbol"="xk" }
attributes #1 = { noinline "mcs251-isr-vector"="1" }

;--- bad-bit-place.ll
target triple = "mcs251-unknown-none"
@bit = global i8 0, align 1 #0
@xk = addrspace(3) global i16 1, align 1 #1
@ordinary = global i8 5
@llvm.used = appending global [3 x ptr] [ptr @bit, ptr @ordinary, ptr addrspacecast (ptr addrspace(3) @xk to ptr)], section "llvm.metadata"

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}

attributes #0 = { "mcs251-bit-object" }
attributes #1 = { "mcs251-place"="0x10010,xdata,object,owned,0" "mcs251-stable-symbol"="xk" }

;--- bad-isr-bit-place.ll
target triple = "mcs251-unknown-none"
@bit = global i8 0, align 1 #0
@xk = addrspace(3) global i16 1, align 1 #1
@ordinary = global i8 5
@llvm.used = appending global [4 x ptr] [ptr @bit, ptr @ordinary, ptr addrspacecast (ptr addrspace(3) @xk to ptr), ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"

define mcs251_intrcc void @irq() addrspace(4) #2 {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_irq", i32 1, i32 0}

attributes #0 = { "mcs251-bit-object" }
attributes #1 = { "mcs251-place"="0x10010,xdata,object,owned,0" "mcs251-stable-symbol"="xk" }
attributes #2 = { noinline "mcs251-isr-vector"="1" }

;--- bad-four-cell.ll
target triple = "mcs251-unknown-none"
@dk = global i32 3, align 1 #0
@xk = addrspace(3) global i32 5, align 1 #1
@ordinary = global i8 5
@llvm.used = appending global [3 x ptr] [ptr @dk, ptr @ordinary, ptr addrspacecast (ptr addrspace(3) @xk to ptr)], section "llvm.metadata"

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}

attributes #0 = { "mcs251-place"="0x40,data,object,owned,1" "mcs251-stable-symbol"="dk" }
attributes #1 = { "mcs251-place"="0x10000,xdata,object,owned,1" "mcs251-stable-symbol"="xk" }

;--- bad-plain-as4.ll
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @f to ptr)], section "llvm.metadata"

define void @f() addrspace(4) {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
