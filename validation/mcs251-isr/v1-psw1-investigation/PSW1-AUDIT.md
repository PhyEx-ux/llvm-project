# V1 机器码审计：T10 路径上每条指令的标志副作用（PSW/PSW1）

**日期**：2026-09-10
**依据**：仓库内 qemu-processmission 模型源码（`target/mcs51/helper.c`、`cpu.c`、`internals.h`）、
`realhw-demo/release/sentinel.lst` / `isr.asm` / `helper.asm` 实际机器码、
`hwframe/REALHW-20260910.md` 实板 4 字节中断帧采样。全部结论可从仓库内文件复核；未实测的硅片行为明确标注"待实板"。

## 1. 两个标志寄存器的位定义（qemu 模型 `target/mcs51/cpu.h`）

| 位 | PSW (0xD0) | PSW1 (0xD1) |
|---|---|---|
| 7 | C | C |
| 6 | AC | AC |
| 5 | **F0** | **N** |
| 4:3 | RS1:RS0 | RS1:RS0 |
| 2 | OV | OV |
| 1 | F1 | **Z** |
| 0 | P（读出时由 ACC 奇偶即时计算，`cpu.c get_psw`） | — |

关键点：

- **0x20 = PSW1 位 5 = N（负标志）**。N 与 Z 只存在于 PSW1；F0/F1/P 只存在于 PSW。
- 模型中 C、AC、OV、RS 是**两个视图共享的同一物理标志**（`set_psw` 与 `set_psw1` 写同一组
  `env->flag_c/flag_ac/flag_ov/reg_bank`）。写 PSW1 会改写 C/AC/RS/OV；写 PSW 不会碰 N/Z。
- 硅片位序**未经非零标志实验证实**——这正是 V1 固件 combo 2/3/5/7 要回答的问题之一。

## 2. 指令级标志副作用（qemu 模型逐条核对）

| 指令（编码） | 模型行为 | 源码依据 |
|---|---|---|
| `add`（0x25/0x2e 族） | 置 C、AC(8位)、OV；并 **置 N=结果位7、Z=结果==0** | `mcs251_add`→`mcs251_set_nz` |
| `djnz direct,rel`（0xD5） | 减量后 **置 N/Z**（经典 8051 DJNZ 不动标志——本模型偏离经典语义） | `case 0xd5: … mcs251_set_nz(env, value, 8)` |
| `inc/dec`（0x04/05/14/15 及原生 0x0b/0x1b 族，含 `inc/dec spx`） | **置 N/Z**（SPX 为 32 位：正常栈值 N=0、Z=0） | `mcs251_native_incdec_execute` 末尾 `mcs251_set_nz` |
| `orl/anl/xrl`（0x42/52/62、0x43/53/63、原生 0x4e/5e/6e） | **置 N/Z**（经典 8051 逻辑运算不动标志——本模型偏离） | `case 0x62/0x63`、`0x4e/5e/6e` 均调 `set_nz` |
| `subb/cmp`（0x94-0x9f、0xbe） | 置 C、AC、OV、N、Z | `mcs251_sub`→`set_nz` |
| `cjne`（0xb4-b7） | 置 C（比较借位） | `mcs251_cjne_flags` |
| `jb/jnb/jbc`（0x10/0x20/0x30/0x40/0x50/0x60/0x70） | **不动任何标志**（JZ/JNZ 按 ACC 值跳，非按 Z 位） | `case 0x20` 无标志写；`0x40-0x70` 只读 ACC/flag_c |
| `mov` 全族（含 `mov direct,#imm`、`mov direct,dir`、`mov dpl,rn`） | **不动标志** | 各 case 无 set_nz |
| `push/pop`（0xC0/0xD0、原生 0xCA/0xDA） | **不动标志**（pop 0xD0/0xD1 经 SFR 写路径间接写标志，见 §4） | `mcs251_native_pushpop_execute` |
| `orl 0x88,#0x10` 之类的 SFR orl | 结果写 TCON；**副作用：按结果置 N/Z** | 同 0x43 |
| `ecall`（0x9A） | 压 3 字节返回地址；**不动标志** | `case 0x9a` |
| `eret`（0xAA） | 弹 3 字节 PC；**不动标志** | `mcs251_return_extended` |
| `reti`（0x32） | 弹 PC(3B) **并弹出第 4 字节写入 PSW1 视图**（置 C/AC/N/RS/OV/Z） | `mcs251_return_interrupt`→`set_psw1` |
| 写 SFR 0xD0（`mov 0xD0,#imm`） | 置 C/AC/F0/RS/OV/F1（不碰 N/Z/P） | `sfr_write` case PSW→`set_psw` |
| 写 SFR 0xD1（`mov 0xD1,#imm`） | 置 C/AC/N/RS/OV/Z（**经共享物理标志同时改写 PSW 视图的 C/AC/RS/OV**；不碰 F0/F1） | `sfr_write` case PSW1→`set_psw1` |

模型注记：经典 8051 教科书语义中 DJNZ/INC/逻辑运算不影响标志；本模型（以及由此生成的
"模型预期"）让它们都写 N/Z。硅片是否同样偏离经典语义，是 V1 实板实验要回答的问题
（对 T10 的 0x20 有直接因果关系，见 §5）。

## 3. T10 路径逐段审计（`release/sentinel.lst`、`isr.asm`、`helper.asm` 实际字节）

### 3.1 sentinel 窗口前（采样点 0x3C/0x3D 之前）

| 指令 | 标志效果 |
|---|---|
| `push 0xd0/0xd1/dr0-28/dpx`（11 个压栈） | 无 |
| `mov 0x35/0x36/0x37/0x38,SP 系列`、`mov spx,#0x0600` | 无（`mov spx,#imm` 走原生 move，无标志） |
| `mov 0x21/0x22,#0`、`mov 0x23,#0x40` | 无 |
| `mov 0x82/0x83/0x84,#imm`（DPTR/DPXL）、读回 0x39-0x3B | 无 |
| `mov rN,#imm` ×16、`mov drN,#imm16`+`movh` ×8 | 无 |
| **`mov 0xd0,#0`、`mov 0xd1,#0`** | **显式清零两视图全部标志**（含 N=0, Z=0） |
| `mov 0x3c,0xd0`、`mov 0x3d,0xd1`（before 快照） | 无 |
| `mov 0xa8,#0x82`（EA+ET0） | 无 |
| **`orl 0x88,#0x10`（TR0=1）** | **写 TCON 的同时置 N/Z = (0x10→N=0, Z=0)** |

→ before 快照（PSW_RAW/PSW1_RAW 的前值）恒为 00/00（P 位对 ACC=0 也是 0）。

### 3.2 等待循环（`_wait`）

```text
0000A2  20 00 09   jb 0x00,_done      ; 无标志
0000A5  D5 21 FA   djnz 0x21,_wait    ; 置 N/Z：0x21 从 0 减到 0xFF → N=1
0000A8  D5 22 F7   djnz 0x22,_wait    ; 同上
0000AB  D5 23 F4   djnz 0x23,_wait    ; 0x40 减到 0 时 Z=1（终态）
```

第一次 `djnz 0x21` 执行后 N=1（0x00-1=0xFF，位7=1）。此后 N/Z 随 0x21/0x22 的减量
逐拍翻转（0x80..0xFF → N=1；0x7F..0x01 → N=0；减到 0 → Z=1）。**中断受理时刻的
PSW1 取决于定时器溢出落在循环的哪一拍**——这是一个对 T10 观测值（实板 0x20、
QEMU 00）有直接因果关系的相位依赖，见 §5。

### 3.3 ISR 序言（编译器生成，`isr.asm`）

| 指令 | 字节数 | 标志效果 |
|---|---|---|
| `push psw`（C0 D0） | 1 | 无（把当时 PSW 视图压栈，P 位为压栈瞬间 ACC 奇偶） |
| `push dr0/dr4/…/dr28`（CA xx ×8） | 32 | 无 |
| `push dpx`（CA EB，4 字节：DPL/DPH/DPXL/保留，即 DR56 视图） | 4 | 无 |
| 合计 | **37 字节** | 与 `check.py` 的断言一致 |

**37 字节保存集不含 PSW1**：N/Z 只能靠硬件中断帧（见 §4）保护，这是结构性的、
不是本次观测到的缺陷。

### 3.4 ISR 体与 helper

- `inc spx,#0x4` ×2：**置 N/Z**（32 位结果 N=0、Z=0）。
- `add r0,#imm`、`add r0,r1` 等：置 C/AC/OV/N/Z。**SEED 大时和数可 ≥0x80 → N=1**
  （10000 轮终局 SEED=0x10，局部和 0x85 → N=1；单发 SEED=1 时全部和 <0x80 → N=0）。
- `orl r0,#0x40`（SHARED）、`anl r0,#0xef`（停 TR0）：按结果置 N/Z（结果 0x52/0x20 → N=0）。
- `xrl r1,#imm`（helper）：按结果置 N/Z（0x24^0x31=0x15 等 → N=0）。
- `mov dpl,r1; ecall _helper`：ecall 无标志。
- helper 内 `add r1,r0` 等：同上，单发轮全部结果 <0x80。
- `mov 0x20,r0`（DONE=1）、`mov 0x33,r0`（RESULT）：无标志。

**模型语义下单发轮（SEED=1）执行完 ISR 体后 N=0、Z=0**——即 ISR 体自身在模型里
不产生 0x20。

### 3.5 ISR 尾声

| 指令 | 标志效果 |
|---|---|
| `dec spx,#0x4` ×2 | 置 N/Z（N=0/Z=0） |
| `pop dpx/dr28…/dr0` | 无（SFR 之外的弹栈） |
| `pop psw`（D0 D0） | **把入口 PSW 写回两视图中的 PSW 部分**（C/AC/F0/RS/OV/F1 恢复；P 重算） |
| `reti`（0x32） | 弹 PC(3B)；**弹第 4 字节写 PSW1（C/AC/N/RS/OV/Z 全部恢复为受理时刻值）** |

## 4. 硬件中断帧与 PSW1 的保护路径

- 帧结构（`hwframe/REALHW-20260910.md` 实板实测，与模型 `mcs251_cpu_do_interrupt` 一致）：
  受理时依次压 **PSW1、PC[23:16]、PC[7:0]、PC[15:8]**（4 字节，升序 `00 FF 74 02`）；
  RETI 逆序弹回，第 4 字节进 PSW1。
- 因此：**受理时刻的 PSW1（含 N/Z）由硬件帧保护，RETI 恢复；PSW 不在帧内，靠编译器
  37 字节保存集里的 `push psw`/`pop psw` 恢复**。
- 实板 hwframe 采样的 PSW1 三点值 02/00/00 中 I=00（受理/入口时刻）与帧第 4 字节 00 一致；
  但 B 与 R 的采样点夹着等待循环，都不是严格对照——V1 固件补上严格对照。

## 5. 对实测 `PSW_RAW=00/00 PSW1_RAW=00/20` 的解释模型

串联 §2-§4（全部为模型语义 + 实测帧）：

1. sentinel `mov 0xd1,#0` 后 before=00。
2. `orl 0x88,#0x10` 置 N/Z=0（模型语义）。
3. 等待循环的 `djnz 0x21` 首拍即把 0x21 从 0 减到 0xFF → **N=1**；N 的取值随循环相位
   在 1/0 间翻转（0x80..0xFF 为 1）。
4. 定时器溢出受理：**当前 PSW1（很可能是 0x20）压入 4 字节帧**。
5. ISR 体内 `add`/`orl`/`anl`/`inc spx` 把 flags 改成各自的值（N 多为 0）。
6. `pop psw` 恢复 PSW；`reti` **弹出受理时刻的 PSW1（0x20）写回**。
7. sentinel 采样 after=0x20。

即：**0x20 最合理的来源是等待循环 DJNZ 写入 N 的受理时刻 PSW1，经硬件帧原样保存并
恢复——按模型语义这是正确行为，不是编译器保存/恢复缺陷**。QEMU 下同一固件
（本会话实测 frozen t10-g12.hex）给出 `PSW_RAW=00/00 PSW1_RAW=00/00`，差异只是
受理相位落在循环的哪一拍（N=1 的拍与 N=0 的拍各约半数）。实板单发与万轮终局都
撞在 N=1 拍，与约 50% 相位概率不矛盾。

**不能据此定案的残余假设**（V1 固件逐项排除，判读见 PROTOCOL.md）：

- H-A：硅片 DJNZ/逻辑运算不写 N/Z（经典语义）→ 需要别的 0x20 来源（帧不恢复 PSW1，
  ISR 内某指令在硅片上置 N）。
- H-B：RETI 不把第 4 帧字节恢复进 PSW1（保存了但不恢复）。
- H-C：PSW1 位序与模型不同（0x20 不是 N）。
- H-D：PSW1 SFR 写路径与模型不同（不可写/写后读不一致）。

## 6. 逐指令审计结论

1. T10 的 37 字节保存集（PSW+DR0..DR28+DPX）**结构上不覆盖 N/Z**；N/Z 只能由 4 字节
   硬件帧保护。帧的第 4 字节实板已见 00，但零值不能区分"PSW1 保存"与"恒 0 占位"。
2. 模型语义下，T10 等待循环的 DJNZ 与窗口开启的 ORL 都会写 N/Z；实测 0x20 与
   "受理时刻 N=1 + 帧恢复"自洽。
3. 夹具（本目录）用严格对照把上述每一步变成可实测数据；不预设 PASS。
