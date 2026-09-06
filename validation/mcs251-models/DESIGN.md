# MCS251 模式体系设计稿

**作者**：Alice  
**状态**：设计提案；默认策略及函数级近远调用契约方向已获确认；PM 验收入库  
**源码基线**：`20367e184`  
**适用范围**：Clang、LLVM MCS251 后端、LLVM MC、MCS251 对象 ABI、链接器、CRT 与 validation 构建层

本文中的“必须”“禁止”“应”分别表示规范要求、不可接受的行为和推荐实现方案。重定位新增编号、对象 v2 字段及 CLI 数值接口均为分配草案，须在实施前统一冻结。

---

## 0. 总体裁定与现状边界

### 0.1 核心裁定

1. **默认存储模型命名为 XSmall，默认代码模型命名为 Huge。**
2. 默认 Huge 是本 fork 的 **flat-24 扩展语义**：保持全 ECALL/EJMP，不因模块归属自动改用近调用。
3. 默认 C 数据模型保持 `int=32、long=32`；Tiny/XTiny 只改变默认数据指针，不改变整数提升规则。
4. 地址空间、默认对象放置、指针 ABI、调用返回帧、控制转移编码、物理板级布局是不同维度，不合并为一个字符串开关。
5. 模式名和器件名只由构建层选择与解释。编译器、链接器生产实现只消费结构化枚举或数值契约。
6. DSEG 自动扩展只适用于**可重定位到 edata 的普通对象**。显式 `__data`、direct-only 对象禁止溢出迁移。
7. 近调用是**函数级 ABI 契约**。只有调用点与被调入口的返回帧契约一致，链接器才允许选择 ACALL/LCALL。
8. ELF RELA 0–8 号语义不变。新布局、松弛、指针 ABI 和初始化协议使用版本化扩展。
9. **默认字节零变化是硬要求**。新模式能力先走显式 v2 路径，不重解释既有 REL 或 ELF v1 输入。
10. Source/native 是编译器唯一内建 CPU 指令模式；不提供 Binary 模式切换，不支持软件 Code Banking。

### 0.2 需求依据

用户补充的 STC 手册 §2.17.7 明确给出：

- 超过 64K 代码需选 Huge。
- Keil Huge 标示为“64K functions, 16M progr.”。
- 示例 ROM 区为起点 `0xFE0000`、大小 `0x20000`。
- 推荐 Memory Model 为 XSmall，界面描述包含 near vars、far const、ptr-4。
- Code Banking 示例未启用。

本 fork 采用这些配置背后的能力维度，不复制 Keil UI、ABI 或工具链历史限制。

**手册模式名不构成 Keil 二进制兼容承诺。** 尤其本 fork 默认 `int=32`，而 Keil C251 全系 `int=16`。

### 0.3 已核实的实现边界

| 项目 | 当前事实 | 设计影响 |
|---|---|---|
| 数据布局 | 大端，普通指针 32 位，标量按字节对齐 | 兼容路径必须保留完整布局字符串 |
| 全局变量 | 可变全局发往 DSEG；非零地址空间定义被拒绝 | named AS 需贯穿前端、ISel、对象输出 |
| DSEG 分配 | Python 链接器按经典规则限制在低 128B | 不能仅修改一个上限常量 |
| 栈门禁 | `--edata-end` 当前只参与剩余栈容量检查 | 变量区语义须通过新布局协议启用 |
| 指针 ABI | 存储为 4B；发送使用 DPL/DPH/B/A，A 置零；接收会重新规范化高字节 | 不是任意 32 位数值地址 |
| 指针后续参数 | 当前拒绝第二及后续指针静态参数 | 2B/4B 静态指针槽需要单独扩展 |
| 控制转移 | ECALL/EJMP；返回 ERET | 近调用不能只替换 opcode |
| ELF | E2 已实现；0–8 已编号 | 不等于链接器已支持全部扩展 |
| 原生链接器 | 当前工作树没有 `lld/MCS251` | 本计划依赖 ELF 链接器基础阶段 |
| 旧方言稿 | AS、默认布局和部分实现状态已过时 | 本文统一模式维度，不把旧建议当事实 |

关键证据：

- `validation/mcs251-elf/SPEC.md`
- `validation/mcs251-demo-test/t4-probes/ISR-STUB-VERDICT.md`
- `validation/mcs251-p11/hw-semantics-report.md`
- `validation/mcs251-dialect/DIALECT-PACKAGE.md`

`SPEC.md` 文件头仍保留待审批文字；本文按用户确认的已批准状态使用其规范，不在本任务修改原文件。

---

# A. CLI、默认值与配置契约

## A.1 三层配置模型

采用以下单向流水线：

**构建层模式名 → 数值化目标契约 → 编译/对象/链接校验**

### 第一层：用户与构建接口

建议提供以下模式选择名称：

- `-mcs251-memory-model={tiny,xtiny,small,xsmall,large}`
- `-mcs251-code-model={small,medium,compact,large,huge}`

**在“生产代码零模式字符串”红线下，这两个接口属于 validation 构建驱动或包装器，不作为 Clang/llc/lld 直接解析的命名选项。**

Make 的等价入口为：

- `MEMORY_MODEL`
- `CODE_MODEL`

如果未来要求 Clang 本身接受上述命名 flag，必须先批准放宽“模式字符串不得进入编译器”的红线。不能同时承诺两种相互矛盾的接口要求。

### 第二层：编译器数值契约

建议的数值接口维度如下，最终拼写在实施阶段冻结：

| 维度 | 建议表达 | 含义 |
|---|---|---|
| 契约版本 | `-mcs251-contract-version=1/2` | 兼容旧对象或新模式体系 |
| 默认数据指针 | `-mcs251-data-pointer-bits=16/32` | Clang、LLVM DataLayout 一致 |
| 默认对象放置 | `-mcs251-default-data-space=1/3/8` | data-preferred、xdata、edata |
| AS 布局版本 | 数值 `as-layout-version` | 固定 named AS 编号和宽度 |
| 函数默认返回帧 | 数值 16/24 | Near16/RET 或 Far24/ERET |
| 调用候选 | 本地/外部候选位掩码及偏好 | 不传 Medium/Huge 字符串 |
| 跳转候选 | 本地/外部候选位掩码及偏好 | 与调用独立 |
| 放置协议 | 数值 `placement-version` | 旧低页分配或新可扩展分配 |
| 初始化协议 | 数值 `init-version` | XINIT v1/v2 |

编码候选位掩码建议定义为：

- bit0：J11/C11。
- bit1：J16/C16。
- bit2：J24/C24。

这些是目标语义字段，不是 CPU feature。

### 第三层：链接器数值契约

链接器消费：

- 对象 ABI note。
- section 存储类别与能力。
- 函数级返回帧契约。
- 调用/跳转 relocation 与其候选策略。
- 板级 ROM、EDATA、XDATA 数值范围。
- 数值化的总跨度、页和 bank 门禁。

链接器不得读取器件名或模式名，再据此推断任何地址范围。

## A.2 默认值与兼容迁移

### 用户可见默认

- `MEMORY_MODEL=xsmall`
- `CODE_MODEL=huge`
- `int=32`
- Source/native
- 无软件 Code Banking

### 工具链兼容默认

无新增选项时：

- 原始 DataLayout 字符串不变。
- 原始 REL/asm 发射不变。
- 显式 ELF 输出仍可生成现有 v1 对象。
- 全 ECALL/EJMP、ERET 不变。
- 旧 DSEG 分配规则和原诊断不变。
- 既有 CRT、初始化协议、链接顺序不变。

**“默认命名为 XSmall”不是声称当前已经实现完整 XSmall 变量区。** 当前实现是该命名下的兼容配置；完整 edata 放置由显式 v2 契约开启。

建议构建层新增 `MODEL_CONTRACT=1/2`：

- `1`：现有黄金基线。
- `2`：完整模式体系。

命名默认不变，能力升级显式。待新体系验收后，是否提升工程默认契约版本应单独审批，不能随实现合入偷偷切换。

## A.3 Huge 的现代定义

本 fork 的 Huge 默认：

- 直接调用 ECALL。
- 非 rel8 控制转移 EJMP。
- Far24 函数返回 ERET。
- 不施加单函数、单输入文件 64K 的模型限制。
- 可执行地址仍限制在 CPU 的 24 位空间及板级实际 ROM 区。

可选“模块内近转移”策略由构建层降成数值候选：

- 模块内优先 16 位。
- 模块外使用 24 位。
- 只有 ABI 和范围允许时采用近形式。
- 契约不匹配时保留远形式、显式经 thunk，或在严格策略下拒绝。

默认不启用这一优化，因此默认字节不变。

## A.4 `int` 宽度裁定

**本模式体系统一建议 `int=32、long=32`。**

Tiny/XTiny 不是“传统 C 数据模型”开关：

- `short=16`。
- `int=32`。
- `long=32`。
- `long long=64` 的语言类型保持现状；不因此扩大后端已支持的运算范围。
- 整数提升规则不随存储模型变化。

当前 `+int16` 前端入口已有 lit 覆盖，应保留在兼容域，不删除、不转义成模式选择。

新体系若继续开放 `int16`：

1. 必须是独立翻译单元配置。
2. 必须记入对象 ABI 身份。
3. 不允许 int16/int32 对象未经桥接裸混链。
4. 不允许函数 `target` 属性改变整型宽度。
5. 首期 v2 可明确只支持 int32，其他组合诊断“不支持”，不能悄悄按 int32 处理。

## A.5 IR 与工具一致性

必须在前端确定类型布局，不能等到 llc 再“把指针变窄”。

- Clang 生成正确的 opaque pointer AS、DataLayout、函数调用约定。
- LLVM 模块保存数值化模块契约。
- llc 校验命令行、模块契约、DataLayout 三者一致。
- 非空且冲突的 DataLayout 必须拒绝，不得覆盖后继续编译。
- 相同指针布局但不同默认对象放置，也需检查模块契约；DataLayout 本身不编码 area 放置。
- 同一个 Module 不允许函数间改变默认数据指针宽度。
- LTO 不得通过覆盖 DataLayout 混合 16/32 位默认指针模块。

---

# B. 存储模型、地址空间与链接布局

## B.1 五种模型

本表“默认指针”指未显式限定地址空间的**数据指针**。

| 模型 | 默认可变静态对象放置 | 默认数据指针 | 普通指针可表示范围 | 自动变量 |
|---|---|---:|---|---|
| Tiny | data-preferred | 2B | `00:0000..00:FFFF` | 寄存器或内部栈 |
| XTiny | edata | 2B | `00:0000..00:FFFF` | 寄存器或内部栈 |
| Small | data-preferred | 4B | `00:0000..FF:FFFF` | 寄存器或内部栈 |
| XSmall | edata | 4B | `00:0000..FF:FFFF` | 寄存器或内部栈 |
| Large | xdata | 4B | `00:0000..FF:FFFF` | 寄存器或内部栈 |

“data-preferred”是本项目对自动扩展要求的明确表达：

- 普通对象优先使用低 direct RAM。
- 放不下时允许整体迁入 edata。
- 它不是源类型 `__data`。
- 显式 `__data` 对象始终必须位于 direct RAM。

模式不把普通局部变量全部改成静态对象，也不把 Large 的栈搬到 XDATA。栈与静态参数槽是独立 ABI 维度。

## B.2 稳定地址空间表

建议保留旧方言草案的主要编号方向，新增独立 edata 和 far generic。以下宽度在所有 v2 模型中保持稳定，只有 AS0 随默认指针模型变化。

| LLVM AS | 源级名称 | 指针位数 / ABI 对齐 | 语义 |
|---:|---|---|---|
| 0 | 默认数据指针 | 16/8 或 32/8 | 由模型决定宽度；不是固定 edata 别名 |
| 1 | `__data` | 16/8 | 严格 direct RAM，已放置对象必须在 `[0,0x80)` |
| 2 | `__idata` | 16/8 | 经典内部间接 RAM，范围受内部 RAM 配置约束 |
| 3 | `__xdata` | 32/8 | 外部数据地址，保持完整 24 位有效地址 |
| 4 | `__code`、程序指针空间 | 32/8 | CODE 只读数据及函数地址 |
| 5 | bit 空间 | 暂不定义普通字节指针 | 保留；禁止借普通 load/store 假装支持 |
| 6 | `__sfr` | 16/8 | classic SFR，direct byte 访问 |
| 7 | `__xfr` | 32/8 | 扩展寄存器空间；不等于普通 XDATA |
| 8 | `__edata` | 16/8 | 00 段内部数据；禁止 SFR-direct 推断 |
| 9 | `__far` generic | 32/8 | 显式 canonical 24 位数据地址容器 |

说明：

- 不使用 8 位 C 指针来实现 `__data`。物理范围小不要求源指针对象只有 1B；16 位表示可减少非法类型、GEP 和 ABI 特例。
- AS1 与 AS6 数值范围不同且访问对象不同，不能合并。
- AS1、AS2、AS8 与相应 generic 地址可能指向同一 RAM；**不同 AS 不自动意味着 NoAlias**。
- AS9 不承载 classic SFR。SFR 访问必须保留 AS6。
- AS5 未开放前，显式引用必须报错，不继承 AS0 宽度后接受。
- XFR 是否可由普通 far 访问及其使能协议，必须由独立能力契约确认；首期禁止隐式丢失 XFR 语义。

旧方言稿将 edata 等同 AS0 的方案不适用于整个模式矩阵，本文替代该部分建议。

## B.3 DataLayout 变体

### 兼容布局

无新增选项时必须原样保留：

```text
E-m:s-p:32:8-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8
```

不得误写为旧方言稿中的简化字符串或 `m:e`。

### v2：Tiny / XTiny

```text
E-m:s-p:16:8:8:16-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0
```

### v2：Small / XSmall / Large

```text
E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0
```

其中：

- `pN:size:abi:pref:index` 均显式固定索引宽度。
- `P4`：程序地址空间为 AS4。
- `A0`：普通 alloca 保持默认数据指针形式，物理分配仍是内部栈。
- `G0`：普通全局保留 AS0；默认 area 由放置契约决定。
- `S8`：栈对象 ABI 对齐仍为 1B，不是 8B。
- Tiny/XTiny、Small/XSmall/Large 各组内部共享 DataLayout；差异在放置契约，不伪造无意义布局字符串。

Clang `TargetInfo` 与后端必须使用共享、纯函数式的目标布局描述。`Triple::computeDataLayout()` 继续提供兼容默认，不以 triple 拼器件名或模式名。

## B.4 named address spaces 的 LLVM-native 路线

### 前端

提供两层接口：

1. 现代接口：`__data/__edata/__xdata/__code/__far` 等目标限定。
2. 基础接口：Clang 已有 `address_space(N)` 属性。

现代名称应映射到稳定的 LangAS/目标 AS，而不是仅给全局附一个 section 属性。

必须区分：

- 指针变量本身存在哪里。
- 指针指向的对象属于哪个 AS。

Sema 必须检查：

- 跨 AS 隐式转换。
- 指针与整数转换。
- CODE store。
- SFR 非 byte 访问。
- 不支持的 AS 局部自动对象。
- 外部声明与定义的 AS 不一致。
- 聚合字段和函数形参的 AS 类型一致性。

首期可允许显式 AS 的静态存储对象，拒绝需要特殊分配的 AS 自动对象；不能把 `__xdata` 自动对象默默放到内部栈。

### IR 与优化器

- global、load/store pointer、GEP、函数参数完整保留 AS。
- 跨 AS 用 `addrspacecast`，不是宽度碰巧相同就用 bitcast。
- 目标提供 address-space cast 和别名关系规则。
- `memcpy/memmove/memset`、聚合展开、SROA、内联与优化后重新物化地址都必须保留 AS。
- 不能由优化前“是不是常量”决定是不是 SFR。

### 后端

改造地址分类器，使输入至少包含：

- AS。
- 指针宽度。
- 对象/指令访问宽度。
- 符号或绝对地址。
- direct-required / indirect-safe。
- frame-relative 信息。
- 原始 MachineMemOperand。

选择方向：

| AS | 优先访问路线 |
|---|---|
| AS1 data | 静态符号可 direct；动态指针可用等价内部间接访问，仍受 data 对象范围约束 |
| AS2/AS8 | `@WR` 16 位间接；未测形式先采用零扩展到 DR 的正确回退 |
| AS0 near | 16 位地址算术，00 段间接访问 |
| AS0 far / AS9 | canonical DR 24 位有效地址访问 |
| AS3 | 经实证的 XDATA 指令族或 DR 统一寻址 |
| AS4 | 经实证的 CODE 读取指令族；禁止 store |
| AS6 | SFR direct byte |
| AS7 | 经能力声明支持的 XFR 访问 |

不因名字叫 xdata/code 就照抄 8051 的 MOVX/MOVC 模板。具体 opcode 必须由 native ISA 及探针确定。

兼容路径保留现有“绝对 byte 常量 `<=0xff` 走 direct”的历史行为；v2 AS0 不再保留这一歧义。v2 SFR 头文件必须显式发出 AS6。

## B.5 默认变量、常量与对象文件 area

| 对象类别 | v2 ELF 建议 section | 逻辑区域 |
|---|---|---|
| 普通 data-preferred 对象 | `.mcs251.dseg.flex.*` | DSEG，可迁移 |
| 显式 `__data` | `.mcs251.dseg.direct.*` | strict DSEG |
| 默认/显式 edata | `.mcs251.edata.*` | EDATA |
| `__idata` | `.mcs251.idata.*` | ISEG |
| 默认/显式 xdata | `.mcs251.xdata.*` | XSEG |
| 显式 CODE 常量 | `.rodata.*` | CODE |
| 静态参数槽 | 专用 DSEG/OSEG v2 section | 内部 RAM |
| 初始化表 | `.mcs251.xinit.v2.*` | CODE |
| SFR/XFR 绝对声明 | 受控符号/元数据 | 不分配普通 RAM |

每个可独立放置对象应有独立 section，或有明确的不可拆分分配组；不再把一个翻译单元的全部变量聚合为巨大 DSEG slice。

### `const` 与 far const

`const` 是可修改性限定，不等于地址空间限定。

推荐规则：

- 显式 `__code` 常量进入 ROM，使用 AS4 指针。
- Tiny/XTiny 中普通 `const` 若必须由普通 2B 数据指针访问，其运行副本必须位于 00 段，不能把 ROM 地址截断。
- 32 位 generic 配置可在访问语义等价、类型转换合法时将只读对象放 ROM。
- “near vars, far const”作为推荐存储策略，用明确 CODE 类型和构建配置表达，不污染所有 C `const` 的类型语义。
- 兼容路径的现有 CSEG 常量放置不变。

字符串字面量、常量池、函数地址初值必须分别测试；不能仅验证一个整数常量数组。

## B.6 DSEG 自动延伸到 edata：完整闭环

### B.6.1 必须保存的区别

链接器必须知道对象属于哪一类：

1. **DirectRequired**：只能在 `[0,0x80)`。
2. **InternalMovable**：可在低页，也可在 edata。
3. **InternalExtended**：按 edata 分配。
4. **ExternalData**：只在声明的 XDATA 范围。
5. **AbsoluteReservation**：固定占用，不得移动。

这些类别由对象元数据表达，不由符号名、器件名或模式名猜测。

旧 DSEG 对象没有“可迁移”承诺，禁止统一扩窗。

### B.6.2 分配顺序

v2 推荐确定性顺序：

1. 登记板级不可用区、固定地址预留、寄存器银行、bit 字节占用。
2. 放置 DirectRequired、经典 ISEG、必须低页的 overlay。
3. 对 InternalMovable 按输入文件/section 顺序尝试低 direct 窗口 first-fit。
4. 低窗找不到连续区时，**整个对象**迁往 edata first-fit。
5. 放置 InternalExtended 与可扩展内部参数槽。
6. 在独立 XDATA 区放置 ExternalData。
7. 根据所有内部 RAM 占用计算栈高水位和门禁。

edata 自动扩展搜索默认从 `0x100` 开始，到 `edata_end+1` 为止；低于 `0x100` 的空间由 direct/idata 类别负责，避免无声明侵占经典间接 RAM。

未来可通过数值区域配置开放更多低 RAM，但不能依靠型号分支。

### B.6.3 对象不可拆分

禁止将同一个数组：

- 前 120B 放低页。
- 余下部分放 edata。
- 再让 GEP 假定它仍连续。

大数组必须整体迁移。不同全局可以分别放置。结构体、初始化目的区、显式链接组也需保持连续性。

### B.6.4 为什么迁移后代码仍正确

所有 InternalMovable 符号访问必须使用：

- 16/32 位可重定位地址加间接访存，或
- 有完整地址重写协议的专用数据松弛。

首期采用前者，不引入数据指令松弛。

禁止对可迁移符号发射不能扩展的 direct8 relocation，再期望链接器“顺便修好”。

显式 `__data` 才可使用 strict direct relocation；越界必须失败。

### B.6.5 `--edata-end` 的正式语义

v2 中：

- `--edata-end` 是内部扩展 RAM 的 **inclusive 物理上界**。
- 所有内部静态对象必须位于其允许区域内。
- 同时参与栈容量检查。
- 不依赖 `__mcs251_stack_base` 是否被引用才做静态变量越界检查。

自启动 v2 固件必须启用栈契约；不允许通过删除链接符号引用绕过剩余栈检查。

保留已有公式：

```text
H          = max(0x100, 所有非空内部 RAM 分配的 exclusive end)
first_byte = align_up(H, 16) + 16
SPX        = first_byte - 1
capacity   = edata_end + 1 - first_byte
```

必须满足：

```text
capacity >= 1024
```

- CODE、XDATA 不计入 H。
- 寄存器、bit 字节、DSEG、OSEG、ISEG、绝对内部预留均计入 H。
- OSEG 按已分配 overlay 范围计一次。
- 运算使用检查过的宽整数，禁止无符号下溢导致误通过。
- map 输出 H、guard、SPX、capacity、物理末地址和每个迁移对象。

`l_DSEG` 不重定义成内部数据末端；新增结构化 map 指标描述总内部数据，避免破坏旧统计符号。

### B.6.6 5091B 验收样例

设一个 5091B 可迁移对象整体放在 `0x0100`，无其他高位内部占用：

```text
object_end = 0x14e3
first_byte = 0x1500
SPX        = 0x14ff
```

- `EDATA_END=0x0fff`：变量已经越物理界，必须失败。
- `EDATA_END=0x3fff`：剩余栈 `0x2b00=11008B`，可通过。
- 同大小显式 `__data`：无论板有多少 edata，都必须报 direct 区不足。

这三种结果必须分别测试，不能把“16KiB 板可运行”写成“DSEG 无限扩展”。

## B.7 XINIT 与 CRT

### v1 保持不变

现有六字节头：

```text
u16 destination; u16 object_size; u16 payload_size;
```

它可以描述 00 段内的 edata 目的地址，但当前验证器只接受 DSEG slice，且不能完整支持 XDATA 或大于 65535B 的对象。

### v2 建议

采用独立 section、独立协议版本：

```text
u32 destination; u32 object_size; u32 payload_size; payload[]
```

均大端，destination 必须是 canonical 24 位有效地址。

- `payload_size=0`：清零。
- `payload_size=object_size`：完整复制。
- `object_size>0`。
- 总表长、循环计数、源/目的地址递增均使用能覆盖 24 位空间的表示。
- 不把 32 位字段误解为允许 4GiB RAM。
- 目的整个区间必须落在一个合法已分配对象中。
- 目的不得为 CODE、SFR、XFR。
- pointer initializer 在 payload 内使用对应严格 relocation。
- ROM 源跨 64K 时必须传播 bank 进位。

若先实现内部 RAM 子集，可以有阶段性限制，但必须明确拒绝未实现的 XDATA/大对象记录。

CRT 必须：

1. 以 v2 契约请求并安装 SPX。
2. 用适合目的 AS 的指令清零/复制。
3. 验证过 24 位 ROM 读取路线。
4. 调用匹配函数返回帧的初始化入口与 main。
5. 不把 RAM NOBITS 当成上电自动归零。

## B.8 链接器分工

### Python REL 兼容路径

`validation/mcs251-ld/mcs251_ld.py`

职责：

- 保持既有合法输入和测试语义。
- 作为冻结的旧布局与 HEX 对照源。
- 不通过放大 `ramlimit` 假装完成模式体系。
- 对需要 v2 元数据的新能力明确拒绝，而非丢弃信息。

### 原生 ELF v2 路径

计划位置：

`lld/MCS251`

职责：

- 校验 AS/ABI/函数契约。
- 根据结构化分配类别实现 DSEG→EDATA。
- 校验物理范围、栈、初始化表。
- 实现控制转移松弛。
- 直接输出 HEX/map。
- 不运行或导入 Python 链接器完成生产 ELF 链接。

因此，本设计中完整模式体系以 ELF v2 为承载，不同时发明第二套 ASxxxx 模式 ABI。

---

# C. 代码模型、返回帧与链接松弛

## C.1 指令模式

CPU 固定为 Source/native：

- EJMP：`8A` 家族。
- ECALL：`9A` 家族。
- ERET：`AA`。
- AJMP/ACALL、LJMP/LCALL 使用 native 模式下对应编码。

不提供 CPU Source/Binary 用户选项；工具链、手写 CRT 与烧录配置必须满足同一 native 契约。

## C.2 代码模型选择矩阵

以下针对非 rel8 的无条件转移与直接调用；jcc 展开见 C.6。

| 代码模型 | 模块内跳转 | 模块外跳转 | 模块内调用 | 模块外调用 | 主要限制 |
|---|---|---|---|---|---|
| Small | AJMP | AJMP | ACALL | ACALL | 应用可执行区单 2K 页 |
| Medium | AJMP | LJMP | ACALL | LCALL | 本模块代码簇单 2K 页；应用单 64K bank |
| Compact | AJMP | AJMP，若存在则也需页可达 | LCALL | LCALL | 跳转闭包单 2K 页；应用单 64K bank |
| Large | LJMP | LJMP | LCALL | LCALL | 应用单 64K bank |
| Huge，默认 | EJMP | EJMP | ECALL | ECALL | 24 位及实际 ROM 范围 |
| Huge，可选模块内近 | 优先 LJMP | EJMP | ABI 允许时优先 LCALL | ECALL | 近转移范围及入口契约 |

说明：

- Compact 的“LCALL/AJMP”是调用/跳转分离，不是两种都可任意替换。
- Small 的 2K、Large 的 64K 都可以位于高地址 bank，不能误要求绝对地址小于 2048/65536。
- 默认 Huge 与用户最初描述的手册式 Huge 不再强求编码一致；这是已确认的现代扩展裁定。
- 显式严格模块内近配置可以要求必须使用近形式；无法满足时诊断，不静默提升代码模型。

## C.3 返回帧是函数 ABI

### 已有实证

`validation/mcs251-demo-test/t4-probes/ISR-STUB-VERDICT.md` 已通过对照实验确认：

- LCALL 压 2B 返回地址。
- RET 与其匹配。
- ECALL 压 3B 返回地址。
- ERET 与其匹配。
- LCALL 调当前 ERET 函数会在返回时破坏控制流。

因此，**ECALL→LCALL 不是普通的编码缩短**。

### 函数级契约

定义两种入口 ABI：

| 函数入口契约 | 合法调用 | 返回指令 |
|---|---|---|
| Near16 | ACALL、LCALL，以及经验证的近间接调用 | RET |
| Far24 | ECALL、远间接调用 | ERET |

要求：

- 返回帧在编译期决定。
- IR 函数定义、声明、call 和函数指针类型必须一致。
- 对外类型使用目标调用约定承载；仅用非类型化字符串属性不足以防止间接调用错配。
- 可补充数值目标属性供 MachineFunction/MC 使用。
- 对象中保留函数级 contract，链接器不反汇编最后一条指令猜 ABI。
- 链接器不得直接改函数 RET/ERET，因为序言、帧偏移、调试信息和所有调用者都依赖该契约。

对于默认 Huge：

- 默认所有函数 Far24。
- 可选近策略首先针对 local、非地址逃逸、定义与调用均可见的函数。
- 全局可调用入口默认保留 Far24，除非声明和定义明确约定 Near16 或生成双入口桥接。

### thunk

允许两类显式桥接入口：

1. **Far 入口桥接 Near 函数**：以 Far24 入场，LCALL Near body，最后 ERET。
2. **Near 入口桥接 Far 函数**：以 Near16 入场，ECALL Far body，最后 RET。

桥接必须：

- 独立命名并有自己的函数契约。
- 不改变函数地址的 canonical 身份。
- 不破坏参数与返回寄存器。
- 不重排静态参数槽。
- Near 桥与近调用者满足相应页/bank 条件。
- 包含桥内实际调用帧和额外栈深度的测试。

前者通常额外增加 2B 嵌套返回帧，后者增加 3B；这不是整函数最大栈深度证明，仍需合并 body 帧和中断开销。

首期默认禁用自动跨 ABI thunk；先明确拒绝。实现并验收后才开放。

## C.4 “模块内/外”的现代定义

模块身份为：

```text
输入对象身份 + 输入 section 身份
```

不是：

- 源文件名字符串。
- 最终输出 `.text` 名称。
- C 函数是否写了 `static`。
- 输出地址是否碰巧相邻。

判定原则：

1. 当前对象内的 STB_LOCAL/STT_SECTION 引用明确属于该对象。
2. 已定义 GLOBAL 若最终定义仍来自当前对象，也属于模块内。
3. 外部 GLOBAL 即使最终与调用者相邻，仍属于模块外。
4. 同名 local section 不会跨对象合并身份。
5. 引用 ABS/非函数符号时，必须按相应 relocation 类别校验，不伪造函数所有者。

首期不支持 weak、COMDAT、ICF、GC、archive 懒提取、LTO 合并语义时，保持明确拒绝。

未来支持这些能力时：

- 以符号解析完成后的有效定义为准。
- ICF 不得让符号归属策略失真。
- LTO 若要保留源模块边界，需独立来源元数据；否则只能声明以 LTO 输出对象为新模块。
- 不使用文件路径哈希作为 ABI。

## C.5 重定位编号扩展草案

### 0–8 保持不变

现有：

- NONE。
- 16。
- 24。
- LO8/MID8/HI8。
- PC8。
- J16。
- J11。

特别保留：

- `R_MCS251_16` 的低 16 位截取语义。
- J11 基于下一条指令所在 2K 页。
- J16 基于下一条指令所在 64K bank。

**普通 R16 不能用于证明 Tiny 指针可表示。**

### 新增编号

| 号 | 建议名称 | 含义 |
|---:|---|---|
| 9 | `R_MCS251_CALL_RELAX` | 带函数契约和候选策略的可变长直接调用 |
| 10 | `R_MCS251_JUMP_RELAX` | 带候选策略的可变长直接跳转 |
| 11 | `R_MCS251_PTR16` | 严格 00 段数据指针；禁止截 bank |
| 12 | `R_MCS251_PTR32` | BE32 canonical 数据地址；高字节为零 |
| 13 | `R_MCS251_DIRECT7` | direct RAM 地址，要求 `0..0x7f` |
| 14 | `R_MCS251_SFR8` | classic SFR 地址，要求 `0x80..0xff` |
| 15 | `R_MCS251_CALL_J16` | 固定 LCALL；额外验证 Near16 入口契约 |
| 16 | `R_MCS251_CALL_J11` | 固定 ACALL；额外验证 Near16 入口契约 |
| 17 | `R_MCS251_CALL24` | 固定 ECALL；额外验证 Far24 入口契约 |
| 18 | `R_MCS251_CODEPTR32` | 32 位 canonical 函数入口地址，校验函数指针契约 |
| 19 起 | 保留 | 后续扩展，不提前复用 |

v1 输入遇到新类型必须拒绝；v2 也不能因“知道编号”而接受尚未实现的行为。

### relax relocation 的位置与字段

- `r_offset` 指向可重写指令的 opcode。
- `S+A` 表示逻辑目标。
- 调用/跳转按最长候选形式预留空间。
- relocation 携带的目标不因选择短形式而被截断。
- side metadata 指定候选集合、优先级、逻辑转移种类及调用签名。
- side metadata 按输入 section 和 relocation 身份关联，不占用指令字段存策略。
- 指令区域禁止其他符号指向将被删除的内部字节。
- CALL_RELAX 不与 R24 对同一字段重复写入。

MC 必须保留供链接松弛使用的本地符号与 relocation，不能提前完全解掉后丢失重排信息。

## C.6 jcc rel8+skip 与长度变化

继续采用已验证的短条件跳过长转移结构，不首先引入通用 CFG 重写。

要求：

- skip 位移必须指向标签，不硬编码“跳过 4B EJMP”。
- 长转移缩短后，skip 位移随最终布局更新。
- 同一可松弛 section 内受布局影响的 PC8 保留 relocation，或由有明确边界的 MC fragment 机制等价维护。
- 不能只更新目标符号地址，忘记已经汇编好的 rel8。
- 保持当前禁用不安全 branch folding/tail merge 的保护，直到完成 branch analysis 与真实 relaxation 接口。

间接跳转、jump-table lowering 当前支持域有限，单列阶段实现，不因本设计出现表项就宣称已经支持。

## C.7 链接松弛算法

必须处理绝对页边界：**总尺寸变小并不保证先前选中的 J11/J16 永远有效。**

建议流程：

1. 解析符号、对象身份、函数契约。
2. 建立可达范围和固定 ROM 预留。
3. 按保守大小预放置，暂不因保守大小超出小模型最终容量就提前拒绝。
4. 在当前布局下评估允许候选。
5. 重建布局、符号、relocation 位置及受影响 addend。
6. 重复至稳定。
7. 对失效的已缩短项提升到更宽且合法候选，并在本轮固定；设置有界迭代和重复状态检测。
8. 严格短模型没有更宽候选时，尝试规定的页/bank packing；仍失败则报错。
9. 对最终布局重新执行全部范围、ABI、PC8、ROM 和表校验。
10. 成功后才生成固件。

对 STT_SECTION+A：

- A 若指向输入 section 内的代码位置，缩短后必须通过“输入偏移→输出偏移”映射求地址。
- 不能仅移动 section 基址而保留旧 A。
- symbol size、函数边界及可接受的调试范围同样需要更新。
- 首期未实现的调试/复杂符号差元数据继续拒绝。

## C.8 2K、64K 和 24 位门禁

### MC 层

负责：

- opcode、字段形状、已知绝对立即数合法性。
- 可确定的 PC8 溢出。
- 有固定地址证据时的页/bank 越界。
- 非法候选组合、缺失函数契约等对象生产错误。

同一输入 section 并不保证已知最终 2K 页，不能在 MC 中假定基址为零完成最终 J11 校验。

### 链接器层

最终负责：

- J11：目标页等于**下一条指令**页。
- J16：目标 bank 等于**下一条指令** bank。
- 24 位目标 `0..0xffffff`。
- Small 全应用可执行分配位于同一 2K 页。
- Medium 模块内代码簇及 Compact 跳转闭包满足 2K 条件。
- Medium/Compact/Large 全应用可执行区满足规定的 64K bank 约束。
- 函数入口返回帧与调用形式一致。
- 最终物理 ROM 范围。

硬件固定向量槽可作为明确的 fixed-encoding 区例外；不能把任意超限代码标成“系统区”逃避模型限制。默认 CRT 不自动满足 Small，需对应构建资产。

错误应包含：对象、section、符号、调用位置、下一 PC、目标地址、候选形式、所需页/bank、函数帧契约。

## C.9 单函数、单文件与单表 64K

### Huge 的函数与文件

flat-24 Huge 不设置 Keil 的单函数/单文件 64K 模型限制。

但“体系允许”不等于当前所有后端细节已经大规模验证：

- 大函数 jcc skip。
- 大 section 符号与 addend。
- 固定 vector 洞。
- MC fragment。
- frame offset 的独立范围限制。

这些均需边界验收。

### 单表必须单列

区分：

1. **16 位表访问路线**：如固定 bank 的 DPTR+MOVC。
2. **24 位表访问路线**：完整 CODE 指针和能传播 bank 进位的指令/循环。

16 位路线要求：

- 表及其全部可访问字节不得跨所属 64K bank。
- 仅有 `size<=65536` 不够；起点偏移也必须满足范围。
- 超限时明确报错，不截断表索引。

大于 64K 表的推荐出路：

- 使用 AS4、32 位指针表示。
- 用 32 位索引/GEP。
- 采用经 QEMU、真机验证的 24 位 CODE 读取。
- 必须覆盖跨 bank 的逐字节、word 与非对齐读取。

若硬件指令路线尚未证实：

- 允许后续开发显式分块表格式。
- 分块会改变表接口，不能偷偷替代一个连续 C 数组。
- 首期明确拒绝超 64K 表，诊断指向所选访问策略，而非错误归咎于 Huge。

跳转表另需：

- 表项保存完整目标或受控相对偏移。
- 24 位间接跳转。
- 范围检查与符号重排支持。
- 在实现前继续保持明确“不支持”。

## C.10 ROM 区与 Code Banking

建议增加：

- `FLASH_END`。
- 必要时 `FLASH_SIZE`，与 end 相互校验。
- 链接器可重复的数值 CODE 区间及排除区参数。
- 可选 image 字节数/逻辑跨度预算。

示例：

```text
FLASH_BASE = 0xFE0000
FLASH_SIZE = 0x20000
FLASH_END  = 0xFFFFFF
```

计算必须使用宽整数：

```text
FLASH_END = FLASH_BASE + FLASH_SIZE - 1
```

门禁检查的不只是有效载荷字节数，还包括：

- HOME/VECS/BOOT/CSEG/常量/XINIT。
- NOBITS CODE 保留洞。
- 固定地址保留区。
- 所有 section 的完整分配区间。
- 加法溢出与重复地址。

`CSEG_BASE`、`XINIT_BASE` 只是起点，不是容量证明。

**Code Banking 不支持且不需要。**

理由：

- MCS251 原生控制转移可携带完整 24 位地址。
- 64K bank 在这里是近指令的范围约束，不是软件窗口切换模型。
- 引入银行寄存器切换会改变调用、函数指针、中断和重入语义，没有必要。
- 构建层对 Code Banking 请求立即报不支持。
- 不提供静默忽略的兼容开关。

---

# D. Tiny/XTiny 的 2B 指针 ABI

## D.1 数据指针与函数指针分离

Tiny/XTiny：

- 普通数据指针 AS0 为 2B。
- 显式 far/xdata/code 指针仍为 4B。
- 默认函数指针采用 AS4 的 4B canonical CODE 地址。

这是为现代正交模型作出的明确裁定：

**Tiny + Huge 不得把位于 `FF:xxxx` 的函数地址截成 00 段。**

因此，手册中的“默认指针 2B”在本 fork 中适用于普通数据指针；不是承诺所有指针类型均 2B。

Near16/Far24 函数 ABI 决定返回帧，不决定函数指针对象宽度。即便函数返回使用 RET，其函数地址仍可保存在 4B CODE 指针中。

近间接调用若无法证明/检查当前 bank 与目标 bank 一致，必须拒绝或使用显式桥接，不能直接丢弃高字节。

## D.2 参数与返回寄存器

| 值类型 | 首参数 | 返回值 | 备注 |
|---|---|---|---|
| i8 | DPL | DPL | 不变 |
| i16 | DPL/DPH | DPL/DPH | DPL 低字节 |
| i32 | DPL/DPH/B/A | DPL/DPH/B/A | 不变 |
| Near 数据指针 16 | DPL/DPH | DPL/DPH | 不读取 B/A 作为指针部分 |
| Far 数据指针 32 | DPL/DPH/B/A | DPL/DPH/B/A | A 发送为零 |
| CODE 指针 32 | DPL/DPH/B/A | DPL/DPH/B/A | A 发送为零，保留 bank |

内存表示均大端：

- 2B：高字节、低字节。
- 4B：`00、bank、high、low`。

寄存器槽按低有效字节优先排列，与内存字节序不同，不得直接照槽顺序存储。

Near 指针不要求清零 B/A；其值未指定，callee 不得依赖。Far 接收端继续做 canonicalization，避免高字节垃圾参与比较。

## D.3 i16 指针算术

Near AS 的 pointer index width 为 16：

- GEP 地址形成按该 AS 的 16 位规则。
- 原始非 inbounds IR 地址计算允许相应模运算；不把硬件回绕解释成合法 C 越界。
- C 指针加减仍受对象范围、one-past、有效性约束。
- `inbounds` 的 poison/UB 语义不能被 lowering 改成无条件回绕。
- 负偏移、scaled GEP、`0x7fff/0x8000`、`0xffff` 边界必须测试。
- 首期大位移可先物化成 WR 加法，再用无位移间接形式，避免猜测 `@WR+dis16` 与 `@DR+dis16` 相同。

推荐保留：

- `size_t=unsigned long`，32 位。
- `ptrdiff_t=signed long`，32 位。
- `intptr_t/uintptr_t` 32 位。

这不是要求 Near 地址形成变成 32 位，而是保持现代整数接口和足够的结果表示范围。

特别是宽 `ptrdiff_t` 不能通过“先截成 i16 差值、再符号扩展”错误实现；应在可证明属于同一对象的指针差计算中保留正确的地址差。

Near 对象最大可分配连续长度及 one-past 表示边界必须单独规范。首期拒绝需要跨 `0x10000` 的对象，也不默认接受覆盖整个 64K 并绕回空指针的对象。

## D.4 AS 转换与 2B/4B 共存

允许：

- 同一 v2 模块中同时存在 AS0 near、AS8 edata、AS3 xdata、AS4 code、AS9 far。
- Near→Far 数据地址零扩展。
- AS1/AS2/AS8→可承载其物理 RAM 地址的 generic，保留别名关系。
- 确定可表示的常量 Far→Near 显式转换。

禁止或诊断：

- 隐式 Far→Near。
- 动态 Far→Near 后直接截断再解引用。
- SFR→generic 丢失访问空间。
- 函数指针与数据指针隐式混用。
- Near16/Far24 函数指针调用约定强转后直接调用。

动态收窄建议提供明确的 checked conversion 接口，返回成功/失败；未经范围检查不生成可解引用 Near 指针。

空指针与实际物理地址零的访问需沿用 LLVM null 语义。访问地址零的特殊底层代码必须用受控绝对对象/目标契约，不能借普通 null 解引用建立硬件语义。

## D.5 多参数、静态槽与混链

当前多参数 ABI 使用命名静态槽，后续指针参数被拒绝。本模式体系不顺带将其改为栈传参，但必须给出闭环：

- v2 第二及后续 Near 指针槽占 2B。
- Far/CODE 指针槽占 4B。
- 使用目标端序、对齐 1B。
- 参数槽宽度由完整函数签名决定，不能由 callee 名字符串猜。
- leaf OSEG 与 non-leaf DSEG 的现有区别保留。
- overlay 的重入、递归、中断限制保留并诊断；2B 指针不修复这一 ABI 限制。
- 首期多参数间接调用仍拒绝，直到静态槽寻址有可验证方案。

混链规则：

- v1 与 v2 默认拒绝裸混链。
- 默认指针 16/32 的 v2 对象默认拒绝混链，即使某个外部接口看起来只有整数。
- 具备统一 AS 布局、相同 ABI 指纹的 Small/XSmall/Large 可按每对象显式放置信息混链。
- Tiny 与 XTiny 同理。
- 不允许 LTO 合并不同默认指针 DataLayout。
- 需要跨默认 ABI 的工程用显式、经审计桥接，不提供忽略 note 的开关。
- 显式 far pointer 在 near 模块中使用，是首选共存方式，不必为此混合两个默认 ABI。

## D.6 QEMU 与真机探针清单

所有探针须保存输入、工具指纹、对象 relocation、最终 map/HEX、原始串口、退出码和判定。源码阅读不是运行证据。

| 编号 | 探针 | 必须区分的结果 |
|---|---|---|
| P01 | `@WR` byte load/store | 00 段寻址；不读写 SFR |
| P02 | `@WR` word load/store | 大端、非对齐、跨 byte 边界 |
| P03 | `@WR+dis16` 四类形式 | 正负位移、编码、有无符号扩展、地址回绕 |
| P04 | `0x7f/0x80/0xff/0x100` RAM 与 SFR 同值对照 | AS6 direct 与 AS8 indirect 不混淆 |
| P05 | Near 首参/返回 | DPL/DPH 正确；B/A 污染不影响指针 |
| P06 | Far 首参/返回 | bank 保留；高字节归零；memory 4B |
| P07 | i16 GEP | 正负、缩放、跨 `0x7fff`；有效对象内结果 |
| P08 | i16 alloca 地址、spill/reload | 与 DR60/SPX 兼容；不截错栈地址 |
| P09 | AJMP/ACALL 11 位 | opcode 高地址位；下一 PC 的 2K 页；页末两侧 |
| P10 | LJMP/LCALL 16 位 | bank 保持、64K 边界、近间接形式 |
| P11 | ACALL/LCALL/RET 与 ECALL/ERET | SPX 增减、返回地址字节、嵌套调用 |
| P12 | 两种 thunk | 参数、返回、PSW、栈哨兵、递归禁区 |
| P13 | 故意 LCALL→ERET 负例 | 重现既有失败，不将错误配对作为可支持路径 |
| P14 | 松弛后的 jcc skip | 最终分支正确，页边界重排后仍正确 |
| P15 | CODE 24 位连续读取 | 不同可读 ROM 区、跨 64K、表索引超过 65535 |
| P16 | XDATA 初始化与访问 | 24 位目的、完整清零/复制、无 EDATA 栈计入 |
| P17 | 5091B 全局 | 4KiB 拒绝、16KiB 正向、strict data 拒绝 |
| P18 | ROM 越界/洞/预留冲突 | 链接失败且不产新固件 |
| P19 | pointer initializer | 2B/4B、非零 addend、Far→Near 溢出 |
| P20 | 静态指针参数槽 | 2B/4B 宽度、跨模块、overlay 交叠 |
| P21 | 函数指针 | CODE bank、Near/Far 类型契约、错误混配拒绝 |

不可用物理 RAM 地址的边界测试应分开：

- linker/MC 合成测试覆盖完整架构范围。
- QEMU 只对已建模地址做内存行为正向断言。
- 真机只在板上实际存在、允许访问的区域运行。

G12 当前使用 G144 QEMU 替代模型的结果，不等于 G12 物理 RAM/Flash 容量实证。

---

# E. 构建层接线与零型号/模式字符串红线

## E.1 配置来源

板文件保持物理能力来源：

- `validation/mcs251-demo-modern/boards/stc32g12k128.mk`
- `validation/mcs251-demo-modern/boards/stc32g144k246.mk`

建议分离：

1. **板能力**：Flash/EDATA/XDATA 范围、时钟、外设数值、QEMU 运行机器。
2. **程序模式**：默认指针、对象放置、代码候选、ABI 版本。
3. **布局配置**：CSEG/XINIT/BOOT 位置、预留区、页/bank 预算。

板可以给模式推荐值，但不是“某型号必须使用某模式”的生产工具规则。

## E.2 建议构建变量

| 分类 | 变量 |
|---|---|
| 模式 | `MEMORY_MODEL`、`CODE_MODEL`、`MODEL_CONTRACT` |
| 近转移策略 | 构建层的可读选择，降为候选掩码与偏好 |
| Flash | `FLASH_BASE`、`FLASH_END`，可选 `FLASH_SIZE` |
| CODE 布局 | `CSEG_BASE`、`XINIT_BASE`、BOOT/向量范围 |
| 内部 RAM | `EDATA_END`、明确低 RAM 预留 |
| 外部 RAM | `XDATA_BASE`、`XDATA_END` 或数值区域列表 |
| 栈 | 最低容量 1024、guard、可选更严格预算 |
| 对象 | `OBJECT_FORMAT`，v2 固定 ELF |
| 运行 | `QEMU_MACHINE`、串口、时限、真机烧录配置 |

`FLASH_BASE` 必须真正参与范围检查，不只生成头文件宏。

不能从 G144 的 XDATA 容量推导 G12 的容量。缺失板能力时，新模型构建失败，不能使用大板默认值兜底。

## E.3 Clang、llc、CRT、link 的贯通

现有双阶段链必须向两端传同一契约：

- CFLAGS：前端类型、AS、整数模型。
- LLCFLAGS：相同目标契约及对象格式。
- CRT：初始化协议、入口返回帧、native ISA。
- Link 参数：物理范围、ABI 版本、放置协议、栈门禁。
- Host 构建：不接受目标编译 flag；必要时用显式测试适配，不假定 host pointer 与目标同宽。

只给 CFLAGS 加模式、llc 仍使用默认 DataLayout 是错误接线，应由自动测试捕获。

## E.4 配置指纹与重建

当前 board stamp 不足以防止模式切换后复用旧对象。

应生成一个规范化配置指纹，至少包括：

- 模式降解后的全部数值字段。
- Clang/llc/链接器工具身份。
- 对象 ABI、AS、初始化协议版本。
- ROM/RAM 范围与各 region 起点。
- CRT 资产版本。
- 当前函数返回帧策略。

C、IR、对象、CRT、链接命令及最终固件均依赖该指纹。

推荐输出目录隔离：

- 按 board、契约版本、memory/code 组合或其配置摘要分目录。
- 修改模式必须触发 IR 重编译，不只重链接。
- 修改布局可只重链接的前提是 ABI/IR 契约未变。

## E.5 生产代码红线

禁止在编译器与链接器语义实现中出现：

- 器件型号判断。
- `if model == "xsmall"` 一类字符串分派。
- 根据板名硬编码 EDATA/Flash 大小。
- 根据函数名或文件名选择 opcode。
- 自动读取某块板的 Makefile。
- 因运行在某 QEMU machine 而改变目标语义。

允许：

- 目标 AS 枚举。
- 指针宽度和返回帧枚举。
- 重定位编号。
- 数值区域。
- 明确对象协议版本。
- 稳定的 ABI section 名。
- 已冻结的 legacy 签名仅留在兼容路径，不新增其依赖。

应增加 CI 审查：

- 生产源码不得新增板名。
- 模式词汇映射只允许出现在 validation 构建层与文档。
- 后端策略测试使用数值输入。
- 链接器同一数值配置对不同输入板名没有可观察分支。

---

# F. 分阶段实施、验收与派工

## F.1 实施依赖

推荐顺序：

**契约冻结 → 指令实证 → AS/EDATA → Near ABI → 代码模型与松弛 → 大表/完整矩阵**

原生 ELF 链接器基础能力是关键前置项。不能在只有 E2 对象输出时宣告 DSEG→EDATA 或链接松弛闭环完成。

## F.2 阶段计划

### 阶段 0：冻结契约与兼容基线

**改动面**

- 冻结 AS 编号、DataLayout、数值配置、对象 v2 note。
- 冻结函数级 Near16/Far24 契约与新 relocation 编号。
- 明确旧 ELF 规范与本稿的版本边界。
- 建立默认构建黄金指纹。

**对象 v2 note 建议**

维持 ELF32/MSB/RELA、相同实验性 machine；`e_flags` 对象版本升级为 2。必需 note 扩为结构化数值字段，至少包含：

- 对象协议版本。
- 调用 ABI major/minor。
- 寄存器参数变体、寄存器集合。
- int/long 宽度。
- AS0 指针宽度。
- AS 布局版本。
- 默认对象放置类别。
- 初始化协议版本。
- 放置协议版本。
- 栈契约版本。
- 必需能力位及保留零字段。

代码模型不是单一 ABI 身份字段；调用/跳转候选应保存在函数或 site 契约中。兼容字段逐项比对，不能对包含可合法不同放置策略的整个 descriptor 简单 `memcmp`。

**验收**

- 默认 REL/asm/ELF v1 与既有黄金字节一致。
- 现有 lit 全通过。
- 既有 QEMU/真机固件产物指纹不变。
- ABI 不匹配、保留位、缺 note 等负例有明确诊断。

**派工**

- Alice：协议、兼容边界、审核。
- Shizuku：基线脚本与配置指纹。
- Sakuna：冻结工具和运行证据。

### 阶段 1：硬件语义探针

**改动面**

- 只新增独立 probe 与测量 harness。
- 不先把未证实语义写进生产 ISel。

**验收**

- 完成 P01–P16 中阻塞 ABI/编码的核心探针。
- QEMU 记录 SPX、寄存器哨兵和完整串口。
- 真机复测 ACALL/LCALL/RET、ECALL/ERET、WR 与 CODE 跨 bank 关键子集。
- QEMU/真机不一致逐项裁定，不以 QEMU 为唯一 ISA 真值。

**派工**

- Sakuna：主测量。
- Alice：设计反例、解释边界。
- Shizuku：可重复构建与结果校验。

### 阶段 2：AS 与 EDATA 放置闭环

**改动面**

- Clang AS 映射与 Sema。
- DataLayout 共享描述。
- AS 驱动 load/store。
- section 分类、对象能力元数据。
- 原生链接器 v2 内部 RAM 分配。
- XINIT v2 内部 RAM 子集与 CRT。

主要落点：

- `clang/lib/Basic/Targets/MCS251.h`
- `clang/lib/Basic/Targets/MCS251.cpp`
- `llvm/lib/TargetParser/TargetDataLayout.cpp`
- `llvm/lib/Target/MCS251/MCS251TargetMachine.cpp`
- `llvm/lib/Target/MCS251/MCS251ISelLowering.cpp`
- `llvm/lib/Target/MCS251/MCS251TargetObjectFile.cpp`
- `llvm/lib/Target/MCS251/MCS251AsmPrinter.cpp`
- `lld/MCS251`

**验收**

- 独立 Clang/LLVM lit 检查 AS、指针宽度、load/store 路线。
- 独立 yaml2obj 链接测试，不依赖 llc 掩盖 linker bug。
- direct/edata/SFR 同值不同语义。
- 5091B 三分支验收。
- 栈刚好 1024B 与少 1B 边界。
- 非零初始化、BSS、聚合、洞、绝对预留。
- QEMU 与有相应 RAM 的真机运行验证。
- 4KiB 板不以越界固件做正向运行验收。

**派工**

- Shizuku：前端接线、section、构建、常规测试。
- Alice：地址分类、别名规则、分配算法与协议闭环。
- Sakuna：edata/SFR/初始化运行证据。

### 阶段 3：Tiny/XTiny Near 指针 ABI

**改动面**

- i16 pointer legalization、寄存器类选择。
- GEP、frame pointer materialization、spill/reload。
- Near/Far 参数与返回。
- strict PTR16、pointer initializer。
- v2 后续静态指针参数槽。
- AS cast。

特别注意当前 `getPointerRegClass()` 固定返回 GPR32，调用目标也使用默认 iPTR；必须同步改造，不能只替换 DataLayout。

**验收**

- O0/O2、寄存器压力、跨函数、跨模块。
- 2B/4B 聚合字段布局和初始化。
- B/A 污染对 Near 指针无影响。
- 超范围 PTR16 relocation 拒绝。
- 数据指针 near + Huge CODE pointer 的组合运行。
- 混 ABI 对象链接拒绝。
- lit、QEMU、真机分别记录通过范围。

**派工**

- Alice：legalizer、寄存器别名、调用 lowering 攻坚。
- Shizuku：类型/布局与负例。
- Sakuna：寄存器和栈探针。

### 阶段 4：代码模型、函数返回帧与松弛

**改动面**

- 函数级 Near16/Far24 calling convention。
- RET 指令和返回 lowering。
- AJMP/ACALL/LJMP/LCALL MC 发射。
- 函数 contract 与 relax metadata。
- 原生链接器迭代布局。
- PC8 与 section-addend 重映射。
- thunk 后置子阶段。

**验收**

- 五种代码模型独立 opcode gold。
- Small 的 2K、Large 的 64K 正反边界。
- 同对象 GLOBAL/local、跨对象、同名 section。
- 页末下一 PC 行为。
- 先缩短后失效的迭代案例。
- RET/ERET 错配在链接前或链接时拒绝。
- thunk 栈平衡、参数返回值、不破坏 canonical 函数地址。
- 不以 ECALL 到达 callee 就算成功，必须验证返回后的哨兵和 SPX。
- 默认 Huge 产物字节不变。

**派工**

- Alice：函数 ABI、松弛和 thunk 主实现/审核。
- Shizuku：MC 接线、readobj/YAML、常规 lit。
- Sakuna：页/bank、调用帧与真机执行。

### 阶段 5：Large 数据、24 位大表与工程矩阵

**改动面**

- XDATA 默认对象。
- XINIT v2 完整 32 位计数与 24 位地址循环。
- 24 位 CODE 表读取。
- 跳转表作为独立子项，未完成则继续明确拒绝。
- Flash/预留区门禁。
- 全部 board/model 构建接线。

**验收**

- 超 64K 程序且跨 bank 调用运行。
- 超 64K 单表通过 24 位访问读取首、边界、尾部。
- 16 位表策略下同样输入明确拒绝。
- 大对象初始化不截 destination、size、循环计数。
- 物理 ROM 越界、hole 越界、XINIT 冲突均拒绝输出新固件。
- 五存储模型 × 五代码模型共 25 组合均有契约和对象测试。
- 能放入实际板范围的组合运行；超容量组合以正确拒绝验收。
- QEMU、G12 真机、G144 真机覆盖单独列示，未拥有的板不记通过。

**派工**

- Shizuku：构建矩阵与普通工程集成。
- Alice：大表、地址传播、最终规范一致性。
- Sakuna：资源测量、QEMU 与真机矩阵。

## F.3 测试目录建议

- `clang/test/Driver`
- `clang/test/CodeGen`
- `clang/test/Sema`
- `llvm/test/CodeGen/MCS251`
- `llvm/unittests/TargetParser`
- `lld/test/MCS251`
- `validation/mcs251-models`

WSL 构建继续使用 `/home/liu/build-mcs251`。共享构建树配置变更须报节点；测量与生成物使用独立绝对路径沙盒，不覆盖其他战役证据。

## F.4 最终验收门槛

完成模式体系必须同时满足：

1. 默认兼容路径字节和行为不变。
2. 每个模型有固定 CLI 降解、DataLayout、对象契约和范围规则。
3. named AS 从 C 类型到机器访存再到 area 全链一致。
4. DSEG→EDATA、XINIT、SPX 与物理 RAM 上界闭环。
5. Tiny/XTiny 的 2B 数据指针不是“IR 变窄、后端仍按 4B 偷跑”。
6. 函数级 Near/Far ABI 受编译器与链接器双重保护。
7. 链接松弛更新全部受影响地址，不只改 opcode。
8. Flash 实际范围受链接门禁约束。
9. 大表支持与限制有独立声明和边界测试。
10. 无型号/模式字符串进入生产语义分派。
11. 所有失败链接不生成或覆盖新的固件。
12. QEMU、真机、纯静态边界测试覆盖分别报告，不互相冒充。

---

## 收口结论

本方案将“模式”拆成五类可验证契约：

**数据指针布局、对象存储空间、函数返回帧、控制转移候选、物理资源范围。**

由此可以同时做到：

- 保持当前 **XSmall 命名 + flat-24 Huge 默认**的既有字节。
- 支持 Tiny/XTiny 的真实 2B 数据指针。
- 将普通 DSEG 对象安全延伸到 edata，而不破坏显式 direct 对象。
- 在函数 ABI 正确的前提下实现模块内/外链接松弛。
- 去掉 Keil 的非必要函数/文件 64K 限制，同时诚实约束 16 位表访问。
- 将板差异、模式名称与 ROM/RAM 配置留在构建层，不把工具链变成器件分支集合。
