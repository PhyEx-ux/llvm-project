// RUN: %clang_cc1 -triple mcs251 -fsyntax-only -verify=common,v2 %s
// RUN: %clang_cc1 -triple mcs251 -mcs251-memory-contract=1,2,16,1,1 -fsyntax-only -verify=common,v2 %s
// RUN: %clang_cc1 -triple mcs251 -mcs251-memory-contract=1,1,32,8,1 -fsyntax-only -verify=common,compat %s
#define AS(N) __attribute__((address_space(N)))
AS(0) int *p0;
AS(1) int *p1; // compat-error {{address space 1 is not defined by the MCS-251 memory contract}}
AS(2) int *p2; // compat-error {{address space 2 is not defined by the MCS-251 memory contract}}
AS(3) int *p3; // compat-error {{address space 3 is not defined by the MCS-251 memory contract}}
AS(4) int *p4; // compat-error {{address space 4 is not defined by the MCS-251 memory contract}}
AS(5) int *p5; // common-error {{address space 5 is not defined by the MCS-251 memory contract}}
AS(6) int *p6; // compat-error {{address space 6 is not defined by the MCS-251 memory contract}}
AS(7) int *p7; // compat-error {{address space 7 is not defined by the MCS-251 memory contract}}
AS(8) int *p8; // compat-error {{address space 8 is not defined by the MCS-251 memory contract}}
AS(9) int *p9; // compat-error {{address space 9 is not defined by the MCS-251 memory contract}}
AS(10) int *p10; // common-error {{address space 10 is not defined by the MCS-251 memory contract}}
