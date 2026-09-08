#!/usr/bin/env bash
# run-acceptance.sh — mcs251-runtime 软浮点 + 内存原语运行时验收驱动
#
# 范围裁定（PM 转达用户 2026-09-08）：
#   - 软浮点 f32 全套（add/sub/mul/div/cmp/conv/neg）—— 验收
#   - libc 内存原语 5 个（memcpy/strcpy/strlen/memset/memcmp）—— 源码审查，不单独建固件
#   - printf/sprintf/putchar/math.h（sin/cos/tan/exp/log/pow/fabs/floor/ceil）—— 不验收
#   - sqrtf 已 PASS 但属 math.h，本脚本不测（math_firmware.c 保留但不构建）
#
# 退出码（Alice 审查 RT-2 修正，假 PASS 已消除）：
#   0 = 全部通过（ACCEPTANCE-PASS）
#   1 = 任一步失败（编译/链接/QEMU/比较/fuzz），输出 ACCEPTANCE-FAIL
#
# 用法：
#   ./run-acceptance.sh                # 正常验收
#   ./run-acceptance.sh --inject-wrong # 负例自测：向期望文件注入错误数据，
#                                      # 脚本必须在比较步失败并以非零退出
#
# 固件 #1 (arith): 运算+比较+转换，CSEG=0xFE0000，链接 arith+cmp+bitutil
set -u
set -o pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

ACC=/home/liu/mcs251-runtime-acceptance
REPO=/mnt/c/Prj/LLVM/MCS251
SRC=$REPO/validation/mcs251-runtime/acceptance
RTSRC=$REPO/validation/mcs251-runtime/src
CLANG=/home/liu/build-mcs251-s1/bin/clang
LLC=/home/liu/build-roalign/bin/llc
LLD=/home/liu/build-mcs251-lld/bin/mcs251-lld
OBJCOPY=/home/liu/build-mcs251-s1/bin/llvm-objcopy
QEMU=/home/liu/build-qemu/qemu-system-mcs251
MACHINE=stc32g144k246
CRT=/home/liu/mcs251-rt-acceptance/crt-selfstart.o
HOSTCC=/usr/bin/cc

INJECT_WRONG=0
if [ "${1:-}" = "--inject-wrong" ]; then
  INJECT_WRONG=1
  echo "== negative-test mode: WRONG data will be injected =="
fi

LOG=$ACC/qemu
mkdir -p "$LOG"

fail=0
step() { echo "== $* =="; }
bail() { echo "FATAL: $*"; exit 1; }

# ---------- 宿主 fuzz 回归（RT-1/RT-4 全量位级比对） ----------
step "host fuzz regression (add/sub/mul/div/cmp/conv vs host IEEE)"
if $HOSTCC -std=c11 -O2 -I"$RTSRC" "$SRC/host_fuzz.c" \
     "$RTSRC/mcs251_float_arith.c" "$RTSRC/mcs251_float_cmp.c" "$RTSRC/mcs251_bitutil.c" \
     -o "$LOG/host_fuzz" -lm 2> "$LOG/host_fuzz.build.err"; then
  :
else
  echo "HOST-FUZZ-BUILD-FAIL"; cat "$LOG/host_fuzz.build.err"; fail=1
fi
if [ $fail -eq 0 ]; then
  if "$LOG/host_fuzz" > "$LOG/host_fuzz.out" 2>&1; then
    tail -2 "$LOG/host_fuzz.out"
  else
    echo "HOST-FUZZ-FAIL"; head -30 "$LOG/host_fuzz.out"; fail=1
  fi
fi
# UBSan 变体（不可恢复模式，任何 UB 即失败）
if [ $fail -eq 0 ]; then
  if $HOSTCC -std=c11 -O1 -fsanitize=undefined -fno-sanitize-recover=all \
       -I"$RTSRC" "$SRC/host_fuzz.c" \
       "$RTSRC/mcs251_float_arith.c" "$RTSRC/mcs251_float_cmp.c" "$RTSRC/mcs251_bitutil.c" \
       -o "$LOG/host_fuzz_ubsan" -lm 2> "$LOG/host_fuzz_ubsan.build.err"; then
    if "$LOG/host_fuzz_ubsan" > "$LOG/host_fuzz_ubsan.out" 2>&1; then
      tail -1 "$LOG/host_fuzz_ubsan.out"
    else
      echo "HOST-FUZZ-UBSAN-FAIL"; head -30 "$LOG/host_fuzz_ubsan.out"; fail=1
    fi
  else
    echo "HOST-FUZZ-UBSAN-BUILD-FAIL (跳过，非阻断)"  # UBSan 为增强检查，构建失败降级为提示
  fi
fi

# ---------- 编译运行时对象 ----------
step "compile runtime objects"
for f in mcs251_float_arith.c mcs251_float_cmp.c; do
  $CLANG --target=mcs251-unknown-none -std=c11 -DMCS251_RT_TARGET -O2 -I"$RTSRC" -S -emit-llvm "$RTSRC/$f" -o "$LOG/$f.ll" 2> "$LOG/$f.clang.err" || { echo "CLANG-FAIL: $f"; cat "$LOG/$f.clang.err"; fail=1; continue; }
  $LLC -mtriple=mcs251 -O2 -mcs251-object-format=elf -filetype=obj "$LOG/$f.ll" -o "$LOG/$f.o" 2> "$LOG/$f.llc.err" || { echo "LLC-FAIL: $f"; cat "$LOG/$f.llc.err"; fail=1; }
done
$CLANG --target=mcs251-unknown-none -std=c11 -DMCS251_RT_TARGET -O0 -I"$RTSRC" -S -emit-llvm "$RTSRC/mcs251_bitutil.c" -o "$LOG/mcs251_bitutil.c.ll" 2> "$LOG/bitutil.clang.err" || { echo "CLANG-FAIL: bitutil"; cat "$LOG/bitutil.clang.err"; fail=1; }
if [ $fail -eq 0 ]; then
  $LLC -mtriple=mcs251 -O0 -mcs251-object-format=elf -filetype=obj "$LOG/mcs251_bitutil.c.ll" -o "$LOG/mcs251_bitutil.c.o" 2> "$LOG/bitutil.llc.err" || { echo "LLC-FAIL: bitutil"; cat "$LOG/bitutil.llc.err"; fail=1; }
fi

# ---------- 宿主 Oracle-A ----------
step "host oracle (arith)"
$HOSTCC -std=c11 -O2 -I"$SRC" "$SRC/host_arith.c" -o "$LOG/host_arith" -lm 2> "$LOG/host_arith.build.err" || { echo HOST-ARITH-BUILD-FAIL; cat "$LOG/host_arith.build.err"; fail=1; }
if [ $fail -eq 0 ]; then
  "$LOG/host_arith" > "$LOG/host_arith.expected" 2>/dev/null || { echo HOST-ARITH-RUN-FAIL; fail=1; }
  echo "host arith lines: $(wc -l < "$LOG/host_arith.expected")"
  if [ "$INJECT_WRONG" -eq 1 ]; then
    # 负例自测：向期望文件注入一行错误数据，后续比较必须失败
    step "INJECT WRONG DATA into expected (negative test)"
    printf 'ADD a=DEADBEEF b=00000000 r=CAFEBABE\n' >> "$LOG/host_arith.expected"
    echo "injected bogus line; expected line count now: $(wc -l < "$LOG/host_arith.expected")"
  fi
fi

# ---------- QEMU 运行函数 ----------
run_qemu() {
  local hex=$1 serial=$2 timeout=$3 sentinel=$4
  : > "$serial"
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
  QEMU_RC=$rc
}

# 运行并校验固件：区分 正常 sentinel 终止 / 异常退出 / 缺失 sentinel / 超时
run_and_check() {
  local name=$1 hex=$2 expected=$3 sentinel=$4 label=$5
  step "QEMU $name firmware"
  run_qemu "$hex" "$LOG/$name.serial" 60 "$sentinel"
  local rc=$QEMU_RC
  if tail -c 256 "$LOG/$name.serial" 2>/dev/null | grep -q "$sentinel"; then
    echo "  qemu: sentinel '$sentinel' observed (rc=$rc, normal termination)"
  elif [ "$rc" -eq 124 ]; then
    echo "  qemu: TIMEOUT (rc=124), sentinel '$sentinel' missing"
    fail=1
  elif [ "$rc" -eq 0 ]; then
    echo "  qemu: exited 0 but sentinel '$sentinel' MISSING"
    fail=1
  else
    echo "  qemu: ABNORMAL exit rc=$rc, sentinel '$sentinel' missing"
    fail=1
  fi
  if [ $fail -eq 0 ] || tail -c 256 "$LOG/$name.serial" 2>/dev/null | grep -q "$sentinel"; then
    compare "$expected" "$LOG/$name.serial" "$label" || fail=1
  fi
}

compare() {
  local host_file=$1 dut_file=$2 label=$3
  python3 - "$host_file" "$dut_file" "$label" <<'PYEOF'
import sys
host_f, dut_f, label = sys.argv[1], sys.argv[2], sys.argv[3]
host = open(host_f,"rb").read().splitlines()
try:
    dut = open(dut_f,"rb").read().splitlines()
except FileNotFoundError:
    print(f"  {label}: FAIL (DUT serial missing)")
    sys.exit(2)
while host and host[-1] == b"": host.pop()
while dut and dut[-1] == b"": dut.pop()
mismatches = 0
for i in range(max(len(host), len(dut))):
    h = host[i] if i < len(host) else b"<missing>"
    d = dut[i] if i < len(dut) else b"<missing>"
    if h != d:
        mismatches += 1
        if mismatches <= 10:
            print(f"  MISMATCH line {i+1}: host={h.decode(errors='replace')} dut={d.decode(errors='replace')}")
if mismatches == 0:
    print(f"  {label}: PASS ({len(host)} lines)")
    sys.exit(0)
print(f"  {label}: FAIL ({mismatches} mismatches / {max(len(host),len(dut))} lines)")
sys.exit(1)
PYEOF
}

link_fw() {
  local name=$1 cseg=$2 fw_obj=$3; shift 3
  local objs="$@"
  $LLD --edata-end 0x3fff \
    --area-start=HOME=0xff0000 --area-start=VECS=0xff0003 --area-start=BOOT=0xff0100 \
    --area-start=CSEG=$cseg --area-start=XINIT=0xff8000 \
    --map "$LOG/$name.map" -o "$LOG/$name.elf" \
    "$fw_obj" "$CRT" $objs > "$LOG/$name.link.stdout" 2> "$LOG/$name.link.stderr"
  return $?
}

# ---------- 固件 #1: 运算+比较+转换 ----------
if [ $fail -eq 0 ]; then
  step "compile + link arith firmware (CSEG=0xFE0000)"
  $CLANG --target=mcs251-unknown-none -std=c11 -O2 -I"$SRC" -S -emit-llvm "$SRC/arith_firmware.c" -o "$LOG/arith_fw.ll" 2> "$LOG/arith_fw.clang.err" || { echo FW-CLANG-FAIL; cat "$LOG/arith_fw.clang.err"; fail=1; }
  if [ $fail -eq 0 ]; then
    $LLC -mtriple=mcs251 -O2 -mcs251-object-format=elf -filetype=obj "$LOG/arith_fw.ll" -o "$LOG/arith_fw.o" 2> "$LOG/arith_fw.llc.err" || { echo FW-LLC-FAIL; cat "$LOG/arith_fw.llc.err"; fail=1; }
  fi
  if [ $fail -eq 0 ]; then
    link_fw arith_fw 0xFE0000 "$LOG/arith_fw.o" "$LOG/mcs251_float_arith.c.o" "$LOG/mcs251_float_cmp.c.o" "$LOG/mcs251_bitutil.c.o" || { echo "ARITH-LINK-FAIL"; cat "$LOG/arith_fw.link.stderr"; fail=1; }
  fi
  if [ $fail -eq 0 ]; then
    $OBJCOPY -O ihex "$LOG/arith_fw.elf" "$LOG/arith_fw.hex" 2>/dev/null || { echo OBJCOPY-FAIL; fail=1; }
  fi
  if [ $fail -eq 0 ]; then
    run_and_check arith_fw "$LOG/arith_fw.hex" "$LOG/host_arith.expected" ARITH-PASS arith
  fi
fi

echo "== done =="
if [ $fail -eq 0 ]; then
  echo "ACCEPTANCE-PASS"
  exit 0
else
  echo "ACCEPTANCE-FAIL"
  exit 1
fi
