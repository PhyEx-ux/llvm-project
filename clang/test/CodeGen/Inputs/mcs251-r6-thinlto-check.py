#!/usr/bin/env python3
"""WP4 round 6: the ThinLTO importing entry must reach the SAME two-phase
verdict as the ordinary code-generation entry, with the ARITHMETIC phase
after the importing/optimization pipeline and before code generation.

Round 5 ran the arithmetic verdict before runThinLTOBackend -- before any
optimization -- which rejected modules the ordinary entry accepts once
folding and dead-code elimination have run (`mul i64 %x, 0` followed by a
truncation; an i64 product reachable only through a constant-false select
arm). Round 6 keeps the structural phase before the pipeline and moves the
arithmetic phase into the PreCodeGenModuleHook, reported through the
ordinary DiagnosticsEngine exit.

This helper pins, per entry (ordinary vs ThinLTO) at O0 and O2 in IR and
object modes:
  * zero_mul / constant_select: rc=1 at O0 (the local interpreter of the
    arithmetic check does not fold these shapes), rc=0 at O2 with a real
    artifact (.ll text / strict ELF object carrying the defined symbol);
  * live i64 arithmetic (an i64 argument keeps the mul from narrowing):
    rc=1 on both entries in every cell, ordinary diagnostics, no crash text;
  * a dead atomic load: rc=1 on both entries (the structural phase still
    runs before optimization, so a later layer cannot resurrect it);
  * the empty-module exemption controls of the round-6 narrowing:
      - module-asm-only and FullDebug-CU-only modules are CHECKED on both
        entries and rejected with the ordinary diagnostic (round 5 exempted
        them on the ThinLTO side);
      - a truly empty module (no functions, globals, aliases, ifuncs, named
        metadata, module asm) keeps the legal IR pass-through (rc=0) on both
        entries; its object cells exit 70 on both entries with the
        pre-existing `!mcs251.signatures` requirement of the object writer,
        which is not this exemption's verdict and is recorded as such;
      - the upstream-fabricated empty module (bitcode without a ThinLTO
        summary): the ThinLTO entry accepts the IR (rc=0); no legal object
        path is claimed for it.

Usage: mcs251-r6-thinlto-check.py --clang-cc1 <cc1 argv...>
"""
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

CRASH_TEXT = ("PLEASE submit", "Stack dump", "PLEASE ATTACH", "crash backtrace")

# name -> (body, {cell: expected rc}) with cells "O0.ir" / "O0.obj" / "O2.ir"
# / "O2.obj"; expected entries are per BOTH entries, which must always agree.
MODULES = {
    "zero_mul": ("""
define dso_local i32 @f(i32 noundef %a) addrspace(4) #0 {
entry:
  %x = zext i32 %a to i64
  %m = mul i64 %x, 0
  %t = trunc i64 %m to i32
  ret i32 %t
}

attributes #0 = { nounwind "frame-pointer"="all" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
""", {"O0.ir": 1, "O0.obj": 1, "O2.ir": 0, "O2.obj": 0}),
    "constant_select": ("""
define dso_local i32 @f(i32 noundef %a) addrspace(4) #0 {
entry:
  %x = zext i32 %a to i64
  %m = mul i64 %x, 3
  %t = trunc i64 %m to i32
  %s = select i1 false, i32 %t, i32 0
  ret i32 %s
}

attributes #0 = { nounwind "frame-pointer"="all" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
""", {"O0.ir": 1, "O0.obj": 1, "O2.ir": 0, "O2.obj": 0}),
    "live_i64arg": ("""
define dso_local i32 @f(i64 noundef %x) addrspace(4) #0 {
entry:
  %m = mul i64 %x, %x
  %t = trunc i64 %m to i32
  ret i32 %t
}

attributes #0 = { nounwind "frame-pointer"="all" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
""", {"O0.ir": 1, "O0.obj": 1, "O2.ir": 1, "O2.obj": 1}),
    "dead_atomic_load": ("""
define i32 @f() {
entry:
  %p = alloca i32, align 4
  store i32 0, ptr %p, align 4
  %v = load atomic i32, ptr %p seq_cst, align 4
  ret i32 7
}
""", {"O0.ir": 1, "O0.obj": 1, "O2.ir": 1, "O2.obj": 1}),
    "mod_asm_only": ("""
module asm "nop"
""", {"O0.ir": 1, "O0.obj": 1, "O2.ir": 1, "O2.obj": 1}),
    "debug_cu_only": ("""
!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!1, !2}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !3, producer: "ctl", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, enums: !{})
!1 = !{i32 2, !"Dwarf Version", i32 4}
!2 = !{i32 2, !"Debug Info Version", i32 3}
!3 = !DIFile(filename: "ctl.c", directory: "/tmp")
""", {"O0.ir": 1, "O0.obj": 1, "O2.ir": 1, "O2.obj": 1}),
    # Truly empty: the legal "nothing to judge" invocation. The object cells
    # exit 70 on BOTH entries with the object writer's pre-existing
    # `!mcs251.signatures` requirement -- identical to the ordinary entry, not
    # a property of the exemption.
    "truly_empty": ("""
""", {"O0.ir": 0, "O0.obj": 70, "O2.ir": 0, "O2.obj": 70}),
}

# The fabricated-empty control runs through the ThinLTO entry only (the
# ordinary entry has no index to consume); no legal object path is claimed.
FABRICATED_EMPTY_EXPECT = {"O0.ir": 0, "O0.obj": 70}

EXPORTED = {
    "zero_mul": "_f", "constant_select": "_f", "live_i64arg": "_f",
    "dead_atomic_load": "_f", "mod_asm_only": "", "debug_cu_only": "",
    "truly_empty": "",
}

failures = []


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


# WP4 round 7 (T1): "a file appeared" is not an acceptance. The artifact checks
# below require the accepted artifact to be PARSABLE and to carry the content
# the cell is about, because the round-6 helper only called exists() on the
# accepted IR and its elf_ok() stopped at ELF32 big-endian plus the defined
# symbol -- an accepted .ll replaced by a 0-byte file, and an accepted object
# whose e_machine was changed to 3 (EM_386), both still passed. Every shape
# below is also checked as a persistent negative control at the end of this
# helper, so the strengthened assertions are exercised against the three
# fabricated artifacts rather than asserted only by inspection.
EM_MCS251 = 0x9999
SHT_SYMTAB = 2


def elf_identity(path):
    """(error, blob) with the ELF identity fields the MCS251 writer emits:
    magic, ELFCLASS32, big-endian, and e_machine == EM_MCS251. A relocation
    object of another architecture is not an MCS251 artifact, however
    well-formed it is."""
    blob = Path(path).read_bytes()
    if blob[:4] != b"\x7fELF":
        return f"not an ELF object (magic {blob[:4].hex()})", blob
    if len(blob) <= 64:
        return f"too small ({len(blob)} bytes)", blob
    if blob[4] != 1 or blob[5] != 2:
        return f"not ELF32 big-endian (class {blob[4]}, data {blob[5]})", blob
    e_machine = struct.unpack(">H", blob[18:20])[0]
    if e_machine != EM_MCS251:
        return f"e_machine is {e_machine:#x}, not EM_MCS251 ({EM_MCS251:#x})", blob
    return None, blob


def ir_ok(path, want_symbol):
    """The accepted IR artifact must be a NON-EMPTY module text that names the
    function the module defines and its triple -- not merely an existing
    file. \p want_symbol is "" for the modules that define no function."""
    text = Path(path).read_text()
    if not text.strip():
        return "accepted IR artifact is empty"
    if 'target triple = "mcs251-unknown-none"' not in text:
        return "accepted IR artifact has no MCS251 target triple"
    if want_symbol and f"@{want_symbol}" not in text:
        return f"accepted IR artifact does not mention the defined @{want_symbol}"
    return None


def elf_ok(path, want_symbol):
    ok, blob = elf_identity(path)
    if ok:
        return ok
    shoff = struct.unpack(">I", blob[32:36])[0]
    shentsize, shnum, shstrndx = struct.unpack(">HHH", blob[46:52])
    raw = [struct.unpack(">IIIIIIIIII",
                         blob[shoff + i * shentsize:
                              shoff + i * shentsize + 40])
           for i in range(shnum)]
    strtab_off = raw[shstrndx][4]
    for sec in raw:
        nameoff, typ, _f, _a, off, size, link, _i, _al, entsize = sec
        if typ != SHT_SYMTAB or entsize == 0:
            continue
        st = raw[link][4]
        for j in range(size // entsize):
            so = off + j * entsize
            _n, _v, _s, _inf, _o, shndx = struct.unpack(">IIIBBH",
                                                        blob[so:so + 16])
            start = st + _n
            name = blob[start:blob.index(b"\0", start)]
            if shndx != 0 and name.decode("ascii", "replace") == want_symbol:
                return None
    return f"defined symbol {want_symbol!r} not found"


# The three fabricated artifacts Alice used to measure the round-6 gap, as
# persistent negative controls: each must be REJECTED by the assertions the
# accepted cells use. They are built from real accepted artifacts (whatever
# the run produced), so a change that weakens the assertions fails here.
def negative_controls(tmp, samples):
    """samples: list of (label, ir_path or None, obj_path or None)."""
    problems = []
    for label, ir_path, obj_path in samples:
        if ir_path is not None and Path(ir_path).exists():
            empty = Path(str(ir_path) + ".empty")
            empty.write_bytes(b"")
            # Empty input: the symbol argument is irrelevant, the emptiness
            # check must fire first.
            got = ir_ok(empty, "")
            if not got:
                problems.append(f"{label}: a 0-byte IR artifact was accepted")
        if obj_path is not None and Path(obj_path).exists():
            text = Path(str(obj_path) + ".notelf")
            text.write_text("not ELF")
            # elf_identity's contract is (error, blob): a NON-EMPTY tuple is
            # always truthy, so `if not elf_identity(...)` can never fire -- the
            # round-6 control was measured to be inert even when the function
            # returned (None, blob), i.e. accepted a fabricated object. Judge
            # the ERROR component explicitly.
            err, _blob = elf_identity(text)
            if err is None:
                problems.append(f"{label}: a 'not ELF' object was accepted")
            blob = bytearray(Path(obj_path).read_bytes())
            blob[18:20] = (3).to_bytes(2, "big")  # e_machine = EM_386
            wrong = Path(str(obj_path) + ".emachine3")
            wrong.write_bytes(blob)
            err, _blob = elf_identity(wrong)
            if err is None:
                problems.append(
                    f"{label}: an object with e_machine=3 was accepted")
    return problems


def main():
    argv = sys.argv[1:]
    if not argv:
        sys.exit("usage: mcs251-r6-thinlto-check.py <cc1 argv...>")
    cc1 = argv
    bindir = Path(cc1[0]).resolve().parent

    def sibling(name):
        cand = bindir / name
        if not cand.exists():
            sys.exit(f"FAIL: could not find {name} next to {cc1[0]}")
        return str(cand)

    llvm_as, opt, llvm_lto2 = (sibling(n) for n in
                                ("llvm-as", "opt", "llvm-lto2"))
    # Accepted artifacts collected during the matrix, replayed at the end as
    # the negative controls for the artifact checks (a truncated IR file, a
    # non-ELF file, an object whose e_machine was changed).
    ir_samples = []
    obj_samples = []
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        probe = tmp / "probe.c"
        probe.write_text("int mcs251_layout_probe;\n")
        r = run(cc1 + ["-triple", "mcs251-unknown-none", "-std=gnu11",
                       "-O0", "-emit-llvm", str(probe),
                       "-o", str(tmp / "probe.ll")])
        if r.returncode != 0:
            sys.exit(f"FAIL: layout probe failed: {r.stderr[:300]}")
        layout = None
        for line in (tmp / "probe.ll").read_text().splitlines():
            if line.startswith('target datalayout = "'):
                layout = line.split('"')[1]
                break
        if layout is None:
            sys.exit("FAIL: no target datalayout in the probe output")
        header = (f'target datalayout = "{layout}"\n'
                  f'target triple = "mcs251-unknown-none"\n')
        # Modules that define @f carry the legal `!mcs251.signatures` record
        # for it (a definition, i32 return, one i32 parameter), so the
        # accepted object cells are judged on a module a real build could
        # produce; the empty-module controls carry no signature.
        footer = ("\n!llvm.module.flags = !{!0, !1}\n"
                  "!mcs251.signatures = !{!2}\n"
                  '\n!0 = !{i32 1, !"wchar_size", i32 2}\n'
                  '!1 = !{i32 7, !"frame-pointer", i32 2}\n'
                  '!2 = !{!"_f", i32 1, i32 0, i32 0}\n')

        def compile_module(name, body, use_index, opt_level, mode, out,
                           input_bc=None):
            """Returns (rc, artifact-exists, stderr)."""
            src = tmp / f"{name}.ll"
            if body is not None:
                extra = footer if EXPORTED.get(name) else ""
                src.write_text(header + body + extra)
            bc = input_bc if input_bc is not None else tmp / f"{name}.bc"
            if body is not None:
                r = run([llvm_as, str(src), "-o", str(bc)])
                if r.returncode != 0:
                    failures.append(f"{name}: llvm-as failed: {r.stderr[:150]}")
                    return None
            cmd = cc1 + ["-triple", "mcs251-unknown-none",
                         f"-{opt_level}",
                         "-emit-llvm" if mode == "ir" else "-emit-obj"]
            cc1_input = bc
            if mode == "obj":
                cmd += ["-mllvm", "-mcs251-object-format=elf"]
            if use_index:
                summary = tmp / f"{name}.summary.bc"
                r = run([opt, "-module-summary", str(bc), "-o", str(summary)])
                if r.returncode != 0:
                    failures.append(f"{name}: opt -module-summary failed")
                    return None
                lto2 = [llvm_lto2, "run", "--thinlto-distributed-indexes",
                        "-o", str(tmp / f"{name}.lto-out"),
                        str(summary)]
                extsym = EXPORTED.get(name, "")
                if extsym:
                    lto2.insert(-1, f"-r={summary},{extsym},plx")
                r = run(lto2)
                index = Path(str(summary) + ".thinlto.bc")
                if not index.exists():
                    failures.append(
                        f"{name}: llvm-lto2 produced no index: {r.stderr[:150]}")
                    return None
                cmd += [f"-fthinlto-index={index}"]
                cc1_input = summary
            out = Path(out)
            out.unlink(missing_ok=True)
            cmd += [str(cc1_input), "-o", str(out)]
            r = run(cmd)
            return r.returncode, out.exists(), r.stderr

        for name, (body, expect) in MODULES.items():
            for opt_level in ("O0", "O2"):
                for mode in ("ir", "obj"):
                    cell = f"{opt_level}.{mode}"
                    want = expect[cell]
                    out_std = tmp / f"{name}.std.{cell}.out"
                    out_lto = tmp / f"{name}.lto.{cell}.out"
                    std = compile_module(name, body, False, opt_level, mode,
                                         out_std)
                    thin = compile_module(name, body, True, opt_level, mode,
                                          out_lto)
                    if std is None or thin is None:
                        continue
                    std_rc, std_art, std_err = std
                    thin_rc, thin_art, thin_err = thin
                    label = f"{name}.{cell}"
                    if std_rc != want:
                        failures.append(f"{label}: ordinary entry expected "
                                        f"{want}, got {std_rc}: "
                                        f"{std_err[:150]}")
                    if thin_rc != want:
                        failures.append(f"{label}: ThinLTO entry expected "
                                        f"{want}, got {thin_rc}: "
                                        f"{thin_err[:150]}")
                    if std_rc != thin_rc:
                        failures.append(f"{label}: entries disagree: "
                                        f"ordinary {std_rc}, ThinLTO {thin_rc}")
                    if want == 0 and not (std_art and thin_art):
                        failures.append(f"{label}: accepted compile left no "
                                        f"artifact (std {std_art}, lto "
                                        f"{thin_art})")
                    if want == 1:
                        if std_art or thin_art:
                            failures.append(f"{label}: rejected compile left "
                                            f"an artifact")
                        for entry, err in (("ordinary", std_err),
                                           ("ThinLTO", thin_err)):
                            if "error:" not in err:
                                failures.append(
                                    f"{label}: {entry} entry has no ordinary "
                                    f"error diagnostic: {err[:150]}")
                            for needle in CRASH_TEXT:
                                if needle in err:
                                    failures.append(
                                        f"{label}: {entry} entry printed "
                                        f"crash text {needle!r}")
                    if want == 0:
                        # An accepted artifact must be parsable and carry the
                        # content the cell is about, not merely exist: a 0-byte
                        # .ll and a `not ELF`/wrong-e_machine object both
                        # passed a bare exists() check (round 6, measured).
                        for entry, path in (("ordinary", out_std),
                                            ("ThinLTO", out_lto)):
                            if not Path(path).exists():
                                continue
                            if mode == "ir":
                                # IR names carry no object-symbol underscore.
                                bad = ir_ok(path,
                                            EXPORTED.get(name, "").lstrip("_"))
                            else:
                                bad = elf_ok(path, EXPORTED.get(name, ""))
                            if bad:
                                failures.append(
                                    f"{label}: {entry} artifact: {bad}")
                    # Collect real accepted artifacts for the negative
                    # controls exercised at the end of this helper. Only the
                    # cells whose artifact checks actually ran are replayed,
                    # so a control never depends on a file the checks skipped.
                    if want == 0:
                        if mode == "ir" and ir_samples is not None \
                                and Path(out_std).exists():
                            ir_samples.append(
                                (f"{label}.ordinary", out_std, None))
                        if mode == "obj" and obj_samples is not None \
                                and Path(out_lto).exists():
                            obj_samples.append(
                                (f"{label}.ThinLTO", None, out_lto))

        # The fabricated-empty control: bitcode WITHOUT a ThinLTO module
        # summary. CodeGenAction::loadModule fabricates a bare empty module
        # (triple only), which must stay exempt -- measured exit 0 for IR,
        # 70 (the object writer's signatures requirement) for objects. No
        # legal object path is claimed for this input.
        empty_src = tmp / "fab_empty.ll"
        empty_src.write_text(header)
        fab_bc = tmp / "fab_empty.bc"
        r = run([llvm_as, str(empty_src), "-o", str(fab_bc)])
        if r.returncode != 0:
            failures.append("fab_empty: llvm-as failed")
        else:
            # Index from a DIFFERENT (summary-carrying) empty module so the
            # dispatch is entered at all; the input bitcode itself has no
            # summary, which is what triggers the fabrication.
            sum_src = tmp / "fab_empty_sum.ll"
            sum_src.write_text(header)
            sum_bc = tmp / "fab_empty_sum.bc"
            run([llvm_as, str(sum_src), "-o", str(sum_bc)])
            run([opt, "-module-summary", str(sum_bc), "-o",
                 str(tmp / "fab_empty_sum.summary.bc")])
            run([llvm_lto2, "run", "--thinlto-distributed-indexes",
                 "-o", str(tmp / "fab_empty.lto-out"),
                 str(tmp / "fab_empty_sum.summary.bc")])
            index = tmp / "fab_empty_sum.summary.bc.thinlto.bc"
            if not index.exists():
                failures.append("fab_empty: no index for the control")
            else:
                for opt_level in ("O0",):
                    for mode, want in (("ir", FABRICATED_EMPTY_EXPECT["O0.ir"]),
                                       ("obj", FABRICATED_EMPTY_EXPECT["O0.obj"])):
                        out = tmp / f"fab_empty.{opt_level}.{mode}.out"
                        out.unlink(missing_ok=True)
                        cmd = cc1 + ["-triple", "mcs251-unknown-none",
                                     f"-{opt_level}",
                                     "-emit-llvm" if mode == "ir"
                                     else "-emit-obj"]
                        if mode == "obj":
                            cmd += ["-mllvm", "-mcs251-object-format=elf"]
                        cmd += [f"-fthinlto-index={index}", str(fab_bc),
                                "-o", str(out)]
                        r = run(cmd)
                        if r.returncode != want:
                            failures.append(
                                f"fab_empty.{opt_level}.{mode}: expected "
                                f"{want}, got {r.returncode}: "
                                f"{r.stderr[:150]}")

        # WP4 round 7 (T1): replay the three fabricated artifacts (round 6
        # measured that a bare exists() accepted all three) against the same
        # assertions the accepted cells now use. s
        samples = ir_samples + obj_samples
        if not samples:
            failures.append("artifact negative controls: no accepted "
                            "artifact was collected to build them from")
        else:
            for line in negative_controls(tmp, samples):
                failures.append(line)

    if failures:
        print("FAIL")
        for line in failures:
            print("  " + line)
        return 1
    print(f"ThinLTO two-phase verdict agrees with the ordinary entry on "
          f"{len(MODULES)} modules x O0/O2 x IR/obj; empty-module controls "
          f"pinned; fabricated-empty control recorded")
    return 0


if __name__ == "__main__":
    sys.exit(main())
