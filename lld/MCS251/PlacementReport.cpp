//===- PlacementReport.cpp - placement report text v1 serializer ----------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "PlacementReport.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

using namespace llvm;

namespace lld::mcs251 {

// The frozen safe set: ASCII alphanumerics plus `_ . / : + -`.
static bool placementReportSafeByte(unsigned char C) {
  if ((C >= '0' && C <= '9') || (C >= 'A' && C <= 'Z') ||
      (C >= 'a' && C <= 'z'))
    return true;
  switch (C) {
  case '_':
  case '.':
  case '/':
  case ':':
  case '+':
  case '-':
    return true;
  default:
    return false;
  }
}

void escapePlacementReportField(StringRef In, raw_ostream &OS) {
  static const char Hex[] = "0123456789ABCDEF";
  for (unsigned char C : In.bytes()) {
    if (placementReportSafeByte(C)) {
      OS << char(C);
      continue;
    }
    OS << "\\x" << Hex[C >> 4] << Hex[C & 0xf];
  }
}

namespace {

// One emitted source line: the decoded sort key plus the rendered bytes.
struct ReportLine {
  std::string Stable, File, Sym, Section;
  std::string Rendered;
};

// `0x` + exactly eight lowercase hex digits (frozen width, no dependence on
// any formatter's prefix accounting).
static std::string fixedHex8(uint32_t V) {
  static const char H[] = "0123456789abcdef";
  std::string S = "0x";
  for (int I = 28; I >= 0; I -= 4)
    S += H[(V >> I) & 0xf];
  return S;
}

static std::string renderLine(const std::string &Stable, const std::string &Sym,
                              const std::string &File,
                              const std::string &Section, uint8_t Class,
                              uint8_t Entity, uint8_t Ownership,
                              uint32_t Address, uint32_t Size, uint32_t Align,
                              uint32_t Flags, uint32_t Hash, bool BoundOnly) {
  std::string S;
  raw_string_ostream OS(S);
  escapePlacementReportField(Stable, OS);
  OS << " | ";
  escapePlacementReportField(Sym, OS);
  OS << " | ";
  escapePlacementReportField(File, OS);
  OS << " | ";
  escapePlacementReportField(Section, OS);
  OS << " | " << unsigned(Class) << " | " << unsigned(Entity) << " | "
     << unsigned(Ownership) << " | " << fixedHex8(Address) << " | " << Size
     << " | " << Align << " | " << Flags << " | " << fixedHex8(Hash) << " | "
     << (BoundOnly ? 1 : 0);
  // retain is appended exactly when the MERGED flags carry bit0.
  if (Flags & 1)
    OS << " | retained";
  OS << '\n';
  OS.flush();
  return S;
}

} // namespace

void serializePlacementReport(const LinkerResult &Result, raw_ostream &OS) {
  std::vector<ReportLine> Lines;
  for (const LinkerResult::PlacementRecord &R : Result.Placement) {
    // One line per DISTINCT (path, ELF name) source; the stable is the row's
    // own merge key.  The dedup key is the contract's (path, ELF name,
    // stable) triple -- the stable is constant within a row, so the pair
    // suffices here.  Repeated bind records from one file legitimately
    // collapse to one physical line; the verifier still checks every
    // original record (contract §2 来源行粒度).
    std::vector<std::pair<std::string, std::string>> Triples; // (path, elf)
    for (const LinkerResult::PlacementRecord::Source &S : R.Sources) {
      std::pair<std::string, std::string> T{S.Path, S.ElfName};
      if (std::find(Triples.begin(), Triples.end(), T) == Triples.end())
        Triples.push_back(std::move(T));
    }
    if (Triples.empty())
      // Defensive: the producer always fills provenance, and the fallback
      // keeps "non-empty contract => non-empty report" true.
      Triples.push_back({R.File, R.Sym});
    for (const auto &T : Triples) {
      // The section column is the SOURCE record's input section: bound and
      // manifest sources have none.
      std::string Section;
      if (!Triples.empty() && !R.Sources.empty())
        for (const LinkerResult::PlacementRecord::Source &S : R.Sources)
          if (S.Path == T.first && S.ElfName == T.second) {
            Section = S.Section;
            break;
          }
      ReportLine L;
      L.Stable = R.Stable;
      L.File = T.first;
      L.Sym = T.second;
      L.Section = Section;
      L.Rendered = renderLine(R.Stable, T.second, T.first, Section,
                              R.StorageClass, R.Entity, R.Ownership, R.Address,
                              R.Size, R.Align, R.Flags, R.LayoutHash,
                              R.BoundOnly);
      Lines.push_back(std::move(L));
    }
  }
  // Byte-order sort on the decoded tuple; never locale collation.
  std::stable_sort(Lines.begin(), Lines.end(),
                   [](const ReportLine &A, const ReportLine &B) {
                     return std::tie(A.Stable, A.File, A.Sym, A.Section) <
                            std::tie(B.Stable, B.File, B.Sym, B.Section);
                   });
  for (const ReportLine &L : Lines)
    OS << L.Rendered;
}

} // namespace lld::mcs251