# AS4 聚合常量嵌套 struct 初始化器 — 设计稿（r3，按 Alice 复审修订）

状态：设计稿 r3（2026-09-14）。r1 三阻断（字节级算法规范化 / 真表 golden
验收 / code-placement.ll 精确修订断言 + 身份无交集证明）已落实（§6A、§8、
§10、§11；原"PM 决策点"按 PM 裁定改为定稿 §9）；r2 复审两阻断（零图判定
先于类型资格与 R1 冲突 / 判定次序一致性 + 零图边界探针入测试矩阵）已落实
（§6A.2/§6A.3/§6A.4 重写、§7/§8.4/§13 增补）。
工作树 `/home/liu/LLVM_STC32/MCS251`（minimal-isr @ 2e08e94ae）。本文为未跟踪
草案，不入 git；进度台账见 `/home/liu/LLVM_STC32/GAP-AS4AGG-PROGRESS.md`。
未改任何产品源码；所有断言带复现命令（r2/r3 探针在 `/tmp/as4agg-rev-probe/`
——r3 零图边界探针 `z1/z2/z3/z5-*.ll` 同目录，r1 探针在
`/tmp/as4agg-probe/`，均仓库外；语料目录只读，探针用拷贝）。

---

## 0. 一句话结论

- **失败层**：llc 的 MCS251 AsmPrinter（不是 clang）——`emitAddressSpacedGlobal`
  的只读初始化器门 `isSupportedROInitializer` 在遇到 `StructType` 时
  `dyn_cast<ArrayType>` 失败返回 false，走 `report_fatal_error` 故意 fail-closed
  （`llvm/lib/Target/MCS251/MCS251AsmPrinter.cpp:2104-2106`，门函数
  `:885-907`，struct 死点 `:897-900`）。
- **支持边界**：AS4 只读初始化器接受"任意深度、每层非空"的**嵌套数组**
  （叶子 i8/i16/i32 / &global 指针叶 / AS4 零图）；**第一层 struct 即拒**。
- **demo 子集**：仅 41/42/43 的 font.h 用 struct 包裹形态（5 张表、两层嵌套
  array→struct→array、70 个 gb18030 字符串叶、**共 5772 B（本轮逐表实发
  核实，§8.2）**）；61/62 同名表是纯二维数组（今日已支持），不需要本切片。
- **方案（定稿）**：Option A——把 `isSupportedMutableInitializer`/
  `emitMutableInitializer` 已有的 struct 递归（StructLayout 驱动）镜像到 RO
  侧，struct 子句只对 AS4 调用点开启；不引入新记录、新重定位、新上限。
  RO 门采用 mutable 门同款"先递归类型资格、后值形态（含零图）"两半结构
  （r3 修订，§6A.2）。实施者无需再决策的逐字节算法规范见 §6A。
- **作用域裁定（定稿，PM 已拍板）**：AS4-only（AS0 冻结语义不动，§9.1）；
  顶层非数组 struct、嵌套 struct、struct 内指针叶**全部入范围**（§9.2/§9.3）。
- **验收（定稿）**：以 demo 41/42/43 真实 font.h 的 5 张表为**主验收**——
  同一张表经"现支持通道"出参照字节，AS4 通道落地后逐字节对表（oracle 选型
  与等价性论证见 §8.1/§8.3）；F1-F5 缩比探针降级为单测（§8.4）。

## 1. 缺口登记与证据链核对

- 登记：A4-FINAL-R2（`/home/liu/LLVM_STC32/A4-FINAL-REVIEW-Alice.md`）——
  早期文档把 gui.c 字体表失败误写为"code 指针初值≈CP-A"；Alice 裁定真实缺口
  是"AS4 非零聚合常量嵌套 struct 形态，无 ptr 叶子"，登记为独立非 A4 回归。
- 冻结证据：`/home/liu/LLVM_STC32/a4-final-review-evidence/alice-a4-final/
  font-initializer-only.ll` + `font.{a4,baseline}.log`。
- 时效修正（r1 实测）：该冻结 IR 是手写模块、无 `!mcs251.signatures`，在
  当前 v2-default 工具链上**先死于身份门**，到不了 AsmPrinter 门：
  ```
  /home/liu/build-mcs251/bin/llc -mtriple=mcs251 \
    -mcs251-memory-contract=1,2,32,8,1 -mcs251-object-format=elf -filetype=obj \
    .../font-initializer-only.ll -o /tmp/font-repro.o
  → LLVM ERROR: a v2 object requires `!mcs251.signatures` metadata ...
  ```
  当前诚实的复现须走 clang 产物（自带 signatures 元数据）或手写 IR 补
  signatures（`llvm/test/CodeGen/MCS251/code-placement.ll` 的 split-file 各片
  就是这个写法）。r1/r2 全部用 clang 探针复现（§3、§8）；手写 IR 与身份门
  的关系详见 §11（结论：补上 signatures 后 struct 形态**不**被身份扫描拦截）。

## 2. 逐层归属：三层都实测过，失败只在 llc

探针：`/tmp/as4agg-probe/p*.c`（11 个形态，r1），T0 同款 flags：
```
/home/liu/build-mcs251-s1/bin/clang --target=mcs251-unknown-none -std=c11 \
  -O0 -fmcs251-keil -Xclang -mcs251-memory-contract=1,1,32,8,1 \
  -S -emit-llvm p3-structarr.c -o p3-structarr.ll     # rc=0
/home/liu/build-mcs251-s1/bin/clang -cc1 -triple mcs251-unknown-none -std=c11 \
  -fmcs251-keil -mcs251-memory-contract=1,1,32,8,1 -emit-llvm -disable-llvm-passes \
  -o - p3-structarr.c                                  # 同样 rc=0（-cc1 口径复核）
/home/liu/build-mcs251/bin/llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 \
  -mcs251-object-format=elf -filetype=obj p3-structarr.ll -o p3-structarr.o
→ LLVM ERROR: MCS251: __code global 't3': unsupported initializer (i8/i16/i32
  scalars, nonempty arrays of integers and &global pointer leaves, or the ROM zero image)
```

- Sema：无拒绝（所有形态 clang rc=0）。
- clang CodeGen 常量发射：无拒绝（`.ll` 里完整 AS4 聚合常量在，demo 41
  `gui.ll` 的 `@asc2_1206` 即实物）。
- llc ISel/Verifier：无拒绝（探针的读表函数正常选出；demo 41 gui.ll 现在死在
  更早的 static-ptr ABI 门，属另一登记缺口——r2 复核：整 gui.ll 报
  "static pointer parameters are not supported by the compatibility ABI"）。
- llc AsmPrinter：**唯一拒绝点**，且是**故意 fail-closed**（门函数 doc 注释
  明说 "Struct aggregates ... are rejected here"，`MCS251AsmPrinter.cpp:877-884`），
  不是路径未实现——但 struct 的发射路径在 mutable 侧已存在（§6），复用面很小。

## 3. 探针矩阵（r1 实测，2026-09-14 当前二进制）

| # | 形态 | C 形状 | clang | llc |
|---|------|--------|-------|-----|
| p1 | 对照：一维 i8 数组 | `const u8 code t1[]={...}` | OK | **PASS** |
| p2 | 嵌套二维数组 | `const u8 code t2[3][4]` | OK | **PASS** |
| p3 | demo 1206/1608 形态：数组×struct×数组 | `const struct{u8 dat[4];} code t3[]` | OK | **FAIL**（unsupported initializer） |
| p4 | struct 套 struct | `struct{struct IN in; u8 b;} code t4[]` | OK | **FAIL** |
| p5 | 字符串叶 | `struct{u8 dat[4]; char txt[2];}` + `"ab"`/`"\xC9\xEE"` | OK | **FAIL** |
| p6 | 顶层非数组 struct | `const struct P code t6={1,2}` | OK | **FAIL** |
| p7 | AS3 const struct 对照 | `const struct FONT __xdata t7` | OK | **PASS**（mutable 通道早已支持 struct） |
| p8 | 混合：零图成员+零图整项+字符串叶 | `{{0,...,\"\0\0\"},{...,\"ab\"},{0,\"zz\"}}` | OK | **FAIL** |
| p9 | 对照：flat 数组 + &global 指针叶 | `const u8 code* const tbl[]={inner,0}` | OK | **PASS** |
| p10 | struct 内指针叶 | `struct CB{void(*f)(void); u8 tag;} code cb[]={{helper,1}}` | OK | **FAIL** |
| p11 | gui.ll 异构 packed-struct 形态（见 §5） | 96 项表的 4 项缩比 | OK | **FAIL** |

边界一句话：**嵌套数组任意深全过（p1/p2/p9），第一层 struct 即死（p3-p6/p8/
p10/p11）；AS3 const struct 已支持（p7）——缺口精确等于"AS4 只读通道的
struct 子句缺失"。** r2 补充：整张真表（96 项全量、packed 异构形态）同样
第一层 struct 即死，见 §8.2 步骤 R0。

opt 维度：p3/p5/p8 在 -O0/-O1/-O2 下 IR 全局形态一致（元素统一的
`[N x %struct.S]`，仅加 `local_unnamed_addr`），无 opt 档新形态。

## 4. demo 需求子集（只覆盖这些即可解锁 41/42/43 字库表）

41/42/43 三份 `font/font.h` 逐字节相同（diff 已核，r2 复核仍相同）。全部表为
**两层嵌套 array→struct→array(+char[] 字符串叶)，叶子 i8**，无更深层、
无指针叶、无 i16/i32 叶。逐表字节量 **r2 已用实发对象逐表核实**（§8.2）：

| 表 | 形态 | 项数 | 元素 | 小计（实测=算术） |
|----|------|-----|------|------|
| asc2_1206 | `struct FONT_1206{u8 dat[12];}` | 96 | 12 B | 1152 B |
| asc2_1608 | `struct FONT_1608{u8 dat[16];}` | 96 | 16 B | 1536 B |
| tfont16 | `typFNT_GB16{u8 dat[32]; char txt[2];}` | 58 | 34 B | 1972 B |
| tfont24 | `typFNT_GB24{u8 dat[72]; char txt[2];}` | 8 | 74 B | 592 B |
| tfont32 | `typFNT_GB32{u8 dat[128]; char txt[2];}` | 4 | 130 B | 520 B |
| **合计** | | | | **5772 B/CODE·每 demo** |

实测补充画像（gui.ll 实物 + 参照对象核出，§8.2）：

- 70 个 gb18030 字符串叶（`[2 x i8] c"…"`，tfont16/24/32 = 58/8/4）——clang
  把无效 UTF-8 输入按字节收：源文件为 UTF-8、`char txt[2]` 对 3 字节汉字产生
  `-Wexcess-initializers` **警告**（非错误，drive.py T0 口径通过）并截取前
  2 字节（gui.ll 实物 `c"\E6\B7"` 即"深"的前两字节）。
- 零图形态实物在库：asc2_1206 含 **3 个整项 `%struct.FONT_1206
  zeroinitializer` + 5 个全零 `[12 x i8] zeroinitializer` 成员**；
  asc2_1608 含 2 个整项 + 5 个全零成员；tfont16/24/32 无整项零图。
- 61/62（及任何"纯二维数组"表）**不在**本缺口内：`unsigned char code
  asc2_1206[95][12]` 形态今日已 PASS（p2 对照 + 61 font.h:47 实文）。
- **残余 gap 记账（G1-4 台账一致）**：解锁字库表≠解锁 demo——
  41：T0 pass，T1 gap = LCD.ll static-ptr ABI（A4-STATIC-PTR-PARAMS-DESIGN）；
  43：同 41；42：T0 gap = 非 8 对齐 SFR bit 门（P09 bit 位地址系列）。字库表
  是 41/43 gui.ll 在 static-ptr 落地后**下一个**且目前已知**最后一个** gui 侧
  数据面缺口；三 demo 的完整解锁依赖链 = static-ptr（41/43）+ bit-addr（42）
  + 本切片。demo 61/62 的 static-ptr 同理与本切片无关。

## 5. 后端必须吃下的 IR 形态清单（clang 实发，非假想；r2 逐表核实）

r2 用整张真表复核（§8.2）：**5 张表里 3 张被 `ConstantAggregateBuilder`
（`clang/lib/CodeGen/CGExprConstant.cpp:383-780`，splitAt :582 / split :610 /
packed 选择 ~:756-772）改写成异构 packed 形态，2 张保持统一数组**：

| 表 | 顶层 IR 形态（gui.ll 与参照探针逐字一致） |
|----|------|
| asc2_1206 | packed 异构 `<{ %struct.FONT_1206, %struct.FONT_1206, { <{ i8,i8,i8,i8,[8 x i8] }> }, %struct.FONT_1206, … }>` |
| asc2_1608 | packed 异构 `<{ %struct.FONT_1608, %struct.FONT_1608, { <{ i8,i8,i8,i8,i8,[11 x i8] }> }, … }>` |
| tfont16 | 统一 `[58 x %struct.typFNT_GB16]` |
| tfont24 | 统一 `[8 x %struct.typFNT_GB24]` |
| tfont32 | packed 异构 `<{ %struct.typFNT_GB32, { <{ [119 x i8],[9 x i8] }>, [2 x i8] }, { <{ [116 x i8],[12 x i8] }>, [2 x i8] }, { <{ [120 x i8],[8 x i8] }>, [2 x i8] } }>`，split 成员内还有 `[N x i8] zeroinitializer` 尾部 |

要点：外层可以是 packed `<{...}>`（不再是 ArrayType！）、成员类型逐项异构、
出现"非 packed 单成员 struct 包 packed struct"的包裹层、零图可出现在任意
子树（整项/成员/成员的尾部数组三级都有实物）。=> 门与发射器都必须按**类型
递归**而非"数组叶"假设来写，且要吃 `getStructLayout` 偏移（含 packed）。
r2 等价性事实：**同一张表的 AS0/AS3 参照对象由现役 mutable 通道（含完整
struct walk）发出，字节与 gui.ll 常量树一致**（§8.2 步骤 R2/R3），即 §6A
算法已被这批实物形态全量喂过。手写 IR 负例/正例测试须带
`!mcs251.signatures`（v2 门，§1、§11）。

## 6. 方案

### Option A（定稿）：RO 侧镜像 mutable 侧的 struct 递归

同文件两处改动，无新记录/重定位/上限：

1. **门** `isSupportedROInitializer`（`MCS251AsmPrinter.cpp:885-907`）：加
   StructType 子句，逐字镜像 `isSupportedMutableType` 的 struct 段
   （`:697-703`：非 opaque、非空、全成员递归）。作用域加一个
   `bool AllowStructs` 参数（形如现有 `AllowZeroImage`），**只从
   `emitAddressSpacedGlobal` 的 AS4 调用点（:2104）传 true**；AS0 RO 调用点
   （:1993）传 false 保持 AS0 冻结语义与既有拒绝文案不变。同时把现行
   `:890-891` 的零图早退改为 `isSupportedMutableInitializer`（:708-711）
   同款**先类型资格后零图**两半结构（r3 修订，次序即规范，§6A.2）——
   零图不再免检类型。
2. **发射** `emitROInitializer`（`:917-945`）：加 StructType 分支，逐字镜像
   `emitMutableInitializer` 的 struct walk（`:770-779`：
   `DL.getStructLayout` + 成员间 `emitInitializerZeros` 填充）。零图分支
   （`:923-928`）与指针叶分支（`:929-936`）已有，无需改；struct 内指针叶
   自然落进既有 `emitPointerInitializer` 4 字节大端容器 +
   `R_MCS251_24`（`hasSymbolPointerLeaf` :845-864 本就递归 struct）。

逐字节算法的**规范文本**（实施者无需再决策）见 §6A。

不推荐 Option B（clang 侧拍平成 flat 字节数组/统一 flat const pool）：
偏离标准 lowering、引入非标模式；AS3 const struct 已不拍平（p7），不对称会
加深；struct 内指针叶本来就需重定位，拍平无收益；且缺口在 llc 侧，clang 侧
本就无拒绝。

### 交互面逐项（结论：零新增机制）

- **CSEG 记录**：AS4 图像原位入 CSEG（`.text` PROGBITS），无 XINIT/XDATA_INIT
  新记录；asm 文本模式不可达（AS4 强制 ELF，`emitAddressSpacedGlobal`
  :2032-2034）。
- **重定位**：仅既有 `R_MCS251_24` 字节通道（含 struct 内偏移上的 4 字节
  容器）；链接侧（X3-R8 门）按目标节判定，数据指针通道语义不变。
- **size 上限**：AS4 无 16-bit 记录上限（对比 AS3 :2061-2064）——本切片不
  新增上限，与 flat 数组完全同权；STT_SIZE 为 MC 常量。实物最大单表 1972 B。
- **zero image**：`AllowZeroImage=true` 已随递归下传（:890-891、:903），struct
  子句沿用同参——空格字形整项零图（实物在，§4）保持合法；AS0 侧仍 false。
  r3 修订：零图**不再免检类型**——零图整项/成员与是非零形态走同一条递归
  类型资格检查（§6A.2），空/嵌套空/opaque struct 的零图在任意深度先按 R1
  拒，只有类型全合格才走零图发射路径（§6A.4 现状探针证明该拒绝必须由本
  切片自带，不能依赖身份门）。
- **对齐**：MCS251 datalayout 全部标量 align 1（`E-m:s-p:32:8-i8:8-…-n8`），
  struct ABI align=1，p3-p11 与 5 张真表全部 `align 1`；顶层非数组的 align-1
  要求（:2099-2103）对 struct 天然满足，文案按 §6A.4 改词。
- **v2 身份/.mcs251.attributes/签名**：纯数据，无新指针类型/寄存器；函数
  签名集合不变；**身份扫描对全局初始化器形态不敏感的 file:line 证明与探针
  见 §11**（要点：身份字节只由契约常数 + `!mcs251.signatures` 渲染；
  scanObjectIdentity 的初始化器走查是"只判定不发射"的能力检查，struct 形态
  今日已放行）。唯一注意点：**手写 ll 测试模块须带 `!mcs251.signatures`**，
  否则死在 v2 身份门（§1 实测）。
- **诊断文案**：AS4 拒绝串与 ":2099" 对齐文案的**定稿文本**见 §6A.4；S1
  顺手改，AS0 侧文案一字不动。

## 6A. 字节级算法规范化（阻断 1，定稿规范）

本节是实施规范：把"镜像 mutable 侧"落到**逐字节发射/比较算法 + 拒绝边界 +
文案全文**。除下列文本外不再有自由度。

### 6A.1 记号

- `storeSize(T)`、`allocSize(T)` = `DataLayout::getTypeStoreSize/getTypeAllocSize`。
  本契约 datalayout `E-m:s-p:32:8-i8:8-i16:8-i32:8-…-n8:16:32-S8` 下所有标量
  ABI align=1，聚合内无洞（实证：§8.2 五张表两通道参照流逐字节相等且尺寸
  精确等于 Σ成员）；但算法**不假设**无洞，洞与尾补一律显式补零。
- `E(C)` = 发射函数（`emitROInitializer` 落地后形态），产出字节流 + 重定位
  子序列；`S(C)` = 支持判定（`isSupportedROInitializer` 落地后形态）。
- "大端"由 streamer 的 `emitIntValue` + 大端 datalayout 给出，与 AS0/AS3
  现行多字节发射同一条路（如 code-placement.ll DEFS 的 `i16 55 → 00 37`）。

### 6A.2 支持判定 S(C, DL, AllowZeroImage, AllowStructs) —— 规范

判定是**两半结构**，与 mutable 门 `isSupportedMutableInitializer`
（:706-731）同形：入口先跑递归**类型资格** T（`isSupportedMutableType`
:682-704 的镜像），通过后按**值形态**分派（含零图优化）。**次序本身是
规范**（r3 修订，Alice 阻断 1）：先类型资格、后零图——AS4 零图与是非零
形态走**同一条**递归类型检查，opaque/空 struct（任意深度）在类型半即拒
（R1），只有全树类型合格后才允许零图整项/成员进入零图发射路径
（§6A.3.2）。零图不豁免类型资格。

**类型资格 T(Ty)**（镜像 `isSupportedMutableType` :682-704，struct 段受
`AllowStructs` 门控，不加、不减）：

- T0. `Ty` ∈ {i8, i16, i32}：true。
- T1. `Ty` 是 `PointerType` 且 `DL.getTypeStoreSize(Ty) == 4`（32 位指针
  契约的 4 字节容器）：true。
- T2. `Ty` 是 `ArrayType`：元素数 ≥ 1 且 T(元素类型)。
- T3. `Ty` 是 `StructType` 且 `AllowStructs`：`!isOpaque() && 元素数 ≥ 1`
  且全成员 T；packed 与否**不影响判定**（packed 只是布局属性，类型树
  同构）。`AllowStructs=false` 时 struct 一律 false。
- T4. 其余（i1/i64/f32/f64、向量、opaque/空 struct、……）：false。

**值形态半 S(C)**（入口断言/前置：`T(C->getType())` 已通过，否则 false；
此后按序分派）：

1. `C` 是 `ConstantInt`：true（类型半已把它限定在 i8/i16/i32）。
2. `C` 是 `ConstantAggregateZero`：返回 `AllowZeroImage`（AS4 传 true，
   AS0 传 false；零图按 6A.3.2 展开成全零字节）。**可达前提：该子树类型
   已过 T**——空 struct、嵌套空 struct、opaque struct 的零图（整项或任意
   深度成员）在类型半已被拒，到不了本条；合格类型的零图整项/成员由此
   统一走零图发射路径。
3. `C` 类型是 `PointerType`：`isSupportedPointerLeaf`（:305-373）——恰为
   `&global[+const]`（GEP/`add` 折叠）或 null。表达式代数（inttoptr/
   ptrtoint/addrspacecast/非常量索引）不支持 → false。
4. `C` 类型是 `ArrayType`：**现行冻结子句逐字保留**（:897-906）——元素数
   ≥1（类型半 T2 已查）、`C` 是 `ConstantDataArray`/`ConstantArray`、
   逐元素递归 `S`（每层重新先类型后值）。
5. `C` 类型是 `StructType`，**仅当 `AllowStructs`**（镜像 mutable 的
   `isSupportedMutableInitializer` :722-729 同一循环，不加、不减）：
   逐成员 `C->getAggregateElement(i)`（null → false）递归 `S`。成员上
   的 `UndefValue`/非叶 `ConstantExpr` 因其叶子最终落在第 6 条 false 而
   传递性拒绝。
6. 其余（`UndefValue` 标量/叶、`ConstantExpr` 非叶形态、向量、嵌套在
   聚合里的 `ConstantExpr` 数组等）→ false。

`AllowStructs=false` 时 T3 对 struct 恒 false，第 5 条不可达 → AS0 行为
与今日逐字节相同（含 :1993-1994 的冻结拒绝文案）。

**与 r2 稿的差异**：r2 把零图放在值分派第 2 条且先于一切类型检查，
`{} zeroinitializer` 会经该条直接返回 true（现行 `:890-891` 的同款缺陷），
与 R1"空 struct（任意深度）拒绝"矛盾；且该形态今日实测 llc rc=0、身份门
不兜底（§6A.4 探针）——r3 起判定改为上述两半结构，S1 落地即
`isSupportedMutableInitializer` :708-711 次序在 RO 门的镜像。

### 6A.3 发射算法 E(C, DL) —— 规范

结构与 `emitMutableInitializer`（:740-780）/现行 `emitROInitializer`
（:917-945）一一对应；每个分支给出字节语义。次序与 §6A.2 对齐的不变式：
E 按值形态分派，但**每个到达 E 的子树都已通过 §6A.2 的先类型后值判定**
（门 S 是唯一拒绝层，E 内不再放行/不再二次判定）——零图分支因此只可能
收到类型合格的零图子树，空/嵌套空/opaque struct 的零图在门上已被拒，
发射器不为其产出任何字节。

1. **标量叶**（`ConstantInt`）：`emitIntValue(getZExtValue(), storeSize(T))`
   —— i8 1 字节、i16/i32 大端 2/4 字节。
2. **零图**（`ConstantAggregateZero`，整项或聚合内任意子树位置）：
   `emitInitializerZeros(storeSize(T))` —— 逐字节 `emitIntValue(0,1)`
   （:733-738；`emitZeros` 不可用：ASxxxx 文本边界要物化零字节）。
   可达前提 = §6A.2 值半第 2 条（该子树类型已过 T）；"零图整项"= 全局
   初始化器本身是零图；"零图成员"= 聚合元素是零图——两者都由本分支
   统一展开，无第三种零形态；非法类型的零图没有对应字节语义（不可达）。
3. **字符串叶**：就是 4 号数组分支下的 `ConstantDataArray` of i8（clang
   对 `char txt[2]` 发 `[2 x i8] c"…"`，无终结符补齐，截断在 clang 前端
   完成，§4）——逐元素走 1 号标量分支，**后端不加 `\00`**。
4. **数组**（`ArrayType`）：对每个元素 i：`E(E_i)`，然后
   `emitInitializerZeros(elemAllocSize − elemStoreSize)`（现行 :938-944；
   本 datalayout 恒为 0）。
5. **struct**（`StructType`，packed/非 packed **同一段代码**，镜像 :770-779；
   前置不变式：T3 已保证非 opaque、非空，`getStructLayout` 合法）：
   ```
   L = DL.getStructLayout(ST);  Pos = 0
   for i in 0..M-1:
     Off = L->getElementOffset(i)
     emitInitializerZeros(Off - Pos)        // 成员间洞（本契约恒 0，仍规范必写）
     E(C->getAggregateElement(i))
     Pos = Off + storeSize(ST->getElementType(i))
   emitInitializerZeros(storeSize(ST) - Pos) // 尾补（本契约恒 0，仍规范必写）
   ```
6. **指针叶**（`PointerType`）：现行分支不动（:929-937）→
   `emitPointerInitializer`（:821-843）：null 叶 = 4 字节零图、无重定位；
   符号叶 = 容器偏移 0 处字面 `0x00` + 偏移 1..3 的 3 字节占位 +
   `.reloc R_MCS251_24`（符号+addend）。struct 内偏移上的指针叶由 5 号分支
   定位容器起点，容器内部语义不变。

**比较（golden）算法**：两对象逐字节相等 = (a) 目标节内
`[st_value, st_value+st_size)` 的原始字节流相等（AS4 侧与参照侧都按符号
STT_OBJECT 边界切片）；且 (b) 切片内的重定位子序列按 (offset, type, symbol,
addend) 相等（本切片五张真表无指针叶 → 双侧重定位皆空，断言"无重定位"
本身是 golden 的一部分）。参照流由 6A 規范的同一族 walk 在 mutable 通道
产生（等价性论证见 §8.1/§8.3）。

### 6A.4 拒绝边界（S1 后**仍然** fail-closed 的全集 + 定稿文案）

| # | 形态 | 拒绝点（现行行号） | S1 后文案（全文，定稿） |
|---|------|------|------|
| R1 | opaque/空 struct（任意深度，**含空 struct 的零图整项/零图成员——零图不豁免 §6A.2 类型半**）、成员类型越界（i1/i64/f32/f64/向量…）、undef 成员、聚合内非叶 ConstantExpr | 门返回 false → `:2104-2106` | `MCS251: __code global '<name>': unsupported initializer (i8/i16/i32 scalars, nonempty arrays of integers, nonempty non-opaque structs of those at any nesting, &global pointer leaves, or the ROM zero image)` |
| R2 | 顶层非数组（含 struct）存储 align 或 ABI-align > 1 | `:2099-2103` | `MCS251: __code global '<name>': non-array storage must be byte-aligned (arrays of any declared alignment are emitted byte-aligned)`（原 "scalar storage" 改词；数组豁免不变） |
| R3 | 非 ELF 输出（REL/asm）下的任何 AS4 定义 | `:2032-2034` | 不变：`storage requires ELF object output (-filetype=obj -mcs251-object-format=elf)` |
| R4 | AS0 const struct（AllowStructs=false） | `:1993-1994` | 一字不变（:1910-1917 冻结串，含 "aggregates … are not supported"） |
| R5 | struct 内指针叶 + 非 ELF | R3 前置 | 同 R3（指针容器只在 ELF 协议存在） |
| R6 | 16 位契约的 AS0 const | `:1924-1929` | 不变（与本切片正交） |

**零图 × 非法类型的现状（r3 实测，2026-09-14，Alice 探针复核于
`/tmp/as4agg-rev-probe/z*-*.ll`）——证明本切片必须自带该拒绝，不能依赖
身份门**：

- **空 struct 零图整项**：`addrspace(4) global {} zeroinitializer` +
  `-mcs251-memory-contract=1,1,32,8,1`（T0/demo 契约，v1 布局）→ 现役
  llc **rc=0**，产出 `_z1` = **Size 0** 的 STT_OBJECT。身份门不兜底：
  v1 契约下 `isV1ObjectCompatible`（:497-499）=
  `scanObjectIdentity(M, false)`，而该走查对 v1 契约在 :546-548 直接短路
  返回 true，**不查任何全局**；漏网发生在 RO 门 `:890-891`——零图分支
  先于一切类型检查返回 `AllowZeroImage=true`。
- **嵌套空 struct 零图**：`addrspace(4) global { {}, i8 } zeroinitializer`
  同契约 → 同样 **rc=0**（`_z2` Size 1，仅 i8 那一字节）。
- **v2-default 契约**下同两例虽被拒，但拒在**身份门**（:1428-1434 的能力
  文案，非初始化器文案）——层与文案都不对；demo 契约是 v1 时更是完全
  放行。两种契约下"在 RO 门按 R1 拒"都必须由本切片实现。
- **合格类型全零图（对照，正例）**：`{ i8, [2 x i8] } zeroinitializer`、
  `[2 x [3 x i8]] zeroinitializer` 两契约下今日 rc=0，S1 类型前置后
  **必须保持 rc=0**（不得误杀既有零图通道）。
- **非零图根含空 struct 成员**：`{ %E, i8 } { %E zeroinitializer, i8 5 }`
  （`%E = type {}`）今日已死于 RO 门（现行 :2105-2106 文案，栈帧
  `MCS251AsmPrinter::emitGlobalVariable`）——非零图形态的拒绝层本就在
  门上，仅零图根形态经 :890-891 漏过；r3 次序修正后与 R1 同层同文案。

**空 struct 零图场景的最终诊断（S1 后，R1 文案逐字，`<name>` 为全局
名）**：
```
LLVM ERROR: MCS251: __code global 'z': unsupported initializer (i8/i16/i32 scalars, nonempty arrays of integers, nonempty non-opaque structs of those at any nesting, &global pointer leaves, or the ROM zero image)
```

门函数 doc 注释（:877-884，"Struct aggregates ... are rejected here"）随
S1 改写为：struct 聚合在 AllowStructs（AS4 调用点）下按 §6A.2 接受
（先类型资格后零图，零图同受递归类型检查），AS0 冻结语义不变。

### 6A.5 落地锚点（全部 file:line 已核）

- 门：`isSupportedROInitializer` `:885-907`（struct 死点 `:897-900`）→ 加参
  `AllowStructs` + §6A.2 两半结构：类型半 T（`isSupportedMutableType`
  镜像）先行，现行 `:890-891` 零图早退移到类型半之后（= 值半第 2 条）。
- 发射：`emitROInitializer` `:917-945`（标量 `:918-922`、零图 `:923-928`、
  指针 `:929-937`、数组 `:938-944`）→ 加 §6A.3.5 struct 分支（§6A.3.6
  指针分支与 §6A.2 引用的现行子句保持冻结）。
- 调用点：AS0 `:1993-1994`（`AllowStructs=false`）；AS4 `:2104-2106`
  （`AllowStructs=true`）。
- mutable 镜像源：`isSupportedMutableType` `:682-704`（struct 段 `:697-703`）、
  `isSupportedMutableInitializer` `:706-731`、`emitMutableInitializer`
  `:740-780`（struct walk `:770-779`）。
- 文案：`:2105-2106`（R1 新文）、`:2102-2103`（R2 新文）。

## 7. 切片

- **S1（串行，核心，单文件）**：§6A 门+发射两处 + §6A.4 文案；单 reviewer
  可闭环。
- **S2（可与 S1 并行开发，落地依赖 S1）**：llvm/CodeGen/MCS251 新测试
  `code-struct-init.ll`（split-file 多片、golden 字节经 `llvm-readobj
  --section-data`，模式照 `code-placement.ll`）：
  - **主验收件**：真表 golden——5 张真表的 AS4 对象字节 vs 参照字节逐表
    对表（规范、命令、断言见 §8；真表 golden 是主验收，不在 llc 单测里
    重建表内容，测试引用探针产物 hex）。
  - **单测（降级自 r1 的 F1-F5"验收"定位）**：缩比形态回归——
    F1 `[N x {u8[12]}]`（1206 缩比）；F2 gui 异构 packed 形态（p11 手写版，
    golden 含零图子树）；F3 `{u8[32],i8[2]}`+gb18030 字符串叶+零图整项
    （p8）；F5 struct 内 `R_MCS251_24`（golden 含 4 字节容器 + reloc 偏移）。
  - **负例单测**：opaque/空 struct、struct+align>1（R2 新文案）、undef 成员、
    AS0 const struct（确认仍拒且文案一字不动，R4）。
  - **零图边界单测（r3 增补，Alice 探针场景入矩阵）**：
    负例——空 struct 零图整项 `{} zeroinitializer`、嵌套空 struct 零图
    `{ {}, i8 } zeroinitializer`（任意深度同拒），断言 §6A.4 R1 文案逐字
    （两形态在契约 1,1,32,8,1 下今日 rc=0，S1 后必须拒——防回归到
    :890-891 零图早退）；正例护栏——合格类型全零图（`{ i8, [2 x i8] }`
    zeroinitializer 整项、struct/数组内零图成员）保持 rc=0，锁死"类型
    前置不误杀既有零图通道"。
- **S3（S1 后）**：clang 端 E2E 回归（`clang/test/CodeGen/mcs251-` 新文件，
  -cc1 -fmcs251-keil emit-llvm 管到 llc/readobj，照 as4-superset-ir.c 口径）
  + 把 §3 探针矩阵固化为 case。
- **S4（文档）**：XDATA-CODE-DESIGN-SUPPLEMENT.md 增补 AS4 RO struct 条目；
  本设计稿定稿；台账更新（GAP-AS4AGG-PROGRESS.md）。
- 并行边界：S1 是唯一产品改动；S2/S3 互并行；S4 随时。
- **意识修订项（唯一，精确配方见 §10）**：`llvm/test/CodeGen/MCS251/
  code-placement.ll` 的 STRUCT 前缀段（:6 负 RUN、:59 CHECK）当前钉死
  AS4 struct 拒绝，S1 落地时按 §10 的逐行断言改正例；SCALIGN 文案行
  （:60）同批换新词。这是唯一与冻结语义冲突的存量测试。

## 8. 验收（阻断 2 落实）：真实 clang 产物逐表验证 = 主验收

### 8.1 oracle 选型（定稿）

**选字节记录 oracle**：同一张表以"现支持通道"（mutable 通道：AS0 非限定
全局或 AS3 `const __xdata`，两者今日都把完整 struct 图像经
`emitMutableInitializer` 发进记录 payload）产出**参照字节流**；S1 后把同
一张表以 `code`（AS4）发出，取 `.text` 里该符号 STT_OBJECT 边界内的记录
字节，与参照流**逐字节对表**（比较算法 = §6A.3 末段）。

> r1 口径修正：评审建议中的"AS0 const 全局"本身今日即 fail-closed
> （`emitGlobalVariable` :1993-1994，AS0 const struct 走只读门被拒，
> 16 位契约下更早在 :1924-1929 拒）——**AS0 const 不是现支持路径**；
> 现支持的 struct 载体是 mutable 通道（AS0 非 const → XINIT payload；
> AS3 const `__xdata` → `.mcs251.xdata_init` payload；p7 实测 PASS）。
> r2 已实测两通道 payload 逐表相等（§8.2 步骤 R2），故"mutable 通道参照"
> 覆盖且强于原建议。

**等价性论证**（为何参照流 == AS4 落地后的正确字节）：

1. **同一 DataLayout、同一规范**：两条通道的发射器是 §6A.3 同一算法的两个
   实例——AS4 侧 struct 分支逐字镜像 `emitMutableInitializer:770-779`；
   标量/数组/零图/指针分支两侧现有代码本就同构（`:740-758` vs
   `:917-937`）。字节语义差异仅可能在"字段如何封装"（XINIT 记录头 vs
   `.text` 原位），对表时两侧都剥到 payload/记录字节层，封装头不参与。
2. **通道无关性实证**：AS0 通道与 AS3 通道（两条独立调用路径）对 5 张真表
   的 payload **逐表相等**（R2 步骤，5/5 True）——同一算法不同调用点的
   输出稳定性已验证；AS4 是第三条调用路径，规范同源。
3. **内容无折叠差异**：clang 对相同初始化器表产出相同的常量树，与地址
   空间/const 无关（r2 实证：参照探针 `.ll` 的 5 张表初始化器文本与 demo 41
   `gui.ll` 实物**逐字相同**，§8.2 步骤 R3）——不存在"AS4 版表在 clang 侧
   是另一份数据"的风险。
4. **clang -S 汇编文本不采用为 oracle**：文本 oracle 只能核字面 `.byte`
   流，核不出符号切片边界、重定位偏移/类型；且两通道的汇编伪机形态不同
   （`.area` 文本 vs ELF 记录），逐字节对表无法机械化。保留为**调试交叉
   手段**（实现者可用 `llc -filetype=asm` 肉眼复核），不作验收断言。

### 8.2 参照字节导出（r2 已实测；探针与 hex 留存 `/tmp/as4agg-rev-probe/`）

语料只读：`font.h` 从
`/home/liu/LLVM_STC32/mcs251-demos-rewritten/src/41-…/font/font.h` **拷贝**
到探针目录（41/42/43 三份 diff 相同已复核）。T0/drive.py 同款 flags
（`drive.py:193-201`：`--target=mcs251-unknown-none -std=c11 -O0
-fmcs251-keil` + `-Xclang -mcs251-memory-contract=1,1,32,8,1`）：

- **R0（现状拒绝，主验收的前置事实）**：`font.h` 原文（`code`=AS4）→
  clang rc=0 出 IR；llc 即拒：
  `LLVM ERROR: MCS251: __code global 'asc2_1206': unsupported initializer (…)`
  —— 真表全量、第一层 struct 即死，复现 p3-p11 结论（2026-09-14 复测）。
- **R1（参照 IR）**：`#define code __xdata`（AS3 通道）/ `#define code`
  （AS0 通道）各出一份 `.ll`；两份 clang rc=0（仅 `-Wexcess-initializers`
  截断警告，§4）。
- **R2（参照对象与导出）**：llc（`-mcs251-memory-contract=1,1,32,8,1
  -mcs251-object-format=elf -filetype=obj`）出两份 ELF；
  `llvm-readobj --sections --section-data` 取 `.mcs251.xinit`（AS0：
  记录头 2B 地址 + u16 objsize + u16 payloadsize）与
  `.mcs251.xdata_init`（AS3：3B 目标 + 同头），按头宽切段得每表 payload。
  **结果：两通道逐表 payload 相等 5/5**；尺寸 1152/1536/1972/592/520
  （合计 5772）与 §4 算术一致；每表 hex 存 `ref-<table>.hex`。
- **R3（gui.ll 一致性）**：参照 `.ll` 的 `@asc2_1206/@asc2_1608/@tfont16/
  @tfont24/@tfont32` 初始化器文本与
  `build/41-…/gui.ll` 实物**逐字相同**（5/5，§8.1 论证 3 的证据）。
- 参照流抽查值（防漂移锚点，完整 hex 在 `ref-*.hex`）：
  | 表 | size | head 12B | tail 12B |
  |----|------|----------|----------|
  | asc2_1206 | 1152 | `000000000000000000000000` | `000000000000000000000000` |
  | asc2_1608 | 1536 | `000000000000000000000000` | `000000000000000000000000` |
  | tfont16 | 1972 | `000027FC140414A481104208` | `13F8120812081FFE1000E580` |
  | tfont24 | 592 | `0000000000001800000CFFFC` | `3608803C00001C000000E8AF` |
  | tfont32 | 520 | `000000000000000000000000` | `000C0000000000000000E8AF` |

### 8.3 主验收断言（S1 落地后逐表执行，规范）

对每张表 T（size 记 |T|，§8.2）：

1. `code` 形态表（font.h 原文）经 T0 flags 出 `.ll` → llc 出 ELF 对象
   （R0 步骤由 FAIL 转 PASS——这一条本身就是主验收第一断言）。
2. `llvm-readobj --symbols` 取 `_<T>`：`Type: Object`、`Size: |T|`、
   `Value` 落在 `.text` 界内。
3. `llvm-readobj --sections --section-data` 切 `.text` 的
   `[Value, Value+|T|)` 字节，与 `ref-<T>.hex` **逐字节相等**。
4. `llvm-readobj --relocations` `.text`：重定位清单为空（5 表无指针叶；
   有指针叶的形态由 S2 F5 单测覆盖其 reloc 断言）。
5. demo 级不 re-run drive.py 全量（依赖链 §4 未变）：gui.ll 单文件 llc 今日
   死于**机器层 static-ptr 门**（"static pointer parameters are not
   supported by the compatibility ABI"，§2 复核）——函数发射发生在
   doFinalization 的全局发射之前，所以 S1 前后 gui.ll 的第一失败都是
   static-ptr；本切片断言只落在逐表隔离探针（§8.2/§8.3.1-4）上，
   AsmPrinter 层对 gui.ll 五表放行由探针证明，不依赖 gui.ll 能跑通。

### 8.4 单测层（降级，非验收主体）

r1 的 F1-F5 缩比探针 + 负例（§7 S2 清单）降级为 `code-struct-init.ll`
单测：形态覆盖回归（异构 packed、零图三级、字符串叶、struct 内 reloc、
opaque/空/对齐/AS0 拒绝面、**零图×非法类型边界**——空 struct 零图整项/
嵌套空 struct 零图负例 + 合格类型全零图正例护栏，§6A.4 探针场景），
golden 断言方式与 §8.3 相同但表是缩比手写
形态。**真表 golden 为主验收，单测为防回归细网；两者断言口径一致
（§6A.3 比较算法）。**

## 9. 作用域与顺序（定稿，PM 已拍板；替代 r1 的"PM 决策点"）

1. **struct 子句作用域 = AS4-only**。AS0 const struct 维持冻结拒绝
   （§6A.4 R4，存量 AS0 拒绝面测试 global-constant-error.ll、
   global-data-error.ll、global-ro-align-policy.ll、
   code-cast-static-init-boundary.ll、elf-errors.ll 全部不动）。
2. **顶层非数组 struct（p6）与嵌套 struct（p4）入范围**：递归写法免费
   获得，裁剪反而要加专门拒绝逻辑。
3. **struct 内指针叶（p10）入范围**：机制全在（§6A.3.6），一张 F5 单测
   + §11 探针已证身份扫描放行。
4. **实施顺序**：本切片独立无依赖；demo 41/43 的用户可见解锁仍需
   static-ptr 先行/同批，42 需 bit-addr（§4 依赖链）。
5. **文案**：随 S1 按 §6A.4 定稿文本落地（不再留"幅度待定"）。

## 10. code-placement.ll 精确修订断言（阻断 3a，定稿）

文件：`llvm/test/CodeGen/MCS251/code-placement.ll`（split-file；现 105 行）。
现行断言（已核行号）：`:6` STRUCT 负 RUN、`:7` SCALIGN 负 RUN、`:59`
STRUCT CHECK、`:60` SCALIGN CHECK、`:87` `;--- struct.ll` 片、`:88`
`@s = addrspace(4) global { i8, i16 } { i8 1, i16 2 }`。

**修订 1 —— `:6` 负 RUN 替换为正 RUN + 读回（片与 flags 不动）**：
```
; RUN: llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/struct.ll -o %t/struct.o
; RUN: llvm-readobj --sections --section-data --symbols %t/struct.o | FileCheck %s --check-prefix=STRUCT
```

**修订 2 —— `:59-:60` 两条 CHECK 替换为**（`struct.ll` 片 `:87-97` 原样保留）：
```
; STRUCT: Name: .text
; STRUCT: SectionData (
; STRUCT-NEXT:     0000: AA010002 |....|
; STRUCT-NEXT:   )
; STRUCT: Name: _s
; STRUCT-NEXT: Value: 0x1
; STRUCT-NEXT: Size: 3
; SCALIGN: LLVM ERROR: MCS251: __code global 'a': non-array storage must be byte-aligned (arrays of any declared alignment are emitted byte-aligned)
```
字节推导（各分量已实测/规范可推）：`ret void` 的平凡函数体 = 单字节
`AA`（r2 探针 `fn-only.o`：`.text` 恰 1 字节 `AA`，无 contract 参数、同一
llc 默认 v2 契约）；`{ i8, i16 }` 图像 = `01 00 02`（i8 1；i16 2 大端
`00 02`，§6A.3.1）；`@s` 在偏移 0x1、size 3、外部链接经 mangler 记作
`_s`（DEFS 前缀 `_devicedesc` 同规则）。**金样 `AA010002` 共 4 字节**；
S1 落地时先跑一次实际 readobj 复核（唯一允许的落地期校准点，仅当函数
发射字节变化时才需动 `AA`，struct 部分由规范锁定）。

**修订 3 —— 文件头注释（:8-13 X3 注释块）追加一行**：
`; AS4 struct aggregates are accepted here since the AS4-AGGREGATE slice`
`; (design AS4-AGGREGATE-INIT-DESIGN §6A); AS0 stays array/scalar only.`
（DEFS/TAB/REL/SCALIGN 片与其余断言**一律不动**。）

## 11. `!mcs251.signatures`/A4 身份不受全局初始化器形态影响（阻断 3b）

**论点**：S1 只改 RO 门+发射器（`isSupportedROInitializer:885-907`、
`emitROInitializer:917-945` 及两调用点/文案），不触碰任何身份路径函数；
且身份载荷的输入与全局初始化器**无交集**。

**发射路径证据（file:line）**：

1. 身份判定与发布时序：`doInitialization` 先跑 `classifyModule(M)`
   （`:1853-1862`，`emitStartOfAsmFile` `:1670-1679` 幂等重入）——发生在任何
   全局字节发射之前；身份节 `.mcs251.attributes` 在 `emitEndOfAsmFile`
   （`:1817-1851`）发布，在所有全局之后。两者之间的全局发射
   （`emitGlobalVariable` `:1865-2001` → `emitAddressSpacedGlobal`
   `:2021-2111`）**不读写任何签名/身份状态**。
2. v2 身份字节的生产者：`emitEndOfAsmFile` 用
   `MCS251Attributes::renderRegisteredIdentity(Contract->AS0PointerBits,
   Contract->DefaultPlacement, FunctionSignatures)` 渲染（`:1829-1831`）——
   输入只有**契约常数**与**签名表**，decode 校验后经
   `emitSelfDescribingAttributesSection` 入独立节（`:1832-1850`）。全局
   初始化器不进任何输入。
3. `!mcs251.signatures` 的消费：`buildFunctionSignatures`（`:1558-1605`）只读
   named metadata（`:1563-1585`）+ 校验模块外部函数全覆盖（`:1592-1604`）；
   由 `emitStartOfAsmFile` 在 v2 对象模式下调（`:1687-1690`）；helper 折叠
   来自机器流（`:1610-1626`、`:1630-1636`）。均与初始化器无关。
4. 身份扫描里的初始化器走查是**能力检查，只判定不发射**：
   `scanObjectIdentity`（`:541-631`）对全局只回 bool（`:586-631`）。它对
   struct 形态**今日已放行**：
   - 值类型半：AS3/AS4 全局值类型要求过 `isSupportedMutableType`
     （`:621-622`），其 struct 段（`:697-703`）早已接受非 opaque 非空
     struct（含 packed/异构）；
   - 初始化器半：`:626-630` 经 `hasV1ObjectCompatibleConstant`
     （`:271-288`，类型形状走查）或 `hasV1PlacementInitializer`
     （`:375-408`；struct 递归 `:393-407`、指针叶 `:388-392`）——两者都
     递归 struct。S1 不改这三个走查器。
   - 判定不通过的后果是**拒绝整个模块**（`report_fatal_error`，
     `:1428-1434`/`:1448-1456`），不是改身份字节；struct 形态既然今日判定
     通过，S1 前后判定结果不变。
5. **探针实证（2026-09-14，`/tmp/as4agg-rev-probe/`）**：
   - `idn-struct.ll`：`@s = addrspace(4) constant { i8, i16 } …` +
     `@t`（嵌套 struct 数组含零图整项与字符串叶）+ `!mcs251.signatures` →
     llc 报 **AsmPrinter 门文案** "unsupported initializer (…)"，栈帧
     `MCS251AsmPrinter::emitGlobalVariable` —— 身份门已过、死在 RO 门。
   - `idn-struct-ptr.ll`：`@p = addrspace(4) constant { ptr addrspace(4), i8 }
     { ptr addrspace(4) @msg, i8 9 }`（struct 内指针叶）→ 同样死在
     AsmPrinter 门 —— 能力扫描对"struct+指针叶"亦放行。
   - 由此：v2-default llc 要求 signatures 只是因为身份门需要元数据
     （`:1564-1568` 的 fatal），**不是因为初始化器形态参与身份**；S2/S3 的
     struct 测试片照 code-placement.ll 现例补 `!mcs251.signatures` 即可，
     不需要任何身份侧改动。
6. 推论：全局初始化器从"数组拒绝"变"struct 发射"，v2 身份字节
   （e_flags=0x102 + `.mcs251.attributes`）与签名集合**逐位不变**；
   §8.3 的 golden 对表因此与身份载体天然正交（不同节、不同输入）。

## 12. 回归预核（2026-09-14 当前二进制，实施不得破）

```
cd /home/liu/LLVM_STC32/MCS251
/home/liu/build-mcs251/bin/llvm-lit -a llvm/test/CodeGen/MCS251
  → Total 149, Passed 149 (100%), ~1.5s
/home/liu/build-mcs251-s1/bin/llvm-lit -a clang/test/CodeGen/mcs251-*.c
  → Total 26, Passed 26 (100%), ~11.7s
```
- clang 侧另有 `mcs251-opencl-half.cl` 不在 `*.c` 族基线内（未跑）。
- 重点不得破族：`code-*`（placement/cast-static-init-boundary/load/store/
  addrspacecast）、`global-*`（AS0 冻结拒绝面）、`as4-*`（clang 端 superset/
  cross-tu/string-literal-provenance/implicit-const-grid）、`xdata-*`、
  `mcs251-xdata-code*.c`、`mcs251-code-string-table.c`、`bit-*`、`isr-*`、
  `mcs251-signatures.c`、`asxxxx-*`。
- 已知必须的意识修订：仅 `code-placement.ll` STRUCT 段（§10 精确配方）。

## 13. 证据命令索引

- r1 探针编译/降级循环：§2 三条命令（改文件名即得 §3 逐行）。
- r2 探针（`/tmp/as4agg-rev-probe/`）：R0-R3 与主验收命令全文见 §8.2/§8.3；
  参照 hex `ref-asc2_1206.hex`、`ref-asc2_1608.hex`、`ref-tfont16.hex`、
  `ref-tfont24.hex`、`ref-tfont32.hex`；解析脚本 `parse.py`（读
  readobj `--section-data`、按记录头切段）。
- 身份探针：`idn-struct.ll`、`idn-struct-ptr.ll`、`fn-only.ll`（§10 金样
  的 `AA` 分量）。复现：
  `/home/liu/build-mcs251/bin/llc -mtriple=mcs251 -filetype=obj
  -mcs251-object-format=elf <probe>.ll -o <out>.o`。
- 零图边界探针（r3，§6A.4）：`z1-empty-struct-zero.ll`（空 struct 零图
  整项）、`z2-nested-empty-struct-zero.ll`（嵌套空 struct 零图）、
  `z3-qualified-all-zero.ll`（合格类型全零图对照）、
  `z5-nested-empty-nonzero.ll`（非零图根含空成员）。复现：同上命令 +
  `-mcs251-memory-contract=1,1,32,8,1`（z1/z2 今日 rc=0、z3 前后均
  rc=0、z5 今日死于 RO 门现行文案）。
- gui.ll 实物：`/home/liu/LLVM_STC32/mcs251-demos-rewritten/build/41-*/gui.ll`
  （`grep -o "@asc2_1206 = .\{0,120\}"`）。
- 41/42/43 台账：`ledger.json` `demos["41-*"].tiers/t0_log/t1_log`（T0 pass/
  T1 static-ptr；42 T0 bit-addr；三份 font.h diff 相同，r2 复核）。
- baseline：§12 两条 lit 命令。
- 拒绝点：`grep -n "unsupported initializer"
  llvm/lib/Target/MCS251/MCS251AsmPrinter.cpp`（:2105-2106）；门
  `:885-907`；mutable 镜像 `:682-704/:706-731/:740-780`；身份路径
  `:1407-1460/:1558-1605/:1817-1851/:541-631`（§11）。
