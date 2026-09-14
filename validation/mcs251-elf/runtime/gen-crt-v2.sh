#!/usr/bin/env bash
# gen-crt-v2.sh - build the pure ELF MCS251 crt object in EITHER identity
# (A4 W7, design section 6: "the generator must produce both v1 and v2; the
# v1 fixtures do not change").
#
#   --identity v1  assembles the frozen v1 fixture (crt-selfstart.yaml /
#                  crt-irq.yaml) and asserts the original EF_MCS251_ABI_V1
#                  word.  The bytes are identical to what gen-crt-elf.sh /
#                  gen-crt-irq.sh produce; this path exists so ONE script
#                  owns both generations and the pair stays diffable.
#   --identity v2  assembles the v2 twin (crt-selfstart-v2.yaml /
#                  crt-irq-v2.yaml) and rewrites the ELF header e_flags word
#                  to 0x00000102 after yaml2obj (yaml2obj registers only the
#                  EF_MCS251_ABI_V1 enum for EM_MCS251 and rejects raw flag
#                  values; same technique as lld/test/MCS251/
#                  v2-object-identity.test setflags.py).  The smoke check
#                  then asserts the v2 word, exactly one .mcs251.attributes
#                  and NO .note.mcs251.abi.
#
# The v2 object carries the same HOME/VECS/BOOT/BSEG_BYTES/.mcs251.isr bytes,
# relocations and symbols as its v1 twin (design 6: preserve startup order,
# vectors, stack initialization, segment boundaries and entry); only the
# identity differs.  Full acceptance is check-crt-v2.py + lld/test/MCS251/
# crt-v2.test.
#
# Discipline inherited from gen-crt-irq.sh: no default tool paths, no default
# output, nothing is ever written into the repository; the frozen v1
# fixtures are only ever read.
#
# Usage:
#   gen-crt-v2.sh --yaml2obj PATH --readobj PATH --variant selfstart|irq \
#                 --identity v1|v2 --out OUT.o
set -eu
DIR="$(cd "$(dirname "$0")" && pwd)"
YAML2OBJ=""
READOBJ=""
VARIANT=""
IDENTITY=""
OUT=""

usage() {
  echo "usage: gen-crt-v2.sh --yaml2obj PATH --readobj PATH --variant selfstart|irq --identity v1|v2 --out OUT.o" >&2
}

while [ $# -gt 0 ]; do
  case "$1" in
    --yaml2obj) [ $# -ge 2 ] || { usage; exit 2; }; YAML2OBJ="$2"; shift 2 ;;
    --readobj)  [ $# -ge 2 ] || { usage; exit 2; }; READOBJ="$2"; shift 2 ;;
    --variant)  [ $# -ge 2 ] || { usage; exit 2; }; VARIANT="$2"; shift 2 ;;
    --identity) [ $# -ge 2 ] || { usage; exit 2; }; IDENTITY="$2"; shift 2 ;;
    --out)      [ $# -ge 2 ] || { usage; exit 2; }; OUT="$2"; shift 2 ;;
    *) echo "gen-crt-v2.sh: unknown argument: $1" >&2; usage; exit 2 ;;
  esac
done

[ -n "$YAML2OBJ" ] && [ -n "$READOBJ" ] && [ -n "$OUT" ] || {
  echo "gen-crt-v2.sh: --yaml2obj, --readobj and --out are all required" >&2
  usage; exit 2
}
[ "$VARIANT" = "selfstart" ] || [ "$VARIANT" = "irq" ] || {
  echo "gen-crt-v2.sh: --variant must be selfstart or irq, got '$VARIANT'" >&2; exit 2
}
[ "$IDENTITY" = "v1" ] || [ "$IDENTITY" = "v2" ] || {
  echo "gen-crt-v2.sh: --identity must be v1 or v2, got '$IDENTITY'" >&2; exit 2
}
command -v "$YAML2OBJ" >/dev/null || { echo "yaml2obj missing: $YAML2OBJ" >&2; exit 1; }
command -v "$READOBJ" >/dev/null || { echo "llvm-readobj missing: $READOBJ" >&2; exit 1; }

if [ "$IDENTITY" = "v1" ]; then
  SRC="$DIR/crt-$VARIANT.yaml"
else
  SRC="$DIR/crt-$VARIANT-v2.yaml"
fi
[ -f "$SRC" ] || { echo "fixture missing: $SRC" >&2; exit 1; }

OUT_DIR="$(dirname "$OUT")"
[ -d "$OUT_DIR" ] || {
  echo "gen-crt-v2.sh: output directory does not exist (not created silently): $OUT_DIR" >&2
  exit 1
}
[ ! -e "$OUT" ] || [ -f "$OUT" ] || { echo "output is not a regular file: $OUT" >&2; exit 1; }

"$YAML2OBJ" "$SRC" -o "$OUT"

# v2: rewrite e_flags (ELF32 big-endian header offset 36) to the v2 word.
# Everything else in the file is untouched by this patch.
if [ "$IDENTITY" = "v2" ]; then
  python3 - "$OUT" <<'PYEOF'
import pathlib, sys
OFF = 36                       # ELF32 e_flags offset, big-endian
EF_V2 = bytes.fromhex("00000102")
p = pathlib.Path(sys.argv[1])
b = bytearray(p.read_bytes())
assert len(b) >= OFF + 4, "object shorter than the ELF32 header"
assert bytes(b[OFF:OFF + 4]) == bytes.fromhex("00000001"), \
    "yaml2obj emitted an unexpected e_flags word"
b[OFF:OFF + 4] = EF_V2
p.write_bytes(bytes(b))
PYEOF
fi

# Acceptance smoke (full fingerprint: llvm-readobj --file-headers --sections
# --symbols --relocations "$OUT").  All header fingerprints are asserted
# individually; the v2 branch additionally asserts the identity exclusivity
# that lld will enforce (exactly one carrier, no v1 note).
HDR="$("$READOBJ" --file-headers "$OUT")"
printf '%s\n' "$HDR" | grep -q "Type: Relocatable" \
  || { echo "header fingerprint mismatch: Type is not Relocatable (ET_REL)" >&2; exit 1; }
printf '%s\n' "$HDR" | grep -q "Machine: EM_MCS251" \
  || { echo "header fingerprint mismatch: Machine is not EM_MCS251" >&2; exit 1; }
if [ "$IDENTITY" = "v1" ]; then
  printf '%s\n' "$HDR" | grep -q "EF_MCS251_ABI_V1" \
    || { echo "header fingerprint mismatch: EF_MCS251_ABI_V1 missing" >&2; exit 1; }
  printf 'generated v1: %s (%s bytes)\n' "$OUT" "$(wc -c < "$OUT")"
else
  printf '%s\n' "$HDR" | grep -q "Flags \[ (0x102)" \
    || { echo "header fingerprint mismatch: e_flags is not the v2 word 0x102" >&2; exit 1; }
  SECS="$("$READOBJ" --sections "$OUT")"
  # grep -c exits 1 on a zero count; neutralize that so the assignment cannot
  # abort the script under set -e (the count itself is what is asserted next).
  CARRIERS="$(printf '%s\n' "$SECS" | grep -c "Name: .mcs251.attributes" || true)"
  NOTES="$(printf '%s\n' "$SECS" | grep -c "Name: .note.mcs251.abi" || true)"
  [ "$CARRIERS" = "1" ] || { echo "identity exclusivity: expected exactly one .mcs251.attributes, saw $CARRIERS" >&2; exit 1; }
  [ "$NOTES" = "0" ] || { echo "identity exclusivity: v2 object carries a v1 .note.mcs251.abi" >&2; exit 1; }
  printf 'generated v2: %s (%s bytes, e_flags 0x102, one carrier, no v1 note)\n' \
    "$OUT" "$(wc -c < "$OUT")"
fi
