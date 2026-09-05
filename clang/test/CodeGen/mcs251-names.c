// REQUIRES: mcs251-registered-target
// RUN: %clang_cc1 -triple mcs251-unknown-none -ffreestanding -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple mcs251-unknown-none -ffreestanding -S -o - %s | FileCheck %s --check-prefix=ASM
// RUN: %clang_cc1 -triple mcs251-unknown-none -ffreestanding -emit-obj -o %t %s

// Normal C declarations retain their identity in IR. Decoration happens once
// in the LLVM mangler, including C names that already start with an underscore.
unsigned char foo(void) { return 0x13; }
unsigned char _foo(void) { return 0x57; }
unsigned char explicit_name(void) __asm__("exact");
unsigned char explicit_name(void) { return 0x89; }
unsigned char explicit_underscore(void) __asm__("_exact");
unsigned char explicit_underscore(void) { return 0xab; }

// IR: target datalayout = "E-m:s-
// IR: define {{.*}}i8 @foo(
// IR: define {{.*}}i8 @_foo(
// IR: define {{.*}}i8 @"\01exact"(
// IR: define {{.*}}i8 @"\01_exact"(
// ASM: .globl _foo
// ASM: {{^}}_foo:
// ASM: .globl __foo
// ASM: {{^}}__foo:
// ASM: .globl exact
// ASM: {{^}}exact:
// ASM: .globl _exact
// ASM: {{^}}_exact:
