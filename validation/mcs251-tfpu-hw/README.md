# TFPU 状态寄存器语义实机探针（STC32G12K128）

目标：在真机上测出 TFPU 的**完成/状态机制**，用串口回传原始观测数据。

本目录只做一件事：**把未文档化的东西变成可读的日志**。不预设结论，不因为
"手册这么写"就跳过实测。

---

## 1. 先看这条：调查前提需要修正

在写这个 demo 之前，我们把官方库重新反汇编了一遍，发现**任务书里的一条
关键证据是 opcode 解码错误**，这直接改变了"矛盾点"的性质。

官方 `AI8051U_32_TFPU.LIB`（以及 `sample.hex` 链接后镜像 `0xff0749` 起）
逐字节如下：

```
75 ED 33   MOV DMAIR,#0x33      ; 读状态
A5 EF      MOV A,R7             ; ← 不是 "MOV A,0xEF"！
22         RET
A5 FF      MOV R7,A             ; ← 不是 "MOV A,0xFF"！写状态前装载
75 ED 34   MOV DMAIR,#0x34      ; 写状态
22         RET
75 ED 35   MOV DMAIR,#0x35      ; 读控制
A5 EF      MOV A,R7
```

**关键点：`A5 EF` 不是 `E5 EF`。**

- `A5 EF` = `MOV A,R7`（Source 模式 `MOV A,Rn` = `[A5][E8+rn]`，rn=7 → EF）
- `A5 FF` = `MOV R7,A`（Source 模式 `MOV Rn,A` = `[A5][F8+rn]`，rn=7 → FF）
- `E5 EF` 才是 `MOV A,0xEF`；官方库里 `E5 EF` 出现 **0 次**。

我们在两个官方库里都做了统计（`AI8051U_32_TFPU.LIB`、`STC32_FPMU_LARGE.LIB`）：

| 字节序列 | 含义 | 出现次数 |
|---|---|---|
| `E5 EF` | `MOV A,0xEF` | **0** |
| `A5 EF` | `MOV A,R7` | 5 |
| `E5 FF` | `MOV A,0xFF` | **0** |
| `A5 FF` | `MOV R7,A` | 5 |
| `75 ED 33` | `MOV DMAIR,#0x33` | 1 |

**结论：不存在"官方读 SFR 0xEF 而非 R7"的矛盾。** 官方库读的就是 R7，
与手册 35.7.3「读状态寄存器，结果保存到 R7 中」完全一致。而且在本芯片上
**0xEF 是 AUXINTIF（INT2/3/4 与 T2/T3/T4 中断标志）、0xFF 是 RSTCFG（复位
配置）**，与 TFPU 无关，官方库从未碰过它们。

顺带，`comp` 的官方实现 `75 ED 21 / A5 EF / 54 0F / A2 E0 / 64 08 / 22`
（`MOV DMAIR,#0x21` → `MOV A,R7` → `AND A,#0x0F` → `MOV C,ACC.0` →
`XOR A,#0x08`）正好证实手册的比较编码：**R7.0 = "小于"（CY），A 异或 8 后为 0
即"相等"**——即结果在 R7，且 R7.0 是小于标志。这也从官方实现侧佐证了
"状态/结果在 R7"。

**那么真正剩下的未知是什么？**

1. **R7 各位的位定义**（手册仍未给）。探针用 `0x33` 在多个时刻采样，用
   **值的变化序列**反推语义。
2. **完成判据**：官方库是 `MOV DMAIR,#cmd ; RET`，零等待。这有几种可能：
   (a) `MOV DMAIR,#N` 这条指令在硬件上**阻塞到运算完成**（写指令本身吃掉
   运算时钟）；(b) 运算足够快、调用者后续开销已覆盖；(c) 官方库另有隐藏
   等待。**延迟扫描直接区分这几种**。
3. **12K128 到底有没有 TFPU**。见下一节，这条比什么都重要。

### 1.1 必须先排除的前提：12K128 可能根本没有 TFPU

手册证据（`manuals-md/STC32G/`）：

- **ch01 产品线表**：`STC32G12K128 系列 … MDU32=●, TFPU=—`（TFPU 一列是
  破折号）；同一张表里只有 `STC32F12K60 系列` 的 TFPU 是 `●`。
- **ch35 产品线表**：TFPU 一栏只列 `STC32F12K54 系列 ●`。
- **ch04 选型表**：12K128 行有 MDU32，但没有 TFPU 列。
- ch09/ch12 的 `RSTCR3` 里有 `RSTFPU` 位、附录 Q 定义了 `DMAIR = 0xed`——
  这些是**寄存器映射层面的存在**，不能单独证明某型号装配了 TFPU 硬件。

而任务书描述的 G144K246 手册 ch35 明确写了 `STC32G144K246 系列 ● TFPU`。

**因此本探针的第一判据就是"TFPU 在不在"**：`MULONCE` 阶段把 AR 预置为
`3.9f = 4079999A`、BR 为 `5.1f = 40A33333`，触发 `0x1E`。

- 若 `MULONCE R4..R7 = 41 9F 1E B8` → **TFPU 存在**，继续解读状态位与延迟。
- 若 `MULONCE R4..R7 = 40 79 99 9A`（**仍等于操作数**）→ **这条指令没有
  执行任何运算**。在 12K128 上这是完全可能的正常结果，应解释为"该型号无
  TFPU"，而不是"demo 坏了"。此时 `0x31/0x32/0x33` 的读数也一并按
  "无此单元"解读。

这一条写在最前面，是因为它决定了后面所有数据的解释方向。

---

## 2. 接线与烧录

- 目标：**STC32G12K128**，STC-ISP 设 **IRC = 24 MHz**。
- 串口：**UART1，P3.0/RXD、P3.1/TXD，115200 / 8N1，无流控**。
  USB-TTL 的 TX 接 P3.0，RX 接 P3.1，共地。
- 镜像：`tfpu-probe.hex`（全部在 FF 程序区，无 XINIT，不占 EEPROM）。

烧录步骤（与 `validation/mcs251-isr/hwframe/README.md` 同一流程）：

1. STC-ISP 选择芯片型号 STC32G12K128，打开 `tfpu-probe.hex`。
2. 输入用户程序运行时的 IRC 频率 **24 MHz**，下载。
3. 打开串口助手，115200 8N1。
4. 上电后应看到 `TPU-PROBE READY 24MHz 115200; send g`，无需抢上电时机。
5. 发送 ASCII 小写 `g`（十六进制 `67`）触发一轮测量。
6. 复位可重测；`g` 可反复触发。

---

## 3. 执行顺序契约（用户要求，已写进源码注释）

**任何 TFPU 动作（含 0x3E 时钟选择、0x31、0x32、XFR 写入）都必须排在首条
串口输出之后。** 理由：若 TFPU 初始化/时钟选择导致跑飞或挂起，而串口还没
初始化或还没输出过，用户无法区分"没启动"和"启动后卡在 TFPU"。

实现：

- `main()` 的第一步就是 `uart_init()`，紧接着两条 banner。
- 之后才是 `run_round()`，逐阶段执行 TFPU 动作，**每个阶段前后各打一条
  `STAGE` 心跳**。
- `check.py` 有一条静态断言，从 hex 里解码 `_main`：**首个 UART SFR 写入 <
  首个串口输出（SBUF）< 首次调用探针入口**。（注意：**跨函数的地址顺序不是
  判据**——sdld 把探针函数排在 `_main` 之前，首个 DMAIR 写入的地址反而更小；
  判据是 `_main` 内部的执行顺序。）

---

## 4. 无输出 / 半截输出的定位方法

日志停在哪一行，就卡在哪一步：

| 最后可见内容 | 含义 |
|---|---|
| 没有 `TPU-PROBE READY` | 还没到 TFPU：检查烧录、24MHz、接线、串口参数、C 启动路径 |
| 有 banner，发 `g` 无 `GO` | RX 接线或发送格式问题（必须是字节 `67`） |
| `GO` 后停在 `STAGE raw` | 极可疑：raw 阶段**不发任何 TFPU 命令**，只是读寄存器；若卡这里说明问题不在 TFPU |
| 停在 `STAGE clk-sel` | 卡在 **0x3E 选 TFPU 时钟** |
| 停在 `STAGE init` | 卡在 **0x31 初始化协处理器** |
| 停在 `STAGE clr-exc` | 卡在 **0x32 清除异常** |
| 停在 `STAGE mul-once` | 卡在 **0x1E 乘法**（或随后的 0x33） |
| 停在某个 `SCAN <op> N=xxxx` | 卡在该延时值的该命令上；`N` 直接告诉你卡在哪一个 N |
| 出现 `DONE` | 一轮跑完 |

测量循环内部每个 N 都先打印一行 `SCAN <op> N=xxxx`，所以即使某次运算挂住，
也能从最后一行读出卡在哪个 N。

---

## 5. 输出格式与判读方法

### 5.1 日志形态

```
TPU-PROBE READY 24MHz 115200; send g
TPU-PROBE stages: raw clk-sel init clr-exc mul-once scan-mul/add/sin
GO
STAGE raw
RAW R7=xx EF=xx FF=xx MK=01          ← 未发任何 TFPU 命令的原始值
STAGE clk-sel
STAGE clk-sel-ok
STAGE init
STAGE init-ok
INIT R7=xx EF=xx FF=xx               ← 0x33 读状态（0x31 之后）
STAGE clr-exc
STAGE clr-exc-ok
CLEAR R7=xx EF=xx FF=xx              ← 0x33 读状态（0x32 之后）
STAGE mul-once
MULONCE R4=xx R5=xx R6=xx R7=xx      ← 3.9×5.1 结果
POSTMUL R7=xx EF=xx FF=xx            ← 结果读走后再 0x33
STAGE scan-mul
SCAN mul n=0..40
SCAN mul N=0000 R4 R5 R6 R7 MK=11
...
SCAN sin N=0110 ...
DONE
EXPECT mul  3.9*5.1 -> 419F1EB8
...
```

- `EF` / `FF` 是 **SFR 0xEF(AUXINTIF) / 0xFF(RSTCFG)** 的读数，**控制项**：
  预期全程 `00`（或至少与 TFPU 无关地恒定）。若它们随 TFPU 变化，那是重大
  发现，需单独复核。
- `MK` 是探针入口最后写入的完成见证（`01`=raw，`02`=0x33，`03`=mul_once，
  `11/12/13`=mul/add/sin 扫描）。**`MK` 没出现就说明该入口没跑完**（可能卡住
  或跑飞），此时该行的 `R4..R7` 不可信。

### 5.2 期望位模式（IEEE-754 单精度，窗口内大端：MSB 在 R4/R0）

| 运算 | 输入 | 期望 R4..R7 |
|---|---|---|
| mul `0x1E` | 3.9f × 5.1f | `41 9F 1E B8` |
| add `0x1C` | 3.9f + 5.1f | `41 10 00 00` |
| sin `0x2D` | sin(1.0 rad) | `3F 57 6A A4` |

操作数位模式：`3.9f = 4079999A`、`5.1f = 40A33333`、`1.0f = 3F800000`。
（`MULONCE` 里若 R4..R7 仍是 `40 79 99 9A`，就是"没算"。）

### 5.3 怎么从日志得出结论

**第 0 步（先做）**：看 `MULONCE`。
- `= 41 9F 1E B8` → TFPU 存在，进入下面。
- `= 40 79 99 9A` → **该型号无 TFPU 运算**（与手册产品线表一致）。到此为止，
  其余数据按"无此单元"记录。

**第 1 步：状态位语义（用变化序列反推，不靠猜）**
把 `RAW / INIT / CLEAR / POSTMUL` 四行的 R7 排成序列：

| 观测形态 | 推论 |
|---|---|
| `RAW=0`，`INIT≠0`，`CLEAR=0` | 0x31 置起了某个"异常/待清"状态位，0x32 能清掉 —— 与手册"初始化后产生异常需软件清除"一致；把 `INIT` 里为 1 的位记为**异常标志位** |
| `INIT=0` | 0x31 后该位为 0；则手册那句"产生异常状态"要么由别的位表达，要么本型号行为不同 —— 记录为**反例** |
| `POSTMUL≠0` 且 `POSTMUL` 与 `INIT` 不同 | 乘法置起了状态位（如 inexact/overflow 类），说明状态位会随运算累积 |
| `POSTMUL=0` 而 `INIT≠0` | 说明 0x32 之后到乘法之间没有新异常，且乘法不置位 |
| 四行全 0 | 该实现的状态寄存器恒 0，或 0x33 不返回状态到 R7 —— **重要反例**，需要与 `MULONCE` 的"存在"证据并列记录 |

关键：**用"位在哪些时刻从 0 变 1、又被什么清 0"来定义位**，这正是手册缺失
的部分。若 `INIT` 只有一位为 1、`CLEAR` 后为 0，那一位就是"异常"位；再结合
`POSTMUL` 是否置起其它位，即可给出位图。

**第 2 步：完成判据（延迟扫描）**
对每个命令，找**结果首次正确的 N**：

| 观测形态 | 结论 |
|---|---|
| **N=0000 就正确**（mul/add/sin 都是） | `MOV DMAIR,#cmd` **阻塞到完成**。官方库"零等待"因此成立；我们现有的 270-NOP 链**完全不必要**，可改为零等待，调用点从 ~270B 降到 ~15B，延迟即真实值 |
| mul 约 26~34、add 约 31~40、sin 首次正确在 32~270 内 | 运算是**异步**的，需要等待；首次正确的 N 就是该运算的真实耗时（下界）。手册的时钟范围得到实机标定 |
| 所有 N 都正确但 `MULONCE` 也正确 | 同样指向"阻塞式"，用最小 N 确认 |
| 所有 N 都等于操作数（没算） | 无 TFPU（回到第 0 步） |
| 某些 N 给出**部分正确/中间值** | 说明存在流水/部分写回，需逐 N 记录，不能只取首末 |

**注意固定开销**：触发后到 sled 之间只有一条 `JMP @A+DPTR`（2 字节），
所以"首次正确的 N"对应的真实时间 ≈ N + 常数（1 条跳转 + 触发指令本身）。
判读时把它当作**常数偏置**，不影响"零等待 vs 需等待"的定性结论，也足以标定
数量级；若要更精确，可用两个不同 N 的差值消掉偏置。

**第 3 步：R7 与 0x33 的关系**
- `INIT/CLEAR/POSTMUL` 的 R7 若与第 1 步的位图自洽 → 确认 **0x33 确实把状态
  送回 R7**（与官方库一致）。
- 若 R7 恒 0 而 `EF/FF` 有变化 → 才需要考虑"状态是否另有出口"（但注意
  `EF/FF` 在本芯片上是无关 SFR，这条几乎不可能）。
- `RAW` 行的 `R7` 是**未发命令时**的寄存器值，用于识别"R7 是否被探针自身
  污染"（探针不写 R7，只读）。

---

## 6. 构建

```sh
bash build.sh <空输出目录> hardware     # 真机镜像（TI 轮询）
bash build.sh <空输出目录> qemu         # 仅 QEMU（跳过 TI 等待与打印延时）
python3 check.py <hardware目录>
python3 check.py <hardware目录> --qemu-dir <qemu目录>   # 附带 QEMU 冒烟
```

链：clang → `.ll` → llc（`.rel` 对象 + `.asm`）→ sdas251 汇编
`tfpu-probe.asm` → sdld 链接 → Intel HEX。内存布局见 `link-tfpu.lk`：
HOME/CSEG 在 `FF:0000/FF:0200`，ISEG/BSEG 未用；可变量全部是固定 EDATA 地址
`0x0030..0x003A`（`0x00..0x1F` 是寄存器组、`0x20..0x2F` 是位寻址区，故从
`0x30` 起），**没有 C 全局对象**，因此不需要 XINIT / CRT 数据拷贝。C 栈从
`0x0800` 向 `0x0FFF` 生长（12K128 的 EDATA 是 4K，`00:0000-00:0FFF`）。

`gen-sled.py` 是延迟扫描的唯一真源：它生成 `sled.inc`（`TPU_SLED` 常量与
NOP 链），`check.py` 从镜像里读回 NOP 数并与之一致性核对。

### 6.1 延迟扫描的机制（为什么不是逐个 N 展开）

`JMP @A+DPTR` 把 `PC[15:0]` 置为 `A + DPTR`（高 8 位保留），而 A 只有 8 位，
所以一个 256 字节窗口内的连续入口可以用**一条共享 NOP 链**表示：

```
PC = sled + (TPU_SLED - n)
  n ≤ 255 : DPTR = sled + TPU_SLED_LO, A = 255 - n
  n >  255 : DPTR = sled,               A = TPU_SLED - n
```

`TPU_SLED = 272` 覆盖 `n = 0..272`（手册 sin/cos 最坏 270），每条命令只花
~272 字节而不是几十 KB 的展开表。触发指令紧贴在 `JMP` 之前。

---

## 7. QEMU 预验（**不是真机证据**）

`qemu` profile 在 `-M stc32g144k246` 上跑通了完整流程（阶段顺序、363 行扫描、
`MK` 见证齐全），说明固件能跑、串口协议正确、sled 寻址正确。

**但 QEMU 结果不能作为 TFPU 语义证据**：

- QEMU 的 `stc32g_tfpu.c` 模型是**瞬时**计算，没有时钟概念，所以扫描里每个 N
  都是"正确"——这**不能**用来判断真机是零等待还是需等待。
- 该模型**自行发明**了状态位布局（INVALID/DIVBYZERO/OVERFLOW/UNDERFLOW/
  INEXACT + ROUNDING），手册并没有这些定义。QEMU 里 `INIT R7=01`、
  `POSTMUL R7=10` 是**模型假设**，不是手册依据，更不是真机事实。
- QEMU 的机器是 G144K246（EDATA 16K / XDATA 128K），与 12K128 不同。

QEMU 唯一有价值的一点：它给出的 `41 9F 1E B8` / `41 10 00 00` /
`3F 57 6A A4` 与我们**独立**在主机上按 IEEE-754 算出的期望值一致，说明探针的
**操作数装载与字节序**（MSB 在 R0/R4、窗口内大端）与模型的理解一致。因此若真机
结果与期望不符，应优先怀疑"硬件行为/型号差异"，而不是探针装错了操作数。

---

## 8. 文件

| 文件 | 作用 |
|---|---|
| `tfpu-probe.asm` | 测量核心：各 TFPU 命令入口、共享 NOP 链、快照 |
| `main.c` | UART、banner、阶段心跳、扫描调度与打印 |
| `gen-sled.py` | 生成 `sled.inc`（`TPU_SLED` 与 NOP 链） |
| `link-tfpu.lk` | 12K128 链接布局 |
| `build.sh` | 构建（hardware / qemu 两个 profile） |
| `check.py` | 静态自检 + 可选 QEMU 冒烟 |
| `tfpu-probe.hex` / `.map` / `.lst` | 构建产物 |

`tfpu-probe.hex` SHA256 见下方或由 `sha256sum tfpu-probe.hex` 现算；
`check.py` 会校验校验和、复位向量、命令码直方图、UART 先于 TFPU 的执行顺序、
共享链几何与"不写 0xEF/0xFF"。
