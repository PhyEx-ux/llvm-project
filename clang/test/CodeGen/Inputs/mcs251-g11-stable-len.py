"""G11-N6 boundary: the identity must be representable in the NOTE's u8 length
field, i.e. at most 255 bytes; the 256-byte case must be an explicit frontend
error with a source location, and the identity must NOT be truncated, hashed
shorter, disambiguated at emission time, or replaced by a bare name.

The fixtures are generated so the *exact* identity length is known:
  * an external plain C entity -> identity = the declaration identifier;
  * a file-scope static -> identity = <underscored basename>.<8 hex FNV-1a(abs
    path)>.<identifier>, i.e. a combination of a long file name and a long
    symbol name (the "超长文件名/符号名组合" requested for this fix).
Both are compiled: the first must succeed and its identity must appear in the
IR verbatim, the second must fail with the length diagnostic. Identical data
with one byte more must fail, one byte less must pass -- the boundary is exact,
not approximate.

usage: mcs251-g11-stable-len.py <clang> <workdir>
"""
import pathlib
import re
import subprocess
import sys

clang = sys.argv[1:-1]
root = pathlib.Path(sys.argv[-1]).absolute()
root.mkdir(parents=True, exist_ok=True)
LIMIT = 255


def fnv(text):
    value = 2166136261
    for byte in text.encode():
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def stable_of_ir(ir):
    found = re.findall(r'"mcs251-stable-symbol"="([^"]*)"', ir)
    return found


def compile_ok(path):
    return subprocess.check_output(
        clang + ["-cc1", "-triple", "mcs251", "-std=c11", "-Werror",
                 "-emit-llvm", "-o", "-", str(path)],
        text=True, stderr=subprocess.STDOUT)


def compile_err(path):
    proc = subprocess.run(
        clang + ["-cc1", "-triple", "mcs251", "-std=c11", "-Werror",
                 "-emit-llvm", "-o", "-", str(path)],
        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return proc.returncode, proc.stdout


# --- external entity: identity == identifier, so the length is exact ---------
for name_len, should_pass in ((LIMIT - 1, True), (LIMIT, True), (LIMIT + 1, False)):
    source = root / ("external-%d.c" % name_len)
    ident = "e" * name_len
    source.write_text("int %s __attribute__((mcu_place_at(0x100)));\n" % ident)
    rc, output = compile_err(source)
    if should_pass:
        assert rc == 0, output
        stables = stable_of_ir(output)
        assert stables == [ident], (name_len, stables)
        assert len(stables[0]) == name_len
    else:
        assert rc != 0, (name_len, output)
        assert "stable symbol" in output and "256 bytes long" in output, output
        # Source location: the diagnostic must carry file:line:col.
        assert re.search(r"%s:1:\d+: error" % re.escape(source.name), output), output
        # No truncation and no fallback: nothing may be emitted.
        assert "mcs251-stable-symbol" not in output, output
        # EXACTLY ONE error. The length check runs in the CodeGen identity
        # helper, which is entered more than once per entity (declaration
        # emission, definition emission and the TU-final refresh pass), so a
        # deduplication regression would report this same error twice; "nonzero
        # exit plus a substring" would not notice. The per-canonical-declaration
        # dedup is locked in depth by Sema/mcs251-g11-stable-len-dup.c, which
        # uses -verify and covers the multi-entry and two-entity shapes.
        errors = [l for l in output.splitlines() if re.search(r":\d+:\d+: error:", l)]
        assert len(errors) == 1, (name_len, errors)

# --- file-scope static: long file name *and* long symbol name ---------------
# Choose a long file name so that <basename>.<8 hex>.<identifier> reaches the
# boundary with a modest identifier length, then solve for the exact identifier
# length in both directions.
for tag, extra in (("max", 0), ("overflow", 1)):
    # First pass with a placeholder to learn the fixed part's length.
    probe = root / ("mcs251-g11-stable-len-%s-long-file-name.c" % tag)
    probe.write_text("static int probe __attribute__((mcu_place_at(0x100), mcu_retain));\n")
    stables = stable_of_ir(compile_ok(probe))
    assert len(stables) == 1, stables
    fixed = len(stables[0]) - len("probe")
    ident_len = LIMIT - fixed + extra
    assert ident_len > 0, (fixed, ident_len)
    ident = "s" * ident_len
    source = root / ("mcs251-g11-stable-len-%s-long-file-name.c" % tag)
    source.write_text(
        "static int %s __attribute__((mcu_place_at(0x100), mcu_retain));\n" % ident)
    expected_prefix = "%s.%08X." % (
        re.sub(r"[^a-zA-Z0-9_]", "_", source.name), fnv(str(source)))
    rc, output = compile_err(source)
    if extra == 0:
        assert rc == 0, output
        stables = stable_of_ir(output)
        assert stables == [expected_prefix + ident], stables
        assert len(stables[0]) == LIMIT, len(stables[0])
    else:
        assert rc != 0, output
        assert "is %d bytes long" % (LIMIT + 1) in output, output
        assert "exceeds the 255-byte limit" in output, output
        assert re.search(r"%s:1:\d+: error" % re.escape(source.name), output), output
        assert "mcs251-stable-symbol" not in output, output
        # Exactly one error, see the comment in the external-entity branch.
        errors = [l for l in output.splitlines() if re.search(r":\d+:\d+: error:", l)]
        assert len(errors) == 1, errors