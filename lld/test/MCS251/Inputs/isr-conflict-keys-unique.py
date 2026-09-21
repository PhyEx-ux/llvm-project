#!/usr/bin/env python3
"""Assert that a `--link-facts` text file carries ONE record per group key.

The reentrancy analysis writes one `conflict` group record per applicable
context combination, and a group key is (kind, same_slot, range value).  Two
records with the SAME key would mean the same combination was graded twice --
the state round 11's single-`Kind` selection could not produce, but that the
per-combination grading of round 13 must keep from reintroducing.

The check cannot be expressed as `FileCheck --check-prefix=X-NOT: conflict`
on `uniq -d` output: the extracted key fields are the NUMERIC fields
(`$2 $4 $5`, i.e. `kind=..`, `same_slot=..`, `range=..`), so the word
`conflict` never appears in the stream FileCheck sees, and `X-NOT: conflict`
passes even when a duplicate key IS present.  That vacuity was measured in
the WP2 round-13 review (a deliberately duplicated key line was fed to the
same two check prefixes and both returned 0).  Comparing the KEY COUNT with
the SET OF KEYS is deterministic and does not depend on how a sibling tool
formats its output, so the assertion lives here instead.

Usage: isr-conflict-keys-unique.py <facts-file> [<facts-file> ...]
Exit 0 when every file has one record per key, 1 otherwise (listing the
duplicated keys on stdout).
"""

import sys
from collections import Counter


def conflict_keys(path):
    keys = []
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if not line.startswith("conflict "):
                continue
            fields = {}
            for token in line.split()[1:]:
                name, sep, value = token.partition("=")
                if sep:
                    fields[name] = value
            keys.append((fields.get("kind"), fields.get("same_slot"),
                         fields.get("range")))
    return keys


def main():
    argv = sys.argv[1:]
    if not argv:
        sys.exit("usage: isr-conflict-keys-unique.py <facts-file> ...")
    failures = []
    for path in argv:
        keys = conflict_keys(path)
        counts = Counter(keys)
        for key, count in sorted(counts.items()):
            if count > 1:
                failures.append(f"{path}: group key {key} appears {count} times")
        print(f"{path}: {len(keys)} conflict record(s), "
              f"{len(counts)} distinct key(s)")
    if failures:
        print("FAIL")
        for line in failures:
            print("  " + line)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
