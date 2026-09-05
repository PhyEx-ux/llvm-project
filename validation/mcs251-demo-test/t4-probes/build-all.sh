#!/bin/bash
# T4 probe suite: build + run + check all 10 bare-asm probes against their
# .expect serial patterns.  These probes guard the test system's assumptions
# about QEMU stc32g144k246 behavior (DESIGN.md section 2, T4).
#
# Tool overrides (env): SDAS, SDCC, QEMU, TIMEOUT (seconds, default 5).
# Intermediates go to ./build/ (safe to delete).  Sources and .expect files
# are the only checked-in artifacts besides this script and README.md.
#
# Per-probe verdict:
#   1. sdas251 assembles NAME.asm -> build/NAME.rel
#   2. sdcc driver links (ASlink/sdld backend) -> build/NAME.hex
#      NOTE: the sdcc driver can exit 0 even when ASlink fails, so the .hex
#      must exist before QEMU is started (this is asserted explicitly).
#   3. QEMU runs the .hex (its loader only accepts the .hex suffix) with the
#      serial port on stdio; the probes spin forever after PASS/FAIL, so the
#      timeout kill (rc 124) is expected and is NOT part of the verdict.
#   4. Verdict from the serial transcript only: first line must match the
#      NAME.expect pattern ('X' = timing-variable hex digit), a line "PASS"
#      must be present, and "FAIL" must not appear anywhere.
#
# g_uartrx additionally needs one input byte piped into the UART ('Z').
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
BUILD="$HERE/build"
SDAS=${SDAS:-/home/liu/build-sdcc/bin/sdas251}
SDCC=${SDCC:-/home/liu/build-sdcc/bin/sdcc}
QEMU=${QEMU:-/home/liu/build-qemu/qemu-system-mcs251}
TIMEOUT=${TIMEOUT:-5}
mkdir -p "$BUILD"

# Per-probe ASlink area-base specs (passed through as sdcc -Wl-b options).
linkspecs() {
  case "$1" in
    d1_tint)    echo "VEC=0xff000b PROBE=0xff0100" ;;
    d2_uint)    echo "VEC=0xff0023 PROBE=0xff0100" ;;
    d3_extint)  echo "VEC=0xff0003 PROBE=0xff0008" ;;
    d4_t1int1)  echo "VEC1=0xff0013 VEC2=0xff001b PROBE=0xff0100" ;;
    *)          echo "" ;;
  esac
}

build_one() {
  local n=$1
  rm -f "$BUILD/$n.rel" "$BUILD/$n.hex" "$BUILD/$n.ihx" "$BUILD/$n.out" \
        "$BUILD/$n.lst" "$BUILD/$n.rst" "$BUILD/$n.sym" "$BUILD/$n.map" \
        "$BUILD/$n.mem" "$BUILD/$n.lk" "$BUILD/$n.adb" "$BUILD/$n.cdb"
  "$SDAS" -plosgffw -o "$BUILD/$n.rel" "$HERE/$n.asm" || return 1
  local wlargs=() spec
  for spec in $(linkspecs "$n"); do wlargs+=("-Wl-b $spec"); done
  # '-Wl-b AREA=0xaddr' must reach sdcc as ONE argv element containing the
  # space; splitting it makes ASlink reject the area spec (error 119) while
  # the sdcc driver still exits 0.
  ( cd "$BUILD" && "$SDCC" -mmcs251 --nostdlib --no-xinit-opt \
      --code-loc 0xff0000 "${wlargs[@]}" -o "$n.hex" "$n.rel" ) >/dev/null 2>&1
  [ -f "$BUILD/$n.hex" ]   # sdcc may return 0 despite an ASlink failure
}

run_one() {
  local n=$1
  if [ "$n" = g_uartrx ]; then
    printf "Z" | timeout "$TIMEOUT" "$QEMU" -M stc32g144k246 \
      -bios "$BUILD/$n.hex" -accel tcg -display none -monitor none \
      -serial stdio > "$BUILD/$n.out" 2>&1
  else
    timeout "$TIMEOUT" "$QEMU" -M stc32g144k246 -bios "$BUILD/$n.hex" \
      -accel tcg -display none -monitor none -serial stdio \
      > "$BUILD/$n.out" 2>&1
  fi
}

# check_one <name>: verdict from transcript + .expect pattern.  Prints
# "PASS"/"FAIL <reason>" and returns 0/1.
check_one() {
  local n=$1
  [ -f "$BUILD/$n.out" ] || { echo "FAIL no-transcript"; return 1; }
  python3 - "$BUILD/$n.out" "$HERE/$n.expect" <<'PYEOF'
import re, sys
out_path, exp_path = sys.argv[1], sys.argv[2]
raw = open(out_path, "rb").read().decode("utf-8", "replace")
# serial text only: strip CR, drop QEMU's own stderr lines
lines = [l for l in raw.replace("\r", "").split("\n")
         if l and not l.startswith("qemu-system-mcs251")]
text = "\n".join(lines)
if "FAIL" in text:
    print("FAIL transcript-contains-FAIL"); sys.exit(1)
if "PASS" not in text.split("\n"):
    print("FAIL no-PASS-line"); sys.exit(1)
exp = [l.rstrip("\n") for l in open(exp_path) if l.strip()]
pat = exp[0]
regex = "^" + "".join("." if c == "X" else re.escape(c) for c in pat) + "$"
if not lines or not re.match(regex, lines[0]):
    got = lines[0] if lines else "<empty>"
    print("FAIL evidence-mismatch pattern=%s got=%s" % (pat, got)); sys.exit(1)
print("PASS"); sys.exit(0)
PYEOF
}

PROBES="a_timer b_gpio c_uart234 d1_tint d2_uint d3_extint d4_t1int1 e_absent f_t234_presc g_uartrx"
rc=0
printf "%-14s %-28s %s\n" "probe" "verdict" "serial-line1"
for n in $PROBES; do
  if ! build_one "$n"; then
    printf "%-14s %-28s\n" "$n" "FAIL build"; rc=1; continue
  fi
  run_one "$n"
  verdict=$(check_one "$n") || true
  case "$verdict" in PASS) ;; *) rc=1 ;; esac
  line1=$(tr -d '\r' < "$BUILD/$n.out" | grep -v '^qemu-system-mcs251' | head -1)
  printf "%-14s %-28s %s\n" "$n" "$verdict" "$line1"
done
echo "=="
if [ "$rc" -eq 0 ]; then echo "t4-probes: ALL PASS"; else echo "t4-probes: FAILURES PRESENT"; fi
exit "$rc"
