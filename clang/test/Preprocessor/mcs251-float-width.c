// REQUIRES: mcs251-registered-target
// RUN: %clang --target=mcs251-unknown-none -ffreestanding -fsyntax-only %s
// RUN: %clang --target=mcs251-unknown-none -Xclang -mcs251-memory-contract=1,1,32,8,1 -ffreestanding -fsyntax-only %s
// RUN: %clang --target=mcs251-unknown-none -ffreestanding -dM -E -o - %s | FileCheck %s --check-prefix=DEFAULT
// RUN: %clang --target=mcs251-unknown-none -Xclang -mcs251-memory-contract=1,1,32,8,1 -ffreestanding -dM -E -o - %s | FileCheck %s --check-prefix=COMPAT

_Static_assert(sizeof(float) == 4, "MCS251 float is IEEE binary32");
_Static_assert(sizeof(double) == 4, "MCS251 double is IEEE binary32");
_Static_assert(sizeof(long double) == 4,
               "MCS251 long double is IEEE binary32");
_Static_assert(__FLT_MANT_DIG__ == 24, "MCS251 float precision");
_Static_assert(__DBL_MANT_DIG__ == 24, "MCS251 double precision");
_Static_assert(__LDBL_MANT_DIG__ == 24, "MCS251 long double precision");

// DEFAULT-DAG: #define __SIZEOF_FLOAT__ 4
// DEFAULT-DAG: #define __SIZEOF_DOUBLE__ 4
// DEFAULT-DAG: #define __SIZEOF_LONG_DOUBLE__ 4
// DEFAULT-DAG: #define __FLT_MANT_DIG__ 24
// DEFAULT-DAG: #define __DBL_MANT_DIG__ 24
// DEFAULT-DAG: #define __LDBL_MANT_DIG__ 24
// COMPAT-DAG: #define __SIZEOF_FLOAT__ 4
// COMPAT-DAG: #define __SIZEOF_DOUBLE__ 4
// COMPAT-DAG: #define __SIZEOF_LONG_DOUBLE__ 4
// COMPAT-DAG: #define __FLT_MANT_DIG__ 24
// COMPAT-DAG: #define __DBL_MANT_DIG__ 24
// COMPAT-DAG: #define __LDBL_MANT_DIG__ 24
