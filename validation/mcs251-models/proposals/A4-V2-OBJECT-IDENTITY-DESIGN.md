# A4 正式实施设计：v2 对象身份发布与静态指针参数槽

- 状态：已实施（W1-W8，见 §11）；开放值与两条 PM 裁定（2026-09-13 #1/#2）见文末裁定记录
- 日期：2026-09-12
- 适用分支：`minimal-isr`，调查 HEAD `36d82d197`
- 唯一目标：为 32 位 v2（Small/XSmall，首期验收以 XSmall 为主）发布“含后续指针静态槽”的最小 ELF 对象身份，并使 lld、v2 CRT 和验收链闭合。

## 1. 范围与不变量

静态槽 lowering、caller 写槽、callee 读槽、D.5 槽宽、大端序列化、leaf OSEG/non-leaf DSEG 已存在且 asm 实测通过；A4 不重写这些路径。`AllowStaticPointers` 继续严格等于 `ProgramAS == 4`（`llvm/lib/Target/MCS251/MCS251ISelLowering.cpp` 3137、3313 附近），所以 v1 `1,1,32,8,1` 仍拒绝第二参数起的指针。

A4 只打开：32 位 v2 对象身份、属性生产发射、身份门禁分流、lld v2 校验、v2 CRT 和 demo 链。Tiny/XTiny 的 16 位可重定位对象闸保持拒绝；AS5/6/7/>9、varargs、struct、i64/f64、首期多参数间接调用、静态 `addrspacecast` 初值、overlay 重入/递归/ISR 交错限制均不因 A4 放宽。82 的前端 AS3→AS0 隐式转换另立切片。

## 2. 开放值登记提案（PM 仲裁载体）

`llvm/include/llvm/BinaryFormat/MCS251Attributes.h:159-171` 的 `RequiredTags` 仍是 21 个必需 Tag；“candidate”不是已批准值，生产发射不得自行采用。以下是本提案推荐值，仅供 PM 一次性仲裁。

| Tag/字段 | 推荐值 | 推荐理由 |
|---|---:|---|
| ObjectProtocolVersion | 2（已裁定） | 与 `e_flags` 低字节 2 一致。|
| CallABIMajor | 2 | v2 已改变后续指针参数规则，不能伪装为 v1 ABI。|
| **CallABIMinor** | **1** | 推荐由此字段明确表达“D.5 指针静态槽”能力，避免能力缺省推断；minor 必须精确匹配。|
| RegisterParameterVariant | 3 | 表示 D.2 首个合格参数寄存器序列加 D.5 静态槽，不重解释 v1 值 2。|
| GeneralRegisterSet | `0xf3ff`（已裁定） | 既有寄存器集合。|
| IntBits/LongBits | 32/32（已裁定） | 当前 v2 目标契约。|
| AS0PointerBits | 实际值 32（16/32 域已裁定） | A4 对象范围仅 32 位 AS0。|
| ASLayoutVersion | **2** | 与 B.3/F.5 v2 地址空间布局接口一致；必须与完整布局及 AS0 宽度核对。|
| DefaultPlacement | 8（XSmall；1/3/8 域已裁定） | 当前四 demo 和默认 xsmall 的 InternalExtended。Small 使用 1，Large 使用 3。|
| InitProtocolVersion | 2 | v2 属性身份与 v2 自启动 CRT 配套；A4 不开放新静态指针初值。|
| PlacementProtocolVersion | 2 | 保持 OSEG/DSEG/overlay 现有分配协议的 v2 版本标识。|
| StackContractVersion | 2 | 与 v2 CRT 的栈门禁相配；不能用 v1 按符号请求语义。|
| FunctionContractVersion | 2 | 为 D.5 完整签名、槽序、CC、属性/site 校验预留明确 v2 代际。|
| RequiredCapabilitiesLo | **0**（若采用 CallABIMinor=1） | A4 必需能力由 ABI minor 表达；未登记位必须为零。若 PM 改采能力位方案，则 bit0 定义为 static-pointer-slots，值为 1，不能两套语义含糊并用。|
| RequiredCapabilitiesHi | 0 | A4 不需要高位能力；未分配位必须为零。|
| ABIOptions | 采用登记的“无额外选项”值 **0** | 不将 static-slots 重复编码；未知位拒绝，不能按位 OR。|
| Reserved0/1/2 | 省略 | 若出现必须为零；不属于最小登记集。|
| MemoryModelProfile | `(32,8)` | 与 AS0=32、XSmall placement=8 一致；不是第二套模型枚举。|
| CodeModelProfile | **推荐 1（Near16/内部默认代码模型）** | A4 demo 使用既有默认返回帧、调用/跳转策略；数值需 PM 与 C.2 档位表最终对号，不能由实现者单凭表行号定值。|
| CodePointerBits | 32（已裁定） | AS4 程序指针容器为 4B。|
| ObjectProtocolMinor | **0** | 首期格式无新增对象级语义；新 Tag 不改变 major/minor。|

### 2.1 推荐的能力表达裁定

本设计推荐 **`CallABIMinor=1` 表达“含指针静态槽”**，而不是另造 `RequiredCapabilitiesLo bit0`。原因是本片改变的是调用 ABI 的可接受签名与序列化规则，且每个 v2 对象必须有 Call ABI 字段；把同一事实同时放在 minor 和 capability 会制造不一致组合。若 PM 认为能力位必须独立可组合，则应反向裁定 `CallABIMinor=0`、`RequiredCapabilitiesLo bit0=1`，并在协议表中明确 bit0；不得让实现者自行选择。

### 2.2 A4 最小登记集

合法 A4 XSmall 对象登记：Tag 4–20（按上表推荐值）、Tag 24、25、26、27；其中 Tag 21–23 省略。也就是 21 个 RequiredTags 全部发出，不能因某字段本片未使用而省略；但只有上述 A4 能力组合获 PM 批准后才可生产。Small 仅将 Tag 13 和 Tag 24 的 placement 改为 1；不同 placement 可合法共存。其他候选模型、future capability、静态初值协议和 16 位对象不发射。

## 3. v2 属性发射

### 3.1 MCELFStreamer 公共入口

扩展 `MCELFStreamer::createAttributesSection` 的公共能力，新增“已确定长度的自描述属性体”入口：调用方传入 vendor `MCS251` 和完整 `[Tag][TypeFlags][Length][Value]` 字节串，由公共逻辑仅负责 ARM attributes 信封。既有 ARM/GNU `AttributeItem` 调用路径、字节、长度计算和接口语义必须完全不变；不得把 MCS251 Type/Length 伪装成 ARM 属性，也不得以字符串内嵌 NUL 携带二进制。

信封必须是：`0x41`；大端 `VendorSize=16+P`；七字节 `MCS251\0`；scope tag `1`；大端 `ScopeSize=5+P`；后接 P 字节属性体。section 为 `.mcs251.attributes`、SHT `0x70000003`、非 ALLOC、align=1、entsize/link/info 均 0；ULEB 最短、记录严格递增、无填充。

### 3.2 MCS251ELFStreamer 分流

v2 且通过 `classifyModule` 白名单时，`MCS251ELFStreamer` 发 `e_flags=0x00000102`、恰好一个 `.mcs251.attributes`，并停发 `.note.mcs251.abi`。v2 不得附带一份“骗过”旧读取器的 v1 note。v1 路径维持 `e_flags=0x1`、原 52B note、原 section/对齐和所有调用方行为。

属性编解码器由 `MCS251Attributes` 生成正式生产调用；发射前校验必需 Tag 恰好一次、Critical=1、类型和值均为已批准组合，不能发 candidate/隐式零值。

### 3.3 门禁白名单

`classifyModule` 先保留现有 v1 判断和 v2 降级扫描。若模块 v1-compatible，走原 v1 发射；若因后续指针静态槽成为 v2-only，仅在以下白名单内走 v2：32 位 AS0、D.5 已定义 AS（0/1/2/3/4/8/9）静态槽，以及已存在且可降级为同一 v2 身份的能力。alias/ifunc、非登记指针能力、静态指针初值、其他 v2-only 协议继续 fatal，诊断明确说明未登记能力。16 位对象总闸（`MCS251TargetMachine.cpp:238-243`）不动。`isV1ObjectCompatible` 不得伪装新 ABI；它只决定是否需要 v2 分支。

## 4. lld v2 身份分支

`lld/MCS251/LinkerCore.cpp` 先按 `e_flags` 分流：

1. `0x1`：原 v1 note 检查逐字节保持不动。
2. `0x102`：要求恰好一个 attributes section，解 ARM 信封和 MCS251 TLV，校验 ELF class/endian/machine/type、长度、vendor/scope、ULEB、重复/缺失/未知 Critical、Tag 4 与 header 一致。
3. 其他 flags：拒绝。

v2 逐字段校验：所有 RequiredTags 出现一次且 Critical；已批准值等值；Tag 11/13/24 三者内部一致；AS 布局和实际符号/site 信息一致；未分配 capability/ABI option 位为零且读取器支持。跨对象不得整体 `memcmp` 属性体或 descriptor：Tag 4–12、14–20、25–27 等 ABI/协议兼容字段按规则精确相等；Tag 13 `default_placement`、Tag 24 placement 分量允许不同，只要每对象 placement 约束可同时满足；能力字段按并集检查支持性；模型内其他约束逐字段核对。

v1/v2 混链一律拒绝，v2 缺 attributes 不回退 v1 note，v1 不从 v2 attributes 补身份。OSEG/DSEG 分配零改动：lld 已按 section 名和 flags 分类 OSEG overlay、DSEG 窗口，并对槽符号按既有规则分配；指针槽与整数槽同 section、同尺寸/重定位机制，身份校验不改变地址分配算法。

## 5. v1 字节不变保证

以下必须作为黄金指纹回归，修改 A4 前后逐字节比较：

- `llvm/test/CodeGen/MCS251/oseg-errors.ll`：v1 pointer-formal、pointer-call 仍报原文；varargs/struct/间接调用拒绝钉子不变。
- `llvm/test/CodeGen/MCS251/elf-oseg.ll`：OSEG/DSEG section、overlay flags、槽符号绑定/size、既有 relocation 位置不变。
- `llvm/test/CodeGen/MCS251/asxxxx-obj.ll`：REL/ASxxxx 输出字节、area、`.ds` 和符号顺序不变。
- `llvm/test/CodeGen/MCS251/call-error-indirect.ll`：多参数间接调用诊断不变。
- v1 ELF fixtures：`e_flags=0x1`；`.note.mcs251.abi` 的 namesz/descsz/type、八个 BE32 字段、section type/align/size 不变。
- `--file-headers`、`--sections`、`--relocations` 黄金输出及 relocation 0–8 位置/截取/下一 PC 语义不变；既有 QEMU/真机固件产物指纹不变。

## 6. v2 CRT、demo 与边界验收

新增 `validation/mcs251-elf/runtime/crt-selfstart-v2.yaml`，从 v1 fixture 保留启动顺序、向量、栈初始化、段边界和入口，仅替换身份为 `e_flags: 0x102` 与完整 `.mcs251.attributes` 原始 Content；不得附 v1 note。所有带 ISR 链的启动 fixture（包括 `crt-irq`）必须有等价 v2 变体，ISR 向量、保存恢复寄存器、栈契约和入口函数契约字段与 v2 属性一致。生成脚本应能同时产生 v1 与 v2，v1 fixture 不改。

41/42/43 迁移为 `1,2,32,8,1`，验收四档分开记录：

1. clang 编译通过；
2. llc `-filetype=obj` 发 v2 flags/attributes；
3. lld + v2 CRT 链接通过，并验证 map 中 OSEG/DSEG；
4. QEMU `stc32g144k246` 运行窗口通过，输出/字节断言与现有基线对齐。

“契约迁移”明确为 `1,1,32,8,1 → 1,2,32,8,1`，不是仅替换 CRT。G3 七 demo（19、21、25、26、40、47、62）按同一口径重跑 `tools/drive.py`，分别记录 clang、llc、lld、QEMU 四档及 T1/T2，不把 asm 成功冒充 obj/link 成功。82 只验显式 AS3→AS0 cast 的后端第二指针最小用例；其前端 AS3→AS0 隐式转换明确排除。

## 7. libc 迁移前置条件

libc 不属于 A4 实现。A4 只冻结三个前置条件：

- v2 属性对象、lld v2 分支和 v2 CRT 已通过；
- libc、所有消费者、CRT、a3-priv-fw 和 rt-acceptance 必须整体迁 v2，禁止把带真实双指针签名的 libc 对象与 v1 固件裸混链；
- 后续独立 S1（删除 setter/global 槽并恢复真实双指针声明）、S2（消费者与字节断言）、S3（重入/ISR 交错回归）逐提交实施；不在 A4 修改 `validation/mcs251-runtime/src/`。

## 8. 测试矩阵定稿

### 8.1 CodeGen P1–P10

- P1：v2 XSmall 第 2 参 AS0，asm/obj 均 A；CHECK `.ds 4`、global `PARM_2`、STT_OBJECT、size 4、v2 flags/attributes。
- P2：AS4 实参 cast 到 AS0 的第 3 参；CHECK canonical 高字节清零、槽写序列和 obj 身份。
- P3：AS4 直接形参；CHECK 4B 槽及 CODE bank 地址值。
- P4：AS3/AS9；CHECK 4B 槽及 bank 保留。
- P5：AS1/2/8；CHECK 2B load/store、无 canonicalize。
- P6：`int,ptr,int,ptr`；CHECK 槽连续、宽度 4/2/1/4、leaf OSEG。
- P7：non-leaf；CHECK DSEG、无 OVERLAY。
- P8：跨 TU；CHECK caller undefined-global/definition global、取函数地址不生成槽依赖。
- P9：首参 AS0/AS4；v1/v2 均 A，寄存器通道回归。
- P10：Tiny asm A（AS0 2B、AS4 4B），obj 仍 R，精确 16 位对象诊断。

### 8.2 负例 N1–N8

- N1：v1 asm/obj formal/call 第 2 参指针，`FileCheck` 精确匹配 `static pointer parameters are not supported by the compatibility ABI`。
- N2：v2 AS5 手写 IR，分别覆盖契约 verifier、ISel、AsmPrinter 的 `unsupported pointer address space 5`/`no ordinary register/static-slot ABI`。
- N3：AS6/7/>9，同 N2 三层 fail-closed。
- N4：v2 多参数间接调用，匹配 `multi-argument indirect calls are not supported`。
- N5：varargs/struct/i64/f64，复用并固定既有 oseg-errors 诊断。
- N6：16 位 v2 obj，匹配 `16-bit pointer ABI cannot emit relocatable objects`。
- N7：alias/ifunc、未登记 v2 能力或旧工具读取 v2，匹配 v1 identity/unsupported-capability fatal；合法指针槽不再误报。
- N8：静态 AS4 `addrspacecast` 初值，v1/v2 obj 均保持 X3 叶子拒绝，不生成 CP-A relocation。

### 8.3 lld 身份组

lit/readobj 断言：v2 flags 为 `0x102`、section 恰一个且 type/align 正确、无 v1 note、逐 Tag 名称/critical/type/length/value 正确；缺失/重复/未知 Critical/畸形 ULEB/尾随字节均拒绝。v1 fixture 做 sections/relocations/header 黄金比较。v1+v2 混链拒绝；同 ABI 不同 placement 接受；不兼容 ABI 字段拒绝；能力并集未知位拒绝；两个 OSEG leaf 的 map 地址复用断言保留。

### 8.4 demo 组

外部验收不入 lit：41/42/43 与 G3 七项各自生成四档报告；报告必须列契约、CRT 代、flags、attributes 摘要、lld 结果、QEMU 窗口。82 仅列后端显式 cast；不计入完整 demo 通过。

## 9. 切片验收表

| 项 | 完成判据 |
|---|---|
| W1 | PM 批准开放值、static-slot 表达字段、最小登记集；无 candidate 进入生产。|
| W2 | MCELFStreamer 自描述入口通过 ARM/GNU 不变回归；MCS251 信封字节与 N.3 一致。|
| W3 | v2 白名单分流可发对象；v1 与 16 位闸、超范围 fatal 保持。|
| W4 | lld v2 解码、逐字段校验、placement 合法差异、能力并集及 v1/v2 混链负例通过；OSEG/DSEG 算法无改动。|
| W5 | llvm-readobj 逐 Tag 展示；YAML/raw fixture、畸形和未知 Critical 负例通过。|
| W6 | P1–P10、N1–N8、lld 身份组 lit 全通过；v1 黄金指纹一致。|
| W7 | v2 self-start 与 ISR CRT 生成；41/42/43 四档及 G3 四档跑批完成，QEMU 窗口通过。|
| W8 | 本设计、范围边界、82 排除、libc S1–S3 前置条件和 v1 不变承诺经 PM 审核；未授权直接改源。|

## 10. PM 拍板清单（逐项推荐）

1. **开放值与能力表达**：推荐 CallABIMajor=2、CallABIMinor=1、RegisterParameterVariant=3、ASLayoutVersion=2、Init/Placement/Stack/Function contract=2、ObjectProtocolMinor=0、XSmall CodeModelProfile=1、Capabilities Lo/Hi=0、ABIOptions=0；推荐 CallABIMinor=1 唯一表达指针静态槽。批准后才可生产。
2. **范围**：推荐 A4 仅 32 位 AS0 v2（Small/XSmall，首期 demo XSmall）；Tiny/XTiny 对象拒绝保留。
3. **契约迁移**：推荐 41/42/43 及 G3 随 A4 执行 `1,1,32,8,1→1,2,32,8,1`，否则无法完成真实 obj/link 验收。
4. **82**：推荐 AS3→AS0 隐式转换另立切片；A4 只收显式 cast 后端半。
5. **libc**：推荐 A4 合并后按 S1→S2→S3 独立提交；A4 不改 libc 签名、不混入 setter 删除或消费者迁移。

以上是设计冻结提案，不是开放值的最终定值，也不授权修改源码。

## 11. 实施状态与后续缺口登记（W8，2026-09-13）

### 11.1 实施状态（worktree a4-v2-identity，全部未提交待终审合并）

| 卡 | 状态 | 验收 |
|---|---|---|
| W1 开放值登记 | 完成 | 单测 37/37；candidate 未入生产 |
| W2 attributes 发射 | 完成（协调员验证） | 172B 信封逐字节对冻结 hex；v1 note/flags 逐字节不变；ARM/GNU 公共入口零影响（readobj 152/152） |
| W3+W3b 门禁 | 完成（W3b 按裁定 #2 改为身份=契约代） | CodeGen 148/148；REL/asm 边界不变；显式 v1 契约三重逐字节对拍（含 W4 冻结指纹 5328bf4d…b1c53） |
| W4 lld v2 分支 | 完成（Alice 三轮 APPROVED） | lld 21/21；混链双向拒绝；短路残留排查零发现 |
| W5 readobj | 完成 | 逐 Tag 展示 + 畸形负例 |
| W7/W7b CRT+验收 | 完成 | v2 CRT 双变体+1068 行独立 checker；四档矩阵 T3=5 PASS/0 GAP/5 BLOCKED（19/21/25/26/40 闭合） |

**v1 状态**：按裁定 #2 标记 **deprecated**——仅显式契约 `1,1,32,8,1` 下产生，供既有资产重建；默认路径全 v2。

### 11.2 本役揭开的后续缺口（独立登记，非 A4 回归，均经"基线 llc 同 IR 复现"验证）

| 缺口 | 现象 | 受阻 demo | 备注 |
|---|---|---|---|
| **br_jt 跳转表 ISel** | switch 跳转表 `Cannot select:`（1,2 完整 DataLayout 揭开；基线对拍证据为 asm 模式同报，ELF 模式基线先死于旧 v2 身份门——终审 R2 口径） | 43、62 | 后端既有缺口，需独立切片 |
| **AS4 聚合常量嵌套 struct 初始化器** | gui.c `asc2_1206` 为 AS4 非零聚合常量（`{ [12 x i8] }`，**无指针叶子**）——只读初始化器不支持该嵌套形态；非 CP-A 指针代数（终审 R2 核实物证修正）。证据：约简 IR 同契约同 ELF 模式下 A4 与基线 llc 同报 `unsupported initializer`（`font-initializer-only.ll` + `font.{a4,baseline}.log`，a4-final-review-evidence/） | 41、42、43 | 独立切片（只读初始化器形态扩展）或语料改写 |
| variadic（=G2 在册） | 41 test.c、47 | 既有缺口 |
| SFR 位寻址（=G6 族） | 42 LCD.c `base SFR not bit-addressable` | 既有缺口 |
| G1 槽 13 隔离 | 41 的 LCM ISR 待 G1-4 精确改写 | 既有缺口（D7 已批） |

### 11.3 边界公布（不变承诺重申）

82 排除（前端 AS3→AS0 隐式转换另立切片）；libc S1-S3 为 A4 合并后独立提交（裁定后默认契约路径已统一 v2，迁移硬前提"整体迁 v2"自然成立）；16 位对象闸保留；静态指针初值代数（CP-A）继续 fail-closed；overlay 重入/ISR 交错限制照旧文档化。

> **PM 裁定记录（2026-09-13）**：用户裁定"暂时按照推荐设计"——§10 五项全部按推荐执行：开放值按 §2 表推荐值集登记（CallABIMajor=2、CallABIMinor=1 唯一表达指针静态槽、RegisterParameterVariant=3、ASLayoutVersion=2、四个协议版本=2、ObjectProtocolMinor=0、CodeModelProfile=1、Capabilities Lo/Hi=0、ABIOptions=0）；范围仅 32 位 AS0 v2（首期 XSmall）；41/42/43+G3 随 A4 迁契约 1,1,32,8,1→1,2,32,8,1；82 的 AS3→AS0 隐式转换另立切片；libc 按 S1-S3 于 A4 后独立提交。"暂时"意为后续可依实据复议。

> **PM 裁定记录（2026-09-13 #2，W7 身份分裂问题的裁定）**：用户裁定 **"V1 标记为过时，默认值只考虑 V2"**。操作含义：**ELF 对象身份 = 契约代，不再按模块内容自动判代**——契约 ASLayoutVersion=2 的 ELF 对象输出一律发 v2 身份（e_flags=0x102 + .mcs251.attributes），删除"可降级模块降回 v1"的默认路径；v1 身份仅在显式 v1 契约（`1,1,32,8,1`）下产生，供既有资产（29 demo 链、crt.o、运行时、真机验收）重建使用，**文档层面标记过时（deprecated）**。边界精确化：REL/asm 输出无身份载体，维持现状（内容含 v2-only 能力→fatal；可降级内容照常出 REL，asxxxx-obj 等不受影响）。此裁定取代 W7 报告登记的后续选项 (a)（force 开关）——不需要新开关，默认行为即统一 v2。本条修订 §3.3 的"v1-compatible 走原 v1 发射"：该规则仅适用于显式 v1 契约。lld §4 混链拒绝规则不变。