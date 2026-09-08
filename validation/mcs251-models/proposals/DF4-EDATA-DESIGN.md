# DF4 阶段细化设计——全局放段与 EDATA/XDATA 闭环

**作者**：Alice，首席设计／审核工程师
**日期**：2026-09-08
**状态**：细化设计终稿；范围与关键取舍按本轮 PM 最终裁定收敛，尚未完成的协议数值登记与实现验收不得视为已放行。
**落盘**：PM 收完整终稿后落盘。

## 0. 结论与证据口径

### 0.1 最终范围

1. **DF4 首批只新增显式 `edata`／AS8 静态对象的 EDATA 放置及零初始化闭环。** 普通 AS0 对象不因本批次自动迁移；用户通过显式限定解除单个大对象受低 128B DATA 窗口限制的问题。[D，本轮 PM 裁定]
2. **首批不实现按对象大小自动迁移，也不实现链接时 DSEG→EDATA 自动溢出。** 编译期阈值放置、默认 EDATA/XDATA、链接期 flex 迁移分别作为后续独立能力。[D]
3. **首批采用最小可执行的 ELF 对象协议 v2，不伪装 ELF v1。** [D]
4. **零初始化采用无 ROM 数据读取的连续清零路线。** CRT 在 EA=0、main 之前清零半开区间 `[0x100,H)`。[D，本轮最终裁定]
5. **保留向上增长栈。** EDATA 静态对象计入内部高水位 H，减少剩余栈容量；链接器必须保证剩余容量至少 1024B。[S09、D01 B.6.5]
6. **非零初始化等待经认证的 ROM 读取路线。** [S08、D02 §5.4、P]
7. **XDATA 单独实施、独立分配，不计内部栈 H。** 依赖 DF3 的 AS3 访存与指针语义。[D]

### 0.2 证据标记

| 标记 | 含义 |
|---|---|
| [Snn] | 源码亲核 |
| [W] | 工作树候选（未提交/未跟踪） |
| [D] | 设计契约 |
| [M] | 手册 |
| [P] | 待探针 |

### 0.3 基线身份

HEAD=`f1facb3afee15434feb9ebb082d4479e3a328df3`。lld/MCS251/、模型设计目录及若干契约实现仍处于未跟踪工作树状态；AsmPrinter、ISelLowering、TargetMachine 存在在飞修改。[S、W]

---

## 1. 当前差距及不能省略的前置条件

| 亲核事实 | DF4 必须补齐的内容 | 证据 |
|---|---|---|
| TargetObjectFile 对 mutable global 返回统一 DSEG | 统一对象分类器和独立对象 section | [S01] |
| AsmPrinter 绕过分类器，直接使用 `getDSEGSection()` | AsmPrinter 必须消费同一分类结果 | [S02:370–399，W] |
| AsmPrinter 拒绝非零 AS global definition | 有条件开放 AS8 定义；大小限制按批次和物理空间检查 | [S02:353–375，W] |
| lld DSEG 上限仍为 `0x80` | 新增独立 EDATA 窗口，不能扩大 DSEG | [S03:659–667，W] |
| lld 只接受固定 v1 note/header/reloc | 最小 v2 身份、对象／引用元数据与所需重定位 | [S03:263–303、390–416，W] |
| TargetMachine 禁止 v2 memory contract 输出 ELF | 对象协议接通后才能解除门禁 | [S05:84–90，W] |
| ELFStreamer 固定发射 v1 | 编译器 MC 层与 lld 同步实施 | [S06、S07] |
| 栈门禁由 `__mcs251_stack_base` 未定义引用触发 | v2 自启动固件强制执行 | [S03:740–756，W] |
| XINIT 六字节头，验证可因缺少 initializer 跳过 | 独立 v2 初始化协议及强制 provider | [S03:856–893，W] |
| CODE 布局只检查 24 位地址界限和重叠 | 新增 FLASH 物理窗口与排除区门禁 | [S03:543–585、S04] |

**显式 AS8 显著降低放置策略复杂度，但"新增 section＋NOBITS"仅完成存储占位，不完成对象身份、正确重定位、启动清零和运行期访问闭环。**[D]

---

## 2. 对象分类规则

### 2.1 分类维度必须分离

每个静态对象至少区分四个维度：① StorageAS ② ValueType/PointerValueAS ③ PlacementClass ④ InitKind。禁止由 section 名、最终地址数值或 AS0 指针宽度反推其他维度。[D01 B.5、D02 §5]

### 2.2 分类终表

| 对象语义 | 放置类别 | section | DF4 首批状态 |
|---|---|---|---|
| 显式 AS8 静态对象 | InternalExtended | `.mcs251.edata.*` | **新增开放零初值子集** |
| AS0 普通 mutable | LowOnlyGeneric | legacy 低页 | 保持低页，不自动迁移 |
| AS0 data-preferred（flex） | InternalMovable | `.mcs251.dseg.flex.*` | 后续 |
| 显式 AS1 strict direct RAM | DirectRequired | `.mcs251.dseg.direct.*` | 必须 <0x80 |
| AS2 idata | InternalIndirect | `.mcs251.idata.*` | 独立能力 |
| AS3 xdata | ExternalData | `.mcs251.xdata.*` | 后续 XDATA 批次 |
| AS4 CODE 常量 | CodeReadOnly | `.rodata.*` | 依赖 CODE 读取能力 |
| 位 backing/寄存器银行/overlay | 固定/受限 | 既有专用 | **不可迁移** |
| SFR/XFR 绝对声明 | 外设语义 | 受控符号 | 不参与普通 RAM 分配 |

### 2.3 首批 AS8 对象允许集

静态存储期、StorageAS=8、i8/i16/i32 标量及数组/结构体、全零初值、字节对齐、local/external linkage、无 TLS/COMMON/weak/COMDAT/ifunc/alias/自定义 section/固定地址、无指针初值/undef/poison/动态初始化、AS8 load/store/GEP 路径已通过 DF3/Tiny RC 验收。[D]

### 2.4 不可迁移对象

显式 AS1、bit backing、寄存器银行、OSEG/参数槽、绝对预留/noinit、显式 AS2/3/4/6/7/8、未支持自定义 section。[D]

### 2.5 可选增强：编译期阈值放置（后续批次）

默认关闭；阈值 T（32B 仅候选）；`AllocSize > T` 触发；仅作用于无显式约束的可间接安全访问 AS0 mutable 静态对象；放入 EDATA 但 StorageAS 仍 AS0；放不下链接失败不退回。阈值策略≠flex 策略≠XSmall 默认放置——三者分别记录。[D]

---

## 3. 编译器放置接口

### 3.1 单一分类结果 `GlobalPlacementDesc`

字段：StorageAS、PlacementClass、InitKind、Size、Alignment、是否只读运行副本、是否允许链接时迁移、必需访问/初始化能力、对象身份、放置原因。

分类优先级：拒绝不支持→识别受控绝对→保留显式 AS→处理普通 const 运行位置→对 AS0 应用已启用策略→校验能力。**不能只依据 `GV->isConstant()` 二分 ROM 与 DSEG。**[D01 B.5]

### 3.2 分工

| 层 | 责任 |
|---|---|
| Clang/IR | 保留声明 AS；传递数值放置策略；拒绝不支持的声明与类型 |
| TargetObjectFile | 根据分类结果选取准确 section/flags/type/身份 |
| AsmPrinter | 消费同一结果，发射符号/大小/NOBITS/初始化描述/元数据 |
| MC writer/streamer | 发射真实 v2 身份和严格 relocation |
| lld | 验证对象协议，执行物理分配与最终范围检查 |

AsmPrinter mutable 路径必须取消无条件 `getDSEGSection()`。[D；S01、S02]

---

## 4. 最小 v2 对象与引用协议

### 4.1 身份前置

ELF32/MSB/ET_REL/RELA、e_flags=0x00000102、`.mcs251.attributes` 主载体、v1/v2 不裸混链。**最小 v2 是缩小支持能力集合，不是省略必需身份字段。** v1 note 固定 52B 逐字段要求既定值，添加标记不兼容。[D01 N.1-N.9、S03:263-283]

### 4.2 逐对象最小语义记录

独立非 ALLOC 元数据载体；字段：record version/length、symbol-table index、section index/object offset、object size/alignment、StorageAS、PlacementClass、InitKind、group/flags。[D]

### 4.3 引用记录

relocation site、被引用符号、期望 AS/引用类别、指针表示宽度、引用用途、偏移/宽度约束。接通 `R_MCS251_PTR16`/`R_MCS251_PTR32`。AS4 CODE 数据指针仍受 CP-A 独立编号门禁。[D01 C.5、D01:1733-1738]

---

## 5. lld 分配算法

### 5.1 物理窗口

| 区域 | 窗口 |
|---|---|
| 低页 DATA | 与 [0,0x80) 交集 |
| ISEG | 内部间接窗口，首批不越 0x100 |
| G12 EDATA | [0x100,0x1000) |
| G144 EDATA | [0x100,0x4000) |
| XDATA | 独立声明可写区间，≤24 位 |

`--iram-size` 不重解释为 EDATA 大小。[D]

### 5.2 统一内部占用账本

DSEG/ISEG/EDATA/bit backing/寄存器银行/OSEG/绝对预留共用账本；XDATA/CODE 独立。overlay 按组包络计一次；有意 byte/bit alias 显式绑定同一物理所有者。[D]

### 5.3 首批放置顺序

校验身份→登记绝对预留→分配寄存器银行/bit/DSEG/OSEG/ISEG→AS8 对象 EDATA first-fit→后续 XDATA→计算 H/清零/栈门禁→CODE/初始化布局/重定位→全部成功才发布。[D]

### 5.4 first-fit 规则

地址递增查找连续空闲；对象整体放置不拆分；宽整数计算；窗口不足不退回其他空间。[D]

### 5.5 后续 flex 算法

低窗 first-fit→失败则 EDATA first-fit→两窗失败报错。迁移资格来自元数据不来自大小猜测。[D]

### 5.6 诊断与 map

失败报告对象/AS/类别/大小/对齐/尝试窗口/最大连续空洞/阻挡区。map 含 region/object/placement/internal/boot-clear/stack/code 记录。`l_DSEG` 不改为"内部 RAM 最末地址"。[D]

---

## 6. 零初始化闭环：连续清零 [0x100,H)

### 6.1 最终策略

CRT 在 EA=0、main 之前，纯 RAM 写循环连续清零 `[0x100,H)`。AS8 零初值对象用 NOBITS 占位。NOBITS 不是硬件上电清零保证。H=0x100 时清零长度为零。本策略清除对象间合法 RAM 空洞（显式启动策略）。本批不承诺保留 EDATA 上次启动内容。"不读 ROM"精确指不进行 ROM 数据读取（CPU 取指不在此排除）。[D]

### 6.2 绝对预留约束

`[0x100,H)` 必须完整落在板级允许启动清零的连续普通 RAM 中。与绝对预留/noinit/保留数据/不可用洞/MMIO/禁止清零区相交→链接失败。不静默擦除、不静默改分段清零。低于 0x100 的寄存器银行/位区/低 DATA 不在范围内。活动栈位于 First 及以上不在清零区。[D]

### 6.3 CRT 顺序

1. EA=0，板级最小启动
2. 安装链接器提供的 SPX
3. 无 ROM 数据读取的 EDATA 清零
4. 其他已认证低区初始化
5. 后续非零初始化（待 ROM reader 认证）
6. 调用 main
7. 中断启用由应用决定，不在清零尾部无条件 SETB EA

清零函数不依赖未初始化的静态参数槽/全局计数器/libc memset。边界用"起始地址+长度"。H 可能为 0x10000，不得截为 16 位零。[D]

### 6.4 低 DSEG 初始化不能遗忘

新 EDATA 循环不清 [0,0x100)。若发布范围含普通 AS0 零初始化对象，必须有无 ROM 读取的合法低区清零路径或已认证 provider。缺 provider 时拒绝镜像，不沿用"找不到 initializer 就跳过验证"的宽松路径。[D；S03:856-863]

### 6.5 清零时间预算

不承诺未实测微秒数。估算方法：`时间 = 清零字节数 × 实测平均每字节周期 / 时钟频率 + 固定启动开销`。必须记录时钟、循环机器码、清零长度、测量方法及 watchdog 影响。QEMU 耗时不替代真机周期结论。[D、P]

---

## 7. 非零初始化：XINIT v2 与 ROM 读取门

### 7.1 v2 记录

`u32 destination | u32 object_size | u32 payload_size | payload`，大端，destination 为 canonical 24 位。object_size>0、payload_size=0 或=object_size、完整目的属合法 RAM 对象、不得初始化 CODE/SFR/XFR/绝对预留。[D]

### 7.2 零清与复制关系

EDATA 连续清零先执行→非零对象从 ROM payload 覆盖→零对象不需再执行运行时清零表→XDATA 零初始化不被 EDATA 循环覆盖须独立路径。[D]

### 7.3 当前 XINIT 验证器缺口

无符号减法下溢路径（S03:883-886）。新验证顺序：验证身份→destination≥对象起点→offset≤对象大小→object_size≤剩余大小→边界/算术不溢出→完整对象初始化要求目的与对象完整范围匹配。[D]

### 7.4 ROM 路线与降级

现有 CRT 用 `mov wr*,@dr0`/`mov r14,@dr0` 读取 ROM（非直接 MOVC）。放行条件：指定运行环境 ROM 数据读取形式已认证（bank/连续读/跨 64K/端序/副作用）。未裁定时零清首批可独立放行，非零初值继续拒绝。[D、P]

### 7.5 FLASH_END 门禁

复制 payload/XINIT 头/CRT 清零代码/CODE 常量/HOME/VECS/BOOT/CSEG/NOBITS CODE 保留洞均纳入 CODE 完整区间检查。生产链 lld→ET_EXEC ELF→objcopy→HEX。[D01 C.10、D03]

---

## 8. XDATA 独立闭环与 DF3 边界

### 8.1 显式大数组路径

`xdata`→AS3→分类器 ExternalData→`.mcs251.xdata.*` NOBITS→lld XDATA 窗口分配→DF3 AS3 32 位 canonical 访存→XDATA 初始化 provider→map 显示 XDATA 占用不改变 H。[D]

### 8.2 lld XDATA 分配

明确可写物理窗口/排除区/起点/上界；保留 bank 不截 16 位；对象整体连续；独立账本但须排除物理映射别名；不强制 MOVX。[D]

### 8.3 DF3/DF4 分工

DF3：AS3/AS8 指针/GEP/cast/ABI/访存/memset 保留 AS/near-far 地址形成。DF4：静态对象分类/物理分配/section/对象引用关联/启动初始化 provider/relocation 范围验证/非法初始化拒绝。[D]

---

## 9. 栈水位与容量公式

### 9.1 方向裁定

源码 `TargetFrameLowering(StackGrowsUp, ...)`，CRT 按静态区上方安装 SPX。[S09、S08]

**以下作废**：栈从 EDATA_END 向下长；EDATA 变量不挤占栈容量；从 H 计算容量后再减 EDATA 字节数；将 guard 16B 加到容量上。[D]

### 9.2 公式

```
H        = max(0x100, 所有非空内部占用的 exclusive end)
First    = align_up(H, 16) + 16
SPX      = First - 1
Capacity = EDATA_END + 1 - First
```

条件：所有内部对象位于允许区域；`First <= EDATA_END+1`；`Capacity >= 1024`。H 含 DSEG/ISEG/EDATA/bit backing/寄存器银行/overlay/绝对预留；不含 CODE/XDATA。H 是地址高水位不是已分配字节数之和。[D01 B.6.5]

### 9.3 验收算例（公式推导非运行结果）

| 布局 | H | First | SPX | 结果 |
|---|---:|---:|---:|---|
| 无高位占用 | 0x100 | 0x110 | 0x10F | G12 剩余 3824B |
| 1000B 对象自 0x100 | 0x4E8 | 0x500 | 0x4FF | G12 剩余 2816B |
| 5091B 对象自 0x100 | 0x14E3 | 0x1500 | 0x14FF | G12 越界；G144 剩余 11008B |

5091B 显式 AS1 对象无论 EDATA 多大均失败。[D]

---

## 10. 与 Shizuku Tiny/XTiny RC 的接口

### 10.1 放置与指针宽度独立

| 对象 | Tiny/XTiny | Small/XSmall/Large |
|---|---:|---:|
| 普通 AS0 | 16 位 | 32 位 |
| 显式 AS8 | 16 位 | 16 位 |
| 显式 AS3 | 32 位 | 32 位 |

物理 EDATA 放置不改变宽度。[D01 B.2/B.3]

### 10.2 M8 const 接口

工作树已加入 Tiny 普通 AS0 const 拒绝。[S02:348-352、S13；W] DF4 接法：零清首批保持现有安全拒绝；非零复制完成后普通 near const 可有 RAM 运行副本；RAM 副本仍 AS0 不偷改 AS4；显式 AS4 const 进 CODE 但读取受 DF8 门控。[D]

### 10.3 文件所有权

| 文件 | Shizuku 线 | DF4 线 |
|---|---|---|
| AsmPrinter.cpp | 参数槽宽度/Tiny const 门禁 | global 分类/对象 section/初始化/元数据 |
| ISelLowering.cpp | near/far 指针/GEP/load/store/ABI | 原则不重写；仅审核迁移对象地址形成 |
| TargetMachine.cpp | DataLayout/执行域门禁 | v2 ELF 接通后受控放行 |
| TargetObjectFile.cpp | 无需改指针 ABI | DF4 分类与 section 主落点 |
| LinkerCore.cpp | 无 | DF4 布局/协议/初始化/栈门禁 |

共享文件按函数块交接；先冻结 Tiny RC 基线再合并 DF4。[D]

---

## 11. 实施批次与依赖

| 批次 | 内容 | 退出条件 |
|---|---|---|
| DF4-P0 | 冻结最小 v2 身份/子协议/对象引用记录/CRT 契约 | 无伪 v1；未知能力拒绝；最小 fixture 可解析 |
| DF4-L1 | 显式 EDATA section 读取/内部账本/窗口/H/栈/清零范围/map | host 正负测闭合 |
| DF4-C1 | 单一分类器/AS8 零对象/独立 NOBITS/严格符号引用 | C/IR→ELF 对象正确；旧 AS0 不迁移 |
| DF4-Z1 | v2 CRT/EA=0/连续清零 [0x100,H)/启动 provider 门禁/FLASH 窗口 | 脏 RAM 预置后真实清零/运行读写/栈哨兵通过 |
| DF4-R1 | 首批集成与重冻结 | 双板数值门禁/真实 C→ELF→HEX→运行证据；Tiny RC 回归 |
| DF4-X | 显式 AS3/XDATA 窗口与独立初始化 | DF3 AS3+24 位引用+零初值路径闭环 |
| DF4-I | 非零 XINIT v2/ROM reader/指针 payload/跨 bank | ROM 路线认证/记录验证/FLASH 门禁全通过 |
| DF4-A | 可选阈值放置 | T 边界/优先级/类型不变/初值门禁通过 |
| DF4-F | default-placement 与 flex 溢出迁移 | B.6 完整算法/不可拆分/5091B 回归 |

并行：P0 后 L1∥C1；Z1 清零循环可并行开发但等 H/身份/启动符号稳定后集成；DF3 AS3∥XDATA host 放置器；MOVC 探针不阻塞零清首批；阈值/flex 不阻塞首批。v2 身份未完成时不能包装成可发布固件链。[D]

---

## 12. 验收矩阵

### 12.1 编译器与对象

显式 AS8 的 1/32/128/129/1000/5091B 零对象；scalar/array/struct 含 padding；对象 AS 与内部指针值 AS 区分；普通 AS0 大对象首批仍低页失败；AS1 大对象即使 EDATA 充足仍失败；custom section/alias/TLS/COMMON/weak/COMDAT/非零 AS8 初值负测；Tiny/XTiny/Small 访问同一 AS8 均为 16 位；v2 header/attributes/对象记录缺失/冲突/未知能力负测；near 引用 bank 越界不被旧 R16 截断；显式请求未实现 default-placement 负测；O0/O2/Os 真实保留 AS8 读写。[D、P]

### 12.2 lld

G12/G144 窗口边界与 5091B 三分支；多对象整体分配/顺序/碎片/连续空洞不足；bit backing/寄存器银行/DSEG/ISEG/OSEG 与 EDATA 共存；overlay 同组计一次/不同组重叠拒绝；连续清零区含保留洞/noinit/绝对预留时拒绝；删除 `__mcs251_stack_base` 引用仍执行 v2 栈门禁；1024B 正例/1023B 负例/算术溢出负例；CODE/XINIT/清零代码越 FLASH_END/跨排除区/重复地址；XDATA 大对象不改变 H；XINIT 目的越界/跨对象/落 SFR/CODE/畸形；现有 XINIT 无符号下溢反例；失败不发布新固件。[D、P]

### 12.3 启动与运行

清零前预置非零 RAM 证明不依赖 QEMU 初始零；验证 [0x100,H) 全零及 H 以上 guard/栈不被清除；访问对象首/中/末字节/动态索引/跨低页边界（非对称数据模式）；清零期间 EA 状态/无隐式中断；SPX 精确恢复/main 正常/栈哨兵；后续复制测试覆盖 64K 进位/payload 端序/addend；G12 用 G144 QEMU 替代模型只证明该镜像运行不写成两板真机全覆盖。[D、P]

---

## 13. 风险与预算

### 13.1 风险

| 风险 | 判断 |
|---|---|
| ROM/MOVC 偏差 | 零清首批不必然阻塞；非零初值在无认证 reader 时是确定发布门。[P] |
| 最小 v2 基础设施 | **首要工期风险**；编译器和 lld 均有明确 v1/v2 门禁，不是改两个 section 分支即可。[S03、S05-S07] |
| lld EDATA 放置器 | 显式单窗 first-fit 低至中等复杂度；对象关联/范围/启动/栈检查占主要审核成本。[D] |
| bit/ISEG/overlay 冲突 | 显式 EDATA 从 0x100 起降低低区碰撞概率，但共享 H/固定预留/overlay 归并仍属高正确性风险。[D] |
| 连续清零覆盖保留区 | 高影响；必须链接拒绝保障。[D] |
| Tiny const/共享文件 | 中高集成风险；按函数块交接。[D] |
| XDATA bank/大尺寸 | 高影响；独立窗口/strict 引用/宽算术/跨 bank 探针。[P] |
| 清零耗时与 watchdog | 未知；按真实机器码测量。[P] |
| 在飞源码身份 | 实施与验收必须重冻结完整源身份和产物指纹。[D] |

### 13.2 人日估算

| 工作 | 估算 |
|---|---:|
| 显式 EDATA 分类+分配+map（v2 基础可复用） | 3–5 人日 |
| CRT 连续清零/范围门禁/集成正负测 | 3–5 人日 |
| 最小 v2 身份/对象引用/MC/lld 接线（从当前补齐） | 额外 7–12 人日 |
| 首批总量 | 约 13–22 人日 |
| 显式 XDATA+独立零初始化 | 额外 3–6 人日 |
| XINIT v2 非零复制（reader 认证后） | 额外 5–9 人日 |
| 阈值策略 | 基础设施稳定后额外 2–3 人日 |
| flex 自动溢出及完整默认放置 | 额外 3–5 人日 |

不可简单相加为自然日；并行可缩短墙钟但共享文件合入/协议冻结/硬件裁定属关键路径。[D]

---

## 14. 与既有设计的显式修订关系

| 编号 | 原契约 | 本稿处理 |
|---|---|---|
| R1 | B.1/B.6 完整默认与 flex | 保留最终方向；首批收窄为显式 AS8。[D] |
| R2 | §5.3 九项闭环 | 全部保留；通过分批能力门实现。[D] |
| R3 | B.7 零记录需 CRT 读取 | 首批改用连续可执行清零；无需运行时读取零记录。[D] |
| R4 | 逐对象选择性清零方案 | 作废，替换为 [0x100,H) 连续清零。[D] |
| R5 | "栈向下/容量不受影响" | 作废；沿用源码向上栈及 B.6.5 公式。[S09、D] |
| R6 | Tiny const 进 CSEG 口头表述 | 按 M8 安全修复理解。[S13、D] |
| R7 | v1 note 增加 EDATA 标记捷径 | 不进入主案；最小 v2 为发布前置。[S03、D] |
| R8 | "非零只等 MOVC" | 精确为等待经认证 ROM reader。[S08、D、P] |
| R9 | B.8 历史直接 HEX | 以 SPEC 修正案为准，输出 ET_EXEC+objcopy。[D03] |
| R10 | 方言旧稿 lowering 不消费 AS | 工作树已新增 AS 消费与拒绝门；视为候选结构。[S10、W] |

---

## 15. 开放问题表

| ID | 开放项 | 默认/建议 |
|---|---|---|
| O01 | 最小 v2 调用 ABI/函数契约/子协议终数值 | 沿 N.5 登记；未冻结不得发射伪支持。[D] |
| O02 | placement/reference 元数据线格式 | 按本稿字段冻结；yaml2obj 畸形输入验收。[D] |
| O03 | 首批板级连续可清零区域声明 | 显式证明 [0x100,H) 合法；遇保留洞硬拒绝。[D] |
| O04 | 普通 AS0 低区初始化与 v2 CRT 共存 | 首批最小镜像可无低区应用全局；扩大前补 provider。[D] |
| O05 | ROM reader 具体形式 | MOVC/@DR 分形式分环境裁定。[P] |
| O06 | XDATA 实际可写窗口与启动条件 | 板级提供数值内存图。[D、P] |
| O07 | 阈值 T 与 CLI 名称 | 默认关闭；32B 仅候选。[D] |
| O08 | AS4 CODE 数据指针 relocation CP-A 编号 | 继续保留未批准编号。[D] |
| O09 | 大于 64K 对象/任意对齐 | 首批不顺带开放。[D、P] |
| O10 | v1 扩展替代路线 | 未闭合非主案；需独立修订 SPEC。[D] |
| O11 | 清零性能与 watchdog | 真实机器码/时钟/板级测量。[P] |
| O12 | 完整镜像栈需求 | 1024B 是下限不是调用深度证明。[D、P] |

---

## 16. 证据索引

| 编号 | 位置 |
|---|---|
| D01 | DESIGN.md B.1-B.7/C.5/C.10/N.1-N.9/F.5 |
| D02 | DIALECT-FRONTEND-DESIGN.md §5/§6/§7.7/§12 |
| D03 | SPEC.md 2026-09-06 输出修正案及运行时身份/CRT/overlay 条款 |
| S01 | MCS251TargetObjectFile.cpp:17-31 |
| S02 | MCS251AsmPrinter.cpp:114-187,206-257,300-315,328-433 [W] |
| S03 | lld/MCS251/LinkerCore.cpp:110-416,506-757,776-893,919-953 [W] |
| S04 | lld/MCS251/Driver.cpp:81-177,219-277; LinkerCore.h:23-38 [W] |
| S05 | MCS251TargetMachine.cpp:38-54,78-90,132-139 [W] |
| S06 | MCS251ELFStreamer.cpp:26-43 |
| S07 | MCS251ELFObjectWriter.cpp:27-56 |
| S08 | validation/mcs251-firmware/crt-selfstart.asm:66-125; crt-selfstart.yaml:122-127,211-250 |
| S09 | MCS251FrameLowering.cpp:1-43,76-139 |
| S10 | MCS251ISelLowering.cpp:1399-1587,1626-1775 [W] |
| S11 | clang/lib/CodeGen/TargetInfo.cpp:139-144; CodeGenModule.cpp:6321-6359 |
| S12 | boards/stc32g12k128.mk:1-16; stc32g144k246.mk:1-16 |
| S13 | llvm/test/CodeGen/MCS251/pointer16-error-constant-placement.ll:1-15 |
| S14 | MCS251TargetParser.h:37-45; MCS251TargetParser.cpp:78-92 [W] |

---

## 17. 最终放行原则

> 在具有合法连续可清零 EDATA 窗口的板级配置上，显式 AS8 零初始化静态对象能够经真实 v2 ELF、lld 独立 EDATA 分配、严格地址引用及 CRT `[0x100,H)` 清零后正确运行；所有内部占用计入向上栈高水位，剩余容量不足 1024B 时链接拒绝。连续清零区与绝对预留、noinit、保留数据或不可用洞相交时，同样链接拒绝。

这解除**显式大变量必须挤入低 128B DATA**的实际限制，但不等于自动迁移、完整 XSmall/Large、XDATA 初始化、CODE 数据读取或非零 XINIT 已全部实现。各项能力按批次分别验收、分别宣布完成。[D]
