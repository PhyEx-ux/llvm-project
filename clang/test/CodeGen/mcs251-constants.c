// REQUIRES: mcs251-registered-target
// RUN: %clang_cc1 -triple mcs251-unknown-none -ffreestanding -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple mcs251-unknown-none -ffreestanding -O2 -S -o - %s | FileCheck %s --check-prefix=ASM
// RUN: %clang_cc1 -triple mcs251-unknown-none -ffreestanding -O2 -emit-obj -o %t %s

// Keep definitions in the IR and exercise a dynamic (not constant-folded) read.
const unsigned char t[] = {0x13, 0x57, 0x89, 0xab, 0, '"', '\\', 0xff};
const unsigned long wide = 0x89abcdefUL;
unsigned char table(unsigned char i) { return t[i]; }
const char *literal(void) { return "MCS251"; }

// IR-DAG: @t = {{.*}}constant [8 x i8] c"\13W\89\AB\00\22\\\FF", align 1
// IR-DAG: @wide = {{.*}}constant i32 -1985229329, align 1
// IR-DAG: private unnamed_addr constant [7 x i8] c"MCS251\00", align 1
// IR: getelementptr inbounds nuw i8, ptr @t,
// ASM: .area CSEG (CODE)
// ASM: _t:
// ASM-NEXT: .byte 19
// ASM-NEXT: .byte 87
// ASM-NEXT: .byte 137
// ASM-NEXT: .byte 171
// ASM-NEXT: .byte 0
// ASM-NEXT: .byte 34
// ASM-NEXT: .byte 92
// ASM-NEXT: .byte 255
// ASM: _wide:
// ASM-NEXT: .word 35243
// ASM-NEXT: .word 52719
// ASM: .L_.str:
// ASM-NEXT: .byte 77
// ASM-NEXT: .byte 67
// ASM-NEXT: .byte 83
// ASM-NEXT: .byte 50
// ASM-NEXT: .byte 53
// ASM-NEXT: .byte 49
// ASM-NEXT: .byte 0
