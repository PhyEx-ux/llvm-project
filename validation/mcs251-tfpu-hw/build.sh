#!/usr/bin/env bash
# Usage: bash build.sh [new-output-directory] [hardware|qemu]
# Hardware profile: STC32G12K128, ISP IRC=24MHz, UART1 P3.0/P3.1 115200 8N1.
#
# Chain (mirrors validation/mcs251-isr/hwframe):
#   clang -> .ll -> llc -> .rel (object) + .asm
#   sdas251 assembles tfpu-probe.asm (with generated sled.inc)
#   sdld links everything at FF:0000 / FF:0200
set -euo pipefail
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OUT=${1:-$(mktemp -d /tmp/mcs251-tfpu-hw.XXXXXX)}
PROFILE=${2:-hardware}
CLANG=${CLANG:-/home/liu/build-mcs251-s1/bin/clang}
LLC=${LLC:-/home/liu/build-mcs251-s1/bin/llc}
SDAS251=${SDAS251:-/home/liu/build-sdcc/bin/sdas251}
SDLD=${SDLD:-/home/liu/build-sdcc/bin/sdld}
case "$PROFILE" in
  hardware) DEFINES=() ;;
  qemu) DEFINES=(-DTPU_QEMU) ;;
  *) printf 'unknown profile: %s\n' "$PROFILE" >&2; exit 1 ;;
esac
mkdir -p -- "$OUT"
OUT=$(cd -- "$OUT" && pwd)
if compgen -G "$OUT/*" >/dev/null; then
  printf 'output directory must be empty: %s\n' "$OUT" >&2
  exit 1
fi

# Regenerate the NOP-sled jump tables from the single source of truth.
python3 "$HERE/gen-sled.py" > "$OUT/sled.inc"

"$CLANG" --target=mcs251-unknown-none -std=c11 -O2 -Wall -Wextra -Werror \
  -Xclang -mcs251-memory-contract=1,1,32,8,1 "${DEFINES[@]}" \
  -S -emit-llvm "$HERE/main.c" -o "$OUT/main.ll"
"$LLC" -mtriple=mcs251-unknown-none -verify-machineinstrs \
  -mcs251-memory-contract=1,1,32,8,1 -filetype=obj \
  "$OUT/main.ll" -o "$OUT/main.rel"
"$LLC" -mtriple=mcs251-unknown-none -mcs251-memory-contract=1,1,32,8,1 \
  "$OUT/main.ll" -o "$OUT/main.asm"

"$SDAS251" -I"$OUT" -los "$OUT/tfpu-probe.rel" "$HERE/tfpu-probe.asm"

python3 - "$HERE" "$OUT" <<'PY'
from pathlib import Path
import sys
here, out = map(Path, sys.argv[1:])
s = (here / 'link-tfpu.lk').read_text()
for key, value in {'OUTPUT_IHX': out/'tfpu-probe.ihx',
                   'PROBE_REL': out/'tfpu-probe.rel',
                   'MAIN_REL': out/'main.rel'}.items():
    s = s.replace('@'+key+'@', str(value))
(out/'link.lk').write_text(s)
PY
"$SDLD" -f "$OUT/link.lk"
if test -f "$OUT/tfpu-probe.ihx"; then mv -- "$OUT/tfpu-probe.ihx" "$OUT/tfpu-probe.hex"; fi
printf 'built (%s): %s/tfpu-probe.hex\n' "$PROFILE" "$OUT"
