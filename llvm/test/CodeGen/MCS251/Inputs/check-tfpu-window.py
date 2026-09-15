#!/usr/bin/env python3
"""Whole-window effect/shape assertion for the MCS251 TFPU pseudos
(G7 S3, design 2.4; round-3 revision).

Checked on `llc -stop-after=finalize-isel` MIR:

1. The command pseudo TFPU_<OP> owns the complete PSW[4:3]-selected R0-R7
   bank window for the whole busy period, so its implicit Defs must be
   EXACTLY {r0..r7} (not a subset: a dropped lane is an unprotected window
   byte; nothing outside the window: a byte the coprocessor does not own).
   Its implicit Uses are exactly {dr4} for the unary five and {dr4, dr0} for
   the binary four, and it has NO explicit operands at all -- neither a vreg
   nor a physical register. An explicit `$dr0`/`$dr4` operand would silently
   add a second definition site for a window byte outside the pseudo's own
   window model, which is exactly the class of escape this test exists to
   catch.

2. The window loads are their OWN pseudos and they are what writes the
   coprocessor's operand bytes:

     TFPU_LD_AR $ar  -> implicit-def $dr4   ; the AR half (r4-r7)
     TFPU_LD_BR $br  -> implicit-def $dr0   ; the BR half (r0-r3), binary only

   Each carries exactly one explicit vreg operand in the parking class
   gpr32win, which does NOT contain any of r0-r7 -- that is the whole point
   of the fix: the operand value cannot be allocated into the window, so the
   only writes into r0-r7 (or r0-r3) are these two pseudos, which the
   post-RA expand pass turns into movs INSIDE the TPIN window.

3. THE WINDOW IS ADJACENT AND SINGLE-BLOCK. The command pseudo must be
   IMMEDIATELY preceded by its load(s) and IMMEDIATELY followed by the
   readback, with nothing in between, all in the same basic block:
   the block of the load == the block of the command == the block of the
   readback, and the three are consecutive instructions. The previous
   revision only walked the TFPU lines themselves, so it accepted a mutated
   MIR with an unrelated side-effecting instruction wedged into the window,
   or with the load and the trigger in different blocks -- either of which
   breaks the design's "the whole load-trigger-wait-readback window is one
   indivisible unit" (§2.4). It also accepted an explicit `$dr0` operand on
   the command because only vreg operands were extracted as "extras".

4. The readback is the post-RA pseudo TFPU_RD_AR, whose def is an explicit
   vreg in a class that excludes dr4 (a def allocated to dr4 would expand to
   the no-op `mov dr4, dr4` and let the real read sink to the consumer,
   outside the window). It must immediately follow the command.

`--self-test` mutates real MIR lines and requires the same checker to reject
each: a dropped window def, an out-of-window def, the operand parked in dr4,
a removed window load, a dropped BR use, a missing BR load, an unrelated
instruction INSERTED BETWEEN the load and the command, a block boundary
inserted between them, and an explicit physical register operand on the
command.
"""

import argparse
import re
import sys

WINDOW = {"r%d" % i for i in range(8)}
AR_HALF = {"r4", "r5", "r6", "r7"}
BR_HALF = {"r0", "r1", "r2", "r3"}
UNARY = {"dr4"}
BINARY = {"dr4", "dr0"}

# The parking class. The bug this test guards against is the operand being
# allocated straight into the window (dr4/dr0), which folded the copy away and
# let the gather stay at the definition site. dr12 is the only member.
PARKING_CLASSES = {"gpr32win"}
# Readback destination classes: they must not contain dr4 (see module doc).
# gpr32win = {dr12} and gpr32rd = {dr0, dr12}; gpr32 (which holds dr4) is
# deliberately NOT accepted.
READBACK_CLASSES = {"gpr32win", "gpr32rd"}

OP_RE = re.compile(r"\bTFPU_(LD_AR|LD_BR|RD_AR|SQRT|SIN|COS|TAN|ATAN|ADD|SUB|MUL|DIV)\b")
CMD_OPS = {"sqrt", "sin", "cos", "tan", "atan", "add", "sub", "mul", "div"}
IMPLICIT_RE = re.compile(
    r"implicit-def\s+(?:dead\s+)?\$(\w+)|implicit\s+(?!-def)\$(\w+)"
)
REGDEF_RE = re.compile(
    r"^\s*-\s*\{\s*id:\s*(\d+),\s*class:\s*([\w:]+)"
)
BB_RE = re.compile(r"^\s*bb\.\d+")
NAME_RE = re.compile(r"^name:\s")
# An explicit operand token: a vreg (with optional class) or a physical reg.
OPERAND_RE = re.compile(r"%\w+(?::\w+)?|\$\w+")


def split_ops(line):
    """Split a TFPU MIR instruction line into (opcode, defs, uses, extras).

    `extras` is every EXPLICIT operand, vreg or physical: the implicit
    clauses are removed first, so what is left is what the instruction itself
    names. A stray `$dr0` therefore shows up here (the round-3 gap), instead
    of being invisible because only `%vreg` was matched.
    """
    m = OP_RE.search(line)
    if not m:
        return None
    opcode = m.group(0)
    defs, uses = set(), set()
    for tok in IMPLICIT_RE.finditer(line):
        if tok.group(1):
            defs.add(tok.group(1))
        else:
            uses.add(tok.group(2))
    stripped = IMPLICIT_RE.sub("", line)
    stripped = OP_RE.sub("", stripped)
    stripped = re.sub(r";.*$", "", stripped)
    stripped = re.sub(r"\b(killed|dead|undef)\b", "", stripped)
    extras = OPERAND_RE.findall(stripped)
    return opcode, defs, uses, extras


def vreg_classes(mir_text):
    """Map vreg number -> register class, per `name:` function block.

    llc -stop-after=finalize-isel -o - concatenates one MIR block per
    function and vreg ids restart in each, so the class map must be rebuilt
    at every `name:` line (a flat map would let a later function's %7 shadow
    the earlier one's).
    """
    per_fn = []
    cur = {}
    started = False
    for line in mir_text.splitlines():
        if NAME_RE.match(line):
            if started:
                per_fn.append(cur)
            cur = {}
            started = True
            continue
        m = REGDEF_RE.match(line)
        if m:
            cur["%" + m.group(1)] = m.group(2)
    if started:
        per_fn.append(cur)
    return per_fn


def index_maps(lines):
    """Per-line (function index, block index), counting `name:` / `bb.N`."""
    fn_of, bb_of = [], []
    fn = -1
    bb = -1
    for line in lines:
        if NAME_RE.match(line):
            fn += 1
            bb = -1
        elif BB_RE.match(line):
            bb += 1
        fn_of.append(fn)
        bb_of.append(bb)
    return fn_of, bb_of


def check(mir_text, binary_ops):
    class_maps = vreg_classes(mir_text)
    lines = mir_text.splitlines()
    fn_of, bb_of = index_maps(lines)

    # Every instruction line that mentions a TFPU pseudo, in file order.
    entries = []
    for idx, line in enumerate(lines):
        parsed = split_ops(line)
        if parsed:
            entries.append((idx, line, parsed))
    assert entries, "no TFPU pseudo found in the MIR"

    seen, n_cmd = set(), 0
    for pos, (idx, line, (opcode, defs, uses, extras)) in enumerate(entries):
        op = opcode.split("_", 1)[1].lower()

        if op in ("ld_ar", "ld_br"):
            want_def = "dr4" if op == "ld_ar" else "dr0"
            assert defs == {want_def}, (
                "line %d %s: implicit Defs %s != {%s} (the load writes exactly "
                "its window half)" % (idx + 1, opcode, sorted(defs), want_def)
            )
            assert not uses, (
                "line %d %s: unexpected implicit Uses %s"
                % (idx + 1, opcode, sorted(uses))
            )
            assert len(extras) == 1, (
                "line %d %s: expected exactly one parked operand, got %s"
                % (idx + 1, opcode, extras)
            )
            assert extras[0].startswith("%"), (
                "line %d %s: operand %s must be a vreg, not an explicit "
                "physical register" % (idx + 1, opcode, extras[0])
            )
            fi = fn_of[idx]
            assert 0 <= fi < len(class_maps), (
                "line %d %s: cannot locate the enclosing function block"
                % (idx + 1, opcode)
            )
            cls = class_maps[fi].get(extras[0].split(":")[0])
            assert cls in PARKING_CLASSES, (
                "line %d %s: operand %s (class %s) is not parked outside the "
                "R0-R7 window (class must be one of %s)"
                % (idx + 1, opcode, extras[0], cls, sorted(PARKING_CLASSES))
            )
            continue

        if op == "rd_ar":
            # The readback pseudo: explicit vreg def, implicit dr4 use, and
            # nothing else.
            assert not defs, (
                "line %d %s: the readback carries no implicit Defs, got %s"
                % (idx + 1, opcode, sorted(defs))
            )
            assert uses == UNARY, (
                "line %d %s: readback implicit Uses %s != {dr4}"
                % (idx + 1, opcode, sorted(uses))
            )
            assert len(extras) == 1 and extras[0].startswith("%"), (
                "line %d %s: expected exactly one vreg destination, got %s"
                % (idx + 1, opcode, extras)
            )
            cls = class_maps[fn_of[idx]].get(extras[0].split(":")[0])
            assert cls in READBACK_CLASSES, (
                "line %d %s: destination %s (class %s) is not a safe readback "
                "class -- a dest allocated to dr4 makes the read a no-op and "
                "lets it sink to the consumer (one of %s)"
                % (idx + 1, opcode, extras[0], cls, sorted(READBACK_CLASSES))
            )
            continue

        # A TFPU_<OP> command pseudo.
        assert op in CMD_OPS, "line %d: unexpected TFPU pseudo %s" % (idx + 1, opcode)
        n_cmd += 1
        seen.add(op)
        assert defs == WINDOW, (
            "line %d %s: implicit Defs %s != the complete R0-R7 window %s"
            % (idx + 1, opcode, sorted(defs), sorted(WINDOW))
        )
        want_uses = BINARY if op in binary_ops else UNARY
        assert uses == want_uses, (
            "line %d %s: implicit Uses %s != %s"
            % (idx + 1, opcode, sorted(uses), sorted(want_uses))
        )
        assert not extras, (
            "line %d %s: unexpected explicit operands %s (the operands ride "
            "the TFPU_LD_AR/TFPU_LD_BR window loads; an explicit physical "
            "register here adds a window write outside the window model)"
            % (idx + 1, opcode, extras)
        )

        # The window must be one adjacent, single-block unit. Walk the real
        # preceding instruction lines: exactly the load(s), in order, with the
        # same block and function as the command, and nothing else in between.
        want_loads = ["ld_ar", "ld_br"] if op in binary_ops else ["ld_ar"]
        got_loads = []
        cursor = pos - 1
        while cursor >= 0 and len(got_loads) < len(want_loads):
            pidx, pline, (pcode, _, _, _) = entries[cursor]
            pop = pcode.split("_", 1)[1].lower()
            if pop not in ("ld_ar", "ld_br"):
                break
            assert pidx == idx - (len(got_loads) + 1), (
                "line %d %s: window load %s is not adjacent (line %d intervenes)"
                % (idx + 1, opcode, pcode, idx)
            )
            assert fn_of[pidx] == fn_of[idx] and bb_of[pidx] == bb_of[idx], (
                "line %d %s: window load %s is in a different block/function"
                % (idx + 1, opcode, pcode)
            )
            got_loads.insert(0, pop)
            cursor -= 1
        assert got_loads == want_loads, (
            "line %d %s: preceding window loads %s != %s -- every operand must "
            "be materialised by an adjacent window load pseudo in the same "
            "block" % (idx + 1, opcode, got_loads, want_loads)
        )
        # Nothing may sit between the last load and the command.
        first_load_idx = idx - len(want_loads)
        for mid in range(first_load_idx, idx):
            assert fn_of[mid] == fn_of[idx] and bb_of[mid] == bb_of[idx], (
                "line %d %s: instruction at line %d splits the window"
                % (idx + 1, opcode, mid + 1)
            )
            assert split_ops(lines[mid]) is not None, (
                "line %d %s: unrelated instruction %r sits inside the window"
                % (idx + 1, opcode, lines[mid].strip())
            )

        # The readback must follow immediately: `%r = TFPU_RD_AR implicit $dr4`
        nxt = idx + 1
        assert nxt < len(lines) and fn_of[nxt] == fn_of[idx] and bb_of[nxt] == bb_of[idx], (
            "line %d %s: no readback in the same block right after the command"
            % (idx + 1, opcode)
        )
        nparsed = split_ops(lines[nxt])
        assert nparsed and nparsed[0] == "TFPU_RD_AR", (
            "line %d %s: the next instruction is not the TFPU_RD_AR readback: "
            "%r" % (idx + 1, opcode, lines[nxt].strip())
        )
        rdefs, ruses, rextras = nparsed[1], nparsed[2], nparsed[3]
        rcls = class_maps[fn_of[nxt]].get(rextras[0].split(":")[0]) if rextras else None
        assert ruses == UNARY and rdefs == set(), (
            "line %d %s: readback effect model %s/%s != {dr4}/{}"
            % (nxt + 1, "TFPU_RD_AR", sorted(ruses), sorted(rdefs))
        )
        assert rcls in READBACK_CLASSES, (
            "line %d TFPU_RD_AR: destination class %s is not a safe readback "
            "class (one of %s)" % (nxt + 1, rcls, sorted(READBACK_CLASSES))
        )

    assert seen == set(binary_ops) | {"sin", "cos", "tan", "atan", "sqrt"}, (
        "pseudo set %s does not cover the nine commands" % sorted(seen)
    )
    return n_cmd


def self_test(mir_text, binary_ops):
    check(mir_text, binary_ops)  # the checker accepts the real MIR

    lines = mir_text.splitlines()
    cmd_idx = next(
        i for i, l in enumerate(lines) if re.search(r"\bTFPU_(SIN|COS|TAN)", l)
    )
    ar_idx = next(i for i, l in enumerate(lines) if "TFPU_LD_AR" in l)
    br_idx = next((i for i, l in enumerate(lines) if "TFPU_LD_BR" in l), None)
    bincmd_idx = next(
        (i for i, l in enumerate(lines) if re.search(r"\bTFPU_(ADD|SUB|MUL|DIV)\b", l)),
        None,
    )
    rd_idx = next(i for i, l in enumerate(lines) if "TFPU_RD_AR" in l)

    def replace(idx, old, new):
        assert old in lines[idx], (idx, old, lines[idx])
        return lines[idx].replace(old, new, 1)

    mutations = {
        # A dropped lane: one window byte would be unprotected across the busy
        # period, so a live value could sit inside it.
        "dropped-def": (
            cmd_idx,
            lambda: replace(cmd_idx, "implicit-def dead $r3, ", "")
            if "implicit-def dead $r3, " in lines[cmd_idx]
            else replace(cmd_idx, "implicit-def $r3, ", ""),
        ),
        # An out-of-window def: claims a byte the coprocessor does not own.
        "extra-def": (
            cmd_idx,
            lambda: replace(
                cmd_idx, "implicit $dr4", "implicit-def $r8, implicit $dr4"
            ),
        ),
        # An explicit PHYSICAL register operand on the command: the round-3
        # gap (only vreg operands were extracted as extras). This is the
        # shape that writes a window byte without the pseudo's window model.
        "explicit-physreg-operand": (
            cmd_idx,
            lambda: replace(
                cmd_idx, "implicit $dr4", "$dr0, implicit $dr4"
            ),
        ),
        # A window load deleted: the operand would never reach r4-r7.
        "missing-ar-load": (ar_idx, None),
    }
    if br_idx is not None:
        # The BR use lives on a BINARY command, not on the unary one that
        # cmd_idx picked.
        mutations["dropped-br-use"] = (
            bincmd_idx,
            lambda: replace(bincmd_idx, ", implicit $dr0", ""),
        )
        mutations["missing-br-load"] = (br_idx, None)

    for name, (idx, fn) in mutations.items():
        bad = list(lines)
        if fn is None:
            del bad[idx]
        else:
            bad[idx] = fn()
        try:
            check("\n".join(bad), binary_ops)
        except AssertionError:
            continue
        except IndexError:
            continue
        raise AssertionError("self-test: %s mutation was NOT rejected" % name)

    # The operand retyped into the window needs its vreg class changed in the
    # `registers:` section, not on the instruction line.
    vreg = re.search(r"%\d+", lines[ar_idx]).group(0)
    fi = index_maps(lines)[0][ar_idx]
    bad = list(lines)
    lo, n, hit = 0, -1, False
    for i, l in enumerate(bad):
        if NAME_RE.match(l):
            n += 1
            if n == fi:
                lo = i
        if i < lo:
            continue
        if re.match(r"^\s*-\s*\{\s*id:\s*%s," % vreg[1:], l) and \
                "class: gpr32win" in l:
            bad[i] = l.replace("class: gpr32win", "class: gpr32")
            hit = True
    assert hit, "self-test operand-in-window found no class to retype"
    try:
        check("\n".join(bad), binary_ops)
    except AssertionError:
        pass
    else:
        raise AssertionError("self-test: operand-in-window mutation was NOT rejected")

    # A readback destination retyped to the window (dr4-containing) class must
    # be rejected too.
    rd_vreg = re.search(r"%\d+", lines[rd_idx]).group(0)
    fi = index_maps(lines)[0][rd_idx]
    bad = list(lines)
    lo, n, hit = 0, -1, False
    for i, l in enumerate(bad):
        if NAME_RE.match(l):
            n += 1
            if n == fi:
                lo = i
        if i < lo:
            continue
        if re.match(r"^\s*-\s*\{\s*id:\s*%s," % rd_vreg[1:], l) and \
                "class: gpr32rd" in l:
            bad[i] = l.replace("class: gpr32rd", "class: gpr32")
            hit = True
    assert hit, "self-test readback-in-window found no class to retype"
    try:
        check("\n".join(bad), binary_ops)
    except AssertionError:
        pass
    else:
        raise AssertionError("self-test: readback-in-window was NOT rejected")

    # A readback deleted entirely: the result would never leave the window.
    bad = list(lines)
    del bad[rd_idx]
    try:
        check("\n".join(bad), binary_ops)
    except AssertionError:
        pass
    except IndexError:
        pass
    else:
        raise AssertionError("self-test: missing-readback was NOT rejected")

    # An unrelated, side-effecting instruction INSERTED BETWEEN the load and
    # the command: the window is no longer one indivisible unit.
    insert_at = cmd_idx
    bad = list(lines)
    bad.insert(insert_at, "    MOV8ri $dr0, 0")
    try:
        check("\n".join(bad), binary_ops)
    except AssertionError:
        pass
    else:
        raise AssertionError(
            "self-test: instruction inserted into the window was NOT rejected"
        )

    # A BLOCK BOUNDARY between the load and the command: the window must
    # never span blocks (design §2.4 hard constraint).
    bad = list(lines)
    bad.insert(cmd_idx, "  bb.1.split:")
    try:
        check("\n".join(bad), binary_ops)
    except AssertionError:
        pass
    else:
        raise AssertionError(
            "self-test: block boundary inside the window was NOT rejected"
        )

    print(
        "self-test: dropped/extra def, explicit physreg operand, "
        "in-window operand, in-window readback, missing readback, "
        "inserted instruction, block split and missing window loads "
        "all rejected"
    )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("mir")
    ap.add_argument("--binary", default="add,sub,mul,div",
                    help="comma-separated binary opcodes")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    binary_ops = {o.strip() for o in args.binary.split(",") if o.strip()}
    text = open(args.mir).read()
    n = check(text, binary_ops)
    print("window ok: %d TFPU command pseudo(s), every one clobbers exactly "
          "R0-R7, is preceded by its adjacent in-block parked-operand window "
          "load(s) and followed immediately by the TFPU_RD_AR readback" % n)
    if args.self_test:
        self_test(text, binary_ops)
    return 0


if __name__ == "__main__":
    sys.exit(main())
