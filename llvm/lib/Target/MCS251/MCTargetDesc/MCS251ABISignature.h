//===-- MCS251ABISignature.h - locked SDCC ABI signature -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The single source of truth for the SDCC MCS-251 ABI signature this backend
// interoperates with.  The payload below was locked during the Phase 12
// dialect study (specimen assembled, linked and executed under QEMU; see
// MCS251AsmPrinter.cpp's Step-1 comment) and must stay byte-for-byte
// identical to what the reference SDCC toolchain emits:
//
//   - the assembly text path prints ".optsdcc " + MCS251ABISignature
//     (MCS251AsmPrinter::emitStartOfAsmFile; pinned by asxxxx-header.ll and
//     by the smoke harness's strict full-line comparison);
//   - the object path writes "O " + MCS251ABISignature
//     (MCS251RELObjectWriter; pinned by asxxxx-obj.ll).  sdld validates the
//     O record in strict --mcs251-abi mode.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MCS251_MCTARGETDESC_MCS251ABISIGNATURE_H
#define LLVM_LIB_TARGET_MCS251_MCTARGETDESC_MCS251ABISIGNATURE_H

namespace llvm {
namespace MCS251 {

// The .optsdcc/O-record payload WITHOUT the directive keyword.
inline constexpr const char *ABISignaturePayload =
    "stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small "
    "stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 "
    "all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 "
    "compiler-build=mcs251-abi1.0-r1";

} // namespace MCS251
} // namespace llvm

#endif // LLVM_LIB_TARGET_MCS251_MCTARGETDESC_MCS251ABISIGNATURE_H
