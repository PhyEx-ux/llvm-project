// RUN: %clang --target=mcs251-unknown-none -O1 -g -c %s -o %t.o
// RUN: llvm-readobj -S -r %t.o | FileCheck %s --check-prefix=OBJ
// RUN: llvm-dwarfdump --debug-info --debug-line %t.o | FileCheck %s --check-prefix=DWARF
// RUN: not %clang --target=mcs251-unknown-none -O1 -g -mllvm -mcs251-object-format=rel -c %s -o %t.rel 2>&1 | FileCheck %s --check-prefix=REL

// REL: error: debug information is not supported for target 'mcs251'

// OBJ: Name: .debug_abbrev
// OBJ: Name: .debug_info
// OBJ: Name: .debug_line
// OBJ: R_MCS251_32

// DWARF: DW_TAG_compile_unit
// DWARF: DW_AT_name ("{{.*}}mcs251-dwarf.c")
// DWARF: DW_TAG_subprogram
// DWARF: DW_AT_name ("compute")
// DWARF: DW_TAG_formal_parameter
// DWARF: .debug_line contents:
// DWARF: mcs251-dwarf.c

unsigned short compute(unsigned short a, unsigned short b) {
  unsigned short sum = (unsigned short)(a + b);
  return (unsigned short)(sum ^ 0x1234);
}
