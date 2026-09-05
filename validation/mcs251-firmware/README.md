# MCS-251 QEMU 固件资产

这个目录保存 QEMU 端到端验证链所需、可以审阅和长期维护的输入资产。它们不再从
SDCC 构建树复制，也不依赖 `/tmp` 中会消失的 crt0/harness/provider/lk 文件。
`/tmp/mcs251-fw/` 只用于每次复验的可再生中间文件和日志。

## 资产清单与职责边界

| 文件/角色 | 负责什么 | 不负责什么 |
| --- | --- | --- |
| `crt0.asm` | `GSINIT0` 的 `__sdcc_gsinit_startup`；设置 `SPX=0x2fff` 并跳到 `__sdcc_program_startup`；在 `CSEG` 提供三个无操作运行时钩子（`__mcs51_genRAMCLEAR`、`__mcs51_genXINIT`、`__mcs51_genXRAMCLEAR`） | 不拥有 HOME 复位向量，不调用 `_main`，不实现检查点 |
| `harness-template.c` | HOME/`_main` 由 SDCC 生成；直接向 `SBUF=0x99` 发串口字符；提供 `harness_check_u8/u16/u32`，失败时打印 `FAIL expected=0x... got=0x...`；成功时打印 `PASS` 并停住 | 不属于被测 LLVM 函数；不替代 Step 4 的自建 C 编译器/运行时 |
| `provider.asm` | 外部函数和外部数据样例：`_p13_init`、`_p13_ext`、`_p13_mem`；展示独立模块的 `ECALL`/`ERET` ABI | 不提供通用 libc，不承担启动、HOME 跳板或测试判定 |
| `link-template.lk` | 固定 HOME、GSINIT0、XDATA/IDATA 地址；记录 `-A` ABI 签名；列出 crt0、harness、被测模块和 provider | 不编译或汇编输入；必须先替换 `@...@` 占位符 |
| `crt-selfstart.asm`（Step 4a + globals） | 自有启动三合一：HOME 3 字节复位跳板 + VECS 8 槽默认兜网 + BOOT；BOOT 设置 `SPX=0x2fff` 后遍历 LLVM XINIT 稀疏记录，逐对象清零 DSEG 并从 ROM 覆盖非零初值，再调用 `_main`；main 返回后打印 `S` 自旋 | 不含 SDCC 启动胶；不服务需自定义中断向量的镜像（那些镜像自带向量模块，如 t4/timer0-irq-llvm） |
| `link-selfstart.lk`（Step 4a + globals） | crt-selfstart 镜像的 mcs251_ld.py 命令模板：固定 `HOME/VECS/XINIT/BOOT/CSEG` 基址 + ABI 签名；XINIT 位于 `0xfe0000`，其 RAM 目标由 DSEG 重定位决定 | 同样必须先替换 `@OUTPUT_STEM@/@CRT_REL@/@MODULE_REL@` |
| `selfstart-smoke/`（Step 4a 验收证据） | 两个用例链接未修改的库资产：smoke1 期望 `MS`（复位/栈/ECALL-ERET 往返），smoke2 期望 `MS!`（另验证默认向量兜网兜住无处理器 TF0） | 不是 PASS/FAIL harness；判定 = transcript 精确匹配 |
| `harness-llvm.ll` + `harness-llvm-cells.asm`（Step 4b） | 判定协议的自建化：`harness_check_u8/u16/u32` 的 LLVM 实现，输出与 C 版逐字节一致（`FAIL expected=0x.. got=0x..`、成功静默、`_harness_pass` 打印 `PASS` 后停住）。多参缺口绕过约定：expected 先 volatile 存进 cells 的固定 idata 单元（0x60-0x66，RSEG 绝对等值），check 只带 got 单参 | 不含 HOME/向量/栈初始化（那是 crt-selfstart 的职责）；不向后端要多参调用；IR 形态避开已探明缺口（i8/i16 移位、i32 lshr/ashr 均不可选） |
| `selfstart-check/`（Step 4b 验收证据） | 全 LLVM 镜像四相：pass 期望 `BrwdPASS`；fail8/16/32 期望 `BFAIL expected=0xA5 got=0x00` / `0x1357/0x0000` / `0x12345678/0x00000000` 后停机 | 同 smoke：transcript 精确匹配，非通用 runner |

分界原则是：crt0 只启动，harness 只驱动/判定，用户模块只实现被测函数，provider
只提供明确的外部依赖。这样更换用户模块不会隐式更换启动代码或判定逻辑。
Step 4a 起新增并列原则：`crt-selfstart.asm`/`link-selfstart.lk` 与上述四件套
并存（设计与实测依据见 `STEP4-SELFSTART-DESIGN.md`），现有四件套在全部用例
切换验收前保持原样。

## 收集与甄别记录

本次收集到并据此整理的来源如下。

* `validation/mcs251-smoke/crt0.asm` 和其已生成的
  `validation/mcs251-smoke/build/crt0-generated.asm` 仍在仓库内；后者含有完整的
  SDCC `.optsdcc` 签名和三个 `ERET` 桩。`crt0.asm` 的 `@OPTSDCC@` 是旧 smoke
  脚本在汇编前填充的临时占位符，已在本目录改成完整、可直接汇编的签名。
* `validation/mcs251-smoke/harness.c` 及 `build/harness.asm/.lst` 仍在仓库内；HOME
  的 `ljmp __sdcc_mcs251_reset_trampoline`、`ejmp __sdcc_gsinit_startup`、
  `_main` 和 `__start__stack` 的实际布局由这些产物核对。
* Windows 临时目录中可找到 `/tmp/mcs251-p13a-crt0.asm`、
  `/tmp/mcs251-p13a-harness.c`、`/tmp/mcs251-p13a-provider.asm` 以及独立链接脚本
  生成脚本；它们提供了多函数/i8/i16/i32/外部调用验收样例的来源记录。
* 请求中列出的 WSL `/tmp/mcs251-p12-acc/smoke-final/`、
  `/tmp/mcs251-p10-acc/smoke-final/`、`/tmp/mcs251-p12/sample/` 和
  `/tmp/mcs251-p12-acc/` 在本次工作环境中已不存在。因此没有把这些易失目录当作
  依赖；缺失部分按现有 `.lst`/`.asm` 和已知职责重建，并在下面的复验中从本目录
  汇编所有启动/测试/provider 资产。

## 工具与 ABI 签名

以下命令以 WSL 安装为例。Windows checkout 的路径是
`/mnt/c/Prj/LLVM/MCS251`；请按本机路径调整三个工具变量。

```sh
export FW=/mnt/c/Prj/LLVM/MCS251/validation/mcs251-firmware
export W=/tmp/mcs251-fw
export SDCCBIN=/home/liu/build-sdcc/bin
export LLC=/home/liu/build-mcs251/bin/llc
export QEMU=/home/liu/build-qemu/qemu-system-mcs251
mkdir -p "$W"
```

`crt0.asm`、`provider.asm` 和由 SDCC 产生的 harness 汇编都必须使用同一个完整签名：

```text
stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1
```

## 从源构建固件侧

### 1. 汇编自维护 crt0 和 provider

```sh
"$SDCCBIN/sdas251" -plosgffw -o "$W/crt0.rel" "$FW/crt0.asm"
"$SDCCBIN/sdas251" -plosgffw -o "$W/provider.rel" "$FW/provider.asm"
```

`-p -l -o -s -g -f -f -w` 是现有验收链使用的 listing/object 选项组合；命令中的
`-plosgffw` 让 `sdas251` 同时留下可审阅的 `.lst/.sym/.rst` 记录。

### 2. 用 SDCC 编译 harness

模板故意不包含 SDCC 头文件或 libc。沿用 smoke 验证使用的 C1 模式，先用主机
预处理器处理 `__sfr` 等 SDCC 语法，再让 SDCC 生成 ASxxxx 汇编，最后交给
`sdas251`：

```sh
cpp -P -undef -nostdinc "$FW/harness-template.c" > "$W/harness.i"
"$SDCCBIN/sdcc" -mmcs251 --c1mode -o "$W/harness.asm" < "$W/harness.i"
"$SDCCBIN/sdas251" -plosgffw -o "$W/harness.rel" "$W/harness.asm"
```

要给某个用户模块添加检查点，写一个临时 wrapper（wrapper 可以放在 `/tmp`，它
不是固件资产）并在包含模板前定义 `MCS251_CHECKPOINTS()`。例如：

```c
#define MCS251_CHECKPOINTS() do { \
    UART_PUTC('r'); harness_check_u8(0xa5, p13_ret8()); \
    UART_PUTC('w'); harness_check_u16(0x1357, p13_ret16()); \
} while (0)
#include "/mnt/c/Prj/LLVM/MCS251/validation/mcs251-firmware/harness-template.c"
```

wrapper 还应声明这些被测函数；检查点可以按目标模块增删。每个检查点前输出一个
短字符，便于从 QEMU 串口定位失败位置。默认模板调用 `mcs251_probe()`，因此也能
直接用于最小 smoke 模块。

## 严格 sdld 链接

`link-template.lk` 是带 `;` 注释的命令模板（SDLD 命令文件沿用 ASxxxx 风格的分号
注释）。模板故意不含空行：这版 `sdld` 的命令文件解析器会把空行当成结束输入；
展开占位符时也请保留这一点。将 `@OUTPUT_IHX@`、`@CRT0_REL@`、`@HARNESS_REL@`、
`@MODULE_REL@`、`@PROVIDER_REL@` 替换为绝对路径后，再执行：

```sh
"$SDCCBIN/sdld" --mcs251-abi -r -nf "$W/image.lk" \
    > "$W/image.sdld.log" 2>&1
cp "$W/image.ihx" "$W/image.hex"
```

关键选项的含义是：

* `-i` 选择 Intel HEX 输出；本机 `sdld` 仍可能按 `.ihx` 生成文件，即使参数中
  写了 `.hex`。QEMU 的 MCS-251 loader 只按 `.hex` 后缀接受 BIOS，因此必须复制或
  重命名为 `.hex`，不能只看链接返回码。
* `-b` 把 HOME 固定在 `0xff0000`、GSINIT0 固定在 `0xfc2800`，并把 XSEG/PSEG/ISEG
  固定到板级地址；这些地址必须和 QEMU machine 及 crt0 入口保持一致。
* `-A` 写入并校验完整 MCS-251 ABI 签名；签名不完整或和模块不一致时，strict
  `--mcs251-abi` 链接应当失败，而不是静默混链。
* `-e` 是链接命令文件的结束标记；它必须位于所有输入 `.rel` 之后。
* `-r` 开启 MCS-251 的可重定位/24-bit ROM accounting 路径，`-nf` 使用给定命令
  文件；这是验收链的 strict 链接形式。使用 `-r -nf` 时，某些版本还要求每个
  `.rel` 旁边存在 `.lst`，对于 `llc -filetype=obj` 没有 assembler listing 的模块，
  建立空文件即可：` : > "$W/module.lst"`。

## 两条 QEMU 复验链

下面的被测输入是一个仅用于复验、放在 `/tmp/mcs251-fw/matrix.ll` 的多函数模块，
包含 i8/i16/i32 返回、32-bit 运算、分支、栈 round-trip、provider `ECALL` 以及
provider 全局数据读取。验证产物（`.rel/.ihx/.hex`、listing、链接日志和串口原文）
全部放在 `/tmp/mcs251-fw/`，不会污染仓库。

先准备该输入和针对它的 harness wrapper：

```sh
cat > "$W/matrix.ll" <<'LL'
@_p13_mem = external global i8
declare i8 @_p13_ext(i8)
define i8 @_p13_ret8() { ret i8 165 }
define i16 @_p13_ret16() { ret i16 4951 }
define i32 @_p13_ret32() { ret i32 305419896 }
define i32 @_p13_logic32(i32 %x) {
entry:
  %a = and i32 %x, 252645135
  %o = or i32 %a, 16711935
  %r = xor i32 %o, 65535
  ret i32 %r
}
define i8 @_p13_branch(i8 %x) {
entry:
  %lt = icmp ult i8 %x, 10
  br i1 %lt, label %small, label %large
small: ret i8 17
large:
  %eq = icmp eq i8 %x, 200
  br i1 %eq, label %special, label %other
special: ret i8 34
other: ret i8 51
}
define i16 @_p13_stack_roundtrip(i16 %x) {
entry:
  %slot = alloca i16
  store i16 %x, ptr %slot
  %v = load i16, ptr %slot
  ret i16 %v
}
define i8 @_p13_local_inc(i8 %x) {
entry:
  %r = add i8 %x, 3
  ret i8 %r
}
define i8 @_p13_call_chain(i8 %x) {
entry:
  %a = call i8 @_p13_local_inc(i8 %x)
  %b = call i8 @_p13_ext(i8 %a)
  ret i8 %b
}
define i8 @_p13_load_global() {
entry:
  %v = load i8, ptr @_p13_mem
  ret i8 %v
}
LL
cat > "$W/matrix-harness.c" <<'C'
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
extern u8 p13_ret8(void); extern u16 p13_ret16(void); extern u32 p13_ret32(void);
extern u32 p13_logic32(u32); extern u8 p13_branch(u8);
extern u16 p13_stack_roundtrip(u16); extern u8 p13_call_chain(u8);
extern u8 p13_load_global(void); extern void p13_init(void);
#define MCS251_CHECKPOINTS() do { \
 p13_init(); UART_PUTC('r'); harness_check_u8(0xa5,p13_ret8()); \
 UART_PUTC('w'); harness_check_u16(0x1357,p13_ret16()); \
 UART_PUTC('d'); harness_check_u32(0x12345678UL,p13_ret32()); \
 UART_PUTC('l'); harness_check_u32(0x05fffc00UL,p13_logic32(0x55aa33ccUL)); \
 UART_PUTC('a'); harness_check_u8(17,p13_branch(3)); \
 UART_PUTC('b'); harness_check_u8(34,p13_branch(200)); \
 UART_PUTC('c'); harness_check_u8(51,p13_branch(42)); \
 UART_PUTC('s'); harness_check_u16(0xbeef,p13_stack_roundtrip(0xbeef)); \
 UART_PUTC('e'); harness_check_u8(0x18,p13_call_chain(0x10)); \
 UART_PUTC('g'); harness_check_u8(0xa7,p13_load_global()); \
} while (0)
#include "/mnt/c/Prj/LLVM/MCS251/validation/mcs251-firmware/harness-template.c"
C
cpp -P -undef -nostdinc "$W/matrix-harness.c" > "$W/harness.i"
"$SDCCBIN/sdcc" -mmcs251 --c1mode -o "$W/harness.asm" < "$W/harness.i"
"$SDCCBIN/sdas251" -plosgffw -o "$W/harness.rel" "$W/harness.asm"
```

### (a) `llc -filetype=asm` 路径

```sh
"$LLC" -mtriple=mcs251-unknown-none -filetype=asm \
    -o "$W/matrix-llvm.asm" "$W/matrix.ll"
"$SDCCBIN/sdas251" -plosgffw -o "$W/matrix-llvm.rel" "$W/matrix-llvm.asm"
: > "$W/matrix-llvm.lst"       # only needed if this llc path did not emit one
```

把 `link-template.lk` 展开到 `$W/asm.lk`，其四个输入依次为 `$W/crt0.rel`、
`$W/harness.rel`、`$W/matrix-llvm.rel`、`$W/provider.rel`，输出设为
`$W/asm.ihx`，然后 strict link 并改后缀：

```sh
"$SDCCBIN/sdld" --mcs251-abi -r -nf "$W/asm.lk" > "$W/asm.sdld.log" 2>&1
cp "$W/asm.ihx" "$W/asm.hex"
timeout 30 "$QEMU" -M stc32g144k246 -bios "$W/asm.hex" -accel tcg \
  -icount shift=0,align=off,sleep=off -display none -monitor none \
  -serial stdio < /dev/null | tee "$W/asm.serial.raw"
```

预期串口原文（若工具没有额外诊断输出）是；首个 `B` 是 `_main` 进入标记：

```text
BrwdlabcsegPASS
```

### (b) `llc -filetype=obj` 路径

```sh
"$LLC" -mtriple=mcs251-unknown-none -filetype=obj \
    -o "$W/matrix-llvm.rel" "$W/matrix.ll"
: > "$W/matrix-llvm.lst"       # obj 直出没有 sdas251 listing
```

使用同样的 `crt0.rel`、`harness.rel`、`provider.rel` 和 `$W/obj.lk`，只把模块输入
替换为这个 obj 直出的 `.rel`，然后：

```sh
"$SDCCBIN/sdld" --mcs251-abi -r -nf "$W/obj.lk" > "$W/obj.sdld.log" 2>&1
cp "$W/obj.ihx" "$W/obj.hex"
timeout 30 "$QEMU" -M stc32g144k246 -bios "$W/obj.hex" -accel tcg \
  -icount shift=0,align=off,sleep=off -display none -monitor none \
  -serial stdio < /dev/null | tee "$W/obj.serial.raw"
```

预期串口原文同样是 `BrwdlabcsegPASS`。harness 末尾是故意的无限循环，所以
`timeout` 返回 124 是正常的；判定依据是串口中出现完整 `PASS` 且没有 `FAIL`，不是
QEMU 是否自然退出。失败时，`harness_check_u8/u16/u32` 会输出形如
`FAIL expected=0x1357 got=0x0000`，然后停住，避免把错误当成超时。

## 与 Step 4 的关系和遗留问题

本步骤固化的是 Step 4 前置资产：crt0 的源已经在仓库中自维护，harness/provider
以及 strict 链接规则也有了可复现的文字和输入来源。它仍然保留 SDCC 作为 harness
C 到 ASxxxx `.rel` 的编译器，并沿用 SDCC 的 ABI 符号/启动约定；这正是 Step 4 后续
要消除的依赖。Step 4 完全自建启动时，需要由 LLVM/MCS-251 侧提供等价的 HOME 复位
跳板、`_main` 入口、栈初始化、串口判定所需的调用/返回约定和数据初始化语义，并且
最终不再要求 SDCC 生成 `harness.asm` 或提供运行时钩子。`crt0.asm` 本身不能被误认为
已经完成那个目标：它目前只是已知 SDCC 入口契约的最小、可审阅实现。
