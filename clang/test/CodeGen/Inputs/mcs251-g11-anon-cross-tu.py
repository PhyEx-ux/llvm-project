"""G11-N4 §8.1 anonymous-namespace ruling: two translation units with the same
anonymous-namespace spelling must be separated by the TU qualifier T, while the
mangled component (which carries the outer namespace) stays identical.

The two TUs are generated with different file names, so their T differs
(<basename>.<8 hex digits FNV-1a of the absolute path>). Everything after the
first two dot-separated fields -- i.e. the entity component -- must be
byte-identical: the anonymous namespace of A is encoded the same way in both,
and no bare `x` may appear.

usage: anon-cross-tu.py <clang> <workdir>
"""
import pathlib
import re
import subprocess
import sys

clang = sys.argv[1:-1]
root = pathlib.Path(sys.argv[-1]).absolute()
root.mkdir(parents=True, exist_ok=True)

SOURCE = """namespace A { namespace { int x __attribute__((mcu_place_at(0x100), mcu_retain)) = 1; } }
namespace B { namespace { int x __attribute__((mcu_place_at(0x110), mcu_retain)) = 2; } }
"""


def identities(path):
    ir = subprocess.check_output(
        clang + ["-triple", "mcs251", "-std=c++17", "-Werror", "-emit-llvm",
                 "-o", "-", str(path)],
        text=True)
    found = re.findall(r'"mcs251-stable-symbol"="([^"]*)"', ir)
    assert len(found) == 2, (path, found)
    return found


def split(identity):
    # <TU basename>.<8 hex digits>.<entity component>
    fields = identity.split(".", 2)
    assert len(fields) == 3, identity
    assert re.fullmatch(r"[0-9A-F]{8}", fields[1]), identity
    return fields


first = root / "anon-first.cpp"
second = root / "anon-second.cpp"
first.write_text(SOURCE)
second.write_text(SOURCE)

tu_a = sorted(split(i) for i in identities(first))
tu_b = sorted(split(i) for i in identities(second))

# The entity components are the anonymous namespaces of A and B; the outer
# namespace is part of the mangled name, and there is no bare `x`.
expected = ["_ZN1A12_GLOBAL__N_11xE", "_ZN1B12_GLOBAL__N_11xE"]
assert [c for _, _, c in tu_a] == expected, tu_a
assert [c for _, _, c in tu_b] == expected, tu_b

# Different translation units: T must differ (different basename and hash).
assert [b for b, _, _ in tu_a] != [b for b, _, _ in tu_b], (tu_a, tu_b)
assert [h for _, h, _ in tu_a] != [h for _, h, _ in tu_b], (tu_a, tu_b)

# Same TU, different outer namespace: distinct even though the variable name
# and the hash are shared.
assert tu_a[0][2] != tu_a[1][2], tu_a
