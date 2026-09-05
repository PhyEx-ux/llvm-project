#!/usr/bin/env python3
"""run-tests.py -- unified runner for the demo x QEMU test system (DESIGN.md section 3).

Layers
------
  t1  algorithm-kernel regression: per-case directory t1/<case>/ with
        kernel.c    the kernel under test (same source feeds all three sides)
        wrapper.c   firmware driver, SDCC --c1mode, shared by Oracle-B and DUT
        host-main.c Oracle-A host driver (gcc).  ABSENT => smoke case:
                    compile+terminate only, no behavioural oracle
        vectors.h / gen-vectors.py   optional stimulus shared by the drivers
  t2  low-level SFR/GPIO/UART cases (t2/<case>/, same layout; reserved)
  t4  QEMU behaviour probes (t4-probes/, layout owned by its maintainer; reserved)

Chains per full t1/t2 case
--------------------------
  Oracle-A (logic truth)    gcc -std=c99 -O0 host-main.c, run, capture stdout
  Oracle-B (target truth)   kernel.c -> cpp -> sdcc --c1mode -> sdas251 -> .rel
                            wrapper.c likewise; strict sdld -r -nf link; QEMU
  DUT (chain under test)    kernel.c -> clang -emit-llvm (host target) -> .ll
                            -> llc -mtriple=mcs251-unknown-none -filetype=obj
                            -> mcs251_ld.py --mcs251-abi production link; QEMU
  verdict                   serial(A) == serial(B) == serial(DUT) char-by-char
                            AND DUT stream contains PASS and no FAIL

Frozen toolchain (DESIGN.md section 3 discipline): llc/QEMU are run from
/home/liu/mcs251-demo-test/bin-frozen to keep the baseline stable while other
engineers rebuild the toolchain.

QEMU note: the harness ends in an infinite loop by design, so timeout's
rc=124 is EXPECTED; the verdict comes from the serial transcript only.

Exit code: 0 iff every executed case passes (a DUT skipped for a missing
clang does not fail the run; it is reported separately).
"""

import argparse
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))          # .../mcs251-demo-test
WORK = "/home/liu/mcs251-demo-test"                        # persistent build area
BUILD = os.path.join(WORK, "build")

FW = "/mnt/c/Prj/LLVM/MCS251/validation/mcs251-firmware"
MLD = "/mnt/c/Prj/LLVM/MCS251/validation/mcs251-ld/mcs251_ld.py"

SDCCBIN = "/home/liu/build-sdcc/bin"
SDCC = os.path.join(SDCCBIN, "sdcc")
SDAS = os.path.join(SDCCBIN, "sdas251")
SDLD = os.path.join(SDCCBIN, "sdld")

FROZEN = os.path.join(WORK, "bin-frozen")
LLC = os.path.join(FROZEN, "llc")
QEMU = os.path.join(FROZEN, "qemu-system-mcs251")

GCC = "gcc"
# System clang 19.1.7 (Debian package; PM decision 2026-09-05: forward version
# gap clang19-IR -> llc24 is the well-trodden auto-upgrade path; the self-build
# bootstrap stays available as a fallback if upgrade friction ever appears).
# TRANSITIONAL: once the fork-clang front end natively emits SDCC-style '_'
# symbols, switch CLANG to it and retire the symbol shim (see --no-ir-shims).
CLANG = "/usr/bin/clang"
QEMU_TIMEOUT = 30
ORACLE_A_TIMEOUT = 60

# IR compatibility shims (PM ruling 2026-09-05 "retirable shims"):
#   1. symbol adaptation  @name -> @_name   (mangle_ll_symbols)
#   2. constant-shift lowering              (lower_constant_shifts)
# FLIPPED OFF by default 2026-09-06: both retirement conditions hold for the
# current llc (post-867232bec shift ISel, post-9d74d7ede m:s '_' prefixing)
# and shims are actively harmful there (double '__name' prefix -> link
# failure).  Re-acceptance: 19/22 three-way PASS shimless, 3 SKIP known
# i32-mul/div limitation (ACCEPTANCE.md).  --ir-shims remains for old-llc
# baseline comparisons only.
IR_SHIMS = False


def sh(cmd, stdin=None, stdout_path=None, cwd=None):
    """Run a build step; return (rc, output-text)."""
    out = open(stdout_path, "wb") if stdout_path else subprocess.PIPE
    try:
        p = subprocess.run(cmd, stdin=stdin, stdout=out,
                           stderr=subprocess.STDOUT, cwd=cwd)
        text = "" if stdout_path else (p.stdout.decode("utf-8", "replace") if p.stdout else "")
        return p.returncode, text
    finally:
        if stdout_path:
            out.close()


def log(msg):
    print("[run-tests] %s" % msg)
    sys.stdout.flush()


# ---------------------------------------------------------------------------
# shared firmware objects (crt0 / provider), built once per runner invocation
# ---------------------------------------------------------------------------

def ensure_firmware_cache():
    fwcache = os.path.join(BUILD, "firmware")
    os.makedirs(fwcache, exist_ok=True)
    for stem in ("crt0", "provider"):
        rel = os.path.join(fwcache, stem + ".rel")
        if os.path.exists(rel):
            continue
        rc, out = sh([SDAS, "-plosgffw", "-o", rel,
                      os.path.join(FW, stem + ".asm")],
                     stdout_path=os.path.join(fwcache, stem + ".sdas.log"))
        if rc != 0:
            raise RuntimeError("sdas251 failed for %s: see log" % stem)
    return fwcache


# ---------------------------------------------------------------------------
# toolchain steps
# ---------------------------------------------------------------------------

def build_sdcc_rel(src, out_rel, casedir):
    """kernel.c / wrapper.c -> cpp -> sdcc --c1mode (stdin) -> sdas251 .rel.

    SDCC_FW marks the firmware side: shared headers (e.g. vectors.h) use it
    to pick the SDCC `code` storage class for ROM tables, because the strict
    link chain drops .area CONST data while .area CSEG places fine
    (probe 2026-09-05, RESULTS.md)."""
    stem = out_rel[:-4]
    asm = stem + ".asm"
    rc, pre = sh(["cpp", "-P", "-undef", "-nostdinc", "-DSDCC_FW",
                  "-I", casedir, src])
    if rc != 0:
        return False, "cpp failed: %s" % pre[:400]
    with open(asm, "wb") as fo:
        p = subprocess.run([SDCC, "-mmcs251", "--c1mode", "-o", asm],
                           input=pre.encode(), stdout=fo,
                           stderr=subprocess.STDOUT, cwd=casedir)
    if p.returncode != 0:
        try:
            err = open(asm, "r", errors="replace").read()[:400]
        except OSError:
            err = ""
        return False, "sdcc --c1mode failed (rc=%d): %s" % (p.returncode, err)
    rc, _ = sh([SDAS, "-plosgffw", "-o", out_rel, asm],
               stdout_path=stem + ".sdas.log")
    if rc != 0:
        return False, "sdas251 failed: see %s" % (stem + ".sdas.log")
    return True, ""


def expand_lk(template, out_lk, output_stem, crt0, harness, module, provider):
    with open(template, "r") as f:
        text = f.read()
    text = (text
            .replace("@OUTPUT_IHX@", output_stem)
            .replace("@CRT0_REL@", crt0)
            .replace("@HARNESS_REL@", harness)
            .replace("@MODULE_REL@", module)
            .replace("@PROVIDER_REL@", provider))
    # CONST area fix (measured 2026-09-05, RESULTS.md): without an explicit
    # base, the CODE-class allocator parks CONST right behind HOME at
    # 0xFF0001, and QEMU's stc32g144k246 machine returns 0 for data reads
    # from the 0xFFxxxx window (execution from HOME still works).  With
    # -b CONST = 0xFC8000 the table lands in the 0xFC window and movc reads
    # it correctly (Bk22PASS probe).  0xFC8000 leaves ~22KB after GSINIT0's
    # cluster, far beyond our image sizes.
    text = text.replace("-b GSINIT0 = 0xfc2800",
                        "-b GSINIT0 = 0xfc2800\n-b CONST = 0xfc8000")
    with open(out_lk, "w") as f:
        f.write(text)   # template carries no blank lines; keep it that way


def run_qemu(hexfile, serial_out):
    cmd = ["timeout", str(QEMU_TIMEOUT), QEMU,
           "-M", "stc32g144k246", "-bios", hexfile, "-accel", "tcg",
           "-icount", "shift=0,align=off,sleep=off",
           "-display", "none", "-monitor", "none",
           "-serial", "stdio"]
    with open(os.devnull, "rb") as devnull, open(serial_out, "wb") as out:
        p = subprocess.run(cmd, stdin=devnull, stdout=out,
                           stderr=subprocess.DEVNULL)
    return p.returncode          # 124 expected: harness loops forever


def read_serial(path):
    try:
        data = open(path, "rb").read()
    except OSError:
        return ""
    return data.decode("ascii", "replace").replace("\r", "")


def verdict_from_serial(serial):
    """DESIGN.md: full PASS present, no FAIL."""
    has_pass = "PASS" in serial
    has_fail = "FAIL" in serial
    return has_pass and not has_fail


def mangle_ll_symbols(src_ll, dst_ll):
    """Front-end symbol adaptation for the DUT chain.

    SDCC prefixes every C-level global (functions and data) with '_' in the
    ASxxxx world (_String_length, _g_str_buf), while clang emits the bare C
    name.  The SDCC-compiled wrapper references the prefixed names, so the
    clang-produced IR must be renamed to match.  llvm.* intrinsics and names
    that already start with '_' are left alone.  This mirrors what the
    handwritten .ll assets (matrix.ll's @_p13_ret8) did manually.

    TEMPORARY (PM ruling 2026-09-05): Alice's front-end workstream is
    deciding the official C-symbol naming mechanism; once ruled, this
    adaptation may move into the compiler side and be removed here."""
    with open(src_ll, "r") as f:
        text = f.read()

    def repl(m):
        name = m.group(1)
        if name.startswith("llvm.") or name.startswith("_"):
            return m.group(0)
        return "@_" + name

    text = re.sub(r"@([A-Za-z$][A-Za-z0-9_$.]*)", repl, text)
    with open(dst_ll, "w") as f:
        f.write(text)


_SHIFT_RE = re.compile(
    r"^(?P<res>%\S+) = (?P<op>lshr|shl|ashr) (?P<ty>i8|i16|i32) (?P<src>%\S+|\-?\d+), (?P<k>\d+)$")


def lower_constant_shifts(src_ll, dst_ll):
    """TEMPORARY constant-shift lowering for the DUT chain (remove when the
    backend grows shift ISel -- PM ruling 2026-09-05).

    Measured capability (llc 24, 2026-09-05): lshr/shl of i8/i16 (constant
    or variable), mul, and udiv have no instruction selection; and, icmp,
    add/sub and select do.  This pass rewrites each CONSTANT shift

        %r = lshr i8 %x, 4        (or shl, i8/i16, any constant k)

    into an LSB-first bit-rebuild chain

        %t0 = and i8 %x, 16 ; %c0 = icmp ne i8 %t0, 0
        %a0 = add i8 0, 1   ; %r  = select i1 %c0, i8 %a0, i8 0
        %t1 = and i8 %x, 32 ; ... (src bit i -> dst bit i-k for lshr,
                                    i -> i+k for shl; overflow drops, mod 2^n)

    which is exactly the mathematical definition of the shift, keeps the
    original SSA result name (users untouched), and uses only selectable
    ops.  Variable shifts are left alone on purpose (none in the pilot set;
    they should fail loudly rather than silently change shape)."""
    counter = [0]
    allocas = {}      # function define output-index -> alloca line
    func_def_idx = None
    func_has_slot = False

    def tmp():
        counter[0] += 1
        return "%%mcs.tmp%d" % counter[0]

    out_lines = []
    for line in open(src_ll, "r").read().splitlines():
        if line.startswith("define "):
            func_def_idx = len(out_lines)
            func_has_slot = False
            out_lines.append(line)
            continue
        m = _SHIFT_RE.match(line.strip())
        if not m or line.startswith(";"):
            out_lines.append(line)
            continue
        res, op, ty, src, k = (m.group("res"), m.group("op"),
                               m.group("ty"), m.group("src"),
                               int(m.group("k")))
        width = {"i8": 8, "i16": 16, "i32": 32}[ty]
        indent = line[:len(line) - len(line.lstrip())]
        # ashr arrives via C integer promotion: `u8 x >> k` promotes x to
        # signed int, so clang emits ashr.  Shift operands here always come
        # from zext'ed u8/u16 loads (non-negative), where ashr is bit-for-
        # bit identical to lshr; treat it as an unsigned rebuild.
        if op in ("lshr", "ashr"):
            pairs = [(i, i - k) for i in range(k, width)]
        else:
            pairs = [(i, i + k) for i in range(0, width - k)]
        pairs = [(s_, d_) for (s_, d_) in pairs if 0 <= d_ < width]
        if not pairs:   # shift by >= width: result is constant 0
            out_lines.append("%s%s = and %s %s, 0" % (indent, res, ty, src))
            continue
        if not func_has_slot:
            allocas[func_def_idx] = "%s%%mcs.shslot = alloca %s" % (indent, ty)
            func_has_slot = True
        slot = "%mcs.shslot"
        y = "0"
        for idx, (src_bit, dst_bit) in enumerate(pairs):
            tm = tmp()
            tc = tmp()
            to = tmp()
            tl = tmp()
            out_lines.append("%s%s = and %s %s, %d"
                             % (indent, tm, ty, src, 1 << src_bit))
            out_lines.append("%s%s = icmp ne %s %s, 0"
                             % (indent, tc, ty, tm))
            # Volatile alloca round-trip as an accumulator barrier: the DAG
            # combiner re-synthesizes even an OR/select rebuild chain into a
            # shift when it can see through it (observed in the DAG dump),
            # which either fails selection or silently miscompiles; volatile
            # memory is the one barrier it will not cross.  OR-accumulation
            # (dst bits disjoint, or == add) avoids the add-based fold too.
            out_lines.append("%sstore volatile %s %s, ptr %s"
                             % (indent, ty, y, slot))
            out_lines.append("%s%s = load volatile %s, ptr %s"
                             % (indent, tl, ty, slot))
            out_lines.append("%s%s = or %s %s, %d"
                             % (indent, to, ty, tl, 1 << dst_bit))
            prev = tl
            last = (idx == len(pairs) - 1)
            y = res if last else tmp()
            out_lines.append("%s%s = select i1 %s, %s %s, %s %s"
                             % (indent, y, tc, ty, to, ty, prev))
    for idx in sorted(allocas, reverse=True):
        out_lines.insert(idx + 1, allocas[idx])
    with open(dst_ll, "w") as f:
        f.write("\n".join(out_lines) + "\n")


# ---------------------------------------------------------------------------
# per-case pipeline
# ---------------------------------------------------------------------------

_WIDTH_TYPEDEF_RE = re.compile(r"typedef\s+(?:unsigned\s+)?int\s")
_U32_TYPEDEF_RE = re.compile(r"^\s*typedef\s+unsigned\s+int\s+u32\s*;")
_WIDTH_EXTERN_RE = re.compile(r"^\s*extern\s+(?:unsigned\s+)?int\s")


def check_shared_widths(kernel_src):
    """Cross-compiler shared-width guard (baseline reset follow-up,
    2026-09-05): `(unsigned) int` remains forbidden for u16 and shared
    globals because SDCC mcs251 int=16 while host clang int=32.  The sole
    exception is the canonical `typedef unsigned int u32`: clang LP64 would
    otherwise turn `unsigned long` function values into unsupported i64,
    while the MCS251 ABI carries those values in its 32-bit long-width ABI
    slots.  Other shared scalars must use unsigned char/short/long."""
    bad = []
    try:
        lines = open(kernel_src).read().splitlines()
    except OSError:
        return bad
    for i, ln in enumerate(lines, 1):
        if ln.strip().startswith(("//", "*", "/*")):
            continue
        if (_WIDTH_TYPEDEF_RE.search(ln)
                and not _U32_TYPEDEF_RE.match(ln)):
            bad.append("%s:%d: typedef uses (unsigned) int -- use unsigned "
                       "short/long, except canonical u32 (cross-compiler "
                       "width differs)" % (kernel_src, i))
        if _WIDTH_EXTERN_RE.match(ln):
            bad.append("%s:%d: extern declares (unsigned) int -- use explicit "
                       "width types" % (kernel_src, i))
    return bad


def run_case(casedir, name, fwcache):
    out = {"case": name, "layer": "t1", "kind": "smoke" if not os.path.exists(
        os.path.join(casedir, "host-main.c")) else "full", "pass": False}
    kernel = os.path.join(casedir, "kernel.c")
    wrapper = os.path.join(casedir, "wrapper.c")
    if not os.path.exists(wrapper):
        # Batch-extracted cases (kernel.c + EXTRACT.md only) wait for
        # wrapper/host instantiation per t1/TEMPLATE.md -- not a failure.
        out["status"] = "pending-instantiation"
        out["pending"] = True
        out["note"] = ("kernel.c present, wrapper.c/host-main.c not yet "
                       "instantiated (see t1/TEMPLATE.md)")
        return out
    width_bad = check_shared_widths(kernel)
    if width_bad:
        out["status"] = "width-check-failed"
        out["error"] = "; ".join(width_bad)
        return out
    b = os.path.join(BUILD, name)
    os.makedirs(b, exist_ok=True)

    # ---- Oracle-A (host truth) -------------------------------------------
    if out["kind"] == "full":
        rc, gout = sh([GCC, "-std=c99", "-O0", "-Wall",
                       "-o", os.path.join(b, "oracle-a.bin"),
                       os.path.join(casedir, "host-main.c")],
                      stdout_path=os.path.join(b, "oracle-a.gcc.log"))
        if rc != 0:
            out["oracle_a"] = {"status": "gcc-failed"}
            out["error"] = "oracle-A gcc build failed"
            return out
        oracle_a_serial = os.path.join(b, "oracle-a.serial")
        with open(oracle_a_serial, "wb") as serial_out:
            p = subprocess.Popen([os.path.join(b, "oracle-a.bin")],
                                 stdout=serial_out, stderr=subprocess.STDOUT,
                                 start_new_session=True)
            try:
                rc = p.wait(timeout=ORACLE_A_TIMEOUT)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(p.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                p.wait()
                out["oracle_a"] = {
                    "status": "run-failed timeout=%ds" % ORACLE_A_TIMEOUT}
                out["error"] = "oracle-A run timed out"
                return out
        out["oracle_a"] = {"status": "ok" if rc == 0 else "run-failed rc=%d" % rc}
        if rc != 0:
            out["error"] = "oracle-A run failed"
            return out
    else:
        out["oracle_a"] = {"status": "skipped (smoke case)"}

    # ---- Oracle-B (SDCC reference chain) ----------------------------------
    krel_b = os.path.join(b, "kernel-b.rel")
    ok, err = build_sdcc_rel(kernel, krel_b, casedir)
    if not ok:
        out["oracle_b"] = {"status": "kernel sdcc build failed"}
        out["error"] = err
        return out
    wrel = os.path.join(b, "wrapper.rel")
    ok, err = build_sdcc_rel(wrapper, wrel, casedir)
    if not ok:
        out["oracle_b"] = {"status": "wrapper sdcc build failed"}
        out["error"] = err
        return out

    bl = os.path.join(b, "oracle-b")
    expand_lk(os.path.join(FW, "link-template.lk"), bl + ".lk", bl,
              os.path.join(fwcache, "crt0.rel"), wrel, krel_b,
              os.path.join(fwcache, "provider.rel"))
    rc, _ = sh([SDLD, "--mcs251-abi", "-r", "-nf", bl + ".lk"],
               stdout_path=os.path.join(b, "oracle-b.sdld.log"))
    bihx = bl + ".ihx"
    if rc != 0 or not os.path.exists(bihx):
        out["oracle_b"] = {"status": "sdld failed"}
        out["error"] = "sdld rc=%d (see oracle-b.sdld.log)" % rc
        return out
    shutil.copy(bihx, bl + ".hex")
    qrc = run_qemu(bl + ".hex", os.path.join(b, "oracle-b.serial"))
    ser_b = read_serial(os.path.join(b, "oracle-b.serial"))
    ok_b = verdict_from_serial(ser_b)
    out["oracle_b"] = {"status": "ok" if ok_b else "serial-verdict-failed",
                       "qemu_rc": qrc, "serial": ser_b}

    # ---- DUT (LLVM production chain) --------------------------------------
    if not os.path.exists(CLANG):
        out["dut"] = {"status": "pending-clang",
                      "note": "clang bootstrap not finished; rerun later"}
        out["pass"] = False
        out["pending"] = True
        return out

    ll = os.path.join(b, "kernel.ll")
    # All cases use -O0 (measured 2026-09-05, RESULTS.md):
    #   -O1 re-syntheses if/else chains into `switch`, whose jump tables the
    #   backend cannot select, and DELETES empty delay loops that the smoke
    #   case exists to prove survivable; -O0 keeps the source's control flow
    #   verbatim.  The x86 front end's i32 promotion of sub-integer ops at
    #   -O0 is handled downstream by lower_constant_shifts().
    rc, cout = sh([CLANG, "--target=x86_64-pc-linux-gnu", "-emit-llvm", "-S",
                   "-O0", "-o", ll, kernel],
                  stdout_path=os.path.join(b, "kernel.clang.log"))
    if rc != 0:
        out["dut"] = {"status": "clang failed"}
        out["error"] = "clang -emit-llvm failed"
        return out
    ll_m = os.path.join(b, "kernel.syms.ll")
    if IR_SHIMS:
        mangle_ll_symbols(ll, ll_m)   # SHIM 1 (retirable): '_' prefix
        shim_input = ll_m
    else:
        shim_input = ll
    ll_f = os.path.join(b, "kernel.final.ll")
    if IR_SHIMS:
        lower_constant_shifts(shim_input, ll_f)  # SHIM 2 (retirable): shifts
        final_input = ll_f
    else:
        final_input = shim_input
    krel_d = os.path.join(b, "kernel-dut.rel")
    rc, lout = sh([LLC, "-mtriple=mcs251-unknown-none",
                   "-filetype=obj", "-o", krel_d, final_input],
                  stdout_path=os.path.join(b, "kernel.llc.log"))
    if rc != 0:
        out["dut"] = {"status": "llc failed"}
        out["error"] = "llc failed"
        return out
    dl = os.path.join(b, "dut")
    expand_lk(os.path.join(FW, "link-template.lk"), dl + ".lk", dl,
              os.path.join(fwcache, "crt0.rel"), wrel, krel_d,
              os.path.join(fwcache, "provider.rel"))
    dhex = dl + ".hex"
    rc, _ = sh([sys.executable, MLD, "--mcs251-abi", "-f", dl + ".lk"],
               stdout_path=os.path.join(b, "dut.mld.log"))
    if rc != 0 or not os.path.exists(dhex):
        out["dut"] = {"status": "mcs251_ld failed"}
        out["error"] = "mcs251_ld.py rc=%d (see dut.mld.log)" % rc
        return out
    qrc = run_qemu(dhex, os.path.join(b, "dut.serial"))
    ser_d = read_serial(os.path.join(b, "dut.serial"))
    ok_d = verdict_from_serial(ser_d)
    out["dut"] = {"status": "ok" if ok_d else "serial-verdict-failed",
                  "qemu_rc": qrc, "serial": ser_d}

    if out["kind"] == "smoke":
        out["pass"] = ok_d
        return out

    ser_a = read_serial(os.path.join(b, "oracle-a.serial"))
    tri = (ser_a == ser_b == ser_d)
    out["triangle_equal"] = tri
    out["serial_a"] = ser_a
    out["pass"] = tri and ok_b and ok_d
    return out


# ---------------------------------------------------------------------------
# discovery + main
# ---------------------------------------------------------------------------

def discover(layer):
    base = os.path.join(HERE, {"t1": "t1", "t2": "t2"}.get(layer, layer))
    if layer == "t4":
        base = os.path.join(HERE, "t4-probes")
    if not os.path.isdir(base):
        return None            # reserved, not yet populated
    cases = sorted(d for d in os.listdir(base)
                   if os.path.isdir(os.path.join(base, d))
                   and os.path.exists(os.path.join(base, d, "kernel.c")))
    return base, cases


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--filter", default="t1",
                    choices=["t1", "t2", "t4", "all"])
    ap.add_argument("--case", help="run a single case by name")
    ap.add_argument("--no-ir-shims", action="store_true",
                    help="disable the two transitional IR shims (symbol "
                         "prefix adaptation + constant-shift lowering); "
                         "use to probe whether the toolchain is ready to "
                         "retire them")
    args = ap.parse_args()
    if args.no_ir_shims:
        globals()["IR_SHIMS"] = False

    layers = ["t1", "t2", "t4"] if args.filter == "all" else [args.filter]
    os.makedirs(BUILD, exist_ok=True)
    try:
        fwcache = ensure_firmware_cache()
    except RuntimeError as e:
        log(str(e))
        return 2

    results = []
    any_fail = False
    for layer in layers:
        found = discover(layer)
        if found is None:
            log("layer %s: directory absent -- reserved for its owner, skipping" % layer)
            continue
        base, cases = found
        if args.case:
            cases = [c for c in cases if c == args.case]
            if not cases:
                log("case %r not found under %s" % (args.case, base))
                return 2
        for c in cases:
            log("=== %s/%s ===" % (layer, c))
            t0 = time.time()
            r = run_case(os.path.join(base, c), c, fwcache)
            r["seconds"] = round(time.time() - t0, 1)
            results.append(r)
            if r.get("pending"):
                log("  PENDING: %s" % r.get("dut", {}).get("note", ""))
            elif r["pass"]:
                log("  PASS (%ss)" % r["seconds"])
            else:
                any_fail = True
                log("  FAIL: %s" % json.dumps(
                    {k: v for k, v in r.items()
                     if k in ("error", "oracle_a", "oracle_b", "dut",
                              "triangle_equal")}, ensure_ascii=False))

    summary = {"timestamp": time.time(), "results": results,
               "total": len(results),
               "passed": sum(1 for r in results if r["pass"]),
               "failed": sum(1 for r in results if not r["pass"] and not r.get("pending")),
               "pending": sum(1 for r in results if r.get("pending"))}
    with open(os.path.join(BUILD, "results.json"), "w") as f:
        json.dump(summary, f, indent=2, ensure_ascii=False)
    log("summary: %d/%d passed, %d failed, %d pending -> %s" %
        (summary["passed"], summary["total"], summary["failed"],
         summary["pending"], os.path.join(BUILD, "results.json")))
    return 1 if any_fail else 0


if __name__ == "__main__":
    sys.exit(main())
