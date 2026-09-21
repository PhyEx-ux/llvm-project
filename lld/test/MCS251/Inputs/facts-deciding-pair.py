#!/usr/bin/env python3
"""Validate the DECIDING-CANDIDATE invariant of a `conflict` facts record.

Rounds 7 and 8 review items (2).  Two earlier shapes were too weak, each
demonstrated by a counter-input:

  * round 6 pinned only that the selected `slots=' identity matched SOME
    candidate's `a=/b='.  Making an EARLIER candidate `proven=1' still
    passed.
  * round 7 pinned the six candidates as an ordered list of patterns and
    added EXACT token comparison of `roots_a='/`roots_b='.  Appending a root
    to `roots_b=' was then rejected, and moving a `proven=1' earlier was
    rejected -- but REPEATING one of the six candidates in the middle still
    passed: a plain ordered list only constrains the expected records to
    appear in order, not that nothing else appears between them.

This helper re-derives the invariant from the PARSED records, so the
assertion does not rest on the FileCheck pattern list alone:

  1. take the one `conflict kind=<k>` record and read its `slots=' (the pair
     identity) and its `roots_a='/`roots_b=' (the deciding pair's roots);
  2. take the `conflict_pair` records belonging to THAT group, in file order;
  3. when `--expect-sequence' is given, require the group's candidate list to
     be EXACTLY that sequence: same length, same order, each candidate's
     `roots_a'/`roots_b' equal as whole comma-separated token lists, and
     each candidate's `proven' equal.  Length plus per-position equality
     rejects a repeated, extra or missing candidate -- the case an ordered
     pattern list admits;
  4. require the candidate identities `(a,b)' to be pairwise DISTINCT, so a
     duplicated pair cannot stand in for two different candidates;
  5. require the FIRST `proven=1' candidate's `a=/b=' to EQUAL the group's
     `slots=', and its `roots_a='/`roots_b=' to EQUAL the group's;
  6. require at least `--min-before' candidates before it that are
     EXPLICITLY `proven=0' (a `proven' value that is neither 0 nor 1 is an
     error, not a silent "before"), so "the first established candidate" is
     a non-vacuous claim;
  7. when `--expect-roots-a'/`--expect-roots-b' are given, require the
     group's root lists to EQUAL those exact token lists -- this is what
     makes a prefix match impossible: `--expect-roots-b _irq3' rejects
     `_irq3,_irq4' (a two-token list is not the one-token list asked for).

Exit 0 when the invariant holds, 1 when it does not, 2 on a usage error.

Usage:
  facts-deciding-pair.py [--min-before N] [--kind K]
                         [--expect-roots-a LIST] [--expect-roots-b LIST]
                         [--expect-sequence SPEC]
                         <facts-file>

  facts-deciding-pair.py --inject-repeat <facts-file> <out-file>
  facts-deciding-pair.py --inject-before-group <facts-file> <out-file>

`--expect-sequence SPEC' is a `;'-separated list of candidates, each written
`roots_a|roots_b|proven' with `,' separating the tokens of a root list: three
fields per item, in that order, and nothing else.  For example the A-B order
of the (z11) regression is

  _irq1|_irq2|0;_irq1|_irq3|0;_irq1|_irq4|0;_irq2|_irq3|1;_irq2|_irq4|1;_irq3|_irq4|0

(Each candidate is the pair's two ROOT LISTS and its `proven' state -- not a
`proven' value on its own, and not the two `a=/b=' SLOT identities: those are
compared separately by rule 4 below, as the pairwise-distinctness check.)

`--inject-repeat' writes a copy of the facts file with the FIRST
`conflict_pair' record repeated immediately after itself, and the same group
record unchanged.  That is the shape a plain ordered pattern list admits and
a `CHECK-NEXT' chain plus `--expect-sequence' reject, so it is the negative
half of the (z11) regression: a test can assert that the checker returns 1
on this file while returning 0 on the original.

`--inject-before-group' writes a copy with one candidate record moved ahead
of the group `conflict' record.  It is the negative half of the claim that
each group's block is closed at BOTH ends: the leading `-NOT' directive, not
just the parser, must reject it.
"""
import re
import sys

CONFLICT = re.compile(r"^conflict kind=(\d+) proven=(\d+)\b")
PAIR = re.compile(r"^conflict_pair\b")


def fields(line):
    """Parse `key=value` and bare flags out of one facts record line."""
    out = {}
    for tok in line.split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            out[k] = v
        else:
            out[tok] = True
    return out


def root_list(value):
    return [] if value is None else value.split(",")


def parse_sequence(spec):
    """`ra|rb|proven;ra|rb|proven;...' -> list of (roots_a, roots_b, proven)."""
    seq = []
    for item in spec.split(";"):
        if not item:
            continue
        parts = item.split("|")
        if len(parts) != 3:
            sys.stderr.write(
                "facts-deciding-pair: --expect-sequence item %r is not "
                "`roots_a|roots_b|proven'\n" % item)
            return None
        seq.append((parts[0], parts[1], parts[2]))
    return seq


def inject_repeat(src, dst):
    """Repeat the first `conflict_pair' record right after itself."""
    lines = open(src, errors="replace").read().splitlines()
    i = next((i for i, l in enumerate(lines) if PAIR.match(l)), None)
    if i is None:
        sys.stderr.write("facts-deciding-pair: no conflict_pair record to "
                         "repeat\n")
        return 1
    lines.insert(i + 1, lines[i])
    with open(dst, "w") as f:
        f.write("\n".join(lines) + "\n")
    return 0


def inject_before_group(src, dst):
    """Insert a candidate record BEFORE the group record.

    Round 9 item (6).  The two orders of the (z11) regression are each
    pinned as a contiguous block that starts at the group `conflict' record
    and runs through its six candidates.  A candidate sitting before the
    group record is outside that block, so it is only caught if the block is
    closed at its FRONT as well as at its back: the leading `-NOT' asserts
    that nothing matching `conflict_pair' precedes the group record.

    The inserted line is derived from the file's own first candidate, so the
    negative input stays a plausible record rather than a hand-frozen
    string: a real candidate of this group, placed ahead of the group
    record.  The group record itself and every other line are unchanged.
    """
    lines = open(src, errors="replace").read().splitlines()
    g = next((i for i, l in enumerate(lines) if CONFLICT.match(l)), None)
    p = next((i for i, l in enumerate(lines) if PAIR.match(l)), None)
    if g is None or p is None:
        sys.stderr.write("facts-deciding-pair: need one group record and at "
                         "least one conflict_pair record\n")
        return 1
    if p < g:
        sys.stderr.write("facts-deciding-pair: this file already has a "
                         "candidate before the group record; nothing to "
                         "inject\n")
        return 1
    lines.insert(g, lines[p])
    with open(dst, "w") as f:
        f.write("\n".join(lines) + "\n")
    return 0


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--inject-repeat":
        if len(sys.argv) != 4:
            sys.stderr.write("usage: facts-deciding-pair.py --inject-repeat "
                             "<facts-file> <out-file>\n")
            return 2
        return inject_repeat(sys.argv[2], sys.argv[3])
    if len(sys.argv) > 1 and sys.argv[1] == "--inject-before-group":
        if len(sys.argv) != 4:
            sys.stderr.write("usage: facts-deciding-pair.py "
                             "--inject-before-group <facts-file> <out-file>\n")
            return 2
        return inject_before_group(sys.argv[2], sys.argv[3])

    argv = sys.argv[1:]
    min_before = 0
    want_kind = None
    expect_a = None
    expect_b = None
    expect_seq = None
    while argv and argv[0].startswith("--"):
        if argv[0] == "--min-before":
            min_before = int(argv[1])
            argv = argv[2:]
        elif argv[0] == "--kind":
            want_kind = int(argv[1])
            argv = argv[2:]
        elif argv[0] == "--expect-roots-a":
            expect_a = argv[1].split(",") if argv[1] else []
            argv = argv[2:]
        elif argv[0] == "--expect-roots-b":
            expect_b = argv[1].split(",") if argv[1] else []
            argv = argv[2:]
        elif argv[0] == "--expect-sequence":
            expect_seq = parse_sequence(argv[1])
            if expect_seq is None:
                return 2
            argv = argv[2:]
        else:
            sys.stderr.write("facts-deciding-pair: unknown option %s\n" % argv[0])
            return 2
    if len(argv) != 1:
        sys.stderr.write(__doc__.splitlines()[-1].strip() + "\n")
        return 2

    lines = open(argv[0], errors="replace").read().splitlines()
    conflicts = [l for l in lines if CONFLICT.match(l)]
    if want_kind is not None:
        conflicts = [l for l in conflicts
                     if int(CONFLICT.match(l).group(1)) == want_kind]
    if len(conflicts) != 1:
        sys.stderr.write("facts-deciding-pair: expected exactly one conflict "
                         "record, found %d\n" % len(conflicts))
        return 1

    c = fields(conflicts[0])
    if c.get("proven") != "1":
        sys.stderr.write("facts-deciding-pair: the conflict group is not "
                         "proven (%s)\n" % conflicts[0])
        return 1
    if "slots" not in c:
        sys.stderr.write("facts-deciding-pair: the conflict record carries no "
                         "slots= field\n")
        return 1
    group_slots = c["slots"]

    # The pairs of THIS group, in file order.
    pairs = []
    for l in lines:
        if not PAIR.match(l):
            continue
        f = fields(l)
        if f.get("group") != c.get("range") and "group" in f:
            continue
        pairs.append((l, f))
    if not pairs:
        sys.stderr.write("facts-deciding-pair: no conflict_pair records\n")
        return 1

    problems = []

    # ---- 5. exact candidate sequence (length, order, roots, proven) ----
    if expect_seq is not None:
        if len(pairs) != len(expect_seq):
            problems.append(
                "the group has %d candidate record(s), but the sequence "
                "assertion names exactly %d; an extra or repeated candidate "
                "would change the order the deciding pair is taken from"
                % (len(pairs), len(expect_seq)))
        for i, ((l, f), (ra, rb, pv)) in enumerate(
                zip(pairs, expect_seq)):
            if (f.get("roots_a"), f.get("roots_b"),
                    f.get("proven")) != (ra, rb, pv):
                problems.append(
                    "candidate %d is roots_a=%r roots_b=%r proven=%r, but "
                    "the sequence assertion names roots_a=%r roots_b=%r "
                    "proven=%r"
                    % (i + 1, f.get("roots_a"), f.get("roots_b"),
                       f.get("proven"), ra, rb, pv))
        if len(pairs) > len(expect_seq):
            for l, f in pairs[len(expect_seq):]:
                problems.append("extra candidate record beyond the asserted "
                                "sequence: %s" % l)

    # ---- 4. candidate identities pairwise distinct ----
    ids = [(f.get("a"), f.get("b")) for _l, f in pairs]
    dupes = sorted({x for x in ids if ids.count(x) > 1})
    if dupes:
        problems.append("the candidate identities are not distinct: %r "
                        "appears more than once" % (dupes,))

    # ---- 6. first proven=1, with explicitly proven=0 predecessors ----
    first_proven = None
    before = 0
    bad_proven = []
    for l, f in pairs:
        pv = f.get("proven")
        if pv == "1":
            first_proven = (l, f)
            break
        if pv != "0":
            bad_proven.append((l, pv))
            continue
        before += 1
    if bad_proven:
        problems.append("candidate record(s) carry a `proven' value that is "
                        "neither 0 nor 1: %r" % (bad_proven,))
    if first_proven is None:
        sys.stderr.write("facts-deciding-pair: no proven=1 conflict_pair "
                         "record\n")
        return 1
    l, f = first_proven

    pair_ab = "%s,%s" % (f.get("a"), f.get("b"))
    if pair_ab != group_slots:
        problems.append("first proven pair a=%s b=%s does not equal the "
                        "group slots=%s" % (f.get("a"), f.get("b"),
                                            group_slots))
    if f.get("roots_a") != c.get("roots_a"):
        problems.append("first proven pair roots_a=%r != group roots_a=%r"
                        % (f.get("roots_a"), c.get("roots_a")))
    if f.get("roots_b") != c.get("roots_b"):
        problems.append("first proven pair roots_b=%r != group roots_b=%r"
                        % (f.get("roots_b"), c.get("roots_b")))
    if before < min_before:
        problems.append("only %d pair(s) precede the first proven one, but "
                        "the regression needs at least %d so that 'first "
                        "established candidate' is non-vacuous"
                        % (before, min_before))
    if expect_a is not None and root_list(c.get("roots_a")) != expect_a:
        problems.append("group roots_a=%r is not the expected list %r"
                        % (c.get("roots_a"), expect_a))
    if expect_b is not None and root_list(c.get("roots_b")) != expect_b:
        problems.append("group roots_b=%r is not the expected list %r "
                        "(an appended root would make the token list longer)"
                        % (c.get("roots_b"), expect_b))

    sys.stdout.write(
        "DECIDING group=%s slots=%s roots_a=%s roots_b=%s\n"
        % (c.get("range"), group_slots, c.get("roots_a"), c.get("roots_b")))
    sys.stdout.write(
        "DECIDING first_proven a=%s b=%s roots_a=%s roots_b=%s "
        "pairs_before=%d\n"
        % (f.get("a"), f.get("b"), f.get("roots_a"), f.get("roots_b"),
           before))
    sys.stdout.write("DECIDING candidate_count=%d distinct_identities=%d\n"
                     % (len(pairs), len(set(ids))))
    if expect_seq is not None:
        sys.stdout.write("DECIDING sequence_len_expected=%d\n"
                         % len(expect_seq))
    sys.stdout.write("DECIDING root_list_lens a=%d b=%d\n"
                     % (len(root_list(c.get("roots_a"))),
                        len(root_list(c.get("roots_b")))))
    if problems:
        for p in problems:
            sys.stderr.write("facts-deciding-pair: FAIL %s\n" % p)
        return 1
    sys.stdout.write("DECIDING ok=1\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
