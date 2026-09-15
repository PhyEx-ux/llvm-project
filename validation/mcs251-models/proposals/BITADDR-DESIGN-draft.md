# G6 设计稿草案：SFR 位寻址缺口（demo 32 / 42 / 58-TFT 的 T0 gap）

- 日期：2026-09-15（调查日）。工作树 `/home/liu/LLVM_STC32/MCS251`，HEAD `13d420b7a`
  （"MCS251: standardize the libc mem/str prototypes over the v2 pointer slots"）。
- 本文为**只读调查 + 设计稿**：不改任何产品源码、不实施。唯一写入的仓库内文件即本文；
  进度记 `/home/liu/LLVM_STC32/GAP-BITADDR-PROGRESS.md`；探针留档
  `/home/liu/LLVM_STC32/GAP-BITADDR-PROBES/`。
- 工具（与 `tools/drive.py` 顶部 `CLANG/LLC/LLD` 常量同源）：clang `/home/liu/build-mcs251-s1/bin/clang`、
  llc `/home/liu/build-mcs251/bin/llc`、lld `/home/liu/build-mcs251-lld/bin/mcs251-lld`。
- 语料只读复用：`/home/liu/LLVM_STC32/mcs251-demos-rewritten/`（`src/32-*`、`42-*`、`58-*`、
  `tools/rewrite.py`、`tools/drive.py`、`ledger.json`、`TIERS.md`）与
  `/home/liu/LLVM_STC32/STC32G144K246-DEMO-CODE/`（原始语料）。
- **并发役漂移注记**：本役进行中工作树有 **31** 项未提交资产（§7；收稿时 33，
  因并行役又落一笔 `validation/mcs251-runtime/src/mcs251_float_arith.c`），其中
  `llvm/lib/Target/MCS251/*`、`clang/lib/{Sema,CodeGen}/*` 为并行 G7/其他役在改；
  本役对这些文件只读。**行号随并发役漂移时以符号检索为准。**

## 0. 结论（一句话）

三 demo 的 "bit addr" gap **不是 P09 bit 类型缺口的延续**，而是**语料改写工具的
宏顺序缺陷**：demo 本地的 `MCS251_SFRBIT` 字节读改写 shim 被插到文件顶部，晚于它的
传递性 `#include "mcs251_bit_compat.h"` 再把同名宏重定义为错误哨兵，把 shim 覆盖掉。
**产品侧（clang/llc/lld）在 bit 层面零改动即可关闭**；42 与 58-TFT 在修正顺序后 T0 全过、
T1 推进到既有 CODE 窗口缺口（另一缺口，非本役）；32 另有一条独立的 `_nop_` 顺序缺陷
（同源，改写工具第二处）。推荐 **双轨**：G6 归**语料改写工具修复**（不改产品），
并把"是否要在产品侧补 MCS251 原生扩展位指令 `0xA9` 通道"登记为**独立可选切片**。

## 1. 问题与登记口径

- 权威登记：`mcs251-demos-rewritten/README.md:379` §8 G6 行——
  > 兼容头 bit 名覆盖不足：`mcs251_bit_compat.h` 显式拒绝 245 名（30 compiler-managed
  > + 215 非 8 对齐基址），语料用到 86 个
  最小证据串：`MCS251: base SFR is not bit-addressable ...`；命中 demo **32, 42, 58**。
- 三档表口径（`TIERS.md:48,58,78`）：32 / 42 / 58[TFT-ST7789] 均
  `T0 gap / T1 skip / T2 skip`，备注 `bit addr`。
- gap 标签来源：`tools/drive.py` 的 `gap_reason()` 里 `("not bit-addressable", "bit addr")` 子串匹配
  （本役读取时为 `:776`，随后被并行役改到 `:846`；**以符号 `gap_reason` 为准**），
  即标签**只对应兼容头的哨兵文案**，不代表产品 bit 能力判定。
- 与既有裁定的关系：本缺口在 A4 设计稿已按"既有缺口"登记
  （`A4-V2-OBJECT-IDENTITY-DESIGN.md:191` "SFR 位寻址（=G6 族）… 既有缺口"）；
  G3 设计稿亦按 `bit addr ×3` 计数（`G3-STATIC-PTR-DESIGN-draft.md:25`）。
  本役不改这些登记，只补精确根因与处置。

## 2. 失败层与精确文案（实测）

三 demo 的 T0 **首错由预处理哨兵产生**，来自
`mcs251_bit_compat.h` 的 `_Pragma("GCC error ...")`；紧随其后的"未声明标识符"
（`MCS251_error_non_bit_addressable_marker`）是**后续 Sema 诊断**——不是产品
bit 能力拒绝。实测区分：`-E` 只有哨兵错误，`-fsyntax-only` 才出现第二类
（哨兵把名字 define 成未声明标识符，Sema 随后报它）。

### 2.1 触发形态（源码）

三个 demo 各自文件顶部都有同一段改写产物（`tools/rewrite.py:155-158` 的 `SFRBIT_MACRO`）：

```c
/* demo-local SFR bit shims (README 3.2): the compat header rejects these
   names; the base SFR byte is bit-addressable per the STC32G manual. */
#define MCS251_SFRBIT(BASE, N) \
    (*(volatile struct { unsigned char b7:1,b6:1,b5:1,b4:1,b3:1,b2:1,b1:1,b0:1; } *)(BASE)).b##N
#undef ADC_FLAG
#define ADC_FLAG MCS251_SFRBIT(0xBC, 5)
```

### 2.2 逐 demo 最小复现与文案

| demo | 首个失败文件:行 | 触发宏 | 实测首错文案 |
|---|---|---|---|
| 32 | `Sources/src/adc.c:33` | `ADC_START`(0xBC,6) | `MCS251: base SFR is not bit-addressable, no hardware bit address exists and no byte-mask emulation is provided` |
| 42 | `LCD/LCD.c:72` | `SPI_S1`(0xA2,3) | 同上 |
| 58-TFT | `.../tft.c:52` | `SPI_S1`(0xA2,3) | 同上 |

每处紧随第二条：`use of undeclared identifier 'MCS251_error_non_bit_addressable_marker'`。

探针原始输出：`GAP-BITADDR-PROBES/evidence/r32.out`、`r42.out`、`r58.out`
（脚本 `GAP-BITADDR-PROBES/repro.py`，flags 与 `drive.py` 的 `demo_compile()` 一致）。

### 2.3 最小复现（已隔离到 8 行）

`GAP-BITADDR-PROBES/min/a_order_bug.c`（失败）与 `b_order_ok.c`（通过）——两者正文
逐字节相同，**唯一差别是 shim 相对于 `mcs251_bit_compat.h` 的可见顺序**：

```c
/* a_order_bug.c：shim 在前，兼容头由 hdr_transitive.h 间接引入 */
#define MCS251_SFRBIT(BASE,N) (*(volatile struct{unsigned char b7:1,b6:1,b5:1,b4:1,b3:1,b2:1,b1:1,b0:1;}*)(BASE)).b##N
#undef ADC_FLAG
#define ADC_FLAG MCS251_SFRBIT(0xBC,5)
#include "hdr_transitive.h"      /* -> #include "mcs251_bit_compat.h" */
void f(void){ ADC_FLAG = 1; }
```
```c
/* b_order_ok.c：先让兼容头进入，再定义 shim */
#include "mcs251_bit_compat.h"
#define MCS251_SFRBIT(BASE,N) ...
#undef ADC_FLAG
#define ADC_FLAG MCS251_SFRBIT(0xBC,5)
void f(void){ ADC_FLAG = 1; }
```

结果：`a_order_bug.c` 报哨兵错（rc=1）；`b_order_ok.c` 编译通过（仅 `-Wempty-body`）。
这证明**根因是宏顺序，不是产品能力**。

## 3. 需求画像与 P09 已支持集的差集

### 3.1 P09 已支持集（先读确认边界）

`P09-BIT-CODEGEN-DESIGN.md` §0/§2 与 `P09-BIT-TYPE-DESIGN.md` §1.1 已冻结并落地：
独立 `MCS251Bit` 类型、bit 对象（global/static/auto/param）代码生成、值 ABI（DPL +
`_callee_PARM_n`）、**受控固定位**经 `__builtin_mcs251_bit_lvalue(ICE)`（ICE 为 0..255
常量位地址）+ `llvm.mcs251.bit.{read,set,clear,toggle}` ImmArg 族，以及 sbit 固定引用。
`SemaDecl.cpp:6349-6425` 明确 `sbit NAME = BASE^INDEX` 要求
**`BASE` 在 0x80..0xFF 且低 3 位为 0**（`err_mcs251_sbit_bad_base`）。

### 3.2 三 demo 的 bit 用法清单

三 demo 的改写产物**已无任何 `bit`/`__bit`/`BOOL` 类型对象**（全语料扫描为 0），
bit 相关用法只剩两类：

| 形态 | 32 | 42 | 58(全部) | 与 P09 支持集的关系 |
|---|---:|---:|---:|---|
| `__builtin_mcs251_bit_lvalue(0xNN)`（8 对齐基址，`hardware-bit`） | 74 | 5 | 87 | **P09 已支持** |
| `MCS251_SFRBIT(base,idx)` 字节 RMW（非 8 对齐基址） | 11 | 10 | 29 | **不在 P09 支持集**（见 §3.3） |
| 其中 shim 名实际被使用的站点数 | 12 | 12 | 25 | 本缺口真实触发面 |
| `bit` 类型对象 / `sbit` / 数组 / 指针 / 位域 struct | 0 | 0 | 0 | P09 拒绝表范围，语料无 |

非 8 对齐 shim 名（定义处，`name(base,idx)`）：
- 32：`S1BRT(AUXR 0x8E,0)`、`S1_S0(P_SW1 0xA2,6)`、`S1_S1(P_SW1 0xA2,7)`、
  `S2TI(S2CON 0x9A,1)`、`S2_S(P_SW2 0xBA,0)`、`T1x12(AUXR 0x8E,6)`（System_init.c）；
  `ADC_FLAG(ADC_CONTR 0xBC,5)`、`ADC_START(ADC_CONTR 0xBC,6)`（adc.c）；`N(PSW1 0xD1,5)`（app_MatrixKey.c）。
- 42：`CPHA/CPOL/DORD/MSTR/SPEN/SSIG(SPCTL 0xCE,{2,3,5,4,6,7})`、`SPIF/WCOL(SPSTAT 0xCD,{7,6})`、
  `SPI_S0/SPI_S1(P_SW1 0xA2,{2,3})`（全在 LCD/LCD.c）。
- 58：SPI 组同 42（tft.c 与 OLED .c 各一份），另 `AI8051U.h` 有
  `ES2/ESPI(0xAF,{0,1})`、`EX2/EX3/EX4(0x8F,{4,5,6})`、`INT2IF/INT3IF/INT4IF(0xEF,{4,5,6})`、
  `LVDF(0x87,5)`、`N(PSW1 0xD1,5)`。

### 3.3 差集的本质

P09 冻结的位寻址模型是**经典 8 位位地址空间**：`bit-registers.json:bit_address_rules`
规定 `sfr_bit: B in [128,255]: backing_byte = B & 0xF8, bit_index = B & 7`，即
**只有低 3 位为 0 的 SFR 基址**才有合法位地址。`mcs251_bit_compat.h` 据此把 215 个
非 8 对齐名硬拒（`README.md:379`）。

但 STC32G 硬件并非如此。手册 `manuals-md/STC32G/ch11-...-CHIPID.md:436`（PDF 页 604）：
> 全部 SFR 区域的 80H~FFH，共 128 个字节，每个字节均可位寻址。

即**任意** SFR 字节的任意位都可位寻址。改写在兼容头之外用字节 RMW（`orl/anl` 序列）
模拟——这是**沿用已登记的字节 RMW 模拟**（`README.md:267` 的冻结规则），
**不据此保证原生位操作等价**。风险（Alice 复审补充）：① 中断竞争——读-改-写
窗口内若 ISR 改动同字节的其它位，回写会丢失；② 硬件异步更新——外设可能在
读与写之间改变该字节；③ **W1C（写一清零）**——手册
`manuals-md/STC32G/ch23-同步串行外设接口-SPI.md:64-74` 明确 SPIF/WCOL 为写一清零，
"全 SFR 可位寻址"**不能**证明整字节回写安全（回写 1 到标志位即清除它）。
因此本模拟仅在"该字节无并发写者、且非 W1C 标志"的前提下成立；这不影响本缺口
的定性（宏顺序 bug），但限定了它的语义强度。

因此本缺口的真实范围**不是"产品少了一种 bit 形态"，而是"改写 shim 的宏顺序"**：
兼容头拒绝 → 改写本地定义 → 但本地定义被兼容头反过来覆盖。

### 3.4 三者是否同一根因

**42 与 58-TFT 完全同一根因**（宏顺序），且**修顺序即 T0 全过**。
**32 有两条根因**：主因同 42/58（`adc.c`、`app_ntc.c`），
外加**同源的第二个顺序缺陷**（`app_MatrixKey.c` 的 `_nop_`，见 §4.3）。
故"是否同一根因"的准确回答是：**bit addr 这一条三者同一；32 另叠一条同工具的
`_nop_` 顺序缺陷**。

## 4. 根因（改写工具的两处顺序缺陷，均在 `tools/rewrite.py`）

### 4.1 缺陷 A：shim 块的插入锚点假设"直接 include"

`fix_bit_names`（`tools/rewrite.py`；`marker`/兜底见 `:201-208`）末尾：

```python
# insert right after the compat triple so the macros see the header types
marker = "#include \"mcs251_bit_compat.h\"\n"
idx = text.find(marker)
if idx >= 0:
    pos = idx + len(marker)
    text = text[:pos] + block + text[pos:]
else:
    text = block + text          # <-- 兜底：整块塞到文件最前
```

- 若本文件**直接**含兼容头，锚点命中，shim 落在其后 → 正确（demo 43/62 即此情形，T0 通过）。
- 若兼容头只由**项目头传递**引入（`adc.c -> adc.h -> config.h`、
  `LCD.c -> sys.h/lcd.h`、`tft.c -> tft.h`），`find` 返回 -1 → 走 `else` 兜底，
  **整块前置到文件最前**，于是兼容头随后被引入时用哨兵宏覆盖 shim。

命中文件（全语料，实测量 4 个 `.c` + 2 个 `.h`）：
`32/.../adc.c`、`32/.../app_ntc.c`、`42/.../LCD/LCD.c`、
`58/.../DMA-SPI驱动1.3寸TFT显示屏-ST7789-240x240/tft.c`，
以及 `32/.../Sources/inc/AI8051U.H`、`58/.../OLED.../AI8051U.h`（该两 .h 未被 .c 包含，
未实际触发）。

### 4.2 缺陷 B：`_nop_` 的 include 注入早于宏注入

`fix_missing_includes` 判 `INTRINS_RE`（`_nop_` 字面量）后注入 `#include "intrins.h"`；
但该步在 `rewrite_file` 流水线中**先于** `fix_missing_defines`（调用序见
`tools/rewrite.py` 尾部 `fix_missing_includes` → `fix_local_shadow` → `fix_missing_defines`）。原始文件里 `_nop_` 只出现在**注入的** `NOP1() _nop_()` 宏定义内部
（原始 `app_MatrixKey.c` 只用 `NOP40()`），故注入 `intrins.h` 时字面量尚不存在，
等 `fix_missing_defines` 把 `NOP1() _nop_()` 注入后已无人再补 `intrins.h`。
实测：`fix_missing_includes` 后 `intrins.h? False`；`fix_missing_defines` 后
`literal _nop_? True, intrins.h? False`。命中 1 个 `.c`（`app_MatrixKey.c`）。

### 4.3 与产品的关系

两处缺陷**都在仓库外（`mcs251-demos-rewritten/`）**，不在产品源码内。
`tools/rewrite.py` 属语料改写工具，不是 clang/llc/lld。

## 5. 方案空间

### 5.1 方案 A（推荐）：修改写工具的宏顺序，产品零改动

- **A1 缺陷 A**：把 `else` 兜底从"前置到文件最前"改为"**在文件首行注入兼容头
  `#include`，再跟 shim 块**"（等价于实测的 `GAP-BITADDR-PROBES/fix3.py`：前置
  `#include "mcs251_bit_compat.h"`）。兼容头有 include guard，重复引入无害。
- **A2 缺陷 B**：把 `fix_missing_defines` 提到 `fix_missing_includes` **之前**，
  或在 `fix_missing_defines` 注入后**再判一次** `INTRINS_RE`；也可直接让
  `fix_missing_includes` 额外识别 `NOP\d*` 宏链（即把 `_nop_` 判据扩到
  `#define NOP\d*\(\) _nop_\(\)` 的注入源）。
- **IR/代码序列**：**采用相同 lowering 形态**（与已通过 demo 43/62 一致；注意
  `GAP-BITADDR-PROBES/sem/bf.ll:8-20` 只证明了样例的字节 RMW 形态，**未**提供与
  43/62 的 IR 逐字节比较证据，也不构成外设语义等价证明）——`hardware-bit` 名走
  `__builtin_mcs251_bit_lvalue(0xNN)`（P09 固定族），非 8 对齐名走 shim 的
  `load/and/or/store` 字节 RMW（实测 IR：`load volatile i8, ptr inttoptr (i32 188 to ptr)`
  + `and i8 %x, -33` + `or i8 %y, 32` + `store`；读为 `lshr`+`and 1`）。
- **与 P09 冻结面的交互**：**零交互**。不走 bit builtin、不走对象句柄、不走值 ABI、
  不走 SFR 位寻址 intrinsic；`hardware-bit` 名仍走既有固定族，非 8 对齐名根本不产生
  bit 地址，故不触碰 `err_mcs251_sbit_bad_base` 的 0x80..0xFF 低 3 位零约束。
- **风险**：极低。改动局限在语料工具；需回归全批 67 demo 确认无新 gap 标签翻转。
- **PM 决策点**：无（属语料工具修复，非产品能力变更）。

### 5.2 方案 B：产品侧补 MCS251 原生扩展位指令通道（`0xA9`）

- **事实**：MCS251 有原生"扩展位寻址"编码，用**直接字节地址 + 位号**表达任意 SFR 位，
  无需 8 对齐基址。三处独立佐证：
  1. 手册 `manuals-md/STC32G/appendixA-指令集.md:244` 指令表：
     `Bit instr(蓝) | 位指令集（MCU251 特有）| 0xA9 | 3 | 1`；
     各条详解给 `A9C(y)H`（CLR bit）、`A9D(y)H`（SETB Bit）等，第三字节为**直接地址**。
  2. SDCC/sdas251 汇编器 `sdcc-upstream/sdas/as251/mcs251mch.c:135-136` 有
     `BIT_CLASSIC 1 / BIT_EXTENDED 2`，`parse_bit`（`mcs251mch.c:145-210`）对"非 0x20..0x2F 且非
     8 对齐 SFR"回退 `BIT_EXTENDED`（`:204`），`out_extended_bit`（`:222-228`）发
     `putcode(0x1A9); outab(opcode + number); outrb(&address, R_PAG0)`。
  3. QEMU `qemu-processmission/target/mcs51/helper.c:2009-2010` `case 0xa9:
     mcs251_native_bit_execute(...)`，实现 SETB/CLR/CPL/MOV/JB/JNB/JBC/ANL/ORL 全族
     （`cpu.h:76-84` 枚举，`FIELD(MCS251_BIT_SPECIFIER, BIT, 0, 3)` /
     `OPERATION, 3, 5`）。
- **产品现状**：LLVM 后端**无** `0xA9` 支持（`grep 0xA9/A9C/BIT_EXTENDED` 在
  `llvm/lib/Target/MCS251/` 无命中）；`MCS251InstrInfo.td:257-338` 的 Bit-addressed 块只实现经典
  `D2/C2/B2/A2/92/20/30/10` 8 位位地址族，且 `MCS251BitAddr.h` 的 `getBitAddr`（`:52-59`）硬校验 `[0,255]`。前端 `CheckMCS251SbitAddress`
  （`clang/lib/Sema/SemaDecl.cpp:6402`）亦以 `(BaseValue & 0x7) != 0` 拒绝非 8 对齐基址。
- **若采纳**：需新增 MIR 指令/编码 + 直接地址操作数（非 `mcs251_bitaddr` 的位地址域）
  + `MCS251BitAddr.h` 旁路校验 + 前端 `sbit BASE^INDEX` 放宽 + `bit-registers.json`
  规则扩展 + 生成头翻正。**跨 Sema/后端/ELF 记录/链接模型多层，且与 P09 的
  `ImmArg i32` 位地址契约正交但相邻**，属独立大切片。
- **对本缺口收益**：仅补后端**不能**消除当前的宏哨兵（失败在预处理层，早于任何
  后端路径），因此 `0xA9` **不作为方案 A 关闭 G6 的前置条件**；它是另一条（更高效、
  更贴近硬件语义）的代码生成路径，对 T0 成败无影响。
- **风险**：高（新编码、新操作数域、前端放宽、记录/契约影响）。
- **PM 决策点**：是否要为 STC32G "全 SFR 可位寻址"补原生通道（性能/保真度），
  **与关闭 G6 无关**，建议独立登记、暂不做。

### 5.3 方案 C：兼容头把非 8 对齐名改为映射（不改 shim）

- 即在 `mcs251_bit_compat.h` 内直接提供 `#define ADC_FLAG MCS251_RAM_BIT(...)` 之类。
- **不可行**：位地址模型（§3.3）表达不了非 8 对齐 SFR 位；`__builtin_mcs251_bit_lvalue`
  的 ICE 只能是 0..255 位地址，而 `bit_address_rules` 的 `sfr_bit` 反算基址必为 8 对齐。
  强行映射会**伪造硬件位地址**，违反 `BIT-DECISION-20260911 P09` 的
  "不静默降级"与 `bit-registers.json:policy.no_silent_downgrade`。
- 结论：**排除**。

### 5.4 方案 D：语料侧改写规避（把非 8 对齐位改为整字节读写）

- 手工把 `SPI_S1 = 0` 之类改为 `P_SW1 &= ~0x08`。语义等价但**丢失位语义、逐处重写
  成本高、易错**，且 shim 已是自动生成的正确等价物。
- 结论：**仅在方案 A 不可行时兜底**；不作为推荐。

### 5.5 对比总表

| 维度 | A 修工具顺序 | B 产品原生 `0xA9` | C 兼容头映射 | D 手工改写 |
|---|---|---|---|---|
| 关闭 G6 三 demo T0 | **是** | 否（不触及触发点） | 不可行 | 是 |
| 产品源码改动 | **零** | 多层 | 兼容头（伪造地址） | 零 |
| 与 P09 冻结面交互 | 无 | 相邻正交、需新契约 | 违反不降级 | 无 |
| 风险 | 低 | 高 | — | 中（人工） |
| 推荐 | **★** | 独立登记 | 排除 | 兜底 |

## 6. 切片、测试矩阵与风险

### 6.1 切片（建议；本役不实施）

| 卡 | 范围 | 判据 |
|---|---|---|
| **G6-S1**（语料工具） | 修 `tools/rewrite.py` 缺陷 A + B | 4 个 `.c` 的 shim 落在兼容头之后；`app_MatrixKey.c` 有 `intrins.h`；重跑 `drive.py` 全批 |
| **G6-S2**（验证） | 32/42/58-TFT 的 T0 实测 | 42/58-TFT T0 PASS；32 T0 需 B 修复后 PASS；三 demo T1 记录到**真实下一层缺口** |
| **G6-S3**（可选，独立） | MCS251 `0xA9` 原生位寻址通道可行性预研 | 与 G6 关闭解耦；产出独立设计稿 |

**并行-串行边界**：G6-S1 只动仓库外 `mcs251-demos-rewritten/tools/`，与
`MCS251/` 产品树并行实例**无文件冲突**。G6-S3 若启动则触碰 `llvm/lib/Target/MCS251/`
与 `clang/lib/Sema/`，**必须与并行 G7/其他役串行排班**（当前这些文件有未提交改动）。

### 6.2 测试矩阵（实测基线，见 §7）

| 用例 | 期望 | 现状（实测） |
|---|---|---|
| 42 T0（修顺序） | PASS 5/5 | **PASS** |
| 58-TFT T0（修顺序） | PASS 2/2 | **PASS** |
| 32 T0（修 A+B） | PASS 11/11 | A 后仍 1 文件失败（`_nop_`） |
| 42 T1（修顺序） | 越过 bit addr | **T1 lld FAIL `CODE overlap for .text`**（既有 G13a，非本役） |
| 58-TFT T1（修顺序） | 越过 bit addr | **T1 lld FAIL `CODE address overflow in .mcs251.xdata_init`**（既有 G13a，非本役） |
| 32 T1（修 A+B） | 越过 bit addr | **T1 lld FAIL `.mcs251.DSEG.2` printf.o 28B 窗口**（既有 G8，非本役） |
| 回归 | 其余 67 demo 无新 gap | 待 G6-S1 后重跑 |

### 6.3 风险

1. **顺序修复的边界**：兼容头有 guard，重复直接 include 安全；但若某文件在 shim 前
   已有**自己的** `#undef`/`#define` 依赖顺序，需逐个复核（当前 4 个 `.c` 已逐一实测）。
2. **`_nop_` 修复的副作用**：注入 `intrins.h` 后必须走 `-DMCS251_PORTING_ELF_NOP_HELPER`
   路径，否则 `__asm__ volatile("nop")` 在 ELF 链（无内联汇编解析器）下会失败。
   `drive.py` 的 `demo_compile()` 已按 `demo_uses_nop()` 自动加该宏，且 32 的
   `demo_uses_nop` 实测为 True，故无需额外改动。
3. **把"越过 bit addr"误记为"demo 通过"**：42/58/32 修完后 T1 仍失败于**其他既有缺口**
   （CODE 窗口 / DSEG 窗口）。验收必须按"**bit addr 归因归零**"记账，不得记 demo 全绿。
4. **`0xA9` 方案的诱惑**：见 §5.2，对本缺口零收益，避免把它塞进 G6 范围。

## 7. 基线（本役实测；工作树有未提交资产）

工具快照：clang mtime 20:25、llc mtime 22:16、lld 已构建（`build-mcs251-lld/bin`）。

| 套件 | 命令 | 结果 |
|---|---|---|
| llvm CodeGen/MCS251 | `python3 bin/llvm-lit -q test/CodeGen/MCS251`（`build-mcs251`） | **170 discovered / 169 PASS / 1 FAIL** |
| 该 1 FAIL | `softfloat-reject.ll` | 预存在、**并发 G7 役在飞状态**：`; UNSIGNED: LLVM ERROR: ... f32 conversion is not in the connected libcall subset` 期望未满足（与 G6 无关） |
| lld MCS251 | `python3 bin/llvm-lit -q tools/lld/test/MCS251`（`build-mcs251-lld`） | **23 / 23 PASS** |
| clang mcs251 拼单 | `python3 bin/llvm-lit -q <clang/test/{Sema,CodeGen}/mcs251-*>`（`build-mcs251-s1`） | **54 / 54 PASS** |

工作树未提交资产 **31** 项（`git status --short`，收稿时 33；本役只新增本文）：21 项 modified
（`clang/include/.../BuiltinsMCS251.td`、`DiagnosticSemaKinds.td`、
`clang/lib/CodeGen/{CGBuiltin,CGMCS251Bit,CodeGenFunction}.cpp`、
`clang/lib/Sema/SemaMCS251.cpp`、`llvm/include/llvm/IR/{IntrinsicsMCS251,RuntimeLibcalls}.td`、
`llvm/lib/Target/MCS251/*` 等）+ 10 项 untracked（TFPU 相关、`validation/mcs251-bit/bs4-*` 等）。
**这些是并行 G7/其他役的在飞改动，不是本役产物**；llc 构建时间晚于其中多数改动，
故上表数字含这些在飞状态，不代表干净 HEAD 的基线。

## 8. PM 决策点

| # | 事项 | 推荐 | 另一选择的代价 |
|---|---|---|---|
| **D1** | G6 归属：语料改写工具修复 vs 产品能力补强 | **语料工具修复（方案 A）**，产品零改动 | 若坚持产品侧，需 `0xA9` 全链（§5.2），跨 Sema/后端/ELF，收益与 G6 无关 |
| **D2** | 是否登记 MCS251 原生扩展位寻址 `0xA9` 通道为独立切片 | **登记为可选独立切片（G6-S3），本役不做** | 不登记则该硬件能力长期无产品通道；登记不承诺排期 |
| **D3** | 验收口径 | 按"**bit addr 归因归零**"记 G6 关闭；42/58/32 的 T1 失败归既有 G8/G13a | 若按 demo 全绿验收，会被 CODE/DSEG 窗口误算为 G6 未完成 |

## 9. 与已冻结裁定的边界核对

- **不推翻 P09 任何裁定**：bit 类型、对象句柄、值 ABI、`ImmArg i32` 位地址族、
  `sbit` 的 8 对齐基址约束全部原样；本役只动语料工具。
- **不新增 ELF/intrinsic 编号**：方案 A 不产生新 IR 指令或记录。
- **不开放拒绝表**：bit 指针/数组/位域/sizeof/原子/`bit xdata` 等全部维持。
- **`0xA9` 若未来启动**，涉及位地址表达式的**新操作数域**与前端放宽，
  按 `BIT-DECISION-20260911`/P07 流程另立协议冻结，不在本稿授权范围。

## 10. 停止规则

发现需要改变 P09 冻结面（bit 类型/句柄/值 ABI/位地址契约）、ELF 编号、
或语料改写规则本身时，执行者停止并带最小证据退 PM。合法形态因实现 bug 失败则退属主，
不降低测试断言。本稿交付本身不表示任何卡已获实施授权或测试 PASS。
