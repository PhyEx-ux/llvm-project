#!/usr/bin/env bash
# measure-g7s1pp-size.sh -- same-caliber runtime size measurement (G7 S1'').
#
# Reproduces every size number in IMPL-G7S1PP-PROGRESS.md section
# "尺寸口径重算".  Self-contained: builds the runtime objects and the
# per-routine callers from the repo, then measures.
#
# METHOD (fixed before any number is quoted):
#   The runtime object files carry NO .rodata/.data section.  Every constant
#   -- including the 32-byte de Bruijn table in bitutil -- lives inside
#   `.text`; the only other PROGBITS sections are .rela.text, the note /
#   attributes section and the sym/str tabs, and the alloc slot regions are
#   NOBITS (they occupy no code).  Therefore readelf -S `.text` IS the
#   "code + constants" figure.  `llvm-nm --print-size` sums FUNC symbols
#   only and silently drops a static table -- that is why the old bitutil
#   figure read 2071 instead of 2103.
#
# Calibers:
#   (ii) full library   : sum of `.text` over the runtime objects.
#   (i)  routine closure: drive.py's symbol-intersection closure, then the
#        sum of `.text` of exactly the objects that closure selects; the lld
#        --map is re-checked to confirm it placed the same set.  mcs251-lld
#        has no --gc-sections, so a pulled object contributes its whole
#        `.text` (object granularity, not routine).
set -u
CLANG=${CLANG:-/home/liu/build-mcs251-s1/bin/clang}
LLC=${LLC:-/home/liu/build-mcs251/bin/llc}
LLD=${LLD:-/home/liu/build-mcs251-lld/bin/mcs251-lld}
READELF=${READELF:-/home/liu/build-mcs251-s1/bin/llvm-readelf}
REPO=${REPO:-/home/liu/LLVM_STC32/MCS251}
RT_SRC=$REPO/validation/mcs251-runtime/src
CRT=${CRT:-/home/liu/LLVM_STC32/mcs251-demos-rewritten/tools/crt/v2/crt-selfstart-v2.o}
OUT=${OUT:-/tmp/g7s1pp-size/repro}
BASELINE=${BASELINE:-0}   # 1 = also build the pre-rewrite baseline arith.o + bitutil.o
# The rewrite is now committed, so HEAD no longer holds the baseline.  Pin the
# last pre-rewrite revision (db7f05ae5) -- RUNTIME's "baseline" is the frozen
# pre-S1'' mcs251_float_arith.c, not "whatever HEAD is today".
BASE_REV=${BASE_REV:-db7f05ae5}

# contract: the migrated printf runtime needs the v2 spec (its fmt slot
# cannot lower under the compat spec -- bs4-demo-e2e.sh:94).  The f32
# arithmetic objects measure byte-identical under v1 and v2, so the whole
# script uses v2 for a single link identity.
C2=1,2,32,8,1
AREAS=(--edata-end 0x0fff --area-start=HOME=0xff0000 --area-start=VECS=0xff0003 \
       --area-start=BOOT=0xff0100 --area-start=CSEG=0xfe0000 --area-start=XINIT=0xff8000)
RTS=(mcs251_float_addsub mcs251_float_mul mcs251_float_div \
     mcs251_float_arith mcs251_float_cmp mcs251_bitutil mcs251_printf)

textsize() { "$READELF" -S "$1" | awk '$3==".text"{print strtonum("0x"$7)}'; }

build_obj() { # <src.c> <opt> <contract> <out.o>
  local ll=${4%.o}.ll
  "$CLANG" --target=mcs251-unknown-none -std=c11 -ffreestanding -fno-builtin \
    -DMCS251_RT_TARGET -I"$RT_SRC" -Xclang -mcs251-memory-contract="$3" \
    -"$2" -S -emit-llvm "$1" -o "$ll" || return 1
  "$LLC" -mtriple=mcs251 -mcs251-memory-contract="$3" -"$2" \
    -verify-machineinstrs -mcs251-object-format=elf -filetype=obj \
    "$ll" -o "$4" || return 1
}

mkdir -p "$OUT"; cd "$OUT" || exit 1

echo "== build runtime objects (repo sources, acceptance recipe) =="
# All objects use the v2 spec so the closure link below has one identity.
# The f32 objects' .text is byte-identical under v1 (measured); printf needs
# v2 regardless (its fmt slot cannot lower under the compat spec).
for n in mcs251_float_arith mcs251_float_addsub mcs251_float_mul \
         mcs251_float_div mcs251_float_cmp; do
  build_obj "$RT_SRC/$n.c" O2 "$C2" "$OUT/$n.o" || echo "BUILD-FAIL $n"
done
build_obj "$RT_SRC/mcs251_bitutil.c" O0 "$C2" "$OUT/mcs251_bitutil.o" || echo "BUILD-FAIL bitutil"
build_obj "$RT_SRC/mcs251_printf.c"  O2 "$C2" "$OUT/mcs251_printf.o"  || echo "BUILD-FAIL printf"

echo
echo "== (ii) per-object .text (readelf -S; no .rodata section exists) =="
full=0
for n in "${RTS[@]}"; do
  sz=$(textsize "$OUT/$n.o"); printf '%-24s .text=%6d\n' "$n" "$sz"; full=$((full+sz))
done
printf '%-24s        %6d\n' "FULL-LIBRARY(7) total" "$full"
f5=0; for n in mcs251_float_addsub mcs251_float_mul mcs251_float_div mcs251_float_arith mcs251_bitutil; do
  f5=$((f5+$(textsize "$OUT/$n.o"))); done
printf '%-24s        %6d\n' "FULL-LIBRARY(f32 5) total" "$f5"

if [ "$BASELINE" = "1" ]; then
  echo
  echo "== baseline ($BASE_REV mcs251_float_arith.c + unchanged bitutil) =="
  git -C "$REPO" show "$BASE_REV:validation/mcs251-runtime/src/mcs251_float_arith.c" \
      > "$OUT/base_arith.c"
  git -C "$REPO" show "$BASE_REV:validation/mcs251-runtime/src/mcs251_float.h" \
      > "$OUT/mcs251_float.h"
  # build against the baseline header (kept in $OUT, which precedes -I$RT_SRC
  # only for this call -- pass an explicit include dir instead).
  "$CLANG" --target=mcs251-unknown-none -std=c11 -ffreestanding -fno-builtin \
    -DMCS251_RT_TARGET -I"$OUT" -I"$RT_SRC" \
    -Xclang -mcs251-memory-contract="$C2" -O2 -S -emit-llvm \
    "$OUT/base_arith.c" -o "$OUT/base_arith.ll" || echo "BUILD-FAIL base arith (clang)"
  "$LLC" -mtriple=mcs251 -mcs251-memory-contract="$C2" -O2 \
    -verify-machineinstrs -mcs251-object-format=elf -filetype=obj \
    "$OUT/base_arith.ll" -o "$OUT/base_arith.o" || echo "BUILD-FAIL base arith (llc)"
  b=$(( $(textsize "$OUT/base_arith.o") + $(textsize "$OUT/mcs251_bitutil.o") ))
  printf '%-24s        %6d\n' "BASELINE(arith+bitutil)" "$b"
fi

echo
echo "== build per-routine callers =="
mk() { # <name> <decl> <call>
  cat > "$OUT/c_$1.c" <<EOF
#include <stdint.h>
$2
volatile uint32_t sink;
int main(void){ sink = $3; return 0; }
EOF
  build_obj "$OUT/c_$1.c" O2 "$C2" "$OUT/c_$1.o" || echo "BUILD-FAIL c_$1"
}
mk none     ''                                       '0u'
mk mul      'uint32_t _mulsf3(uint32_t,uint32_t);'   '_mulsf3(0x3F800000u,0x40000000u)'
mk div      'uint32_t _divsf3(uint32_t,uint32_t);'   '_divsf3(0x3F800000u,0x40000000u)'
mk add      'uint32_t _addsf3(uint32_t,uint32_t);'   '_addsf3(0x3F800000u,0x40000000u)'
mk sub      'uint32_t _subsf3(uint32_t,uint32_t);'   '_subsf3(0x3F800000u,0x40000000u)'
mk floats   'uint32_t _floatsisf(int32_t);'          '_floatsisf(-5)'
mk fixs     'uint32_t _fixsfsi(uint32_t);'           '_fixsfsi(0x3F800000u)'
mk sign     'uint32_t _floatunsisf(uint32_t);'       '_floatunsisf(5u)'
mk fixuns   'uint32_t _fixunssfsi(uint32_t);'        '_fixunssfsi(0x3F800000u)'
mk neg      'uint32_t _negsf2(uint32_t);'            '_negsf2(0x3F800000u)'
mk cmp      'int32_t _eqsf2(uint32_t,uint32_t);'     '_eqsf2(1u,2u)'

echo
echo "== (i) symbol-intersection closure (drive.py _runtime_for) + map check =="
FLOAT_RT=(mcs251_float_arith mcs251_float_addsub mcs251_float_mul \
          mcs251_float_div mcs251_float_cmp mcs251_bitutil)

closure_py() { # <caller.o> -> newline-separated selected object stems
python3 - "$1" "$READELF" "$OUT" "${FLOAT_RT[@]}" <<'PY'
import subprocess, sys, os
caller, readelf, outdir = sys.argv[1], sys.argv[2], sys.argv[3]
floats = sys.argv[4:]
def syms(obj):
    d, u = set(), set()
    r = subprocess.run([readelf, "-sW", obj], capture_output=True, text=True).stdout
    for line in r.splitlines():
        p = line.split()
        if len(p) < 8 or not p[0].rstrip(":").isdigit():
            continue
        if p[6] == "UND": u.add(p[7])
        elif p[4] == "GLOBAL": d.add(p[7])
    return d, u
defs, und = set(), set()
d, u = syms(caller); defs |= d; und |= u; und -= defs
used, changed = [], True
while changed:
    changed = False
    for name in floats:
        if name in used: continue
        d, u = syms(os.path.join(outdir, name + ".o"))
        if d & und:
            used.append(name); defs |= d; und = (und | u) - defs; changed = True
print("\n".join(used))
PY
}

for c in none mul div add sub floats fixs sign fixuns neg cmp; do
  [ -f "$OUT/c_$c.o" ] || continue
  mapfile -t pulled < <(closure_py "$OUT/c_$c.o")
  args=(); tot=0; names=""
  for n in "${pulled[@]:-}"; do
    [ -n "$n" ] || continue
    args+=("$OUT/$n.o"); sz=$(textsize "$OUT/$n.o"); tot=$((tot+sz))
    names="$names ${n#mcs251_float_}"
  done
  # link that exact set; re-check the map placed the same objects
  if "$LLD" "${AREAS[@]}" --map "$OUT/m_$c.map" -o "$OUT/cl_$c.elf" \
       "$OUT/c_$c.o" ${args[@]+"${args[@]}"} "$CRT" >/dev/null 2>"$OUT/ld_$c.err"; then
    mapped=0
    for n in "${RTS[@]}"; do
      grep -qF "$n.o:" "$OUT/m_$c.map" && mapped=$((mapped+$(textsize "$OUT/$n.o")))
    done
    printf '%-8s closure=%6d  map-check=%6d  objects:%s\n' "$c" "$tot" "$mapped" "$names"
  else
    printf '%-8s LINK-FAIL %s\n' "$c" "$(head -1 "$OUT/ld_$c.err")"
  fi
done

echo
echo "== --gc-sections support gate =="
"$LLD" --gc-sections -o "$OUT/gc.elf" "$OUT/c_mul.o" "$OUT/mcs251_float_mul.o" "$CRT" \
  2>&1 | head -1
