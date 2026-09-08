#!/usr/bin/env python3
"""Audit Keil intrinsic calls using raw, lexical, and condition-filtered bases."""

from __future__ import annotations

import argparse
import collections
import json
import re
import sys
from pathlib import Path

SOURCE_SUFFIXES = {".c", ".h"}
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]*intrins\.h)[>"]', re.IGNORECASE | re.MULTILINE)
CALL_RE = re.compile(r"\b(?P<name>_[A-Za-z][A-Za-z0-9_]*_)\s*\(")
DIRECTIVE_RE = re.compile(r"^[ \t]*#\s*(?P<op>\w+)(?P<body>.*)$")


def decode_source(path: Path) -> str:
    raw = path.read_bytes()
    for encoding in ("utf-8-sig", "gb18030"):
        try:
            return raw.decode(encoding)
        except UnicodeDecodeError:
            pass
    raise ValueError(f"cannot decode {path} as UTF-8 or GB18030")


def mask_comments_and_literals(source: str) -> str:
    """Blank comments/literals while preserving all line and offset positions."""
    output = list(source)
    state = "code"
    index = 0
    while index < len(source):
        char = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""
        if state == "code":
            if char == "/" and following == "*":
                output[index] = output[index + 1] = " "
                state = "block"
                index += 2
                continue
            if char == "/" and following == "/":
                output[index] = output[index + 1] = " "
                state = "line"
                index += 2
                continue
            if char in {"'", '"'}:
                output[index] = " "
                state = "char" if char == "'" else "string"
        elif state == "block":
            if char == "*" and following == "/":
                output[index] = output[index + 1] = " "
                state = "code"
                index += 2
                continue
            if char != "\n":
                output[index] = " "
        elif state == "line":
            if char == "\n":
                state = "code"
            else:
                output[index] = " "
        else:
            if char == "\\" and following:
                output[index] = " "
                if following != "\n":
                    output[index + 1] = " "
                index += 2
                continue
            if char == ("'" if state == "char" else '"'):
                output[index] = " "
                state = "code"
            elif char != "\n":
                output[index] = " "
        index += 1
    return "".join(output)


def definite_if_value(body: str) -> bool | None:
    expression = body.strip()
    if re.fullmatch(r"(?:0[xX][0-9A-Fa-f]+|\d+)(?:[uUlL]+)?", expression):
        return int(re.sub(r"[uUlL]+$", "", expression), 0) != 0
    return None


def mask_definitely_inactive_branches(lexical: str) -> str:
    """Mask only branches proven dead by simple integer #if expressions.

    This is intentionally not a replacement for the Keil preprocessor. It gives
    a reproducible third audit base while retaining unknown first branches,
    including ordinary header guards.
    """
    output: list[str] = []
    stack: list[tuple[bool, bool, bool]] = []  # parent, active, taken/unknown
    active = True
    for line in lexical.splitlines(keepends=True):
        directive = DIRECTIVE_RE.match(line)
        if directive:
            op = directive.group("op").lower()
            value = definite_if_value(directive.group("body"))
            conditional = op in {"if", "ifdef", "ifndef", "elif", "else", "endif"}
            if op == "if":
                parent = active
                selected = value is not False
                active = parent and selected
                stack.append((parent, active, value is not False))
            elif op in {"ifdef", "ifndef"}:
                parent = active
                stack.append((parent, parent, True))
                active = parent
            elif op == "elif" and stack:
                parent, _old, taken = stack[-1]
                selected = not taken and value is not False
                active = parent and selected
                stack[-1] = (parent, active, taken or value is not False)
            elif op == "else" and stack:
                parent, _old, taken = stack[-1]
                active = parent and not taken
                stack[-1] = (parent, active, True)
            elif op == "endif" and stack:
                parent, _old, _taken = stack.pop()
                active = parent
            if conditional:
                output.append("".join("\n" if char == "\n" else " " for char in line))
                continue
        output.append(line if active else "".join("\n" if char == "\n" else " " for char in line))
    return "".join(output)


def count_calls(text: str, relative: str, calls: collections.Counter[str], files: dict[str, set[str]]) -> None:
    for match in CALL_RE.finditer(text):
        name = match.group("name")
        calls[name] += 1
        files[name].add(relative)


def count_object(calls: collections.Counter[str], files: dict[str, set[str]]) -> dict[str, object]:
    return {
        "counts": dict(sorted(calls.items())),
        "file_counts": {name: len(files[name]) for name in sorted(calls)},
        "only_nop_called": set(calls) == {"_nop_"},
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("corpus_root", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    corpus_root = args.corpus_root.resolve()
    raw_calls: collections.Counter[str] = collections.Counter()
    lexical_calls: collections.Counter[str] = collections.Counter()
    active_calls: collections.Counter[str] = collections.Counter()
    raw_files: dict[str, set[str]] = collections.defaultdict(set)
    lexical_files: dict[str, set[str]] = collections.defaultdict(set)
    active_files: dict[str, set[str]] = collections.defaultdict(set)
    included_by: list[str] = []
    source_files = 0
    for path in sorted(corpus_root.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        source_files += 1
        text = decode_source(path)
        relative = path.relative_to(corpus_root).as_posix()
        lexical = mask_comments_and_literals(text)
        active = mask_definitely_inactive_branches(lexical)
        if INCLUDE_RE.search(lexical):
            included_by.append(relative)
        count_calls(text, relative, raw_calls, raw_files)
        count_calls(lexical, relative, lexical_calls, lexical_files)
        count_calls(active, relative, active_calls, active_files)

    raw = count_object(raw_calls, raw_files)
    lexical = count_object(lexical_calls, lexical_files)
    active = count_object(active_calls, active_files)
    output = {
        "format": "mcs251-porting-intrinsics-audit-v2",
        "corpus_root": str(corpus_root),
        "source_files_scanned": source_files,
        "intrins_header_include_files": len(included_by),
        "intrins_header_includes": included_by,
        "raw_text_call_matches": raw,
        "lexical_call_candidates": lexical,
        "preprocessed_call_candidates": active,
        # Compatibility aliases deliberately refer to the acceptance basis, not
        # raw grep-like matches that include comments.
        "intrinsic_call_counts": lexical["counts"],
        "intrinsic_call_file_counts": lexical["file_counts"],
        "only_nop_called": lexical["only_nop_called"],
        "preprocessor_strategy": "comments/literals removed; only definitely inactive integer #if branches excluded",
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(
        f"source_files={source_files} raw={raw['counts']} lexical={lexical['counts']} "
        f"preprocessed={active['counts']}"
    )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as exc:
        print(f"audit-intrinsics: error: {exc}", file=sys.stderr)
        sys.exit(2)
