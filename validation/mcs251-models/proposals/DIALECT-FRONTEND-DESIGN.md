# MCS-251 方言前端核心能力设计

**作者**：Alice，首席设计／审核工程师
**日期**：2026-09-07
**状态**：设计终稿；本文明确的架构方向及收紧边界已获 PM 确认，具体能力位、对象协议补充项及实现仍须分阶段验收。
**落盘**：PM 亲核抽查（S01 AVR.cpp:577-587 宏映射、S06 lowering 零 AS 消费）后落盘。

**范围**：存储区限定、地址空间类型与 ABI、全局放置、按空间选择访存指令、位访问及其原子性。

**明确排除**：

- SFR 声明语法。
- Keil／SDCC 头文件解析、转换及兼容。
- 官方寄存器名、位名全集。
- 中断关键字、ISR 序言／尾声及优先级系统的重复设计。
- pdata 首期实现。
- 实现代码、构建、提交及设计文件写入；本轮仅只读核证与设计。

---

## 1. 总体裁定

### 1.1 核心结论

1. **存储区限定采用现成 `address_space(N)` 属性，不新增词法关键字或声明语法。**
   方言词只由可选头文件宏映射；编译器语义层消费 AS 数字和结构化能力，不检查 `data`、`xdata` 等关键字字符串。

2. **不新增 MCS251 专属 LangAS 枚举。** AVR 的真实先例本来就是宏直接映射数值属性，而非 `LangAS::avr_flash`。严格类型检查和良好诊断应通过目标语义检查实现，不依赖新增 LangAS 名称。[S01–S03]

3. **每-AS 指针宽度保持 DESIGN.md B.2/B.3，不复制 Keil 1B／2B／3B 指针 ABI。**
   仅 AS0 随存储模型取 16／32 位；AS1/2/6/8 固定 16 位，AS3/4/7/9 固定 32 位。AS5 无普通指针 ABI。[D01，设计 ABI]

4. **第一阶段首先封堵 CODE store 穿透，再封堵全部未实现的非零 AS 访问。**
   当前 lowering 不消费地址空间，非零 AS 固定低地址可能进入 direct SFR 路线；CODE store 也没有空间级拒绝。[S06]

5. **L1 采用"受控位左值＋强制语义 lowering"，不能只用 `volatile _Bool` 加 AS5 后等待优化器猜中。**
   位左值的类型表示"位访问语义"；地址常量由受控表达式和 intrinsic 操作数携带。普通 AS5 指针、普通 AS5 load/store 继续禁止。

6. **L2 保持 AVR 式 SelectionDAG 惯用法折叠，但不是原子性承诺接口。**
   OR／AND 参考 AVR；XOR→CPL 是本项目扩展，须独立探针验证。折叠不成立时保留普通非原子 RMW，不包装成"原子成功"。[S04，设计扩展／待探针]

7. **L3 临界区必须保存并恢复原 EA。**
   CLR EA、SETB EA 各自为候选单指令操作；二者组成的区域不是天然原子，不得无条件 SETB，不保护 NMI／DMA，不解除中断设计 v2 的静态槽、SPX 与调用闭包限制。[D02，设计／待探针]

### 1.2 证据等级

本文使用以下标记：

| 标记 | 含义 |
|---|---|
| **[S] 源码核证** | 已亲读源码，确认当前代码结构或上游先例；不是运行 PASS |
| **[W] 工作树候选** | 未提交或未跟踪的工作树内容；不代表 HEAD 或生产工具能力 |
| **[Q] QEMU 已验** | 已有 QEMU 探针记录，仅覆盖明确样本 |
| **[H] 真机已验** | 已有真机记录；与 QEMU 证据分别记账 |
| **[D] 设计契约** | 已有设计裁定或本方案规定；不是实现完成证明 |
| **[P] 待探针** | 不得在获得证据前作为生产指令选择依据 |

下文表格中的 ABI 规则默认属于 **[D]**；涉及机器指令时另列 [Q]/[H]/[P]。

---

## 2. 源码亲核结论与证据身份

### 2.1 实际仓库与基线

任务给定的 `/home/liu/llvm-project-fork` 在本次环境不存在。实际核证仓库为：

- Windows：`C:/Prj/LLVM/MCS251`
- WSL：`/mnt/c/Prj/LLVM/MCS251`

宿主 git 亲核 HEAD 为：

`eb1696fe6b09da0f0d71c0a9a5c671df34a05232`

工作树存在在飞修改，**不能将工作树文件都视为该 HEAD 的内容**。本稿对相关候选文件单独标记 [W]。

用户提供的 CodeGen/MCS251 **68/68** 是本设计的回归基线；本轮没有重跑构建或测试，不将其写成本轮实测结果。

### 2.2 AVR 的真实前端链

已亲核如下链路：

> 宏展开为 `address_space(N)`
> → Sema 构造 target-specific LangAS 值
> → `TargetInfo::getTargetAddressSpace` 还原 N
> → `GetGlobalVarAddressSpace` 保留声明的 AS
> → LLVM global／pointer 带 AS
> → AVR 后端按 AS 选择访问和 section。

关键事实：

- `AVR.cpp:576–587` 将 `__flash` 至 `__flash5` 定义为 `address_space(1..6)`。
- **上游宏位于目标预定义宏代码，不是头文件。** 本项目遵循用户裁定，将同类别名放在可选头文件层；借鉴的是属性与 AS 语义链，不照搬宏所在文件。[S01]
- `AddressSpaces.h` 中没有 AVR 专属 LangAS 枚举。[S02]
- 通用 `TargetCodeGenInfo::getGlobalVarAddressSpace` 返回声明类型上的 AS；无需通过改名或 section 字符串恢复地址空间。[S03]

### 2.3 AVR 位折叠的真实层次

AVR 的 SBI／CBI 先例位于 **TableGen 生成的 SelectionDAG 指令匹配**：

- `SBIAb`：`store(or(i8(load address), mask), address)`。
- `CBIAb`：`store(and(i8(load address), mask), address)`。
- 地址减去 subtarget I/O 偏置后必须小于 `0x20`。
- OR 掩码的低 8 位必须为单 bit。
- AND 掩码的低 8 位取反后必须为单 bit。
- 位号由掩码的 log2 得到。
- `io.ll` 包含 volatile CBI 用例。[S04]

**不能据此声称**：

- AVR 在 Clang 中把普通 `|=` 定义成语言级原子操作。
- 存在独立 AVR LLVM IR 原子化 pass。
- 任意 volatile RMW 都能安全折叠。
- 此处存在 XOR→toggle 的上游先例。
- AVR 的具体外围寄存器访问语义可以直接移植到 MCS251。

AVR 程序存储器 load 则在 DAG selector 中识别 program-memory 访问，检查 LPM／ELPM 能力后选择相应指令；地址模式也有程序存储器限制。[S05]

### 2.4 MCS251 静默错译的具体机制

当前 `MCS251ISelLowering.cpp` 中：

1. `LowerLoad`／`LowerStore` 检查 atomic 与访存宽度，**不检查访问 AS**。
2. 调用 `parseAddress` 时只传基址和 `AllowDirect`。
3. 对 i8 访问，绝对地址值 `<=0xff` 即标记为 direct。
4. direct 路径选 `MOV8di`／`MOV8id`。
5. 其他普通地址走 `MOV8rmP`／`MOV8mrP`，其地址操作数要求 GPR32。
6. 符号物化和部分地址算术固定为 i32。
7. 原始 MMO 虽被附回机器节点，**AS 信息保留在 MMO 中不等于选指令时消费了它**。[S06]

因此准确结论是：

> 当前地址分类器在语义上忽略了 AS，复用了历史 AS0 数值分类规则；它并不一定先在 IR 中显式把指针 cast 成 AS0。

下面是首阶段负测必须覆盖的核心形态，属于**待执行负例设计**，不是本轮运行结果：

```llvm
define void @reject_code_store() {
  store volatile i8 1,
    ptr addrspace(4) inttoptr (i32 128 to ptr addrspace(4))
  ret void
}
```

此形态没有非零 AS global definition，可绕开 AsmPrinter 的全局定义拒绝；现有地址分类会把 `0x80` 认作 direct byte 地址，而不是拒绝 CODE 写入。[S06–S07]

同样：

- AS3／AS4 的 `0x80` 访问可能误选 direct SFR。
- AS2／AS8 的 `0x80` 本应保持内部间接 RAM 语义，不能误触同数值 SFR。
- 动态 16 位 named-AS 指针还可能撞上固定 GPR32 操作数要求，表现为失败或错误代码；**不宣称所有此类输入均以同一种方式静默错译**。

### 2.5 工作树候选不能当成现有安全网

本轮还发现：

- `MCS251ContractVerifier.cpp` 是未受 HEAD 跟踪的候选文件；其指针白名单放行 AS1/2/3/4/6/7/8/9，只拒 AS5 与未知编号。
- 当前所读 llc／BackendUtil／TargetMachine 中未发现该 verifier 的接入调用，不能视为已生效。
- 候选检查仅遍历部分类型和值，不能代替递归检查 initializer、ConstantExpr、alias／ifunc 等完整目标契约。
- 工作树 `MCS251TargetInfo::getPointerAlignV` 返回指针宽度，导致 named-AS 对齐查询得到 16／32 位，而契约要求 8 位。
- 当前所读 llc 的布局回调仍接受 `OldDLStr` 后返回 TM 布局，须按既有契约核验是否存在覆盖原始布局的入口漏洞。[W01，S10]

以上分别是**工作树候选缺口或实际读取到的入口状态**，不归咎为 eb1696fe6 已提交了这些候选实现。

---

## 3. 宏映射与 LangAS 裁定

### 3.1 宏映射表

所有别名只属头文件／文档层，是否提供裸词别名由未来兼容包装决定；核心编译器测试可直接使用属性，不依赖任何头文件。

| 方言概念／可选别名 | 映射目标 | 首期裁定 |
|---|---|---|
| `data`／`__data` | `__attribute__((address_space(1)))` | strict direct RAM 类型 |
| `idata`／`__idata` | `__attribute__((address_space(2)))` | 内部间接 RAM 类型 |
| `xdata`／`__xdata` | `__attribute__((address_space(3)))` | 完整 24 位有效外部数据地址，4B 指针容器 |
| `code`／`__code` | `__attribute__((address_space(4)))` | 只读 CODE 数据；写入禁止 |
| `edata`／`__edata` | `__attribute__((address_space(8)))` | 00 段内部数据，固定 2B 指针 |
| `pdata`／`__pdata` | **无有效 AS 映射** | 首期不支持，不分配新号，不假映 AS3 |
| `sbit` 概念 | §7 的受控 AS5 位左值方案 | 不映为普通 byte／`_Bool` global |
| `far`／`__far`，补充项 | `__attribute__((address_space(9)))` | canonical 24 位 generic 数据地址 |

AS6／AS7 保持 DESIGN.md 的保留语义，不因本设计重新分配，也不设计其声明格式。

**pdata 的重要边界**：

- AS1–AS9 中不存在 pdata。
- 不得将 pdata 映为 xdata，再隐含引入页寄存器状态。
- 不提供将其展开为空的宏。
- 核心编译器不新增识别 `"pdata"` 字符串的诊断分支；不支持状态由能力文档、可选包装层和未分配 AS 门禁体现。

### 3.2 CODE 的 const 规则

`const` 与 AS 独立，宏不偷偷把二者混合成不同属性 ABI。

本方案规定：

- AS4 数据定义应为 const-qualified 对象。
- 经 AS4 的 store，无论是否 volatile、是否通过去 const cast 得到，均为目标错误。
- 将 AS4 cast 为其他 AS 以绕过只读限制，首期禁止。
- 普通 `const` 不自动变 AS4；Tiny 中普通近指针可访问的 const 运行副本问题仍遵循 DESIGN.md B.5。
- 函数位于 AS4 不等于 CODE 数据读取已实现；函数地址和数据读取能力分开验收。[D01]

### 3.3 为什么不新增 LangAS 枚举

**裁定：纯数字 attribute＋target-specific LangAS 值。**

理由：

1. 属性已经进入 `QualType` 的地址空间限定，参与类型检查，不是仅附加 section。
2. `getTargetAddressSpace` 已能直接还原数字。[S02]
3. `TargetInfo::isAddressSpaceSupersetOf` 存在目标扩展钩子，可定义有向隐式转换关系。[S03]
4. LangAS 枚举自身不会自动完成 CODE 禁写、AS5 限制、范围检查或 ABI 保留。
5. 避免让宏拼写与核心 AST 枚举绑定，也避免扩大跨目标公共枚举。

诊断应包含：

- AS 数字。
- 指针宽度。
- 操作类别。
- 目标能力状态。
- 源码位置。

例如"AS4 数据空间不允许 store"，不需要依赖用户写的是 `code`、`__code` 还是裸属性。

---

## 4. 指针类型与 ABI

### 4.1 两轴宽度矩阵

**本表沿用 DESIGN.md B.2/B.3/D.2，属于设计 ABI，不是本轮运行证据。**

| AS | 类型语义 | Tiny | XTiny | Small | XSmall | Large | ABI 对齐 |
|---:|---|---:|---:|---:|---:|---:|---:|
| 0 | 默认数据指针 | 16 | 16 | 32 | 32 | 32 | 8 bit |
| 1 | strict direct RAM | 16 | 16 | 16 | 16 | 16 | 8 bit |
| 2 | 内部间接 RAM | 16 | 16 | 16 | 16 | 16 | 8 bit |
| 3 | XDATA | 32 | 32 | 32 | 32 | 32 | 8 bit |
| 4 | CODE 数据／函数地址 | 32 | 32 | 32 | 32 | 32 | 8 bit |
| 5 | 位访问语义 | 无普通指针 ABI | 同左 | 同左 | 同左 | 同左 | 不适用 |
| 6 | classic SFR direct-byte | 16 | 16 | 16 | 16 | 16 | 8 bit |
| 7 | XFR，独立能力待定 | 32 | 32 | 32 | 32 | 32 | 8 bit |
| 8 | 显式 EDATA | 16 | 16 | 16 | 16 | 16 | 8 bit |
| 9 | far generic 数据 | 32 | 32 | 32 | 32 | 32 | 8 bit |
| — | pdata | 不支持 | 不支持 | 不支持 | 不支持 | 不支持 | 不分配 |

16／32 位同时也是对应 B.3 指针 index width。

#### 为什么保持这张表

- AS1 物理范围只有低 128B，不要求 C 指针必须压缩为 1B。
- AS3 需要保留 bank，不能因借鉴经典 Keil 表格就缩为 2B。
- 32 位是 canonical 24 位地址容器，不是 4GiB 可访问空间。
- Tiny＋Huge 中函数地址仍为 4B AS4，不得截断高 CODE bank。
- XSmall 普通对象即使放在 EDATA，取地址仍为 4B AS0；显式 AS8 对象取地址才是 2B。
- 本项目不承诺 Keil／SDCC 二进制兼容。调研中不同工具链的实际指针 ABI 本身也不能混为一表。[D01，S11]

### 4.2 首期拒绝矩阵

区分"布局可描述"和"实现可执行"。

| 能力 | Tiny／XTiny | Small／XSmall／Large |
|---|---|---|
| 当前发布边界的 AS0 | 阶段3验收前整模块拒绝 | 保留现有已支持子集 |
| AS1/2/8 普通指针 | 每-AS i16 机器支持前拒绝 | **同样拒绝，不能因 AS0=32 放行** |
| AS3/9 指针访问、首参／返回 | Tiny 模块门禁及相应能力未解除前拒绝 | AS 感知 lowering／ABI 验收后开放 |
| AS4 数据 load | CODE 路线验收前拒绝 | 同左 |
| AS4 数据 store | 始终拒绝 | 始终拒绝 |
| AS4 函数地址／调用 | 独立调用契约；不得缩成 AS0 | 保留已支持调用子集，不连带开放 CODE load |
| AS5 普通指针、普通访存 | 始终拒绝 | 始终拒绝 |
| AS5 受控位操作 | 位能力及模块能力通过后开放 | 位探针与 L1 验收后开放 |
| AS6 普通参数／返回、动态指针 | 首期拒绝 | 首期拒绝 |
| AS7 普通用途 | 独立能力冻结前拒绝 | 同左 |
| 后续指针静态参数槽 | D.5 实现前拒绝 | 同左 |
| 非默认 AS 自动存储对象 | 首期拒绝 | 首期拒绝 |
| pdata | 拒绝 | 拒绝 |

未来可以分项开放，但**不得仅用"AS 编号已分配"作为可执行白名单**。

### 4.3 指针对象位置与所指空间

必须区分：

1. 指针变量本身的存储 AS。
2. 指针值指向的 AS。
3. 链接后所在物理区域。

例如：

`unsigned char __attribute__((address_space(3))) *p`

表示 p 指向 AS3；若 p 为普通局部变量，其自身仍在寄存器或 AS0 栈上。

结构体内存放一个 AS3 指针是普通指针载荷布局问题；把字段本身声明为特殊存储空间则是另一能力，首期不支持后者。

### 4.4 隐式转换允许集

以下是**完整执行契约中建议开放的有向关系**；只在源／目标能力及 cast lowering 均通过后启用。当前兼容执行域不因表存在而自动启用。

| 源 AS | 隐式目标 AS | 条件 |
|---|---|---|
| 任意已支持 AS | 同 AS | 正常 C 类型与 cv 限定规则 |
| AS1/2/8 | AS0 | AS0 为已实现的 RAM generic 语义；16／32 位均可承载其合法地址 |
| AS1/2/8 | AS9 | 内部 RAM 地址零扩展，保持别名 |
| AS3 | AS9 | canonical 数据地址，保留 bank |
| AS3 | AS0 | 仅 AS0=32 且 generic 访问等价 |
| AS0 | AS9 | AS0=16 时零扩展；AS0=32 时保留 canonical 地址 |
| AS9 | AS0 | 仅 AS0=32 且 generic 域等价 |

其余跨 AS 隐式转换禁止，包括：

- Far32→Near16。
- AS0→AS1/2/8。
- AS1↔AS2↔AS8 的自动互转。
- generic→XDATA。
- CODE↔数据。
- SFR／XFR／bit→generic。

补充约束：

- `void *` 不免除 AS 检查。
- `T **` 不能因一层 `T *` 转换合法，就协变为另一 AS 的双重指针。
- 函数指针签名、形参 AS、返回 AS 必须一致；不能用强转隐藏不一致。
- 条件表达式、比较、复合类型形成须使用相同有向规则。
- 禁止根据 AS 不同自动产生 NoAlias；AS1/2/8/0/9 可别名。

### 4.5 显式转换和整数转换

- 跨 AS 使用 `addrspacecast`，不因同宽而伪装成 bitcast。
- 已知常量 Far→Near 只在目标空间、范围、对象合法性均成立时接受。
- 动态 Far→Near 不允许直接截断后解引用；checked conversion 留作独立接口。
- 内部 near 空间之间的显式转换不能只检查 `<=0xffff`，还要满足目的空间实际域。
- 指针到整数保留 canonical 数值；普通整数运算不得自动获得合法指针证明。
- 首期新增 named-AS 的动态整数构造解引用不作通用逃生口。
- 普通 null 语义保持；位地址 0 由位 intrinsic 的整数操作数表达，不能通过 AS5 null 解引用表达。[D01]

### 4.6 参数、返回与 Shizuku 线接口

沿用 D.2：

| 指针值 | 首个合格参数／合格返回值 | 内存表示 |
|---|---|---|
| 16 位 AS0/1/2/8 | DPL／DPH | `high, low` |
| 32 位 AS0/3/9，以及受支持 AS4 | DPL／DPH／B／A | `00, bank, high, low` |

- 寄存器按低有效字节优先分配。
- 32 位 canonical 指针 A 发送为零；普通 i32 的 A 是有效数据。
- 16 位指针不要求清 B/A，接收方不得依赖其值。
- 后续参数槽由**完整指针类型**取 2B／4B，不按目标对象区域或槽地址宽度取值。
- AS5/6/未开放 AS7 不进入普通参数 ABI。
- bit 读取后得到的普通布尔值不是 bit 引用；其传参适用普通值规则，不构成 AS5 ABI。[D01]

与 Shizuku 线冻结的接口面：

1. 每次指针布局查询输入实际 AS，不能只查询 p0。
2. 指针算术、载荷宽度、机器地址寄存器宽度分别建模。
3. named near 在 32 位模块中也走 i16 legalization。
4. Tiny 中 AS3／AS4／AS9 仍需要 i32 载荷与地址支持。
5. frame／alloca 保持 A0，不能为方便间接访问改成 AS8。
6. call lowering 保留原始参数索引、AS、指针宽度及调用契约，不能仅查看 legalize 后的 i16／i32。
7. `getPointerAlignV` 必须取 ABIAlignment；Tiny 的 `getMaxPointerWidth` 仍须反映 32 位 named-AS 指针。
8. 后续槽的"载荷类型"与"访问该槽的地址类型"必须分开。

本稿不假设 Shizuku 的具体寄存器或 pseudo 设计。

---

## 5. 全局放段与 ELF／EDATA 闭环

### 5.1 前端职责

`GetGlobalVarAddressSpace` 保留显式 AS；未限定对象保持 AS0。

默认放置枚举 1／3／8 **不是将普通 global 改为 AS1／AS3／AS8 的请求**。[D01，S03]

### 5.2 分节规则

| 对象语义 | 逻辑区域 | v2 ELF section 类别 |
|---|---|---|
| AS0 data-preferred，可迁移 | DSEG，必要时 EDATA | `.mcs251.dseg.flex.*` |
| AS1 strict data | DSEG | `.mcs251.dseg.direct.*` |
| AS2 | ISEG | `.mcs251.idata.*` |
| AS3 | XSEG | `.mcs251.xdata.*` |
| AS8 | EDATA；可称逻辑 ESEG | `.mcs251.edata.*` |
| 默认放 EDATA 的 AS0 | EDATA／ESEG | `.mcs251.edata.*` |
| 默认放 XDATA 的 AS0 | XSEG | `.mcs251.xdata.*` |
| AS4 const 数据 | CSEG／CODE | `.rodata.*` 或批准的 CODE 子类 |
| AS9 存储对象 | 须带合法物理放置类别 | 首期建议只开放明确 XDATA 放置；未确定则拒绝 |
| AS5 位引用 | 不分配普通对象 | 数值位访问；RAM backing reservation 独立 |
| AS6／AS7 | 不分配普通 RAM | 本设计不定义声明格式 |

"ESEG"在此是逻辑区域称呼，**不宣称当前汇编器或链接器已支持 `.area ESEG`**。

### 5.3 与现有体系的真实差距

已核源码：

- TargetObjectFile 对 mutable global 无条件返回 DSEG。
- AsmPrinter 还直接调用 `getDSEGSection()`，绕过分类结果。
- 非零 AS global definition 当前在 AsmPrinter 被拒绝。
- lld 当前识别若干 legacy DSEG／ISEG／XSEG 类别，但 DSEG 分配仍封顶 `0x80`。
- 当前 XINIT 验证仍要求目的属于一个 DSEG slice。[S07–S08]

因此**只改 Clang AS，或者只新增一个 section 名，不会解除 128B 天花板**。

完整闭环必须同时完成：

1. 每对象放置类别与能力。
2. TargetObjectFile 与 AsmPrinter 使用同一分类结果。
3. lld 接受并校验 v2 section／对象协议。
4. strict data 不可迁移；flex 对象整体迁移。
5. EDATA 与 ISEG、bit backing、寄存器银行、overlay 共用正确的内部占用账本。
6. XDATA 独立分配，不计 EDATA 栈水位。
7. XINIT／CRT 按目的类别执行初始化。
8. 大小、初值指针 relocation、bank、addend 不截断。
9. 未限定 const、字符串与常量池不因放在 ROM 就绕过访问能力门禁。

### 5.4 初始化与 ROM 读取依赖

沿用 B.7 的 v2 初始化方向，但要补足：

- v2 destination 是 canonical 数据地址，不自动说明"如何访问"。
- 初始化目的必须能通过对象记录关联到合法 RAM 放置类别。
- 同一地址数字不能被用来初始化 CODE／SFR／XFR。
- 非零初始化依赖 ROM payload 读取；**CODE/MOVC 未验可能同时阻塞 XDATA／EDATA 非零初始化闭环**。
- 可以先开放已经独立验收的清零子集，但不能把 NOBITS 当成上电必然为零。
- 不以当前六字节 XINIT 头"能表示 16 位地址"为理由跳过 v2 目的域验证。

### 5.5 .area 与 ELF 边界

新生产能力只走 ELF/lld。

- `.area DSEG/ISEG/XSEG/CSEG` 可保留为历史汇编展示或档案锚点。
- 不新增 REL 协议，不要求新能力与 REL 逐字节对拍。
- ELF section 名与数值放置类别可供目标链接器识别，但**不能反推源 pointer AS**。
- 自定义 section 不得覆盖 AS 范围／访问规则；未经分类的 section 不能成为逃生口。

---

## 6. 按 AS 选择访存指令

### 6.1 统一地址分类接口

地址分类至少消费：

- LLVM AS。
- layout／execution contract。
- pointer width、index width。
- 访问宽度、对齐、volatile／atomic 属性。
- 符号或绝对地址。
- direct-required／indirect-safe。
- frame-relative 状态。
- 原始 MMO。
- 对象／区域能力。

入口先按语义域判断合法性，再做常量地址或位移优化。

### 6.2 指令路线表

| AS／用途 | 首期允许路线 | 证据与边界 |
|---|---|---|
| AS0 兼容执行域 | 保留既有 AS0 行为 | [S] 不借布局升级悄改兼容语义 |
| AS0 near，完整执行域 | `@WR` byte／word；符合规则的位移形式 | [Q][H] 机器形式已有证据；i16 软件 lowering 仍须验收 |
| AS0 far／AS9 RAM | canonical DR 间接访存，signed dis16 | [Q] Phase11；保持 24 位有效地址 |
| AS1 静态 strict RAM | 经范围检查的 direct byte；其他可用内部间接路线 | [Q][H] RAM direct／间接已有交叉证据；symbol relocation 必须验证 |
| AS1 动态、AS2、AS8 | `@WR` 内部访问 | [Q][H] 不因数值 `0x80..0xff` 选 SFR direct |
| AS3 | 经验证的 DR 24 位间接路线 | [Q] 不自动换成经典 MOVX |
| AS4 数据读 | 暂不启用；等待 CODE 路线探针裁定 | [P] MOVC 历史偏差未裁定 |
| AS4 数据写 | 拒绝 | [D] 永久只读边界 |
| AS5 | 仅专用位操作路线 | [P] 全部位指令未探针 |
| AS6 受控 byte 访问 | 已验证 direct-byte 子集 | [Q][H] 不含普通动态 SFR 指针 ABI |
| AS7 | 拒绝未认证用途 | [P] 不假装普通 XDATA |
| pdata | 拒绝 | [D] 无首期 AS |

补充：

- i32 普通 RAM 访存首期可继续按大端有序 byte 拆分。
- 已验证 word 形式不等于其 RMW 或任意总线行为具有原子性。
- volatile MMIO 不允许未经协议许可的宽度改变。
- 禁止从地址数值小于 `0x100` 推断空间。

### 6.3 位移与回绕

已接受事实的精确范围：

- `@WR` byte／word、对齐／非对齐及大端已有 QEMU＋真机证据。
- `@WR±dis16` 的 ±0x3EFF 最大合法 RAM 样本是 QEMU 证据。
- 真机子集覆盖 0／±0x10／±0x100。
- 不把这些样本提升为所有 signed16 极值、回绕或不存在 RAM 的访问已测。
- `@DR+dis16` 的 signed16 语义已有 Phase11 QEMU 证据。[Q01–Q02]

无法证明近指针折叠后的地址生成符合 16 位 IR index 规则时，应先完成正确宽度的地址计算，再使用已验证无位移路线。**不能把 16 位 GEP 直接变成不截断的 32 位 DR 加法。**

---

## 7. L1：受控位左值与强制原子 lowering

### 7.1 为什么普通 AS5 volatile 对象不够

以下推论不成立：

> "声明成 `volatile _Bool address_space(5)`，所有位操作自然是原子位指令。"

原因：

- C `_Bool` 不是 1 bit 可寻址存储 ABI。
- LLVM `load i1`／`store i1` 不自动表示 MCS251 位寻址。
- `volatile` 不提供 RMW 原子性。
- 普通 C 取反赋值可能先形成 load，再形成 store。
- 优化等级、表达式结果使用及 legalization 会改变模式形状。
- B.2 明确禁止普通 AS5 指针。[D01]

### 7.2 具体形态

**推荐新增目标 builtin 的语义能力，不新增语法。**

接口形状建议冻结为：

`__builtin_mcs251_bit_lvalue(unsigned_constant_bit_address)`

语义：

1. 使用现有函数调用表达式语法解析。
2. Sema 按 builtin ID 识别，而不是比较方言关键字字符串。
3. 唯一参数必须为整数常量表达式。
4. 产生**受控、volatile、不可取址的位左值**。
5. 左值的值语义为布尔值，存储语义标记为 AS5。
6. bit address 存在该受控 AST 表达式的操作数中；不为每个地址创造一个普通 QualType。
7. 不产生真实函数调用，也不向 LLVM IR 暴露普通 AS5 指针。
8. Clang CodeGen 必须直接产生目标 bit intrinsic。

可选头文件将某个名字映为该表达式，是未来包装选择；**本设计不规定位名或声明转换格式**。

`sbit` 在本方案中表示上述受控位引用能力，而非保证旧式 `sbit name = byte^bit` 可以原样解析。后者不在零语法改动承诺中。

### 7.3 类型与用途限制

首期拒绝：

- 对位左值取地址。
- 普通 AS5 指针声明、转换、参数、返回。
- 位引用数组、聚合载荷、动态选择、PHI 化引用。
- 对位左值做指针算术、GEP、memcpy。
- 通过 `typeof` 等途径构造普通可分配 AS5 对象。
- 普通 AS5 `load/store`，包括手写 IR。
- 将位引用转换为 byte 指针。
- 对位存储应用 `_Atomic`，以免冒充 C11 原子对象。

普通变量接收一次位读取的布尔结果是允许的；结果不是位引用。

### 7.4 地址空间与 backing byte

硬件映射事实沿用用户确认：

| bit address | backing byte | bit index |
|---|---|---|
| `0x00..0x7f` | `0x20 + (B >> 3)`，内部 RAM | `B & 7` |
| `0x80..0xff` | `B & 0xf8`，位寻址 SFR | `B & 7` |

注意：

- **bit address 0xff 与 direct byte address 0xff 不是同一地址。**
- 即便 bit address 编码有效，也不证明其 backing SFR 可安全读写。
- 探针安全策略须先计算 backing byte，再决定是否可访问。
- AS5 bit address 0 是有效位编号；不用 null pointer 表达。

编译器管理的 ACC／B／PSW 等寄存器，其位访问可能干扰 ABI 或活跃寄存器值。首期普通位接口应拒绝这类未经独立寄存器效果建模的用途；探针可在隔离汇编 harness 中使用，不代表 C 接口已授权。

EA 属中断控制特殊操作，走 §9 的接口与效果模型，不作为绕过中断契约的普通位访问许可。

### 7.5 强制 lowering 表

下表规定的是**待探针通过后** L1 的语义保证，不是当前指令已支持。[D][P]

| 源操作类别 | 必需效果 | lowering |
|---|---|---|
| 位赋常量 1／转换后 true | 一次置位 | SETB bit |
| 位赋常量 0／false | 一次清位 | CLR bit |
| 独立布尔值赋位 | 计算值一次，执行一次目标位写 | 分支到 SETB／CLR；未验证 MOV bit,C 前不依赖它 |
| 丢弃结果的 `b ^= 1` | 原子翻转当前位 | CPL bit |
| 丢弃结果、同一受控引用的 `b = !b` | 本目标明确规定的原子翻转 | CPL bit |
| 读取位值 | 一次位采样 | MOV C,bit，再物化普通布尔值 |
| 位作为条件 | 一次位测试 | JB／JNB；不能破坏 volatile 次数 |
| 测试并清除 | 独立专用操作，首期普通 C 模式不自动推断 | JBC，待独立语义接口验收 |

重要限制：

- `b = ~b` **不是** `_Bool` 的逻辑翻转；不能选 CPL。`~0` 和 `~1` 都是非零整数。
- CPL 操作的结果还要被使用时，首期拒绝该 L1 形式。
- 不能通过"CPL 后再读一次"伪造原子操作返回值。
- `b = other_bit` 可由一次源位读＋一次目的位写组成，但二者整体不是原子复制。
- 自读后参与复杂计算的赋值不在 L1 原子 RMW 集合，首期拒绝或要求显式改写，不能静默降为 byte RMW。
- `if (b) b=0` 不自动等于 JBC；尤其不能把不同可观察操作任意合并。
- O0／O2／Os 下 L1 必须保持同一保证；不能依赖可选 DAG combine 才成立。

### 7.6 IR／机器层效果模型

建议使用具有独立操作 ID 的目标 intrinsic：

- bit read。
- bit set。
- bit clear。
- bit toggle。
- 后续 test-and-clear。

具体 intrinsic 名称与能力版本实施前登记，不在本稿擅自分配正式数值。

必须保证：

1. bit address 为必需常量操作数。
2. 操作不被删除、复制、投机执行或普通 CSE。
3. toggle 的读改写是一个不可拆分目标操作。
4. 不能标成 `readnone` 或仅访问 inaccessible memory；bit RAM 与普通 RAM 可以别名。
5. 初期采用保守内存效果；有精确 backing 信息时，MMO 仍需覆盖 backing byte 的潜在别名。
6. 不能因为位宽是 1 就宣布相邻位与 byte 访问 NoAlias。
7. MOV C,bit 必须声明 carry／PSW 影响，并用 glue／依赖保护其读取到布尔物化之间的状态。
8. JBC 应记录读写效果；JB／JNB 记录读取效果。
9. 机器 verifier 检查受保证 bit pseudo 最终只能变成批准的位指令路线，不能回退普通 RMW。

硬件指令原子性与 LLVM C11 原子内存模型不同：本方案保证的是批准运行环境下的**单指令位更新／采样**，不是 `_Atomic`、全局内存屏障或任意外设总线事务原子性。

### 7.7 RAM 位占用

固定 bit reference 不自动分配内存。

RAM backing byte 必须：

- 指向一个已合法预留或分配的 backing 对象。
- 纳入 linker 内部 RAM 占用。
- 与普通 DSEG／ISEG／overlay 避免意外重叠。
- 明确有意 byte／bit alias，不把多个别名重复计为多个物理字节。

首期建议对固定引用采用保守 backing-byte reservation；自动 bit packing、跨 TU bit allocator 和 bit relocation 留作后续独立能力。

---

## 8. L2：AVR 式惯用法折叠

### 8.1 实现层裁定

采用 SelectionDAG 模式／目标谓词路线：

- OR／AND 以 AVR TableGen 模式为结构先例。
- MCS251 需要额外 AS、寄存器语义及原子能力谓词。
- 复杂合法性条件放目标 matcher helper，不能只靠 opcode 字面模式。
- 若现有 Custom LowerStore 过早把访存转换为机器节点，应在这一不可逆步骤前完成受控匹配，或保留目标 RMW 节点供选择。
- 不在普通 late peephole 中凭指令文本重建 C volatile 语义。[S04，D]

### 8.2 必须同时满足的判据

| 条件 | 要求 |
|---|---|
| 访问对象 | 单个 i8 byte 对象 |
| 读写关系 | 同一已证明地址、同一 AS、同一 backing 对象 |
| 地址 | 选择时已知绝对地址；首期不依赖尚未冻结的 bit relocation |
| 空间 | 明确位寻址 RAM，或已认证可折叠的 AS6 byte 访问 |
| 掩码 | OR／XOR：`1<<k`；AND：低 8 位恰为清除一个 bit 的掩码 |
| bit index | 0..7 |
| volatile | 主目标是 volatile 固定地址惯用法；双方访问属性一致 |
| atomic | 不是 LLVM atomic 操作；不得借此支持 `_Atomic` |
| use | load 值仅服务该 RMW；RMW 结果不被额外使用 |
| chain | 无中间 volatile、call、barrier、可能冲突访问或其他可观察副作用 |
| 外围语义 | 证明 byte RMW 与所选位操作等价 |
| 能力 | 目标位指令探针及本操作发布能力已通过 |

对应指令：

| 惯用法 | 目标 | 来源 |
|---|---|---|
| `\|= single_bit` | SETB | AVR SBI 结构先例＋MCS251 探针 |
| `&= ~single_bit` | CLR | AVR CBI 结构先例＋MCS251 探针 |
| `^= single_bit` | CPL | **本项目扩展；无上述 AVR XOR 先例** |

### 8.3 必须拒绝折叠的情形

- 地址或掩码动态。
- 多 bit 掩码，不能拆成多个位指令后称整个操作原子。
- xdata／edata 普通变量，或未证明属于位寻址 RAM 的 generic 指针。
- AS0 低地址但没有明确 RAM／direct 语义证明。
- 非位寻址 SFR byte。
- GPIO 引脚读值与 latch RMW 语义不等价。
- read-to-clear、write-one-to-clear、write-only 等外围副作用不等价。
- 旧值／新值另有用途。
- 转换、truncate、扩展链无法证明严格等价。
- 仅凭注释、变量名、宏名或地址末尾 0／8 推定可折叠。
- 不可达代码删除前存在非法 CODE store：该输入应先诊断，不能依靠 DCE 隐藏错误。

"地址可位寻址"只是编码必要条件，**不是外围行为等价的充分条件**。

### 8.4 对用户的承诺

L2 是优化，不是保证接口：

- 成功匹配时最终确实使用单条批准位更新指令。
- 未匹配时保留普通非原子 RMW，并可提供优化 remark。
- 不承诺所有优化等级必然折叠。
- 需要稳定原子性时使用 L1，而不是在反汇编中碰运气。
- L1 受保证操作不允许失败后静默退为 L2／L3。

---

## 9. L3：不原子区域与临界区规格

### 9.1 不原子边界

以下普通 C RMW 不承诺原子：

- xdata／edata 普通对象。
- 非位寻址 SFR。
- 未满足 L2 全部判据的 byte RMW。
- 多 byte 读写或 RMW。
- 多 bit 更新。
- 测试后另行写入。
- 源位读取加目的位写入。

即使单次 byte load／store 恰为一条指令，`load → 运算 → store` 仍可能被中断插入。

### 9.2 临界区抽象接口

接口语义应为：

- `irq_save_disable()`：保存原 EA，关闭普通可屏蔽中断，返回恢复 token。
- `irq_restore(token)`：恢复此前 EA 状态。

上述为接口规格名称，不规定未来宏拼写或头文件格式。

必须满足：

1. 保存原 EA，不保存后直接假定它为 1。
2. 进入时执行已批准的 EA 清位路线。
3. 退出仅在原 EA=1 时重新置位；原 EA=0 时保持关闭。
4. 进入／退出具有编译器 memory barrier 效果。
5. 受保护操作不能被提到关闭前或沉到恢复后。
6. token 为每次调用独立的普通值，不使用共享静态槽。
7. 支持严格 LIFO 嵌套。
8. 拒绝或明确禁止跳过恢复的 return／goto／longjmp 等用法。
9. 临界区内不得未经接口擅自修改 EA／优先级。
10. 恢复只改 EA，不整体恢复旧 IE byte，避免覆盖其他使能位变更。

### 9.3 不能把 CLR／SETB 对称作天然原子对

必须采用准确表述：

> CLR EA 与 SETB EA 是分别需要验证的单指令位操作；临界区安全由保存原态、关闭后的排他条件、编译器排序和正确恢复共同建立。

- "读 EA，再 CLR EA"可能存在中断进入窗口；需要中断 v2 的上下文保护与 EA 管理契约共同保证。
- 首期不依赖 JBC EA 优化成测试清除；JBC 独立探针通过后再评估。
- 不能依赖 QEMU 未建模的 one-instruction deferral 才正确。
- 仅清 EA 不屏蔽 NMI、DMA 或外设自主状态变化。
- 不提供 C11 全局原子／memory_order 保证。[D02，P]

### 9.4 与 INTERRUPT-DESIGN v2 的接口

本线提供位操作／EA 原语及其机器效果。

中断线负责：

- 普通可屏蔽源、优先级与嵌套环境。
- 完整现场保存及 carry／PSW 恢复。
- 临界区控制流与调用约束认证。
- 安全摘要／栈预算。
- NMI 排除与运行时接管边界。

本方案**不解除**：

- ISR 闭包静态参数槽禁令。
- 未认证动态 SPX 更新禁令。
- 递归／未知间接调用限制。
- 优先级运行时重配置的安全保证失效边界。

---

## 10. 第一阶段工程：让静默错译响亮失败

### 10.1 优先级

**P0-A：CODE 写入全链拒绝。**

覆盖：

- store，包括 volatile store。
- atomic store／RMW／cmpxchg。
- memset 目的。
- memcpy／memmove 目的。
- 聚合展开后的写入。
- 去 const、addrspacecast、整数往返形成的可识别绕过路径。

随后：

**P0-B：全部尚未实现的非零 AS 数据用途 fail-closed。**

不能用"编号已分配"作为执行白名单。

### 10.2 具体落点

| 层 | 落点与要求 |
|---|---|
| Clang Sema | 属性类型构造、声明完成、转换、赋值、参数／返回检查；CODE 写入优先诊断 |
| Clang CodeGen | 验证受控 builtin；不生成普通 AS5 指针；不能只依赖 Sema |
| 原始 IR／bitcode 入口 | 在布局覆盖和输出文件创建前，核对原始 DL 与数值契约 |
| 目标 Module verifier | 真正接入 llc、Clang 后端及 LTO；在优化前和选择前检查 |
| DAG LowerLoad／LowerStore | **在 `parseAddress` 前取真实 AS 并校验用途** |
| 其他 DAG lowering | GlobalAddress、addrspacecast、memory intrinsic、call／return、atomic 入口再次检查 |
| Machine 层 | 非法 MMO AS／未支持 bit pseudo 不得进入通用路线；MIR 入口也须验证 |
| AsmPrinter／对象层 | 定义、声明引用、初始化 relocation 和 section 契约最终校验 |

完整 verifier 要覆盖：

- 纯声明模块、纯 global 模块、无函数模块。
- 函数本身 AS 与数据访问 AS 的区别。
- 参数／返回、call operand、ret。
- 指针嵌套聚合。
- initializer／ConstantExpr。
- alias／ifunc 等额外 global value，未支持时明确拒绝。
- 未分配 AS。
- 普通 AS5 类型和值，即使没有 load/store。
- 代码优化前可能被删除的非法操作。
- 优化后重新形成的非法访问。

**AS4 函数位置不能被"一切非零 AS 都拒绝"的粗暴规则误杀。** 放行必须按用途，而不是只按数字。

### 10.3 负测矩阵

| 负例组 | 必须结果 |
|---|---|
| AS4 固定地址 `0x80` store | 确定性 CODE-write 诊断，非零退出 |
| AS4 动态指针 store | 同上 |
| AS4 memset／memcpy destination | 同上 |
| AS3／AS4 低地址 byte load | 未实现时拒绝，不能生成 direct SFR 指令 |
| AS2／AS8 `0x80`／`0xf0` load/store | 未实现时拒绝，不按 AS0 direct |
| 32 位模型中的 AS1/2/8 指针 | i16 能力未完成时拒绝 |
| AS5 declaration／常量／聚合／call／ret | 无 load/store 也拒绝 |
| 未分配 AS10 等 | 不得靠 DL p0 fallback 放行 |
| CODE→RAM cast 后写 | 首期拒绝转换 |
| 非零 AS extern global 访问 | 不因没有定义而绕过 |
| `-disable-verify` | 不能关闭目标契约门禁 |
| bitcode／MIR／LTO | 与文本 IR 同样 fail-closed |
| 原始 p0=16 与 TM=32 | 在覆盖前拒绝 |
| 输出文件已存在 | 错误输入不留下新的可用对象或误覆盖旧产物 |

以 asm 和显式 ELF obj 两条输出检查；合法 AS0 与已支持函数调用路径保持回归。

---

## 11. QEMU／真机探针清单

### 11.1 通用协议

沿用 p6／p7 方法，不复用其批次编号；本设计使用 **DF-Pxx**。

每个探针必须：

1. 先冻结工具指纹、源码身份、运行机器和实际映射。
2. 先运行不含待测指令的 control。
3. 隔离 WR／DR／R 别名、carry、PSW、ACC、B 与打印代码。
4. 测量期间不调用打印；先保存快照，再输出。
5. 使用独立已验证路线交叉初始化／观察。
6. 设置目标、邻接与竞争地址哨兵。
7. 记录每个子断言，不只匹配 PASS 字符串。
8. timeout 124 不能独立算成功。
9. QEMU 与真机分别结案；模型缺项记 INCONCLUSIVE。
10. 不读写 direct `0xff` RSTCFG；不把危险配置寄存器用于通用扫地址测试。
11. 新生产编译链验收使用 ELF/lld；历史探针产物只作证据。
12. 保存源码、命令、工具指纹、对象／relocation、编码、map、HEX、串口、stderr、退出码、期望值、逐项判定和 manifest。

### 11.2 探针表

| 编号 | 目的 | 判据 | 预期产物 |
|---|---|---|---|
| DF-P00 | harness 控制、寄存器别名隔离 | 不含待测位指令仍正确保存／输出全部哨兵 | control 镜像、寄存器占用表、控制串口 |
| DF-P01 | SETB／CLR，RAM 位地址映射 | 对已预留 `0x20..0x2f` backing byte 测 bit 0..7；只改变目标 bit | 位号×初值矩阵、byte 快照、编码 |
| DF-P02 | CPL，**本项目 XOR 扩展** | 0→1、1→0；连续两次恢复；其他 bit 不变 | CPL 镜像、前后快照、负对照 |
| DF-P03 | MOV C,bit 与布尔物化 | 读 0／1 正确；邻 bit 不影响；carry／PSW 效果符合记录 | carry 快照、布尔结果、压力用例 |
| DF-P04 | JB／JNB | 0／1 两路径、邻 bit、前后向分支、短分支边界及批准远分支展开正确 | 路径哨兵、最终编码、CFG 对照 |
| DF-P05 | JBC | 初值 0 不跳且保持0；初值1跳且清0；其他 bit 不变 | 双初值输出、读改写快照 |
| DF-P06 | SFR 位空间与 RAM 同值分离 | 选择已审查安全 backing SFR；位访问不误触同数值间接 RAM | 双空间哨兵、外围状态说明 |
| DF-P07 | SFR bit／byte RMW 等价边界 | 区分 latch、引脚读值及副作用；不能证明等价的类别维持禁止 L2 | 类别判定表、QEMU／真机差异 |
| DF-P08 | MOV bit,C，后续候选 | C=0／1 写入及标志效果正确；通过前不用作动态赋位主路线 | 独立编码／效果记录 |
| DF-P09 | 位更新与中断竞争 | 经认证 ISR 在竞争窗口更新同 byte 的其他 bit；位路线无丢失，普通 RMW 负对照能重现丢失 | 调度／触发记录、正负对照串口 |
| DF-P10 | EA 保存／关闭／恢复 | 初始 EA=0／1、嵌套、pending 中断、恢复路径均正确；不依赖 deferral | token／EA／ISR 次数快照 |
| DF-P11 | 每-AS 指针 ABI | 两组模型的 2B／4B 首参、返回、spill、后续槽；B/A 污染及 bank 保留 | C→IR→ELF、槽字节、返回哨兵 |
| DF-P12 | AS 分类回归 | AS2/8 间接 RAM 与 direct SFR 同值分离；AS3 bank 不丢；负偏移正确 | 各 AS 对照镜像与有效地址快照 |
| DF-P13 | CODE／MOVC 偏差裁定 | 精确待选形式的基址、索引、bank、跨64K、连续读与无副作用；同镜像对比 | 形式逐项结论、QEMU／真机差异单 |
| DF-P14 | 全局与初始化闭环 | 5091B 三分支、XDATA／EDATA 非零与零初值、2B／4B pointer initializer、栈门禁 | map、初始化快照、失败链接产物检查 |
| DF-P15 | 位操作优化不变性 | O0/O2/Os、内联、LTO、寄存器压力；L1 指令次数不变，L2 正负判据正确 | IR/MIR/反汇编与运行对拍 |

说明：

- DF-P01–P07 **整体覆盖要求中的 SETB／CLR／CPL／MOV C,bit／JB／JNB／JBC 指令族**。
- JBC 探针通过不自动授权 `if (b) b=0` 的普通 C 折叠。
- DF-P09／P10 依赖中断线的可认证环境；没有这一依赖时不得伪称已验证中断竞争原子性。
- 未测的 WR 极值／回绕继续以 MC／合成 IR 测试和合法有效地址观测分开处理，不使用不存在 RAM 的读零结果作 ISA 结论。

---

## 12. 分阶段实施、测试与并行协调

### 12.1 阶段安排

| 阶段 | 内容 | 依赖 | 退出条件 |
|---|---|---|---|
| **DF0** | CODE store 第一优先拒绝；非零 AS 能力门禁；核清候选 verifier 接入 | 无需等待位探针或 Tiny 实现 | 所有负例响亮失败，68/68 不回归 |
| **DF1** | 数字属性、Sema 类型规则、完整宽度／对齐查询、参数 AS 保留 | B.2/B.3 契约 | C 类型→IR AS 无丢失；未实现用途仍拒绝 |
| **DF2** | 位指令与 CODE 独立探针 | 安全 harness；竞争子项依赖中断线 | 每条指令单独结案，不整体虚报 PASS |
| **DF3** | AS 感知地址分类；AS3/9 与 AS1/2/8 分批开放 | **Shizuku 每-AS i16 legalization 接口完成**；对应机器证据 | 两组宽度、同值不同空间、ABI 正确 |
| **DF4** | 全局 section、对象属性、EDATA／XDATA、XINIT／CRT | lld v2 与初始化读取路线 | 128B 缺口真正关闭；5091B／栈边界通过 |
| **DF5** | L1 位 builtin、intrinsic、机器 lowering | 对应位探针；DF0 门禁 | O0/O2/Os 强制原子路线或明确拒绝 |
| **DF6** | L2 OR／AND；再开放 XOR→CPL | DF5 基础与外围等价判据 | 全部正负模式测试；无假原子 |
| **DF7** | EA 临界区与中断 v2 集成 | 中断安全契约、DF-P09／P10 | 原 EA 恢复、嵌套与编译器排序认证 |
| **DF8** | CODE 数据与剩余模型矩阵 | MOVC／替代读取路线裁定、CP-A relocation 等 | CODE load／初始化正式验收；store 永久拒绝 |

idata 可与 AS1/8 共享已验证机器路线后低成本开放，但不为了"表格齐全"提前放行。

pdata 不进入首期阶段。

### 12.2 与 Shizuku 的时序协调

1. **DF0 可立即先行**，但对共享 LowerLoad／LowerStore 的改动须由 PM 统一串行集成。
2. 前端契约测试与位／CODE 探针可并行设计、运行。
3. 本线不另起一套 i16 指针机器实现；等待 Shizuku 的每-AS i16 支持接口。
4. Shizuku 的 M2 机器能力完成后，再排方言前端的相关 lowering 实现批次。
5. 不要求她先完成所有 EDATA linker 功能才测试 i16 指针机器语义；机器、ABI、分配分别验收。
6. 解除门禁按用途逐项进行，不一次性删除"非零 AS 不支持"防线。
7. 每次集成保全候选工具指纹；工作树残留不得混入无关提交批次。
8. Alice 只负责接口、反例、证据审核，不提交实现。

### 12.3 lit 测试矩阵

| 测试层 | 必测内容 |
|---|---|
| Clang Driver／TargetInfo | 五档到数值契约；两组指针宽度；所有 AS 对齐1B；Tiny max pointer width=32 |
| Clang Sema | 数字属性与宏等价；跨 AS 赋值／传参；CODE禁写；自动对象拒绝；AS5／未知AS；嵌套指针与聚合 |
| Clang CodeGen | global／参数／返回／GEP保留AS；不把默认placement变成AS；位操作直接产生目标intrinsic |
| LLVM verifier 负例 | 文本／bitcode／MIR／LTO；纯global／声明；ConstantExpr；优化前后；`-disable-verify`不可绕过 |
| LLVM CodeGen | AS×16/32×i8/i16/i32×volatile×常量/动态/符号×正负位移 |
| LLVM ABI | 首参／返回／后续槽；AS0=32与AS8=16并存；Tiny中AS3/4/9=32；寄存器压力 |
| 位 L1 | 每操作 O0/O2/Os；CPL结果使用拒绝；`~`不误选CPL；普通AS5 IR拒绝 |
| 位 L2 | 单bit正例、动态/多bit/结果使用/副作用/跨AS/非位区负例 |
| MC／对象 | 位立即数边界、branch范围、pointer relocation、section类别、未知能力拒绝 |
| lld | DSEG strict/flex、EDATA、XDATA、bit backing占用、初始化、栈1024/1023边界、混ABI拒绝 |

每阶段要求：

- 已有 **68/68 不回归**。
- 新增测试另计，不把总数变化当回归。
- 默认兼容产物差异必须解释，不自行更新 gold。
- 新完整执行域单独建立 gold，不覆盖兼容档案。
- lit、QEMU、真机三类结果分开汇报。

---

## 13. 与 DESIGN.md 的显式修订关系

| 编号 | 既有条款 | 本方案处理 |
|---|---|---|
| R1 | B.2/B.3 每-AS 宽度 | **保持不变**；完整矩阵已列，不重开 Keil 压缩 ABI |
| R2 | B.4 "稳定 LangAS／目标 AS" | 明确采用 target-specific 数字 AS，不新增 MCS251 LangAS 枚举 |
| R3 | B.2 普通 AS5 全拒绝 | **保持普通 AS5 拒绝**；新增不产生普通 AS5 指针的受控位左值／intrinsic 例外 |
| R4 | D.2 bit 参数／返回不开放 | 保持；普通布尔读取值不等于 bit 引用 ABI |
| R5 | B.4/D.4 转换方向 | 细化为 §4.4 有向允许集；未实现前不启用 |
| R6 | B.4 历史低地址 direct | 兼容执行域保留；完整执行域按AS消歧；不能只看layout版本切换 |
| R7 | B.5 EDATA 分节 | 保持；ESEG仅逻辑称呼，不声称新增 `.area` 已存在 |
| R8 | F.5 候选 verifier／门禁计划 | 明确区分未跟踪候选、接入状态与已发布能力 |
| R9 | D.6 历史 P3 阻塞文字 | 当前态以 p7 结案为准；旧 FAIL 和旧结案原文作为历史证据保留 |
| R10 | E.5 字符串红线 | 方言关键字宏只在头文件／文档；语义层按AS、builtin ID、数值能力分派 |
| R11 | B.7 初始化 | 补充目的类别关联与 CODE 读取依赖，不单靠地址字段猜访问空间 |
| R12 | pdata | 既有表无此AS；本稿不增号、不假映射，首期不支持 |

---

## 14. 开放问题表

"已裁定"项列入此表用于防止实施时重开；"待定"项附推荐案。

| 编号 | 问题 | 状态与推荐案 |
|---|---|---|
| **O01** | 每-AS 指针宽度矩阵 | **方向已批准**：保持 B.2/B.3，仅AS0变化；不追随Keil 1/2/3B |
| **O02** | 每-AS 机器能力接口 | 待实现验收；共用Shizuku i16支持，named near不得在32位模型绕过 |
| **O03** | pdata 首期 | **已批准不做**；不分配AS、不假映AS3；未来须先定义页状态与中断/ABI |
| **O04** | MOVC 历史 QEMU 偏差 | 待DF-P13；推荐CODE数据load维持拒绝，按具体形式裁定，不能按QEMU名称分支 |
| **O05** | CODE初始化连带阻塞 | 待运行证据；推荐将清零与ROM复制分开发布，非零初始化不绕过CODE读取门禁 |
| **O06** | 位 builtin／intrinsic 具体拼写与能力版本 | 推荐§7受控左值接口；实现前冻结ID、效果属性和版本，不开放普通p5 |
| **O07** | CPL 有返回值形式 | **首期拒绝已批准**；将来需独立fetch语义与机器方案，不能补读伪造返回值 |
| **O08** | L2 SFR 等价能力来源 | 推荐结构化数值访问类别；无证明时不折叠。不能根据宏名或地址尾数猜外围语义 |
| **O09** | 位RAM自动分配／packing | 推荐首期仅固定引用＋backing byte预留；自动packing与bit relocation另案 |
| **O10** | AS9 存储对象默认放置 | 推荐首期只接受明确XDATA放置；AS9首先服务指针值，不按名称猜区域 |
| **O11** | 动态Far→Near checked转换 | 推荐后续独立成功/失败接口；首期禁止直接截断解引用 |
| **O12** | AS4函数指针与CODE数据指针对象身份 | 沿用DESIGN CP-A／FP裁定；不得混用函数入口relocation和CODE数据relocation |
| **O13** | EA临界区认证载体 | 由中断v2线冻结；必须原态恢复、编译器排序、控制流闭合；不放宽静态槽/SPX限制 |
| **O14** | 位操作对编译器管理寄存器的影响 | 推荐普通接口首期拒绝；逐项建模寄存器Use/Def后另行开放 |
| **O15** | execution contract 能力升级 | 推荐按用途能力发布，不重解释当前五字段中的兼容执行值；正式数字经PM登记 |
| **O16** | 原始IR布局与候选verifier接入缺口 | 第一阶段阻塞项；先确认当前发布源快照，再确保入口前/后均不可绕过 |

---

## 15. 证据索引

所有路径均为本轮亲核的绝对路径；行号用于本次读取定位。

| 编号 | 证据 |
|---|---|
| **D01** | `C:/Prj/LLVM/MCS251/validation/mcs251-models/DESIGN.md`，重点 B.2/B.3、B.4–B.7、D.2–D.6、E.5、F.2/F.5 |
| **D02** | `C:/Prj/LLVM/MCS251/validation/mcs251-models/proposals/INTERRUPT-DESIGN.md`，v2；重点§1.3、§2、EA／SPX／静态槽限制及开放问题 |
| **S01** | `C:/Prj/LLVM/MCS251/clang/lib/Basic/Targets/AVR.cpp:576–587`；`C:/Prj/LLVM/MCS251/clang/lib/Basic/Targets/AVR.h:25–60` |
| **S02** | `C:/Prj/LLVM/MCS251/clang/include/clang/Basic/AddressSpaces.h:28–114`；`C:/Prj/LLVM/MCS251/clang/lib/Sema/SemaType.cpp:6524–6575`；`C:/Prj/LLVM/MCS251/clang/include/clang/Basic/TargetInfo.h:1694–1699` |
| **S03** | `C:/Prj/LLVM/MCS251/clang/lib/CodeGen/CodeGenModule.cpp:6020–6025,6321–6359`；`C:/Prj/LLVM/MCS251/clang/lib/CodeGen/TargetInfo.cpp:139–144`；`C:/Prj/LLVM/MCS251/clang/include/clang/Basic/TargetInfo.h:493–518`；`C:/Prj/LLVM/MCS251/clang/lib/AST/Type.cpp:72–108` |
| **S04** | `C:/Prj/LLVM/MCS251/llvm/lib/Target/AVR/AVRInstrInfo.td:140–180,1409–1421`；`C:/Prj/LLVM/MCS251/llvm/test/CodeGen/AVR/io.ll:81–96` |
| **S05** | `C:/Prj/LLVM/MCS251/llvm/lib/Target/AVR/AVR.h:43–92`；`C:/Prj/LLVM/MCS251/llvm/lib/Target/AVR/AVRISelDAGToDAG.cpp:624–706`；`C:/Prj/LLVM/MCS251/llvm/lib/Target/AVR/AVRISelLowering.cpp:1000–1148`；`C:/Prj/LLVM/MCS251/llvm/lib/Target/AVR/AVRTargetObjectFile.cpp:37–81` |
| **S06** | `C:/Prj/LLVM/MCS251/llvm/lib/Target/MCS251/MCS251ISelLowering.cpp:1213–1471,1780–1882`；`C:/Prj/LLVM/MCS251/llvm/lib/Target/MCS251/MCS251InstrInfo.td:140–151`；`C:/Prj/LLVM/MCS251/llvm/lib/Target/MCS251/MCS251RegisterInfo.cpp:76–77` |
| **S07** | `C:/Prj/LLVM/MCS251/llvm/lib/Target/MCS251/MCS251TargetObjectFile.cpp:17–31`；`C:/Prj/LLVM/MCS251/llvm/lib/Target/MCS251/MCS251AsmPrinter.cpp:320–419` |
| **S08** | `C:/Prj/LLVM/MCS251/lld/MCS251/LinkerCore.cpp:170–231,645–704`，以及 XINIT 目的 DSEG 检查 |
| **S09** | `C:/Prj/LLVM/MCS251/llvm/lib/IR/DataLayout.cpp:748–757`，未列出的AS回退p0 |
| **S10** | `C:/Prj/LLVM/MCS251/llvm/tools/llc/llc.cpp:631–671`；`C:/Prj/LLVM/MCS251/llvm/lib/Target/MCS251/MCS251TargetMachine.cpp:37–58,124–126`；`C:/Prj/LLVM/MCS251/clang/lib/CodeGen/BackendUtil.cpp` |
| **S11** | `/home/liu/mcs251-models-research/report-2026-09-06.md`，三节均已读；xdata 363次与真实AS形参见第②节；Keil语料是需求证据，不是本项目ABI |
| **W01** | `C:/Prj/LLVM/MCS251/llvm/lib/CodeGen/MCS251ContractVerifier.cpp`，未跟踪候选；`C:/Prj/LLVM/MCS251/clang/lib/Basic/Targets/MCS251.h:55–67`，工作树diff；`C:/Prj/LLVM/MCS251/llvm/lib/TargetParser/MCS251TargetParser.cpp`，共享布局候选内容 |
| **Q01** | `C:/Prj/LLVM/MCS251/validation/mcs251-p11/hw-semantics-report.md:89–129`，DR signed dis16及四种访存形式的QEMU记录 |
| **Q02** | `/home/liu/mcs251-models-probe/p6/VERDICTS.txt`，WR byte／word／位移子集、独立交叉验证与原始串口 |
| **H01** | `C:/Prj/LLVM/MCS251/validation/mcs251-models/DESIGN.md:1263–1282`，用户烧录＋PM判读的p7真机结案 |
| **Q03** | `/home/liu/mcs251-models-probe/p7-g12/VERDICTS-g12.txt`，旧QEMU移交结案；其中"真机待测"为历史状态，当前真机状态以H01为准 |

---

## 16. 最终放行原则

本方案将"方言前端"限定为可验证的编译器能力，而不是头文件兼容工程：

> **宏只提供拼写；AS 决定类型与访问语义；每-AS 布局决定指针 ABI；对象协议决定物理放置；位 intrinsic 决定原子操作；探针决定哪些机器形式可以开放。**

发布必须同时满足：

- CODE store 不再静默穿透。
- 未实现 AS 不再按 AS0 数值规则生成代码。
- 参数、返回、指针载荷和对象区域不混淆。
- named near 在32位模型中仍正确采用每-AS i16支持。
- L1 不依赖优化器猜中，失败时明确拒绝。
- L2 不把普通 RMW 包装成保证原子。
- L3 保存／恢复原 EA，并服从中断 v2 安全边界。
- EDATA 全局真正经过 section、lld、初始化、栈门禁闭环，而非只改变宏或布局字符串。
- 位指令与 CODE 读取的未测项不进入生产假设。
- SFR 声明语法和头文件转换始终留在本设计之外。
