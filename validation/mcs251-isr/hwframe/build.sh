#!/usr/bin/env bash
# Usage: bash build.sh [new-output-directory] [hardware|qemu]
# Hardware profile: STC32G12K128, ISP IRC=24MHz, UART1 P3.0/P3.1 115200 8N1.
set -euo pipefail
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OUT=${1:-$(mktemp -d /tmp/mcs251-hwframe-v2.XXXXXX)}
PROFILE=${2:-hardware}
CLANG=${CLANG:-/home/liu/build-mcs251-s1/bin/clang}
LLC=${LLC:-/home/liu/build-mcs251-s1/bin/llc}
SDAS251=${SDAS251:-/home/liu/build-sdcc/bin/sdas251}
SDLD=${SDLD:-/home/liu/build-sdcc/bin/sdld}
case "$PROFILE" in
  hardware) DEFINES=() ;;
  qemu) DEFINES=(-DHWF_QEMU) ;;
  *) printf 'unknown profile: %s\n' "$PROFILE" >&2; exit 1 ;;
esac
mkdir -p -- "$OUT"
OUT=$(cd -- "$OUT" && pwd)
if compgen -G "$OUT/*" >/dev/null; then
  printf 'output directory must be empty: %s\n' "$OUT" >&2
  exit 1
fi
"$CLANG" --target=mcs251-unknown-none -std=c11 -O2 -Wall -Wextra -Werror \
  -Xclang -mcs251-memory-contract=1,1,32,8,1 "${DEFINES[@]}" \
  -S -emit-llvm "$HERE/report.c" -o "$OUT/report.ll"
"$LLC" -mtriple=mcs251-unknown-none -verify-machineinstrs \
  -mcs251-memory-contract=1,1,32,8,1 -filetype=obj \
  "$OUT/report.ll" -o "$OUT/report.rel"
"$LLC" -mtriple=mcs251-unknown-none -mcs251-memory-contract=1,1,32,8,1 \
  "$OUT/report.ll" -o "$OUT/report.asm"
"$SDAS251" -los "$OUT/hwframe.rel" "$HERE/hwframe.asm"
python3 - "$HERE" "$OUT" <<'PY'
from pathlib import Path
import sys
here, out = map(Path, sys.argv[1:])
s = (here / 'link-hwframe.lk').read_text()
for key, value in {'OUTPUT_IHX': out/'hwframe.ihx',
                   'HWFRAME_REL': out/'hwframe.rel',
                   'REPORT_REL': out/'report.rel'}.items():
    s = s.replace('@'+key+'@', str(value))
(out/'link.lk').write_text(s)
PY
"$SDLD" -f "$OUT/link.lk"
if test -f "$OUT/hwframe.ihx"; then mv -- "$OUT/hwframe.ihx" "$OUT/hwframe.hex"; fi
printf 'built (%s): %s/hwframe.hex\n' "$PROFILE" "$OUT"
