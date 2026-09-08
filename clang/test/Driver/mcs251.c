// RUN: %clang --target=mcs251-unknown-none -### -c %s 2>&1 | FileCheck %s --check-prefix=COMPILE
// RUN: %clang --target=mcs251-unknown-none -### -S %s 2>&1 | FileCheck %s --check-prefix=ASSEMBLY
// RUN: %clang --target=mcs251-unknown-none -### -c -fhosted %s 2>&1 | FileCheck %s --check-prefix=HOSTED
// RUN: not %clang --target=mcs251-unknown-none -### %s 2>&1 | FileCheck %s --check-prefix=LINK
// RUN: not %clang --target=mcs251-unknown-none -### -c -fno-integrated-as %s 2>&1 | FileCheck %s --check-prefix=EXTERNAL-AS
// RUN: %clang --target=mcs251-unknown-none -dM -E %s | FileCheck %s --check-prefix=MACROS
// RUN: %clang --target=mcs251-unknown-none -fsyntax-only %s
// RUN: %clang --target=mcs251-unknown-none -### -c %s 2>&1 | FileCheck %s --check-prefix=ADDRSIG --implicit-check-not=error:
// RUN: %clang --target=mcs251-unknown-none -### -c -faddrsig %s 2>&1 | FileCheck %s --check-prefix=ADDRSIG --implicit-check-not=error:
// RUN: %clang --target=mcs251-unknown-none -### -c -fno-addrsig %s 2>&1 | FileCheck %s --check-prefix=NO-ADDRSIG --implicit-check-not=error:

// RUN: %clang --target=mcs251-unknown-none -### -O2 -c %s 2>&1 | FileCheck %s --check-prefix=TAIL --implicit-check-not=error:
// RUN: %clang --target=mcs251-unknown-none -### -O2 -c -foptimize-sibling-calls %s 2>&1 | FileCheck %s --check-prefix=TAIL --implicit-check-not=error:
// RUN: %clang --target=mcs251-unknown-none -### -O2 -c -fno-optimize-sibling-calls %s 2>&1 | FileCheck %s --check-prefix=NO-TAIL --implicit-check-not=error:

// The driver maps its default and all five models to canonical five-field
// numeric cc1 contracts.
// RUN: %clang --target=mcs251-unknown-none -### -c %s 2>&1 | FileCheck %s --check-prefix=MEMORY-DEFAULT --implicit-check-not=error:
// RUN: %clang --target=mcs251-unknown-none -mcs251-memory-model=tiny -### -c %s 2>&1 | FileCheck %s --check-prefix=MEMORY-TINY --implicit-check-not=error:
// RUN: %clang --target=mcs251-unknown-none -mcs251-memory-model=xtiny -### -c %s 2>&1 | FileCheck %s --check-prefix=MEMORY-XTINY --implicit-check-not=error:
// RUN: %clang --target=mcs251-unknown-none -mcs251-memory-model=small -### -c %s 2>&1 | FileCheck %s --check-prefix=MEMORY-SMALL --implicit-check-not=error:
// RUN: %clang --target=mcs251-unknown-none -mcs251-memory-model=xsmall -### -c %s 2>&1 | FileCheck %s --check-prefix=MEMORY-XSMALL --implicit-check-not=error:
// RUN: %clang --target=mcs251-unknown-none -mcs251-memory-model=large -### -c %s 2>&1 | FileCheck %s --check-prefix=MEMORY-LARGE --implicit-check-not=error:
// MEMORY-DEFAULT: "-cc1"
// MEMORY-DEFAULT-SAME: "-mcs251-memory-contract=1,2,32,8,1"
// MEMORY-TINY: "-mcs251-memory-contract=1,2,16,1,1"
// MEMORY-XTINY: "-mcs251-memory-contract=1,2,16,8,1"
// MEMORY-SMALL: "-mcs251-memory-contract=1,2,32,1,1"
// MEMORY-XSMALL: "-mcs251-memory-contract=1,2,32,8,1"
// MEMORY-LARGE: "-mcs251-memory-contract=1,2,32,3,1"

// TAIL: "-cc1"
// TAIL-NOT: "-fno-optimize-sibling-calls"
// TAIL: "-faddrsig"
// TAIL-NOT: "-fno-optimize-sibling-calls"
// NO-TAIL: "-fno-optimize-sibling-calls"
// ADDRSIG: "-cc1"
// ADDRSIG: "-faddrsig"
// NO-ADDRSIG: "-cc1"
// NO-ADDRSIG-NOT: "-faddrsig"

// COMPILE: "-cc1" "-triple" "mcs251-unknown-none"
// COMPILE-SAME: "-emit-obj"
// COMPILE-SAME: "-ffreestanding"
// COMPILE-NOT: "-cc1as"
// ASSEMBLY: "-S"
// ASSEMBLY: "-ffreestanding"
// HOSTED: "-cc1"
// HOSTED-NOT: "-ffreestanding"
// LINK: error: {{.*}}MCS251 driver linking; use mcs251_ld.py
// EXTERNAL-AS: error: {{.*}}MCS251 assembly input; use sdas251
// MACROS-DAG: #define __MCS251__ 1
// MACROS-DAG: #define __mcs251__ 1
// MACROS-DAG: #define __STDC_HOSTED__ 0
// MACROS-DAG: #define __SIZEOF_INT__ 4
// MACROS-DAG: #define __SIZEOF_LONG__ 4
// MACROS-DAG: #define __SIZEOF_POINTER__ 4
// MACROS-DAG: #define __BYTE_ORDER__ __ORDER_BIG_ENDIAN__

// The driver uses its existing cc1 passthrough, not llc's -mattr syntax.
// RUN: %clang --target=mcs251-unknown-none -Xclang -target-feature -Xclang +int16 -dM -E %s | FileCheck %s --check-prefix=INT16
// RUN: %clang --target=mcs251-unknown-none -Xclang -target-feature -Xclang +int16 -fsyntax-only %s
// RUN: not %clang --target=mcs251-unknown-none -Xclang -target-feature -Xclang +long16 -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=BAD-MODEL
// RUN: %clang --target=mcs251-unknown-none -Xclang -target-feature -Xclang +int16 -Xclang -target-feature -Xclang -int16 -dM -E %s | FileCheck %s --check-prefix=MACROS
// RUN: not %clang --target=mcs251-unknown-none -Xclang -target-feature -Xclang +ptr16 -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=BAD-MODEL
// INT16-DAG: #define __MCS251_INT16__ 1
// INT16-DAG: #define __SIZEOF_INT__ 2
// BAD-MODEL: error: invalid feature combination: MCS251 supports only +int16/-int16; long is fixed at 32 bits and pointer widths come from the numeric memory contract

#include <stddef.h>
#include <stdint.h>
_Static_assert(sizeof(uint16_t) == 2, "16-bit stdint type");
_Static_assert(sizeof(uint32_t) == 4, "32-bit stdint type");
_Static_assert(sizeof(size_t) == 4, "32-bit size_t");
