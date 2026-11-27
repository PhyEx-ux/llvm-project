//===- LinkerScript.cpp - GNU-style linker script parser for MCS251 -------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "LinkerScript.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include <cctype>
#include <map>
#include <set>

using namespace llvm;

namespace lld::mcs251 {
namespace {

struct MemRegion {
  uint64_t Origin = 0;
  uint64_t Length = 0;
  uint64_t Cursor = 0;
};

struct Token {
  enum Kind {
    Eof,
    Ident,
    Number,
    String,
    LParen,
    RParen,
    LBrace,
    RBrace,
    Colon,
    Semicolon,
    Comma,
    Equals,
    PlusEq,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Pipe,
    Amp,
    Caret,
    Tilde,
    LShift,
    RShift,
    Greater,
  };
  Kind K = Eof;
  std::string Text;
  uint64_t Num = 0;
  unsigned Line = 1;
};

static std::string lexicalAbsPath(StringRef P) {
  SmallString<256> Buf(P);
  if (sys::path::is_relative(Buf))
    sys::fs::make_absolute(Buf);
  sys::path::remove_dots(Buf, /*remove_dot_dot=*/true);
  return std::string(Buf);
}

static bool failAt(raw_ostream &Err, StringRef File, unsigned Line,
                   const Twine &Msg) {
  Err << "mcs251-lld: error: " << File << ":" << Line << ": " << Msg << '\n';
  return false;
}

static bool tokenizeScript(StringRef File, StringRef Buf,
                           std::vector<Token> &Out, raw_ostream &Err) {
  size_t I = 0;
  const size_t N = Buf.size();
  unsigned Line = 1;
  while (I < N) {
    char C = Buf[I];
    if (C == '\n') {
      ++Line;
      ++I;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(C))) {
      ++I;
      continue;
    }
    if (C == '/' && I + 1 < N && Buf[I + 1] == '*') {
      unsigned StartLine = Line;
      I += 2;
      bool Closed = false;
      while (I + 1 < N) {
        if (Buf[I] == '\n')
          ++Line;
        if (Buf[I] == '*' && Buf[I + 1] == '/') {
          I += 2;
          Closed = true;
          break;
        }
        ++I;
      }
      if (!Closed)
        return failAt(Err, File, StartLine, "unterminated block comment");
      continue;
    }
    if (C == '#' || (C == '/' && I + 1 < N && Buf[I + 1] == '/')) {
      while (I < N && Buf[I] != '\n')
        ++I;
      continue;
    }
    if (C == '"') {
      unsigned StartLine = Line;
      ++I;
      std::string S;
      bool Closed = false;
      while (I < N) {
        if (Buf[I] == '"') {
          ++I;
          Closed = true;
          break;
        }
        if (Buf[I] == '\n')
          ++Line;
        S.push_back(Buf[I++]);
      }
      if (!Closed)
        return failAt(Err, File, StartLine, "unterminated string literal");
      Out.push_back({Token::String, std::move(S), 0, StartLine});
      continue;
    }
    if (C == '+' && I + 1 < N && Buf[I + 1] == '=') {
      Out.push_back({Token::PlusEq, "+=", 0, Line});
      I += 2;
      continue;
    }
    if (C == '<' && I + 1 < N && Buf[I + 1] == '<') {
      Out.push_back({Token::LShift, "<<", 0, Line});
      I += 2;
      continue;
    }
    if (C == '>' && I + 1 < N && Buf[I + 1] == '>') {
      Out.push_back({Token::RShift, ">>", 0, Line});
      I += 2;
      continue;
    }
    auto Single = [&](Token::Kind K) {
      Out.push_back({K, std::string(1, C), 0, Line});
      ++I;
    };
    switch (C) {
    case '(': Single(Token::LParen); continue;
    case ')': Single(Token::RParen); continue;
    case '{': Single(Token::LBrace); continue;
    case '}': Single(Token::RBrace); continue;
    case ':': Single(Token::Colon); continue;
    case ';': Single(Token::Semicolon); continue;
    case ',': Single(Token::Comma); continue;
    case '=': Single(Token::Equals); continue;
    case '+': Single(Token::Plus); continue;
    case '-': Single(Token::Minus); continue;
    case '*': Single(Token::Star); continue;
    case '/': Single(Token::Slash); continue;
    case '%': Single(Token::Percent); continue;
    case '|': Single(Token::Pipe); continue;
    case '&': Single(Token::Amp); continue;
    case '^': Single(Token::Caret); continue;
    case '~': Single(Token::Tilde); continue;
    case '>': Single(Token::Greater); continue;
    default: break;
    }

    if (std::isdigit(static_cast<unsigned char>(C))) {
      size_t Start = I;
      if (C == '0' && I + 1 < N && (Buf[I + 1] == 'x' || Buf[I + 1] == 'X')) {
        I += 2;
        while (I < N && std::isxdigit(static_cast<unsigned char>(Buf[I])))
          ++I;
      } else {
        while (I < N && std::isdigit(static_cast<unsigned char>(Buf[I])))
          ++I;
      }
      uint64_t Mult = 1;
      StringRef Raw = Buf.slice(Start, I);
      if (I < N && (Buf[I] == 'K' || Buf[I] == 'k')) {
        Mult = 1024ULL;
        ++I;
      } else if (I < N && (Buf[I] == 'M' || Buf[I] == 'm')) {
        Mult = 1024ULL * 1024ULL;
        ++I;
      }
      uint64_t Val = 0;
      if (Raw.getAsInteger(0, Val))
        return failAt(Err, File, Line, "invalid integer literal '" + Raw + "'");
      Out.push_back({Token::Number, Buf.slice(Start, I).str(), Val * Mult, Line});
      continue;
    }

    // Identifiers, section names (.text, .mcs251.XSEG.foo), paths, or "."
    if (std::isalpha(static_cast<unsigned char>(C)) || C == '_' || C == '.' ||
        C == '$' || C == '?') {
      size_t Start = I;
      while (I < N) {
        char Ch = Buf[I];
        if (std::isalnum(static_cast<unsigned char>(Ch)) || Ch == '_' ||
            Ch == '.' || Ch == '$' || Ch == '?') {
          ++I;
        } else {
          break;
        }
      }
      Out.push_back({Token::Ident, Buf.slice(Start, I).str(), 0, Line});
      continue;
    }

    return failAt(Err, File, Line,
                  "unexpected character '" + std::string(1, C) + "'");
  }
  Out.push_back({Token::Eof, "", 0, Line});
  return true;
}

static std::string mapAreaName(StringRef Name) {
  std::string Up = Name.upper();
  if (Up == "HOME" || Up == ".HOME")
    return "HOME";
  if (Up == "VECS" || Up == ".VECS" || Up == ".VECTORS" || Up == ".ISR_VECTOR")
    return "VECS";
  if (Up == "BOOT" || Up == ".BOOT" || Up == ".STARTUP")
    return "BOOT";
  if (Up == "CSEG" || Up == ".TEXT" || Up == ".CODE" || Up == ".RODATA")
    return "CSEG";
  if (Up == "XINIT" || Up == ".XINIT")
    return "XINIT";
  if (Up == "XDATA_INIT" || Up == ".XDATA_INIT")
    return "XDATA_INIT";
  if (Up == "BITINIT" || Up == ".BITINIT")
    return "BITINIT";
  if (Up == "DSEG" || Up == ".DATA" || Up == ".BSS")
    return "DSEG";
  if (Up == "ISEG" || Up == ".ISEG")
    return "ISEG";
  if (Up == "XSEG" || Up == ".XDATA")
    return "XSEG";
  if (Name.starts_with(".mcs251.XSEG") || Name.starts_with(".mcs251.DATA."))
    return Name.str();
  return "";
}

static bool isCodeOrXdataArea(StringRef Area) {
  return Area == "HOME" || Area == "VECS" || Area == "BOOT" || Area == "CSEG" ||
         Area == "XINIT" || Area == "XDATA_INIT" || Area == "BITINIT" ||
         Area == "XSEG" || Area.starts_with(".mcs251.XSEG");
}

class ScriptParser {
  StringRef File;
  std::vector<Token> Toks;
  size_t Pos = 0;
  ParsedLinkerScript &Out;
  raw_ostream &Err;
  std::set<std::string> &ActiveFiles;
  std::map<std::string, MemRegion> &Regions;
  std::map<std::string, uint64_t> &SymVals;
  uint64_t Dot = 0;
  bool HasDot = false;

  const Token &peek(size_t Look = 0) const {
    return (Pos + Look < Toks.size()) ? Toks[Pos + Look] : Toks.back();
  }
  const Token &consume() { return Toks[Pos++]; }
  bool atEof() const { return peek().K == Token::Eof; }
  bool eat(Token::Kind K) {
    if (peek().K == K) {
      ++Pos;
      return true;
    }
    return false;
  }
  bool expect(Token::Kind K, StringRef What) {
    if (peek().K == K) {
      ++Pos;
      return true;
    }
    return failAt(Err, File, peek().Line,
                  "expected " + What + ", got '" + peek().Text + "'");
  }

  void setAreaStart(StringRef Area, uint32_t Addr) {
    for (auto &P : Out.AreaStarts) {
      if (P.first == Area) {
        P.second = Addr;
        return;
      }
    }
    Out.AreaStarts.emplace_back(Area.str(), Addr);
  }

  void addSymbol(StringRef Name, uint32_t Val, bool Provide) {
    SymVals[Name.str()] = Val;
    for (auto &S : Out.Symbols) {
      if (S.Name == Name) {
        S.Value = Val;
        S.Provide = Provide;
        return;
      }
    }
    Out.Symbols.push_back({Name.str(), Val, Provide});
  }

  bool parseExpr(uint64_t &Val) { return parseBitOr(Val); }

  bool parseBitOr(uint64_t &Val) {
    if (!parseBitXor(Val))
      return false;
    while (eat(Token::Pipe)) {
      uint64_t RHS = 0;
      if (!parseBitXor(RHS))
        return false;
      Val |= RHS;
    }
    return true;
  }

  bool parseBitXor(uint64_t &Val) {
    if (!parseBitAnd(Val))
      return false;
    while (eat(Token::Caret)) {
      uint64_t RHS = 0;
      if (!parseBitAnd(RHS))
        return false;
      Val ^= RHS;
    }
    return true;
  }

  bool parseBitAnd(uint64_t &Val) {
    if (!parseShift(Val))
      return false;
    while (eat(Token::Amp)) {
      uint64_t RHS = 0;
      if (!parseShift(RHS))
        return false;
      Val &= RHS;
    }
    return true;
  }

  bool parseShift(uint64_t &Val) {
    if (!parseAdd(Val))
      return false;
    while (peek().K == Token::LShift || peek().K == Token::RShift) {
      Token::Kind Op = consume().K;
      uint64_t RHS = 0;
      if (!parseAdd(RHS))
        return false;
      if (Op == Token::LShift)
        Val = (RHS >= 64) ? 0 : (Val << RHS);
      else
        Val = (RHS >= 64) ? 0 : (Val >> RHS);
    }
    return true;
  }

  bool parseAdd(uint64_t &Val) {
    if (!parseMul(Val))
      return false;
    while (peek().K == Token::Plus || peek().K == Token::Minus) {
      Token::Kind Op = consume().K;
      uint64_t RHS = 0;
      if (!parseMul(RHS))
        return false;
      if (Op == Token::Plus)
        Val += RHS;
      else
        Val -= RHS;
    }
    return true;
  }

  bool parseMul(uint64_t &Val) {
    if (!parseUnary(Val))
      return false;
    while (peek().K == Token::Star || peek().K == Token::Slash ||
           peek().K == Token::Percent) {
      const Token &Op = consume();
      uint64_t RHS = 0;
      if (!parseUnary(RHS))
        return false;
      if (Op.K == Token::Star) {
        Val *= RHS;
      } else {
        if (RHS == 0)
          return failAt(Err, File, Op.Line, "division by zero in linker script");
        Val = (Op.K == Token::Slash) ? (Val / RHS) : (Val % RHS);
      }
    }
    return true;
  }

  bool parseUnary(uint64_t &Val) {
    if (eat(Token::Plus))
      return parseUnary(Val);
    if (eat(Token::Minus)) {
      if (!parseUnary(Val))
        return false;
      Val = 0ULL - Val;
      return true;
    }
    if (eat(Token::Tilde)) {
      if (!parseUnary(Val))
        return false;
      Val = ~Val;
      return true;
    }
    return parsePrimary(Val);
  }

  bool parsePrimary(uint64_t &Val) {
    const Token &T = peek();
    if (T.K == Token::Number) {
      Val = consume().Num;
      return true;
    }
    if (T.K == Token::LParen) {
      consume();
      if (!parseExpr(Val))
        return false;
      return expect(Token::RParen, "')'");
    }
    if (T.K == Token::Ident) {
      std::string Name = consume().Text;
      if (Name == ".") {
        Val = Dot;
        return true;
      }
      std::string Up = StringRef(Name).upper();
      if (peek().K == Token::LParen) {
        consume();
        if (Up == "ORIGIN" || Up == "LENGTH") {
          if (peek().K != Token::Ident)
            return failAt(Err, File, peek().Line,
                          "expected memory region name in " + Up + "(...)");
          std::string RName = StringRef(consume().Text).upper();
          if (!expect(Token::RParen, "')'"))
            return false;
          auto It = Regions.find(RName);
          if (It == Regions.end())
            return failAt(Err, File, T.Line,
                          "unknown memory region '" + RName + "'");
          Val = (Up == "ORIGIN") ? It->second.Origin : It->second.Length;
          return true;
        }
        if (Up == "ALIGN") {
          uint64_t First = 0;
          if (!parseExpr(First))
            return false;
          uint64_t Base = Dot;
          uint64_t Alignment = First;
          if (eat(Token::Comma)) {
            Base = First;
            if (!parseExpr(Alignment))
              return false;
          }
          if (!expect(Token::RParen, "')'"))
            return false;
          if (Alignment == 0)
            return failAt(Err, File, T.Line, "ALIGN(0) is invalid");
          Val = ((Base + Alignment - 1) / Alignment) * Alignment;
          return true;
        }
        if (Up == "DEFINED") {
          if (peek().K != Token::Ident)
            return failAt(Err, File, peek().Line,
                          "expected symbol name in DEFINED(...)");
          std::string Sym = consume().Text;
          if (!expect(Token::RParen, "')'"))
            return false;
          Val = SymVals.count(Sym) ? 1 : 0;
          return true;
        }
        return failAt(Err, File, T.Line,
                      "unsupported linker script function '" + Name + "'");
      }
      auto It = SymVals.find(Name);
      if (It != SymVals.end()) {
        Val = It->second;
        return true;
      }
      return failAt(Err, File, T.Line,
                    "undefined symbol '" + Name + "' in linker script expression");
    }
    return failAt(Err, File, T.Line,
                  "expected expression, got '" + T.Text + "'");
  }

  bool parseMemoryBlock() {
    if (!expect(Token::LBrace, "'{' after MEMORY"))
      return false;
    while (!atEof() && peek().K != Token::RBrace) {
      if (peek().K != Token::Ident)
        return failAt(Err, File, peek().Line, "expected memory region name");
      unsigned Line = peek().Line;
      std::string RawName = consume().Text;
      std::string UpName = StringRef(RawName).upper();
      if (eat(Token::LParen)) {
        while (!atEof() && peek().K != Token::RParen)
          consume();
        if (!expect(Token::RParen, "')' after memory attributes"))
          return false;
      }
      if (!expect(Token::Colon, "':' after memory region name"))
        return false;
      bool HaveOrg = false, HaveLen = false;
      uint64_t Org = 0, Len = 0;
      while (!atEof() && peek().K != Token::RBrace) {
        if (peek().K != Token::Ident)
          break;
        StringRef Key = peek().Text;
        std::string UpKey = Key.upper();
        if (UpKey == "ORIGIN" || UpKey == "ORG" || UpKey == "O") {
          consume();
          if (!expect(Token::Equals, "'=' after ORIGIN"))
            return false;
          if (!parseExpr(Org))
            return false;
          HaveOrg = true;
        } else if (UpKey == "LENGTH" || UpKey == "LEN" || UpKey == "L") {
          consume();
          if (!expect(Token::Equals, "'=' after LENGTH"))
            return false;
          if (!parseExpr(Len))
            return false;
          HaveLen = true;
        } else {
          break;
        }
        if (!eat(Token::Comma))
          break;
      }
      eat(Token::Semicolon);
      if (!HaveOrg || !HaveLen)
        return failAt(Err, File, Line,
                      "memory region '" + RawName +
                          "' requires ORIGIN and LENGTH");
      if (Len == 0)
        return failAt(Err, File, Line,
                      "memory region '" + RawName + "' must have non-zero LENGTH");
      Regions[UpName] = {Org, Len, Org};

      // Map canonical MCU memory region names onto MCS-251 board windows.
      if (UpName == "FLASH" || UpName == "ROM" || UpName == "CODE") {
        if (Org > 0xffffff || Org + Len > 0x1000000)
          return failAt(Err, File, Line,
                        "FLASH region exceeds 24-bit CODE address space");
        Out.FlashBase = static_cast<uint32_t>(Org);
        Out.FlashSize = static_cast<uint32_t>(Len);
      } else if (UpName == "IRAM" || UpName == "DATA" || UpName == "DSEG") {
        if (Org + Len > 0x10000)
          return failAt(Err, File, Line,
                        "IRAM/DATA region exceeds 16-bit DATA space");
        Out.IramSize = static_cast<uint32_t>(Len);
        if (Org != 0)
          setAreaStart("DSEG", static_cast<uint32_t>(Org));
      } else if (UpName == "EDATA") {
        if (Org + Len > 0x10000)
          return failAt(Err, File, Line,
                        "EDATA region exceeds 16-bit DATA space");
        Out.EdataEnd = static_cast<uint32_t>(Org + Len - 1);
      } else if (UpName == "XDATA" || UpName == "XRAM" || UpName == "XSEG") {
        if (Org > 0xffffff || Org + Len > 0x1000000)
          return failAt(Err, File, Line,
                        "XDATA region exceeds 24-bit XDATA space");
        setAreaStart("XSEG", static_cast<uint32_t>(Org));
        Out.XdataSize = static_cast<uint32_t>(Len);
      }
    }
    return expect(Token::RBrace, "'}' closing MEMORY");
  }

  bool parseProvideOrAssign(bool IsProvide) {
    if (IsProvide && !expect(Token::LParen, "'(' after PROVIDE"))
      return false;
    if (peek().K != Token::Ident)
      return failAt(Err, File, peek().Line, "expected symbol name");
    unsigned Line = peek().Line;
    std::string Name = consume().Text;
    bool IsPlusEq = false;
    if (eat(Token::Equals)) {
      IsPlusEq = false;
    } else if (eat(Token::PlusEq)) {
      IsPlusEq = true;
    } else {
      return failAt(Err, File, peek().Line, "expected '=' in assignment");
    }
    uint64_t Val = 0;
    if (!parseExpr(Val))
      return false;
    if (IsProvide && !expect(Token::RParen, "')' after PROVIDE"))
      return false;
    eat(Token::Semicolon);
    if (Name == ".") {
      Dot = IsPlusEq ? (Dot + Val) : Val;
      HasDot = true;
      return true;
    }
    if (IsPlusEq) {
      auto It = SymVals.find(Name);
      Val += (It != SymVals.end() ? It->second : 0);
    }
    if (Val > 0xffffffffULL)
      return failAt(Err, File, Line, "symbol value exceeds 32-bit range");
    addSymbol(Name, static_cast<uint32_t>(Val), IsProvide);
    return true;
  }

  bool parseSectionBodyPatterns(bool InsideKeep) {
    // Parse patterns inside `*(.text* .rodata*)` or `KEEP(*(.isr*))`.
    while (!atEof() && peek().K != Token::RParen) {
      if (peek().K == Token::Star || peek().K == Token::Ident) {
        std::string Pat;
        while (!atEof() && peek().K != Token::RParen &&
               peek().K != Token::LParen && peek().K != Token::Semicolon &&
               peek().K != Token::Comma) {
          Pat += consume().Text;
          // Separate space-delimited patterns by checking if next token starts
          // a fresh glob (e.g., `.text* .rodata*` -> two Ident tokens).
          if (peek().K == Token::Ident &&
              (StringRef(peek().Text).starts_with(".") ||
               peek().Text == "COMMON"))
            break;
        }
        if (InsideKeep && !Pat.empty())
          Out.KeepSectionPatterns.push_back(std::move(Pat));
        eat(Token::Comma);
        continue;
      }
      consume();
    }
    return expect(Token::RParen, "')' closing section pattern");
  }

  bool parseOutputSectionBody() {
    if (!expect(Token::LBrace, "'{' opening section body"))
      return false;
    while (!atEof() && peek().K != Token::RBrace) {
      const Token &T = peek();
      if (T.K == Token::Semicolon) {
        consume();
        continue;
      }
      if (T.K == Token::Ident && StringRef(T.Text).upper() == "PROVIDE") {
        consume();
        if (!parseProvideOrAssign(/*IsProvide=*/true))
          return false;
        continue;
      }
      if (T.K == Token::Ident && StringRef(T.Text).upper() == "KEEP") {
        consume();
        if (!expect(Token::LParen, "'(' after KEEP"))
          return false;
        // Expect `*(patterns)` or `file(patterns)`
        while (!atEof() && peek().K != Token::RParen) {
          if (peek().K == Token::Star || peek().K == Token::Ident ||
              peek().K == Token::String) {
            consume();
            if (eat(Token::LParen)) {
              if (!parseSectionBodyPatterns(/*InsideKeep=*/true))
                return false;
            }
          } else {
            return failAt(Err, File, peek().Line, "malformed KEEP(...) rule");
          }
        }
        if (!expect(Token::RParen, "')' after KEEP"))
          return false;
        eat(Token::Semicolon);
        continue;
      }
      if (T.K == Token::Ident &&
          (peek(1).K == Token::Equals || peek(1).K == Token::PlusEq)) {
        if (!parseProvideOrAssign(/*IsProvide=*/false))
          return false;
        continue;
      }
      // Input section spec: `*(.text*)` or `obj.o(.text*)`
      if (T.K == Token::Star || T.K == Token::Ident || T.K == Token::String) {
        consume();
        if (eat(Token::LParen)) {
          if (!parseSectionBodyPatterns(/*InsideKeep=*/false))
            return false;
        }
        eat(Token::Semicolon);
        continue;
      }
      return failAt(Err, File, T.Line,
                    "unsupported statement '" + T.Text + "' in section body");
    }
    return expect(Token::RBrace, "'}' closing section body");
  }

  bool parseSectionsBlock() {
    if (!expect(Token::LBrace, "'{' after SECTIONS"))
      return false;
    while (!atEof() && peek().K != Token::RBrace) {
      const Token &T = peek();
      if (T.K == Token::Semicolon) {
        consume();
        continue;
      }
      if (T.K == Token::Ident && StringRef(T.Text).upper() == "PROVIDE") {
        consume();
        if (!parseProvideOrAssign(/*IsProvide=*/true))
          return false;
        continue;
      }
      if (T.K == Token::Ident &&
          (peek(1).K == Token::Equals || peek(1).K == Token::PlusEq)) {
        if (!parseProvideOrAssign(/*IsProvide=*/false))
          return false;
        continue;
      }
      if (T.K == Token::Ident) {
        unsigned Line = T.Line;
        std::string SecName = consume().Text;
        std::optional<uint64_t> ExplicitAddr;
        if (peek().K != Token::Colon) {
          uint64_t Addr = 0;
          if (!parseExpr(Addr))
            return false;
          ExplicitAddr = Addr;
        }
        if (!expect(Token::Colon, "':' after output section name"))
          return false;
        if (!parseOutputSectionBody())
          return false;
        std::string TargetRegion;
        if (eat(Token::Greater)) {
          if (peek().K != Token::Ident)
            return failAt(Err, File, peek().Line,
                          "expected memory region name after '>'");
          TargetRegion = StringRef(consume().Text).upper();
        }
        // Optional AT> region (accepted & ignored for ROM/RAM split).
        if (peek().K == Token::Ident && StringRef(peek().Text).upper() == "AT" &&
            peek(1).K == Token::Greater) {
          consume();
          consume();
          if (peek().K == Token::Ident)
            consume();
        }
        eat(Token::Semicolon);

        std::string Area = mapAreaName(SecName);
        if (Area.empty())
          return failAt(Err, File, Line,
                        "unsupported output section '" + SecName +
                            "' (expected HOME/VECS/BOOT/CSEG/.text/XINIT/"
                            "XDATA_INIT/BITINIT/DSEG/.data/ISEG/XSEG/.xdata)");
        std::optional<uint64_t> FinalAddr;
        if (ExplicitAddr) {
          FinalAddr = *ExplicitAddr;
          Dot = *ExplicitAddr;
          HasDot = true;
        } else if (!TargetRegion.empty()) {
          auto It = Regions.find(TargetRegion);
          if (It == Regions.end())
            return failAt(Err, File, Line,
                          "unknown memory region '" + TargetRegion + "'");
          FinalAddr = It->second.Cursor;
          Dot = It->second.Cursor;
          HasDot = true;
        } else if (HasDot) {
          FinalAddr = Dot;
        }
        if (FinalAddr) {
          const bool Is24 = isCodeOrXdataArea(Area);
          if (Is24 && *FinalAddr > 0xffffff)
            return failAt(Err, File, Line,
                          "section '" + SecName +
                              "' address exceeds 24-bit CODE/XDATA space");
          if (!Is24 && *FinalAddr > 0xffff)
            return failAt(Err, File, Line,
                          "section '" + SecName +
                              "' address exceeds 16-bit DATA space");
          setAreaStart(Area, static_cast<uint32_t>(*FinalAddr));
        }
        continue;
      }
      return failAt(Err, File, T.Line,
                    "unexpected token '" + T.Text + "' in SECTIONS");
    }
    return expect(Token::RBrace, "'}' closing SECTIONS");
  }

  bool parsePathOrIdent(std::string &Res) {
    if (peek().K == Token::String || peek().K == Token::Ident) {
      Res = consume().Text;
      return true;
    }
    // Support `-lfoo` inside INPUT(-lfoo)
    if (peek().K == Token::Minus && peek(1).K == Token::Ident) {
      consume();
      Res = "-" + consume().Text;
      return true;
    }
    return failAt(Err, File, peek().Line, "expected file path or identifier");
  }

  bool parseFileListDirective(bool IsExtern) {
    if (!expect(Token::LParen, "'('"))
      return false;
    while (!atEof() && peek().K != Token::RParen) {
      std::string Item;
      if (!parsePathOrIdent(Item))
        return false;
      if (IsExtern) {
        Out.ExternSymbols.push_back(std::move(Item));
      } else {
        if (!StringRef(Item).starts_with("-l") &&
            sys::path::is_relative(Item)) {
          SmallString<256> Joined(sys::path::parent_path(File));
          sys::path::append(Joined, Item);
          if (sys::fs::exists(Joined))
            Item = std::string(Joined);
        }
        Out.ExtraInputs.push_back(std::move(Item));
      }
      eat(Token::Comma);
    }
    if (!expect(Token::RParen, "')'"))
      return false;
    eat(Token::Semicolon);
    return true;
  }

public:
  ScriptParser(StringRef File, std::vector<Token> Toks,
               ParsedLinkerScript &Out, raw_ostream &Err,
               std::set<std::string> &ActiveFiles,
               std::map<std::string, MemRegion> &Regions,
               std::map<std::string, uint64_t> &SymVals)
      : File(File), Toks(std::move(Toks)), Out(Out), Err(Err),
        ActiveFiles(ActiveFiles), Regions(Regions), SymVals(SymVals) {}

  bool parseTopLevel();
};

static bool parseScriptImpl(StringRef Path, ParsedLinkerScript &Out,
                            raw_ostream &Err,
                            std::set<std::string> &ActiveFiles,
                            std::map<std::string, MemRegion> &Regions,
                            std::map<std::string, uint64_t> &SymVals) {
  std::string Abs = lexicalAbsPath(Path);
  if (!ActiveFiles.insert(Abs).second) {
    Err << "mcs251-lld: error: recursive INCLUDE of linker script " << Path
        << '\n';
    return false;
  }
  auto MB = MemoryBuffer::getFile(Path, /*IsText=*/true);
  if (!MB) {
    ActiveFiles.erase(Abs);
    Err << "mcs251-lld: error: cannot read linker script " << Path << '\n';
    return false;
  }
  Out.LoadedFiles.push_back(Abs);
  std::vector<Token> Toks;
  if (!tokenizeScript(Abs, (*MB)->getBuffer(), Toks, Err)) {
    ActiveFiles.erase(Abs);
    return false;
  }
  ScriptParser P(Abs, std::move(Toks), Out, Err, ActiveFiles, Regions, SymVals);
  bool OK = P.parseTopLevel();
  ActiveFiles.erase(Abs);
  return OK;
}

bool ScriptParser::parseTopLevel() {
  while (!atEof()) {
    const Token &T = peek();
    if (T.K == Token::Semicolon) {
      consume();
      continue;
    }
    if (T.K != Token::Ident)
      return failAt(Err, File, T.Line,
                    "unexpected token '" + T.Text + "' in linker script");
    std::string Up = StringRef(T.Text).upper();
    if (Up == "MEMORY") {
      consume();
      if (!parseMemoryBlock())
        return false;
    } else if (Up == "SECTIONS") {
      consume();
      if (!parseSectionsBlock())
        return false;
    } else if (Up == "ENTRY") {
      consume();
      if (!expect(Token::LParen, "'(' after ENTRY"))
        return false;
      std::string Sym;
      if (!parsePathOrIdent(Sym))
        return false;
      if (!expect(Token::RParen, "')' after ENTRY"))
        return false;
      eat(Token::Semicolon);
      Out.EntrySymbol = std::move(Sym);
    } else if (Up == "EXTERN") {
      consume();
      if (!parseFileListDirective(/*IsExtern=*/true))
        return false;
    } else if (Up == "INPUT" || Up == "GROUP") {
      consume();
      if (!parseFileListDirective(/*IsExtern=*/false))
        return false;
    } else if (Up == "SEARCH_DIR") {
      consume();
      if (!expect(Token::LParen, "'(' after SEARCH_DIR"))
        return false;
      std::string Dir;
      if (!parsePathOrIdent(Dir))
        return false;
      if (!expect(Token::RParen, "')' after SEARCH_DIR"))
        return false;
      eat(Token::Semicolon);
      if (sys::path::is_relative(Dir)) {
        SmallString<256> Joined(sys::path::parent_path(File));
        sys::path::append(Joined, Dir);
        if (sys::fs::exists(Joined))
          Out.SearchDirs.push_back(std::string(Joined));
      }
      Out.SearchDirs.push_back(std::move(Dir));
    } else if (Up == "RESERVE_DATA") {
      consume();
      if (!expect(Token::LParen, "'(' after RESERVE_DATA"))
        return false;
      uint64_t Start = 0, Size = 0;
      if (!parseExpr(Start) || !expect(Token::Comma, "',' in RESERVE_DATA") ||
          !parseExpr(Size) || !expect(Token::RParen, "')' after RESERVE_DATA"))
        return false;
      eat(Token::Semicolon);
      if (Start + Size > 0x10000)
        return failAt(Err, File, T.Line,
                      "RESERVE_DATA range exceeds 16-bit DATA space");
      Out.ReservedData.push_back(
          {static_cast<uint32_t>(Start), static_cast<uint32_t>(Start + Size)});
    } else if (Up == "INCLUDE") {
      consume();
      std::string Inc;
      if (!parsePathOrIdent(Inc))
        return false;
      eat(Token::Semicolon);
      SmallString<256> Resolved(Inc);
      if (sys::path::is_relative(Resolved)) {
        SmallString<256> Joined(sys::path::parent_path(File));
        sys::path::append(Joined, Resolved);
        Resolved = Joined;
      }
      if (!parseScriptImpl(Resolved, Out, Err, ActiveFiles, Regions, SymVals))
        return false;
    } else if (Up == "PROVIDE") {
      consume();
      if (!parseProvideOrAssign(/*IsProvide=*/true))
        return false;
    } else if (peek(1).K == Token::Equals || peek(1).K == Token::PlusEq) {
      if (!parseProvideOrAssign(/*IsProvide=*/false))
        return false;
    } else {
      return failAt(Err, File, T.Line,
                    "unsupported linker script command '" + T.Text + "'");
    }
  }
  return true;
}

} // namespace

bool parseLinkerScriptFile(StringRef Path, ParsedLinkerScript &Out,
                           raw_ostream &Err) {
  std::set<std::string> ActiveFiles;
  std::map<std::string, MemRegion> Regions;
  std::map<std::string, uint64_t> SymVals;
  for (const auto &S : Out.Symbols)
    SymVals[S.Name] = S.Value;
  return parseScriptImpl(Path, Out, Err, ActiveFiles, Regions, SymVals);
}

} // namespace lld::mcs251
