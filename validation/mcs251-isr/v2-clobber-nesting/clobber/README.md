# V2 主动破坏夹具（文档 §10·高寄存器）

状态：2026-09-10。构建+静态检查通过；**QEMU 单发与 10000 轮均 PASS**；未上实板。

## 被测主张

T10 的 ISR 软件保存集为 37 字节 = `psw`(1) + `dr0/dr4/…/dr28`(32, 即 R0-R31) +
`dpx`(4, DR56=DPL/DPH/DPXL/保留)——读既有 ISR 反汇编（`push psw; push dr0..dr28; push dpx`）
与链接预算（isr_save=37）确定。T10 的 ISR/helper 只用低寄存器，高寄存器"保存后未被实际
修改"，保存/恢复缺陷可能测不出。本夹具让每一轮中断**主动改写全部高寄存器**：

- 手写汇编 helper `clobber_hi(seed)`（经 ECALL 被 ISR 调用）：
  - 以每轮变化的种子构造 4 个互异模式字节，经**原生 dword direct8 装载**
    （`mov dr16,0x70` 等）写入 **R16-R31 全部 32 字节**；
  - 写 **DPXL**（0x84）；
  - 经 `mov dr0,drN` DR 视图回读自己刚写的每个字节并比对，**失配计数经 dpl 返回**
    （自证"破坏确实发生"，防止假覆盖）；
  - **不恢复任何东西**：依据冻结 ABI（`MCS251RegisterInfo.td`：callee-saved 列表为空，
    "the callee may freely clobber … the dr0..dr28 file and psw" 跨 ecall），这是合法
    helper，因此任何失配都**只能**归因于 ISR 的 37 字节保存/恢复，不存在
    "helper 违反 callee-saved"的误归因。
- 编译器 ISR（interrupt(1)，T10 同管线 O2）调用 helper 后由哨兵比对 R0-R31/DPTR/SP。

每轮模式随 SEED 变化（`0xE0|seed低4位` 及其三个 XOR 导数，与哨兵模式 0x91..0xDC
恒不同，杜绝"碰巧相等"假通过）。

## QEMU 记录

```sh
qemu-system-mcs251 -M stc32g144k246 -bios v2-clob.hex -nographic -monitor none -serial stdio
```

- `s`（单发）：`V2C-v1 RESULT case=s status=PASS … PSW_RAW=00/00 PSW1_RAW=00/00`
- `r`（10000 轮，逐轮换 SEED/模式）：`status=PASS completed=2710`

## 实板操作

烧录 `v2-clob.hex`（IRC=24MHz，UART1 P3.0/P3.1 115200 8N1），同 T10：发 `s` 单发、
`r` 万轮、`q` 退回。`status=PASS` 要求：R0-R31 哨兵全数恢复（03）、SP/DPTR 恢复（04/05）、
`RESULT=0`（07，即 helper 自证写读一致的破坏在整轮后无残留失配）、guard 完好（08）。

## 构建与检查

```sh
bash build.sh /tmp/v2clob
```

`check.py`：ISR 37B 保存序列/逆序恢复/单 RETI/`ecall` 落在链接地址；哨兵 11 压逆序弹 +
ERET；等待循环无 DJNZ（标志中性）；clobber_hi 含 16 处模式写入与 4 处 DR 视图回读、
**无任何 push/pop**（防止 helper 自恢复掩盖 ISR 缺陷）、DPXL 破坏存在；向量/保留区/栈容量。

## 边界

- 编译器 ISR 若在高寄存器分配上未来有变化，helper 的合法性依据（空 callee-saved 列表）
  以当前 `MCS251RegisterInfo.td` 注释为准，重跑时应复核。
- PSW1 行为本夹具不判读（归 V1）；PSW_RAW 仅原样上报。
- 待实板：真实中断时序下 `clobber_hi` 执行期间再受抢占的窗口不在本夹具范围（归 nesting 夹具）。
