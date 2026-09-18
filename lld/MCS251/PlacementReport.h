//===- PlacementReport.h - placement report text v1 serializer -*- C++ -*-===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// G11-D (design §3.4, contract §2, PM ruling R-2026-09-17-3 R1): the pure
// serializer for the placement report companion text.  It is deliberately a
// free function over `LinkerResult` with no environment, no file system and
// no failure policy -- the Driver decides whether and where to write.  The
// byte protocol is frozen as "placement report text v1":
//
//   * ASCII byte stream, one source triple per line; no BOM, no header, no
//     comments; exactly `SP PIPE SP` between columns; single LF line ends.
//   * first four columns (stable, sym, file, section) are byte-escaped: the
//     safe set is ASCII alphanumerics plus `_ . / : + -`; every other byte is
//     `\xHH` with two UPPERCASE hex digits.  The empty string is zero bytes.
//   * class/entity/ownership/bound_only are decimal digits; address and hash
//     are `0x` plus exactly eight lowercase hex digits; size/align/flags are
//     unsigned decimal with no leading zeros.
//   * a row whose merged flags have bit0 set gets ` | retained` appended.
//   * rows are sorted by the decoded (stable, file, sym, section) tuple in
//     raw byte order, never by locale.
//
// The report is not shared with the MCS251_PLACEMENT_DUMP test instrument
// (contract §2.4): the instrument observes "one row per group" at merge time,
// this serializer observes the expanded source contract at delivery time.
//===----------------------------------------------------------------------===//

#ifndef LLD_MCS251_PLACEMENTREPORT_H
#define LLD_MCS251_PLACEMENTREPORT_H

#include "LinkerCore.h"
#include "llvm/Support/raw_ostream.h"

namespace lld::mcs251 {

// Serialize `Result.Placement` per placement report text v1.  Pure: reads no
// environment variable, opens no file, cannot fail.  An empty contract writes
// zero bytes.
void serializePlacementReport(const LinkerResult &Result,
                              llvm::raw_ostream &OS);

// Escape one report string column (stable/sym/file/section) per the v1 byte
// protocol.  Exposed for the producer and for byte-level tests only.  The
// independent verifier deliberately does NOT call this: it implements its own
// strict decoder from the frozen protocol text, so producer and verifier can
// never share an escape/parse oracle (contract §2.4/§3, R-2026-09-17-3 R1).
void escapePlacementReportField(llvm::StringRef In, llvm::raw_ostream &OS);

} // namespace lld::mcs251

#endif
