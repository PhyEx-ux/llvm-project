# G11：`_at_` 绝对地址放置（切片级实施设计，修订 7）

**日期**：2026-09-16（修订 7，Alice；仅处理六审 B1/B2、N1/N2 与两项文字纠错；PM-4/5/6 单一方案、Synth 封闭枚举、实体计数边界、RETAIN 双向互斥四项已通过裁定保持不变）
**修订 7 记录**：依据 `/home/liu/LLVM_STC32/G11-DESIGN-REVIEW-R6-Alice.md`：B1 区分原记录完整性 hash 与合并 report hash，唯一校验职责归 verifier，hash 不参与链接决策；B2 统一 mergePlacement 前置的诊断优先级与 fixture；N1 登记 G13a/G13b 交叠及开工前门禁（§4.1）；N2 登记 probe4/5 强断言改造义务与验收禁用条件（§9）；同步实体计数摘要、reserveCode 定义引用、§10/§11。以下修订 3–6 说明仅保留历史；本轮修订待定点复审，不代表实现或验收通过。
**状态**：PM 方向的可实施设计稿；不授权产品代码提交。**评审口径（Alice 二审原话）："已闭合=设计评审闭合，不代表功能已实现"**——本稿所有"已裁定/已冻结"均指设计评审层面，实现状态一律按 §11 实现列（PLANNED→切片号）陈述。
**修订说明**：修订 3 已裁定 N1（台账接入）、N6（探针三档）、N8（7E/7F 勘误、demo 85 划归 G13b），本轮不动其裁定。修订 4 裁定三审剩余各项：重定位层改动面补齐与 FIXED-XDATA 截断处置（N2）、report/symtab 联动冻结与 bind-only 输出符号路径（N3）、owned↔bind 字段语义合并 / 函数实体跨度与跳表 / NOTE.size 生成路径 / namesz / bind-only 调用序与数值源（N4）、keepalive 分类前移控制流与 retain/bind 唯一裁定（N5）、诊断可达 fixture 与归属边界（N7）、PM 决策点改为"提案 + 依赖合约状态"（§6）。修订 5 处理四审各项：keepalive 身份普查落点文件纠错与 AsmPrinter 行号刷新（§3.2）、全部未来插入点改"当前不存在；G11-C 于 … 插入"句式（§1.3/§3.3）、行号基准改 HEAD+抽查日期（稿首）、probe5 [T0] 基线实测刷新（§9）、补齐三个设计级裁定——Synth 输出符号命名字段与 collectSymbols 排序/撞名规则、layoutFixedCode 碰撞检查落点=reserveCode 统一 CODE 台账（verifier 只复核）、节内多实体的 emitter 侧跨度归属约束（§3.2/§3.3）；§11 改"设计裁定状态/实现状态"双列，消除"闭合"歧义。修订 6 按五审"设计稿必须给出唯一答案"标准做四项唯一化：(1) §6.4/§6.5/§6.6 三个 PM 决策点收敛为单一方案——采纳 placement report + symtab 通道、retain 仅限 place_at 定义、强制显式 CODE 窗，删除"输出 ELF 内嵌 audit 节"与"型号预设窗口注入"两个备选，统一句式"设计采纳单一方案；PM 保留实施前否决权（veto window：G11-A 开工前），否决则该项回炉重设计"，§4/§11 同步改口径；(2) §3.3 Synth 撞名规则唯一化——"既有 Synth 键"由举例式表述冻结为封闭枚举（`Synth` map 构造点 ：3079-3236 全集 + `isReservedBoundarySymbol` 保留名 ：1596-1618，经 grep 实测列入稿内），写明 :1628（输入保留名检查）与 :4522-4524（输出键空间）职责分工，collectSymbols 前对 defined 集合与完整集合做不相交检查，任何遗漏=链接失败；(3) §3.2 节内实体计数边界冻结——凡 defined binding 且 STT_FUNC/STT_OBJECT，无论 st_size 是否为 0、绑定属性如何，一律计入实体计数，计数 ≠ 1 即拒；(4) F1 增补 RETAIN 双向互斥三条统一裁定，§8 增补"多余 retain 位"/"节名与 NOTE flags 不一致"两条冻结文案。
**行号基准**：**HEAD `032c90d66`（2026-09-16 逐行抽查）**。文件前缀映射：`Core:` = `lld/MCS251/LinkerCore.cpp`；`Printer:` = `llvm/lib/Target/MCS251/MCS251AsmPrinter.cpp`；`BasePrinter:` = `llvm/lib/CodeGen/AsmPrinter/AsmPrinter.cpp`；`Driver:` = `lld/MCS251/Driver.cpp`；`SemaType:` = `clang/lib/Sema/SemaType.cpp`；`CodeGenModule:` = `clang/lib/CodeGen/CodeGenModule.cpp`。修订 5 全稿引用按该 HEAD 重核；HEAD 上 P-4 提交（K&R 签名校验）使 `Core:` 的 run() 签名验证区后移（validateIdentitySet 调用 ：4563-4564、签名/ISR 区 ：4575-4587、layout :4588、checkFlashGate :4594-4595）；F9-F13 各行号（:3424-3440/:3513-3532/:3548-3557/:3559-3561/:3567-3569、F13=:3196-3211）经抽查与修订 4 一致，保持。设计中的新增函数（mergePlacement/layoutFixed*/placementTargetClass/classifyMCS251KeepaliveMember/validatePlacementResolution）**当前工作树均不存在**，其行号一律以"插入点（当前不存在；G11-C/G11-B 于 … 处插入）"陈述。探针：`/home/liu/LLVM_STC32/GAP-G11-PROBES/`（`RESULTS-REVISION.txt` 为修订 3 重跑输出，[SRC]/[DEMO]/[TREE] 三档；probe4/5 为设计轮 PLANNED fixture，见 §9）。

## §0 结论与裁定摘要

保留固定地址，拆为四个正交机制：

1. **地址空间限定符**（修订 2 已裁定，维持）：正式拼写 **`__attribute__((address_space(3)))`**；`__addrspace(xdata)` 不存在（探针 1[1]），从设计中除名。与 `__xdata` 同 `HandleAddressSpaceTypeAttribute`（SemaType.cpp:6723/:9241-9246）、同 LangAS（`getLangASFromTargetAS(3)`）。探针 1[5]（双向 + typeof 两侧）实测互赋 **0 个 addrspacecast**。不新增 LangAS/LLVM AS。`address_space(N)` 的 N 不在布局 `Pointers` 表内 → 新 Sema 诊断 `err_mcs251_address_space_unavailable`（探针 1[4] 实测今天由后端 fatal 兜底）。
2. `[[mcu::place_at(A)]]`（OwnedDefinition）/`[[mcu::bind_at(A)]]`（ExternalEntity）：不变；C11 用 GNU 拼写 `mcu_place_at(A)`、`mcu_bind_at(A)`、`mcu_retain`。
3. **retain**（N5 修订 4 收敛）：本链接器**无 GC、无归档提取**。链接端 RETAIN = **白名单接受 + 约束声明 + 未来兼容约定**。编译端 keepalive 经 `RetainAttr → addUsedGlobal`（CodeGenModule.cpp:3521-3522/:3534-3535）**保留不变**；G11-B 新增**按成员分类的 placement keepalive root 结构验收**（§3.2-N5，分类前移到旧硬失败之前，单点判定多处消费），并把 `SHF_GNU_RETAIN`（ELF.h:1304，=0x200000）由新 emitter 显式 OR 进 `.mcu.fixed.*` 节。**retain 载体唯一裁定：retain 只与 place_at 定义组合；bind_at 声明携带 retain 由 Sema 拒绝**（复用 `err_mcs251_retain_no_definition`，§2.2/§8；理由：bind-only 无定义无节无发射物，节位/keepalive/verifier 字节复核均无载体）。探针 2[B] 已用正确位重跑：本链接器实测拒 `SHF_ALLOC|SHF_GNU_RETAIN` 节（exit 1）。
4. lld 侧 FIXED Region + NOTE/manifest 合约（§3.3/§4）；台账接入点、存储类轴、重定位门、输出证据通道按 §1.3/§2.2/§3.3/§3.4 的精确裁定执行（修订 2 的"其余管线零改动"表述撤回，修订 3 的 F1-F8 清单修订 4 扩为 F1-F13）；独立 verifier 读最终 ELF + placement report。

## §1 现状与证据

### 1.1 地址空间（维持修订 2，探针已按 N6 扩覆盖）

同修订 2 §1.1 全文（Attr.td:865-868；SemaType.cpp:6567/:6586/:6598/:6626-6640/:6795；MCS251TargetParser.cpp:22-28 v2 布尔表仅声明 AS 0,1,2,3,4,6,7,8,9；AddressSpaces.h:122-131；MCS251.cpp:77）。探针 1[5] 双向赋值 + `__typeof__` 两侧互证，实测 0 个 addrspacecast（RESULTS-REVISION.txt [5] 段）。

### 1.2 retain 与自定义链接器（维持修订 2 框架，证据升级）

* 自定义链接器无 GC/MarkLive/归档提取；探针 2[C] `.a` 硬错误（exit 1）。
* `classifySection` ALLOC 白名单 `Common` 掩码在 **Core:483-486**：`ALLOC|WRITE|EXECINSTR|OVERLAY|EDATA_MOVABLE`，**不含 `SHF_GNU_RETAIN`**。探针 2[B]（正确位 0x200002）：`.rodata.retain` 节 flags 显示 `AR`，链接报 `unsupported ALLOC section flags for .rodata.retain`，exit 1——**GNU_RETAIN 被拒的动证**。对照 [B2] 无位 0x2 正常链接（exit 0）。各 region 的**精确相等** flags 检查（如 `.text` :487-489）放行时必须按 `S.Flags & ~SHF_GNU_RETAIN` 比较（F1/F2）。
* 发射端：通用 TLOF `Retain=Used.count(GO)`（TargetLoweringObjectFileImpl.cpp:742/:931）在本树存在，但外部 x86 演示（[DEMO]）与本树无关。本树实测（探针 2[A2]，[TREE]）：
  * `retain`+`__xdata` 普通对象 → **fatal**：`module uses an ABI capability outside the registered A4 v2 object identity`——llvm.used 容器初始化器里的 AS3 指针先触发身份门；
  * `used`+普通 AS0 对象 → **fatal**：`... custom sections ... not supported`——容器落入 AsmPrinter Reject（Printer:2160-2168 的 `hasSection()`/linkage 判断，`Reject()` 于 :2168）。
  * 结论：`SHF_GNU_RETAIN` 位今天在 MCS251 对象上**不会出现**（两条路径都在更早处 fail-closed），retain 位必须由 G11-B 显式支持（§3.2）。
* AS3 XSEG emitter 硬编码 `SHF_ALLOC|SHF_WRITE` 在 **Printer:2291-2293**（AS4 在 **Printer:2337** 切换到共享 text section；两条路径都不按对象走普通 retain 选节逻辑）。

### 1.3 FIXED Region 的 VMA=A 分配路径（N1 已裁定；本轮仅刷新行号并前移 mergePlacement）

链接器布局与输出链路（file:line 均为 `lld/MCS251/LinkerCore.cpp`，HEAD 032c90d66）：

* 输入 section 强制 `sh_addr==0`（loadFile :976-977）⇒ 地址 A 必须由 NOTE 记录携带，不能骑在 sh_addr 上。
* **既有先例 DATA_ABS**：`.mcs251.DATA.<name>` → `Region="DATA_ABS"`（classifySection :620-626），`layoutData()` 内按 section 名查 `--area-start=<section-name>` 得 `S->Address=A` 并 `reserve()` 进 DataUsed 台账（**:2653-2662，位于 :2646 台账清空之后**）。探针 3[1-4]：pin 0x1234 → map → ELF `st_value=0x1234`；pin [0x2,0x6) 驱逐 first-fit（`dyn_var` 0x0→0x6）。
  **先例证明了什么**：(a) 绝对 pin 若在 first-fit 类之前进入 DATA 台账，确实驱逐动态分配；(b) `reserve()+S->Address` 的写入经符号终值 → `collectSymbols`（:4506-4525）→ Driver `st_value`（Driver.cpp:241-247）端到端落进输出 ELF。
  **先例没证明什么**：(a) 它天然在 `DataUsed.clear()` 之后——任何置于 `layoutCode()+layoutData()` 之前的固定预留都会被清空；(b) 只覆盖 DATA 台账，不覆盖 `CodeUsed`（`layoutCode` :2380 清空）与 `XDataUsed`（layoutData 内 :3001 清空）；(c) 不覆盖 G8 重试种子快照/恢复（:2740/:2793）；(d) 地址来自 CLI 而非 NOTE；(e) 不经过初始化记录校验（validateXInit :3807-3811 白名单含 DATA_ABS 是既有分支）。
* 三本台账与清空点：`CodeUsed.clear()`（**Core:2380**，layoutCode 开头）、`DataUsed.clear()`+`StackH=0x100`（**Core:2646-2647**，layoutData 开头）、`XDataUsed.clear()`（**Core:3001**，layoutData 中 XSEG 分配前）。G8 重试：种子快照 **:2740**（`SeedUsed`，在所有固定类预留之后、DSEG/OSEG first-fit 之前拍摄），每次尝试恢复 **:2793**。
* `layout()=layoutCode()+layoutData()`（**Core:3241**）；符号终值 `S.Address=S.Sec->Address+S.Value`（**:3213-3218**）；`checkFlashGate`（**:3334-3367**，布局后、输出前，门可选 ：3335-3336）；`errorUndefined`（**:3369-3380**）；输出符号 `st_size` 直接取输入符号尺寸（装载 ：1101 → `collectSymbols` :4517 → Driver :244），链接器不重算。

**N1 裁定——统一调用序**（维持修订 3 结论；修订 4 变更仅一处：`mergePlacement()` 前移到 `resolveSymbols` 之前，理由与论证见 §3.3。调用序中 `[NEW]` 条目均为设计插入点，当前工作树不存在，实现归属切片见各行标注）：

```
run()（Core:4527）:
  loadFile（逐文件：NOTE 节结构校验 + 记录解析进 F->PlacementRecords；:926 createObjectFile 之前做归档双魔数检查，§8 P-3）
  validateIdentitySet（:4563-4564）
  [NEW] mergePlacement()                   ← 插入点（当前不存在；G11-C 于 :4563 与 :4565 之间插入）← §3.3；修订 4 前移：validateIdentitySet 之后、resolveSymbols 之前
  resolveSymbols（:4565-4566；第二循环 :1641-1647 扩 PlacementNames 分支——分支当前不存在，G11-C 加入）
  validateFileSignatures / validateSignatureSet / buildBitIdentities / ISR 合成（:4575-4587）
  [NEW] validatePlacementResolution()      ← 插入点（当前不存在；G11-C 于 :4587 与 :4588 之间插入）← §3.3（bind-only 与既有定义同名的 post-resolve 矛盾检查，轻量）
  layout()（:3241）:
    layoutCode():
      CodeUsed.clear()                      ← :2380
      [NEW] layoutFixedCode()               ← 插入点（当前不存在；G11-C 于 :2380 之后、:2384 光标初始化之前插入）← FIXED-CODE：reserveCode + Image 写入（重复字节检查沿用 :2410-2416）
      光标初始化 + CODE 类分配循环           ← :2384-2417（reserveCode 重叠检查扩展后含 FIXED 区间——当前不含；G11-C 使 FIXED 预先进台账，见 §3.3 碰撞落点裁定）
    layoutData():
      DataUsed.clear(); StackH=0x100        ← :2646-2647
      Config.ReservedData                   ← :2648-2650
      [NEW] layoutFixedData()               ← 插入点（当前不存在；G11-C 于 :2650 与 :2652 之间插入）← FIXED-AS0：reserve()（先于 DATA_ABS，冲突时报 DATA overlap 点名固定区间）
      DATA_ABS                               ← :2653-2662（不变）
      BSEG_BYTES/REG/BIT_BANK/allocateBitSlots ← :2664-2718（不变；FIXED-AS0 进台账后，位分配经 DataUsed 排除固定占位——G11-C 生效）
      G8 种子快照 SeedUsed                  ← :2740（**FIXED 区间因插入点在前而被构造性包含**——设计不变式，G11-C 落地）
      G8 重试循环（恢复 :2793 + DSEG/OSEG）
      ...
      XDataUsed.clear()                     ← :3001
      [NEW] layoutFixedXdata()              ← 插入点（当前不存在；G11-C 于 :3001 之后、:3006 XSEG 光标初始化之前插入）← FIXED-XDATA：XDataUsed（重叠/64K 不跨/--xdata-size 门全部沿用）
      XSEG 光标循环                          ← :3006-3057（:3050-3052 重叠检查扩展后含 FIXED 区间——当前不含；固定放置不推进 XsegCursor）
      X3 容量门（扩展后）                    ← :3058 起
      Synth + 符号终值                       ← :3128-3218（s_XSEG/l_XSEG 统计 ：3196-3211，FIXED 处置见 F13）
  [NEW] validatePlacementResolution / checkFlashGate（扩展后）← checkFlashGate 现位于 :4594-4595；前者为插入点（当前不存在；G11-C 于 :4587 与 :4588 之间插入）
  errorUndefined（扩展 bind-only 豁免）/applyRelocations（扩展 F9-F12 门）/validateXInit/validateXDATAInit ← :4596-4598
  collectSymbols（扩展 Synth 通道，§3.3 裁定）/ Image / Map            ← :4612-4616
```

G8 种子规则冻结（维持修订 3）：`layoutFixedData()` 必须位于种子快照行之前（即 :2650 与 :2653 之间），使 `SeedUsed` 构造性包含 FIXED 区间；lit 冻结断言"重试恢复后的台账仍含全部 FIXED 区间"（负例：固定 pin 与迁移后 EDATA 对象重叠时重试必失败且不静默；失败语义=不得产生重叠的成功布局，若 first-fit 可改放其他空洞则重试成功不违反种子不变式）。

### 1.4 memory map 权威来源（N8 已裁定，维持）

* **STC32G144K246（G144）**：`manuals-md/G144K246/12-存储器-全球唯一ID号CHIPID.md` **ch12 编址表（:419-428，权威）**：`00:0000H~00:FFFFH` 数据区（edata）；`01:0000H~02:FFFFH` 内部扩展数据区（xdata，128K）；**`7E:0000H~7E:FFFFH` = 扩展 SFR 区（XFR），需 EAXFR 使能——不是数据 RAM**；**`7F:0000H~7F:FFFFH` = 外部数据区（xdata），需 EXTRAM（AUXR.1）使能**；`80:0000H~80:0FFFH` RAM 代码区（RAMEXE）；`FC:2800H~FF:FFFFH` 扩展/普通代码区。修订 2 的"7E 段 XRAM"与"02:0000H~00:FFFFH 映像表"转写错误**全部作废**。
* **STC32G12K128（12K128）**：`manuals-md/STC32G/ch11-…md`（Flash 128K `FE:0000H~FF:FFFFH`，edata 4K、xdata 8K）。维持。
* 层级裁定维持：芯片级=手册；板级窗口=链接器 CLI（`--flash-base/--flash-size`、`--edata-end`、`--xdata-size`、`--area-start`）；指针宽度=`-mcs251-memory-contract`/`getLayoutDesc`。
* **demo 85**：65536 字节对象 > 单对象上限 65535（Printer:2286-2289 记录格式 u16；Core:3014-3021 链接器门）。裁定：**G11 本轮不支持 >65535 单对象，demo 85 归 G13b 路线**，不得混入 place+noinit 正向验证清单（§5 已相应修改）。"[0x020000,0x030000) 片内地址合法"仍成立，但**地址合法 ≠ 本轮可实现**。

## §2 语义定义

### 2.1 类型与实体分离

不变：类型由元素/长度/AS/cv 决定；实体由 owned/external、地址、初始化策略、retention 决定。固定 RAM 不隐含 volatile、不禁优化、不变调用模型；函数 place_at 只固定入口地址。

### 2.2 四机制（N2/N4/N5 修订 4）

* `place_at(A)`：定义实体且正常初始化；VMA=A。允许静态全局、函数、合规完整定义；拒绝 automatic 与非静态成员。与 `noinit`、`retain` 可组合。
* `bind_at(A)`：外部实体名字/类型引用，无初始化器/函数体，**不产生 storage**。声明必须是完整类型且**尺寸非零**（零长数组扩展 SemaType:2314-2321 不拒零长，见 §8 `err_mcs251_placement_zero_size`）；对象尺寸=声明类型 `sizeof`（NOTE.size+跨载体比对）；**bind 函数：尺寸恒为 0、不约束 st_size，align 字段仅约束入口对齐（1=不约束）**。**bind 要求实体具有 external linkage**（本 TU 的 static 无法被其他 TU 引用；跨 TU 引用 static 符号不进 Globals，`errorUndefined` :3369-3380 必失败）——冻结诊断 `err_mcs251_bind_at_linkage`（§8）。**bind 声明不得携带 retain**（见 retain 条）。
* `retain`（N5 修订 4 唯一裁定）：**本轮作用域=仅 place_at 定义的固定实体**。`mcu_retain` 出现在 bind_at 声明上 → Sema 拒绝（`err_mcs251_retain_no_definition`，fixture retain-bind.c，§8）；出现在非固定实体上 → `err_mcs251_retain_requires_placement`。裁定理由（消除修订 3 中 :99/:253/:177 的三向矛盾）：(1) RETAIN 的链接端不变式"带 RETAIN 的 section 永不进入任何丢弃路径"要求节存在，bind-only 无节；(2) keepalive（llvm.used）是每 TU 的编译端集合，bind 声明处的 retain 无法施加到定义 TU 的发射物；(3) verifier 的 retained 行检查需要 report+symtab 双证，bind-only 在 symtab 的合成符号无尺寸无节，"保留"无产品语义。由此 §3.2 NOTE flags.bit0 只会出现在 owned 记录（bind 记录恒 0，Sema 保证，mergePlacement fail-closed 复核），§7 manifest 的 `[retain]` 仅允许 owned 实体。编译端 keepalive=既有 `RetainAttr→addUsedGlobal`；对象端=新 emitter OR `SHF_GNU_RETAIN`；链接端=白名单接受（`Flags & ~SHF_GNU_RETAIN` 分类）+ 硬不变式 + verifier 复核。无 GC 链接器中链接端 RETAIN 不承载存活算法；`.a` 未提取成员 retain 无意义且无提取保证（本链接器不支持 `.a`，探针 2[C]）。
* 地址空间限定符：同 §0.1。

**N2 裁定——存储类分类轴重建**（维持修订 3 正交轴，修订 4 补 bind-only 判定载体）：`EXECINSTR` ≠ CODE 存储空间。冻结为**权威正交字段**（NOTE schema v1，§3.2）：`storage_class ∈ {AS0-DATA, XDATA, CODE}`，`entity ∈ {object, function}`，`ownership ∈ {owned, bind}`。CODE 空间的 const 对象 storage_class=CODE 且可无 EXECINSTR；EXECINSTR 仅由函数 emitter 置位。**一致性检查**（mergePlacement 执行）：`storage_class==CODE ⇒ 节类型 PROGBITS`；`storage_class∈{AS0-DATA, XDATA} ⇒ 节类型 NOBITS 且带 WRITE`；`XDATA/AS0-DATA 不得带 EXECINSTR`；不匹配 → `section %name disagrees with placement NOTE for %sym`（§8）。

**bind-only 无 section 时的存储类判定载体（N2 闭合项）**：bind-only 实体没有输入节，任何按 `Region` 字段的检查对它失明。冻结：mergePlacement 产出的 `PlacementNames` 是 **记录结构**而非裸地址表：`PlacementNames[name] = {Address, Size, StorageClass, Entity, Stable}`（键=ELF 符号名；字段来自 NOTE/manifest 记录，`Stable` 承载 stable_symbol 身份——键与输出名的关系见 §3.3 命名字段裁定）。重定位层的**目标分类统一走判定函数** `placementTargetClass(Target)`（当前不存在；G11-C 实现，applyRelocations 三门 F9/F10/F11 共用）：`Target->Sec` 存在 → 用该节的 `Region/StorageClass/IsCode`；否则查 `PlacementNames`。F9/F10/F11 三个重定位门（下表）全部经此函数取目标类别——**判定载体是 PlacementNames 记录，不是 Region 字段**。

**逐项 region/重定位过滤器扩展清单**（修订 3 F1-F8 行号刷新 + 修订 4 新增 F9-F13；"其余管线零改动"撤回的完整改动面以此为准）：

| # | 位置（HEAD 032c90d66） | 现条件 | G11 扩展 |
|---|---|---|---|
| F1 | Core:483-486 classifySection `Common` 掩码 | 5 个 flag | 掩码加 `SHF_GNU_RETAIN`；**仅** `.mcu.fixed.*` 名可携带该位（其他名带位维持原拒绝）；随后该节按 `S.Flags & ~SHF_GNU_RETAIN` 进入精确相等比较。**RETAIN 双向互斥声明（修订 6 冻结，三条统一裁定）**：(i) `.mcu.fixed.*` 以外任何分类路径（含 Common 掩码精确比较之外的一切到达路径）携带 `SHF_GNU_RETAIN` 一律拒绝——该位不是分类比较时可无条件忽略的噪声位；(ii) `.mcu.fixed.*` 携带该位**当且仅当** NOTE `flags.bit0=1`，双向不一致均拒（bit0=1 而节无位 → `retain 位丢失` 文案；节有位而 bit0=0 → `多余 retain 位` 文案）；(iii) 非 retain 固定节不得因分类比较屏蔽该位而被静默当 retain 节接受——`& ~SHF_GNU_RETAIN` 仅用于分类比较，节的实际接受还需 NOTE `flags.bit0=1` 佐证（§3.2 mergePlacement flags 合并表复核；§8 两条新文案） |
| F2 | Core:487-489（`.text` 等精确相等） | flags 全等 | 一律 `S.Flags & ~SHF_GNU_RETAIN` 后比较（无位时行为不变） |
| F3 | Core:3058 起 X3 容量门 | `Region=="XSEG"` | `||(Region=="FIXED" && StorageClass==XDATA)`；XDATA 固定实体同受 `--area-start=XSEG` 声明要求（:3004-3005）与该门约束 |
| F4 | Core:3886-3891 validateXDATAInit 目的∈一节 | `D->Region=="XSEG"` | 加 `||(D->Region=="FIXED" && D->StorageClass==XDATA)`（固定初始化 XDATA 对象由 clang 发 `xdata_init` 记录 dest=A） |
| F5 | Core:3807-3811 validateXInit 目的∈一片 | DSEG/EDATA/DATA_ABS/BSEG_BYTES | 加 `FIXED && StorageClass==AS0-DATA` |
| F6 | Core:3344-3357 checkFlashGate `IsCodeArea` | 按 region 名列举 | 加 `Region=="FIXED" && StorageClass==CODE`（函数与 CODE 空间对象都受门；EXECINSTR 与否无关） |
| F7 | Core:2754 G8 迁移候选 | `Region=="DSEG"` 且带 EDATA_MOVABLE | FIXED 天然排除（Region 不符）；classifySection 对 `.mcu.fixed.*` 拒绝 `SHF_MCS251_EDATA_MOVABLE` 位（F1 之后显式拒绝） |
| F8 | Core:1637 resolveSymbols / :3369-3380 errorUndefined | — | 不变式：FIXED 不改变符号解析规则；errorUndefined 增 bind-only 豁免（§3.3） |
| F9（新） | Core:3424-3440 `R_MCS251_16/R_MCS251_J16` 拒绝门 | 仅 `Target->Sec->Region=="XSEG"` 拒 16 位通道 | 门条件改为**分类制**：`placementTargetClass(Target)==XDATA` 即拒（覆盖 XSEG、FIXED-XDATA owned 节、bind-only XDATA 记录三类载体）；文案沿用 `XDATA symbol %sym truncated to 16 bits …`。**这是 FIXED-XDATA 今天会绕过 16 位截断门的漏点** |
| F10（新） | Core:3513-3532 `R_MCS251_24` 对象边界检查 | 仅 XSEG 检查 [slice, slice+size]（one-past-end 合法） | 同 F9 分类制扩展：FIXED-XDATA owned 用节 [Address, Address+Size)；bind-only XDATA 用 `PlacementNames` 记录的 [Address, Address+Size)（bind 对象 size=声明 sizeof，记录自带）。**FIXED-XDATA 今天会绕过存储指针对象边界约束** |
| F11（新） | Core:3559-3561 J16/J11 控制目标 CODE 门 | `!Target->Sec || !Target->Sec->IsCode` 即拒 | bind-only **函数**目标（无 Sec）按 PlacementNames 的 `Entity==function && StorageClass==CODE` 放行（A=入口）；bind-only **对象**仍拒。mergePlacement 对 `StorageClass==CODE` 的 FIXED 节置 `IsCode`，使 owned 函数/CODE 对象同过此门 |
| F12（新） | Core:3548-3557 溢出带检查 / :3567-3569 16 位写入 | `R_MCS251_16` 在 Is24Slice 带内允许 24 位数值，:3567-3569 只写低 16 位 | **裁定：拒绝而非截断**。任何 XDATA 分类目标（含 FIXED-XDATA/bind-only-XDATA）取 16 位通道已在 F9 被拒，控制流到不了 :3548-3557/:3567-3569；F12 不改写这两处代码，冻结两条不变式并 lit 断言：(a) 门序不变式——F9 检查先于写入；(b) 负例——FIXED-XDATA 符号 + `R_MCS251_16` 必得到 F9 文案而非静默低 16 位 |
| F13（新） | Core:3196-3211 `s_XSEG`/`l_XSEG` 统计 | 仅统计 `Region=="XSEG"` 节 | **显式裁定：维持仅动态 XSEG，FIXED/bind-only 不计入**（非遗漏）。理由：s_XSEG/l_XSEG 的契约是"动态 XSEG 窗口的连续跨度"，供运行时边界/容量数学消费；固定 pin 可落在窗口之外或窗内空洞处，计入会使跨度覆盖未用空洞、静默放大 l_XSEG 并扰动既有消费方。固定实体地址以自带符号与 report 行暴露，不借边界符号。lit 冻结：存在 FIXED-XDATA 时 s_XSEG/l_XSEG 与无 FIXED 基线逐字节一致 |

**未配置 CODE/flash 窗口时固定代码实体的裁定（N2 闭合项，命名统一）**：`checkFlashGate` 是可选门（:3335-3336），链接器不知道板级 CODE 窗。冻结：**存在 FIXED-CODE 实体而 `--flash-base/--flash-size` 未配置 → 冻结错误** `fixed CODE entity %sym requires an explicit CODE window (--flash-base/--flash-size)`（§8 新行）。已配置时：固定实体跨度必须整体落在 `[FlashBase, FlashBase+FlashSize)`，文案 `fixed CODE entity %sym at 0x%x is outside the CODE window`（§8）。**统一命名**：两条文案对函数与 CODE 空间 const 对象一律用 `fixed CODE entity`（修订 3 的 `fixed function` 字样废止，避免对象触发时误称 function）。始终生效的建筑学检查（24 位 `rangeFits`、reserveCode 重叠）不依赖该门。

### 2.3 冻结拒绝矩阵

见 §8（含修订 4 新增诊断与四列对应）。

## §3 实现方案与 file:line 落点

### 3.1 Clang 前端（G11-A）

维持修订 2 §3.1（Attr.td 拼写注册、Sema 检查按 §8、`err_mcs251_address_space_unavailable` 前移）。本轮派生：`err_mcs251_bind_at_linkage`、`err_mcs251_retain_requires_placement`、`err_mcs251_placement_zero_size`（§8）；retain 组合裁定落地（retain+bind_at 拒绝）；static 实体 stable-symbol 生成（§3.2）；实体身份/属性传递（下）。

### 3.2 IR/对象合约（G11-B，S0 schema 冻结稿）

**NOTE 载体**：输入节 `.mcs251.placement`，`SHT_NOTE`、flags=0、align=4（validateMetaSection 白名单，镜像 `.note.mcs251.abi` 的形状检查）。**envelope namesz 裁定（N4 闭合项）**：名称串 `"MCS251\0"` 含终止 NUL 为 **7 字节，`namesz=7`**；note 流中 name 字段填充到 4 字节倍数，**存储占 8 字节（7+1 字节 0 填充）**。"8" 是布局产物，不是任何字段的值。writer 恒写 `namesz=7` + 8 字节 name 区；reader/verifier 按 `namesz` 读名、跳过 `(4 - namesz%4)%4` 填充；`namesz != 7` 或 name 串不匹配 → `malformed placement NOTE in %file: %reason`，fail-closed。`type=u32(1)`（placement v1）、`descsz`，全 BE。

**字节序（维持修订 3）**：输入对象是 ELF32 **big-endian**（loadFile :930-932 经 `ELF32BEObjectFile` dyn_cast 强制）；note 头与 desc 内所有多字节字段一律 **BE**。输入 section `sh_addr` 强制为 0（:976-977）。

**记录 v1 布局（S0 冻结；writer/reader/verifier 三方独立可实现）**：

```
u32 record_size                     # 本字段之后到下一记录的字节数（含填充）
u8  schema_version (=1)
u8  storage_class (0=AS0-DATA, 1=XDATA, 2=CODE)
u8  entity        (0=object, 1=function)
u8  ownership     (0=owned, 1=bind)
u32 address       # 24 位值零扩展；bind 也必须给出 A
u32 size          # 见下方"实体跨度裁定"；bind function=0（无尺寸约束）
u32 align         # 2 的幂; 1=不约束（bind 函数仅约束入口对齐）
u32 flags         # bit0 retain（仅 owned 记录可为 1）, bit1 noinit; 其余位必须为 0（否则 malformed）
u32 layout_hash   # 见下（不含 size）
u8  stable_len; u8 stable_symbol[stable_len]; 填充到 4 字节倍数
```

未知 kind/flags 处理：reader（lld 与 verifier）对未知 `storage_class/entity/ownership` 编码、未知 flags 位、截断、record_size 与 stable_len 不一致一律报 `malformed placement NOTE in %file: %reason`，fail-closed。bind 记录 flags.bit0=1 → malformed（Sema 已拒，fail-closed 复核）。

**实体跨度裁定（N4 闭合项；修订 3 的"三时点构造性相等"表述撤回）**：修订 3 断言 `NOTE.size == sh_size == 定义符号 st_size` 构造性相等，但实际发射路径是：函数 `st_size` 由**结束标签−起始标签**之差生成（BasePrinter:2531-2539 `emitELFSize`），而 **BasePrinter:2558 在函数 size 之后调用 `emitJumpTableInfo()`**，MCS251 跳表列**追加进函数同一 CODE 节**（Printer:1043 `emitFunctionBody` → :1048-1054 几何契约注释 → :1095-1108 ljmp 列发射）。故含跳表函数现有即可发射 `sh_size > st_size`；修订 3 的断言会拒绝既有合法产物。冻结裁定：

1. **实体跨度（G11 布局消费的唯一尺寸）= 节跨度 `sh_size` = 函数体标签跨度 + 同节附属载荷（BRJT 跳表列等）**。理由：布局台账（reserveCode/Image/重叠/窗口检查）必须覆盖实体在该区域占据的每个字节，否则下一固定预留与活跳表字节碰撞；现发射器已把附属载荷放进同一节、排在体后，`sh_size` 天然是权威跨度，**发射几何零改动**（与 BRJT §3.2.4 L3 相容）。函数体标签跨度（st_size）继续覆盖 memcpy 内联缩减、长分支扩展、体内对齐——三者都体现在最终标签位置中，不用 IR/预布局估计。**碰撞检查落点裁定见 §3.3"统一 CODE 台账"（reserveCode 消费同一台账，verifier 只复核）**。
2. **定义符号 st_size 语义不变**（保持 BasePrinter:2531-2539 的标签差）；mergePlacement 对 owned 函数断言 `NOTE.size == sh_size` 且 `st_size <= sh_size`（等号当且仅当无同节附属载荷），不再要求 `st_size == sh_size`；§3.2 主符号约束相应放宽为函数 `st_size ≤ sh_size`、对象仍 `st_size == sh_size`。
3. **NOTE.size 生成路径 = 同源符号差表达式**：G11 emitter 在每个 `.mcu.fixed.*` 节首尾各放一个汇编期标签（`<stable>.begin` 于节首、`<stable>.end` 于该实体全部载荷（含跳表）发射完之后），NOTE writer 在流末尾把 `size` 字段以 `end − begin` 的 **MC 符号差 fixup** 写入 `.mcs251.placement`，由对象写入器在 `finish()` 最终布局后解析。**时点论证**：`doFinalization`（BasePrinter:3218 `emitEndOfAsmFile`）先于 ：3223 `finish()` 的布局/松弛（长分支扩展等发生在布局期），doFinalization 时刻没有"全部 MC 布局后的数值尺寸"可取——常量路径必然早熟；符号差 fixup 把求值推迟到布局终值出现之后，是唯一不依赖布局时点的载体。链接器读的是已物化对象中的已解析 u32，读侧零特殊逻辑。 owned 对象（AS3/AS4 `emitELFSize` 常量路径，Printer:2270-2272）不涉及该问题，维持常量写入。

**layout_hash（修订 7 B1：同一算法、两种载体语义，均不含 size）**：算法冻结：`H(F) = SHA-256(BE(F))` 的低 32 位；`F = (schema_version, storage_class, entity, ownership, address, align, flags)`，前四字段各 u8、后三字段各 u32（即 `>BBBBIII`），低 32 位取摘要最后 4 字节作 BE u32。`size`、`stable_symbol`、源文件、行号不参与。**size 排除的裁定理由维持**：size 是布局派生属性（符号差 fixup 在 `finish()` 后才有终值），而原记录 hash 必须在记录组装期即可计算；尺寸一致性仍由 NOTE、节跨度、report 与 symtab 的显式尺寸互核承担，不经 hash。

1. **原记录完整性 hash（`H_source`）**：每份输入 NOTE 的 `layout_hash` 由 writer 按该份记录自己的原始字段 `F_source` 生成；verifier 在任何合并前逐份重算并与该份存储值比较，不得拿合并字段替换原字段。§7 manifest 文本没有 hash 字段，不虚构其原记录 hash；manifest 的显式约束仍参与语义合并与来源核验。
2. **合并 report hash（`H_report`）**：mergePlacement 按合并表仅用显式字段产出合约；`Result.Placement.LayoutHash` 及 report 的 `layout_hash` 是按合并后的 `F_merged` 新算的 `H(F_merged)`（schema_version=1），不是拷贝某份输入 hash。verifier 独立按 NOTE/manifest 显式字段重建合并结果，核对 report 显式字段，再按 report 合并字段重算并比较 report 自己的 hash。原记录与合并 report 之间**不设 hash 相等断言**；ownership、retain、align 合法变化时数值可以不同。
3. **唯一校验职责冻结**：hash 仅作 report 审计侧的完整性标注，由独立 verifier 校验；**链接器的所有接受/拒绝、合并、解析、布局裁定只看显式字段，layout_hash 不参与任何链接决策**。loadFile/mergePlacement 不因 hash 数值损坏拒绝或改变合并结果（字段截断等结构畸形仍按既有规则拒绝）。原记录或 report 自身 hash 不符均由 verifier 报 `VERIFY FAIL: layout hash mismatch for %sym`，附来源文件/记录或 report 行定位。`--verify-placement` 的最终非零可来自该独立验证阶段，不能转写为 mergePlacement 拒绝。不开验证时，hash 单独损坏不导致链接失败。
4. **身份与完整性分工**：身份键恒为 stable_symbol（下条），来源映射按 report 三元组；hash 是 32 位且不含身份串，不能替代身份关联、显式字段合并或尺寸检查。

**实体身份与 stable-symbol（维持修订 3）**：G11 身份载体是 `stable_symbol`，不是 ELF 符号名。external 实体 `stable_symbol = 声明名`；static 实体 `stable_symbol = <TU 限定>.<声明名>`，`<TU 限定> = 下划线化的基名 + "." + 8 位十六进制 FNV-1a(绝对源文件路径)`。ELF 符号保持自然装配名（static 保持 STB_LOCAL；Core:4501-4505 允许跨对象重名的现状**不改变**）；placement report 行以 `(输入文件, 符号, stable_symbol)` 三元组为键。链接内 stable_symbol 撞名 → `conflicting placement for %sym: …`（§8，边界见 P-1/P-5）。

**节主符号约束（修订 4 放宽函数侧）**：`.mcu.fixed.<stable-symbol>` 节**恰有一个定义符号**，`st_value==0`；对象：`st_size==sh_size`；函数：`st_size<=sh_size`（差值=同节附属载荷）；节内不得有其他绑定符号。检查点：loadFile 符号装载后。`S.Address=S.Sec->Address+S.Value`（:3213-3218）在 offset-0 下给出实体入口==A。

**节内实体唯一性与附属载荷归属（修订 5 新增裁定；Alice 四审 N4 缺口：节内多实体时如何避免跨度吞并）**：上述"节内不得有其他绑定符号"细化为 emitter 侧结构与对象检查两侧可执行规则：

1. **每节恰一实体（多实体=多节）**：emitter 侧规则——每个携带 `"mcs251-place"` 属性的实体获得**专用节** `.mcu.fixed.<stable-symbol>`；同一 TU 的两个固定实体**绝不共享一个 `.mcu.fixed.*` 节**（即使地址相邻、对齐相同也不合节）。stable_symbol 的 TU 限定（static 实体，§3.2 身份段）保证同 TU 多实体节名互异；emitter 对"第二个带 place 属性的实体解析到已存在的 `.mcu.fixed.*` 节名"视为内部不变式违反，直接 fatal（正常输入不可达）。
2. **附属载荷归属判定 = 发出它的函数实体所在节**：同节内、主符号之后、由该实体的发射路径直接产生的载荷（BRJT 跳表列、函数体后对齐填充、`<stable>.end` 标签之前的全部字节）归属该节唯一实体，计入其节跨度。几何依据是既有契约：跳表列由 `emitJumpTableInfo()` 钩子在当前函数的节内、函数体后发射（Printer:1048-1054/:1095-1108），流位置就是该函数实体的节——不存在脱离实体发射路径的"孤儿载荷"。禁止 emitter 把任何其他实体的字节排进本节。
3. **对象侧防吞并检查（loadFile/mergePlacement 复核）**：`.mcu.fixed.*` 节内实体计数恰为 1。**实体计数边界冻结（修订 6 唯一裁定）**：节内凡定义符号（defined binding）且类型 `STT_FUNC` 或 `STT_OBJECT`，**无论 st_size 是否为 0、绑定属性（LOCAL/GLOBAL/WEAK）如何，一律计入实体计数；计数 ≠ 1 即拒**——不存在"零尺寸第二实体解释为辅助符号"的例外：辅助定位标签仅限 local `STT_NOTYPE` 无尺寸 LJTI 形状标签（它们是附属载荷的定位标签，不是实体，不参与该计数）；带尺寸的局部符号、零尺寸 `STT_FUNC`/`STT_OBJECT` 定义符号均使计数 ≥ 2，按不一致拒绝：`section %name disagrees with placement NOTE for %sym`（§8）。此检查使"A 实体的节跨度吞并 B 实体"在对象装载期 fail-closed，而不等到布局期才表现为重叠。
4. 与合并表的关系：该规则与 owned↔bind 合并（§3.2 合并表）正交——合并按 stable_symbol 分组，节唯一性按输入节判定；n 个 bind 声明 + 1 个 owned 定义的实体仍然只对应定义 TU 的那一节。

**owned↔bind 字段语义合并表（N4 闭合项；修订 3 的"A/尺寸/layout 字段全等"表述撤回）**：mergePlacement 按 stable_symbol 分组后，对"1 份 owned + n 份 bind"逐字段按**字段语义**合并（bind 函数的 0 解释为"无尺寸约束"，不要求 owned 尺寸为 0；ownership 允许不同，owned 主导）：

| 字段 | 合并规则（三选一） | 不满足时 |
|---|---|---|
| stable_symbol | 相等（分组键） | — |
| address | **相等要求** | `conflicting placement for %sym: 0x%x (%file) vs 0x%x (%file)` |
| storage_class | **相等要求**（CODE/XDATA/AS0-DATA 不可两说） | 同上 |
| entity | **相等要求**（object vs function 不可两说） | 同上 |
| size | **语义合并**：bind function 的 0="无尺寸约束"→ 取 owned 值；bind object 的 sizeof 必须==owned 尺寸；owned 间必相等 | 不等 → `conflicting placement …` |
| align | **语义合并**：取更严格值（1=不约束；一侧为 1 取另一侧；两侧非 1 且不等 → 冲突） | 不等且均非 1 → `conflicting placement …` |
| ownership | **允许不同**（owned 主导；本行即 owned↔bind 合并的定义） | — |
| flags | bit1 noinit：**相等要求**；bit0 retain：bind 记录恒 0（Sema 保证）→ **owned 值主导**，bind 带位按 malformed 拒（fail-closed） | noinit 不等 → `conflicting placement …` |
| layout_hash | **不参与字段合并或链接接受/拒绝**：输入值保留在原始对象供 verifier 按各自原字段核验；合并输出按合并字段新算 report hash，不比较跨载体 hash | 原记录或 report 的自身完整性不符 → 仅 verifier 报 `VERIFY FAIL: layout hash mismatch for %sym` |

**B1 合约 fixtures（修订 7；PLANNED→G11-B/C/D，不是本轮实现结果）**：下列同一实体均为 schema=1、XDATA object、A=0x10000、size=4、noinit=0；省略的 align=1、flags=0，owned 对应合法专用节。

| fixture | 原记录及各自 `H_source` | 合并字段与 `H_report` | 强断言 |
|---|---|---|---|
| hash-owned-bind | owned=`457bdcf1`，bind=`e6e499c9` | ownership=owned，align=1，flags=0；`457bdcf1` | 链接与 verifier 均成功；不要求 bind hash 等于 report hash |
| hash-retain-bind | owned+retain=`6dd2c36a`（节带 RETAIN），bind=`e6e499c9`（不带 retain） | ownership=owned，flags=1；`6dd2c36a` | 链接与 verifier 均成功；retain 仍只由 owned 主导 |
| hash-align-merge | owned align=1：`457bdcf1`；bind align=4：`396ddf6c` | ownership=owned，align=4，flags=0；`3bfcb603` | 链接与 verifier 均成功；report hash 可与两份原记录都不同（不改变两侧非 1 且不等即冲突的规则） |
| verify-hash-source | 将 hash-owned-bind 中 bind 存储值从 `e6e499c9` 改为 `e6e499c8`，其余字节不变 | 显式合并结果与 report hash 仍同正例 | 不启用验证的链接仍成功；携原始对象运行 verifier 必以 hash mismatch 非零，不能因 report hash 正确而掩盖原记录损坏 |
| verify-hash-report | 原记录不变，仅翻转 report hash 一位 | 合并字段不变 | verifier 在 report 自身完整性核验时报同一文案并定位 report 行 |

**IR 传递（维持修订 3）**：CodeGen 把 AST 属性写入实体 GlobalValue 的 IR 属性 `"mcs251-place"`（值=`A,storage_class,entity,ownership,flags` 结构串；函数与对象同机制，类似 `"bss-section"` 属性写入 CodeGenModule.cpp:3523-3530 的既有模式）。AsmPrinter 新 `.mcu.fixed.*` emitter 读该属性写 NOTE 记录并建节（节首尾标签对供 NOTE.size 符号差）；**retain 位以属性 flags.bit0 为唯一写者**（Sema/CodeGen 保证与 `RetainAttr` 的 llvm.used 成员身份一致），emitter 显式 OR `SHF_GNU_RETAIN`；通用 TLOF 的 `selectSectionForGlobal` 不经过。函数节走显式 section 名（CodeGenModule:3536-3538 的既有 `setSection` 模式），其 flags 由 G11-B 显式裁定，lit 逐格断言（§5）。

**keepalive 根处理（N5 修订 4 裁定冻结：分类前移 + 单点判定多处消费；修订 5 纠正文件归属）**：本段全部 `Printer:` 落点均在 **`llvm/lib/Target/MCS251/MCS251AsmPrinter.cpp`**（修订 4 稿未标注文件全路径，致四审抽查误指 `LinkerCore.cpp:612-621`——该处实为 `.mcs251.SSEG` 节分类，与 keepalive 无关。修订 5 按 HEAD 032c90d66 在 AsmPrinter.cpp 重核全部行号）。`classifyMCS251KeepaliveMember` 谓词当前不存在（G11-B 实现，插入点见下）。

* 现状三处拒绝（均有动证/源证，行号为 HEAD 032c90d66 实测）：
  1. v2 身份普查拒绝 llvm.used 初始化器中的 AS3 指针（探针 2[A2] a2-ret-xdata fatal；豁免先例 **Printer:612-621，其中 ：619 对任何非程序 AS 成员直接 `return false`**）；
  2. 存储预留扫描（**Printer:1987-1990**，仅豁免 ISR 形状的 llvm.used）；
  3. `emitGlobalVariable` 门（**Printer:2111-2114** ISR 根转通、:2120-2123 BT12 bit 根转通；**Printer:2160-2168** Reject 链：`hasSection()`/非 external-local linkage，`Reject()` 于 :2168；bit 根豁免 :474-507 只收全 bit-object 容器）。
* **构造性缺陷（修订 3 未解决，本轮闭合）**：修订 3 的"既有检查保留不动，新检查放在其后"对 **ISR 混合容器不成立**——旧分支 ：612-621 对"有 ISR 定义且形状合法"的容器立即逐成员检查，任何非程序 AS 函数成员在 ：619 直接 `return false`；ISR+bit、ISR+placement-object 容器根本到不了"其后的超集"检查。
* **裁定：成员分类前移到旧硬失败之前，且四处消费同一验收结果**。新单点判定谓词：

  `classifyMCS251KeepaliveMember(Member) ∈ {ISR, Bit, Placement, Unmarked}`

  成员经单操作数 no-op cast 链（沿用 ：494-500 形状）分类：`ISR` = `ModuleHasISRDefinitions && 程序地址空间 Function`（T06 规则不变）；`Bit` = `isBitObjectGlobal` 的 GlobalVariable（BT12 规则不变）；`Placement` = **携带 `"mcs251-place"` 属性的已定义 GlobalObject**（G11 新类）；其余 = `Unmarked`。容器形状检查（llvm.used/llvm.compiler.used、appending、指针数组、"llvm.metadata"、无 materialized use）维持在容器级先行。
* **控制流重排（四处一致）**：
  1. **Printer:612-621（身份普查）**：旧"逐成员取函数 + 非程序 AS 即 `return false`"改为"逐成员 `classify` → 任一 `Unmarked` → `return false`（负例不变式保持）；`ISR` 成员沿用程序 AS 校验（T06 不放宽）；`Bit`/`Placement` 成员放行"。
  2. **Printer:1987-1990（存储预留扫描）**：排除条件改为消费同一分类——`ISR ∨ Bit ∨ Placement` 的容器成员不计普通 RAM 预留（不再各处复制 OR 分支）。
  3. **Printer:2111-2123（发射门）**：两个既有转通（ISR 根 ：2111-2114、bit 根 ：2120-2123）保持；新增第三转通"全部成员 ∈ {ISR, Bit, Placement} 的容器 → `AsmPrinter::emitGlobalVariable` 按 special-LLVM-global 消费为身份非字节"；判定调用同一谓词。
  4. **Printer:2160-2168（Reject 链）**：经 3 转通的容器不触达；未转通者维持原拒绝。
  单点判定 = 谓词函数只实现一次，四处调用；lit 以"同一容器在四处判定一致"为断言面。
* **混合规则**：单模块 llvm.used 唯一（多次 `addUsedGlobal` 合并进一个容器），成员分类验收使 ISR+bit、ISR+placement、bit+placement、ISR+bit+placement 混合容器合法，而"任何 Unmarked 成员"仍整体拒绝——豁免强度不变式：**只按成员逐个结构验收，绝不按容器名或 section 名豁免**（沿用 ：423-427 冻结原则）。
* mcu_retain 作用域（§2.2 裁定）：Sema 拒绝非固定实体与 bind 声明，故容器中 Placement 类成员必为 place_at 定义实体；bind-only 外部实体无定义、无 keepalive 根、不进容器。
* **验证要求**（§5 落地）：AS0 对象、AS3 对象、AS4 对象（CODE 空间 const，无 EXECINSTR）、程序 AS4 函数四格 × {place, place+retain}；**新增混合容器 fixtures：ISR+bit、ISR+placement、bit+placement、ISR+bit+placement 各一正例 + 各一"掺一个 Unmarked 成员"负例**。

### 3.3 lld（G11-C，含 mergePlacement——修订 4 重排）

* **classifySection**：`.mcu.fixed.<stable-symbol>` 结构识别（PROGBITS/NOBITS、ALLOC、按 F1/F2 屏蔽位后精确比较、拒 OVERLAY/EDATA_MOVABLE）→ `Region="FIXED"`；`StorageClass` 尚未判定（NOTE 在同文件另节），语义归 mergePlacement。
* **NOTE 解析**：validateMetaSection 增 `.mcs251.placement` 白名单（形状）；记录解析在 loadFile 的节循环内（每文件 `F->PlacementRecords`）。结构检查与关联检查分工维持修订 3：节形状/记录结构在 loadFile（--print-input 亦覆盖）；NOTE↔节↔符号的语义关联在 mergePlacement。
* **mergePlacement() 调用序裁定（N4 闭合项；修订 7 与 P-1/P-5 同步）**——`mergePlacement()` 当前不存在；插入点（G11-C 于 :4563 validateIdentitySet 调用与 :4565 resolveSymbols 调用之间插入）：
  * **依赖约束**：resolveSymbols 第二循环（:1641-1647）读取 `PlacementNames`，故该表必须预先由 mergePlacement 完整生成；不得让消费方读取尚未生成的表。
  * **裁定：mergePlacement 整体前移到 `validateIdentitySet` 之后、`resolveSymbols` 之前**。论证：mergePlacement 的全部输入在此时已齐备——(a) 各文件 `F->PlacementRecords` 与 `.mcu.fixed.*` 节及其符号在 loadFile 装载；(b) manifest 在 Driver 选项期装载进 `Config.PlacementManifest`；(c) 跨对象合并/一致性断言/主符号约束/StorageClass 赋值均不消费 Globals 或解析后地址（节地址全 0、符号取输入值即可）。前移后 PlacementNames 先于任何解析存在，第二循环读取不再是循环依赖；相比之下"resolveSymbols 内嵌 merge 钩子"会把身份检查拆进解析函数、增加两态中间暴露，不取。
  * mergePlacement 职责（此时点）：跨对象合并（按 stable_symbol：重复 owned → §8 P-1 文案；owned↔bind → 字段语义合并表；仅 bind → 进 `PlacementNames`）；manifest 合取（§7）；设置各 FIXED 节的 `StorageClass/IsCode`（F11 依赖）；断言 §2.2 一致性与主符号约束；产出 `Result.Placement`（§3.4，含 bind-only 行）。
* **bind-only 外部实体进解析不产 storage（修订 4 数值源闭合；以下三处分支/豁免当前均不存在，G11-C 加入）**：
  1. 插入点一：`resolveSymbols` 第二循环（:1641-1647）内扩一分支——undefined、不在 Globals、且 `PlacementNames.count(S.Name)` → `S.Address = PlacementNames[S.Name].Address`；
  2. 插入点二（**重定位数值源改到实际写值处**）：修订 3 只在 :3390-3395 的 Target 回退处加 PlacementNames 是不够的——applyRelocations 的实际写入值来自 **Core:3479-3482** 的选择链 `IS->Defined ? IS->Address : Globals : Synth : 0`，该处既不读 `Target->Address` 也不读 PlacementNames，bind-only 会写入 **0+addend**。冻结：该链插入 PlacementNames 分支于 **Globals 之后、Synth/0 之前**：
     `IS->Address / Globals / PlacementNames / Synth / 0`（顺序即优先级：定义 > 全局解析 > 放置名 > 合成边界 > 0）；
  3. 插入点三：`errorUndefined`（:3369-3380）豁免条件加 `PlacementNames.count(S.Name)`（修订 3 裁定保留）；
  4. 无 section、无台账项、无输出字节。bind 函数同路径（入口 A、无尺寸核）。
* **validatePlacementResolution()（新函数，当前不存在；插入点 G11-C 于 :4587 validateSignatureSet/ISR 合成结束与 :4588 layout() 之间插入——post-resolve 轻量检查）**：bind-only 名与解析结果矛盾的唯一裁定——若某 `PlacementNames` 键在 Globals 中存在 **Defined** 符号（即别 TU 有无放置的定义）→ fail `conflicting placement for %sym: bind-only reference collides with a definition without placement`。有 owned NOTE 记录的同名体不进此检查（走 owned↔bind 合并表，且解析经 Globals 自然落到定义地址）。
* **输出符号路径（N3 闭合项；修订 5 补命名字段/排序/撞名三个裁定）**：bind-only 进 `collectSymbols`（:4506-4525）的 **Synth 通道**（:4522-4524）：每条 PlacementNames 记录输出 `{Name, Address, Size=0, STB_GLOBAL, STT_NOTYPE, Synth=true}`——"名字→地址合成符号"，与 s_XSEG 等边界符号同通道。
  * **命名字段裁定（修订 5，Alice 四审 N3 缺口）**：symtab 输出名 = **记录的 ELF 符号名（声明名），不是 stable_symbol**；`PlacementNames` 的**键 = ELF 符号名**（resolveSymbols 第二循环按 `S.Name` 查表的载体），值结构 = `{Address, Size, StorageClass, Entity, Stable}`（`Stable` 字段承载 stable_symbol 身份）。论证：(1) **可去重**——bind-only 强制 external linkage（`err_mcs251_bind_at_linkage`，§2.2），声明名在链接集内构造性唯一（Globals 键唯一 + validatePlacementResolution 拒撞无放置定义），无需借 stable_symbol 去重；(2) **可追溯**——stable_symbol 对 external 实体恒等于声明名（§3.2 身份段），本类实体上两载体字符串重合，追溯身份由 report 行的 `(输入文件, 符号, stable_symbol)` 三元组承担（§3.2），audit 键与输出名分工、互不替代；(3) **符号面契约**——symtab 消费方（nm/调试器/未来重链接）按声明名检索，合成符号用声明名才可被查到；stable_symbol 的 TU 限定形态（static 场景）若进 symtab 会把内部命名泄漏进用户符号面并破坏"名字→地址"契约。**回退规则**：仅经 manifest 引入、无 NOTE 无输入符号的实体（manifest-only），其可用标识只有 stable_symbol，此时键与输出名均取 stable_symbol（有声明名用声明名，无则回退 stable_symbol——回退分支正常输入不可达，lit 以 manifest-only fixture 覆盖）。
  * **collectSymbols 排序规则冻结（修订 5）**：维持既有输出顺序**纯追加**（E5 追加式语义，§3.4 字节面承诺）：`Emit(STB_LOCAL)`（:4520）→ `Emit(STB_GLOBAL)`（:4521）→ 既有 Synth 边界/栈符号（:4522-4524，map 键序）→ **bind-only Synth 行最后追加**（不插入既有序列之间；实现为 collectSymbols 末尾对 `Result.Placement` 的 BoundOnly 行追加遍历，不改 :4520-4524 的既有循环）。bind-only 行之间按 PlacementNames 键字典序，保证同输入序 → 同输出序（可重放）。
  * **撞名处理（唯一可执行裁定，修订 6 冻结；修订 5 的"既有 Synth 键（s_XSEG 等）"举例式表述废止）**："既有 Synth 键"不是开放集合，冻结为下述**完整 Synth 名称集合**，其封闭枚举以代码实际生成为准（HEAD 032c90d66 grep 实测全部 Synth 键构造点，清单入稿）：
    * **输出键空间构造点（`Synth` map，layoutData 内 ：3079-3236；collectSymbols :4522-4524 据此发射）**——无条件键：`s_DSEG`（:3079）、`l_DSEG`（:3087）、`l_IRAM`（:3088-3089）、`s_<R>`/`l_<R>` × {HOME, VECS, BOOT, CSEG, XINIT, XDATA_INIT}（:3092-3096 循环，12 键）；条件键：`s_OSEG`/`l_OSEG`（:3100-3101，存在 OSEG 节）、`s_ISEG`/`l_ISEG`（:3123-3125，存在 ISEG 节）、`s_SSEG`/`l_SSEG`（:3131-3132，存在 SSEG 组节）、`s_BSEG_BYTES`/`l_BSEG_BYTES`（:3146-3148，存在非空 BSEG_BYTES 节）、`s_BIT_BANK`/`l_BIT_BANK`（:3155-3160，存在 BIT_BANK 组节）、`s_REG_BANK_n`/`l_REG_BANK_n` n=0..3（:3173-3174，组存在）、`s_EDATA`/`l_EDATA`（:3192-3193，存在非空 EDATA 节）、`s_XSEG`/`l_XSEG`（:3208-3209，`hasAreaStart("XSEG")` 且存在非空 XSEG 节）、`__mcs251_stack_base`（:3236，StackRequested）。
    * **输入保留名集合（`isReservedBoundarySymbol`，:1596-1618）**——`l_IRAM`（:1601-1602）、`__mcs251_stack_base`（:1603-1604）、`s_`/`l_` × 18 区名 {DSEG, EDATA, OSEG, ISEG, SSEG, HOME, VECS, BOOT, CSEG, XINIT, XDATA_INIT, BSEG_BYTES, BIT_BANK, XSEG, REG_BANK_0, REG_BANK_1, REG_BANK_2, REG_BANK_3}（:1605-1615）。这是**形态级封闭枚举**（按名字形态匹配全部 18 区，无论本链是否实际生成），与上一条 map 实键集合不是同一个集合。
    * **两处职责分工（冻结，不得互相替代）**：`:1628`（resolveSymbols 第一循环消费 `isReservedBoundarySymbol`）是**输入侧**职责——拒绝用户输入定义符号占用保留边界名（源头防污染；只查 `F->Symbols` 中 `S.Defined` 的输入符号，bind-only 名称不经过它）；`:4522-4524` 是**输出侧**职责——把 layoutData 产出的 `Synth` map 实键发射为输出符号，该输出键空间（连同 defined 输出 ：4507-4518）就是 bind-only 去重的对象。bind-only 名称不是输入定义符号，:1628 对其失明，必须由本裁定新增的检查覆盖。
    * **检查序列（唯一检查点，fail-closed）**：collectSymbols 之前（此时 layoutData 已完成、`Synth` map 已填充，构造性可行）先构造**完整 Synth 名称集合 = 输入保留名形态集合（:1596-1618 封闭枚举）∪ 本链 `Synth` map 实际键集合（:3079-3236 构造点全集）**；bind-only 每个输出名先对 (1) defined 输出集合（:4507-4518 将输出的 `S.Defined` 非空名）与 (2) 该完整集合做不相交检查；全部通过后才按上方排序裁定追加到既有 Synth 序列之后（bind-only 行间字典序）。任一命中或检查遗漏 → `fail()`，**链接失败**（lit 冻结：bind-only 声明名取 `s_XSEG`/`l_IRAM`/`__mcs251_stack_base`/任一 18 区边界名/同链 defined 符号名，均链接退出非零）。实现必须双通道并查（静态封闭枚举 + map 实键），不得只背静态表或只查实键——防未来 Synth 新增键时枚举漂移；owned↔bind 合并体与撞定义体在 mergePlacement 已从 PlacementNames 剔除（§3.2 合并表 / validatePlacementResolution）仍维持，作为该唯一检查点的前置削减，不另行承担撞名终判。
* **layoutFixed{Code,Data,Xdata}（三个函数当前不存在；插入点见 §1.3 调用序，G11-C）**：按 §1.3 调用序；每个实体：`A % align==0`、`rangeFits(A,size)`、owned size==0 → `zero-size entity %sym at 0x%x is not placeable`（bind 函数 size=0 合法跳过；bind 对象 size==0 已在 mergePlacement 按 malformed 拒）；CODE：`reserveCode(A,size)`+Image（重复字节检查沿用 :2410-2416）+ **跨度=节跨度（含跳表，§3.2 实体跨度裁定）**；XDATA：重叠/64K 不跨（:3014-3052 同规则）/`--xdata-size` 门经 F3；AS0-DATA：16 位 `reserve()`。FIXED-CODE 窗口裁定见 §2.2。
* **碰撞检查落点裁定（修订 5，Alice 四审 N4 缺口：检查落点是布局期统一台账，不是只在 verifier）**：`layoutFixedCode()` 对每个 FIXED-CODE 实体以**实体跨度**（节跨度 sh_size，含跳表）调用既有 `reserveCode(A, sh_size)`（Core:2367–2376 成员函数定义；:2381–2383 仅为转发 lambda，:2406 既有调用点同一路径），将 `[A, A+sh_size)` 记入**统一 CODE 台账 `CodeUsed`**；此后 (a) 动态 CODE 分配循环的光标推进与重叠检查（:2384-2417）、(b) 任何后续固定预留（同函数内多个 FIXED 实体互检）消费**同一本台账**——"固定×固定"与"固定×动态"两个碰撞方向都在 reserveCode 的既有重叠失败路径统一报出，不新造检查逻辑。PROGBITS 镜像字节经 Image 写入复用既有 duplicate CODE byte 检查（:2410-2416）。AS0-DATA/XDATA 同理：FIXED 预留进 `DataUsed`/`XDataUsed`（§1.3 插入点），动态 first-fit 与位分配消费同一台账。**verifier 只做复核**（对称性/窗口/重叠的独立重验，§3.4），不承载首次碰撞发现职责；lit 断言两个方向的拒绝都发生在链接器（链接 exit 非 0 + 定位文案），verifier 漏检冗余防线不作为唯一防线。

### 3.4 链接后独立验证（G11-D，N3 修订 4 冻结）

**现状证据缺口**（维持修订 3）：LinkerResult（LinkerCore.h）无 placement/原节身份/flags 载体；Driver 仅 `--keep-symbols` 出符号表（:104/:372-377）；NOBITS 符号报 ABS；输出节名 `.mcs251.load.N`（:166-170）；输出节 flags 硬编码 `ALLOC|EXECINSTR`（:257-258）。

**裁定（修订 4 冻结）**：

* **不变式（维持）**：*"不保留原输入节形态" ≠ "丢弃实体"。* 实体存活的判定依据=最终 symtab 中该实体符号以约定地址/尺寸存在 + placement report 对应行，绝非输出节的存在性。NOBITS 固定对象本就无镜像字节。
* **LinkerResult 新增**：`struct PlacementRecord { std::string Stable, Sym, File, Section; uint32_t Address, Size, Align, Flags, LayoutHash; uint8_t StorageClass, Entity, Ownership; bool BoundOnly; }`；`std::vector<PlacementRecord> Placement;`（mergePlacement 产出的合并合约，含 bind-only 行；`LayoutHash` 专指按合并字段新算的 report hash，原记录 hash 由 verifier 从原始对象读取，不用此字段代替）。
* **report 与 KeepSymbols 的冻结关系（N3 闭合项，唯一裁定）**：**`--placement-report=<path>` 与 `--verify-placement` 都隐含 `KeepSymbols=true`**（两选项在 Driver 选项解析期置位 Core.KeepSymbols；:104 的 `EmitSyms=KeepSymbols && !Result.Symbols.empty()` 逻辑本身不变）。裁定理由：(1) 双证不变式"report 行必可在同一 ELF 的 symtab 找到符号"要求 report ⇒ symtab，任何"report 无 symtab"组合都是坏状态；(2) 单一耦合规则消灭四格组合矩阵，测试面最小；(3) 两选项本身就是审计 opt-in，字节面随之变化是声明过的语义（下条），不触 E5。`--keep-symbols` 仍可独立用于产品审计。
* **字节面承诺（显式收窄）**：**默认模式**（不请求 report/verify/keep-symbols 任一）保持输出字节逐位不变（E5 冻结哈希不受扰，symtab/strtab 严格追加在既有布局之后，Driver:91-104 语义维持）。**report/verify 模式不承诺字节不变**：强制 symtab 本身增加 ELF 内容（.symtab/.strtab/.shstrtab 增名与两节行），这是审计模式的声明属性，不得把"默认字节不变"扩述到该模式。
* **`--placement-report=<path>` 伴生通道**（map 同级文本，不属于 ELF 哈希面）：每行 `stable | sym | file | section | class | entity | ownership | A | size | align | flags | layout_hash | bound_only`；retain 行追加 `retained`。`layout_hash` 仅指合并 report hash（schema_version 隐含为 v1），不承载原记录 hash；若同一合并实体按来源三元组展开多行，各行携带相同合并字段与 report hash，原记录完整性仍逐份独立校验。最终 NOTE 的"生成/保留"由 report 承担（输出 ELF 不复制 NOTE 节，:250-270 的节构造不含该通道）。
* **双证枚举方向（N3 闭合项，冻结不变式）**：verifier 的枚举源是**原始输入对象的 NOTE 节 + manifest 文本**，不是 report——先逐条遍历 NOTE/manifest 行，再向 report 与 symtab 索证。冻结两条失败方向：(a) NOTE/manifest 行在 report 无对应行 → 失败（`VERIFY FAIL: no placement report row for placement record %sym`）——防"report 行本身丢失后无人发现"；(b) report 行无 NOTE/manifest 来源 → 失败——防 report 单方增行。之后才是逐记录的 symtab 断言。
* **缺 symtab 的明确失败行为（N3 闭合项）**：verifier 读输入 ELF，若无 symtab → **立即失败**，冻结文案 `VERIFY FAIL: input ELF has no symbol table (relink with --verify-placement or --placement-report)`，不做任何降级检查。
* **verifier 算法**（`mcs251-placement-verify`，输入=最终 ELF+report+原始对象+manifest+memory map）：先按 §3.2 B1 逐份核验原 NOTE 的自身 hash，再独立执行显式字段语义合并、核对 report 合并字段并校验 report 自身 hash；manifest 无原 hash 字段，按显式约束参与合并。按枚举不变式遍历；逐记录断言 symtab `st_value==A`；尺寸：owned 对象精确相等、owned 函数按实体跨度（NOTE/report 终值）、bind 只核地址（bind 函数不核尺寸）；`[A,A+size)` 在窗口内且两两不重叠；带 retain 的行在 report+symtab 存在；PROGBITS 固定 CODE 实体可对原始对象做镜像字节比对（NOBITS 跳过）；noinit 行无对应 XINIT/XDATA_INIT 覆盖；bind-only 行核引用 reloc 落 A（F10 边界同核）。失败非零。
* **retain 语义如实限定（维持）**：无 GC 链接器中链接端 RETAIN = 白名单接受 + 约束声明 + 未来兼容约定；"retained section 存在于输出镜像"只对 PROGBITS CODE 实体有字节级含义，其余实体的存活证据是 report+symtab。

### 3.5 旧 `_at_`（G11-E）

维持修订 2 §3.5，另加：65536 字节对象类改写目标本轮直接拒注入（§1.4 demo 85 裁定）。

## §4 切片依赖图与 S0 冻结状态

依赖链维持修订 2 §4（S0 → A → B → C → D 串行；E 在 A 拼写冻结后开工、D 绿后批量；B/C 仅脚手架/fixture 可提前）。**S0 合约状态两栏（修订 4；不宣布无条件冻结）**：

| 已冻结（设计层，以修订 7 本稿为准；本轮变更待定点复审） | PM veto window（当前无未决项；否决则回炉重设计） |
|---|---|
| 记录 v1 布局、字段编码、BE、envelope namesz=7+8 字节存储、未知编码 fail-closed | 无（以上为 pure schema） |
| stable-symbol 生成规则与 ELF 符号关系；节主符号约束（对象等式/函数不等式） | — |
| layout_hash 字段集（**不含 size**）、SHA-256 低 32 位；原记录完整性 hash 与合并 report hash 分别按自身字段核验，禁止跨语义相等要求；校验仅归 verifier，不参与链接决策（修订 7 B1） | — |
| 实体跨度定义（节跨度，含同节附属载荷）与 NOTE.size 符号差生成路径 | 依赖 G11-B emitter 标签对落地（实现项，非裁定项） |
| owned↔bind 字段语义合并表；bind-only 进 PlacementNames/解析/Synth 的三段路径 | — |
| report⇒KeepSymbols 耦合；双证枚举方向；缺 symtab 失败文案 | 无（§6.4 已采纳单一方案：report + symtab 通道；PM veto window 至 G11-A 开工） |
| retain 作用域=place_at 定义；bind+retain Sema 拒绝 | 无（§6.5 设计裁定已采纳；PM veto window 至 G11-A 开工） |
| F1-F13 改动面清单；FIXED-CODE 强制显式窗口（`fixed CODE entity` 文案） | 无（§6.6 已采纳单一方案：强制显式 CODE 窗；PM veto window 至 G11-A 开工） |
| manifest 行格式/mergePlacement 阶段/缺字段语义 | — |
| **修订 5 增**：Synth 输出符号命名字段（ELF 符号名/声明名，manifest-only 回退 stable_symbol）与 collectSymbols 排序/撞名三层 fail-closed 规则 | — |
| **修订 5 增**：FIXED 碰撞检查落点=reserveCode/reserve 进统一台账（CodeUsed/DataUsed/XDataUsed），动态与后续固定预留共消费，verifier 只复核 | — |
| **修订 5 增，修订 7 同步摘要**：节内实体唯一性（每节恰一实体/附属载荷归属=发出它的函数实体所在节；凡 defined 且 STT_FUNC/STT_OBJECT，无论尺寸是否为 0、绑定属性如何均计数，计数 ≠ 1 即拒；保持修订 6 边界） | — |

### 4.1 G13a/G13b 冲突登记与实施开工前门禁（修订 7，六审 N1）

参照 `/home/liu/LLVM_STC32/MCS251/validation/mcs251-models/proposals/G13B-XDATA-DESIGN-draft.md` 与 `/home/liu/LLVM_STC32/MCS251/validation/mcs251-models/proposals/G13A-CODE-DESIGN-draft.md`。此处登记交叠与集成义务，不把并行工作树修改当作 G11 回归，不改变本稿 HEAD 行号基准。

| 并行流 | 交叠域与文件 | 开工/集成约束 |
|---|---|---|
| G13b（直接产品交叠） | `llvm/lib/Target/MCS251/MCS251AsmPrinter.cpp` 的 AS3 发射；`llvm/include/llvm/BinaryFormat/ELF.h` 的 flags；`lld/MCS251/LinkerCore.cpp` 的 flags/section 分类、XSEG 放置、XDATA_INIT 校验与合成、CODE 预留 | G11-B/C 与 G13b 同文件产品切片必须串行集成；开工前登记先后顺序、集成基线与双方负责人，后集成方按已集成实现重核插入点及条件，不直接叠加旧行号补丁 |
| G13a（主要为配方/runtime 交叠） | CODE 窗 CLI、XINIT/XDATA_INIT 初始化镜像区配方、启动/runtime 消费路径与组合回归 | **S1 不改链接器产品源码**，不得登记为正在修改 LinkerCore；配方变化仍须与 FIXED-CODE/初始化记录组合回归。仅后备 **S5** 启用新链接区域时升级为直接产品交叠，再纳入同文件串行门禁 |

**FIXED × XSEG_SPLIT 唯一裁定**：同一链接中可同时存在合法 FIXED 实体与 G13b 动态 XSEG_SPLIT 实体；**同一 `.mcu.fixed.*` 节携带 `SHF_MCS251_XSEG_SPLIT` 不合法，分类阶段拒绝**（沿用 `unsupported ALLOC section flags for …`，fixture `fixed-xseg-split.yaml`）。即使 G13b 扩展 Common 掩码，FIXED 分支仍须显式排除此位；G11 emitter 不得为 FIXED 发该位。FIXED-XDATA 继续遵守非零、单对象 ≤65535、64K 不跨窗及既有容量门；G13b 对动态 XSEG 的放宽不外溢到 FIXED，demo 85 的固定放置仍不支持。混合链接仍共用 XDataUsed/CODE 台账，不能各自放置后绕开对方预留。

**集成后重核清单（门禁，PLANNED，不是已完成证据）**：

1. 按实际集成基线重核 mergePlacement、layoutFixed*、初始化合成的调用/插入点；保持 `validateIdentitySet → mergePlacement → resolveSymbols` 与 P-1/P-5 优先级。
2. 重放三台账清空→固定预留→动态消费、G8 种子快照/恢复；混合 FIXED 与动态 split 对象既有成功分配正例，也有重叠/容量拒绝负例；FIXED 超长/跨窗/带 SPLIT 位各自必须拒绝。
3. 重核 AS3 发射与 flags 分类、F3/F4/F9–F13、XDATA_INIT 合成记录及其目的范围；FIXED noinit 不得被合成 clear/copy 记录覆盖，合成记录的 CODE 区间仍须经 reserveCode 进统一 CodeUsed 后写 Image，不得绕过固定预留。
4. G13a 当前配方 × G13b 合成初始化 × G11 FIXED-CODE 组合回归：显式 CODE 窗成功、缺窗拒绝、越窗拒绝，以及固定区间与 XINIT/XDATA_INIT 镜像区冲突拒绝；显式窗义务不得被配方默认值或动态区域放宽替代。
5. 三套二进制身份 hash 与组合测试结果留档；§9 的 probe4/5 强断言门禁通过后才能计 B/C/D 验收。若同文件顺序、FIXED/SPLIT 隔离或组合回归尚未确认，对应产品切片不得开工/通过集成验收；登记顺序不改变 §4 的 G11 内部串行依赖。

## §5 验证矩阵（修订 4 增补：跨度四 fixture、混合容器四 fixture、retain/bind 组合）

| 类别 | 正例 | 负例/目标断言 |
|---|---|---|
| AS3 | 数组退化、取址、参数/返回、typedef、跨 TU 双向互赋≡`__xdata`（探针 1[5]） | AS3/AS0 错误转换、auto xdata；`address_space(-1)`/`(5)` 冻结诊断 |
| place | 标量/数组/函数、初始化、VMA≠LMA、对齐边界 | auto/成员、不可表示地址、未对齐、跨声明冲突、重叠、越窗口；**零尺寸对象（`int x[0]` + place_at）Sema 拒**（`err_mcs251_placement_zero_size`） |
| bind | 硬件对象、外部函数、跨 TU 引用、bind-only 进解析（reloc 落 A，无 storage；J16 调用 bind-only 函数过 F11；24 位指针过 F10 边界） | 初始化器/函数体/产生 storage；static 上 bind；跨 TU 尺寸/字段不一致；**bind+retain Sema 拒**（retain-bind.c）；bind 对象 sizeof==0 Sema 拒 |
| noinit/retain | place+noinit 无清零记录；place+retain 全链保留 | 带位输入今天必拒（探针 2[B] 动证）；修复后放行；无 retain 不带位 |
| keepalive 四格（N5） | AS0 对象 / AS3 对象 / AS4 对象（无 EXECINSTR）/ 函数 × {place, place+retain} 各自 编译→对象→链接→verifier；断言节 flags | 容器含无标记成员 fatal（探针 2[A2] 保持）；retain 位仅当声明 mcu_retain；非 ISR 模块普通函数 AS4 cast 根仍拒 |
| **混合容器（N5 新增）** | ISR+bit、ISR+placement、bit+placement、ISR+bit+placement 四类容器逐一 编译→对象→链接 全链 | 每类各掺一个 Unmarked 成员的负例 → 旧 :619 路径等价拒绝；四处判定一致断言 |
| FIXED 分配 | 探针 3 全链；pin 驱逐 first-fit；G8 重试后台账含 FIXED（负例：固定区间与迁移对象重叠必失败不静默） | duplicate CODE byte / XDATA overlap / DATA overlap；64K 跨窗；`--xdata-size` 含 FIXED-XDATA（F3）；flash 门含 FIXED-CODE（F6）；未配窗报显式窗口错误 |
| **函数跨度（N4 新增）** | 四 fixture（探针 4，生产端构造）：span-memcpy（体内 memcpy 调用被内联缩减）、span-longbranch（跨窗分支布局期扩展）、span-align（体内对齐）、span-jumptable（switch→同节跳表列）；每例断言 `NOTE.size == sh_size >= st_size`、固定邻接预留不与跳表字节重叠、verifier 窗口/重叠按跨度通过 | 对象侧（yaml 手工）：NOTE.size ≠ sh_size → mergePlacement `disagrees` 文案；函数 st_size > sh_size → 主符号约束拒绝 |
| 存储类轴（N2） | CODE 空间 const 对象（无 EXECINSTR）place 成功 | NOBITS 声明 storage_class=CODE、XDATA 带 EXECINSTR → `disagrees` 文案；**FIXED-XDATA + R_MCS251_16 → F9 文案（不静默截断）**；FIXED-XDATA + R_MCS251_24 出界 → F10 文案；bind-only XDATA 同两项 |
| hash 双语义（修订 7 B1） | §3.2 hash-owned-bind、hash-retain-bind、hash-align-merge：分别核验 source/report 自身 hash，合法合并成功 | verify-hash-source：仅损坏原记录 hash，链接不拒、verifier 拒；verify-hash-report：report 自身 hash 损坏由 verifier 拒 |
| 重复定义优先级（修订 7 B2） | 单 owned+n bind 正常合并 | dup-owned.c ×2 → mergePlacement 重复 owned；plain-dup.c ×2（无 placement）→ resolveSymbols 通用重复定义；note-dup-rec.yaml（单定义/双 NOTE）→ mergePlacement 专用文案；各例身份门与结构检查先通过 |
| NOTE/manifest | NOTE→link→verify 一致；双证枚举（NOTE/manifest 驱动，report 缺行=失败）；report⇒symtab 耦合 | 重复/畸形/不一致/manifest 冲突各自 §8 文案；归档/瘦归档硬错误（P-3 双魔数）；主符号约束（对象等式/函数不等式）；**无 symtab 的 ELF 送 verifier → 冻结失败文案** |
| rewrite/demo | 56 DMA XDATA place | **demo 85 移出正向清单**（>65535 单对象，G13b 路线）；不可安全转换拒注入 |

## §6 PM 决策点（修订 6：设计采纳单一方案 + PM veto window）

维持修订 2 三项（verifier map 预设、bind 函数 NOTYPE 入口、NOTE↔manifest 合取裁定确认）不变。修订 4 的"提案 + 依赖的 S0 合约状态"口径废止——第 4-6 项按"设计稿必须给出唯一答案"标准收敛为唯一方案，统一处置句式：**设计采纳单一方案；PM 保留实施前否决权（veto window：G11-A 开工前），否决则该项回炉重设计**。

4. **placement report + symtab 通道（设计采纳单一方案）**。`--placement-report=<path>` 文件 + 输出 symtab 增行是唯一最终证据通道；report⇒KeepSymbols 耦合、双证枚举方向、缺 symtab 失败行为按 §3.4/§4 已冻结裁定执行；report/verify 模式 ELF 字节增大属审计 opt-in 的声明属性（§3.4 字节面承诺）。历史备选"输出 ELF 内嵌 audit 节"已否决，理由：内嵌节改默认输出形态、违背默认模式字节不变纪律。依赖的 S0 状态无未决项。
5. **retain 本轮作用域=仅 place_at 定义的固定实体（设计采纳单一方案）**。裁定理由与冻结结果见 §2.2 retain 条：bind_at 声明携带 retain 由 Sema 拒绝（`err_mcs251_retain_no_definition`），NOTE `flags.bit0`/manifest `[retain]` 的 owned-only 约束随裁定冻结。若未来需要"对 bind 声明做约束声明"，须另立载体并重开裁定，不在本轮范围。设计采纳单一方案；PM 保留实施前否决权（veto window：G11-A 开工前），否决则该项回炉重设计。依赖的 S0 状态无未决项。
6. **FIXED-CODE 强制显式 CODE 窗（设计采纳单一方案）**。存在 FIXED-CODE 实体而 `--flash-base/--flash-size` 未配置 → 冻结错误（§2.2/§8）；检查点（layoutFixedCode 前置）、文案（`fixed CODE entity …`）、与可选门（:3335-3336）的关系均已冻结；旧链接可选行为维持。历史备选"型号预设窗口注入"已否决，理由：型号知识不进链接器（§1.4 层级裁定——芯片级=手册、板级窗口=链接器 CLI，链接器不内置任何型号预设）。依赖的 S0 状态无未决项。

## §7 NOTE 与 manifest 冲突优先级

维持修订 2 §7（合取 + 冲突硬错）。消费语义（修订 3 冻结，修订 4 补 retain 限定）：manifest 输入格式=行式键值：`place <stable_symbol> <A> [align=N] [size=N] [retain] [noinit] [bind] [data|xdata|code]`、`#` 注释；经 `--placement-manifest=<file>` 在 Driver 选项期装载进 `Config.PlacementManifest`；合并阶段=mergePlacement（修订 4 位置：resolveSymbols 之前）；**缺字段语义**：manifest 行缺 size → 仅地址约束（owned 以 NOTE 为准核尺寸；bind-only 不核）；缺 align → 1；缺 class → 按 NOTE，两者皆无 → 拒（`malformed placement manifest entry for %sym`）；manifest 与 NOTE 同实体字段按 §3.2 合并表逐字段处理，冲突 → `conflicting placement constraints (NOTE vs manifest) for %sym: 0x%x vs 0x%x`。manifest-only/bind-only 外部实体按 §3.3 进解析、不产 storage。**manifest 行带 `[bind]` 与 `[retain]` 组合 → malformed**（与 §2.2 Sema 裁定对齐）。

## §8 冻结诊断（修订 4：可达 fixture 修正 + 优先级裁定 P-1..P-6）

**clang（`DiagnosticSemaKinds.td`）**

| ID | 文案 | 检查阶段 | 正/负 fixture（lit 计划 ID） |
|---|---|---|---|
| err_mcs251_place_at_not_static | "`mcu::place_at` requires a static object or function definition" | Sema 声明检查 | pos: fixed-obj.c；neg: place-auto.c |
| err_mcs251_address_not_representable | "placement address %0 is not representable in the MCS-251 address model" | Sema attr 参数 | neg: place-addr24.c |
| err_mcs251_place_at_alignment | "placement address %0 does not satisfy alignment %1" | Sema attr 参数 | neg: place-align.c |
| err_mcs251_place_at_conflict | "conflicting `mcu::place_at` addresses %0 and %1" | Sema 同声明重入 | neg: place-twice.c |
| err_mcs251_place_at_overlap | "fixed placement range [%0, %1) overlaps another entity" | Sema 同 TU 两实体 | neg: place-overlap.c |
| err_mcs251_bind_at_init | "`mcu::bind_at` cannot have an initializer or function body" | Sema 声明检查 | neg: bind-init.c |
| err_mcs251_noinit_init | "`noinit` cannot be combined with an initializer" | Sema 组合检查 | neg: noinit-init.c |
| err_mcs251_place_incomplete | "fixed placement requires a complete type" | Sema 完整性 | neg: place-incomplete.c |
| err_mcs251_placement_zero_size（新） | "fixed placement entity %0 must have a non-zero size" | Sema 声明检查（place_at 与 bind_at 对象同查；**完整性检查不覆盖零长数组**——SemaType:2314-2321 非 SFINAE 情形允许零长数组扩展） | neg: place-zero-size.c、bind-zero-size.c |
| err_mcs251_address_space_unavailable | "address space %0 is not defined by the MCS-251 memory contract" | Sema 目标 AS 检查（前移） | neg: as5.c（现状动证探针 1[4]） |
| err_mcs251_retain_no_definition | "`mcu::retain` requires a definition"（**修订 4 裁定落点：覆盖 bind_at+retain 组合**——bind 声明无定义，本行即拒绝） | Sema 组合检查 | neg: retain-extern.c、**retain-bind.c（新）** |
| err_mcs251_retain_requires_placement | "`mcu::retain` requires a fixed placement (`place_at`) in this profile"（文案随裁定收窄为 place_at） | Sema 组合检查 | neg: retain-plain.c |
| err_mcs251_bind_at_linkage | "`mcu::bind_at` requires an entity with external linkage" | Sema | neg: bind-static.c |

**lld（`LinkerCore.cpp` `fail()` 冻结字符串；"阶段"列含触发点，行号为 HEAD 032c90d66）**

| 场景 | 文案 | 阶段/触发点 | 正/负 fixture |
|---|---|---|---|
| 归档/瘦归档输入 | `archive inputs are not supported: %file (link placement objects directly)` | loadFile 输入格式识别：`createObjectFile`（:926）之前检查首部 **8 字节双魔数 `!<arch>\n` 与 `!<thin>\n`**（输入缓冲已在 :924 装载为 `F.Buffer`）；现状是通用 "not recognized"（探针 2[C]），G11-C 改写为专用文案并覆盖 thin archive | neg: arch-input.a、arch-thin.a |
| NOTE 畸形 | `malformed placement NOTE in %file: %reason`（含未知 kind/flags/截断/record_size/namesz≠7/bind 带retain 位/**bind 对象 size==0**） | loadFile 节循环记录解析 + mergePlacement 复核 | neg: note-trunc.yaml、note-bindsize0.yaml |
| NOTE 重复 owned | `placement NOTE duplicate owned record for %sym in %file and %file` | mergePlacement（可达性见 P-1） | neg: **dup-owned.c ×2（两个同名 external owned 定义，各一条合法 NOTE，身份字段相同）**；**note-dup-rec.yaml（手工对象：单一定义符号 + 两条同 stable_symbol 的 owned 记录）**；均断言本行专用文案 |
| section↔NOTE 不一致 | `section %name disagrees with placement NOTE for %sym` | mergePlacement（结构/存储类/**owned：NOTE.size==sh_size；函数：st_size<=sh_size**） | neg: class-mismatch.yaml、span-mismatch.yaml |
| retain 位丢失 | `fixed section %name declares retain but lacks SHF_GNU_RETAIN` | mergePlacement：NOTE flags.bit0=1 而节 flags 无位 | neg: retain-drop.yaml |
| 多余 retain 位 | `fixed section %name carries SHF_GNU_RETAIN but placement NOTE flags.bit0=0` | mergePlacement：`.mcu.fixed.*` 节带位而 NOTE flags.bit0=0（F1 双向互斥裁定 (ii) 反向；分类时的位屏蔽仅用于比较，不作为接受依据） | neg: retain-extra.yaml |
| 节名与 NOTE flags 不一致 | `section %name carries SHF_GNU_RETAIN outside .mcu.fixed.*` | classifySection 之后全到达路径兜底（F1 双向互斥裁定 (i)：Common 掩码精确比较之外的一切路径携带该位一律拒绝；现状由 `unsupported ALLOC section flags` 先行拒绝，探针 2[B]，本行冻结残余到达路径） | neg: retain-wrongsec.yaml |
| FIXED-CODE 显式窗口 | `fixed CODE entity %sym requires an explicit CODE window (--flash-base/--flash-size)`（**修订 4 统一命名：函数与 CODE 对象同文案**） | layoutFixedCode 前置检查 | neg: fixed-code-nowin |
| 代码实体 CODE 越界 | `fixed CODE entity %sym at 0x%x is outside the CODE window` | layoutFixedCode（布局期），先于 checkFlashGate（P-2） | neg: fixed-code-out.c |
| 同址零长实体 | `zero-size entity %sym at 0x%x is not placeable` | layoutFixed*（**仅 owned**；bind 函数 size=0 合法跳过；bind 对象 size==0 已按 malformed 在 mergePlacement 拒，见 P-4） | neg: zero-sized.c |
| 16 位通道截断 XDATA | `XDATA symbol %sym truncated to 16 bits (use the 24-bit relocation channel) in %sec` | applyRelocations F9 门（:3424-3440 扩分类制：XSEG ∨ FIXED-XDATA ∨ bind-only-XDATA）——**拒绝而非 :3567-3569 的低 16 位写入**（P-6/F12 不变式） | neg: fixed-xdata-16.yaml |
| 存储指针出界 | `stored XDATA pointer in %sym resolves outside the target object %sym: …` | applyRelocations F10 门（:3513-3532 扩：FIXED-XDATA 节跨度 / bind-only 记录 [A,A+size)，one-past-end 同规） | neg: fixed-xdata-oob.yaml |
| bind-only 与无放置定义撞名 | `conflicting placement for %sym: bind-only reference collides with a definition without placement` | validatePlacementResolution（post-resolve，P-5） | neg: bind-vs-plaindef.c |
| 跨 TU/stable 撞名冲突 | `conflicting placement for %sym: 0x%x (%file) vs 0x%x (%file)` | mergePlacement（合并表任一字段冲突；stable_symbol 身份碰撞，见 P-1/P-5 边界） | neg: cross-tu.c ×2 |
| NOTE↔manifest 冲突 | `conflicting placement constraints (NOTE vs manifest) for %sym: 0x%x vs 0x%x` | mergePlacement（§7） | neg: manifest-clash |
| manifest 畸形 | `malformed placement manifest entry for %sym`（含 `[bind]`+`[retain]` 组合） | mergePlacement 装载 | neg: manifest-bad.txt |

**verifier（`mcs251-placement-verify`，独立进程，非零退出）**

| 场景 | 文案 | 阶段 | 正/负 fixture |
|---|---|---|---|
| 无符号表 | `VERIFY FAIL: input ELF has no symbol table (relink with --verify-placement or --placement-report)` | 入口检查（N3 冻结） | neg: verify-nosymtab |
| 枚举缺行 | `VERIFY FAIL: no placement report row for placement record %sym` / `VERIFY FAIL: placement report row %sym has no NOTE/manifest source` | NOTE/manifest 驱动枚举（N3 冻结双证方向） | neg: verify-reportmissing / verify-reportextra |
| 地址不符 | `VERIFY FAIL %sym: address 0x%x != expected 0x%x` | ELF symtab×report | pos/neg: verify-addr |
| 尺寸不符 | `VERIFY FAIL %sym: size %0 != recorded %1` | 同上（bind 只核地址；owned 函数按实体跨度） | neg: verify-size |
| retain 实体缺失 | `VERIFY FAIL: retained entity %sym absent from placement report/symtab` | report+symtab | neg: verify-retained |
| 越窗口/重叠 | `VERIFY FAIL: %sym [0x%x,0x%x) outside %region` / `VERIFY FAIL: %sym overlaps %sym` | map/记录（按实体跨度） | neg: verify-window / verify-overlap |
| 记录无实体 | `VERIFY FAIL: no output entity for placement record %sym` | symtab | neg: verify-absent |
| layout_hash 损坏 | `VERIFY FAIL: layout hash mismatch for %sym`（附原始来源或 report 行定位；字段集不含 size） | verifier 分别按原记录原字段、report 合并字段作自身重算比对；不跨语义比较，不在 mergePlacement 校验 | neg: verify-hash-source / verify-hash-report（§3.2） |

**优先级裁定（N7 修订 4：竞争关系与可达性）**

* **P-1（修订 7 B2）duplicate definition 与 NOTE 重复 owned 的边界**：唯一调用序为 **`validateIdentitySet → mergePlacement → resolveSymbols`**（§1.3/§3.3）。fixture 须先通过输入结构与身份门。`dup-owned.c ×2` 各发一条合法 owned NOTE、具有同名 external 符号/同 stable_symbol 且身份字段相同 → **mergePlacement 先报 `placement NOTE duplicate owned record`**，不进入通用重复定义检查；身份字段不一致时按 P-5 先报 stable 冲突。**无 placement** 的 `plain-dup.c ×2` 不在 placement 分组中，mergePlacement 不裁普通重复定义，继续由 `resolveSymbols()`（Core:1637）报通用 `duplicate definition`。另保留 **note-dup-rec.yaml**：单文件一个定义符号 + 两条同 stable_symbol、同身份字段的 owned NOTE，专门证明记录层重复独立于符号重复，仍报 NOTE 专用文案。owned↔bind 不属于 duplicate-owned 服务面，按 §3.2 字段语义合并，失败走 `conflicting placement`。
* **P-2（维持）FIXED 越界先于 CODE ROM overflow**：`layoutFixedCode` 布局期报 `fixed CODE entity … outside the CODE window`；`checkFlashGate` 的 `CODE ROM overflow` 继续只覆盖动态 CODE 区，两文案不双报。
* **P-3（修订）归档魔数覆盖 thin archive**：识别点=loadFile 首部 8 字节，**双魔数 `!<arch>\n` 与 `!<thin>\n`**，先于 `createObjectFile`（:926）的通用失败；文案不区分两种归档。
* **P-4（修订）零长收窄回 bind-function，bind object 用显式检查**：owned 记录 size==0 → 链接端拒（兜底）；bind **函数** size==0 合法（无尺寸约束，尺寸检查整体跳过）；bind **对象** size=声明 sizeof，sizeof==0 由 Sema `err_mcs251_placement_zero_size` 显式拒绝（**完整性检查不兜底**：SemaType:2314-2321 非 SFINAE 情形允许零长数组扩展）；万一到达链接端（手工会/损坏对象）→ mergePlacement 按 `malformed placement NOTE` fail-closed。
* **P-5（修订 7 同步优先级）stable_symbol 撞名 vs duplicate owned vs bind-only 撞定义的归属边界**：在 resolveSymbols 尚未执行的 mergePlacement 中，按 stable_symbol 分组后按序判定——(1) 组内 ≥2 份 owned 且身份字段（sym 名/storage_class/entity）**不一致** → `conflicting placement for %sym`（stable 撞名，身份碰撞）；(2) 组内 ≥2 份 owned 且身份字段**全等**（同实体重复记录）→ `placement NOTE duplicate owned record`；(3) 组内 0 份 owned、n 份 bind：bind 记录间按合并表合并，冲突 → `conflicting placement for %sym`；(4) 组内 1 owned + n bind：字段语义合并表；(5) bind-only 键在 post-resolve 撞 Globals 定义 → P-5 专项文案（validatePlacementResolution）。判定顺序即优先级，四类文案互斥可达。
* **P-6（新）16 位截断的门序不变式**：F9 门（:3424-3440）必须先于 :3548-3557 溢出带检查与 :3567-3569 低 16 位写入执行；任何 XDATA 分类目标取 16 位通道得到 F9 文案。**修订 7 拆为两测，不能互相代替**：(a) 黑盒 lit：FIXED-XDATA 符号 + `R_MCS251_16` 必非零退出且命中 F9 文案，在全新输出路径不生成成功 ELF/镜像；不得读取通常不存在的失败 ELF 来声称内部未写字节。(b) 内部门序测试（G11-C 单测/测试钩子）：对该失败重定位预置 Image 目标 2 字节哨兵并观测写入路径，断言 F9 返回失败先于溢出带检查/写入、该重定位写入次数为 0、哨兵不变。此断言仅针对该重定位，不声称此前布局从未写 Image。

（修订 2 的"24 条均有可达触发落点"主张维持降级：可达性以逐行"阶段/触发点+fixture"为准，未实现前 fixture 为计划 ID。本表"阶段/触发点"列引用的新函数/新分支（layoutFixed*、mergePlacement、validatePlacementResolution、F9 分类制扩展等）**当前工作树不存在**，行号为插入点参照；实现归属切片：clang 诊断=G11-A、emitter/NOTE=G11-B、链接端=G11-C。）

## §9 探针与证据

目录 `/home/liu/LLVM_STC32/GAP-G11-PROBES/`；`RESULTS-REVISION.txt` 为修订 3 一次完整重跑（维持，不再重复执行），三档标注：
* `run-probe1-as-syntax.sh`：[1] `__addrspace` 不存在；[2] AS≡`__xdata` IR；[3] `-1` Sema 报错；[4] AS5 后端 fatal；[5] 双向赋值 + `__typeof__` 两侧互证，0 个 addrspacecast。
* `run-probe2-retain.sh`：[0] 本树 custom-section 门 fatal；[A] [DEMO] 外部 clang 14 x86 演示；[A2] [TREE] retain+`__xdata` fatal 于身份门、used+AS0 fatal 于 Reject 门；[B] [TREE] 正确位 0x200002 → `AR`、`unsupported ALLOC section flags` exit 1；[B2] 无位对照 exit 0；[C] 归档硬错误。
* `run-probe3-fixed-vma.sh`：DATA_ABS 端到端（先例证据）。
* **修订 4 新增（设计轮 fixture，PLANNED——实现归属切片见各行；修订 7 明确：现存 probe4/5 不是强断言自动验收器，能力门 SKIP 不能计通过）**：
  * `run-probe4-span-fixtures.sh` + `span-memcpy.c`/`span-longbranch.c`/`span-align.c`/`span-jumptable.c`：函数实体跨度四类生产端构造；**计划断言（当前脚本尚未落实）** `NOTE.size==sh_size>=st_size`、邻接固定预留不撞跳表字节、verifier 跨度窗口通过（§5"函数跨度"行）。**状态 PLANNED→G11-B/G11-C**：能力门依赖尚不存在的 `place_at` 属性与 `.mcu.fixed.*` emitter（G11-B）及链接端 FIXED 区（G11-C），当前 SKIP 只表示未执行目标验收，不能证明实现通过。
  * `run-probe5-reloc-gates.sh` + `fixed-xdata-16.yaml`/`fixed-xdata-oob.yaml`/`bindonly-j16.yaml`：F9/F10/F11 三门与 P-6 门序不变式的链接端负例（yaml2obj 手工对象）。**状态 PLANNED→G11-C**：[P5-1..3] 目标断言需 G11-C 的 FIXED Region/PlacementNames/门扩展落地后断言。
  * **[T0] 当前树基线（修订 5 在 HEAD 032c90d66 实测，2026-09-16）**：三个对象今天全部在进入任何放置逻辑前被通用检查拒绝——fixed-xdata-16/fixed-xdata-oob 报 `unsupported ALLOC section .mcu.fixed.xd_dev`（exit 1）、bindonly-j16 报 `unsupported non-ALLOC metadata section .mcs251.placement`（exit 1）；能力门探到 `unknown option --placement-report` 后 SKIP。该基线实证 F9-F11 门今天对 FIXED/bind-only 目标失明（门不可达），仅为基线证据、**不是实现闭合证据**。
**probe4/5 验收禁用条件与四项义务（修订 7，六审 N2）**：以下全部落实为强断言自动验收器之前，现存脚本及其 SKIP/打印输出**不得用于 G11-B/C/D 验收**。本轮只登记，不修改脚本；既有 [T0] 仅为留档基线。

| # | 待完成义务 | 归属与验收判据 |
|---|---|---|
| N2-1 | probe4 与生产 fixture 的 GNU 拼写统一为 `__attribute__((mcu_place_at(...)))`，消除裸 `place_at` 拼写 | G11-B fixture 准入：属性确被识别，未知/被忽略属性不能算成功 |
| N2-2 | probe4 实际解析每个对象的 placement NOTE.size，与 sh_size/st_size 比较；真实调用 verifier，检查退出码与窗口/邻接断言 | G11-B 提供解析断言，G11-C/D 接通链接+verifier；四跨度 fixture 全部跑到目标阶段并通过，打印值不算断言 |
| N2-3 | 修复 probe5 能力链接“任何原因失败即 SKIP”会掩盖回归的问题，并为目标段补强断言 | G11-C：仅在独立前置探测确认工具缺失/功能尚未提供时明确标未就绪；验收模式该情况也不得计通过。能力已声明具备后，任何编译/链接/解析异常均 FAIL，不得降格 SKIP；逐例强断言成功/失败退出码、准确 F9/F10 文案、F11 成功与实际重定位值，通用输入拒绝不是目标门通过 |
| N2-4 | P-6 “失败后未写字节”拆为黑盒拒绝/无成功输出与内部门序/零写入两测 | G11-C 依 §8 P-6：黑盒在新路径检查失败文案与无输出；内部用哨兵/写入观测证明 F9 先于写入。缺任一测不得宣称门序验收通过 |

* 实施前仍须冻结三套二进制 hash 后重跑全部探针与 G5 lit；probe4/5 依上述义务完成后才可升级为验收证据。

## §10 撤回与降级主张清单（同步至修订 7；历史条目按本轮裁定纠正）

1. **撤回**："三时点构造性相等（NOTE.size == sh_size == st_size）"（修订 3 §3.2）→ 实体跨度=节跨度（含同节附属载荷）；st_size 语义不变、函数侧约束放宽为 `st_size<=sh_size`；含跳表函数不再被拒（§3.2）。
2. **撤回**："NOTE.size 在 doFinalization 以最终发射尺寸写出" → 同源符号差 fixup（`<stable>.end − <stable>.begin`），`finish()` 布局后解析；doFinalization（BasePrinter:3218）早于 finish（:3223）无终值可取（§3.2）。
3. **撤回**："layout_hash 输入含 size" → size 移出 hash 输入（布局派生属性，时点不可满足）；尺寸一致性由三载体直接互核（§3.2）。
4. **撤回**："owned↔bind 要求 A/尺寸/layout 字段全等" → 字段语义合并表（bind 函数 0=无尺寸约束；ownership 允许不同；逐字段三选一）（§3.2）。
5. **调用序唯一化**：`validateIdentitySet → mergePlacement → resolveSymbols`；PlacementNames 在解析消费前生成，重定位数值源落到 :3479-3482 选择链（PlacementNames 分支在 Synth/0 之前）（§3.3）。
6. **撤回**："`--verify-placement` 置 KeepSymbols（仅此）" → report 与 verify 双选项均隐含 KeepSymbols=true；默认模式字节不变承诺显式收窄，report/verify 模式字节增大为声明属性（§3.4）。
7. **撤回**："`bind 尺寸恒 0` 覆盖整个 bind 类别"（修订 3 §8 P-4 表述）→ 收窄回 bind-function；bind object=sizeof，零尺寸由新 Sema 诊断显式拒（完整性检查不兜底，SemaType:2314-2321）（§8 P-4）。
8. **改判（修订 7 B2）**：`dup-owned.c ×2`（同身份 placement 重复 owned）由前置 mergePlacement 报 NOTE 专用文案；`plain-dup.c ×2`（无 placement）才由 resolveSymbols（Core:1637）报通用 `duplicate definition`；note-dup-rec.yaml（单定义+双 NOTE）继续覆盖记录层重复。唯一调用序为 `validateIdentitySet → mergePlacement → resolveSymbols`，身份碰撞优先级按 P-5；owned↔bind 不属于 duplicate-owned 文案服务面。
9. **修订**：归档识别从单一 `!<arch>\n` 扩为双魔数（+`!<thin>\n`）（P-3）。
10. **修订**：`fixed function` 文案统一为 `fixed CODE entity`（函数与 CODE 空间对象）（§2.2/§8）。
11. **新增裁定（非撤回）**：s_XSEG/l_XSEG 维持仅动态 XSEG（F13）；keepalive 分类前移（旧 ：619 硬失败前移为 Unmarked 判定）；retain 仅限 place_at 定义、bind+retain Sema 拒（§2.2）。
12. **收敛（修订 6）**：PM-4/5/6 由"提案 + 备选"收敛为唯一方案（report+symtab 通道 / retain 仅限 place_at 定义 / 强制显式 CODE 窗）；"输出 ELF 内嵌 audit 节"与"型号预设窗口注入"两备选废止；§11 三行状态改"设计已采纳（PM veto window 至 G11-A 开工）"。同轮新增裁定（非撤回）：Synth 撞名完整集合封闭枚举与唯一检查点（§3.3）；实体计数边界"凡 defined 且 STT_FUNC/STT_OBJECT 一律计数"（§3.2）；RETAIN 双向互斥三条统一裁定（F1）与 §8 两条新文案。

13. **收敛（修订 7 B1）**：原记录完整性 hash 按各自原字段核验；合并 report hash 按合并字段新算并核验自身；取消跨载体 hash 相等要求，所有 hash 校验唯一归 verifier，mergePlacement 只按显式字段裁定。补 owned+bind、retain+bind、align 合并三正例与 source/report hash 损坏负例（§3.2/§3.4/§5/§8）。
14. **同步（修订 7 B2）**：第 8 项、P-1/P-5 与诊断/验证表统一前置 mergePlacement 优先级，不改变符号解析本身的普通重复定义规则。
15. **登记（修订 7 N1/N2）**：§4.1 补 G13b 直接产品交叠、G13a 配方/runtime 交叠（S1 不改链接器，后备 S5 才涉及新区域）、FIXED/SPLIT 同节禁止与同链混合合法、串行集成及重核门禁；§9 登记 probe4/5 四项强断言义务，完成前禁用于 B/C/D 验收；P-6 拆黑盒/内部门序两测。
16. **文字纠错（修订 7）**：§4 实体计数摘要同步修订 6 完整边界；reserveCode 真正定义补准 Core:2367–2376（:2381 为转发 lambda）；稿首与 §11 同步修订 7。PM-4/5/6 单一方案、Synth 封闭枚举、实体计数边界、RETAIN 双向互斥四项裁定不重开。

## §11 N1–N8 / PM 状态表（修订 7：设计裁定状态 × 实现状态双列）

**口径声明（Alice 二审原话）："已闭合=设计评审闭合，不代表功能已实现。"** 本表不使用单独的"闭合"字样作为行状态；"设计裁定状态"列只回答"本稿是否给出构造性裁定"，"实现状态"列只回答"当前工作树落地到哪一步"（PLANNED→目标切片号，或已验证）。切片号：G11-A=Clang 前端/Sema、G11-B=IR/对象合约/emitter、G11-C=lld 链接端、G11-D=verifier。

| 编号 | 设计裁定内容 | 证据位置（稿节/源码/探针） | 设计裁定状态 | 实现状态 |
|---|---|---|---|---|
| N1 台账清空会抹掉 FIXED 预留 | `layoutFixed()` 拆三类子步骤嵌于三处清空之后、消费之前；G8 种子构造性包含 FIXED | §1.3 调用序（行号 HEAD 032c90d66）；探针 3[1-4] | 已裁定 | PLANNED→G11-C |
| N2 分类轴与过滤器 | 正交 storage_class/entity/ownership；F1-F13 完整改动面（重定位三门 F9-F11、截断处置 F12、统计裁定 F13）；bind-only 判定载体=PlacementNames 记录；`fixed CODE entity` 文案统一 | §2.2 表；F9=Core:3424-3440、F10=:3513-3532、F11=:3559-3561、F12=:3548-3557/:3567-3569、F13=:3196-3211 | 已裁定 | PLANNED→G11-C |
| N3 verifier 证据通道 | report⇒KeepSymbols 耦合裁定；缺 symtab 冻结失败；bind-only→Synth 输出符号通道+命名字段/排序/撞名裁定（修订 5；修订 6 撞名集合封闭枚举+唯一检查点）；双证由 NOTE/manifest 驱动枚举（缺行=失败）；字节承诺收窄 | §3.3/§3.4；Driver:104/:372-377；collectSymbols :4506-4525（:4507-4518 defined、:4522-4524 Synth）；Synth 构造点 ：3079-3236、保留名 ：1596-1618/:1628 | 已裁定 | PLANNED→G11-C（Synth/PlacementNames）+G11-D（verifier/report） |
| N4 S0 合约缺口 | owned↔bind 字段语义合并；实体跨度/NOTE.size fixup；namesz；merge 前置与数值源；reserveCode 统一台账/节内单实体；**修订 7 B1：原记录与合并 report 两种 hash 自身核验，仅 verifier 校验、不参与链接决策；计数摘要与修订 6 同步** | §3.2/§3.3/§3.4；BasePrinter:2531-2539/:2558/:3218/:3223；Printer:1043/:1048-1054/:1095-1108；Core:3479-3482/:1641-1647/:2367–2376 | 修订 7 已裁定，待定点复审（既有已通过边界不变） | PLANNED→G11-B（schema/emitter）+G11-C（merge/layout）+G11-D（hash 核验）；数字例已复算，不代表功能实现 |
| N5 keepalive 根 | 分类谓词前移到旧 ：619 硬失败之前；四处单点判定多处消费；retain 仅限 place_at 定义、bind+retain Sema 拒；混合容器四 fixture（落点文件纠错=AsmPrinter.cpp，修订 5） | §3.2 keepalive 段；Printer（MCS251AsmPrinter.cpp）:612-621/:619/:1987-1990/:2111-2114/:2120-2123/:2160-2168；探针 2[A2] | 已裁定 | PLANNED→G11-A（retain/bind Sema 诊断）+G11-B（谓词与四处消费/emitter） |
| N6 探针纠错 | 0x200002 重跑真动证；[DEMO]/[TREE] 分档；probe1[5] 双向+typeof | §9；RESULTS-REVISION.txt | 已裁定 | 已验证（探针 1-3 已运行，RESULTS-REVISION.txt 三档全量） |
| N7 §8 诊断 | 四列表 + P-1..P-6；**修订 7 B2：placement 重复 owned 由 merge 先裁、无 placement 普通重复定义归 resolve、单定义双 NOTE 保留**；P-5 身份碰撞边界不变；P-6 按六审 N2 拆黑盒与内部门序两测 | §1.3/§3.3/§5/§8/§10.8；SemaType:2314-2321；Core:1637/:924-926/:4563-4566 | 修订 7 已裁定，待定点复审 | PLANNED→G11-A（Sema/fixture）+G11-C（链接端文案/门序两测）；可达构造已定义，待实现验证 |
| N8 PM 材料 | 7E/7F 勘误；demo 85 归 G13b | §1.4、§5、§10 | 已裁定 | 已验证（手册编址表转写已逐行核对；勘误属设计材料，无产品代码） |
| 六审 N1（区别于历史 N1）集成门禁 | G13b 产品交叠；G13a S1 配方/runtime、后备 S5 新区域；FIXED/SPLIT 同节拒绝与同链合法；同文件串行集成+重核清单 | §4.1 | 修订 7 已登记并裁定，开工前门禁待执行 | PLANNED→G11-B/C 与 G13a/G13b 协同集成；未授权开工 |
| 六审 N2（区别于历史 N2）probe4/5 义务 | GNU 拼写、NOTE.size 解析+verifier、SKIP 不掩盖回归、P-6 两测；完成前禁用于 B/C/D 验收 | §9/§8 P-6 | 修订 7 已登记，验收准入受限 | PLANNED→G11-B/C/D；本轮未改/未重跑脚本，未计功能验收 |
| PM 决策点 4（report 通道） | 设计采纳单一方案：placement report + symtab 通道；"输出 ELF 内嵌 audit 节"备选已否决（改默认输出、违背字节不变纪律） | §6.4、§3.4、§4 两栏表 | 设计已采纳（PM veto window 至 G11-A 开工） | PLANNED→G11-D |
| PM 决策点 5（retain 范围） | 设计裁定：retain 仅限 place_at 定义、bind+retain Sema 拒（§2.2，唯一方案） | §6.5、§2.2 | 设计已采纳（PM veto window 至 G11-A 开工） | PLANNED→G11-A+G11-B |
| PM 决策点 6（固定 CODE 窗） | 设计采纳单一方案：强制显式 CODE 窗；"型号预设窗口注入"备选已否决（型号知识不进链接器） | §6.6、§2.2 | 设计已采纳（PM veto window 至 G11-A 开工） | PLANNED→G11-C |

## 实施补记（G11-B R2 复批建议②；2026-09-17，G11-C 收尾实例追加）

本节仅登记已实现并已验证的 A/B 保活职责时点与接收面，不改变任何既有裁定；修订 8 由协调员另行应用，本节不与其冲突。

* **A 层注册时点（clang）**：`CodeGenModule::Release()` 内、**最终属性刷新之后、优化管线之前**，把每个 bind（`mcu_bind_at`）载体的全局值注册进 `llvm.compiler.used` 容器。选此时点的理由：`Release()` 前的属性刷新（`mcs251-place`/`mcs251-stable-symbol` 写入）是最后一次可写 IR 属性的时机，而 `compiler.used` 必须在优化前成型——否则优化器会把"仅有声明、无普通引用"的 bind 载体当死值删除（`addUsedGlobal` 的既有机制见 `CodeGenModule.cpp` 的 `RetainAttr` 路径；bind 走同一容器通道，仅成员分类不同）。
* **B 层接收面（llc）**：`registerMCS251BindCarriers`（AsmPrinter 侧）作为**手写 IR 接收面的双保险**存在——clang 产物已由 A 层保证容器成员身份，手写 IR/直接 llc 输入则由 B 层在对象发射前把 bind 载体补进同一 keepalive 分类谓词（`classifyMCS251KeepaliveMember` 的 `Placement` 类），使两条输入路径在"bind 载体不被当普通 RAM 预留/不被 Reject 链拒绝"上等价。
* **边界**：两者都只影响 keepalive 容器身份，不改变 §3.2 的节发射几何、NOTE 记录或任何链接端裁定；混合容器四格（ISR/Bit/Placement/Unmarked）验收面不变。

---

## 修订 8（2026-09-17，协调员；应用 N4/N7 裁定）

本修订按 **PM-RULINGS R-2026-09-17-1**（用户拍板，采纳 `G11-N4N7-DESIGN-Alice.md` 的设计推荐）应用。历史修订 2–7 不回写；下文各段**替换**所指节的相应文字，未列出的条款维持原状；实施状态一律记"设计裁定完成，实施/验收未完成"。

| 项 | 落点 | 依据 |
|---|---|---|
| N4 身份编码（方案甲′） | §3.2 身份段 | R-2026-09-17-1 第 1 项 |
| N4 命名字段裁定 | §3.3 相应段 | R-2026-09-17-1 第 1 项 |
| N4 符号关联载体 `.mcs251.placement.names` | §3.2 schema 增补（S0 补裁） | R-2026-09-17-1 第 2 项 |
| N7 noinit 合并政策（选项甲：owned 权威） | §2.2 增补 + §3.2 合并表两行 + §7 增补 | R-2026-09-17-1 第 3 项 |
| 实施补记表述纠正 | §实施补记 | G11-C R2 评审 §五 |

### 8.1 实体身份与 stable-symbol（N4 修订；替换 §3.2 身份段）

G11 的实体身份键为 `stable_symbol`，不是 ELF 符号名。身份只由 AST 声明结构、既定 TU 限定和局部静态源序号产生；asm-label、IR/ELF 发射名、模块级临时编号、发射顺序及优化级均不得进入身份。

令 `T = 下划线化的主源文件基名 + "." + 8 位大写十六进制 FNV-1a(拼写路径绝对化后的主源文件路径)`。路径绝对化、符号链接拼写和前导零规则沿用已提交实现，不改用 realpath。

对非函数局部静态的对象或函数，定义顶层身份分量 `E(D)`：

1. 在 C 和 C++ 两种语言模式下均查询目标 Itanium `MangleContext::shouldMangleCXXName(D)`。
2. 谓词为真时，`E(D)` 为对应声明的纯 `mangleCXXName` 编码；函数重载签名、命名空间、类上下文及具体模板参数由该编码承载。不得使用会遵循 asm-label 的 `mangleName`，也不得从 LLVM GlobalValue 的名字反推身份。
3. 谓词为假时，`E(D)` 为声明标识符原文，不加语言模式标签；因此普通 C 与对应 C++ `extern "C"` 声明具有相同分量。
4. 谓词为假且声明名以 `_Z` 开头时，若该实体参与 G11 顶层身份生成，则 Sema 拒绝，诊断说明其侵入 Itanium mangled identity namespace，要求重命名声明。该规则同时适用于 C 和 C++；asm-label 不构成豁免。无 G11 身份的普通实体不因本规则被拒绝。

身份形状为：

- external formal linkage 实体：`E(D)`；
- 非局部 internal 实体，包括匿名命名空间实体：`T + "." + E(D)`；
- 函数局部静态：沿用 `T + "." + H(host) + "." + 声明名 [+ "." + 源序号]`。

函数局部静态的 `H(host)` 及源序号算法不变：需要 mangling 的宿主使用纯 Itanium 编码；C++ 未 mangle 宿主使用 `N+声明名`；普通 C 宿主使用裸声明名，并继续执行既定 C `_Z` 宿主输入边界拒绝。源序号由同宿主内同名静态局部声明的 AST 源顺序决定，从第二个起追加，统计不以是否带 G11 属性为条件。

**身份域隔离。** 本期可表示的单个名字分量不含 `.`。external、非局部 internal、函数局部静态分别具有 1、3、4 或 5 个点分字段，三个域互不相交。顶层域内部，mangled 分量以 `_Z` 开头，裸分量通过实际 Sema 输入边界排除该前缀；局部宿主域继续使用既定 `_Z` / `N` 首字符隔离和 C 输入边界。不能仅以"保留标识符"代替实际拒绝。

名字分量为空、含不受本期编码支持的分隔形态，或最终身份超出 NOTE 的 255 字节上限时，必须明确诊断，不得截断、散列缩短、追加发射去重号或回退到裸名。依赖模板声明的检查时点由模板支持裁定负责；未完成实例化的声明不得被送入要求具体实体的编码路径。

TU 限定用于分离不同翻译单元的 internal 实体；它不能代替同一 TU 内的命名空间编码。32 位 FNV-1a 沿用冻结合约，不承诺任意路径集合上的数学无碰撞；相同逻辑源路径及 stdin 身份限制不因本修订消失。不可区分的碰撞不得通过重命名 ELF 符号静默修补。

ELF 符号保持目标自然装配名及显式 asm-label，internal 实体保持 STB_LOCAL。placement report 的来源键仍为 `(输入文件, ELF 符号名, stable_symbol)`；跨对象语义合并仍以 `stable_symbol` 分组。

**匿名命名空间裁定**：同 TU 的 `A::{anonymous}::x` 与 `B::{anonymous}::x` 由完整 mangling 区分（不是由 TU 哈希区分）；同一匿名命名空间在同 TU 重开的同一实体得同一身份；不同 TU 中相同匿名命名空间拼写由 `T` 隔离；不把"匿名命名空间都有 `_GLOBAL__N_1`"误认为必然碰撞。

**不解决**：asm-label 不进入身份（不同声明名实体身份不同，即使 asm-label 相同；同一声明身份在不同 TU 指定不同 asm-label 则 placement 合并必须拒绝不一致关联）；不扩展 weak/COMDAT、多版本 ctor/dtor、隐式模板实例、LTO、命名模块；不重开 TU 路径哈希/stdin 唯一性/未发射宿主局部静态独立保活；不提供新旧 C++ stable 兼容层或双名回退。

### 8.2 命名字段（N4 修订；替换 §3.3 相应段）

`PlacementNames` 的键及 bind-only Synth 输出名均为记录显式关联的实际 ELF 符号名；值中的 `Stable` 仍为 stable_symbol。两者不要求字符串相等。

stable 分组内的记录必须指向同一链接符号身份：对于 external owned/bind 合并，ELF 名不一致即 `conflicting placement`，不得把它们当作别名。不同 stable 分组若试图占用同一 external ELF 名，也必须在填充符号映射时拒绝，禁止 map 插入覆盖或静默去重。internal 符号仍按输入文件作用域处理，不增加全局裸名唯一性要求。

resolveSymbols、重定位数值源、errorUndefined、目标存储类分类及 Synth 输出继续按实际 ELF 名查表。不得从 stable 推导、去前缀或 demangle 得到查表键。

manifest-only 且无任何 NOTE/输入符号可提供关联时，维持已有显式回退：manifest 的 stable token 同时作为其输出符号名；这不是 C++ 名字推导机制。manifest 与 NOTE 合取时使用 NOTE 的显式关联。

### 8.3 新增输入节 `.mcs251.placement.names`（N4 载体，S0 补裁）

保留 placement NOTE v1 字节布局与 `layout_hash` 算法不变；**新增专用关联 NOTE**（不静默扩展 v1 记录）：

- 输入节 `.mcs251.placement.names`，SHT_NOTE、flags=0、align=4；name `MCS251\0`、namesz=7、按 4 字节填充；**type=2**（placement-name association，不表示 placement schema version=2）。
- desc 全部多字节数值为 BE：`u32 association_version = 1`；`u32 entry_count`；重复 `entry_count` 次：`u32 placement_record_index`（按本文件 `.mcs251.placement` 内物理记录顺序从 0 编号）、`u32 elf_name_len`、`u8 elf_name[elf_name_len]`（实际 ELF 符号名字节，无终止 NUL）、补零至 4 字节边界。
- writer 对每份 placement 记录写恰一条关联；index 不得重复或越界；名字不得为空或含嵌入 NUL；reader 用有界长度检查，拒绝截断、非规范填充、缺条目或多条目。
- B 从该实体的最终 MC 符号取实际 ELF 名；该名字**只进入关联载体，不反向参与 stable 生成**。bind 的关联符号必须作为 undefined external 符号存在（即使无普通代码引用），此举不产生 storage。
- owned 关联必须与固定节唯一主符号的实际名字一致；bind 关联必须指向本输入文件相应的 undefined external 符号，不允许猜测或按顺序配对。
- 关联节**不进入输出镜像**；关联正确性由结构检查、输入 symtab 与显式身份一致性检查承担，不能声称 `layout_hash` 覆盖名字关联。
- **旧对象接受边界**：无关联节的旧 owned 记录可按既定专用节主符号规则恢复；**无关联节的旧 bind 对象不得猜测关联，必须拒绝并要求重新生成**；不支持新旧身份编码混合恢复为同一实体。

### 8.4 noinit 初始化政策（N7 选项甲；§2.2 增补）

`noinit` 仅可作用于本 TU 中具有 `place_at` 定义的对象，且完整 redeclaration chain 上不得存在显式初始化器。`bind_at` 声明不得携带 `noinit`；同 TU 的 place/bind 互斥规则不变。

noinit 是**定义侧存储的启动初始化政策**，不是引用侧类型或地址约束。跨 TU 的普通 bind 可以引用 noinit owned 定义，无须重复声明 noinit。bind NOTE 的 `flags.bit1` 恒为 0，语义为"不施加初始化政策"，**不是**"要求提供者初始化"。

owned 与 bind 合并时，noinit 以 owned 为权威；bind-only 不建立初始化政策。bind `flags.bit1=1` 为 malformed，B、C 和 verifier 均须防御拒绝，不能忽略后继续。

本裁定不改变 noinit 的冷启动值不确定性，不保证用户代码、DMA 或其他运行时主体不写该对象；承诺范围是本镜像受审计的启动初始化路径。fail-closed 不要求把"声明未携带政策"解释成一个无法表达的相反政策。

### 8.5 §3.2 合并表替换行（N7 选项甲）

| 字段 | 合并规则 | 不满足时 |
|---|---|---|
| flags.bit1 noinit | **owned 权威**：合法 bind 位恒 0（表示不施加政策，不参与相等比较）；存在 owned 取其值，仅 bind 取 0 | bind 带 bit1 → malformed |
| flags.bit0 retain | **owned 权威**：合法 bind 位恒 0；存在 owned 取其值，仅 bind 取 0 | bind 带 bit0 → malformed |

NOTE flags 注释与未知输入段同步为：`flags.bit0 retain` 与 `flags.bit1 noinit` 仅 owned 记录可为 1；其余位必须为 0。bind 记录 flags 必须为 0，非零按 malformed 拒绝。

### 8.6 §7 manifest 约束增补（N7 选项甲）

manifest `[bind]` 不得与 `[noinit]` 或 `[retain]` 组合；组合为 malformed。非 bind 的 `[noinit]` 是对 owned 初始化政策的显式约束，不能把一个实际带初始化记录的 owned 对象"链接时改成 noinit"。没有实际 owned 存储的 manifest-only 项不得借 `[noinit]` 宣称已验证外部初始化政策。

### 8.7 实施补记表述纠正（依 G11-C R2 评审 §五）

- A 层时点表述改为"**`Release()` 内**前述属性刷新之后、优化管线之前"（避免读成 Release 外发生）。
- B 层不得把 bind 统一描述为 `Placement` 类——`MS251AsmPrinter.cpp` 存在**独立 `Bind` 类**（bind 全局对象返回 Bind；函数路径与 ISR 情况另有分类），如实描述，不把不同分类压成单一 Placement。
- A 层 bind 是**直接加入 `LLVMCompilerUsed`**，不是经 `addUsedGlobal`；文案应避免与 retain 的 `llvm.used` 混同。

### 8.8 实施状态与排程（§4 S0 同步）

- **设计裁定状态**：N4（甲′+关联载体）、N7（甲）已裁定并落于本修订；D-S4c 类型号前提不适用于 G11。
- **实现状态**：N7=甲 已在 G11-C 落地（单一决策点 `placementMergedFlag`、bind flags≠0 一律 malformed，reader/manifest 两处）；N4 的 A 身份编码与 B 关联载体 writer 属**第二批 A/B 修复切片**（未实施）；C 的关联 NOTE 消费增量在 B 载体落地后补；D 独立解析关联 NOTE。
- 在 A/B 增量、C 单一决策点及 D 独立复核测试闭合前，一律记"**设计裁定完成，实施/验收未完成**"。
