#!/usr/bin/env bash
# a3-libc-e2e.sh - A3 libc/runtime end-to-end chain for the approved 32-bit
# AS0 superset of AS4 (RUNTIME-AS-PTR-DESIGN-A.md §3-A3 "验收补充二").
#
# Two runtimes are exercised SEPARATELY (the design forbids merging them into
# one "libc passes" line):
#
#   A. corpus shim (mcs251-corpus-matrix/shim/shim-libc.c, standard C
#      prototypes, single-pointer family only): strlen/strchr/atoi receive
#      __code sources and memset writes an AS0 destination.
#   B. private runtime (validation/mcs251-runtime/src/mcs251_libc.c, the
#      setter + global-uint32-slot ABI): the three _set_src integer
#      round-trips are exercised and recorded as technical debt. Callers first
#      convert AS4 to the setter's AS0 parameter using addrspacecast; the later
#      integer-slot transport is not independent proof of cast semantics.
#
# BOTH runtimes go through the same byte checker (a3-libc-bytes.py --mode), so
# the private chain is no longer exempt: its CODE payloads, its CODE-context
# literal and its AS0 control literal are all byte-verified.  The two-pointer
# family remains untestable under this ABI (recorded as a boundary of the
# follow-on slice, not an AS-conversion defect).
#
# Usage: a3-libc-e2e.sh [--no-qemu]
# Environment overrides: CLANG, LLC, LLD, QEMU, REPO.
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "$0")" && pwd)
REPO=${REPO:-$(cd -- "$ROOT/../.." && pwd)}
CORPUS=${CORPUS:-$(cd -- "$REPO/.." && pwd)/mcs251-corpus-matrix}
OUT="$ROOT/build/a3-libc"
SRC="$ROOT/src"

CLANG=${CLANG:-/home/liu/build-mcs251-s1/bin/clang}
LLC=${LLC:-/home/liu/build-mcs251/bin/llc}
LLD=${LLD:-/home/liu/build-mcs251-lld/bin/lld}
READOBJ=${READOBJ:-/home/liu/build-mcs251-lld/bin/llvm-readobj}
OBJCOPY=${OBJCOPY:-/home/liu/build-mcs251-s1/bin/llvm-objcopy}
YAML2OBJ=${YAML2OBJ:-/home/liu/build-mcs251-lld/bin/yaml2obj}
QEMU=${QEMU:-/home/liu/build-qemu/qemu-system-mcs251}
MACHINE=stc32g144k246

CONTRACT=1,1,32,8,1
DIALECT_INC="$REPO/validation/mcs251-dialect/include"
CRT_YAML="$REPO/validation/mcs251-elf/runtime/crt-selfstart.yaml"

AREA_ARGS=(
  --area-start=HOME=0xff0000 --area-start=VECS=0xff0003
  --area-start=BOOT=0xff0100 --area-start=CSEG=0xff0200
  --area-start=XINIT=0xff8000 --area-start=XDATA_INIT=0xff9000
  --area-start=XSEG=0x010000 --edata-end 0x3fff
)

rm -rf -- "$OUT"
mkdir -p -- "$OUT"

echo "== A. corpus shim (single-pointer family, __code sources) =="

# The shim itself, both optimization levels.
for opt in -O0 -O2; do
  "$CLANG" --target=mcs251-unknown-none -std=c11 "$opt" -Wall -Wextra \
    -I"$CORPUS/shim/include" \
    -Xclang -mcs251-memory-contract="$CONTRACT" \
    -S -emit-llvm "$CORPUS/shim/shim-libc.c" -o "$OUT/shim-libc$opt.ll"
  "$LLC" -mtriple=mcs251 -mcs251-memory-contract="$CONTRACT" \
    -mcs251-object-format=elf -filetype=obj \
    "$OUT/shim-libc$opt.ll" -o "$OUT/shim-libc$opt.o"
done
echo "shim: clang+llc OK at -O0 and -O2"

# The firmware (a3-libc-fw.c) at both levels; it is the only unit that uses
# __code sources with the shim's prototypes.
#
# The shim's own <string.h> prototypes are the standard ones, so this TU must
# include them rather than redeclare (the firmware declares them inline to
# stay independent of the shim header layout; both agree textually).
# Byte-exact comparison: the report compares every byte against a recomputed
# constant.  The previous sum/xor check could be folded and could not detect
# a permutation.
for opt in -O0 -O2; do
  "$CLANG" --target=mcs251-unknown-none -std=c11 "$opt" -fmcs251-keil \
    -Wall -Wextra -Werror \
    -I"$SRC" -I"$DIALECT_INC" -I"$CORPUS/shim/include" \
    -Xclang -mcs251-memory-contract="$CONTRACT" \
    -S -emit-llvm "$SRC/a3-libc-fw.c" -o "$OUT/a3-fw$opt.ll"
  "$LLC" -mtriple=mcs251 -mcs251-memory-contract="$CONTRACT" \
    -mcs251-object-format=elf -filetype=obj \
    "$OUT/a3-fw$opt.ll" -o "$OUT/a3-fw$opt.o"
done
echo "firmware: clang+llc OK at -O0 and -O2"

# The conversion must be an addrspacecast in the IR, never a bitcast or an
# integer round-trip: assert on the -O2 IR (which also proves the conversion
# survives optimization).  The CODE-context literal must be an AS4 global and
# the AS0 control literal an AS0 global.
for opt in -O0 -O2; do
  if grep -q "bitcast ptr addrspace(4)" "$OUT/a3-fw$opt.ll"; then
    echo "FAIL: bitcast used for an AS4 conversion at $opt" >&2
    exit 1
  fi
  if grep -qE "inttoptr i32 %[A-Za-z0-9._]+ to ptr addrspace\(4\)" \
      "$OUT/a3-fw$opt.ll"; then
    echo "FAIL: integer-laundered AS0 -> AS4 conversion at $opt" >&2
    exit 1
  fi
  # The cast may print either as a real instruction ("%x = addrspacecast ptr
  # addrspace(4) ...") or inline as a ConstantExpr ("addrspacecast (ptr
  # addrspace(4) @sym to ptr)"); both are addrspacecast.
  if ! grep -q "addrspacecast" "$OUT/a3-fw$opt.ll"; then
    echo "FAIL: no addrspacecast found at $opt" >&2
    exit 1
  fi
  # CODE-context literal: the AS4 global must be the one `code_lit_ptr`
  # points to.
  grep -qE '@\.str[0-9.]* = private unnamed_addr addrspace\(4\) constant \[21 x i8\] c"code context literal\\00"' \
    "$OUT/a3-fw$opt.ll" || {
    echo "FAIL: CODE-context literal is not an AS4 global at $opt" >&2
    exit 1
  }
  # AS0 control literal: must stay a plain AS0 global.
  grep -qE '@\.str[0-9.]* = private unnamed_addr constant \[18 x i8\] c"plain as0 literal\\00"' \
    "$OUT/a3-fw$opt.ll" || {
    echo "FAIL: AS0 control literal is not a plain AS0 global at $opt" >&2
    exit 1
  }
  # No integer-round-trip laundering anywhere in this module: `ptrtoint` of a
  # CODE pointer is legal for a pointer *difference* (strchr uses it), so the
  # rejected shape is specifically an integer becoming an AS4 pointer
  # (inttoptr to ptr addrspace(4)) without an addrspacecast.
  if grep -qE "inttoptr i[0-9]+ %?[A-Za-z0-9._]* to ptr addrspace\(4\)" \
      "$OUT/a3-fw$opt.ll"; then
    echo "FAIL: integer-laundered AS0 -> AS4 conversion at $opt" >&2
    exit 1
  fi
  # The complete assertion set (the review found the old script only printed
  # this claim without asserting it): both literal globals are present with
  # the exact payloads AND their distinct address spaces, and the conversion
  # reaches strlen.
  grep -qE "call .*@strlen\(ptr noundef addrspacecast \(ptr addrspace\(4\) @\.str[0-9.]* to ptr\)\)" \
    "$OUT/a3-fw$opt.ll" || {
    echo "FAIL: no AS4 -> AS0 converted strlen call at $opt" >&2
    exit 1
  }
  # The dataflow laundering check (Alice review R10-4): greps alone only
  # reject the `inttoptr to AS4` shape; this proves the whole AS4 -> AS0 path
  # is carried by addrspacecast and never an integer round-trip, for every
  # AS4 global use in the module.
  python3 "$ROOT/a3-as-launder-check.py" "$OUT/a3-fw$opt.ll" \
    --what "shim-fw$opt" || exit 1
done
echo "IR: addrspacecast present; no bitcast/inttoptr/ptrtoint launder;"
echo "    CODE-context literal is AS4, AS0 control literal stays AS0, and"
echo "    the converted strlen call is explicit"

# Negative: an XDATA (AS3) destination must keep being rejected (the approved
# relation is AS4 -> 32-bit AS0 only).  This is a compile-must-fail check and
# needs no linking.  Run at BOTH optimization levels (Alice review R10-3:
# the old check was -O0 only, so an O2-only regressions would not be caught).
for opt in -O0 -O2; do
  if "$CLANG" --target=mcs251-unknown-none -std=c11 "$opt" -fmcs251-keil \
      -Wall -I"$SRC" -I"$DIALECT_INC" -I"$CORPUS/shim/include" \
      -Xclang -mcs251-memory-contract="$CONTRACT" -S -emit-llvm -o /dev/null \
      -x c - <<'EOF' 2> "$OUT/as3-negative$opt.log"
#include "mcs251_type_compat.h"
#include <string.h>
BYTE __xdata xbuf[8];
void bad(void) { memset(xbuf, 0, 8); }
EOF
  then
    echo "FAIL: an AS3 (__xdata) destination was accepted by memset at $opt" >&2
    exit 1
  fi
  grep -q "changes address space of pointer" "$OUT/as3-negative$opt.log" || {
    echo "FAIL: the AS3 negative was rejected for the wrong reason at $opt" >&2
    cat "$OUT/as3-negative$opt.log" >&2
    exit 1
  }
done
echo "negative: AS3 (__xdata) destination still rejected by memset (-O0/-O2)"

# Negative: the 16-bit Tiny AND XTiny models must refuse the AS4 -> AS0
# conversion outright.  Alice review R10-3 (driver half): the XTiny case was
# covered by the llvm lit errors test but the e2e chain had no equal check,
# and Tiny had none at all.  The refusal is a backend gate (the front end
# still emits the addrspacecast), so the check runs clang then llc and
# requires the documented diagnostic from llc.
for tiny in "tiny:1,2,16,1,1" "xtiny:1,2,16,8,1"; do
  model=${tiny%%:*}; contract=${tiny#*:}
  "$CLANG" --target=mcs251-unknown-none -std=c11 -O0 -fmcs251-keil \
    -Wall -I"$SRC" -I"$DIALECT_INC" -I"$CORPUS/shim/include" \
    -Xclang -mcs251-memory-contract="$contract" \
    -S -emit-llvm -o "$OUT/tiny-$model.ll" -x c - <<'EOF' 2> "$OUT/tiny-$model.log"
#include "mcs251_type_compat.h"
char code tbuf[4] = "abc";
unsigned t_len(const char *s);
unsigned bad(void) { return t_len((const char *)tbuf); }
EOF
  grep -q "addrspacecast" "$OUT/tiny-$model.ll" || {
    echo "FAIL: the front end did not emit the conversion under $model" >&2
    exit 1
  }
  if "$LLC" -mtriple=mcs251 -mcs251-memory-contract="$contract" -O0 \
      -mcs251-object-format=elf -filetype=obj "$OUT/tiny-$model.ll" \
      -o /dev/null 2>> "$OUT/tiny-$model.log"; then
    echo "FAIL: the AS4 -> AS0 conversion was accepted under the $model model" >&2
    exit 1
  fi
  grep -q "unsupported address-space cast involving CODE" "$OUT/tiny-$model.log" || {
    echo "FAIL: the $model negative was refused for the wrong reason" >&2
    cat "$OUT/tiny-$model.log" >&2
    exit 1
  }
  echo "negative: $model contract still refuses the AS4 -> AS0 conversion"
done

# Link + byte checks (both levels) + QEMU for the -O0 and -O2 image.
"$YAML2OBJ" "$CRT_YAML" -o "$OUT/crt.o"

# Checker self-tests: deleting a payload, promoting the AS0 control into CODE
# and dropping a whole ROM object must each FAIL.  Run against a scratch copy
# of a known-good image so the negative cases cannot mutate the artefact the
# real assertions use.
"$LLD" -flavor mcs251 "${AREA_ARGS[@]}" --keep-symbols \
  --map="$OUT/self.map" -o "$OUT/self.elf" \
  "$OUT/a3-fw-O0.o" "$OUT/shim-libc-O0.o" "$OUT/crt.o"

python3 "$ROOT/a3-libc-bytes.py" "$OUT/self.elf" "$OUT/self.map" --mode shim \
  > "$OUT/self-ok.log" || {
    echo "FAIL: byte checker rejects the good image" >&2
    cat "$OUT/self-ok.log" >&2
    exit 1
  }

# The mutation harness patches the linked ELF bytes and requires a FAIL.
python3 - "$OUT/self.elf" "$OUT/self.map" "$ROOT/a3-libc-bytes.py" <<'PY'
import importlib.util
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

elf, mapf, checker = (Path(sys.argv[1]), Path(sys.argv[2]),
                      Path(sys.argv[3]))
spec = importlib.util.spec_from_file_location("bytes_check", checker)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

data, sections = mod.parse_elf(elf)
syms = mod.read_symbols(data, sections)


def sym_addr(name):
    return syms["_" + name][0]


def mutate_and_require_fail(tag, addr, newbytes):
    """Patch the file at the file offset of `addr`; the checker must fail."""
    target = Path(str(elf).replace(".elf", f".{tag}.elf"))
    shutil.copy(elf, target)
    blob = bytearray(target.read_bytes())
    for s in sections:
        if s["type"] == 1 and s["addr"] <= addr < s["addr"] + s["size"]:
            off = s["offset"] + (addr - s["addr"])
            blob[off:off + len(newbytes)] = newbytes
            break
    else:
        print(f"SELFTEST {tag}: cannot locate 0x{addr:x}")
        sys.exit(1)
    target.write_bytes(bytes(blob))
    p = subprocess.run([sys.executable, str(checker), str(target),
                        str(mapf), "--mode", "shim"],
                       capture_output=True, text=True)
    target.unlink()
    if p.returncode == 0:
        print(f"SELFTEST {tag}: ESCAPED (checker still passed)")
        sys.exit(1)
    print(f"SELFTEST {tag}: detected")


# S1: delete a payload (rom_digits) -> must FAIL.
mutate_and_require_fail("del-payload", sym_addr("rom_digits"), b"\x00" * 4)
# S2: corrupt rom_bytes -> must FAIL.
mutate_and_require_fail("bad-bytes", sym_addr("rom_bytes"), b"\x99\x99\x99\x99")
# S2b: Alice's exact counterexample -- destroy rom_digits AND rom_bytes while
#     leaving rom_msg intact; the old "present subset" logic still passed.
target = Path(str(elf).replace(".elf", ".del-two.elf"))
shutil.copy(elf, target)
blob = bytearray(target.read_bytes())
for name, fill in (("rom_digits", b"\x00" * 12),
                   ("rom_bytes", b"\x99" * 8)):
    a = sym_addr(name)
    for s in sections:
        if s["type"] == 1 and s["addr"] <= a < s["addr"] + s["size"]:
            off = s["offset"] + (a - s["addr"])
            blob[off:off + len(fill)] = fill
            break
target.write_bytes(bytes(blob))
p = subprocess.run([sys.executable, str(checker), str(target), str(mapf),
                    "--mode", "shim"], capture_output=True, text=True)
target.unlink()
if p.returncode == 0 or "rom_digits" not in p.stdout:
    print("SELFTEST del-two-payloads: ESCAPED")
    sys.exit(1)
print("SELFTEST del-two-payloads: detected (rom_msg alone cannot pass)")
# S3: copy the AS0 control literal's bytes into the CODE-context literal ->
#     the control-literal-in-CODE rule must catch the promoted layout.
code_ptr = sym_addr("code_lit_ptr")
target = int.from_bytes(
    [b for s in sections if s["type"] == 1
     and s["addr"] <= code_ptr < s["addr"] + s["size"]
     for b in data[s["offset"] + code_ptr - s["addr"]:
                   s["offset"] + code_ptr - s["addr"] + 4]][0:4], "big")
needle = mod.MODES["shim"]["as0_lit"]
mutate_and_require_fail("as0-in-code", target, needle[:len(needle)])

# ---- Alice review R10: map and indirect-symbol escapes -------------------
# A scratch MAP is written for each mutation; the ELF is known-good.
import re


def mutate_map_and_require_fail(tag, mutator):
    """Rewrite the map with `mutator` and require a checker FAIL."""
    mp = Path(str(mapf).replace(".map", f".{tag}.map"))
    mp.write_text(mutator(Path(mapf).read_text()))
    p = subprocess.run([sys.executable, str(checker), str(elf), str(mp),
                        "--mode", "shim"], capture_output=True, text=True)
    mp.unlink()
    if p.returncode == 0:
        print(f"SELFTEST map-{tag}: ESCAPED (checker still passed)")
        sys.exit(1)
    print(f"SELFTEST map-{tag}: detected")


def mutate_elf_and_require_fail(tag, mutator):
    """Patch the ELF bytes with `mutator` and require a checker FAIL."""
    target = Path(str(elf).replace(".elf", f".{tag}.elf"))
    blob = bytearray(elf.read_bytes())
    mutator(blob)
    target.write_bytes(bytes(blob))
    p = subprocess.run([sys.executable, str(checker), str(target), str(mapf),
                        "--mode", "shim"], capture_output=True, text=True)
    target.unlink()
    if p.returncode == 0:
        print(f"SELFTEST elf-{tag}: ESCAPED (checker still passed)")
        sys.exit(1)
    print(f"SELFTEST elf-{tag}: detected")


# R10-1: every DSEG address replaced by NOT_AN_ADDRESS -> must FAIL (the old
#        checker `continue`d over the unparseable line and still PASSed).
mutate_map_and_require_fail(
    "malformed", lambda t: re.sub(
        r"(\S+:\.mcs251\.(?:DSEG|dseg)\S* )0x[0-9a-f]+", r"\1NOT_AN_ADDRESS",
        t))
# R10-2: a DSEG slice that starts below CODE and crosses into it -> must FAIL
#        (the old checker only looked at the start address).
mutate_map_and_require_fail(
    "cross-code", lambda t: re.sub(
        r"(\S+:\.mcs251\.(?:DSEG|dseg)\S* )0x[0-9a-f]+ \+0x[0-9a-f]+",
        r"\g<1>0xfeffff +0x200", t))


# R10-3: shrink the two indirect pointer symbols to 1 byte -> the 4-byte
#        pointer read must be rejected (the old checker never read the size).
def shrink_pointer_symbols(blob):
    (_, sections2) = mod.parse_elf(elf)
    for s2 in sections2:
        if s2["type"] != 2:
            continue
        strbase2 = sections2[s2["link"]]["offset"]
        for off in range(s2["offset"], s2["offset"] + s2["size"],
                         s2["entsize"]):
            ni = struct.unpack_from(">I", blob, off)[0]
            end = blob.index(b"\0", strbase2 + ni)
            name = bytes(blob[strbase2 + ni:end]).decode()
            if name in ("_code_lit_ptr", "_plain_lit_ptr"):
                struct.pack_into(">I", blob, off + 8, 1)


mutate_elf_and_require_fail("ptr-size-1", shrink_pointer_symbols)
# R13: malformed function rows are not ignorable map metadata.
mutate_map_and_require_fail(
    "malformed-func", lambda t: re.sub(
        r"(?m)^FUNC 0x[0-9a-f]+", "FUNC NOT_AN_ADDRESS", t))

# R13: adjacent file-backed sections do not authorize a spanning read, and
# a declared section size does not authorize reading past the actual EOF.
read_sections = [
    dict(type=1, addr=0x100, size=4, offset=0, sname="first"),
    dict(type=1, addr=0x104, size=4, offset=4, sname="second"),
]
for tag, blob, addr, n, want in (
        ("within-section", b"abcdefgh", 0x100, 4, b"abcd"),
        ("cross-section", b"abcdefgh", 0x102, 4, None),
        ("past-eof", b"ab", 0x100, 4, None)):
    failures = []
    got = mod.read_bytes_strict(blob, read_sections, addr, n, failures, tag)
    if got != want or bool(failures) != (want is None):
        raise SystemExit(f"SELFTEST {tag}: wrong result {got!r}, {failures!r}")
    print(f"SELFTEST {tag}: PASS")
print("byte-checker self-tests: PASS")
PY

# Laundering-checker self-tests (Alice review R10-4): the counterexamples must
# each FAIL.  The three shapes are built by rewriting a known-good module.
python3 - "$OUT/a3-fw-O0.ll" "$ROOT/a3-as-launder-check.py" <<'PY'
import subprocess
import sys
from pathlib import Path

module, checker = Path(sys.argv[1]), Path(sys.argv[2])
text = module.read_text()

cases = {
    "int-round-trip": (
        "addrspacecast (ptr addrspace(4) @rom_digits to ptr)",
        "%laund = ptrtoint ptr addrspace(4) @rom_digits to i32\n"
        "  %lp = inttoptr i32 %laund to ptr\n"
        "  %x = add i32 0, 0\n"
        "  %call = call i32 @atoi(ptr noundef %lp)\n"
        "  %y = call i32 @atoi(ptr noundef addrspacecast (ptr addrspace(4) "
        "@rom_digits to ptr))"),
    "bare-as4-arg": (
        "addrspacecast (ptr addrspace(4) @rom_digits to ptr)",
        "@rom_digits)"),
    "bitcast-cast": (
        "addrspacecast (ptr addrspace(4) @rom_msg to ptr)",
        "bitcast (ptr addrspace(4) @rom_msg to ptr)"),
}
for tag, (old, new) in cases.items():
    if old not in text:
        print(f"SELFTEST launder-{tag}: cannot construct (pattern absent)")
        sys.exit(1)
    p = Path(module).with_suffix(f".launder-{tag}.ll")
    p.write_text(text.replace(old, new, 1))
    r = subprocess.run([sys.executable, str(checker), str(p), "--what", tag],
                       capture_output=True, text=True)
    p.unlink()
    if r.returncode == 0:
        print(f"SELFTEST launder-{tag}: ESCAPED (checker still passed)")
        sys.exit(1)
    print(f"SELFTEST launder-{tag}: detected")
print("laundering-checker self-tests: PASS")
PY

link_and_check() { # opt
  local opt=$1
  "$LLD" -flavor mcs251 "${AREA_ARGS[@]}" --keep-symbols \
    --map="$OUT/fw$opt.map" -o "$OUT/fw$opt.elf" \
    "$OUT/a3-fw$opt.o" "$OUT/shim-libc$opt.o" "$OUT/crt.o"
  # QEMU loads the ihex image (the ELF is not a loadable firmware container).
  "$OBJCOPY" -O ihex "$OUT/fw$opt.elf" "$OUT/fw$opt.hex"
  # The CODE-resident sources must appear verbatim in their own symbols'
  # ranges, and the AS0 control literal must stay outside CODE.
  python3 "$ROOT/a3-libc-bytes.py" "$OUT/fw$opt.elf" "$OUT/fw$opt.map" \
    --mode shim
  echo "link+bytes($opt): OK"
}

link_and_check -O0
link_and_check -O2

if [ "${1:-}" = "--no-qemu" ]; then
  echo "QEMU skipped (--no-qemu); byte chain verified in $OUT"
else
  run_qemu() { # opt
    local opt=$1
    local serial="$OUT/fw$opt.serial"
    : > "$serial"
    ( for _ in $(seq 1 120); do
        if [ -s "$serial" ] && grep -q "A3-LIBC-" "$serial"; then
          pkill -TERM -f "qemu-system-mcs251.*fw$opt" 2>/dev/null
          exit 0
        fi
        sleep 0.5
      done ) &
    local watcher=$!
    timeout --foreground 60 "$QEMU" -M "$MACHINE" -bios "$OUT/fw$opt.hex" \
      -accel tcg -display none -monitor none -serial "file:$serial" \
      > "$OUT/fw$opt.qemu.stdout" 2> "$OUT/fw$opt.qemu.stderr" || true
    kill "$watcher" 2>/dev/null || true
    wait "$watcher" 2>/dev/null || true
    echo "== QEMU($opt) transcript =="
    cat "$serial"
    # Byte-exact: every OK<n> must appear and the sentinel must be the PASS
    # line, so a single failing byte is visible.
    if ! grep -q '^OK1 OK2 OK3 OK4 OK5 OK6 $' "$serial"; then
      echo "A3 libc e2e($opt): FAIL (not every byte check reported OK)" >&2
      exit 1
    fi
    if ! grep -q '^A3-LIBC-PASS$' "$serial"; then
      echo "A3 libc e2e($opt): FAIL (see $serial)" >&2
      exit 1
    fi
    echo "A3 libc e2e($opt): PASS"
  }
  run_qemu -O0
  run_qemu -O2
fi

echo "== B. private runtime (setter + uint32 slot ABI; recorded as debt) =="
RT="$REPO/validation/mcs251-runtime/src"
mkdir -p "$OUT/priv"

# The runtime unit itself, both levels. -O2 is EXPECTED to hit the documented
# PC-rel branch-range limit (mcs251_libc.h:40); a refusal there is recorded,
# not treated as an AS-conversion failure.
for opt in -O0 -O2; do
  if "$CLANG" --target=mcs251-unknown-none -std=c11 "$opt" -Wall \
      -I"$RT" -Xclang -mcs251-memory-contract="$CONTRACT" \
      -S -emit-llvm "$RT/mcs251_libc.c" \
      -o "$OUT/priv/mcs251_libc$opt.ll" 2> "$OUT/priv/mcs251_libc$opt.log"; then
    if "$LLC" -mtriple=mcs251 -mcs251-memory-contract="$CONTRACT" \
        -mcs251-object-format=elf -filetype=obj \
        "$OUT/priv/mcs251_libc$opt.ll" \
        -o "$OUT/priv/mcs251_libc$opt.o" 2>> "$OUT/priv/mcs251_libc$opt.log"; then
      echo "private runtime mcs251_libc.c $opt: compiled"
      if grep -q "addrspacecast" "$OUT/priv/mcs251_libc$opt.ll"; then
        echo "NOTE: mcs251_libc.c $opt now contains an addrspacecast; the" \
             "private slot ABI changed and the debt entry must be re-read" >&2
      fi
    else
      echo "private runtime mcs251_libc.c $opt: llc refused (known constraint band)"
      sed -n '1,3p' "$OUT/priv/mcs251_libc$opt.log" | sed 's/^/  /'
    fi
  else
    echo "private runtime mcs251_libc.c $opt: clang refused"
    sed -n '1,3p' "$OUT/priv/mcs251_libc$opt.log" | sed 's/^/  /'
  fi
done

# The private-ABI firmware: setter + integer-slot round-trip with __code
# sources, linked against the runtime at the SAME level.  The byte checker
# runs over the private image too (mode priv), so its CODE payloads and its
# literal provenance are verified, not just its serial output.
refuse=0
for opt in -O0 -O2; do
  if [ ! -f "$OUT/priv/mcs251_libc$opt.o" ]; then
    echo "private ABI $opt: runtime object unavailable; not run"
    refuse=1
    continue
  fi
  if "$CLANG" --target=mcs251-unknown-none -std=c11 "$opt" -fmcs251-keil \
      -Wall -Wextra -Werror -I"$SRC" -I"$DIALECT_INC" \
      -Xclang -mcs251-memory-contract="$CONTRACT" \
      -S -emit-llvm "$SRC/a3-priv-fw.c" -o "$OUT/priv/a3-priv$opt.ll" \
      2> "$OUT/priv/a3-priv$opt.log" \
     && "$LLC" -mtriple=mcs251 -mcs251-memory-contract="$CONTRACT" \
          -mcs251-object-format=elf -filetype=obj \
          "$OUT/priv/a3-priv$opt.ll" -o "$OUT/priv/a3-priv$opt.o" \
          2>> "$OUT/priv/a3-priv$opt.log"; then
    # Alice review R10-3 (driver half): the private chain got no IR gates at
    # all, unlike the shim chain.  Apply the same source/conversion
    # assertions per level: no bitcast, an explicit AS4 -> AS0 addrspacecast
    # at the setter call sites, and no integer-laundered AS0 -> AS4 path
    # (inttoptr to an AS4 pointer without an addrspacecast).  The setter's
    # own `(uint32_t)(uintptr_t)src` transport is the recorded debt and is
    # NOT used to manufacture an AS4 pointer here.
    if grep -q "bitcast ptr addrspace(4)" "$OUT/priv/a3-priv$opt.ll"; then
      echo "FAIL: bitcast used for an AS4 conversion in private ABI at $opt" >&2
      exit 1
    fi
    if grep -qE "inttoptr i[0-9]+ %?[A-Za-z0-9._]* to ptr addrspace\(4\)" \
        "$OUT/priv/a3-priv$opt.ll"; then
      echo "FAIL: integer-laundered AS0 -> AS4 path in private ABI at $opt" >&2
      exit 1
    fi
    if ! grep -qE "addrspacecast \(ptr addrspace\(4\) @priv_(msg|pat) to ptr\)" \
        "$OUT/priv/a3-priv$opt.ll"; then
      echo "FAIL: private ABI $opt lacks the explicit AS4 -> AS0" \
           "addrspacecast at the setter call sites" >&2
      exit 1
    fi
    # The same dataflow laundering check the shim chain uses, so the private
    # chain's AS4 -> AS0 path is proven free of integer round-trips too.
    python3 "$ROOT/a3-as-launder-check.py" "$OUT/priv/a3-priv$opt.ll" \
      --what "priv-fw$opt" || exit 1
    echo "private ABI $opt: IR gates OK (addrspacecast present, no launder)"
    "$LLD" -flavor mcs251 "${AREA_ARGS[@]}" --keep-symbols \
      --map="$OUT/priv/a3-priv$opt.map" -o "$OUT/priv/a3-priv$opt.elf" \
      "$OUT/priv/a3-priv$opt.o" "$OUT/priv/mcs251_libc$opt.o" "$OUT/crt.o"
    "$OBJCOPY" -O ihex "$OUT/priv/a3-priv$opt.elf" "$OUT/priv/a3-priv$opt.hex"
    if ! python3 "$ROOT/a3-libc-bytes.py" "$OUT/priv/a3-priv$opt.elf" \
        "$OUT/priv/a3-priv$opt.map" --mode priv; then
      echo "private ABI $opt: byte chain FAIL" >&2
      exit 1
    fi
    echo "private ABI $opt: link + byte chain OK"
  else
    echo "private ABI $opt: build refused (recorded; see $OUT/priv/a3-priv$opt.log)"
    sed -n '1,3p' "$OUT/priv/a3-priv$opt.log" | sed 's/^/  /'
    refuse=1
    continue
  fi
  if [ "${1:-}" != "--no-qemu" ]; then
    serial="$OUT/priv/a3-priv$opt.serial"
    : > "$serial"
    ( for _ in $(seq 1 120); do
        if [ -s "$serial" ] && grep -q "A3-PRIV-" "$serial"; then
          pkill -TERM -f "qemu-system-mcs251.*a3-priv$opt" 2>/dev/null
          exit 0
        fi
        sleep 0.5
      done ) &
    watcher=$!
    timeout --foreground 60 "$QEMU" -M "$MACHINE" -bios "$OUT/priv/a3-priv$opt.hex" \
      -accel tcg -display none -monitor none -serial "file:$serial" \
      > "$OUT/priv/a3-priv$opt.qemu.stdout" 2> "$OUT/priv/a3-priv$opt.qemu.stderr" || true
    kill "$watcher" 2>/dev/null || true
    wait "$watcher" 2>/dev/null || true
    echo "== QEMU(private ABI, $opt) transcript =="
    cat "$serial"
    if ! grep -q '^OK1 OK2 OK3 OK4 $' "$serial"; then
      echo "A3 private-ABI e2e($opt): FAIL (byte check not all OK)" >&2
      refuse=1
      continue
    fi
    if ! grep -q '^A3-PRIV-PASS$' "$serial"; then
      echo "A3 private-ABI e2e($opt): FAIL (see $serial)" >&2
      refuse=1
      continue
    fi
    echo "A3 private-ABI e2e($opt): PASS"
  fi
done

echo "private runtime: setter/uint32-slot path exercised; NON-REENTRANT and"
echo "ISR-interleaving limits and the -O2 branch-range constraint are recorded"
echo "as pre-existing technical debt, NOT as AS-conversion evidence."

echo "A3 libc/runtime e2e complete"
exit $refuse
