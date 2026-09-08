#!/usr/bin/env bash
# run-rt-acceptance.sh — mcs251rt 运行时验收驱动（合法语义固件 + UB 观察固件）
#
# 规范依据：除法设计 v4 §8.2/§8.4/§8.3；SPEC 2026-09-07 修正案 §3/§6。
# 板参数沿用 E4 chain-B（--area-start 五项 + --edata-end 0x3fff）。
# QEMU 无 icount（用户口径：不用 -icount）。
set -u
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

ACC=/home/liu/mcs251-rt-acceptance
REPO=/mnt/c/Prj/LLVM/MCS251
SRC=$REPO/validation/mcs251-elf/runtime/acceptance-host
CLANG=/home/liu/build-mcs251-s1/bin/clang
LLC=/home/liu/build-mcs251-s1/bin/llc
LLD=/home/liu/build-mcs251-lld/bin/mcs251-lld
OBJCOPY=/home/liu/build-mcs251-s1/bin/llvm-objcopy
QEMU=/home/liu/build-qemu/qemu-system-mcs251
MACHINE=stc32g144k246
CRT=$ACC/crt-selfstart.o
HOSTCC=/usr/bin/cc

LOG=$ACC/qemu
mkdir -p "$LOG"

fail=0
step() { echo "== $* =="; }

# ---------- 宿主 Oracle-A ----------
step "host oracle"
$HOSTCC -std=c11 -O2 -Wall -Wextra "$SRC/host_expect.c" -o "$LOG/host_expect" 2> "$LOG/host-build.stderr" || { echo HOST-BUILD-FAIL; fail=1; }
"$LOG/host_expect" > "$LOG/host.expected" 2> "$LOG/host.run.stderr" || { echo HOST-RUN-FAIL; fail=1; }
echo "host expected lines: $(wc -l < "$LOG/host.expected")"

# ---------- 固件编译（clang -> llc，与运行时同一管道） ----------
compile_mod() { # name src extra...
  local name=$1; shift
  local src=$1; shift
  $CLANG --target=mcs251-unknown-none -std=c11 -O2 -I"$SRC" -S -emit-llvm "$src" -o "$LOG/$name.ll" 2> "$LOG/$name.clang.err" || return 1
  $LLC -mtriple=mcs251 -O2 -mcs251-object-format=elf -filetype=obj "$LOG/$name.ll" -o "$LOG/$name.o" 2> "$LOG/$name.llc.err" || return 1
}

step "compile rt firmware (legal)"
compile_mod rt_fw "$SRC/rt_firmware.c" || { echo RT-FW-COMPILE-FAIL; fail=1; }

step "compile ub observe firmware + probe"
compile_mod ub_ob "$SRC/ub_observe.c" || { echo UB-COMPILE-FAIL; fail=1; }
$LLC -mtriple=mcs251 -O2 -mcs251-object-format=elf -filetype=obj "$SRC/ub_probe.ll" -o "$LOG/ub_probe.o" 2> "$LOG/ub_probe.llc.err" || { echo UB-PROBE-LLC-FAIL; fail=1; }

# ---------- CRT（从 E4 冻结 fixture 复制，只读使用） ----------
cp /home/liu/mcs251-elf-e4/objects/elf/crt.o "$CRT"

# ---------- 链接（合法固件：清单序八对象全量；§3.5 无条件参与） ----------
link_fw() { # out objects...
  local out=$1; shift
  $LLD --edata-end 0x3fff \
    --area-start=HOME=0xff0000 --area-start=VECS=0xff0003 --area-start=BOOT=0xff0100 \
    --area-start=CSEG=0xff0200 --area-start=XINIT=0xff8000 \
    --map "$out.map" -o "$out.elf" "$@" > "$out.link.stdout" 2> "$out.link.stderr"
}

step "link legal firmware (crt + rt_fw + 8 objects in manifest order)"
mapfile -t OBJS < "$ACC/mcs251rt.manifest"
link_fw "$LOG/rt_fw" "$LOG/rt_fw.o" "$CRT" "${OBJS[@]}" || { echo RT-FW-LINK-FAIL; cat "$LOG/rt_fw.link.stderr"; fail=1; }

step "link ub observe firmware (independent session)"
link_fw "$LOG/ub_ob" "$LOG/ub_ob.o" "$LOG/ub_probe.o" "$CRT" "${OBJS[@]}" || { echo UB-LINK-FAIL; cat "$LOG/ub_ob.link.stderr"; fail=1; }

step "objcopy to ihex"
$OBJCOPY -O ihex "$LOG/rt_fw.elf" "$LOG/rt_fw.hex" || fail=1
$OBJCOPY -O ihex "$LOG/ub_ob.elf" "$LOG/ub_ob.hex" || fail=1

# ---------- QEMU（无 icount；串口文件 + 哨兵截止 + 超时上限 + 完整终止行检测） ----------
run_qemu() { # hex serial timeout sentinel
  local hex=$1 serial=$2 timeout=$3 sentinel=$4
  : > "$serial"   # 清空陈旧串口文件，防止哨兵误读上一轮终止行 */
  # 哨兵监视：终止行出现即 SIGTERM 收尾（固件尾部死循环与 demo-modern
  # 惯例一致；超时上限仅作兜底，不作为证据——timeout 124 不是证据）。
  ( for i in $(seq 1 $((timeout * 2))); do
      if [ -s "$serial" ] && tail -c 256 "$serial" 2>/dev/null | grep -q "$sentinel"; then
        pkill -TERM -f "qemu-system-mcs251.*$(basename "$hex")" 2>/dev/null
        exit 0
      fi
      sleep 0.5
    done ) &
  local watcher=$!
  timeout --foreground "$timeout" "$QEMU" -M "$MACHINE" -bios "$hex" \
    -accel tcg -display none -monitor none -serial "file:$serial" \
    > "$serial.qemu.stdout" 2> "$serial.qemu.stderr"
  local rc=$?
  kill "$watcher" 2>/dev/null
  wait "$watcher" 2>/dev/null
  echo "qemu rc=$rc"
  return $rc
}

step "QEMU legal firmware"
if run_qemu "$LOG/rt_fw.hex" "$LOG/rt_fw.serial" 120 RT-SEMANTIC-PASS; then
  echo "serial lines: $(wc -l < "$LOG/rt_fw.serial")"
  tail -3 "$LOG/rt_fw.serial"
else
  echo "RT-FW-QEMU-TIMEOUT-OR-FAIL (recorded, not asserted)"
fi

step "QEMU ub observe firmware (independent session, bounded timeout, record only)"
if run_qemu "$LOG/ub_ob.hex" "$LOG/ub_ob.serial" 60 UB-OBSERVE-DONE; then
  cat "$LOG/ub_ob.serial"
else
  echo "UB-QEMU-TIMEOUT-OR-EARLY-EXIT (three-state allowed: value/terminate/timeout)"
  cp "$LOG/ub_ob.serial" "$LOG/ub_ob.serial.partial" 2>/dev/null
  [ -f "$LOG/ub_ob.serial" ] && cat "$LOG/ub_ob.serial" || echo "(no serial output)"
fi

step "three-way comparison (host vs DUT)"
python3 - <<'PYEOF'
import sys
host = open("/home/liu/mcs251-rt-acceptance/qemu/host.expected","rb").read().splitlines()
try:
    dut = open("/home/liu/mcs251-rt-acceptance/qemu/rt_fw.serial","rb").read().splitlines()
except FileNotFoundError:
    print("DUT serial missing"); sys.exit(2)
# strip host tail marker
if host and host[-1] == b"HOST-EXPECT-DONE": host = host[:-1]
# DUT tail
if not dut or not dut[-1].startswith(b"RT-SEMANTIC"):
    print("DUT missing termination line"); sys.exit(2)
term = dut[-1]; dut = dut[:-1]
mism = 0
if len(host) != len(dut):
    print("LINE COUNT MISMATCH host=%d dut=%d" % (len(host), len(dut))); mism += 1
for i,(h,d) in enumerate(zip(host,dut)):
    if h != d:
        mism += 1
        if mism <= 10:
            print("DIFF line %d:\n  host: %s\n  dut : %s" % (i+1, h.decode(errors="replace"), d.decode(errors="replace")))
print("termination line:", term.decode(errors="replace"))
if mism == 0 and term == b"RT-SEMANTIC-PASS":
    print("THREE-WAY-SEMANTIC-PASS (host vs DUT; SDCC oracle separate)")
    sys.exit(0)
sys.exit(1)
PYEOF
rc=$?
echo "compare rc=$rc"
exit $(( fail | (rc==0?0:1) ))
