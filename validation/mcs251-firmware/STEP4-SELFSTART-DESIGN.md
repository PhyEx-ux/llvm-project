# Step 4 自建启动设计：去 SDCC 的最小启动链

状态：设计定稿候选 v1（2026-09-05，Moka）。工具：冻结 llc/qemu（md5
09c438e6…/6b9edfd0…）。实验资产：WSL `/home/liu/mcs251-step4/`（startup.asm、
startup2.asm、main.ll、globals.ll、s4.lk、s4vec.lk、s4self.hex/.out、
s4vec.hex/.out、e2/e3/e5 脚本）。

标注约定：**[实测]** = 有 QEMU transcript 或产物字节/map 证据；**[推断]** =
设计推断，进 §5 最小实测清单。

## 0. 摘要

现状链的启动 = SDCC 编译 harness.c 时附带生成的 HOME/GSFINAL/`_main` 胶 +
自维护 crt0.asm（GSINIT0）。本设计用**一个自有 asm 启动模块**替换全部 SDCC
附带胶：3 字节 HOME 跳板 + 天生给 8 个中断向量留槽的 VECS 区 + BOOT 区
（SPX 初始化 + `ecall _main` + 返回后停摆）。全链（asm 启动 + llc 编译的
`_main` + mcs251_ld.py）**已实测跑通**：transcript `MS`；默认向量兜网（未
提供处理器的 TF0 → 打印 `!` + spin）**已实测**：transcript `MS!`。初始化
数据（GSINIT）当前无事可做——llc 对已定义全局数据**响亮报错** [实测]，
设计预留不实现。

## 1. 现状契约盘点

证据来源：`validation/mcs251-smoke/build/smoke-sdcc.hex/.map`（字节级）+
`mcs251-firmware/crt0.asm` + `mcs251-endian/ACCEPTANCE.md` §7 + T4 实测。
复位 PC=0xFF0000（外设调查 §2.3）。

| # | 组件 | 地址/尺寸 | 字节/内容 | 行为 | 证据 |
|---|---|---|---|---|---|
| 1 | HOME 复位跳板（SDCC 生成） | 0xFF0000，**13 字节** | `02 00 03` LJMP→0xFF0003；`8A FC 28 00` EJMP→__sdcc_gsinit_startup；`9A FC 28 0B` ECALL _main（即 __sdcc_program_startup）；`80 FE` SJMP . | 复位→GSINIT0→ecall _main→返回后自旋 | [实测] hex 字节+map（l_HOME=0x0D）；**0xFF0003 与 0xFF000B 分别压在 INT0/TF0 向量槽上**（T4 实测踩到，SDCC harness 不可用于中断测试的根因） |
| 2 | GSFINAL 尾跳（SDCC 生成） | 0xFF000D，4B | `8A FF 00 07` EJMP→__sdcc_program_startup | GSINIT 执行完跳回 startup | [实测] 字节+map；端序专项 §7 实测坑：GSINIT/GSFINAL 可不连续，**不能 fall-through，必须尾跳** |
| 3 | GSINIT0（自维护 crt0.asm） | 0xFC2800，8B | `7E F8 2F FF` MOV SPX,#0x2FFF；`8A FF 00 07` EJMP→__sdcc_program_startup | 设栈后进 SDCC 启动 | [实测] 字节+map（l_GSINIT0=8） |
| 4 | `_main` | CSEG 内（0xFC280B） | SDCC 由 harness.c `main()` 生成 | 被 HOME 内 ECALL 调用 | [实测] |
| 5 | `__start__stack`/SSEG | s_SSEG=0x0008 | harness.asm `.area SSEG; __start__stack: .ds 1` | SDCC 习惯法的栈起点符号 | [实测] grep 四个 .rel：**仅 harness.rel 定义，零引用**——crt0 用立即数直设 SPX，此符号是化石 |
| 6 | 三个 ERET 桩（crt0.asm） | CSEG | `__mcs51_genRAMCLEAR/__mcs51_genXINIT/__mcs51_genXRAMCLEAR` 各一条 ERET | SDCC 启动序列引用这些符号；空桩保可链接+保 3 字节返回帧 | [实测] 存在性；纯 LLVM/asm 链是否可省见 §5-1 [推断] |
| 7 | .optsdcc ABI 签名 | 全模块 | `stc32-mcs251 abi-major=1 … compiler-build=mcs251-abi1.0-r1` | mcs251_ld.py --mcs251-abi 强制逐字节校验 | [实测]（T4 生产链 strict 链接通过） |

实测指令编码（从镜像字节确认，供手写启动汇编）：LJMP addr16=`02 hi lo`
（3B，保留 PC 高字节）；EJMP addr24=`8A b2 b1 b0`（4B）；ECALL addr24=
`9A …`（4B）；MOV SPX,#imm16=`7E F8 hi lo`（4B）；MOV direct,#imm8=
`75 dir imm`（3B）；SJMP rel=`80 rel`（2B）；RETI 弹 4 字节硬件中断帧
（PSW1+PC24，T4 实测）。

## 2. 自有启动设计

### 2.1 布局：天生向量留槽 **[实测]**

```
0xFF0000  HOME  ljmp boot                          ; 3B，恰好不碰 INT0 槽（d3 范本）
0xFF0003  VECS  8 × { ejmp isr_unhandled; .ds 4 }  ; 64B，覆盖 03/0B/13/1B/23/2B/33/3B
0xFF0100  BOOT  mov spx,#0x2fff; ecall _main; <post-main>; sjmp .
0xFF0200  CSEG  LLVM 模块（_main 等）
```

- 向量槽 8 字节标准间距，ejmp 4B + 4B 余量；HOME 3 字节根治了 SDCC 版
  13 字节盖向量的问题。
- **默认兜网**：任何未武装的中断（含 stray TF0/TI）落 `isr_unhandled`：
  打印 `!` 自旋。已实测：boot 武装 T0（65536 tick 全量程）后 ecall LLVM
  `_main`，transcript `MS!`——LLVM 主函数打印 'M'、ERET 返回、boot 打印
  'S'，随后 TF0 被默认槽兜住打印 '!'。短初值（16 tick）时序实验中得到过
  裸 `!`（中断抢在 'M' 前），证实兜网本身无时序依赖。
- **向量定制模式**：ASxxxx 无 weak symbol，"默认槽+覆盖"不可行（重定义即
  链接错）。约定：不需要中断的镜像链 `crt-vectors`（默认兜网 VECS）；
  需要中断的测试**不链 VECS**，自带向量模块——`t4/timer0-irq-llvm` 已是
  此模式（自带 VEC area @0xff000b），实测可行。
- BOOT 放 0xFF0100 是为对齐易读；紧随 VECS（0xFF0043）也可，属自由选择。

### 2.2 启动序列与等价物

BOOT = `mov spx,#0x2fff` → `ecall _main` → 返回后 `<post-main>` → 自旋。

- SPX=0x2FFF 沿用 crt0 取值（edata 16K @0x000000–0x3FFF 内）[实测沿用值；
  取值的自由度未探究，非关键]。
- `__start__stack`/SSEG 等价物：**不需要**（§1-5 零引用）[实测]。兼容
  条款：未来混链 SDCC 模块若引用之，补 1 字节 SSEG 定义即可 [推断，§5-2]。
- 三 ERET 桩：纯 LLVM/asm 链无 SDCC 启动序列引用，可省 [推断，§5-1]；
  混链期保留 crt0.asm 不动。
- `<post-main>`：当前测试契约里 main 不返回（harness PASS/FAIL 后自旋），
  post-main 仅作返回证据/保险丝。clang 落地后由前端契约定 main 返回语义
  （exit code 打印？裸机惯例：打印退出码进 transcript）。

### 2.3 GSINIT 数据初始化：当前为空集，设计预留

- **[实测]** llc 对任何已定义全局数据响亮报错：`LLVM ERROR: MCS251:
  defined global data is not supported yet (…data-area support is Step 2
  of the Phase 12 plan)`。即纯 LLVM 链当前**没有可初始化对象**，GSINIT
  无事可做；后端数据区落地前，初始化设计不定稿。
- 预留两条路（待数据区落地后定稿，[推断]）：
  a. 混链期：SDCC 模块初值走其 GSINIT——.lk 必须显式 `-b GSINIT` +
     `-b GSFINAL` 且 GSFINAL 保留尾跳（端序专项 §7：不可假定连续）；
  b. 自有期：自有 INIT 区 + BOOT 在 `ecall _main` 前
     `ecall __init_data`（flash→xdata COPY），格式随后端数据区形态定。
- QEMU loader 对 .hex 中 xdata 地址记录的行为（直接预置 RAM、免 COPY 的
  可能）：[推断，§5-4]。

### 2.4 mcs251_ld.py 布线（-b 选项组）

- [实测] 有效组合：`-b HOME=0xff0000 -b VECS=0xff0003 -b BOOT=0xff0100
  -b CSEG=0xff0200` + `-A` 签名 + `-I 0x0100`，60B 镜像跑通。
- [实测] `-b` 引用**未定义区**报错：`ASlink-Error-No definition of area
  XSEG/PSEG`——无数据区的镜像不要写 XSEG/PSEG 的 -b；Areas51 只预建
  `_CODE/REG_BANK_0-3/BSEG/BSEG_BYTES/BIT_BANK/DSEG/OSEG/ISEG/SSEG`
  （读码+链接通过佐证）。
- [推断，§5-3] 未 -b 的 CODE 区从 rloc=0 起排（lnkarea2 读码），而 QEMU
  取指 <0x800000 被禁（外设调查）→ **所有代码区必须显式 -b**，建议写成
  runner 前置检查。
- .lk 纪律沿用：无空行、`;` 注释、`-e` 收尾、输出直出 .hex
  （mcs251_ld.py 行为，T4 实测）。

## 3. 迁移策略（不破坏现有固件资产链）

原则：新资产并列新增，`crt0.asm`/`harness-template.c`/`provider.asm`/
`link-template.lk` 原样保留，直到 T1/T2/T4 全部用例切换并回归通过。

- **Step 4a（现在可做，纯 asm+现有工具）**：`crt-selfstart.asm`（HOME+
  VECS+BOOT 三合一模块）进 `mcs251-firmware/`；配 `link-selfstart.lk`
  模板。t4/timer0-irq-llvm 已是"无 SDCC 资产"形态（asm harness + llc
  模块），可直接切换验证。[实测基础已备]
- **Step 4b**：harness 判定逻辑（harness_check_u8/16/32、PASS/FAIL 协议）
  移植为 asm 或 .ll 模块（固定地址 volatile SBUF 已证明够用）；
  `harness-template.c` 退出。T1 的 wrapper 模式（MCS251_CHECKPOINTS 宏）
  需要一个 .ll 侧等价写法约定。
- **Step 4c（等 clang/前端）**：`_main` 语义（参数/返回/初始化责任划分）
  由 Clang 前端契约定稿；届时 BOOT 的 post-main 行为、数据初始化责任随之
  固化。此前 `_main` 均为手写 .ll，不阻塞 4a/4b。

## 4. 设计判断证据标注汇总

[实测]：现状契约全部 7 项（§1）；向量留槽布局+自有启动全链 `MS`；默认
向量兜网 `MS!`/裸 `!`；llc 全局数据报错；`-b` 未定义区报错；指令编码表；
ecall/eret 24 位帧平衡（T4）。
[推断]：ERET 桩可省性、SSEG 兼容条款、未 -b 代码区落零页行为、loader 对
xdata 记录行为、自有 INIT 区格式。

## 5. 最小实测清单（按价值排序，均小成本）

1. 混链缺桩：自有启动 + 一个 SDCC 编译 harness 模块、不提供三 ERET 桩 →
   观察链接是否失败/运行是否异常。决定桩去留。
2. `__start__stack` 引用场景普查：SDCC `--stack-auto`/reentrant 函数是否
   生成对它的引用（编译一个带 reentrant 的 C 看 .rel）。
3. 漏 `-b CSEG` 的失败形态固化：确认取指禁区的 QEMU 表现（无输出/spin），
   写成 runner 检查文案。
4. QEMU loader 对 .hex 内 xdata 地址记录：写特征值记录后在固件里读回。
   决定初始化数据能否"加载器预置"。
5. 兜网与 UART1 电平 IRQ：stray TI 落 `isr_unhandled` 后 TI 未清，中断
   是否重复触发（不影响兜网正确性，影响 transcript 形态——写进测试注意
   清单）。
