# load i16 静默错码探针（Shizuku, 2026-09-05）

现象：llc(MCS251, 冻结版 md5 09c438e6) 编译的 `load i16` 读 SDCC 写入的
u16 全局时返回错误值：SDCC(大端) 写 0x1234，llc 侧读出 0x1212
（低字节 0x12、移位取高字节也是 0x12——0x34 字节丢失，疑似同字节广播）。
反向 store i16 完全正确（llc store 0x00FF -> SDCC 读回 0x00FF）；
u8 全局双向正确（含 0xFF 值域）。

相关观察（nec-decode t1 用例第二帧）：kernel 内连续赋值
UserCode=(IR_UserH<<8)+IR_UserL; IR_code=IR_data; B_IR_Press=1;
在第二帧（含 0xFF 字节、IR_UserH=0x00）出现 UserCode 写 0、IR_code store
丢失，但 B_IR_Press 置位——部分 store 生效。疑同族 codegen 问题。

## 复现
文件（本目录）：
- load-i16-probe.kernel.ll   被测 IR（_lo16/_hi16/_read_u8/_read_u16_hi/
  _write_u8_ff/_write_u16_00ff；lshr i16 8 已用 volatile alloca 屏障手工展开
  ——直接移位无法过 ISel，见 RESULTS.md 移位缺口）
- load-i16-probe.wrapper.c   SDCC 侧驱动（写 0x1234/0xFF00 后读回检查）
- load-i16-probe.img.lk      链接命令文件（CONST 固定 0xFC8000）

命令（WSL）：
  LLC=/home/liu/mcs251-demo-test/bin-frozen/llc
  QEMU=/home/liu/mcs251-demo-test/bin-frozen/qemu-system-mcs251
  SD=/home/liu/build-sdcc/bin
  FW=/mnt/c/Prj/LLVM/MCS251/validation/mcs251-firmware
  FC=/home/liu/mcs251-demo-test/build/firmware
  $LLC -mtriple=mcs251-unknown-none -filetype=obj -o k.rel load-i16-probe.kernel.ll
  : > k.lst
  cpp -P -undef -nostdinc load-i16-probe.wrapper.c > w.i
  $SD/sdcc -mmcs251 --c1mode -o w.asm < w.i
  $SD/sdas251 -plosgffw -o w.rel w.asm
  python3 /mnt/c/Prj/LLVM/MCS251/validation/mcs251-ld/mcs251_ld.py \
      --mcs251-abi -f load-i16-probe.img.lk
  timeout 15 $QEMU -M stc32g144k246 -bios img.hex -accel tcg \
    -icount shift=0,align=off,sleep=off -display none -monitor none \
    -serial stdio < /dev/null

期望（bug 在场时的实际输出）：`Bl12h12PASSPASS`（0x1234 -> lo=hi=0x12）
正确行为应为：`Bl12h34`（lo=0x12, hi=0x34）
另见 l12/h12 段：wrapper 写 u16 0x1234；r/H/w/W 段为 u8/u16 正反向对照。
