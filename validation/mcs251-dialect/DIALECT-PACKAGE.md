# Keil C251 → MCS-251 LLVM 方言兼容包设计

状态：研究/设计稿，不修改生产代码。日期：2026-09-05。

## 0. 范围、证据与结论

本设计以三份已落盘材料为输入：

- 官方 demo 静态扫描：`/home/liu/mcs251-demo-scan/per-feature-counts.tsv`、`peripheral-groups.tsv`、`qemu-demo-status.tsv`、`scan-summary.md`。
- QEMU 外设实测：`/home/liu/mcs251-qemu-periph/REPORT.md`，10 个 probe 全 PASS。
- 当前测试体系设计：`/mnt/c/Prj/LLVM/MCS251/validation/mcs251-demo-test/DESIGN.md`。

扫描口径是 275 个 C 变体文件、88 个 demo 目录、2751 个函数定义；函数按文件/定义计数，不把汇编变体算入。频率基线：

|特性|函数频率|文件频率|设计含义|
|---|---:|---:|---|
|多参数定义（≥2）|765/2751 = 27.81%|99/275 = 36.00%|OSEG 是高优先级 ABI 缺口，但不是 80% 的函数定义|
|多参数调用点（≥2）|1117 个函数，4400 个调用点|159/275 = 57.82%|最高优先级，demo 文件覆盖过半|
|SFR 名称依赖|735 个函数|181/275 = 65.82%|固定地址头文件垫片先行；AS 地基决定后续原样编译|
|`bit`/`sbit`|307 个函数|94/275 = 34.18%|第二梯队；先 u8 垫片，再 BSEG 真语义|
|Keil `interrupt`|191 个函数|100/275 = 36.36%|第三梯队；后端属性 + 向量槽自动生成|
|浮点|97 个函数|27/275 = 9.82%|不因 TFPU 存在就预设 QEMU 可测|
|结构体按值|0|0|不作为方言包首要工作|
|varargs 定义|3|3|仅保留 fail-loud/后续运行时路线|

总原则：**地址空间和目标属性在 LLVM 后端实现；Clang 只解析无法用预处理器表达的原始 Keil 语法；能由公共头重写的内容不进 Clang。** 设计不把静态出现误当作运行行为。QEMU 只背书 T0/T1、GPIO P0-P7、UART1 和已实测中断路径；PWM/SPI/I2C/CAN/RTC/DMA/USB/比较器等没有模型，UART2/3/4、ADC、WDT、T2/T3/T4 是静默读 0/写丢弃，PCON 低功耗停振不生效。

---

## 1. 存储类 → 地址空间：方言包地基

### 1.1 物理区域和稳定 LLVM 表示

STC32G144K246/QEMU 报告给出的物理事实是：edata 约 16 KiB，基址 `0x000000`；xdata 约 128 KiB，基址 `0x010000`；代码/CONST 放在高地址窗口，生产链使用 CSEG/CONST 的显式链接基址。经典 direct 地址 `0x00–0x7f` 是 page-zero/data 直达窗口，`0x80–0xff` 与 SFR direct 窗口重叠；扩展 SFR/XFR 是 `0x7e0000–0x7effff`，必须先开启 `P_SW2.EAXFR`，不能当成普通 xdata。

建议把“LLVM 中的指针值宽度”和“硬件物理地址有效位数”分开。当前 MCS251 后端已经以 canonical `i32`/DR 表示普通指针，DataLayout 是：

```text
E-m:e-p:32:8-i8:8-i16:8-i32:8-n8:16:32-S8
```

第一阶段不改变 AS0 的 `p0:32:8` 表示，避免破坏已通过的单参数、i32 canonical pointer、alloca/VLA 和函数指针 ABI。24 位物理地址装在 i32 中，高 8 位必须为零；AS 专属指令选择决定该值使用 direct、edata 间接、MOVX 或 MOVC，而不是靠截断 pointer value 猜测区域。

建议的**稳定第一版 AS 表**如下。这里的 `pN:32:8` 是 LLVM 表示宽度；`physical` 是硬件有效地址/选择器，二者不同的地方必须由后端验证高字节为零并在 lowering 中选择正确指令。

|LLVM AS|Keil/C251 名称|LLVM pointer 表示|物理/链接区域|访问语义|推荐实现归属|
|---:|---|---|---|---|---|
|0|默认 `edata` / generic|`p0:32:8`|edata `0x000000..`；普通 canonical 指针|动态/base 指针走 `@wr/@dr` 的 region-00 edata；绝对 byte 常量可走既有 direct 判定|后端 address-space/lowering；保持现状|
|1|`data`|`p1:8:8`（或先 canonicalize 到 i32）|direct data `0x00..0x7f`|`MOV8di/MOV8id` direct；对象/指针必须受 7-bit/8-bit 范围检查|后端 address-space + direct selector|
|2|`idata`|`p2:16:16`|内部 RAM 的 indirect 16-bit 窗口|`@wr`/16-bit indirect；禁止把它误选成 SFR direct|后端 address-space + `@wr` selector|
|3|`xdata`|`p3:32:8`，物理有效 24 bit|xdata `0x010000..`|MOVX/DR canonical；链接到 XSEG，保留高字节校验|后端 + linker area|
|4|`code` / `const`|`p4:32:8`，只读|CSEG/CONST；当前验证链将 CONST 显式放在 `0xfc8000` 一类窗口|MOVC；只读 load；不能生成 store|后端 + AsmPrinter/MC + linker|
|5|`bit` / BSEG|建议 `p5:16:8` 仅作不透明 bit-address；bit value 是 i1|BSEG bit 编号和 BSEG_BYTES 映射|`SETB/CLR/CPL/JB/JNB` 或 BSEG byte-mask；不得按普通 byte pointer 解引用|后端 bit lowering；Clang 尽量不改|
|6|`sfr`|`p6:8:8`|classic SFR `0x80..0xff`|direct SFR byte；与 AS1 在数值上重叠但**别名类别不同**|后端 direct SFR selector；头文件生成|
|7|扩展 SFR / `xfr`|`p7:32:8`，物理有效 24 bit|XFR `0x7e0000..0x7effff`|开启 EAXFR 后才可访问；MOVX/XFR 序列；不能与普通 xdata 合并|后端属性/序列 + 头文件生成|

`edata` 不另外新建 AS：在 Keil XSmall 中它就是默认数据模型，映射到 AS0。`data` 必须单列，即使其物理地址与 edata 的低地址有重合，因为 direct `MOV` 与 `@wr/@dr` 的寻址行为不同。AS6 SFR 也必须单列，即使 SFR 的数值地址处于 direct byte 范围；这样优化器和别名分析不能把 `*(volatile u8 *)0x80` 的 SFR 访问错误合并成普通 RAM 访问。

如果后续实验证明某一 AS 的 `pN:8`/`pN:16` 能完整通过 LLVM legalizer，再把表示宽度收窄；首版不为追求物理 24-bit 而引入非法 i24 ABI。24-bit 是硬件地址事实，不是 C 标量类型事实。

### 1.2 C 声明形态

通用 Clang 已有 `__attribute__((address_space(N)))` 语法，方言包优先提供头文件宏：

```c
#define MCS251_AS(N) __attribute__((address_space(N)))
#define MCS251_DATA  MCS251_AS(1)
#define MCS251_IDATA MCS251_AS(2)
#define MCS251_XDATA MCS251_AS(3)
#define MCS251_CODE  MCS251_AS(4)
#define MCS251_BIT   MCS251_AS(5)
#define MCS251_SFR   MCS251_AS(6)
#define MCS251_XFR   MCS251_AS(7)

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned long  u32;

MCS251_DATA  u8 counter;
MCS251_XDATA u8 rx_buffer[128];
static const u8 MCS251_CODE table[4] = { 1, 2, 4, 8 };
```

宏只负责把声明变成 LLVM pointer/global address space；寻址指令、段名、绝对地址和只读约束由后端负责。为了让 Keil 原样声明 `u8 xdata x;` 也能工作，有两条路线：

1. **首选，头文件垫片**：目标专用兼容头把 `xdata/idata/data/edata/code` 变成声明可接受的 attribute/typedef 组合；`code` 同时施加 `const` 语义或要求对象声明为 `const`。
2. **原始语法终态，Clang 极薄扩展**：仅在 `-fkeil-c251` 下把这些 token 解析为 `LangAS`/变量属性，AST 直接带 AS；不改变一般 C 的默认规则，不把它们做成全局关键字。

`code` 不能只做字符串替换为 `const`：`const` 只表示只读，不会自动把全局对象送入 CONST/CSEG 或让 load 走 MOVC。因此 code 的“只读类型”可以由头文件处理，code 的“存储区域/访问指令”必须由后端和链接器处理。

### 1.3 DataLayout 和后端落点

建议在 `llvm/lib/TargetParser/TargetDataLayout.cpp` 的 MCS251 分支保留 AS0 兼容布局，并逐步增加 `p1/p2/p3/p4/p5/p6/p7` 规格。最终字符串须由单元测试固定，而不是由每个 test `.ll` 自行覆盖。`MCS251TargetMachine.cpp` 应按 `-mdata-model=`/`-mmodel=` 和 feature string 选择布局变体；`MCS251Subtarget` 缓存同一选择，禁止一个 Module 中函数随机改变默认 AS。

后端文件级工作包：

|文件|改动|
|---|---|
|`llvm/lib/TargetParser/TargetDataLayout.cpp`|MCS251 每-AS `pN:size:abi`、默认 AS0 edata、int16 模型布局选择；加完整字符串单测|
|`llvm/lib/Target/MCS251/MCS251TargetMachine.cpp`|读取 data-model/int16 feature，构造与 `Subtarget` 一致的布局；拒绝未知组合|
|`MCS251ISelLowering.cpp`|`PointerType` AS 驱动地址分类；AS1 direct、AS2 `@wr`、AS0 `@dr` edata、AS3 MOVX、AS4 MOVC、AS6 SFR、AS7 EAXFR+XFR；AS cast 统一 canonicalize|
|`MCS251InstrInfo.td`|为 direct/edata/xdata/code/SFR/XFR/bit 建独立 operand/pseudo 和 pattern；不能再用“常量 ≤0xff”作为唯一区域判据|
|`MCS251AsmPrinter.cpp`|按 GlobalValue AS 选择 DSEG/ISEG/XSEG/CSEG/CONST/BSEG；AS4 只读；AS5/AS6/AS7 不误发射为普通 CSEG 数据|
|`MCS251MCAsmInfo.cpp` / MC emitter|声明大端、`.word/.3byte` 和各 AS 的段/绝对重定位；沿用已有端序修复，不重新改 ABI lane|
|`MCS251FrameLowering.cpp` / `MCS251RegisterInfo.cpp`|栈对象默认 AS0 edata；AS1/AS2 局部对象的 frame access 规则；BSEG 计数与 bit-byte area；XSEG 溢出报错|
|`MCS251CallingConv.td`、`MCS251ISelLowering.cpp`|默认 pointer 与 AS pointer 的参数/返回 canonicalization；AS4 函数指针与 data pointer 不隐式混用；多参 OSEG 单独推进|
|`validation/mcs251-ld/mcs251_ld.py`|为 CSEG/CONST/XSEG/ISEG/DSEG/BSEG/BSEG_BYTES/ OSEG 提供可审计 area；全局数据支持必须和 AsmPrinter 同步落地|

当前代码的事实边界必须写进实现 issue：`MCS251ISelLowering.cpp` 的 `parseAddress` 目前主要按 constant `K <= 0xff` 保留 direct，否则构造 canonical DR；`MCS251AsmPrinter.cpp` 目前拒绝定义全局数据以避免错误发射到 CSEG；`MCS251TargetMachine.cpp` 直接使用 `TT.computeDataLayout()`。这些都说明 AS 设计尚未在生产后端实现，不能把建议表误写成现状。

### 1.4 QEMU 验证方案和优先级

按 AS 分别做最小 C/IR probe，检查三层：IR pointer AS、llc 汇编寻址、链接后 QEMU transcript。

|AS/场景|QEMU 验证|优先级|
|---|---|---:|
|AS1 data/direct|P0/P1/P2/P3/P4/P5/P6/P7 模式读回；P0-P7 是 QEMU 真实模型|P0|
|AS0 edata|局部/栈对象和 `@dr/@wr` 往返；现有 i8/i16/i32/alloca probe 扩展|P0|
|AS2 idata|内部 RAM 间接读写，证明不会误走 SFR；超界 fail-loud|P1|
|AS3 xdata|在 `0x010000` xdata 写入特征并读回；XSEG relocation 与 QEMU loader|P1|
|AS4 code|CONST 显式放 `0xfc8000`，MOVC 逐字节读回；避免 QEMU `0xffxxxx` 数据读 0 的已知陷阱|P1|
|AS6 SFR|SBUF `0x99`、SCON `0x98`、TCON/TMOD、P0-P7；QEMU 仅对 UART1/T0/T1/GPIO 背书|P0|
|AS7 XFR|先写 `P_SW2.EAXFR`，验证 TM0PS 等已建模窗口；对无映射地址保留“读 0/写丢弃”负断言|P2|
|跨 AS cast|AS4→AS0、AS3→AS0 的显式 cast 必须生成合法 canonical 化；禁止隐式 bit/SFR alias|P1|

QEMU 不支持的 PWM/SPI/I2C/CAN/RTC/DMA/USB/比较器不作为正向外设验证；这些只能做静态段/指令选择检查或“缺席=读 0/写丢弃”的负向保护。

---

## 2. `sfr`/`sbit`：固定地址 volatile 垫片

### 2.1 `sfr`

经典 Keil 头文件的：

```c
sfr SBUF = 0x99;
sfr P0   = 0x80;
```

应在生成的 `mcs251_keil_compat.h` 中变成固定地址 volatile lvalue。建议先按 AS6 表达 SFR 类别，最终 IR 形状等价于：

```c
#define MCS251_SFR8(addr) \
  (*(volatile u8 MCS251_AS(6) *)(unsigned long)(addr))
#define SBUF MCS251_SFR8(0x99)
#define P0   MCS251_SFR8(0x80)
```

若当前 Clang 不接受带 AS 的 lvalue cast，则过渡宏可用普通 volatile pointer，后端依据**常量地址 + volatile + direct**选择 SFR；但生产终态必须把 AS6 保留下来，否则 optimizer 无法知道 AS1 page-zero 与 AS6 SFR 不同。地址 `0x80–0xff` 必须优先走 direct SFR，不能变成 `@wr` 的 region-00 edata。

扩展寄存器不是 classic SFR 的简单续号：公共头中存在形如 `(*(unsigned char volatile far *)0x7efe50)` 的宏，地址属于 AS7/XFR，且需要 `P_SW2.EAXFR=1`。兼容头应生成：

```c
#define MCS251_XFR8(addr) \
  (*(volatile u8 MCS251_AS(7) *)(unsigned long)(addr))
#define TM0PS MCS251_XFR8(0x7efea0)
```

后端要在第一次 AS7 load/store 前插入或要求显式的 EAXFR 开启序列，不能把该副作用偷偷加入所有普通 xdata 访问；更稳的第一版是由 `__mcs251_enable_xfr()`/启动代码显式开启，AS7 只负责选择 XFR addressing。

### 2.2 `sbit`

`sbit P33 = P3^3;` 有两个层次：

- **过渡层（头文件垫片即可，但不是原始 lvalue 透明语义）**：生成 `P33_GET()`/`P33_SET(v)`，读用 `P3 & 0x08`，写用 read-modify-write；或把 bit 状态改成 `u8`+掩码。适合当前 demo 改写，T2 已证明固定地址 volatile byte 可到达 SFR。
- **原样 C 终态（后端 address-space/属性可做，Clang 可能需极薄扩展）**：AST 需要表达“位地址 lvalue”，后端选择 `SETB/CLR/CPL/JB/JNB` 或 BSEG byte-mask；普通 `__attribute__((address_space(6))) u8` 不能表达一条 byte 中的单 bit 写入。若要求源文件继续使用 `P33 = 1`，建议增加 target-only `mcs251_bit_address(bit_base, bit)` attribute，而不是把普通 C 的 `^` 运算符重载。

`sfr/sbit` 分工：classic `sfr` 的声明替换是**头文件垫片即可**；AS6/AS7 的 load/store 是**后端 address-space/属性可做**；源透明 `sbit` lvalue 是**后端 bit lowering + Clang 极薄 AST 属性**，不能承诺只靠宏完成。

### 2.3 QEMU 方案和频率优先级

直接 SFR probe：P0-P7 模式读回、TCON/TMOD/TL0/TH0/TL1/TH1、SCON/SBUF/TI/RI。QEMU 报告已确认 GPIO 四模式、T0/T1 计数/溢出/重载/预分频、UART1 TX/RX/TI 均 PASS。UART2/3/4 是静默 0，不能用它们验证 SFR 通用正确性。扩展 AS7 只对 TM0PS 等已有模型做正向，其余用写特征后读 0 负断言。

优先级：SFR 固定地址垫片 P0（181 文件、65.82%）立即可做；AS6/AS7 选择 P0/P1；源透明 sbit P1（94 文件、34.18%），排在 byte SFR 后。

---

## 3. `bit`：u8 过渡和 BSEG 完整路线

### 3.1 过渡包

最低风险头文件定义：

```c
#define bit unsigned char
```

并把 `bit flag;` 当作 `u8 flag;`。位测试/赋值由普通 C `if (flag)`、`flag = 0/1` 完成；如果原代码使用 `sbit`，配合前节的掩码宏。该路线能让一部分算法和状态机先进入 LLVM，但不保留 Keil 的 1-bit 紧凑存储，也不宣称 bit ABI 已实现。扫描中 `bit/sbit` 命中 307 个函数、94 个文件；这解释了为什么过渡垫片具有现实收益。

### 3.2 BSEG 完整路线

完整实现需把 `bit` 对象放入 BSEG，而不是把 i1 随意塞进 DSEG：

1. Clang AST 为 `bit` 类型标记 target bit type；若采用薄扩展，生成 LLVM `i1` 对象并附 `mcs251-bit`/BSEG address-space metadata。
2. LLVM 后端为 bit global/local 分配 BSEG bit index；按 8 bit 聚合到 BSEG_BYTES，维护 BSEG 与 BSEG_BYTES 的链接符号/大小。
3. SelectionDAG 为 load i1、store i1、`!`、`&&/||` 短路和比较分支选择 `JB/JNB/SETB/CLR/CPL` 或 byte-mask read-modify-write；不能把 i1 load 当作 i8 load 后忘记掩码。
4. `volatile bit` 必须保持每次硬件可见访问；非 volatile bit 可在函数内合并，但不能跨可能修改 BSEG 的调用/中断任意合并。
5. 地址取值、指针算术和 bit→byte cast 要 fail-loud，避免把 bit address 当普通 data pointer。

后端文件级改动集中在 `MCS251InstrInfo.td`（bit 指令/pseudo）、`MCS251ISelLowering.cpp`（i1 与 BSEG）、`MCS251RegisterInfo/FrameLowering`（BSEG 分配）、`MCS251AsmPrinter.cpp` 与 `validation/mcs251-ld/mcs251_ld.py`（BSEG/BSEG_BYTES）。Clang 的最小改动是识别 `bit` token 并给出类型/属性；如果坚持 `bit` 作为普通 `unsigned char` 过渡，Clang 改动为零。

### 3.3 验证和优先级

QEMU 目前没有独立 BSEG bit probe；GPIO/SFR 的 bit field 可通过 P0-P7、SCON、TCON 的 byte 语义做间接验证，但不能把它当 BSEG 真实性能证明。先做 host/LLVM 汇编逐位 oracle，再用 QEMU UART1/GPIO/T0/T1 观察结果；对 bit 单独访问的生成指令做静态检查。优先级为 P1：低于 AS/SFR 地基，高于浮点和无模型外设。

---

## 4. `interrupt N`：函数属性、VECS 槽和自动向量生成

### 4.1 QEMU 已背书的向量/帧约定

Moka 实测报告给出的中断控制器事实：

- 8 个经典源，向量槽地址为 `0xff0003 + irq * 8`：INT0、TF0、INT1、TF1、UART1、ADC、LVD、PCA。
- EA/IE 生效，IP/IPH 优先级和嵌套帧生效；入场自动清 TF0/TF1/IE0/IE1；UART1 pending 由 ISR 自清 TI/RI。
- 中断帧压 PSW1 + 24-bit PC，`RETI` 恢复硬件帧。
- probe 已实测 TF0、TF1、INT0、INT1、UART1-TI 触发和返回；ADC/LVD/PCA 只有向量、没有设备触发。INT2/3/4、UART2/3/4 等 demo 路径不可测。

当前测试设计建议的 stub 形态是：

```asm
; VECS[n] 槽，具体入口布局由 linker 固定
lcall _mcs251_isr_body_N
reti
```

其中 `RETI` 负责硬件中断帧，`ERET`/返回路径负责 `lcall` 产生的调用帧，不能把普通 `ecall; eret` 和中断 `reti` 混用。该 `lcall _body; reti` 约定应以现有裸 asm probe 在目标 QEMU 上先做最小实测，再由 LLVM 固化；报告已背书向量/RETI 机制，但“LLVM 生成 C ISR → stub → body”的组合仍需专门 probe。

### 4.2 用户语法到 IR/后端

原始 Keil 语法：

```c
void timer0(void) interrupt 1 { ... }
```

推荐最终流水线：

1. Clang 在 `-fkeil-c251` 下解析尾部 `interrupt N`，生成普通函数声明加 target attribute，例如 LLVM function attribute `"mcs251-interrupt"="1"`；函数体不携带通用 `interrupt` ABI 猜测。
2. 后端在 `MCS251AsmPrinter.cpp`/目标 lowering 读取该属性，检查 N 是常量、范围合法、无重复定义；函数 body 生成普通 `_isr_body_N`。
3. 后端输出 `.mcs251.veс`/VECS 合成 section，或由 linker 根据 function attribute 生成唯一 8-byte 槽；槽内发射 `_mcs251_vector_stub_N`，stub 调用 body 后 `reti`。
4. linker (`mcs251_ld.py`) 在 `VECS` 上做范围、重叠、重复向量检查；没有 ISR 的槽填默认 handler/短跳板，不能由普通 CSEG 顺排数据碰巧占用。
5. `interrupt 0..7` 的语义必须按 MCS251 向量编号解释；Keil demo 中还出现 T2/T3/T4、PWM、USB、DMA、I2C 等向量名，若该源在 QEMU 无设备，仍可静态生成槽，但测试标 L3/QEMU 不可测。

### 4.3 Clang 分工

- `interrupt N` 原始尾部语法：**需 Clang 极薄扩展**，因为预处理器无法把 `interrupt 1` 重写成一个合法的带参数 attribute。扩展只做 token 解析和 function attribute，不实现向量、保存寄存器、RETI。
- 推荐同时支持用户可写的 `__attribute__((mcs251_interrupt(N)))`：可在 `clang/include/clang/Basic/Attr.td`、`SemaDeclAttr.cpp` 注册 target-only 属性；没有该属性时可先用生成头文件/源转换。
- 向量槽、stub、保存/恢复 ABI：**后端 address-space/属性可做**，落点是 `MCS251AsmPrinter.cpp`、`MCS251ISelLowering.cpp`、`MCS251InstrInfo.td`、`MCS251MCAsmInfo.cpp` 和 linker。
- `__interrupt` 作为 `interrupt` 别名可以由 target parser 处理；静态扫描中未检出 `__interrupt` 拼写，Keil `interrupt` 为 191 个函数。

### 4.4 验证与优先级

验证顺序必须遵循 QEMU 实测，不先为无模型外设写正向 oracle：

|阶段|probe/证据|判定|
|---|---|---|
|stub 帧|裸 asm 生成 VECS 槽，body 只打印一次，`RETI` 返回|先验证 `lcall; reti` 组合不破坏帧|
|TF0/TF1|现有 d1/d4 probe|QEMU 可测|
|INT0/INT1|软件驱动 P3.2/P3.3 下降沿|QEMU 可测；demo06 只把 INT0/INT1 子集标 L2|
|UART1|SBUF/TI/RI，ISR 自清 TI/RI|QEMU 可测；ES 开启时避免 ISR 内打印风暴|
|LVD/ADC/PCA|向量槽只做负向/未触发测试|有向量但无设备，不标正向可测|
|INT2/3/4、UART2/3/4 等|写特征并验证静默 0 或不触发|QEMU 不可测|

优先级 P1/P2：中断命中 100/275 文件（36.36%），但需要先有 AS/段和裸 stub；实际 demo 正向价值集中在 TF0/TF1/INT0/INT1/UART1。

---

## 5. memory model：默认 AS、大小模型和指针宽度

### 5.1 已测默认：XSmall

官方 demo 注释明确写明：“Keil C251 Memory Model 推荐 XSmall，默认定义变量在 edata”；并建议 edata 预留 1 KiB 给堆栈，大数组/不常用变量用 xdata。方言包的确定规则是：

```text
-mmodel=xsmall  → 默认对象 AS0 (edata)，默认 C data pointer = p0:32:8 canonical
u8 data_var    → AS1 (data/direct)
u8 idata_var   → AS2 (idata/indirect)
u8 xdata_var   → AS3 (xdata/MOVX)
const/code obj  → AS4 (code/MOVC)
bit var        → AS5/BSEG（或过渡 u8）
sfr/sbit       → AS6/bit-address
extended SFR   → AS7/XFR
```

默认对象选择 AS0 而不是 AS1 是原样 demo 兼容的关键；不能因为 `edata` 地址从零开始就把 XSmall 全部改成 direct data。默认普通指针仍用 i32 canonical 是当前后端的稳定 ABI；只要 C 声明带显式 AS，后端才选择专用寻址。

### 5.2 Small/Large 模型的设计规则

Keil/SDCC 的所有 memory model 细节不能仅凭名称猜测，必须通过编译器 oracle（`sdcc -mmcs251`/官方 Keil 对照）固定。方言包先规定**可实现的目标矩阵**，并把未经 oracle 的默认值标为待定：

|模型|建议默认 AS|默认指针表示|显式 qualifier|注意|
|---|---|---|---|---|
|XSmall（首要）|AS0 edata|i32 canonical，物理 24 bit|data/idata/xdata/code 各自覆盖|官方 demo 已明确推荐；首个终态|
|Small|AS1 data 或 AS2 idata，二者由 oracle 决定|8/16-bit source pointer 可在后端 canonicalize 到 i32|显式 xdata/code 仍强制 AS3/AS4|不能把 Small 的默认值从 XSmall 继承为事实|
|Medium/Compact（若启用）|保留 AS0/AS2，按 compiler probe 定义 bank/pdata 规则|建议 canonical i32，保留 bank bits|只实现有 demo/ABI 证据的 qualifier|非首批|
|Large|AS3 xdata|canonical i32，物理 24 bit|data/idata/code 显式覆盖|XSEG/MOVX 成为默认对象访问；需全局数据 area|
|Code-large/函数指针|AS4 code/function|canonical i32，代码 24 bit|禁止与 data pointer 隐式互转|ECALL/EJMP 目标必须保留 24-bit|

为避免模型切换污染 ABI，`-mmodel=` 只能在编译单元/Module 级设置；后端在 module layout 与 linker script 中记录模型。源中显式 `xdata`/`code` 的对象不随默认模型漂移。模型选择的 Clang 改动只应是 target option、预定义宏和默认 `LangAS`；实际段和指令全在后端。

### 5.3 全局数据和 `code` 的现实约束

当前 `MCS251AsmPrinter::emitGlobalVariable` 仍会拒绝定义全局数据，防止把对象静默发射到 CSEG；方言包若要达到“demo 原样可编译”，必须把 global AS→area 的实现作为 AS 地基第二阶段：

- AS0/AS1/AS2 → DSEG/ISEG/BSEG 等 data area；
- AS3 → XSEG；
- AS4 → CSEG/CONST，且只读；
- AS5 → BSEG/BSEG_BYTES；
- linker 显式检查 stack、edata、xdata、CONST 不重叠。

QEMU 报告还记录了 `0xffxxxx` 数据窗口读 0 的已知陷阱，因此 code/CONST probe 必须显式链接在 QEMU 可读的 CONST/FC 区，而不能因“hex 中有字节”就宣称 MOVC 已验证。

### 5.4 优先级

XSmall 默认 AS0 是 P0 地基；AS3 xdata/AS4 code 是 P1，因为大量 demo 有常量表、大数组和指针。Small/Large 默认模型切换是 P2，必须在完成首个 XSmall 链后用 SDCC 对照定案。无模型外设的外设寄存器不会因 Large model 而获得 QEMU 行为。

---

## 6. int16 兼容（与 Alice 的 `-target-feature +int16` 联动）

### 6.1 类型和 ABI 规则

当前 `clang/lib/Basic/Targets/MCS251.h` 已把 `IntWidth = ShortWidth = 16`，`LongWidth = PointerWidth = 32`，并调用 `resetDataLayout()`；这应和 Alice 的 `+int16` feature 统一，而不是再在方言头里用 `typedef int` 猜大小。

方言包规则：

|C 类型/方言类型|`+int16` 模型|存储类影响|ABI|
|---|---|---|---|
|`char`/`u8`|8 bit|AS0/显式 AS 不改变标量宽度|DPL|
|`short`/`u16`/`int`|16 bit|对象所在 AS 决定 load/store 寻址；内存按 MCS251 大端|DPL:DPH，内存高字节在低地址|
|`long`/`u32`|32 bit|AS0/AS3 等决定 4 个 byte 访问|DPL:DPH:B:A，按现有实测 lane 约定|
|pointer|LLVM 默认 i32 canonical|AS 决定区域/指令；不能因 int16 把 pointer 改成 i16|普通 canonical pointer 当前 i32|
|`bit`|过渡 u8 或完整 i1/BSEG|AS5/BSEG 独立|不走普通 i16 promotion|

C integer promotion 必须由 Clang 的 `+int16` 模型决定：`u8` 表达式提升到 i16，但后端应在已知非负值时正确处理 zext；不要把 Keil `bit` 的 1-bit promotion 与普通 u8 混淆。`u16` 的 storage AS 与 `int16` 的 scalar width 是正交维度：`u16 xdata value` 是 16-bit scalar + AS3 pointer/object，不是 24-bit scalar。

### 6.2 所需改动

- **Clang 极薄**：`MCS251TargetInfo`/target feature 注册 `+int16`，预定义 `__MCS251_INT16__`（名称由 Alice 最终确定），使 `int/short/Int16Type`、`sizeof`、默认 promotion 一致；driver 只传 feature，不解析 Keil 存储类。
- **后端**：`MCS251TargetMachine.cpp`/`MCS251Subtarget` 读取 feature，拒绝与不同 `DataLayout` 混用；`MCS251ISelLowering.cpp` 保持 i16 legal、AS-aware load/store；`MCS251CallingConv.td` 继续使用 DPL:DPH；对 i8→i16 signext 的既有限制保持 fail-loud，直到 Alice 的 lowering 方案完成。
- **头文件垫片**：`u8/u16/u32` 用 `unsigned char/unsigned short/unsigned long`，不使用 `int` 作为宽度别名；对官方 demo 的 `unsigned int` 在 `+int16` 下直接得到 i16。

### 6.3 验证

1. Clang AST/IR：同一源分别开启/关闭 `+int16`，检查 `sizeof(int)==2`、函数参数/返回为 i16、AS-qualified pointer 不变。
2. llc：i16 参数/返回继续到 DPL:DPH；AS0/AS3 i16 load/store 保持高字节在低地址。
3. QEMU：沿用已有 i16 endian/stack probe；新增 AS1/AS2/AS3 i16 往返；不能把全宽往返 PASS 当作窄化路径全部正确，端序审计已有负例。
4. 编译器交叉对照：SDCC `--model-small`/`+int16` 参考输出与 LLVM host oracle 逐字符对比；不同模型分别归档。

优先级 P1/P2：int16 本身已经是目标 Clang 的初始数据模型，不是方言包最先新增的语法；优先把 AS0/AS3 i16 memory path 与 Alice 的 feature 统一，避免两个数据模型分叉。

---

## 7. 总体分工矩阵和实施顺序

### 7.1 “后端可做 / Clang 极薄 / 头文件垫片”总表

|项目|后端 address-space/属性可做|需 Clang 极薄扩展|头文件垫片即可|优先级|
|---|---|---|---|---:|
|AS0 edata 默认 + AS1 data + AS2 idata + AS3 xdata + AS4 code|是，核心工作|默认模型 token/option 可薄接|显式 qualifier 过渡宏|P0|
|AS6 classic SFR fixed volatile|是，direct selector|否|是，生成固定地址宏|P0|
|AS7 XFR extended SFR|是，EAXFR/XFR selector|否|宏生成地址|P1|
|`code` 只读对象|是，CSEG/CONST/MOVC|否|`const` 可辅助但不充分|P1|
|`sbit` 原始 lvalue|是，bit/mask lowering|是，表达 bit-address 属性|只做 GET/SET 过渡|P1|
|`bit` 过渡|否，后端无需新能力|否|`#define bit unsigned char`|P0.5|
|`bit` 完整 BSEG|是，BSEG allocation/bit ISel|可选 `bit` AST token|不能只靠垫片|P1|
|`interrupt N` function attribute|是，stub/VECS/RETI/linker|是，尾部语法解析|attribute 形式可由头文件写|P1|
|`__interrupt` 别名|是，读取同一属性|是，token 兼容|不能可靠宏化带参数语法|P1|
|XSmall 默认 edata|是，TargetMachine/DataLayout|默认 option/宏|官方头注释已有证据|P0|
|Small/Large 默认选择|是，布局/area/selector|只传 `-mmodel`|不能只靠头文件|P2|
|int16|是，i16 legal/ABI|`+int16` target feature|u16 typedef|P1/P2|

### 7.2 推荐实施顺序

1. **P0：AS0 + AS1/AS2 基础、classic SFR、XSmall 默认**。先让 `u8/u16/u32`、局部/栈对象、P0/T0/T1/UART1 的固定地址访问有单一解释；同时补 DataLayout 单测。
2. **P1：AS3 xdata、AS4 code/CONST、global data area**。解决当前 AsmPrinter 拒绝全局数据的问题，补 xdata/CONST relocation 和 MOVX/MOVC QEMU probe。
3. **P1：interrupt 属性和向量生成**。先裸 asm `lcall body; reti` probe，再接 Clang attr；只把 QEMU 背书的 TF0/TF1/INT0/INT1/UART1 纳入正向。
4. **P1：bit u8 过渡和 BSEG 设计**。先扩大可编译语料，再实现 BSEG/bit ISel；sbit 透明 lvalue 单独验收。
5. **P1/P2：int16 与 Alice feature 合并**。固定 `IntWidth/Int16Type/DataLayout/CallingConv` 的单一来源，补 AS-qualified i16。
6. **P2：Small/Large 模型**。每个模型由 SDCC/Keil oracle 定默认 AS、指针宽度、area 和函数指针规则；没有 oracle 的模型保持显式 qualifier only。
7. **不纳入方言包首轮正向**：PWM/SPI/I2C/CAN/RTC/DMA/USB/比较器等 QEMU 无模型外设，以及 UART2/3/4、ADC、WDT、T2/T3/T4 的运行行为。它们可以编译/静态检查，但不能伪造 QEMU PASS。

---

## 8. 验收清单

- [ ] `Triple::computeDataLayout()` 的 MCS251 字符串固定包含预定 `pN`，并有 `DataLayout` 单测。
- [ ] AS0 默认 XSmall edata；AS1 direct、AS2 idata、AS3 MOVX、AS4 MOVC、AS6 SFR、AS7 XFR 的 IR→asm 选择均有最小测试。
- [ ] SFR 头文件垫片生成 `volatile` 固定地址；`0x80–0xff` 不走间接 edata；`0x7efe..` 需要 XFR 类别。
- [ ] `code` 同时满足只读类型、CONST/CSEG area、MOVC load；不再仅替换成 const。
- [ ] bit 过渡路线与 BSEG 完整路线明确分开；普通 `&|^~<<>>` 不被误报为 Keil bit type。
- [ ] `interrupt N` 生成唯一 VECS 槽和 `lcall body; reti` stub；QEMU 先通过 TF0/TF1/INT0/INT1/UART1，再扩展其他源。
- [ ] XSmall/Small/Large 的默认 AS 由 oracle 定案；显式 qualifier 在所有模型不漂移。
- [ ] `+int16` 与 `MCS251TargetInfo`、DataLayout、CallingConv、u16 typedef 只有一个数据模型来源。
- [ ] QEMU 报告中的不可测外设仍标不可测；静态可编译不被写成运行 PASS。

本文件是设计研究，不代表上述生产代码改动已经落地。