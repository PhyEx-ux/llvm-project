#!/usr/bin/env python3
"""Convert the direct SFR subset of a Keil C251 device header.

v1 deliberately emits only standard-C direct-address SFR lvalue macros. Bit
aliases and extended (XFR) register macros are retained as comments so that a
translation never silently claims support for Keil ``sbit``, ``xdata``, or
``far``.  The classifier is deliberately lexical: it strips comments/literals
without losing physical line numbers, recognizes every declaration statement,
and records unsupported or inactive declarations rather than dropping them.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Iterable

IDENTIFIER = r"[A-Za-z_]\w*"
NUMBER = r"(?:(?:0[xX][0-9A-Fa-f]+|\d+)(?:[uUlL]+)?)"
SFR_AT_RE = re.compile(
    rf"\bsfr\s+(?P<name>{IDENTIFIER})\s*=\s*(?P<address>{NUMBER})\s*;"
)
SBIT_AT_RE = re.compile(
    rf"\bsbit\s+(?P<name>{IDENTIFIER})\s*=\s*"
    rf"(?P<byte>{IDENTIFIER})\s*\^\s*(?P<bit>{NUMBER})\s*;"
)
DECLARATION_TOKEN_RE = re.compile(r"\b(?:sfr16|sfr|sbit)\b")
XFR_MACRO_RE = re.compile(
    rf"^[ \t]*#\s*define\s+(?P<name>{IDENTIFIER})\s+"
    # Keil headers spell qualifiers on either side of the base type, e.g.
    # ``unsigned char volatile far``. Capture everything before the pointer's
    # closing parenthesis and validate it separately.
    rf"\(\s*\*\s*\(\s*(?P<type>.*?)\s*\*\s*\)\s*"
    rf"(?P<address>{NUMBER})\s*\)[ \t]*$",
    re.MULTILINE,
)
XFR_CANDIDATE_RE = re.compile(
    rf"^[ \t]*#\s*define\s+(?P<name>{IDENTIFIER})\b.*?\(\s*\*\s*\(",
    re.MULTILINE,
)
DIRECTIVE_RE = re.compile(r"^[ \t]*#\s*(?P<op>\w+)(?P<body>.*)$")


@dataclass
class Entry:
    line: int
    name: str
    address: int | None = None
    byte: str | None = None
    bit: int | None = None
    qualifier: str | None = None
    reason: str | None = None


@dataclass
class Classification:
    direct_sfr: list[Entry] = field(default_factory=list)
    xfr: list[Entry] = field(default_factory=list)
    sbit: list[Entry] = field(default_factory=list)
    ignored: list[Entry] = field(default_factory=list)
    # Raw lexical declaration counts are separate from v1-supported unique
    # mappings. Thus 1,708 XFR statements can yield 1,706 unique XFR names.
    raw_declaration_counts: dict[str, int] = field(
        default_factory=lambda: {"direct_sfr": 0, "xfr": 0, "sbit": 0}
    )

    def counts(self) -> dict[str, int]:
        return {
            "direct_sfr": len(self.direct_sfr),
            "xfr": len(self.xfr),
            "sbit": len(self.sbit),
            "ignored": len(self.ignored),
        }


def read_source(path: Path) -> str:
    """Decode headers distributed as UTF-8, GBK, or ASCII-compatible text."""
    raw = path.read_bytes()
    for encoding in ("utf-8-sig", "gb18030"):
        try:
            return raw.decode(encoding)
        except UnicodeDecodeError:
            pass
    raise ValueError(f"cannot decode {path} as UTF-8 or GB18030")


def physical_xfr_address(address: int) -> bool:
    """Check documented C251 XFR physical ranges, not merely ``>= 0xFE00``."""
    # C251's external SFR window is 0x7EF000..0x7EFFFF in the official
    # STC headers; the direct alias form occupies 0xFE00..0xFFFF.  Do not use
    # an open-ended ``>= 0xFE00`` predicate: it accepts nonphysical values.
    return 0xFE00 <= address <= 0xFFFF or 0x7EF000 <= address <= 0x7EFFFF


def looks_like_xfr_type(type_text: str) -> bool:
    lowered = " ".join(type_text.lower().split())
    return "volatile" in lowered and ("xdata" in lowered or "far" in lowered)


def parse_c_integer(value: str) -> int:
    return int(re.sub(r"[uUlL]+$", "", value), 0)


def line_of(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def safe_excerpt(text: str, limit: int = 180) -> str:
    """Make arbitrary source text safe inside a generated C block comment."""
    compact = " ".join(text.replace("\r", " ").replace("\n", " ").split())
    compact = compact.replace("/*", "/ *").replace("*/", "* /")
    if len(compact) > limit:
        compact = compact[: limit - 3] + "..."
    return compact


def mask_comments_and_literals(source: str) -> str:
    """Replace comments and literals with spaces while retaining line layout.

    A declaration-like token in a block comment, a ``//`` comment, or a string
    literal must never become a usable SFR macro. Newlines are preserved so
    every report line refers to the original header.
    """
    output = list(source)
    index = 0
    state = "code"
    while index < len(source):
        char = source[index]
        next_char = source[index + 1] if index + 1 < len(source) else ""
        if state == "code":
            if char == "/" and next_char == "*":
                output[index] = output[index + 1] = " "
                index += 2
                state = "block"
                continue
            if char == "/" and next_char == "/":
                output[index] = output[index + 1] = " "
                index += 2
                state = "line"
                continue
            if char == '"':
                output[index] = " "
                index += 1
                state = "string"
                continue
            if char == "'":
                output[index] = " "
                index += 1
                state = "char"
                continue
        elif state == "block":
            if char == "*" and next_char == "/":
                output[index] = output[index + 1] = " "
                index += 2
                state = "code"
                continue
            if char != "\n":
                output[index] = " "
        elif state == "line":
            if char == "\\" and next_char == "\n":
                # C line splice: the // comment continues on the next physical
                # line. Mask the backslash but keep the newline for numbering.
                output[index] = " "
                index += 2
                continue
            if char == "\n":
                state = "code"
            else:
                output[index] = " "
        elif state in {"string", "char"}:
            if char == "\\" and next_char:
                output[index] = " "
                if next_char != "\n":
                    output[index + 1] = " "
                index += 2
                continue
            terminator = '"' if state == "string" else "'"
            if char == terminator:
                output[index] = " "
                state = "code"
            elif char == "\n":
                # C literals cannot span an unescaped newline; a stray quote
                # must not mask the rest of the file.
                state = "code"
            else:
                output[index] = " "
        index += 1
    return "".join(output)


def evaluate_if_expression(body: str) -> bool | None:
    """Evaluate only definite integer #if values; unknown expressions stay live.

    This is not a Keil preprocessor. Its safety promise is narrower: a branch
    proven inactive by ``#if 0`` (including simple integer zero) is never used
    to create a v1 mapping. Unknown guard expressions retain their first branch
    because ordinary include guards must remain parseable without a macro model.
    """
    expression = body.strip()
    if not expression:
        return None
    try:
        if re.fullmatch(NUMBER, expression):
            return parse_c_integer(expression) != 0
    except ValueError:
        pass
    return None


def split_by_preprocessor(masked: str) -> tuple[str, str]:
    """Return potentially-active and definitely-inactive lexical source.

    Directives themselves are blanked in both views. A simple conditional stack
    handles #if 0/#if 1/#else/#elif/#endif. Unknown conditions retain only their
    first branch as potentially active, which is stated in the README; v1 does
    not claim a full Keil preprocessor implementation.
    """
    active_lines: list[str] = []
    inactive_lines: list[str] = []
    # parent_active, current_active, branch_taken_or_unknown
    stack: list[tuple[bool, bool, bool]] = []
    currently_active = True
    for line in masked.splitlines(keepends=True):
        match = DIRECTIVE_RE.match(line)
        if match:
            op = match.group("op").lower()
            value = evaluate_if_expression(match.group("body"))
            conditional = op in {"if", "ifdef", "ifndef", "elif", "else", "endif"}
            if op == "if":
                parent = currently_active
                selected = value is not False
                currently_active = parent and selected
                stack.append((parent, currently_active, value is not False))
            elif op in {"ifdef", "ifndef"}:
                parent = currently_active
                # Unknown guard: retain the first branch, suppress #else.
                currently_active = parent
                stack.append((parent, currently_active, True))
            elif op == "elif" and stack:
                parent, _old_current, taken = stack[-1]
                selected = not taken and value is not False
                currently_active = parent and selected
                stack[-1] = (parent, currently_active, taken or value is not False)
            elif op == "else" and stack:
                parent, _old_current, taken = stack[-1]
                currently_active = parent and not taken
                stack[-1] = (parent, currently_active, True)
            elif op == "endif" and stack:
                parent, _old_current, _taken = stack.pop()
                currently_active = parent
            if conditional:
                blank = "".join("\n" if char == "\n" else " " for char in line)
                active_lines.append(blank)
                inactive_lines.append(blank)
                continue
        if currently_active:
            active_lines.append(line)
            inactive_lines.append("".join("\n" if char == "\n" else " " for char in line))
        else:
            active_lines.append("".join("\n" if char == "\n" else " " for char in line))
            inactive_lines.append(line)
    return "".join(active_lines), "".join(inactive_lines)


def statement_end(text: str, start: int) -> int:
    end = text.find(";", start)
    line_end = text.find("\n", start)
    if end < 0 or (line_end >= 0 and line_end < end):
        return len(text) if line_end < 0 else line_end
    return end + 1


def append_unique(
    result: Classification,
    category: str,
    entry: Entry,
    names: set[str],
    duplicate_reason: str,
) -> None:
    if entry.name in names:
        entry.reason = duplicate_reason
        result.ignored.append(entry)
        return
    names.add(entry.name)
    getattr(result, category).append(entry)


def classify_declarations(
    source: str,
    lexical: str,
    result: Classification,
    inactive: bool,
) -> None:
    direct_names = {entry.name for entry in result.direct_sfr}
    sbit_names = {entry.name for entry in result.sbit}
    position = 0
    while match := DECLARATION_TOKEN_RE.search(lexical, position):
        start = match.start()
        token = match.group()
        line = line_of(source, start)
        end = statement_end(lexical, start)
        excerpt = safe_excerpt(source[start:end])
        if inactive:
            result.ignored.append(
                Entry(line, "<inactive>", reason=f"inside definitely inactive conditional: {excerpt}")
            )
            position = max(end, match.end())
            continue
        if token == "sfr":
            parsed = SFR_AT_RE.match(lexical, start)
            if parsed:
                name = parsed.group("name")
                address = parse_c_integer(parsed.group("address"))
                result.raw_declaration_counts["direct_sfr"] += 1
                if 0x80 <= address <= 0xFF:
                    append_unique(
                        result, "direct_sfr", Entry(line, name, address), direct_names, "duplicate sfr name"
                    )
                else:
                    result.ignored.append(
                        Entry(line, name, address, reason="sfr address outside direct 0x80..0xff")
                    )
                position = parsed.end()
                continue
        elif token == "sbit":
            parsed = SBIT_AT_RE.match(lexical, start)
            if parsed:
                name = parsed.group("name")
                bit = parse_c_integer(parsed.group("bit"))
                byte = parsed.group("byte")
                result.raw_declaration_counts["sbit"] += 1
                if 0 <= bit <= 7:
                    append_unique(
                        result,
                        "sbit",
                        Entry(line, name, byte=byte, bit=bit),
                        sbit_names,
                        "duplicate sbit name",
                    )
                else:
                    result.ignored.append(
                        Entry(line, name, byte=byte, bit=bit, reason="sbit index outside 0..7")
                    )
                position = parsed.end()
                continue
        elif token == "sfr16":
            result.ignored.append(Entry(line, "sfr16", reason="sfr16 is not supported by v1"))
            position = max(end, match.end())
            continue
        result.ignored.append(Entry(line, "<unparsed>", reason=f"unparsed Keil declaration: {excerpt}"))
        position = max(end, match.end())


def classify_xfr_macros(
    source: str,
    lexical: str,
    result: Classification,
    inactive: bool,
) -> None:
    xfr_names = {entry.name for entry in result.xfr}
    for candidate in XFR_CANDIDATE_RE.finditer(lexical):
        start = candidate.start()
        line = line_of(source, start)
        line_end = lexical.find("\n", start)
        if line_end < 0:
            line_end = len(lexical)
        excerpt = safe_excerpt(source[start:line_end])
        # ``re.match(..., pos)`` does not make ``^`` relative to ``pos``.
        # Match the complete physical line instead, then retain its source line.
        parsed = XFR_MACRO_RE.fullmatch(lexical[start:line_end].rstrip("\r"))
        if inactive:
            name = parsed.group("name") if parsed else candidate.group("name")
            result.ignored.append(
                Entry(line, name, reason=f"inside definitely inactive conditional: {excerpt}")
            )
            continue
        if not parsed:
            result.ignored.append(Entry(line, candidate.group("name"), reason=f"unparsed XFR declaration: {excerpt}"))
            continue
        name = parsed.group("name")
        address = int(parsed.group("address"), 0)
        type_text = parsed.group("type")
        result.raw_declaration_counts["xfr"] += 1
        if not looks_like_xfr_type(type_text):
            result.ignored.append(
                Entry(line, name, address, reason="pointer macro lacks volatile xdata/far XFR type")
            )
            continue
        if not physical_xfr_address(address):
            result.ignored.append(
                Entry(line, name, address, reason="XFR physical address outside documented ranges")
            )
            continue
        qualifier = "xdata" if "xdata" in type_text.lower() else "far"
        append_unique(
            result, "xfr", Entry(line, name, address, qualifier=qualifier), xfr_names, "duplicate XFR name"
        )


def classify(lines: Iterable[str] | str) -> Classification:
    source = lines if isinstance(lines, str) else "\n".join(lines)
    masked = mask_comments_and_literals(source)
    active, inactive = split_by_preprocessor(masked)
    result = Classification()
    classify_declarations(source, active, result, inactive=False)
    classify_xfr_macros(source, active, result, inactive=False)
    # Inactive declarations never become mappings, but are still observable.
    classify_declarations(source, inactive, result, inactive=True)
    classify_xfr_macros(source, inactive, result, inactive=True)
    return result


def header_guard(output: Path) -> str:
    return "MCS251_PORTING_" + re.sub(r"[^A-Za-z0-9]", "_", output.stem).upper() + "_H"


def render_header(source: Path, output: Path, c: Classification) -> str:
    guard = header_guard(output)
    rows = [
        "/*",
        " * Generated by sfr-convert.py; do not edit by hand.",
        f" * Source: {source.name}",
        " * v1 emits only direct SFRs. XFR and sbit aliases remain documented",
        " * comments until the MCS251 Keil-dialect frontend provides them.",
        " */",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        "/* Direct-address SFRs (0x80..0xff): supported by current fork-clang. */",
    ]
    rows.extend(
        f"#define {entry.name:<16} (*(volatile unsigned char *)0x{entry.address:02X})"
        for entry in c.direct_sfr
    )
    rows.extend(["", "/* XFR mappings: registered only; pending xdata/far frontend support. */"])
    rows.extend(
        f"/* XFR {entry.name} = 0x{entry.address:X} ({entry.qualifier}); not defined in v1. */"
        for entry in c.xfr
    )
    rows.extend(["", "/* sbit aliases: normalized byte^bit; pending bit frontend support. */"])
    rows.extend(
        f"/* sbit {entry.name} = {entry.byte}^{entry.bit}; not defined in v1. */" for entry in c.sbit
    )
    if c.ignored:
        rows.extend(["", "/* Ignored Keil declarations (see JSON report for source lines). */"])
        rows.extend(
            f"/* ignored line {entry.line}: {safe_excerpt(entry.reason or '')} */" for entry in c.ignored
        )
    rows.extend(["", f"#endif /* {guard} */", ""])
    return "\n".join(rows)


def report_object(source: Path, output: Path, c: Classification) -> dict[str, object]:
    return {
        "format": "mcs251-sfr-convert-v1",
        "source": str(source),
        "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "output": str(output),
        "counts": c.counts(),
        "raw_declaration_counts": c.raw_declaration_counts,
        "direct_sfr": [asdict(e) for e in c.direct_sfr],
        "xfr": [asdict(e) for e in c.xfr],
        "sbit": [asdict(e) for e in c.sbit],
        "ignored": [asdict(e) for e in c.ignored],
        "conditional_strategy": (
            "v1 lexically excludes only definitely inactive integer #if 0 branches; "
            "unknown guards retain their first branch and are not a full Keil preprocessor"
        ),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="official Keil device header")
    parser.add_argument("-o", "--output", required=True, type=Path, help="generated v1 header")
    parser.add_argument("--report", required=True, type=Path, help="write classification JSON")
    args = parser.parse_args()

    source = args.input.resolve()
    output = args.output.resolve()
    report = args.report.resolve()
    classification = classify(read_source(source))
    output.parent.mkdir(parents=True, exist_ok=True)
    report.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(render_header(source, output, classification), encoding="utf-8", newline="\n")
    report.write_text(
        json.dumps(report_object(source, output, classification), indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    counts = classification.counts()
    raw = classification.raw_declaration_counts
    print(
        f"{source.name}: direct_sfr={counts['direct_sfr']} xfr={counts['xfr']}/{raw['xfr']} "
        f"sbit={counts['sbit']}/{raw['sbit']} ignored={counts['ignored']}"
    )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as exc:
        print(f"sfr-convert: error: {exc}", file=sys.stderr)
        sys.exit(2)
