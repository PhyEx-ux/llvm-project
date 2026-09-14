# G1：MCS251 ISR profile 扩展正式设计（待用户仲裁）

**日期**：2026-09-12。  
**状态**：设计提案；所有“应／必须”均指用户批准后的实施契约，**本设计不授权实施**。  
**工作基线**：用户指定 `/home/liu/LLVM_STC32/MCS251`，分支 `minimal-isr`，HEAD `36d82d197`。本轮未执行任何 git 操作，未独立查询分支或提交身份。  
**唯一交付文件**：`/home/liu/LLVM_STC32/MCS251/validation/mcs251-models/proposals/G1-ISR-PROFILE-EXTENSION-DESIGN.md`。  
**输入**：已通读 `/home/liu/LLVM_STC32/mcs251-demos-rewritten/G1-INTERRUPT-PROFILE-EVIDENCE.md`。报告为取证基线；下列引用的源码、头文件和手册行号均经本轮对盘核实。未构建、未运行产品测试、未运行 QEMU、未烧录。  
**结构参照**：同目录 `/home/liu/LLVM_STC32/MCS251/validation/mcs251-models/proposals/RUNTIME-AS-PTR-DESIGN-A.md`，只参照“语义裁定→方案→切片→测试矩阵→风险”的结构，不继承该文的其他战役裁定或基线。

---

## 0. 设计结论与仲裁摘要

**G1 不是“把 52 改成 127”这么简单，而是一次设备拓扑、链接布局和冻结资产的联合修订。推荐采用 G144K246 证据最小 profile：0..126，共 127 槽，其中 Legal=109、Reserved=16、System=2；BOOT=0xFF0500，CSEG=0xFF0700；ISR 元数据协议版本由 1 升至 2。**

推荐组合如下，最终决定权均在用户。

| 编号 | 推荐裁定 | 主要理由 | 合理替代及代价 |
|---|---|---|---|
| D1 | 上界 126，即 127 槽 | 手册和官方头最大号均为 126；132 是宏数量，不是最大号 | 表长 133、覆盖 0..132，末六槽 Reserved；无新增能力，多占 48B 保留区 |
| D2 | 81、92、93、94、95、113 暂列 Reserved | 仅头文件有宏，§15.3 无行；fail-closed；13 个受阻 demo 均不需要这些槽 | 接受官方头为充分证据，六槽 Legal；多解锁六槽，但降低本片证据门槛，必须明确批准 |
| D3 | 31、45、46 改 Legal；7、13 保持 Reserved；14、15 保持 System | 三个重分类槽双源齐备；7 证据不足；13 有既有 transfer-slot 特殊语义 | 7 可在补证后另开；13 若开放须另设计转接语义，不能借 G1 顺带改成通用 ISR |
| D4 | 保留整个 VECS 区间禁占；BOOT=0xFF0500、CSEG=0xFF0700；BOOT 最低地址同步为 0xFF0500 | 当前 0x106B CRT 可完整容纳；256B 边界易审计；同一推荐配方也能容纳 D1 的 133 槽备选 | 更紧凑 0xFF0400/0xFF0600，或按实际区间放置；详见 §2 |
| D5 | `.mcs251.isr` 的 ProtocolVersion 1→2，严格拒绝旧 ISR 记录，不加自动降级 | 拓扑与整个默认向量覆盖面改变；旧 linker 即使接受低槽对象也不能履行新 profile；避免静默混用 | 保持 1，承认仅线格式版本并依赖整套工具／CRT 原子发布；迁移少但缺少机器可检查的语义隔离 |
| D6 | 人类诊断保留具体上界，但数字全部由共享常量生成 | 可操作性与单一注册点兼得；不能再各层写死 `0-126` | 完全不显示数字，文案更稳定但排障信息较少 |
| D7 | demo 41 仅对已核实 LCM_Interrupt 借槽站点批准 13→59 的改写例外 | 真源为 LCMIF/TFT，官方 LCM_VECTOR=59；无需解除 13 禁令 | 暂不批准改写，则产品扩表完成后 demo 41 仍为 isr-unresolved |

**三个必须分开的通过层次**：槽号可注册 ≠ 全 demo 可编译／链接 ≠ 外设运行合格。本片不承诺 13 个 demo 全绿，不开放优先级管理、额外保存状态、调用闭包安全、ABI v2、LTO/GC、自动清标志或自动使能中断。

### 0.1 对输入报告的设计级补充

本轮发现以下承重细节，必须覆盖输入报告 §4.2 的简化描述。

1. `/home/liu/LLVM_STC32/MCS251/lld/MCS251/LinkerCore.cpp:1198` 存在 **`InputSymbol *SlotSym[52]`**。注册写入、合成和 MAP 读取都按槽索引访问。只扩共享表会造成高槽越界访问，不能把 lld 描述成“仅改一条诊断，其余自动跟随”。
2. 同文件 `:2431-2438` 有独立 **BOOT 最低地址 0xFF0210** 检查；`:2389` 有旧 VECS 区间字符串 `[0xff0003,0xff01a3)`。二者不会随向量常量自动更新。
3. 当前 `/home/liu/LLVM_STC32/MCS251/validation/mcs251-elf/runtime/crt-irq.yaml:76-95` 的 BOOT 是 **0x106B（262B）**，含 XDATA_INIT walker；现盘真机发布 MAP 则仍记 **0xA0B**。新布局必须按当前源资产计算，旧验收产物不得只改地址／行数冒充重建。
4. 六个含界内失配的 demo 中，**68 同时还使用 90/91**。重分类解除它的 45/46 阻塞，但只有扩表后全部 G1 槽号阻塞才解除。
5. `/home/liu/LLVM_STC32/MCS251/validation/mcs251-isr/qualify.py:751-757` 的自测假模型有 `range(52)`，且原循环给所有号填 EJMP。它不是 QEMU 源码；同步时必须按 Legal 分类填充，不得只改成 `range(127)`。

---

## 1. 语义裁定与逐槽证据

### 1.1 证据口径与适用边界

本节表格使用两个已核实的绝对路径缩写，仅为减少逐行重复。

- **H** = `/home/liu/LLVM_STC32/STC32G144K246-DEMO-CODE/COMM/STC32G144K246.H`，宏块 `:2755-2887`。
- **M** = `/home/liu/LLVM_STC32/manuals-md/G144K246/15-中断系统及外部中断.md`，§15.3，`:177-299`，印 400–404／PDF 434–438。

本轮独立按字节读取 H 的 ASCII 宏行，避免其其他注释的混合编码干扰；计得 **132 宏、117 个不同号、最大号 126**。M §15.3 有 **109 个不同显式向量号**，四对定时器共用向量，合计 113 个中断源，不能把源数、宏数和槽数混用。独立程序核对 H 的全部 132 条地址注释及 M 的全部 109 条显式地址，均满足：

`VectorAddress(n) = 0xFF0003 + 8*n`。

这比输入报告的 20 点抽验更广，但仍只是现盘 MD 转写和官方头的交叉核对，不声称本轮重读了原 PDF 图版。§15.3 的缺行只支持“本 profile 暂不授权”，**不等于物理器件绝对没有该中断源**。

现行冻结表是旧家族口径。其来源见 `/home/liu/LLVM_STC32/MCS251/validation/mcs251-models/proposals/ISR-TASK-BREAKDOWN.md:405-412`；特别语义见该文 `:498-500` 及 `/home/liu/LLVM_STC32/MCS251/llvm/include/llvm/BinaryFormat/MCS251ISR.h:119-121`。本设计采用 G144K246 的号表，不声明所有旧 STC32G 板都有新增外设。低槽旧板回归必须重新跑；如产品必须同时保留旧板的“只能登记旧 39 槽”承诺，应另行批准带明确设备选择的多 profile，而非偷偷从 ELF ABI／HardwareProfileIRQ4 推断器件。

### 1.2 上界：127 槽优于 133 槽

| 方案 | 号域 | VECS 半开区间 | 面积 | 推荐分类统计（† 保留） | 判断 |
|---|---|---|---:|---|---|
| **推荐最小扩展** | 0..126 | `[0xFF0003,0xFF03FB)` | 0x3F8 = 1016B | 109 / 16 / 2 | 与正证最大号一致 |
| Reserved 填充扩展 | 0..132 | `[0xFF0003,0xFF042B)` | 0x428 = 1064B | 109 / 22 / 2 | 127..132 无源，仅保留余量，不产生新 ISR 能力 |

上界包括界内 Reserved/System；`n <= MaxSlot` 从来不等于 Legal。若用户选择填充方案，127 的测试仍应拒绝，但拒绝原因改为“界内 Reserved”；第一个越界号变为 133。不得把“132 宏”解释为“0..131”，也不得把“到 133”解释为未经裁定的号 133。

### 1.3 52..126 逐号分类依据表（本设计规范表）

记号：**L=Legal**；**R†=Reserved，只有 H 宏而 M §15.3 无行**；**R∅=Reserved，H/M 均无源**。表中宏统一省略 `_VECTOR` 后缀；斜线表示同号别名，不是额外槽。每个 L 行均同时有手册行和官方头行。表内 M 行所对应的地址已按上述公式独立复算。

| 号 | H 宏名（省略后缀） | H 行 | M 行 | 推荐 |
|---:|---|---|---|---|
| 52 | DMA_UR2T | 2806 | 223 | L |
| 53 | DMA_UR2R | 2807 | 224 | L |
| 54 | DMA_UR3T | 2808 | 225 | L |
| 55 | DMA_UR3R | 2809 | 226 | L |
| 56 | DMA_UR4T | 2810 | 227 | L |
| 57 | DMA_UR4R | 2811 | 228 | L |
| 58 | DMA_LCM | 2812 | 229 | L |
| 59 | LCM | 2813 | 230 | L |
| 60 | DMA_I2CT / DMA_I2C1T | 2814–2815 | 231 | L |
| 61 | DMA_I2CR / DMA_I2C1R | 2816–2817 | 232 | L |
| 62 | I2S / I2S1 | 2818–2819 | 233 | L |
| 63 | DMA_I2ST / DMA_I2S1T | 2820–2821 | 234 | L |
| 64 | DMA_I2SR / DMA_I2S1R | 2822–2823 | 235 | L |
| 65 | DMA_QSPI | 2824 | 236 | L |
| 66 | QSPI | 2825 | 237 | L |
| 67 | TMR11 | 2826 | 238 | L |
| 68 | DMA_I2C2T | 2827 | 239 | L |
| 69 | DMA_I2C2R | 2828 | 240 | L |
| 70 | DMA_I2S2T | 2829 | 241 | L |
| 71 | DMA_I2S2R | 2830 | 242 | L |
| 72 | DMA_PWMAT | 2831 | 243 | L |
| 73 | DMA_PWMAR | 2832 | 244 | L |
| 74 | DMA_PWMCT | 2833 | 245 | L |
| 75 | DMA_PWMCR | 2834 | 246 | L |
| 76 | DMA_ADC2 | 2835 | 247 | L |
| 77 | DMA_DAC / DMA_DAC1 | 2836–2837 | 248 | L |
| 78 | DMA_DAC2 | 2838 | 249 | L |
| 79 | DMA_SPI2 | 2839 | 250 | L |
| 80 | DMA_SPI3 | 2840 | 251 | L |
| 81 | DMA_SPI4 | 2841 | 无 | R† |
| 82 | DMA_UR5T | 2842 | 252 | L |
| 83 | DMA_UR5R | 2843 | 253 | L |
| 84 | DMA_UR6T | 2844 | 254 | L |
| 85 | DMA_UR6R | 2845 | 255 | L |
| 86 | DMA_UR7T | 2846 | 256 | L |
| 87 | DMA_UR7R | 2847 | 257 | L |
| 88 | DMA_UR8T | 2848 | 258 | L |
| 89 | DMA_UR8R | 2849 | 259 | L |
| 90 | PAINT | 2850 | 260 | L |
| 91 | PBINT | 2851 | 261 | L |
| 92 | PCINT | 2852 | 无 | R† |
| 93 | PDINT | 2853 | 无 | R† |
| 94 | PEINT | 2854 | 无 | R† |
| 95 | PFINT | 2855 | 无 | R† |
| 96 | TMR5_TMR6 | 2856 | 264–265（共用） | L |
| 97 | TMR7_TMR8 | 2857 | 266–267（共用） | L |
| 98 | TMR9_TMR10 | 2858 | 268–269（共用） | L |
| 99 | TMR17_TMR18 | 2859 | 270–271（共用） | L |
| 100 | 无 | 无 | 无 | R∅ |
| 101 | 无 | 无 | 无 | R∅ |
| 102 | UART5 | 2860 | 272 | L |
| 103 | UART6 | 2861 | 273 | L |
| 104 | UART7 | 2862 | 274 | L |
| 105 | UART8 | 2863 | 275 | L |
| 106 | ADC2 | 2864 | 276 | L |
| 107 | DAC / DAC1 | 2865–2866 | 277 | L |
| 108 | DAC2 | 2867 | 278 | L |
| 109 | I2C2 | 2868 | 279 | L |
| 110 | I2S2 | 2869 | 280 | L |
| 111 | SPI2 | 2870 | 281 | L |
| 112 | SPI3 | 2871 | 282 | L |
| 113 | SPI4 | 2872 | 无 | R† |
| 114 | CMP2 | 2873 | 283 | L |
| 115 | CMP3 | 2874 | 284 | L |
| 116 | CMP4 | 2875 | 285 | L |
| 117 | DMA_CANT / DMA_CAN1T | 2876–2877 | 286 | L |
| 118 | DMA_CANR / DMA_CAN1R | 2878–2879 | 287 | L |
| 119 | DMA_CAN2T | 2880 | 288 | L |
| 120 | DMA_CAN2R | 2881 | 289 | L |
| 121 | PWMC | 2882 | 290 | L |
| 122 | PWMD | 2883 | 291 | L |
| 123 | PWME | 2884 | 292 | L |
| 124 | PWMF | 2885 | 293 | L |
| 125 | DMA_PWMET | 2886 | 294 | L |
| 126 | DMA_PWMER | 2887 | 297 | L |

计数：52..126 共 75 槽 = **67 L + 6 R† + 2 R∅**。别名不得重复计数；96..99 各自只能注册一个函数，由应用处理共享源。

**† 类两种裁定的权衡**：

- **Legal**：官方头属于一手资料，且号和地址公式一致；可避免未来 DMA_SPI4／PC..PF／SPI4 用户再受阻。缺点是缺少本片采用的手册表行，尚不能排除跨型号头文件残留、未文档化功能或器件变体，默认向量也会因此为这些槽生成 EJMP。
- **Reserved（推荐）**：保持“证据不足不开放”，不增加这 13 个 demo 的剩余 G1 阻塞；整个 8B 仍保留且无载荷。代价是即便硬件事实上支持，用户也暂不能注册；必须明确记录“待补证”，不是永久判定无硬件。
- 若全部六槽改 Legal：127 槽统计变为 **115 / 10 / 2**；133 槽为 **115 / 16 / 2**。测试正例矩阵相应 109→115；不得把该选择连带套用到 7 或 13。
- 无源 100/101 无论以上哪种方案都保持 Reserved；127..132 若存在也全部 Reserved。

### 1.4 0..51 界内重分类与特殊槽

| 号 | 冻结分类 | H 证据 | M §15.3 | 推荐裁定 |
|---:|---|---|---|---|
| 31 | Reserved | LIN2_VECTOR，H:2786 | M:206，FF:00FBH | Legal，源名 LIN2 |
| 45 | Reserved | P8INT_VECTOR，H:2797 | M:216，FF:016BH | Legal，源名 P8 |
| 46 | Reserved | P9INT_VECTOR，H:2798 | M:217，FF:0173H | Legal，源名 P9 |
| 7 | Reserved | PCA_VECTOR，H:2763 | 无行 | 保持 Reserved；单头证据不足，不顺带开放 PCA |
| 13 | Reserved transfer slot | USER_VECTOR，H:2770 | 无行 | 保持 Reserved，不能注册一般用户 ISR，不能全局 13→59 |
| 14、15 | System | 无宏 | 无行 | 保持 System，禁止用户注册 |

其余旧 Legal 保持 Legal；22/23、32..35 保持 Reserved。推荐最终集合可独立审计为：

- `System = {14, 15}`。
- `Reserved = {7, 13, 22, 23, 32, 33, 34, 35, 81, 92, 93, 94, 95, 100, 101, 113}`。
- `Legal = [0,126] \ (Reserved ∪ System)`，**109 个**。
- 统计推导：旧 39 Legal + 界内重分类 3 + 高槽双源 67 = 109；旧 11 Reserved − 3 + 高槽 8 = 16；System=2。

**demo 41**：本轮核实官方 `/home/liu/LLVM_STC32/STC32G144K246-DEMO-CODE/41-3.2寸ILI9341驱动TFT显示屏实验程序-硬件I8080接口程序/LCD/LCD.c` 中 `LCM_Interrupt` 使用 `interrupt 13`，函数清 `LCMIFSTA`；M:230 的 TFT 彩屏源和 H:2813 的 LCM_VECTOR 均为 59。推荐只为此明确站点批准源特定改写；DMA_LCM=58 是另一个源，不能混用。保留 demo 35 的既有 13→36 例外，证明“一刀切 13→59”错误。

### 1.5 profile 所不表达的能力

Legal 只表示本 profile 允许登记该向量，不证明具体板外设存在、QEMU 可注入、外设标志清除正确、优先级配置正确或 ISR 调用闭包安全。HardwareProfileIRQ4=1 和 SaveProfileINT37=1 继续只表达硬件帧／软件保存约定，不用它们充当 G144K246 器件 ID。

---

## 2. BOOT/CSEG 重排方案及协议兼容

### 2.1 冲突证明

推荐 VECS 完整保留 `[0xFF0003,0xFF03FB)`。旧 BOOT 起点 0xFF0210 位于槽 65 的 `[0xFF020B,0xFF0213)`，准确地说落在其后 4B 无载荷尾部；**尾部没有 PT_LOAD 字节，不等于可放 BOOT**。现行冻结条款明确禁止利用空洞，见 `/home/liu/LLVM_STC32/MCS251/validation/mcs251-models/proposals/ISR-TASK-BREAKDOWN.md:513-522`。

旧 CSEG=0xFF0400 与新 VECS end 仅距 5B。当前 BOOT 为 0x106B，无法插入。133 槽备选的 end=0xFF042B 则直接覆盖旧 CSEG 起点。

### 2.2 布局方案比较

| 方案 | BOOT / CSEG | 契约与优点 | 代价／判断 |
|---|---|---|---|
| **A：整体上移，推荐** | **0xFF0500 / 0xFF0700** | VECS 全区禁占不变；同 FF bank；BOOT/CSEG 均 256B 对齐；兼容 127／133 槽两种仲裁；当前 BOOT 后有 250B 余量 | 相对旧 CSEG 上移 0x300B；需同步所有 IRQ 资产，重新检查 ROM 容量 |
| A-tight：紧凑上移 | 0xFF0400 / 0xFF0600 | 对 127 槽足够；VECS 后 5B 空隙；比 A 多留 256B 应用 ROM | 与 133 槽备选不兼容；后续多六槽便又要变更 BOOT；可接受但非推荐 |
| B：按真实区间约束 | BOOT 至少在 VECS end，CSEG 至少在实际 BOOT end；配方取对齐后的值 | 不再保留额外固定 floor；节约保留余量；更通用 | 必须重订“floor”契约、错误优先级及配方计算；BOOT 大小变化影响地址稳定性；本片不必为扩表引入自动放置器 |
| C：只禁止实际 EJMP 字节、让 BOOT 占向量尾洞 | 尝试保留旧 BOOT | 表面上少改地址 | **不认可**：违反冻结全区保留；当前 262B BOOT 连续跨过后续 Legal 槽；不是安全替代 |

推荐 A 的精确配方：

| 区域 | 起点／区间 | 说明 |
|---|---|---|
| HOME | `[0xFF0000,0xFF0003)` | 仍恰好 3B LJMP；不能改 4B EJMP |
| VECS | `[0xFF0003,0xFF03FB)` | 127 格 × 8B，包含所有尾洞／Reserved／System |
| 间隙 | `[0xFF03FB,0xFF0500)` | 0x105B；配方不使用，不发射填充载荷；不是新声明的通用 CODE 禁区 |
| BOOT | `[0xFF0500,0xFF0606)` | 当前 0x106B CRT；BOOT entry=0xFF0500 |
| default entry | `[0xFF0602,0xFF0606)` | BOOT+0x102，仍为 C2 AF 80 FE |
| BOOT 后配方余量 | `[0xFF0606,0xFF0700)` | 0xFA=250B；CRT 长大到跨过 CSEG 时必须报重叠 |
| CSEG | 从 0xFF0700 起 | 由实际节面积、初始化区和 flash gate 限制，不能宣称无限可用 |
| XINIT | 0xFF8000 | 不因 G1 改动；改写包的 XDATA_INIT=0xFF9000 也保留并验重叠 |

256B 对齐是**配方审计与余量选择**，不是 MCS251 指令或 ELF 节对齐硬性要求。当前 CRT 的 AddressAlign=1，故不凭空要求所有用户 BOOT 256B 对齐。共享 `ISRBootMinAddress` 推荐固定为 **0xFF0500**，加断言 `ISRBootMinAddress >= ISRVectorEnd`；不能各处重复魔数。链接参数仍显式提供 BOOT/CSEG，**不声称 lld 已有自动 BOOT 默认值**。

保持全部检查：VECS 固定起点、完整区间无非合成 CODE、HOME 大小及同 CRT 配对、reset 零 addend 且命中 BOOT 入口、J16 同 bank、default 字节及禁止普通引用、BOOT/CSEG 互斥、flash window。新 HOME 最终机器字节应为 **02 05 00**；CRT 内部 ECALL 地址由原重定位重新求值，不手改模板操作码或相对分支。

### 2.3 为什么推荐 ProtocolVersion bump

`/home/liu/LLVM_STC32/MCS251/llvm/include/llvm/BinaryFormat/MCS251ISR.h:38` 当前 ProtocolVersion=1；`:39-58` 明确记录长 24B、槽字段 u16。**字段宽度和布局不要求 bump，但语义隔离建议 bump。**

反例：新编译器只登记 Timer0=1 的对象，若仍发 version=1，旧 lld 可成功链接，只生成旧 52 槽表；它不会给新 profile 的其他 Legal 槽合成默认 fail-stop。新 CRT 若也保持 version=1，旧 lld 在 BOOT 已上移的配方下也可能接受它。故“旧 lld 会拒绝 >51”不足以保证新契约不会被静默降级。

推荐把 ISR ProtocolVersion=2 定义为**本片获批拓扑与布局下限的语义修订**，并采用以下迁移矩阵：

| 生产者／输入 | 新 ISR reader | 旧 ISR reader |
|---|---|---|
| 新 compiler ISR + 新 IRQ CRT，均 version=2 | 按 127 槽新规则接受 | unsupported metadata version，即使只登记低槽也拒绝 |
| 旧 ISR 对象或旧 IRQ CRT，version=1 | 明确拒绝；要求重建 | 保持旧工具既有行为，不能当作新 profile 验收 |
| version=1 与 2 的 ISR 记录混合 | 拒绝，不自动选最低版本／兼容子集 | 拒绝 version=2 |
| 无 `.mcs251.isr` 的普通对象 | 保持其他既有 ABI／bit 等门禁；不因 G1 强行添 ISR 记录 | 不作新能力承诺 |

保持 **RecordSize=24、RequiredCaps=0x0001、NoSlot=0xFFFF、record/entry kinds、IRQ4/INT37、AssetProfileCRT=1、AssetProfileCompiled=0** 不变。这里的“2”**不是 ELF ABI v2、ASLayoutVersion=2 或 `.mcs251.attributes` 的 ObjectProtocolVersion=2**；那些能力不连带开放。ISR 元数据版本改变也不允许改变固定保存序列和 RETI。

代价：所有带 ISR 记录的对象（含手写 YAML fixture）和 IRQ CRT 必须重建；旧资格结果失效。合理替代是保持 version=1 并将它严格解释成线格式版本，依赖新工具／新配方／新 CRT 的发行包锁定；能省去重建低槽对象，但无对象内 profile 区分。**鉴于接口冻结含槽分类及语义，本设计推荐 fail-closed 的 bump，而不是用“字节布局没变”回避兼容问题。** 若用户需要并存旧 profile，应另设计 profile ID／多 reader，不能在本片临时接受 1 和 2 而无明确布局选择。

### 2.4 受冻结配方约束的完整资产面

以下均为**未来同步项，本轮未改**。为避免漏掉被忽略的产物，本轮还对 validation 做了包括隐藏／忽略目录的只读检索；不使用 git 来判定单个文件的跟踪状态。已在仓库工作树中的真机资产全部纳入，不把现盘历史二进制等同于当前源码重建结果。

| 资产面／绝对路径 | 同步要求 |
|---|---|
| `/home/liu/LLVM_STC32/MCS251/validation/mcs251-models/proposals/ISR-TASK-BREAKDOWN.md` | 经用户→PM→design owner 修订 §0.1（当前 :50 范围、:56 BOOT、:62 禁扩展）、A3 协议、A4 表、A5 区间、T07/T08 验收配方和 MAP／矩阵统计。旧条款作为历史，不在同一有效规范中留下互相矛盾的“冻结” |
| `/home/liu/LLVM_STC32/MCS251/validation/mcs251-models/proposals/MINIMAL-ISR-SLICE.md` | 标明旧 52 槽及 BOOT 方案已由获批 G1 修订；保留历史语境，不改写历史证据 |
| `/home/liu/LLVM_STC32/MCS251/lld/test/MCS251/isr-vectors.test` | 所有正常链接和单因素负例的 BOOT/CSEG 改新配方；专门旧布局负例保留旧值；更新 MAP、ROM 边界、BOOT floor、版本及逐槽矩阵 |
| `/home/liu/LLVM_STC32/MCS251/lld/test/MCS251/Inputs/isr-fixture.py` | 独立 Legal 副本 39→109；合法记录 version=2；version mutation 不再误用合法的 2，改 1／3；其余单因素 mutation 不得因旧版本先失败 |
| `/home/liu/LLVM_STC32/MCS251/lld/test/MCS251/Inputs/isr-check-image.py` | COUNT、NON_LEGAL、BOOT_FLOOR、头部文字、MAP 127 行；保持独立 PT_LOAD 解析与逐槽字节检查，不导入产品表当 oracle |
| `/home/liu/LLVM_STC32/MCS251/validation/mcs251-elf/runtime/crt-irq.yaml` | 更新布局文字和两条 ISR 记录的 version；保持 262B BOOT 模板、12 个 BOOT 重定位、两条记录及相对符号偏移不变；重新生成对象 |
| `/home/liu/LLVM_STC32/MCS251/validation/mcs251-elf/runtime/check-crt-irq.py` | DEFAULT/RESET 记录版本金样 1→2（包括字段字典和实际 header 断言）；BOOT_SIZE=0x106、指令边界和重定位模板不因搬家变化 |
| `/home/liu/LLVM_STC32/MCS251/validation/mcs251-elf/runtime/gen-crt-irq.sh`、同目录 `README.md` | 重新走既有生成／核验入口，登记新配方和版本；README 中 selfstart 的旧地址须保留其非 IRQ 语境 |
| `/home/liu/LLVM_STC32/MCS251/validation/mcs251-isr/compile-matrix.py` | :95-107 的 COUNT/NON_LEGAL/BOOT_FLOOR/LINK_AREAS 全同步；:116/:120 诊断引用、neg-slot52→neg-slot127；**:370 的独立对象记录版本检查 `ver != 1` 改为获批版本2**，记录长仍24；加入高槽和重分类用例 |
| `/home/liu/LLVM_STC32/MCS251/validation/mcs251-isr/qualify.py`、`qualification.test`、`firmware.c` | 同步假模型 range、地址和 MAP 说明；重新运行协议自测与实际 qualification；firmware 原有保存窗口用例保留，另增高槽静态用例，不假设硬件注入编号等于槽号 |
| `/home/liu/LLVM_STC32/MCS251/validation/mcs251-isr/Output/image/*.map`、同目录关联 ELF/HEX 与 `/home/liu/LLVM_STC32/MCS251/validation/mcs251-isr/Output/matrix-result.json` | 生成物全部从新工具／CRT 重建；更新 hash、路径清单和检查结论，不文本替换成新金样 |
| `/home/liu/LLVM_STC32/MCS251/validation/mcs251-isr/realhw-demo/build.sh`、`check.py`、`README.md` | build.sh:30 是现盘旧布局；checker／说明重新审计；原板行为重新验收，不能继承旧“静态通过” |
| `/home/liu/LLVM_STC32/MCS251/validation/mcs251-isr/realhw-demo/release/` | 包括 `t10-g12.map`、`t10-g12.elf`、`t10-g12.hex`、`crt.o`、带 ISR 记录的对象、`manifest.json`、`STATIC-CHECKS.txt` 及关联反汇编。MAP:3/:23 为旧 0xA0B BOOT，:13/:15 为旧起点，不能假定其来自当前 0x106B CRT |
| `/home/liu/LLVM_STC32/MCS251/validation/mcs251-isr/v1-psw1-investigation/build.sh`、`check.py`、`gen-v1.py` | build.sh:32 配方；check.py:44-47 的 reset 02 02 10→02 05 00；gen-v1.py:175 的手写 ISR 记录版本同步；三臂镜像、MAP、manifest 一起重建，不能改变实验臂语义 |
| `/home/liu/LLVM_STC32/MCS251/validation/mcs251-isr/v2-clobber-nesting/clobber/build.sh`、`check.py` | build.sh:47 配方；check.py:45 的 reset 字节；生成的 ELF/HEX/MAP／manifest 重建 |
| `/home/liu/LLVM_STC32/MCS251/validation/mcs251-isr/v2-clobber-nesting/nesting/build.sh`、`check.py` | build.sh:45 配方；检查派生 ISR 地址、嵌套结果及产物 hash；不把目录名 v2 当 ELF ABI v2 |
| `/home/liu/LLVM_STC32/mcs251-demos-rewritten/tools/drive.py`（仓库外） | :49 的 AREA_ARGS_IRQ 改 BOOT/CSEG；:338/:354 诊断签名与归因标签同步；AREA_ARGS 非 IRQ 配方不动 |
| `/home/liu/LLVM_STC32/mcs251-demos-rewritten/tools/crt/crt-irq.o`（仓库外） | 从获批 version=2 CRT 重建并核 hash，避免 drive.py 继续使用旧镜像 |

**qualification 内地址分类，禁止全局替换**：

- `qualify.py:717-720` 的 CRT_LOOP、MAIN_LOOP、ISR_ENTRY、DEFAULT_ENTRY 是假模型执行地址，应一起重排并保持互不覆盖；例如 CRT=0xFF0500、MAIN=0xFF0700、ISR=0xFF0800/0xFF0900、DEFAULT=0xFF0A00。此 DEFAULT 只是自测替身，真实 CRT default 必须由符号／MAP 求得（当前推荐 0xFF0602）。
- `qualify.py` 中 MAP/BOOT 保护范围自测（含现盘 :1749、:1776-1784、:2186-2194）应同步新布局；用于保护实际固件的范围必须来自实际节面积，不把旧 0x9A/0x100 写成当前 CRT 大小。
- RSP 寄存器编解码及硬件帧单元测试里的 0x00FF0210（如 :1190、:1214、:2063-2065）可作为**任意 24 位测试值**保留；若保留须注释不代表真实 BOOT。若改值，packet checksum、字节序期望也一起改。
- 现盘 `/home/liu/LLVM_STC32/MCS251/validation/mcs251-isr/hwframe/` 的独立硬件帧探针、`validation/mcs251-demo-test/t4-probes/` 历史 `.rst` 里偶然出现的 FF0210，不自动升级为新 IRQ CRT 资产。需按入口／是否消费 `.mcs251.isr` 判定，历史记录不覆写。

**检索命中但不应整体迁移的非 IRQ 面**：`/home/liu/LLVM_STC32/MCS251/validation/mcs251-firmware/`、`mcs251-demo-modern/`、`mcs251-porting/`、`mcs251-uartdemo-33m/`、`mcs251-runtime/acceptance/`、`mcs251-elf/runtime/acceptance-host/`、`mcs251-xdata-e2e/`、`mcs251-demos/` 和 `mcs251-ld/` 中仍有 BOOT 或 0xFF0100 selfstart 配方。它们并非全部属于 ISR 注册链，须保留非 IRQ 行为回归，不用全文替换把旧 selfstart 改成新 IRQ CRT。BIT 任务书引用旧 IRQ CRT 作为另片资产边界，G1 仅修订 ISR profile／协议／布局，不连带实施 bit-aware CRT。

---

## 3. 产品实现方案（7 文件清单）

### 3.1 单一注册点与新表结构

推荐仍由 `/home/liu/LLVM_STC32/MCS251/llvm/include/llvm/BinaryFormat/MCS251ISR.h` 承担产品唯一权威。不从安装环境中的官方 H／MD 动态解析表，也不让 Sema、后端或 lld 拥有第二个 Legal 集合。

**拓扑结构推荐**：保留 `ISRSlotKind` 三分类，使用逐号、稠密、显式初始化的 `ISRSlots[]`（推导数组长度，避免漏写尾行后被零初始化为 Legal）。补充带来源身份的证据描述，取代只含单个 `PDFPage` 的模糊表达：

- `Kind`：唯一执法字段。
- `EvidenceClass`：`DualSource`、`HeaderOnly`、`NoSource`、`LegacySpecial`。
- `ManualLine`：本设计 M 路径的显式行，0 表示无对应行。
- `HeaderLine`：H 中该号第一个宏行，0 表示无宏；全部别名保留逐行注释／设计表。
- `LegacyPDFPage`：只用于保留 13/14/15 的旧家族特殊语义证据，且明确旧手册身份；其他行可为 0。

M 的统一章节页域 PDF434–438／印400–404 放在表头证据声明；**不得给新槽伪填旧 PDF674–676，也不得未查图版就给每行猜精确 PDF 页**。这些字段只服务审计，不进入 ELF 的 24B 序列化。合理的轻量替代是保持两字段结构、用注释补来源，但未来容易把不同手册的页号混淆，故推荐显式证据类型。实施者需检索所有 `PDFPage` 使用点，若发现外部消费者再报 owner，不保留有歧义的假值凑兼容。

统一常量与断言：

1. `ISRVectorCount = sizeof(ISRSlots)/sizeof(ISRSlots[0])`，`ISRVectorMaxSlot = ISRVectorCount - 1`；显式 `static_assert(ISRVectorCount == 127)` 为获批 profile 锚点。
2. `countSlots` 用 Count 终止，推荐 constexpr 循环，去掉现盘 `I == 52`；断言 **Legal=109、Reserved=16、System=2、总数=127**。
3. 保持 Base=0xFF0003、Stride=8；End 从公式导出并断言 **0xFF03FB**；断言在 24 位域内且小于等于 BOOT floor。
4. `isLegalISRSlot` 先做范围判断再索引；推荐接受 `uint64_t` 输入，防调用方把超宽号截成有效 unsigned 后误放行，返回条件仍是 `N < Count && Kind==Legal`。
5. BOOT floor 单一常量 **ISRBootMinAddress=0xFF0500**；CSEG=0xFF0700 为发布配方，不硬编码为所有用户链接的固定地址。
6. 所有 header 注释中的旧 52／39／0-51 和“第一冻结修订”语义一并修订；保留 `:16-17` 的仲裁纪律。

**文本解析推荐共享化**：在同一 header 提供小型、无重型依赖的 canonical 十进制解析助手，供 Verifier、ContractCheck、AsmPrinter 调用。拒绝空串、符号、空白、前导零（单个 0 除外）、十六进制文本、非数字和超界；在乘 10 前进行 MaxSlot 溢出／上界检查，超界立即返回失败，绝不继续累积再截断。解析成功后仍调用 `isLegalISRSlot`，Reserved/System 不能由“解析成功”放行。保留三个层级各自的调用和诊断，共享解析器不等于取消防御层。替代是保留三份 parser 但统一上限常量，改动更小、未来漂移更大，不推荐。

### 3.2 统一诊断裁定

**保留数字，但由常量生成，不在产品中重新硬编码 `0-126`。**

- Clang `.td` 模板改为 `MCS251 interrupt vector must be a legal slot in 0-%0`，Sema 传入 `ISRVectorMaxSlot`。
- Verifier／ContractCheck／AsmPrinter／lld 使用同一词干 `MCS251 ISR: vector is not a legal slot in profile 0-`，上界由共享常量格式化；推荐 header 注册共享词干，调用层按自身 StringRef／Twine／std::string API 组合并管理生命周期，避免返回悬空 StringRef。
- 推荐 profile 下人类最终看到 `0-126`。文案仍明确“legal slot”，不能改成“整数在范围内即可”。非法文本、Reserved、System 和越界继续使用该槽号错误，不借本片拆出不必要的新诊断 ID。
- lld VECS 重叠消息中的两个地址、BOOT floor 消息中的地址也从常量格式化，避免实际执行新范围却打印旧范围。
- 测试 oracle 中允许明确写 `0-126` 作为获批行为金样；改写包归因应匹配稳定词干并解析范围，不再把特定 `0-51` 当唯一 G1 签名。诊断是人类接口，不承诺完整字符串是永久机器 API。

### 3.3 按文件的改动形态

| # | 产品文件（绝对路径） | 已核实位置与未来改动 |
|---:|---|---|
| 1 | `/home/liu/LLVM_STC32/MCS251/llvm/include/llvm/BinaryFormat/MCS251ISR.h` | :38 协议 1→2；:100-176 表结构／127 行／31、45、46 重分类／统计／范围助手；:182-186 Count/End；新增 MaxSlot、BOOT floor、共享 parser／词干；其余记录字段及 default bytes 不变 |
| 2 | `/home/liu/LLVM_STC32/MCS251/clang/lib/Sema/SemaDeclAttr.cpp` | :6763 `Vector->ugt(51)` 改比较共享 MaxSlot；保持负数检查在先、宽 APInt 检查先于取低位；:6765 诊断传入上界；保留其他声明身份／函数类型门禁 |
| 3 | `/home/liu/LLVM_STC32/MCS251/clang/include/clang/Basic/DiagnosticSemaKinds.td` | :12705-12706 改参数化范围模板，保留诊断 ID；不得仅将字符串替换为另一硬编码 |
| 4 | `/home/liu/LLVM_STC32/MCS251/llvm/lib/IR/Verifier.cpp` | :3201-3217 改用共享 canonical parser＋合法性检查，消除 `>51`；动态生成消息；更新 profile 注释，CC／属性绑定及其他验证不变 |
| 5 | `/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCS251ContractCheck.cpp` | :464-476 本地 parser 改共享调用／删除重复实现；:633-635 拒绝消息统一；保留后端独立复检 |
| 6 | `/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCS251AsmPrinter.cpp` | :149-160 parser 改共享；:1019-1023 对象边界复检消息统一；:1033 元数据发射引用 ProtocolVersion 自动变 2；24B 记录、u16 槽、RELA 不变 |
| 7 | `/home/liu/LLVM_STC32/MCS251/lld/MCS251/LinkerCore.cpp` | :841-843 消息；**:1198 SlotSym[52] 改由 ISRVectorCount 定长并零初始化**；:2389 VECS end 文案动态化；:2431-2438 BOOT floor 判断／消息改共享常量；52／39／两位槽号注释同步 |

**lld 自动跟随部分（有条件）**：

- `:1451-1489` 合成循环／公式／分类、`:3577-3598` MAP 循环／分类、`:2367/:2383-2384` VECS 固定起点和区间判断都引用共享常量。
- **前提是 SlotSym 容量先修正**。否则即便没有注册高槽，给新 Legal 槽生成 DEFAULT 时也会读取越界数组。
- `:792-793` metadata reader 和 AsmPrinter 都引用 ProtocolVersion；bump 后自然形成严格单版本门禁，测试和手写记录须同步。
- MAP 槽号现有实现是**至少两位**，100..126 会自然输出三位；不改成固定两字符宽，不截断、不按名字字典序改槽序。
- Legal 仍为前 4B EJMP＋后 4B NOBITS；非 Legal 为 8B NOBITS；不压缩槽，不写 NOP，不用普通 R_MCS251_24 之外的新跳转编码。
- LinkerCore 中另有“52B ABI note”校验（现盘 :608），以及 ELF32 header／note checker 的 52B，**不属于向量计数，禁止机械替换**。

不列入新增产品改动：CodeGenModule 属性值仍可透传；Keil parser 与声明链身份逻辑没有独立范围表；调用约定、FrameLowering、ISelLowering 无需为高槽改变保存／返回序列。若实施发现新增改动需求，按冻结纪律报 owner，而非顺手扩围。

---

## 4. 实施切片、依赖与工作量（未来计划）

单位为有效工程人日，含本片编码／测试／review 修复，不含排队构建、用户仲裁、板卡运输和外设等待。估计不是已用工时。

| 切片 | 内容／交付 | 前置 | 估计 | 完成门槛 |
|---|---|---|---:|---|
| G1-0 裁定与实施期首查 | 用户确认 D1–D7；PM/owner 修订有效冻结契约；**首先查 QEMU 的实际向量模型与机器来源**；锁定目标板／工具版本 | 用户另行授权实施 | 1–1.5 | 127 槽统计、† 策略、布局、协议迁移和模型能力边界无悬空结论 |
| G1-1 注册表与五层检查 | 7 产品文件；显式证据表／常量／parser／诊断；SlotSym 容量修复；ProtocolVersion=2 | G1-0 | 1.5–2 | 静态断言、Sema／IR／后端边界测试；高槽无越界；不单独发布 |
| G1-2 链接布局与 CRT | BOOT floor、区间文案；IRQ CRT metadata；独立 CRT checker；配方／重定位验证 | G1-1 接口冻结 | 1–1.5 | 127 槽镜像与 HOME→BOOT→main；旧配方拒绝；当前 BOOT 字节形状不变 |
| G1-3 完整测试矩阵 | lld 109 个逐槽、18 非 Legal、越界、全注册、MAP／PT_LOAD／ROM；clang/LLVM 全回归；独立镜像副本同步 | G1-1/G1-2 | 2–3 | §5 所列矩阵全部有真实结果；每个负例确认命中预期门禁 |
| G1-4 改写包与 qualification | rewrite/drive/CRT 镜像；demo41 精确例外；compile-matrix／qualify 假模型；13 demo 重新记账 | D7 获批、G1-2 | 1–1.5 | G1 blocker 分层归因清楚；其他 gap 不吞掉；模型不支持不记 PASS |
| G1-5 发布资产与真机回归 | 重建仓库现有真机资产、MAP／HEX／hash；低槽旧板回归；G144K246 高槽实测计划／结果；owner review | G1-3/G1-4 | 1.5–2.5 | 全套工具／CRT／脚本版本一致，实机结果与未测项分开，用户接受发布 |

**合计 8–12 人日**。已获许可的构建／review 可并行准备，预计日历约 1.5–2.5 周，取决于模型源码和真机可用性。若需要修改 QEMU 中断控制器、增加外设或设计旧／新多 profile，超出此估计，必须单列任务，不把未知模型改动承诺成一天修完。

共享 header 和 LinkerCore 应由单一 owner 串行收敛，避免一人在改分类、另一人按旧数组长度开发。表、协议、CRT、lld 配方必须作为一个可发布单元；中间切片只可用于隔离开发测试，不能向改写包发布“新 compiler＋旧 lld/CRT”。回退也应整套回退发行资产，不尝试只降诊断或只恢复 LEGAL_SLOTS。

---

## 5. 测试矩阵与验收规格

### 5.1 Sema／语法／CodeGen

| 维度 | 用例 | 推荐预期 |
|---|---|---|
| 上界正负 | 126、127、132、133、65535、-1、超宽 0x100000001ULL | 126 接受；其余拒绝；先比较 APInt 再取值，不截断成低槽 |
| 原负例变正 | 31、45、46、52、64 | 全接受；不能只删除旧 expected-error，须有独立正例证明合法 |
| Reserved | 7、13、22、23、32..35、81、92..95、100、101、113 | 全拒绝，最终文案范围 0-126 |
| System | 14、15 | 拒绝，特殊语义不变 |
| 双拼写 | GNU `interrupt(N)`、获准 Keil 后缀 | 同一合法性；Keil 未开启选项仍拒绝 |
| C 常量 vs IR 文本 | C `interrupt(0x7e)`、枚举常量 126；IR 文本 `"0x7e"` | C 以整数值裁定接受；IR 必须 canonical 十进制，拒绝文本十六进制 |
| 身份 | 高槽首次声明、重复声明／跨 TU、PCH 往返、不同号冲突 | 保留身份与冲突规则；高槽不因存储字段宽度丢失 |
| IR 发射 | 126 的定义 | 属性文本为 `"126"`，MCS251_INTR CC 和保活仍在；对象 VectorSlot BE bytes=00 7E |

必须回归输入报告 §4.3 全部 clang 文件：

- `/home/liu/LLVM_STC32/MCS251/clang/test/Sema/mcs251-isr.c`
- `/home/liu/LLVM_STC32/MCS251/clang/test/Sema/mcs251-isr-pch.c`
- `/home/liu/LLVM_STC32/MCS251/clang/test/CodeGen/mcs251-isr.c`
- `/home/liu/LLVM_STC32/MCS251/clang/test/CodeGen/mcs251-isr-opt.c`
- `/home/liu/LLVM_STC32/MCS251/clang/test/CodeGen/mcs251-keil-interrupt.c`
- `/home/liu/LLVM_STC32/MCS251/clang/test/Parser/mcs251-keil-interrupt.c`

### 5.2 Verifier／ContractCheck／AsmPrinter 独立消息与行为

三层均覆盖 `"126"` 接受；`"127"`、`"81"`、`"100"`、`"13"`、`"14"` 拒绝；`""`、`"00"`、`"0126"`、`"+126"`、`"-1"`、`"0x7e"`、首尾空白、数字后垃圾、超长十进制串拒绝，且无溢出／截断。完整共同消息至少在每层各断言一次 `MCS251 ISR: vector is not a legal slot in profile 0-126`。

**层间不可互相冒名**：只跑 llvm-as 命中 Verifier 不算 ContractCheck 或 AsmPrinter 已测。后端测试要使用现有绕过前序验证的测试入口或定向 unit/pass harness，让非法属性实际到达目标复检；若当前 lit 驱动无法隔离 AsmPrinter，需补受控测试 harness 或单元测试，不关闭产品复检来凑通过。对象负例不产生可用 ISR metadata；合法 126 的 24B ENTRY/REGISTER、type9 RELA、CC 和保存／RETI 字节均保持正确。

完整 LLVM 回归面：

- `/home/liu/LLVM_STC32/MCS251/llvm/test/Verifier/mcs251-isr-invalid.ll`
- `/home/liu/LLVM_STC32/MCS251/llvm/test/Assembler/mcs251-isr-cc.ll`
- `/home/liu/LLVM_STC32/MCS251/llvm/test/CodeGen/MCS251/isr-exits.ll`
- `/home/liu/LLVM_STC32/MCS251/llvm/test/CodeGen/MCS251/isr-frame.ll`
- `/home/liu/LLVM_STC32/MCS251/llvm/test/CodeGen/MCS251/isr-instructions.mir`
- `/home/liu/LLVM_STC32/MCS251/llvm/test/CodeGen/MCS251/isr-object.ll`
- `/home/liu/LLVM_STC32/MCS251/llvm/test/CodeGen/MCS251/xdata-isr-window.ll`

另加共享 parser／分类／常量的穷举单元或等价参数化测试：0..126 每个号对规范表，127／超宽值拒绝。新单元文件如需构建登记属于测试配套，不扩大 7 个产品文件清单。

### 5.3 lld：39→109 逐槽与独立 oracle

主文件 `/home/liu/LLVM_STC32/MCS251/lld/test/MCS251/isr-vectors.test`，生成器／checker 为 §2.4 列出的两个 Inputs 文件。

1. **109 个 Legal 槽逐个单独注册、链接、PT_LOAD 检查**；不能用“全注册一次”代替逐槽。额外全 109 注册的最终态，要求没有 DEFAULT 向量行但 CRT default 资产仍合法存在。
2. **18 个非 Legal（16 Reserved＋2 System）逐个拒绝**；越界 127/132/133/65535 各拒绝。原 31/45/46/52/64 负例移到正例，不留下合法槽配 `not lld` 的假测试。
3. 空用户注册（仅有效 IRQ CRT）生成完整 127 槽；高 Legal 默认也需安全读取初始化后的 SlotSym。注册 52、65、90、99、102、126 的组合以及全表在可用的 ASan／UBSan 构建下检查，重点防 SlotSym 越界。普通构建也必须逐槽验字节，不能把 sanitizer 可用性当功能验收前提。
4. 3 位槽名及顺序：`.mcs251.VECS.99`、`.100.rsv`、`.102`、`.126` 不截断；MAP 按数值顺序，100/101/113 是 RESERVED。
5. 生成器保留独立 109 Legal 副本；checker 保留独立 COUNT=127 和 18 非 Legal 副本。二者与本设计表做集合相等断言，**不通过 import 产品表把被测实现变成自己的预期**。
6. 既有完整 mutation 集保持：版本／record size／caps／reserved 字段／缺失与重复 RELA／ref addend／section symbol／非 metadata type9／ENTRY-REGISTER 缺配／非法 entry/save profile／default 冒充用户／未定义 identity／跨对象重复槽／同函数重复注册／旧 CRT 与输入 VECS（含零长）／GC/LTO 参数拒绝／无 CRT／HOME 配对／default 引用保护／ROM 与 XINIT-BSEG containment。
7. 原“slot31 hole”例须改到真正 Reserved，例如 **slot32=0xFF0103** 或高槽 100=0xFF0323；slot31 已 Legal，仍可保留单独“占用 Legal 向量”负例，但不能再声称它覆盖 Reserved 空洞防护。
8. version bump 后所有普通 mutation fixture 的基准记录先改 version=2，版本负例至少含旧 1 和未来 3；混版本对象／同对象混版本均拒绝。不要让原来用 2 的 `--mutation version` 意外变正例。

### 5.4 MAP 与最终镜像金样

**固定变化是 IRQ 行数 52→127，增加 75 行；不是整个 MAP 文件永远恰好多 75 行。** 合成节行数、CRT 地址、对象大小和其他符号也会改变。

| 检查项 | 原值 | 推荐新值 |
|---|---|---|
| `IRQ` 行数 | 52 | **127**，槽集合恰好 0..126，无重复／漏号 |
| Legal EJMP 数 | 39 | **109**（ISR＋DEFAULT） |
| Reserved / System 行 | 11 / 2 | **16 / 2** |
| VECS 保留面积 `l_VECS` | 0x1A0 | **0x3F8** |
| VECS 实际向量指令载荷 | 39×4=156B | **109×4=436B**，不是整个保留面积都写入 ELF/HEX |
| 末向量 | 51，0xFF019B | **126，0xFF03F3**；尾部 `[0xFF03F7,0xFF03FB)` 无载荷 |
| BOOT / CSEG 配方 | 0xFF0210 / 0xFF0400 | **0xFF0500 / 0xFF0700** |
| 当前 CRT default 地址 | 随旧资产版本变化 | **0xFF0602**（当前 BOOT+0x102） |

追加 MAP parser 负例：重复号覆盖字典项不能被行数检查漏掉，缺号、错误三位号、错误地址和错误标签均拒绝。PT_LOAD 独立验证所有 Reserved/System 的完整 8B 及 Legal 后 4B **无载荷覆盖**；不能只看 HEX 没列字节。每个已注册 EJMP 目标必须等于自己的 handler 入口，不仅是“某个可执行地址”；未注册 Legal 指向唯一 default，default 字节逐字节检查。

全注册 fixture 若仍用 41B handler＋2B `_main`，CSEG 面积应为 `109*41+2 = 4471 = 0x1177`，从 0xFF0700 到 0xFF1877，仍低于 XINIT。独立性要求不变，但必须测试输入 slots 排序假设，避免 fixture 发射次序与 checker 按 sorted 次序分配预期错配。

### 5.5 VECS／BOOT 新布局正负例

所有负例必须保持其他条件有效，核对错误阶段，不能因为旧 BOOT 提前失败就算目标负例通过。

| 场景 | 预期 |
|---|---|
| 推荐完整配方、当前 CRT、仅低槽／仅126／全109 | 链接成功，reset 02 05 00；BOOT 0x106B；三段无重叠 |
| 原 BOOT=0xFF0210、CSEG 使用新地址 | 因完整 VECS 区间占用拒绝；不可允许槽65尾洞 |
| 旧 BOOT=0xFF0100 | 仍因 VECS 重叠拒绝 |
| **BOOT=0xFF0430、CSEG=0xFF0700** | 不与推荐 VECS 重叠，但低于新 floor=0xFF0500，必须命中显式 floor 错误；替换旧 0xFF01B0 floor 负例 |
| BOOT=0xFF0500，CSEG=0xFF0605／0xFF0606 | 前者重叠 1B 拒绝，后者在面积层面可接受（测试刻意非默认配方），证明不暗中把 CSEG 固定为 0xFF0700 |
| 新 BOOT、仍 CSEG=0xFF0400，2B 小载荷 | 对127槽在区间层面可不重叠，可能接受；**不把旧 CSEG 数字本身当非法**。若负例要拒绝，构造确实跨入 BOOT 的载荷 |
| 普通 2B CSEG 放 slot32／slot100 Reserved 内、Legal slot0 尾内、slot126 尾内 | 全部拒绝，证明 NOBITS 仍占用 |
| 普通 CODE 从 End−1 放1B、从 End 放1B | 前者因 VECS 拒绝；后者在无其他重叠的定向用例中接受，证明半开边界 |
| VECS 改起点、HOME 改地址／大小／4B EJMP、reset 非零 addend／异 CRT／异 bank | 继续按原身份与布局门禁拒绝 |
| default 机器字节错误、普通引用／section+addend 引用、default 内零宽重定位 | 仍拒绝 |
| ROM 窗口精确到最后一字节、短1B | 精确通过／短1B拒绝，且失败不留下新可用固件 |

原单槽 fixture 为 41B handler＋2B main，即新 CSEG end=0xFF072B。因此原 `flash-size=0x42A/0x42B` 单槽边界应重算为 **0x72A 拒／0x72B 过**，用例中不要额外配置 0xFF8000 的空 XINIT 起点导致更早“area start outside flash window”错误。若有初始化载荷，按实际最高地址另算，不照抄这个数。

### 5.6 qualification、真机与整套回归

- `/home/liu/LLVM_STC32/MCS251/validation/mcs251-isr/qualification.test` 继续执行 `qualify.py --self-test`；假模型按新 Legal 集初始化，非 Legal 不填 EJMP。MAP 文案 52 行改 127，三位槽解析加入自测。
- `/home/liu/LLVM_STC32/MCS251/validation/mcs251-isr/compile-matrix.py` 的所有现有优化级别与 fixture 保持，增加 31/45/46、高槽 52/59/90/102/126 的 C→IR→ELF→lld 静态链；neg-slot52 改127，增加†及无源负例；保存窗口／嵌套资格不因扩表放松。
- `/home/liu/LLVM_STC32/MCS251/validation/mcs251-isr/firmware.c` 原低槽运行用例继续作为旧行为回归；高槽运行用例要有板卡或模型实际路由证明，不能只改 `interrupt(N)` 就声称测试了相应外设。
- `/home/liu/LLVM_STC32/MCS251/validation/mcs251-elf/runtime/crt-irq.yaml` 经 generator→独立 checker；验证 clear EA、PSW/DPS、栈、BSEG_BYTES、XINIT/XDATA_INIT 顺序和 default shape 都没有因搬家／版本更改发生偏移错误。
- §2.4 所列真机开发、PSW1 调查、clobber/nesting 资产全部静态重建核验；实机运行须另获许可，按实际板报告。G12 类低槽测试能证明低槽回归，不能替代 G144K246 高槽外设验收。
- 扩大回归：全部 MCS251 clang、LLVM CodeGen／Verifier／Assembler、lld MCS251 测试及相关 BinaryFormat 单元；非 IRQ selfstart、bit/EDATA/XDATA/初始化／ROM 放置链保持行为。报告 §4.3 所列文件一个不漏；不引用其他设计文档的旧“测试总数”当本次预期总数。
- 结果分四栏：静态槽合法性、完整字节链、模型执行、真机外设。未执行／模型未实现／其他既有阻塞分别列出；超时不是 PASS，不通过降低断言或忽略诊断制造成功。

---

## 6. 未决依赖与外部同步

### 6.1 实施期首查：QEMU 是否有 52 向量假设

**仍未决，不能由本轮 qualify.py 的 52 得出 QEMU 也只有 52，亦不能据报告“未见源码”得出模型支持127。**

G1-0 在任何产品改动前完成：

1. 查实际使用的 `qemu-system-*` 二进制来源、构建对应源码与 `stc32g144k246` 机器定义；报告提及 `/home/liu/LLVM_STC32/MCS251/qemu-processmission/` 未取到该向量模型，不视为已排除风险。
2. 找 CPU IRQ 入口、interrupt controller／pending／priority 数组、qdev GPIO 数量、qtest line 编号、源→槽→PC 转换及最大合法号。搜索 51/52 只能作线索，须追调用链；GPIO 号不必等于 Keil 槽号。
3. 检查复位 bank、`0xFF0003+8*n`、向量索引乘法宽度，及 96..99 多源共享槽；覆盖至少旧槽1、新槽52/65、三位槽102和端点126。
4. 分开记录“CPU 能跳到高向量地址”“控制器可注入高槽”“真实外设能产生该源”三个能力。只改 PC 到 vector 地址不等价于真实硬件中断入口／4B 帧。
5. 若缺模型或外设：G1 静态及字节链可独立推进；模型运行栏标未支持，真机接管必要验收。若用户要求模型全绿，另派 QEMU 实施切片，重新估期，不能扩大本片写权限。

### 6.2 改写包镜像（仓库外，只列同步项）

- `/home/liu/LLVM_STC32/mcs251-demos-rewritten/tools/rewrite.py:31-35` 的 LEGAL_SLOTS 改获批 109 集合，并有集合摘要／版本核对；不能从“官方132宏全接受”推导 Legal。
- 同文件 ISR_OVERRIDE／ISR_UNRESOLVED：D7 获批后仅精确处理 demo41 的 LCM 站点 13→59，删除该站点过时的“真源未证”理由，保留其他 unresolved 和 demo35 的 RTC 例外；官方原始源文件不修改。
- `/home/liu/LLVM_STC32/mcs251-demos-rewritten/tools/drive.py:49` 更新 IRQ 布局；`:338/:354` 不再把 `0-51` 作为唯一错误签名，支持稳定词干与实际范围。Reserved 的新拒绝仍归 profile gap，未知其他错误不能吞成 G1。
- `/home/liu/LLVM_STC32/mcs251-demos-rewritten/tools/vector_names.json` 仍保留 132 宏全量字典，不删†宏；名称存在和产品 Legal 是两个集合。`tools/gen-vector-names.py` 只核一致性，不按 Legal 截断官方语料。
- `tools/crt/crt-irq.o` 重建；重写输出、ledger.json、README 的 profile/G1 说明及每 demo tiers 从真实新运行产生。清除旧缓存结论须保留历史来源；不直接把 gap 改 PASS。

### 6.3 13 demo 的解锁判定（仅 G1／借槽层）

以下宏／号清单以输入报告附录为基线；不是本轮重编结果。“产品完成”指 G1-1/2/3 一致通过；“改写同步”指 G1-4 的 LEGAL_SLOTS、诊断、CRT／配方和重新生成均完成。

| demo | 相关受阻槽 | G1 槽号解锁条件 | 本片不能承诺的部分 |
|---|---|---|---|
| 02：13个定时器测试 | 67、96、97、98、99 | 扩表＋改写同步；全部双源 Legal | 定时器共享向量的应用分发和模型／板级实现 |
| 13：8串口同时收发 | 102–105 | 扩表＋改写同步 | 高串口外设、完整固件其他门禁 |
| 41：ILI9341 硬件 I8080 | 原借13；真源59 | **扩表＋D7 精确13→59例外＋移除对应 unresolved**；只扩表仍被13拒绝 | CODE 指针／ABI 等独立 gap，屏幕实机行为；不开放13 |
| 45.1：LIN1/LIN2主从 | 31 | **界内重分类＋镜像同步即可解除槽号拒绝**；新发行仍须完整布局／CRT | LIN 外设运行 |
| 45.2：LIN 软件自动波特率 | 31、45 | 同上，两个重分类都需要 | 波特率／端口路由等行为 |
| 48：LIN2从机 | 31 | 界内重分类＋镜像同步 | 其他编译／外设阻塞 |
| 49：LIN2主机 | 31 | 界内重分类＋镜像同步 | 同上 |
| 50：LIN1/LIN2双从机 | 31 | 界内重分类＋镜像同步 | 同上 |
| 59：DMA UART | 52–57、82–89、102–105 | 扩表＋改写同步，检查全部子工程 | DMA 路由／内存访问／模型能力 |
| 60：DMA I2C | 60、61 | 扩表＋改写同步 | I2C DMA 实际运行 |
| 61：DMA LCM | 58、59 | 扩表＋改写同步；DMA 与非DMA源分开 | 屏幕接口／DMA 行为 |
| 68：普通IO休眠唤醒 | 45、46、90、91 | **重分类只解除45/46；全部G1解除还须扩90/91＋改写同步** | 休眠唤醒／端口配置；不能按“六个界内demo”笼统提前解锁 |
| 82：CANFD DMA | 117、118、119、120 | 扩表＋改写同步；CAN1/2=28/29 原已Legal | AS3／bit／ABI 等其他 gap；不改变既有教学线范围裁定 |

因此可以精确说：**5 个纯界内失配 demo**（45.1、45.2、48、49、50）的槽号问题只需重分类；第 6 个含界内失配的 demo68 还需扩表。完成推荐产品与镜像后，除 demo41 的专门改写批准外，另外12个在清单所列槽上均无剩余G1拒绝；这不是12个完整demo PASS。六个†槽的 Legal／Reserved 仲裁不改变这13项的上述解锁判定。

---

## 7. 风险表、停止条件与用户最终仲裁

| 风险 | 等级 | 后果 | 处置／退出条件 |
|---|---|---|---|
| lld SlotSym 仍为52 | 严重 | 高槽写／读越界，崩溃或静默错向量 | Count 定长、零初始化、逐槽＋全表＋空注册检查；审计所有索引容器 |
| BOOT 落向量尾洞或Reserved洞 | 严重 | 运行跳入启动代码／互相覆盖 | 保留整个 VECS 禁占；旧布局负例；PT_LOAD 与完整区间双重检查 |
| “仅加长”漏31/45/46 | 高 | 六个含界内失配demo继续拒绝 | 显式重分类正例；demo68 分阶段判断 |
| †单头定义误当双源 | 高 | 未获证的器件源被静默开放 | 默认Reserved，证据类型入表；放开须单独批准／补证 |
| 13借槽被通用化 | 严重 | RTC／LCM等不同借槽站点被错误映射 | 禁用13保持；demo41精确override，保留demo35 RTC规则 |
| 新旧工具／CRT混链 | 高 | 低槽对象被旧lld接受但缺高槽默认表 | 推荐ProtocolVersion=2严格门禁；整套发布／重建；不复用旧资格结论 |
| 测试副本不同步或共用产品oracle | 高 | 假失败或相同错误互相证明 | fixture/checker/compile-matrix各自对规范集合；独立字节解析 |
| 负例被新布局／版本错误遮蔽 | 高 | 旧安全门禁实际失去覆盖 | 每个mutation只改一个因素，检查首个错误；floor用0xFF0430隔离 |
| 旧发布MAP的BOOT大小过时 | 高 | 按0xA0而非0x106算布局，hash与镜像不一致 | 从当前源重建全部资产；保留旧记录为历史，不文本美化金样 |
| QEMU向量或外设有限 | 高／未定 | 静态通过被误报运行通过 | G1-0首查；CPU入口／控制器／外设三层分账；必要时另切片 |
| 新profile泛化到旧板 | 高 | “Legal”被误读为该板有外设 | 明示G144K246证据profile；旧板仅认证子集；要求多profile则另设计 |
| CSEG上移损失0x300B可用空间 | 中 | 边界固件ROM溢出、既有分支或绝对地址假设暴露 | flash gate／初始化区重叠回归；绝对地址及reset字节重算；不削门禁 |
| 表来源只有无身份PDFPage | 中 | 又将旧家族页码当新设备证据 | 显式证据类型／M行／H行，旧特殊语义来源单列 |
| 三位槽号／MAP重复行 | 中 | 截断、解析漏号、字典覆盖掩盖坏MAP | 99/100/102/126边界、重复／缺失／乱序测试；数值顺序 |
| 地址／数字机械全文替换 | 高 | 52B ABI note被破坏；假模型与RSP checksum错配 | 逐语义分类，非IRQ与任意测试值保留；审查变化白名单 |

### 7.1 必须停止并报 PM／design owner 的条件

- 任一 L 行发现手册／官方头号或地址不一致；不得选“能让demo过”的一方。
- QEMU或真机证明源路由／硬件帧与采用profile不兼容；不得把向量直接跳转测试当硬件中断证明。
- 当前CRT重建面积不再是0x106或出现新绝对编码，必须重新证明布局；不得靠删walker缩小。
- 用户选择不同上界、†分类、协议策略或BOOT方案，必须重算统计、MAP、测试和兼容矩阵后才实施。
- 需要新产品文件、设备profile选择器、调用约定或bit-aware CRT变化，超出本7文件方案时先报范围变更。
- 原有效资格资产与工作树来源不一致，先澄清来源并重建，不覆盖历史结果。

### 7.2 待用户最终拍板的规范文字

建议用户一次确认以下完整组合，避免只批准“扩表”却遗漏最大的布局／协议契约变更：

> 批准设计方向为 G144K246 证据 profile 0..126（127槽）；31/45/46 为Legal；81/92–95/113和7/13保持Reserved；14/15保持System；100/101无源保留。最终统计109/16/2。VECS整段 `[0xFF0003,0xFF03FB)` 禁占，IRQ发布配方BOOT=0xFF0500、CSEG=0xFF0700，BOOT最低地址同步为0xFF0500，HOME仍为3B LJMP。`.mcs251.isr` ProtocolVersion升为2但记录布局／CC／保存／ELF ABI不变，旧ISR记录严格拒绝并整套重建。所有产品范围诊断由共享常量生成。仅对已证的demo41 LCM站点批准13→59改写例外，不开放槽13。实施首先核查QEMU向量模型；产品、独立测试镜像、CRT、改写包和真机验收资产同步发布。

**本段仍是推荐裁定，不是用户已批准记录，更不是实施授权。** 用户可选本设计明确列出的替代；一经改变选项，owner须先更新对应规范表和测试oracle。只有用户另行授权实施、冻结契约完成仲裁、测试和资产重新取得资格后，才能宣布G1切片完成。

> **PM 裁定记录（2026-09-13）**：用户裁定"暂时按照推荐设计"——D1-D7 全部按推荐组合执行（上界 0..126/127 槽、统计 109/16/2；六个†槽 Reserved；31/45/46→Legal、7/13 Reserved、14/15 System；VECS 整段禁占、BOOT=0xFF0500、CSEG=0xFF0700；ProtocolVersion 1→2 严格拒绝旧记录；诊断数字由共享常量生成；demo41 仅 LCM 站点 13→59 精确例外）。"暂时"意为后续可依实据复议；实施按 §4 切片推进并逐卡验收，G1-0 的 QEMU 向量模型首查仍是实施第一步。
