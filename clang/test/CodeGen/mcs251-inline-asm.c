// RUN: %clang --target=mcs251-unknown-none -O1 -c %s -o %t.o
// RUN: llvm-objdump -d %t.o | FileCheck %s --check-prefix=DIS
// RUN: not %clang --target=mcs251-unknown-none -O1 -mllvm -mcs251-object-format=rel -c %s -o %t.rel 2>&1 | FileCheck %s --check-prefix=REL

// REL: inline assembly requires ELF object output

// DIS-DAG: <_raw_helper>:
// DIS-DAG: nop
// DIS-DAG: eret
__asm__(
    ".text\n"
    ".globl _raw_helper\n"
    ".type _raw_helper, @function\n"
    "_raw_helper:\n"
    "  nop\n"
    "  eret\n");

// DIS-DAG: <_add_inline>:
// DIS-DAG: add {{r[0-9]+}}, #0x05
unsigned char add_inline(unsigned char x) {
  __asm__ volatile("add %0, %1" : "+r"(x) : "i"(5) : "psw");
  return x;
}

// DIS-DAG: <_nop_bar>:
// DIS-DAG: nop
void nop_bar(void) {
  __asm__ volatile("nop" ::: "a", "b");
}
