#!/bin/sh
# validate.sh -- de-SDCC Step 3 acceptance run for mcs251_ld.py.
#
# Reproduces the full acceptance matrix against sdld on the same inputs:
#   1. four link groups (probe / matrix-O0 obj / matrix-O2 obj / #_sym
#      immediate asm module), comparing memory images byte-for-byte
#      against sdld's .ihx;
#   2. QEMU serial equality with the sdld chain (BPASS level);
#   3. strict-ABI tamper cases (module O edited / removed, -A edited);
#   4. an undefined-symbol link.
#
# Run inside WSL:  sh /mnt/c/Prj/LLVM/MCS251/validation/mcs251-ld/validate.sh
# Requires: sdas251+sdld (/home/liu/build-sdcc/bin), llc
# (/home/liu/build-mcs251/bin), qemu-system-mcs251
# (/home/liu/build-qemu), and the firmware .rel set in /tmp/mcs251-fw
# (see validation/mcs251-firmware/README.md to rebuild it).

set -u
FW=/mnt/c/Prj/LLVM/MCS251/validation/mcs251-firmware
MLD=/mnt/c/Prj/LLVM/MCS251/validation/mcs251-ld/mcs251_ld.py
SDCCBIN=${SDCCBIN:-/home/liu/build-sdcc/bin}
LLC=${LLC:-/home/liu/build-mcs251/bin/llc}
QEMU=${QEMU:-/home/liu/build-qemu/qemu-system-mcs251}
FWCACHE=${FWCACHE:-/tmp/mcs251-fw}   # crt0.rel harness.rel provider.rel matrix*.rel
W=${W:-/tmp/mcs251-fw2}
mkdir -p "$W"

SIG="stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1"

fail=0
note() { echo "[validate] $*"; }

# --- inputs ---------------------------------------------------------------
cp /mnt/c/Prj/LLVM/MCS251/validation/mcs251-smoke/probe.ll "$W/probe.ll"
"$LLC" -mtriple=mcs251-unknown-none -filetype=obj -o "$W/probe.rel" "$W/probe.ll"
: > "$W/probe.lst"
cp "$FWCACHE/matrix.ll" "$W/matrix.ll" 2>/dev/null || true
"$LLC" -mtriple=mcs251-unknown-none -O2 -filetype=obj -o "$W/matrix-o2.rel" "$W/matrix.ll"
: > "$W/matrix-o2.lst"

cat > "$W/imm.asm" <<ASM
        .module mcs251_fw_imm
        .source
        .optsdcc $SIG
        .globl _imm_ret
        .globl _imm_dptr
        .globl _p13_mem
        .area CSEG (CODE)
_imm_ret:
        mov     dpl,#(_p13_mem+1)
        eret
_imm_dptr:
        mov     dptr,#_p13_mem
        eret
ASM
"$SDCCBIN/sdas251" -plosgffw -o "$W/imm.rel" "$W/imm.asm"

cat > "$W/probe-harness.c" <<EOF
#include "$FW/harness-template.c"
EOF
cat > "$W/imm-harness.c" <<EOF
typedef unsigned char u8;
extern u8 imm_ret(void); extern u8 imm_dptr(void); extern void p13_init(void);
#define MCS251_CHECKPOINTS() do { \\
 p13_init(); UART_PUTC('i'); harness_check_u8(0x21,imm_ret()); \\
 UART_PUTC('d'); harness_check_u8(0x20,imm_dptr()); } while (0)
#include "$FW/harness-template.c"
EOF

mklk() { # name module.rel harness.c
  cpp -P -undef -nostdinc "$3" > "$W/$1-harness.i"
  "$SDCCBIN/sdcc" -mmcs251 --c1mode -o "$W/$1-harness.asm" < "$W/$1-harness.i"
  "$SDCCBIN/sdas251" -plosgffw -o "$W/$1-harness.rel" "$W/$1-harness.asm"
  sed -e "s|@OUTPUT_IHX@|$W/$1-ours|" -e "s|@CRT0_REL@|$FWCACHE/crt0.rel|" \
      -e "s|@HARNESS_REL@|$W/$1-harness.rel|" -e "s|@MODULE_REL@|$2|" \
      -e "s|@PROVIDER_REL@|$FWCACHE/provider.rel|" \
      "$FW/link-template.lk" > "$W/$1.lk"
}

mklk probe "$W/probe.rel" "$W/probe-harness.c"
mklk obj "$FWCACHE/matrix-obj.rel" "$FWCACHE/matrix-harness.c"
mklk o2 "$W/matrix-o2.rel" "$FWCACHE/matrix-harness.c"
mklk imm "$W/imm.rel" "$W/imm-harness.c"

# --- 1+2: image equality + QEMU serial equality ---------------------------
for n in probe obj o2 imm; do
  sed "s|$W/$n-ours|$W/$n-sdld|" "$W/$n.lk" > "$W/$n-sdld.lk"
  "$SDCCBIN/sdld" --mcs251-abi -r -nf "$W/$n-sdld.lk" > "$W/$n-sdld.log" 2>&1
  cp "$W/$n-sdld.ihx" "$W/$n-sdld.hex"
  python3 "$MLD" --mcs251-abi -f "$W/$n.lk" > "$W/$n-ours.log" 2>&1
  res=$(python3 - "$W" "$n" <<'EOF'
import sys
def load(p):
    img, hi = {}, 0
    for line in open(p):
        line = line.strip()
        if not line.startswith(":"):
            continue
        b = bytes.fromhex(line[1:])
        n, a, t = b[0], (b[1] << 8) | b[2], b[3]
        if t == 4:
            hi = ((b[4] << 8) | b[5]) << 16
        elif t == 0:
            for i in range(n):
                img[hi + a + i] = b[4 + i]
        elif t == 1:
            break
    return img
w, n = sys.argv[1], sys.argv[2]
a = load("%s/%s-sdld.ihx" % (w, n))
b = load("%s/%s-ours.hex" % (w, n))
d = [k for k in set(a) | set(b) if a.get(k) != b.get(k)]
print("EQUIVALENT sdld=%d mine=%d" % (len(a), len(b)) if not d
      else "MISMATCH %d diffs e.g. %s" % (len(d), [hex(k) for k in d[:6]]))
EOF
)
  case "$res" in EQUIVALENT*) ;; *) fail=1 ;; esac
  note "image $n: $res"
  for side in ours sdld; do
    timeout 30 "$QEMU" -M stc32g144k246 -bios "$W/$n-$side.hex" -accel tcg \
      -icount shift=0,align=off,sleep=off -display none -monitor none \
      -serial stdio < /dev/null > "$W/$n-$side-serial.raw" 2>&1
  done
  ours=$(tr -d "\r\n" < "$W/$n-ours-serial.raw" | sed "s/qemu-system.*//")
  ref=$(tr -d "\r\n" < "$W/$n-sdld-serial.raw" | sed "s/qemu-system.*//")
  case "$ours$ref" in *FAIL*) fail=1;; esac
  if [ "$ours" = "$ref" ]; then
    note "qemu   $n: serial IDENTICAL [$ours]"
  else
    note "qemu   $n: serial DIFF ours=[$ours] sdld=[$ref]"; fail=1
  fi
done

# --- 3: strict ABI tampering ----------------------------------------------
tamper() { # case-name module-sed [lk-sed]
  sed "$2" "$W/probe-harness.rel" > "$W/tamper.rel"
  sed -e "s|@OUTPUT_IHX@|$W/$1|" -e "s|@CRT0_REL@|$FWCACHE/crt0.rel|" \
      -e "s|@HARNESS_REL@|$W/tamper.rel|" -e "s|@MODULE_REL@|$W/probe.rel|" \
      -e "s|@PROVIDER_REL@|$FWCACHE/provider.rel|" \
      "$FW/link-template.lk" > "$W/$1.lk"
  [ -n "${2:-}" ] && true
  if [ -n "${3:-}" ]; then sed -i "$3" "$W/$1.lk"; fi
  python3 "$MLD" --mcs251-abi -f "$W/$1.lk" > "$W/$1.log" 2>&1
  rc=$?
  note "$1: ours-exit=$rc log: $(grep -m1 "ASlink-Error" "$W/$1.log")"
  [ "$rc" -ne 0 ] || fail=1
}
tamper abi-mismatch "s/model=small/model=large/"
tamper abi-missing "/^O /d"
tamper abi-bad-a "s/^O .*/O x/" "s/abi-minor=0/abi-minor=9/"

# --- 4: undefined symbol ----------------------------------------------------
cat > "$W/undef.ll" <<'LL'
target triple = "mcs251-unknown-none"
declare i8 @_missing_thing(i8)
define i8 @_mcs251_probe() {
entry:
  %r = call i8 @_missing_thing(i8 7)
  ret i8 %r
}
LL
"$LLC" -mtriple=mcs251-unknown-none -filetype=obj -o "$W/undef.rel" "$W/undef.ll"
: > "$W/undef.lst"
sed -e "s|@OUTPUT_IHX@|$W/undef|" -e "s|@CRT0_REL@|$FWCACHE/crt0.rel|" \
    -e "s|@HARNESS_REL@|$W/probe-harness.rel|" -e "s|@MODULE_REL@|$W/undef.rel|" \
    -e "s|@PROVIDER_REL@|$FWCACHE/provider.rel|" \
    "$FW/link-template.lk" > "$W/undef.lk"
python3 "$MLD" --mcs251-abi -f "$W/undef.lk" > "$W/undef.log" 2>&1
rc=$?
note "undefined-symbol: ours-exit=$rc log: $(grep -m1 "Undefined Global" "$W/undef.log")"
[ "$rc" -ne 0 ] || fail=1

note "RESULT: $([ $fail -eq 0 ] && echo ALL-PASS || echo FAILURES-PRESENT)"
exit $fail
