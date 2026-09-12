# 运行期 AS3/AS4 指针与 Keil 方言兼容方案（待拍板）

**日期**：2026-09-12。**状态**：提案冻结待用户拍板，**未实施**。**第一原则（用户裁定）**：符合 C 语言要求优先于 Keil 兼容。
**来源**：Alice 调查（agent_3ffec198，网络中断前结论经消息完整交付，代码事实由协调员独立复核）。

## 1. 问题陈述

X5 官方语料矩阵（`/home/liu/LLVM_STC32/mcs251-corpus-matrix/REPORT.md`）显示 `as-cast-gap` 阻塞 **20 个 demo = 16 USB + 4 非 USB**。用户裁定 USB 不做，故本提案目标为非 USB 的 **4 个**：

| demo | 形态 |
|---|---|
| 41-3.2寸ILI9341驱动TFT显示屏（硬件I8080） | `unsigned char code gImage_qq[3200]` 传给 `u8*` 参数 |
| 42-硬件SPI接口驱动1.3寸TFT显示屏 | 同上 |
| 43-硬件USART1复用SPI驱动1.3寸TFT显示屏 | 同上 |
| 82-CANFD使用DMA收发测试 | `uint8_t xdata *` 传给 `uint8_t*` 参数 |

### 1.1 根因更正（重要——此前分析有两处错误，已由代码实证推翻）

**更正一：不是字符串字面量问题。** 报错行是 `Gui_Drawbmp16(30,30,gImage_qq)`（41 的 test.c:227/229/231），`gImage_qq` 定义在 `font/pic.h:9` 为 `unsigned char code gImage_qq[3200]`（AS4 code 数组）。`Show_Str(...,"QQ",...)` 在 228/230/232，**无诊断**。

**更正二：X1 未破坏标准 C 字面量语义。** 实证：
- 纯 C11（无 `-fmcs251-keil`、无 `__code`）：`f("QQ")` rc=0，AST `char[3]`，IR `@.str` 为 AS0；
- 加 `-fmcs251-keil`：同样 rc=0；
- X1-4 的 `AdjustMCS251StringLiteralPointerInit` 仅在 **AS4 目标上下文**（`__code char *p = "..."`）调整字面量节点。

故本矩阵可见诊断中**字面量失败计数 = 0**。此前"X1 可能改坏字面量、放行反而是恢复 C 语义"的假设**不成立**。

### 1.2 真实性质

`code` 对象（AS4）隐式传给普通 AS0 指针参数。在 C 严格语义下这是**不允许**的隐式转换：WG14 N1275（TR18037 草案）§5.1.3 规定**具名地址空间不必是 generic 的子集，只有声明了包含关系才允许相应的隐式赋值**，不能仅凭表示同宽放行。

**所以路线 A 若要放行，本质是引入"AS4 数据对象空间 ⊆ 32 位 AS0"这一 implementation-defined 包含关系声明**——TR 允许实现这样做（不是让用户批准违反 C），但必须**正式冻结**该关系，而非静默删诊断。

### 1.3 既有实现状态（代码实证，逐条复核）

| 事实 | 位置 | 结论 |
|---|---|---|
| `LowerAddrSpaceCast` 的 `IsFarRAM = {0,3,9}` **不含 AS4** | `MCS251ISelLowering.cpp:1048` | AS4→AS0 的 cast **未实现**；"已有 AS4 DR load"≠"cast 可用" |
| `isV1ObjectCompatible` 在 `ASLayoutVersion==1` 直接 `return true` | `MCS251AsmPrinter.cpp:449` | 矩阵契约 `1,1,32,8,1` 下**没有撞 v2 身份门禁** |
| Compatibility layout `ProgramAS=0`；v2 layout `ProgramAS=4` | `MCS251TargetParser.cpp:42/48/51` | v1 下 `AllowStaticPointers=false` |
| D.5 静态指针槽序列化代码已存在，受 `ProgramAS==4` 门控 | `MCS251ISelLowering.cpp:3105/3281`（`AllowStaticPointers`）、`:3155-3166/3314-3320`（`canonicalizePointer32` 序列化） | 非从零未实现，是**软件路径部分存在、身份与完整验收未开放** |

**关键澄清**：当前 4 例（及 16 USB 例）**卡在前端 Sema 诊断**（`changes address space of pointer`），根本未到后端。后端这些"部分存在"的路径是**未来实现路线 A 时可复用的资产**，不是当前阻塞点。

## 2. C 语言合规性评估（硬门槛）

### 2.1 标准依据

- **N1275 §5.1.3**（TR18037）：具名 AS 不必是 generic 子集；只有声明包含关系才允许隐式赋值。
- C 标准本身不规定具名地址空间；`__xdata`/`__code` 是**目标扩展**。TR18037 是扩展空间的规定。
- 字符串字面量：C 里类型为 `char[N]`（无空间限定），可隐式转 `const char *`——我们**未违反**（§1.1 更正二实证）。

### 2.2 逐路线判定

| 路线 | C 合规判定 | 依据 |
|---|---|---|
| **A. 前端放宽 AS4→AS0 隐式转换** | **C 未规定（目标扩展）**；若正式声明 `AS4 数据对象空间 ⊆ AS0` 包含关系，则**符合 TR**；若不声明而硬放行，则**违反 N1275 §5.1.3** | N1275 §5.1.3 |
| **B. 保持设计，改 demo** | **符合 C**（严格类型，最保守） | C/TR 无冲突 |
| **C. 真 Keil 通用指针（3 字节带 tag）** | **C 未规定**，但推翻 B.2/B.3 冻结的 4B canonical AS0，代价最大 | DESIGN B.2/B.3 |
| **D. 头文件方言兼容层** | **符合 C**（仅拼写替换，不改类型/转换语义） | 见 §4 |

## 3. 路线 A 详情与张力

### 3.1 推荐方向（Alice）：**有条件 A**

先**正式冻结 TR 允许的包含关系**（AS4 数据对象空间 ⊆ 32 位 AS0，保留 32/8 表示与 AS 类型），再实现前端 + AS4 cast + 身份/静态槽发布闭环。**不是简单删诊断。**

### 3.2 必须拍板的独立张力（Alice 明确指出）

**const-only 与 TR 完整子集规则的张力**：TR 对满足普通类型/CVR 约束的转换**不区分目标是否 const**。若只放行 const 指针（例如只让 `const code` 数据传参），这是**更窄的目标安全策略**，**不能冒称完整 TR 子集规则**。

- 选择 A-1（完整）：声明 `AS4 ⊆ AS0` 全量包含 → 符合 TR，但 **CODE 写责任扩大**（AS0 指针可解引用写，需后端保证 AS4 区域只读，或在 Sema 层对写入路径单独诊断）。
- 选择 A-2（const-only）：只放行 const → 更安全，但需**明确标注为"目标策略，非 TR 完整子集"**。

### 3.3 路线 A 实施切片草案（若采纳）

1. **设计冻结**：在 DESIGN.md 正式写入 AS4→AS0 包含关系声明（或 const-only 策略说明）+ N1275 依据；修订 §365/§379 的"跨 AS 隐式转换必须诊断"表述为"未声明包含关系的跨 AS 隐式转换必须诊断"。
2. **前端**：SemaMCS251 增加 AS4→AS0（及可选 AS3→AS0）隐式转换放行点，插入 `addrspacecast`；保留其他跨 AS 诊断。
3. **后端**：`LowerAddrSpaceCast` 的 `IsFarRAM` 纳入 AS4（i32 路径）；静态指针槽在 v1/v2 的开放决策；`AllowStaticPointers` 门控调整。
4. **身份**：v1/v2 对象身份门禁的处置（当前 v1 下 AS3/AS4 定义全局已由 X3 支持放置，但**签名含 AS3/AS4 指针**仍是 v2-only 判定点，需核）。
5. **测试**：4 个非 USB demo 编译+链接+字节链；正负例（仍拒的跨 AS 形态）；对 X1/X2/X3/X4 回归。
6. **CODE 写责任**：AS0 指针指向 AS4 后的写入路径处置（拒绝 or 定义 UB）。

## 4. 路线 D 详情：纯头文件方言兼容层（用户提出，Alice 复核）

**目标**：不开 `-fmcs251-keil`，用头文件吃下 Keil 方言拼写。

### 4.1 事实依据

- 核心拼写 `__xdata`/`__code`/`__bit` 在 MCS-251 目标上**无条件可用**（`IdentifierTable.cpp:323/341-342`，`TargetInfo::adjust` 自动开 `MCS251Bit`/`MCS251AddrSpaces`）——**仅 C，C++ 不可用**。
- 裸词 `xdata`/`code`/`bit`/`sbit`/`interrupt` 才需要 `-fmcs251-keil`（`IdentifierTable.cpp:306/324-326/344-346`）。
- BT06 先例：`mcs251_bit_compat.h` 用预生成头替代官方 sbit 声明（343 名台账：103 候选映射 + 拒绝项）。

### 4.2 三类判定

| Keil 语法 | 头文件方案 | 说明 |
|---|---|---|
| `xdata`/`code` 限定符、`bit` 类型 | ✅ 可行 | `#define xdata __xdata` 等；**风险**：`code`/`xdata` 是常见词，宏遮蔽同名标识符——须沿用 BT06 显式台账/最小集纪律 |
| `sbit name = addr;` | ✅ 可行（替换，非 include） | 宏无法造声明语法；必须**替换/排除官方头原 sbit 声明**，用预生成 compat 头直接给名字定义。**光 include 再保留原头不行** |
| `void f(void) interrupt N` | ❌ 不可行 | 声明符后缀，预处理器无法匹配"标识符+数字"形式、无法重排到参数括号后。只能 `-fmcs251-keil` 或改写源码 |

### 4.3 台账口径更正

BT06 头的 343 名是「103 候选映射 + 拒绝项」，**不可写成 343 全可用**（`mcs251_bit_compat.h:19-39` 明确有拒绝项）。

## 5. 推荐组合

**D + A 组合**：
- **D** 只改变拼写（`xdata`/`code`/`bit` 宏化、sbit 头替换），**不修 AS 转换**，**不消除 bit 对象/ABI 门禁**（那些是 P09 与身份发布的事）；
- **A** 负责 `code` 对象传参的语义（含 TR 包含关系冻结）；
- **`interrupt N`** 按用户裁定走**改写源码**（不修方言）。

## 6. 待用户拍板清单

1. **【头条】C 合规 vs Keil 兼容**：是否正式声明 `AS4 数据对象空间 ⊆ 32 位 AS0`（TR 允许的 implementation-defined 选择）以放行 `code` 对象传参？还是维持严格诊断、改 demo？
2. **const-only 还是全量**：若放行，选 A-1（全量，符合 TR，CODE 写责任扩大）还是 A-2（const-only，更安全但非完整 TR 子集）？
3. **是否连带 AS3→AS0**（`BYTE xdata *` 传参，82-CANFD 形态）？
4. **函数指针与数据指针**是否一并放开？（DESIGN D.4 明确禁止，建议不动。）
5. **路线 D 是否实施**：头文件方言兼容层（覆盖 xdata/code/bit/sbit，不含 interrupt）；`code`/`xdata` 宏遮蔽风险是否可接受？
6. **v2 身份门禁**：若采纳 A，签名含 AS3/AS4 指针的模块是否需一并开放 v2 身份（或仅 v1）？
7. **DESIGN §801 的 AS4 数据指针重定位契约（CP-A）**是否本片一并落地？

## 7. 附：本提案未做的事

- 未改任何产品代码/测试/已落库文档；仅新增本提案。
- 4 个非 USB demo 的精确字节链验证留待实施切片。
- 16 个 USB demo 按用户裁定排除。
