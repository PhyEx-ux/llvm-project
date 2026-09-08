; RUN: llvm-as %s -o %t.bc
; RUN: llvm-dis %t.bc -o %t.ll
; RUN: FileCheck %s < %t.ll
; RUN: llvm-as %t.ll -o %t.2.bc
; RUN: llvm-dis %t.2.bc -o - | FileCheck %s
; RUN: opt -passes='globaldce,verify' -S %t.bc -o - | FileCheck %s

target triple = "mcs251-unknown-none"

@llvm.used = appending global [1 x ptr] [ptr @irq], section "llvm.metadata"

define internal cc 128 void @irq() #0 {
  ret void
}

define void @ordinary() {
  ret void
}

attributes #0 = { noinline "mcs251-isr-vector"="51" }

; CHECK: @llvm.used = appending global [1 x ptr] [ptr @irq]
; CHECK: define internal mcs251_intrcc void @irq()
; CHECK: define void @ordinary()
; CHECK: attributes #0 = { noinline "mcs251-isr-vector"="51" }
