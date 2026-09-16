"""Check exact FNV identity, leading zeroes, symlinks and hidden linkage."""
import pathlib
import re
import subprocess
import sys

clang = sys.argv[1:-1]
root = pathlib.Path(sys.argv[-1]).absolute()
root.mkdir(parents=True, exist_ok=True)


def fnv(text):
    value = 2166136261
    for byte in text.encode():
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


# Choose a deterministic path whose hash requires a leading zero.
for i in range(4096):
    source = root / ("source-%d.c" % i)
    if fnv(str(source)) < 0x10000000:
        break
else:
    raise AssertionError("could not find leading-zero fixture")
source.write_text("""
static int local __attribute__((mcu_place_at(0x100), mcu_retain));
int external __attribute__((mcu_place_at(0x200)));
""")


def check(path, relative=False):
    name = path.name if relative else str(path)
    ir = subprocess.check_output(
        clang + ["-triple", "mcs251", "-fvisibility=hidden", "-emit-llvm",
                 "-o", "-", name], cwd=str(root), text=True)
    base = re.sub(r"[^a-zA-Z0-9_]", "_", path.name)
    identity = "%s.%08X.local" % (base, fnv(str(path)))
    assert '"mcs251-stable-symbol"="%s"' % identity in ir, ir
    assert '"mcs251-stable-symbol"="external"' in ir, ir
    # Repeated CodeGen hooks must not duplicate a same-kind attribute.
    for line in ir.splitlines():
        if line.startswith("attributes #"):
            assert line.count('"mcs251-place"=') <= 1, line
            assert line.count('"mcs251-stable-symbol"=') <= 1, line
    return ir


check(source)
check(source, relative=True)
link = root / "symlink-source.c"
if link.is_symlink():
    link.unlink()
link.symlink_to(source)
check(link)  # Hash the spelled path, not the symlink target's realpath.
