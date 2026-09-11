// End-to-end byte check for MCS-251 controlled fixed bit references (BIT M2):
// C source -> clang -emit-llvm -> llc (-mcs251-object-format=elf) -> object
// -> hex dump of .text. Both `sbit` declaration forms at bit address 0x80
// must lower to the MCS-251 bit-write instructions CLR 0x80 (C2 80) and
// SETB 0x80 (D2 80), matching the frozen baseline bytes "C2 80 D2 80 .."
// (see validation/mcs251-models, BIT M2 evidence). The trailing AA is the
// function return.

// REQUIRES: mcs251-registered-target

// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-llvm -o %t.ll %s
// RUN: llc -mtriple=mcs251-unknown-none -mcs251-object-format=elf -filetype=obj -o %t.o %t.ll
// RUN: llvm-readelf -x .text %t.o | FileCheck %s
//
// M2-8 self-check: a dump carrying one surplus return opcode (ten bytes
// instead of nine) must be REJECTED by the anchored assertions below. The
// bytes are the frozen baseline plus one extra AA; feeding it to FileCheck
// must fail (rc=1), proving the end-of-line anchor actually bites.
// RUN: printf "Hex dump of section '.text':\n0x00000000 c280d280 c280d280 aaaa              ..........\n" > %t.bad.dump
// RUN: not FileCheck %s --input-file %t.bad.dump

// The dump line is pinned in full, byte-exact and end-of-line anchored
// (M2-8): the nine bytes are the literal hex prefix (CLR 0x80, SETB 0x80,
// twice, then the AA return), then the readelf padding spaces and the
// nine-dot ASCII column for those nine non-printable bytes, then EOL. A
// surplus byte anywhere changes the hex digits (e.g. "aaaa"), the padding
// width or the dot count, and no longer matches the anchored pattern.
// CHECK: Hex dump of section '.text':
// CHECK-NEXT: 0x00000000 c280d280 c280d280 aa{{ +}}.........{{$}}
// The dump is exactly one line: no extra trailing code (a second hex line
// would mean extra instructions beyond the pinned nine bytes).
// CHECK-NOT: 0x00000010

enum { P0 = 0x80 };
sbit A = 0x80;
sbit B = P0 ^ 0;

void entry(void) {
  A = 0;
  A = 1;
  B = 0;
  B = 1;
}
