# MCS251 switch 跳转表（br_jt）缺口设计稿（rev3，Alice 二轮阻断已落实）

- 状态：draft rev3（rev2 后 Alice 复审余两阻断，逐条落实；PM 已裁定，**本稿无开放决策**）
- 日期：2026-09-15；工作树 minimal-isr @ 2e08e94ae
- 探针记录（仓库外）：/home/liu/LLVM_STC32/GAP-BRJT-PROGRESS.md（P1–P8 基线 + R1/R2 修订节）；探针源码：/home/liu/LLVM_STC32/GAP-BRJT-PROBES/
- 参照结构：SEXTLOAD-DESIGN.md / X3-IMPL-BRIEF.md；已冻结裁定参照：DESIGN.md §"单表必须单列"（:1012-1023）、ELFRelocs/MCS251.def 冻结号段
- rev2 修订记录：①案 a 补 Expand 六形态×两档 golden/语义证据（§3.1.2–3.1.3）；②案 b-1 dispatch 序列修正（原 `add a,dpl; mov dpl,a` 丢进位，作废；定稿序列及依据 §3.2.1）+ 补齐 DPTR 生命期/表项格式/bank 约束/索引换算规范级文字（§3.2.2–3.2.5）；③D3/D4 按 PM 裁定写死，资格谓词具体化（§3.0、§3.2.6）；④两切片独立入口/独立测试族/独立门槛（§4、§5）
- rev3 修订记录（Alice 二轮两阻断）：⑤§3.2.6 E3/E5 上游语义修正——上游 Range = High−Low+1（SwitchLoweringUtils.cpp:24-34），且 base 判据 `(OptForSize ∥ Range ≤ MaxJumpTableSize)`（TargetLoweringBase.cpp:1810-1812）使硬帽在 optsize/minsize 下整体失效（MaxJumpTableSize 默认 UINT_MAX，:76-78；hasOptSize 短路实测确证，P9 系列）——E3/E5 改为覆写虚函数 isSuitableForJumpTable（TargetLowering.h:1463）的**后端独立检查**，并补 optsize/minsize 超界回退测试矩阵（§3.2.6、§4.2 H4、§5 表二）；⑥§3.2.4 整表同 bank 依据重写——现役 CODE 放置无整 area/整 section 单 bank 检查（LinkerCore.cpp:2291-2301/:2303-2340 实读），逐项 J16 不能作为设计依据；写明三道检查归属（编译期跨度断言 + lld 新增显式跨度检查为主闸，现役逐项 J16 仅兜底）与负例设计（§3.2.4、§4.2 H5）

## 0. 问题陈述

任何让 SelectionDAG 生成 jump table 的 switch，在 MCS251 后端都会以 fatal error 终止编译：
`Cannot select: … ch = br_jt …, JumpTable:i32<0>, …`（ISD::BR_JT 无 pattern）。BRIND 同样无 pattern。
这是基线对拍登记的缺口，阻塞官方 demo 43、62 改写副本（LCD.c / tft.c 的 `LCD_direction`）。
下游后果分两层：

- clang -O0（以及任何把 switch 送到 ISel 的管线）：直接崩溃；
- llc 直读含 switch 的 IR：任意优化档（-O0/-O2）都崩溃——本后端对所有前端第二来源都是断路。

clang -O1/-O2/-Os/-Oz 的小 switch 因 SimplifyCFG switch-to-lookup-table 变成 IR 级常量表而不踩；
但 demo 43/62 的 switch 带 side effect（分支体调用+不同存储），在 -O2 仍以 `switch i32` 幸存到 ISel，
因此真实 demo 在 -O0/-O2 双档均崩（前提：ELF 输出；默认 REL 输出先撞 A4 v1 identity 报错，属另一缺口）。

## 0.1 证据基线（全部已实测；复现命令见探针记录 P1–P7、R1）

定位（file:line）：

| 环节 | 位置 | 事实 |
|---|---|---|
| JT 资格门（上游通用层） | llvm/include/llvm/CodeGen/TargetLowering.h:1444-1450 | 默认 `areJTsAllowed` = `isOperationLegalOrCustom(ISD::BR_JT,MVT::Other) || …(ISD::BRIND…)` |
| 默认 Legal 的来源 | llvm/lib/CodeGen/TargetLoweringBase.cpp:765-767 | `initActions()` memset → 未定制即 Legal；MCS251 未定制 → 门恒开 |
| JT 门槛与密度 | SwitchLoweringUtils.cpp:62（areJTsAllowed）、:65、:68-70（`N < 2 || N < MinJumpTableEntries` 弃 JT）；TargetLoweringBase.cpp:72-75（`min-jump-table-entries` 默认 **4**）、:82-91（密度默认 10 / optsize 40）、:1794-1812（`isSuitableForJumpTable`：**`(OptForSize ∥ Range ≤ MaxJumpTableSize) && NumCases×100 ≥ Range×MinDensity`**——rev3 更正：`OptForSize` 在 `∥` 左侧短路，**硬帽在 optsize/minsize 下整体失效**；且 `max-jump-table-size` 默认 **UINT_MAX**（:76-78），默认无帽。OptForSize 由 `shouldOptimizeForSize`（MachineSizeOpts.cpp:36-44）对 `hasOptSize()`（optsize/minsize 属性）恒真）。上游 Range = **High−Low+1**（getJumpTableRange，SwitchLoweringUtils.cpp:24-34；buildJumpTable 按 `(High−Low)+1` 逐项填表、空洞填 DefaultMBB，:218-228）。**结论：资格检查不能依赖上游硬帽，须后端独立实现（§3.2.6 rev3）** | **case 数 < 4 时上游根本不进 JT 分支**——这是 §3.1.2 探针法的依据 |
| JT 构建/pivot | SwitchLoweringUtils.cpp:86-89 起 | -O0 不做 pivot 二分（直接 return）；-O1+ 走 Kannan-Proebsting 分片 |
| br_jt 节点/header | SelectionDAGBuilder.cpp:3023-3032（visitJumpTable）、:3037-3082（visitJumpTableHeader） | header 生成 `SUB(cond, Low)` → zextOrTrunc 到 `getJumpTableRegTy`（默认 i32）→ `SETUGT idx, (High−Low)` → BRCOND 出界走 default；全部落在现装 BR_CC/BRCOND 定制上 |
| 崩溃点 | SelectionDAGISel.cpp:4623 | `CannotYetSelect` → "Cannot select: …" |
| 后端现装 lowering | MCS251ISelLowering.cpp:33-58 | 只定制 BR_CC(i8/i16/i32)/BRCOND/SETCC/SELECT_CC；无任何 BR_JT/BRIND 动作 |
| 指令覆盖 | MCS251InstrInfo.td | 有 SJMP/EJMP（696-697，直跳 brtarget）、ECALL/ECALLr（768-771）、**ADD8rr/ADD8ri（486-491/530-533，双操作数累加 `$lhs=$dst`）、MOV8ri（183）、MOV8a/MOV8ra（232/241）、MOV8dpl/MOV8dph（196-198，Defs 钉位）、MULAB（578，无 pattern）、cmp 族（635-642）**；无 ADDC、无间接跳转、无 MOVC、无 0x73/0x90、无 br_jt/brind pattern |
| 寄存器事实 | MCS251RegisterInfo.td:110-167、:250-274 | A(0xe0)/B(0xf0)/DPL(0x82)/DPH(0x83)/DPTR=DPL:DPH 均为**固定 SFR、不属于任何可分配类、getReservedRegs 保留**，仅经钉位指令访问；CSR_MCS251 callee-saved 列表为空，**一切寄存器跨 ecall 均 caller-saved**（dpl/dph/dptr 明确无 preserved 位） |
| MC fixup 族 | MCTargetDesc/MCS251FixupKinds.h:28-49 | fixup_mcs251_{16,24,lo8,mid8,hi8,bitaddr8}；**无 J16/J11 fixup**（J16/J11 目前非 MC 产出通道） |
| 同类先例 | MSP430ISelLowering.cpp:87、AVRISelLowering.cpp:131 | `setOperationAction(ISD::BR_JT, MVT::Other, Expand)` 关 JT，switch 全走 case-cluster 比较链 |

比较链路径（现装且可用）：

- MCS251ISelLowering.cpp:451-472（BRCOND 定制→转 BR_CC）、:1570（LowerBR_CC：位分支捷径 + i8/i16/i32 通用比较路径）。
- 实证：所有探针 + demo43 LCD.c 在 `-fno-jump-tables`（= 属性 "no-jump-tables" → areJTsAllowed=false）下 -O0/-O2 全部通过；
  LCD_direction -O0 生成 4 个 cmp 的线性链，-O2 生成 5 个 cmp 的 pivot 树（SwitchLoweringUtils 只在 -O1+ 做 pivot）。
- rev2 追加实证（R1 系列，见 §3.1.2）：**无需任何属性**，case 数 < 4（默认阈值）即不进 JT 分支，同一链出口对六形态×两档全绿。

运行时/链接事实（决定方案 b-1 的形态，见 §3.2）：

- QEMU target/mcs51/helper.c:925-929：opcode 0x73 `jmp @a+dptr`：`PC ← (PC & 0xff0000) | ((DPTR+A) & 0xffff)`——**硬件完成 16 位 DPTR+A 求和**、本 bank。
- QEMU helper.c:982-986 / decode.c:75 / disas.c:438：opcode 0x90 `mov dptr,#imm16`：3 字节（90 hi lo），DPTR ← 16 位立即数。
- QEMU helper.c:988-994：opcode 0x93 `movc a,@a+dptr`：读 CODE，地址 = (PC & 0xff0000) | ((DPTR+A) & 0xffff)。
- SDCC gold（对照基准）：src/mcs51/gen.c:12313-12455 `genJumpTab`：n≤7 用 **ljmp 表**（表项即 3 字节 ljmp，`mov dptr,#tab; ×3; jmp @a+dptr`——**加法交给 0x73 的硬件 16 位求和**）；n>7 用 lo/hi 双字节表（b-2，本稿不做）。
- lld/MCS251/LinkerCore.cpp:1790-1802：R_MCS251_24/J16/J11 = CODE 通道；R_MCS251_16 与 LO8/MID8/HI8 = DATA 通道。
- LinkerCore.cpp:3303-3343：J16 = 2 字节**大端**绝对地址（`Put(U>>8); Put(U)`），目标必须 CODE（:3333），bank 检查 `((P+2)&0xff0000) != (U&0xff0000)` → fail `"J16 bank overflow in <sec>"`（:3335-3336，P 为 fixup 场地址）；R_MCS251_24 = 3 字节大端 24 位绝对地址，无 bank 约束。
- ELFRelocs/MCS251.def:13-28：号段 0..11 冻结已用，下一个空闲号 = 12。
- DESIGN.md:1012-1023 已冻结"单表必须单列/16 位表访问路线"措辞：表及其全部可访问字节不跨所属 64K bank（:1021），"**仅有 `size<=65536` 不够；起点偏移也必须满足范围**"（:1022），超限报错不截断（:1023）。

## 1. demo 需求画像（最小真实子集）

demo 43（src/43-…/LCD/LCD.c）与 demo 62（src/62-…/tft.c）各恰好 1 个 switch，形态完全一致：

- `LCD_direction(u8 direction)`；IR 中因 reg-params ABI 提升为 `switch i32`；
- 4 个 case，值域 0..3（密集、无空洞），有 default（default 体为空）；
- case 体 = 常量全局存储 + 常量参数调用（`LCD_WriteReg(0x36, 常量)`），非 value-only（有 side effect）→ SimplifyCFG 在 -O2 无法整体变 lookup table，switch 幸存；
- 不在循环内；单次判派。

结论（本设计要覆盖的最小真实子集）：**单一 switch i32（源级 u8/i8/i16 均可），case 数 4–16，密集值域，带 default，case 体任意（含调用/存储）**。
稀疏值域（switch4-char 形态）在 -O0 也踩 br_jt，属同一缺口，但不是 demo 需求——归入"正确性兜底"（比较链天然覆盖），不进 JT 使能条件。

## 2. 现装 switch lowering 路径（事实陈述）

1. clang 前端 → SimplifyCFG（-O1+）：value-only switch → IR 常量表（`@switch.table.*`，落 CSEG，经既有 CODE 常量读机制访问；-O2 探针反汇编证实）→ 不踩。side-effect switch 幸存 → 踩。
2. SelectionDAG 层：`areJTsAllowed` 恒 true（本后端无 override）→ case 数 ≥ 4 时建表 → `visitJumpTable` 发 ISD::BR_JT → pattern 匹配失败 → CannotYetSelect fatal。case 数 < 4 时不进 JT 分支，走比较链（§3.1.2 实测）。
3. 比较链（唯一现装出口）：case-cluster 机器在 JT 资格被否决后退化为 BRCOND/BR_CC 链（-O0 线性、-O1+ pivot 二分树）。触发手段现有三种：函数属性 `-fno-jump-tables`、case 数 < 4、以及本设计的 S1 关闸。S1 的本质 = 把第三种手段变成默认，把第二种手段从"偶然幸存"变成"可证不变式"。

## 3. 方案（形态已由 PM 裁定，见 §3.0）

### 3.0 PM 裁定（已定，2026-09-14；本稿全篇按此执行，不保留开放决策）

1. **D1 关闸 + 真表两切片都做**：案 a（BR_JT/BRIND 置 Expand，switch 全走比较链）为切片 S1；案 b-1（CODE 空间 ljmp 真跳转表）为切片 S2+S3。b-2（双字节表）不做（§3.3）。
2. **D3 资格检查定稿**：案 b-1 采用**编译期资格检查，不合格回退比较链**；lld 的 J16 检查仅作几何兜底（越限显式报错，不静默截断）。具体判据见 §3.2.6 谓词 E1–E6。
3. **D4 默认策略定稿**：**默认关闸**（比较链，现装行为固化）；真跳转表经 `-mllvm -mcs251-jump-tables`（MCS251 cl::opt，默认 false；clang 驱动可同名透传）显式 opt-in。先保证稳定，再谈尺寸。

### 3.1 案 a：关闸——BR_JT/BRIND 置 Expand，switch 全走比较链（切片 S1）

#### 3.1.1 机制

改动点：MCS251ISelLowering.cpp 构造器，按 cl::opt `MCS251JumpTables`（`-mcs251-jump-tables`，cl::init(false)，D4 裁定默认关）分派（仿 MSP430ISelLowering.cpp:87）：

```
if (!MCS251JumpTables) {
  // 默认：关闸。areJTsAllowed 读资格位返回 false，BR_JT/BRIND 节点从此不被生成。
  setOperationAction(ISD::BR_JT,  MVT::Other, Expand);
  setOperationAction(ISD::BRIND,  MVT::Other, Expand);
} else {
  // S2+S3 opt-in：BR_JT 走 Custom（LowerBR_JT）；BRIND 仍 Expand（本设计不为 indirectbr 建 pattern，R3）。
  setOperationAction(ISD::BR_JT,  MVT::Other, Custom);
  setOperationAction(ISD::BRIND,  MVT::Other, Expand);
}
```

- `Expand` 仅作资格位（areJTsAllowed 读它返回 false），默认路径下 BR_JT/BRIND 节点不再被生成，无需真的写 Expand 语义；
- `-fno-jump-tables` 属性（属性门）与 S1（目标门）叠加时取交集，天然兼容；
- 重定位需求：无（链上只有既有 brtarget/EJMP + cmp 立即数）；
- 与 CSEG/AS4 规则交互：无（纯控制流，不产生数据节）。

#### 3.1.2 Expand 证据（Alice 阻断①；R1 系列探针，全部实测于 2026-09-14）

**方法**：构造 case 数 = 3（< 默认阈值 4，SwitchLoweringUtils.cpp:70）的六形态探针，**不加任何属性/开关**，llvm-as + llc -O0/-O2 实测。当前后端链出口已工作，不触发 br_jt 即不崩，故可直接取得"Expand 落地后超阈值形态将进入的同一个出口"的金样。

命令骨架（六探针同构，替换文件名即可）：

```
P=/home/liu/LLVM_STC32/GAP-BRJT-PROBES
/home/liu/build-mcs251/bin/llvm-as $P/chain3-dense-emptydef.ll -o $P/chain3-dense-emptydef.bc
/home/liu/build-mcs251/bin/llc -mtriple=mcs251 -O0 $P/chain3-dense-emptydef.bc -o $P/chain3-dense-emptydef-O0.s
/home/liu/build-mcs251/bin/llc -mtriple=mcs251 -O2 $P/chain3-dense-emptydef.bc -o $P/chain3-dense-emptydef-O2.s
/home/liu/build-mcs251-s1/bin/clang --target=mcs251 -S -O0 $P/switch3-u8.c -o $P/switch3-O0.s   # C 侧交叉验证
```

结果总表（12 次 llc 全部退出码 0，无 br_jt）：

| 探针 | 形态维度 | -O0 链形态 | -O2 链形态 |
|---|---|---|---|
| chain3-dense-emptydef.ll | 密集 0..2 + 空 default | 3×（`mov dr4,#k; cmp dr0,dr4; jne 下一案; ejmp 本案`）线性；全不中落 default | 3 cmp 线性（**倒序** 2,1,0 + je/jne 反转取 fall-through） |
| chain3-nonemptydef.ll | 同上 + **非空 default**（default 带 store） | 同上；default 块 .LBB0_4 含 store 序列 | 同上 |
| chain3-sparse.ll | **稀疏** 1/100/10000 | 同构 eq 链，cmp 常量 `#0x0001 / #0x0064 / #0x2710` | 同上（倒序） |
| chain3-neglow.ll | **负下界** -3/-2/-1 | `mov dr4,#0xfffd; movh dr4,#0xffff`（=-3）后同构 eq 链 | 常量 `#0xffff/0xffff`、`#0xfffe/0xffff`（-1/-2，倒序） |
| chain3-nonzerolow.ll | **非零下界** 5/6/7 | cmp 常量 `#0x0005/6/7` | 同上（倒序） |
| chain3-sideeffect.ll | demo43 同形（u8 提升开关点，case 体 = volatile store + `LCD_WriteReg(常量,常量)`） | 3 cmp 线性链 + 每 case 块完整 store/调用 | 3 cmp + 完整 case 体 |

C 侧交叉验证（switch3-u8.c，同 demo43 写法，**不加 -fno-jump-tables**）：
clang -O0 退出码 0、3 cmp 线性链；clang -O2 退出码 0（SimplifyCFG 折叠为范围判派，1 cmp，不触 JT）。

金样摘录（chain3-dense-emptydef -O0，Case 0 段；完整 .s 入库 GAP-BRJT-PROBES/）：

```
	mov dr4, #0x0000
	cmp dr0, dr4
	jne .LBB0_7          ; 不中 → 下一案比较
	ejmp .LBB0_1         ; 命中 → case 0 体
…
.LBB0_4:                    ; %default（三次比较全不中）
	dec spx, #0x4
	eret
```

**语义论证**（每条对应上表一行，全部可由金样直接核）：

1. **完备且不重不漏**：每个 case 值恰被一次 eq 比较（`cmp dr0,dr4` + `jne`）命中；三次比较全不中的唯一出口是 default 块。判派正确性 = "值相等 ⇔ 走该 case 体"，与 case 排列无关。
2. **符号无关性 → 负下界天然正确**：eq 比较不依赖符号标志语义；负常量以 `mov dr4,#低16位; movh dr4,#高位` 32 位形态置入比较寄存器（0xfffd/0xffff = -3，实测），比较本身逐位进行，无符号歧义。
3. **稀疏与空洞不影响语义**：稀疏值只是 cmp 立即数不同（0x2710 = 10000，实测）；空洞落在"jne 下一案 → 最终 default"的既有通路上。
4. **-O0/-O2 同为线性 eq 链**：N=3 时无 pivot（pivot 属 JT 分片机制，-O1+ 的 Kannan-Proebsting 只作用于 JT 资格内的 cluster）；-O2 仅有降序排列与 je/jne 反转（fall-through 优化），比较次数不变（3）。
5. **default 空/非空只差 default 块体**：判派结构逐字节同构（非空版 .LBB0_4 多一个 store 序列，实测）。
6. **case 体 side effect 不受链形态影响**：case 块为独立 MBB，store/call 序列与判派块解耦（sideeffect 探针 -O0/-O2 均含完整 store+ecall，实测）。

#### 3.1.3 Expand 落地后的等价性论证 + S1 测试矩阵（Alice 阻断①后半）

**同一性论证**：JT 与链的唯一分岔点是 `areJTsAllowed`（SwitchLoweringUtils.cpp:62）与 `N < MinJumpTableEntries`（:70）。案 a 置 Expand 后，**任意 case 数**在 :62 同样被否决——超阈值（N ≥ 4）形态与上述 N=3 形态走**同一条 case-cluster → BRCOND/BR_CC 链路径**，唯一差异是上游把连续 case 值打包为 range cluster（判派树内出现范围比较形态）。N=3 侧的六形态语义已由金样证明；N ≥ 4 侧"当前崩、落 Expand 后进链"这一步的正确性**不由本稿断言，由 S1 实施切片的测试矩阵实证**：

| S1 矩阵项（超阈值，实施前红/实施后绿） | 维度 | 验收断言 |
|---|---|---|
| dense-i8/i16/i32 × 4/8/16 case | cond 类型 × 规模 | -O0/-O2 编译通过；cmp 次数 ≤ case 数（-O0 线性）/ ≤ 2⌈log2 n⌉（-O1+ pivot）；FileCheck pin 判派树覆盖全部 case 值与 default |
| sparse（switch4-char 形态扩展到 ≥4 值） | 稀疏 | eq 链，判派正确，不出 br_jt |
| 负下界（-4..-1）/ 非零下界（5..8） | 下界 | 常量 32 位形态正确；语义同 §3.1.2 论证 2 |
| default 空 / 非空 | default | 同构判派，default 块体差异仅块内 |
| case 体含 call+store（demo43/62 同形 4 case） | side effect | 全编译通过；链与 case 体解耦 |
| demo43 LCD.c / demo62 tft.c 全文件（ELF 输出） | 端到端 | -O0/-O2 均不再崩（原 P2 崩溃用例转绿） |
| llc 直读 switch4-u8-O0.ll / switch16-i16-O0.ll | 第二来源 | -O0/-O2 均不崩（原 P1 崩溃用例转绿） |

- S1 门槛（独立验收，见 §4 G1–G5）以本矩阵为主体；
- 风险注记：BRIND 置 Expand 后 llc 直读 indirectbr 的 Legalizer 实际行为需在 S1 实测并入库（R3，上游多数目标同样 Expand，预期 not --crash，但不由本稿断言）。

### 3.2 案 b-1：CODE 空间 ljmp 真跳转表（切片 S2+S3，`-mllvm -mcs251-jump-tables` 显式 opt-in）

#### 3.2.1 dispatch 指令序列（rev2 定稿；原稿序列作废）

**原稿序列作废原因**（存档备查）：

```
    add  a, dpl          ; 8 位加法：CY 丢弃
    mov  dpl, a          ; ← base_lo + 3×idx ≥ 256 时 DPH 未 +1 → 静默跳错表项（跨 256 字节页即触发）
    clr  a               ; 依赖"手动加法"才需要的清零，最终序列中不存在
    .db 0x73
```

本 ISA **无 ADDC**（MCS251InstrInfo.td 全文仅 CLRC + CY 旋转链，无任何带进位加法），任何"先把和写回 dpl/dph"的软件 16 位加法都需 CY 链展开，不可取。

**rev2 定稿序列**（6 槽；核心思想：**加法交给 0x73 的硬件 16 位求和，软件只备 A 与 DPTR**）：

```
; 输入：rI = 索引（GPR8，0..Range，由 JT header 产生，见 §3.2.5）；rT = GPR8 暂存
    mov  rT, rI          ; MOV8rr
    add  rT, rT          ; ADD8rr（双操作数累加）：2×idx
    add  rT, rI          ; ADD8rr：3×idx —— 谓词 E3 保证 3×idx ≤ 255，8 位不溢出
    mov  a, rT           ; MOV8a（Defs=[A]）
    mov  dptr, #jt       ; MOVDPTRri（新增，0x90：90 hi lo；Defs=[DPL,DPH]）表基址，J16 fixup，见 §3.2.3
    jmp  @a+dptr         ; JMPIAD（新增，0x73；Uses=[A,DPL,DPH]）
                         ;   PC ← (PC & 0xFF0000) | ((DPTR + A) & 0xFFFF)——16 位和由硬件完成，无进位问题
```

**依据**（逐条）：

1. **0x73 语义**（QEMU helper.c:925-929，实测模拟）：`jmp @a+dptr` 内部即做 16 位 `DPTR+A`——SDCC gold 同构（gen.c:12313-12455：`mov dptr,#tab; ×3; jmp @a+dptr`）。把索引留在 A、基址留全 DPTR，天然无进位。
2. **`mov dptr,#imm16` 硬件形态存在**（QEMU decode.c:75 / helper.c:982-986，disas.c:438 打印 `mov dptr,#0x%04x`，3 字节 90 hi lo）。现装 ISA 无 dpl/dph 立即数形式（MOV8dpl/MOV8dph 仅收 GPR8 源，MCS251InstrInfo.td:196-198），MOV8ri（:183）仅收 mcs251_imm8 字面立即数不能带符号重定位，FIADDR 仅限 frame-index（MCS251ISelLowering.cpp:1927）——故 **0x90 是表基址建立的唯一可行形态，属 S2 必增指令，不是可选优化**。
3. **×3 用双 ADD8rr 而非 mul ab**：MOV8b 无立即数形式（:239 仅收 GPR8），`mov b,#3` 需先 `mov rB,#3; mov b,rB`（4 槽 ≥ 3 槽），且 MULAB 无 pattern（:578）；add/add 更短且不碰 B。
4. **8 位域约束进谓词**：A 为 8 位，`3×idx ≤ 255 ⇔ idx ≤ 85 ⇔ 表项数（上游 Range = High−Low+1）≤ 86`（§3.2.6 E3，rev3 语义更正）；越限即错项，故为资格谓词而非运行时检查。

#### 3.2.2 DPTR 建立责任、跨调用保存/恢复、寄存器活跃性交互（规范级）

1. **基址建立在每个 dispatch 点内联完成**（§3.2.1 的 `mov dptr,#jt` 槽）。**禁止跨调用/跨块缓存 DPTR 表基址**——不存在"表指针寄存器"约定。
2. **无跨调用保存/恢复义务**：CSR_MCS251 的 callee-saved 列表为空，一切寄存器跨 ecall 均 caller-saved（MCS251RegisterInfo.td:256-274 注释明示 callee 可任意覆写 dpl/dph/dptr）；dispatch 序列内部无 call，故既不新增保存义务也不受既有调用序列影响。
3. **DPL/DPH 覆写安全**：DPL/DPH 是 ABI 返回值/首参位，但其值从不驻留（call 结果经 CopyFromReg 立即取走，RegisterInfo.td 注释明示"never assumed live across the call"）；覆写点与任何读点之间由 Defs=[DPL]/[DPH] 钉位依赖排序——与现役 MOV8dpl/MOV8dph 同一机制。
4. **活跃性**：A/B 固定 SFR、从未分配（不在 GPR8，RegisterInfo.td:150-163）；rI/rT 为普通 GPR8 虚拟寄存器，RA 正常处理。`mov dptr,#jt` 与 `jmp @a+dptr` 之间为同一 MBB 内的 Defs→Uses 链，调度器不得交越（JMPIAD Uses=[A,DPL,DPH] 声明完整）。header 产生的索引跨块传递由上游 CopyToReg 机制承接（SelectionDAGBuilder.cpp:3059-3061）。
5. **与 MOVX/DPXL 通道无交互**：本序列不触碰 DPXL（0x84）；X2-1 自愈协议（每次 MOVX 前重指 region）不受影响——dispatch 改写的只是 dpl/dph 本身。
6. **PSW**：两条 ADD8rr 写 PSW（:485 声明），JMPIAD 不读 PSW；判派块内无跨 dispatch 的标志依赖（链/header 的比较在 dispatch 之前完成）。

#### 3.2.3 表项格式与发射（规范级）

1. **表 = N 个连续 3 字节 ljmp**：byte0 = `0x02`，byte1–2 = J16 字段（**大端**，`Put(U>>8); Put(U)`，LinkerCore.cpp:3341）；项内偏移 +1 即 J16 字段——与 HOME 3 字节 trampoline 同构（LinkerCore.cpp:3059-3064 校验逻辑不变，仅新增产生方）。
2. **MC 层新增 fixup kind `fixup_mcs251_j16`**（MCS251FixupKinds.h 现族 16/24/lo8/mid8/hi8/bitaddr8 之外追加）→ ELF 重定位**复用 R_MCS251_J16 = 7**，号段 0..11 冻结不动。该 fixup 用于两处：表项 ljmp 目标字段、`mov dptr,#jt` 的 16 位立即数字段——两者同为"同 bank CODE 控制地址"，恰是 J16 的语义（CODE 通道 + 同 bank 检查在链接期一并兜底，见 §3.2.4）。
3. **.s 发射形态**（ASXXXX 风格，字节序与 lld 写入序一致；参照 R1 探针实测的 `.db` 字节流拼写）：每表项
   `.db 0x02, (<Lcase_k>) >> 8, (<Lcase_k>)`
   （hi 字节在前、lo 字节在后，项距恒 3 字节——index×3 索引算术依赖项距不变）。
4. **AsmPrinter**：覆盖 `MCS251AsmPrinter::emitJumpTableInfo` 为逐项 ljmp 字节发射，**禁走默认 EK 路径**（默认 emitValue 产出 .word → R_MCS251_16 = DATA 通道，通道错，LinkerCore.cpp:1790-1802）。
5. **表排布**：表为专用数据列（DESIGN.md "单表必须单列"），不与其它数据混排；表头无对齐要求（索引算术不依赖 256 字节页边界，0x73 为全 16 位求和）。

#### 3.2.4 bank 假设与 CSEG 约束（D3 定稿的放置部分；rev3 重写整表跨度检查归属）

1. **硬件约束两条**：
   - 0x73 读表：目标地址 = `(PC & 0xFF0000) | ((DPTR+A) & 0xFFFF)` → **dispatch 指令与整张表必须同 64K bank**；
   - J16：每个表项目标必须与该表项同 bank（LinkerCore.cpp:3335-3336）→ 各 case 块与表同 bank。
   - 合取 ⇒ **函数 + dispatch + 表 + 全部 case 块单 bank**。
2. **rev3 事实更正（Alice 二轮阻断②：原稿"跨 bank 越限不分割、由 lld 显式报错"缺实现依据，作废）**。现役 CODE 放置实读结论：
   - `Linker::layoutCode()`（LinkerCore.cpp:2303-2340）按 HOME/VECS/BOOT/CSEG/XINIT/XDATA_INIT 各一把顺序游标放置，`reserveCode`（:2291-2301）只查 24 位 `rangeFits` 与 overlap——**没有任何整 area / 整 section 单 bank 检查**；section 可自由跨 64K 边界（对照：XSEG 有整对象 64K 窗口帽与游标跳 bank 逻辑，:2806-2811/:2820-2835，CODE 侧没有）。
   - 现役逐项 J16 检查（:3333-3336）只证明"每个 fixup 场与其目标同 bank"（表项目标↔表项自身、表基址↔dispatch），**不能直接读出"整表 [base, base+3N−1] 同 dispatch bank"**——原稿以它为放置依据不成立。
   - DESIGN.md:1012-1023 冻结措辞（不跨 bank + 起点偏移满足范围 + 超限报错不截断）对 CODE/CSEG 目前**无对应实现**。
3. **整表跨度检查归属（rev3 定稿；三道，L1+L2 为主闸）**：
   - **L1 编译期跨度断言（S3 交付；发射期 fail-fast）**：`emitJumpTableInfo` 发射整表前按表长 `3×N ≤ 65536` 计算跨度并直接断言/拒绝（`report_fatal_error`）。E3 生效下 N ≤ 86 ⇒ 表长 258 字节恒过——该断言是**防御性硬闸**：任何未来放宽 E3 的改动一旦使表长越 64K bank 容量，编译期直接拒绝，不得静默遗留到链接期。
   - **L2 链接期显式跨度检查（S4 交付；几何终判）**：表列最终绝对地址只在 lld 放置后存在，编译期不可知，故"起点偏移满足范围"（DESIGN.md:1022）的终判必须由 lld 实现——**S4 新增检查（现役没有）**：放置完成后对 jt 表列符号（专用列，`size = 3×N` 已知）要求 `(Base ^ (Base + 3×N − 1)) & 0xFF0000 == 0`（整列不跨 64K 边界）；"表列与 dispatch 同 bank"由 `mov dptr,#jt` 的 J16（:3335-3336 同一机制）蕴含。越限显式报错，不静默截断/错跳。
   - **L3 现役逐项 J16（:3333-3336）仅兜底**：在发射契约（表列与函数体同 InputSection、表在函数体之后、逐项 J16 存在、section 原子放置）前提下，逐项检查可证蕴含整表同 bank——几何论证：base 的 J16 通过 ⇒ 表基址与 dispatch 同 bank ⇒ 函数体未在表前跨 bank ⇒ 全部 case 块与 dispatch 同 bank；此时若任一表项字节落入他 bank，该项 J16 场 P 与其目标 U 必异 bank ⇒ "J16 bank overflow" 必触发。注记：此为兜底性质证明，依赖三条未成文前提（section 原子性、发射次序、逐项 fixup 覆盖），**不作设计依据**（rev2 的错误正在于此）；主闸是 L1+L2。
4. **两道闸的分工（D3 裁定落地）**：
   - 编译期：结构谓词 E1–E5（§3.2.6）+ L1 跨度断言——可静态判定者，不合格**回退比较链**或**编译期拒绝**（L1）；
   - 链接期：L2 显式跨度检查（越限显式报错）+ L3 逐项 J16 兜底——**永不静默截断/错跳**。
5. **branch relaxation 交互**：函数因 relaxation 变大不破坏同 area 单元性；若 relaxation 导致 area 无法单 bank 容纳，落 L2 链接期闸。
6. **整表跨度负例设计（H5，rev3 细化）**：
   - **H5a lld lit（构造接近/超过 64K 边距的表场景）**：单一 CSEG section 含 dispatch+函数体+jt 专用列，经 `--area-start=CSEG=0x00FE00` 把 section 定位到 bank 顶部附近，用前置 padding（`.ds` 或占位函数）把表列基址摆出三分例：(i) `base + 3N − 1 ≤ 0xFFFF` 边内正例（链接过、判派可达）；(ii) 跨 0xFFFF 负例（S4 后：L2 显式报错；S4 前现状：L3 逐项 J16 亦必报错——兜底有效性实测入库）；(iii) `base + 3N − 1 = 0xFFFF` 恰压边界正例。限制注明：E3 生效下表长 ≤ 258 字节，跨窗口只能人为构造；自然触发需 ≥64K 函数+表列的超大 TU，不进常规 lit。
   - **H5b 编译期断言单元级（注明限制）**：L1 的 `3×N > 65536` 分支在 E3 生效下不可达（N ≤ 86）——以临时放宽 E3 的本地构建验证 `report_fatal_error` 行为一次、输出入库，并注明"防御性断言、非常规 lit"的限制。

#### 3.2.5 case 值 → 索引换算规则（规范级）

1. **密集 cluster（每个 JT cluster 独立）**：上游 visitJumpTableHeader 既有序（SelectionDAGBuilder.cpp:3037-3082）：
   `idx = zextOrTrunc(cond − Low)`（SUB 在 cond 位宽内进行，零/截断到 `getJumpTableRegTy` = i32 的 JT 寄存器）；
   随后 `SETUGT idx, (High − Low) → BRCOND → default`（出界走 default）。
   **负下界**：SUB 为带符号域内的常数减法（cond 为 i32 有符号语义），减后 idx ∈ [0, High−Low]（= [0, 表项数−1]，SETUGT 界常量 `JTH.Last − JTH.First`，SelectionDAGBuilder.cpp:3071）——上游机制对负下界天然正确，无需本后端特判。
2. **稀疏值域**：上游 findJumpTables 的 -O1+ 分片把 cluster 切成若干**稠密段 + 稀疏 case-cluster**：每个稠密段独立建表、独立 header（各减各自 Low）；稀疏段保持比较链。**"密集段建表、稀疏段走链"的混合派发是上游既有机制**，本设计不新增逻辑，只须保证链出口（§3.1）与表出口（§3.2.1）在各自 cluster 内语义正确。
3. **索引消费**：JT.Reg（i32）取低 8 位 subreg → GPR8 rI（谓词 E3 已保证最大索引 85 ≤ 255/3，8 位精确）→ §3.2.1 序列。
4. **索引宽度截断风险**（原稿风险条目）由谓词 E3/E5 封死：表项数（上游 Range = High−Low+1）> 86 的 cluster 不建表（回退链），不存在 9 位索引进入 ×3 序列的路径。

#### 3.2.6 编译期资格谓词（D3 定稿；opt-in 后逐 cluster 判定，任一不合格 → 该 cluster 回退比较链；rev3 修正 E3/E5 实现锚点）

| # | 谓词 | 判据（可实现锚点） |
|---|---|---|
| E1 | **使能位** | `-mllvm -mcs251-jump-tables` 开（cl::opt `MCS251JumpTables`，默认 false）。关 ⇒ 永远走链（D4） |
| E2 | **形态** | switch cond 为整数标量，原始 case 值位宽 ∈ {8,16,32}（legalize 前后均成立；i64/浮点/指针 cond ⇒ 不合格，避免 dispatch 引入 64 位 libcall 序列）。**落点（rev3）**：覆写 `isSuitableForJumpTable` 内经 `SI`（SwitchInst 参数）直接读 cond 位宽——该虚函数收 `const SwitchInst *`（TargetLoweringBase.cpp:1794-1799），是后端拿到原始 switch 的唯一资格门 |
| E3 | **值域宽度** | **rev3 语义更正：上游 Range = High−Low+1（表项数，含空洞填充项）**——getJumpTableRange 返回 `(HighCase − LowCase) + 1`（SwitchLoweringUtils.cpp:24-34），buildJumpTable 按 `(High−Low)+1` 逐项填表、空洞填 DefaultMBB（:218-228）。判据改写为 **`Range ≤ 86`**（表项数 ≤ 86 ⇒ 最大索引 85 ⇒ `3×85 = 255` 在 8 位 A 内精确，§3.2.1 依据 4）。（rev2 写 "Range = High−Low ≤ 85"，数值结论相同但语义与上游变量名不符，作废） |
| E4 | **case 数下限** | cluster case 数 N ≥ `getMinimumJumpTableEntries()`（上游默认 4；SwitchLoweringUtils.cpp:68-70 整表检查、:181 分片应用检查）。**rev3 注**：该检查独立于密度/硬帽、**不受 OptForSize 短路影响**（与 :1810-1812 不同位），且 getter 为虚（TargetLowering.h:2102）——可继续依赖上游机制 |
| E5 | **上界 + 密度（rev3：后端独立检查，不依赖上游硬帽）** | **MCS251 覆写虚函数 `TargetLowering::isSuitableForJumpTable`**（TargetLowering.h:1463 virtual），实现为 `Range ≤ 86 && (NumCases×100 ≥ Range×MinDensity)`，**不含 base 版的 `OptForSize ∥` 短路**。密度档位沿用上游默认 10 / optsize 40（TargetLoweringBase.cpp:82-91，`getMinimumJumpTableDensity(OptForSize)` 非虚可直接复用）——密度是启发式，允许随 optsize 变档；**正确性谓词 E2/E3 不允许任何 OptForSize 旁路**。原稿 "setMaximumJumpTableSize(85)、E3/E5 在上游机制内生效" **作废**，依据见下表 |
| E6 | **放置** | 表发射进函数自身 CSEG area（§3.2.4 单元性契约）；整表跨度检查 = §3.2.4 L1（编译期断言）+ L2（lld 显式跨度检查，S4 新增）+ L3（现役逐项 J16 仅兜底） |
| — | **循环项** | **循环内不作为否决条件**（谓词不含循环项）。理由：dispatch 为基本块内完整定值-使用链，无跨块/跨循环缓存的表指针状态（§3.2.2 条 1）；DPTR 覆写由 Defs 钉位排序约束，与循环结构无交互 |

**rev3 修正说明（Alice 二轮阻断①：E3/E5 不能依赖上游硬帽）**。上游机制核实（file:line 均实读）：

| 上游事实 | 锚点 | 对本设计的影响 |
|---|---|---|
| Range = High−Low+1（含空洞填充） | SwitchLoweringUtils.cpp:24-34、:83-89（整表调用）、:141-149（分片循环同谓词）、:218-228（空洞填 DefaultMBB） | E3 判据对象是**表项数** ≤ 86，不是 case 数 |
| 硬帽默认不存在：`max-jump-table-size` cl::init(**UINT_MAX**) | TargetLoweringBase.cpp:76-78 | 默认配置下上游对任意 Range 都可能建表（>86 项照建） |
| base 判据 = `(OptForSize ∥ Range ≤ MaxJumpTableSize) && NumCases×100 ≥ Range×MinDensity` | TargetLoweringBase.cpp:1804-1812（短路在 :1810） | **即便 setMaximumJumpTableSize(85)，optsize/minsize 下 `OptForSize=true` 整体短路硬帽**——大 Range 表照建，8 位索引不变式被破 |
| OptForSize 对 optsize/minsize 属性恒真 | MachineSizeOpts.cpp:36-44（`hasOptSize()` 先于 PSI/BFI 判定） | llc 直读 IR 只要在函数上带 optsize/minsize 即触发旁路 |
| `getMaximumJumpTableSize` **非虚**（setter 只改参数）；`isSuitableForJumpTable` **虚** | TargetLowering.h:2109（非虚）、:1463（虚） | 设参无法修复短路——唯一修复锚点 = 覆写 `isSuitableForJumpTable`，在覆写内去掉 `OptForSize ∥` |

实测确证（P9 系列，2026-09-15；switch100-dense：i32 cond，密集 case 0..99 ⇒ Range=表项数=100，密度 100%；以 `-mllvm -max-jump-table-size=10` 人为压低上游帽使"建表/回退"可观察——后端未改，crash br_jt = 建表、OK = 回退链；clang=-mcs251-s1，IR 直读故无前端变换干扰 -O0 判定）：

| 探针 | 属性 | 上游帽 | 结果 | 结论 |
|---|---|---|---|---|
| P9-A | 无 | 默认 UINT_MAX | -O0 **crash br_jt** | 默认无帽：Range=100 > 86 的表上游照建不误 |
| P9-B | 无 | 10 | -O0 **OK（链）** | 无 size 属性时硬帽有效：Range=100 > 10 被拒、回退链 |
| P9-C | **optsize** | 10 | -O0 **crash br_jt** | **同一 IR 只加 optsize 即绕过硬帽建表**——短路实测确证 |
| P9-D | **minsize** | 10 | -O0 **crash br_jt** | minsize 同样绕过（`hasOptSize()` 覆盖 MinSize） |

⇒ 资格检查必须是后端独立实现（E2/E3/E5 收拢进 `isSuitableForJumpTable` 覆写；E4 依赖上游非旁路检查；E1 是构造器使能位），上游 `max-jump-table-size` 只在探针中充当可观察旋钮，不进入实现。

### 3.3 案 b-2：双字节表——不做

属"16 位表访问路线"内但需单字节 CODE 地址片重定位（新增 R_MCS251_JTLO8/JTHI8 号 12/13，或挪用 LO8/HI8 DATA 通道语义）——两者都动冻结裁定。PM 裁定仅开案 a + b-1 两切片；**在 PM 明确开闸前 b-2 禁止实施**（此为边界陈述，非开放决策）。

### 3.4 案 c：比较链分片/pivot 质量（可选后续切片）

- 现状：-O1+ 的 case-cluster pivot 二分已存在，demo43 -O2 实测 5 cmp；-O0 恒线性。
- 仅在 S1 落地后作为独立质量切片评估（调 `getMinimumJumpTableEntries`/密度参数或 -O0 分片链）；不解决缺口本体，不与本设计绑定。

### 3.5 对比总表（rev2 更新；rev3 未改动维度结论，仅资格/几何闸落地归属细化）

| 维度 | S1 关闸(链) | S2+S3 b-1 ljmp 表（opt-in） | b-2 双字节表（禁做） | c 链优化（可选） |
|---|---|---|---|---|
| 解除 demo 43/62 阻塞 | 是（默认即解除） | 是（opt-in 尺寸优化） | — | 否 |
| 新增指令 | 无 | 0x73 JMPIAD + 0x90 MOVDPTRri | 0x73+0x93 | 无 |
| 新增重定位 | 无 | MC 加 fixup_mcs251_j16（ELF 复用号 7，号段不动） | 新号 12/13 或动 DATA 通道语义 | 无 |
| 与冻结裁定冲突 | 无 | 无 | 是 | 无 |
| 实现面 | 构造器 4 行（含开关分派） | ISel+AsmPrinter+MC+资格谓词 | 同左+lld | DAG 层参数 |
| 代码尺寸 | O(n) 链 | O(1) dispatch + 3N 表 | O(1) + 2N 表 | O(log n)@-O1+ |
| 回归风险 | 极低 | 中（dispatch/DPTR、bank 资格——均已有 §3.2.2/§3.2.4 规范与两道闸） | 中高 | 低 |
| QEMU 对拍既有支持 | 天然 | 0x73/0x90 均已模拟 | 同左 | 天然 |

## 4. 切片划分与独立验收（Alice 阻断④）

```
S1 案 a 关闸（切片一，独立入口/族/门槛）      [可独立落地，先做]
   ├─ 入口：MCS251ISelLowering 构造器 cl::opt 分派（§3.1.1，默认关闸分支）
   ├─ 测试族：llvm/test/CodeGen/MCS251/switch-chain-*.ll（§5 表一）+ §3.1.3 矩阵
   └─ 门槛：G1–G5（§4.1）
S2 案 b-1 ISA 原语（切片二之一）               [独立入口：-mllvm -mcs251-jump-tables；依赖 S1（链是回退出口）]
   ├─ 0x73 JMPIAD + 0x90 MOVDPTRri：def/编码/InstPrinter/CodeEmitter/错误文案
   └─ 产出：指令级 lit（汇编/编码/objdump 三方对齐；0x73=1 字节、0x90=90 hi lo）
S3 案 b-1 JT lowering + 表输出（切片二之二）   [与 S2 在"指令名冻结"前并行，落地前汇合]
   ├─ LowerBR_JT + emitJumpTableInfo 覆盖 + fixup_mcs251_j16（ELF 号 7 复用）
   ├─ 资格谓词 E1–E6 落地（§3.2.6；rev3：E2/E3/E5 = 覆写 isSuitableForJumpTable 的后端独立检查，
   │    不含 OptForSize 短路，不经上游 max-jump-table-size；含 §3.2.4 L1 编译期跨度断言）
   └─ 测试族：llvm/test/CodeGen/MCS251/switch-jt-*.ll（§5 表二）
   └─ 门槛：H1–H5（§4.2）
S4 lld 侧验收（随 S3 门槛 H5）                 [依赖 S3 表格式冻结；含 §3.2.4 L2 整表跨度检查（现役无，必增）]
S5 质量切片（案 c 参数评估）                   [依赖 S1；独立]
S6 文档（DESIGN.md 增补"switch 路线"小节 + 本稿转正）[随 S1，滚动更新]
```

必须串行的边界：S1 → {S2,S3}（回退出口必须先存在）；S3 表格式冻结 → H5。可并行：S2 ∥ S3（以 mnemonic/编码一次评审冻结为汇合点）。

### 4.1 S1 独立验收（案 a 门槛，全过才收）

- **G1（回归绿）**：§3.1.2 六形态 × {-O0,-O2} 在实施后保持编译通过（当前基线已绿，不得变红）；
- **G2（缺口转绿）**：§3.1.3 矩阵全部超阈值形态（含 demo43/62 全文件、llc 直读 switch4/switch16 IR）实施后 -O0/-O2 编译通过且落链——实施前红、实施后绿，逐项入库；
- **G3（链语义等价）**：对 §3.1.2+§3.1.3 全部形态做 lit FileCheck pin：每 case 值恰一次比较命中、全不中落 default、负下界常量为 32 位 mov+movh 形态、cmp 常量与 ejmp 目标逐一 pin（以 R1 金样为 golden）；
- **G4（不破既有）**：llvm/test/CodeGen/MCS251 149、lld 22、clang mcs251 28 全过不回退（基线 P8）；
- **G5（BRIND 行为入库）**：llc 直读 indirectbr.ll 在 BRIND=Expand 下的实际行为实测记录（R3 关闭条件）；`-fno-jump-tables` 属性语义 pin 不变。

### 4.2 S2+S3 独立验收（案 b-1 门槛，全过才收）

- **H1（开态 golden 字节）**：flag 开时对 dense n≤86 用例逐字节 pin：JMPIAD 编码 `0x73`、`mov dptr,#jt` 编码 `90 hi lo`、每表项 `02 (tgt)>>8 (tgt)`、J16 字段在项内偏移 +1、项距 3 字节、obj 层 J16 大端；
- **H2（开态语义）**：QEMU 对拍——同一 IR 在 flag off（链版）与 flag on（表版）下判派结果一致（0x73/0x90 均已模拟；判派序列含命中每 case + 出界落 default + 负下界）；
- **H3（关态回退零扰动）**：flag 关（默认）时，§5 全部测试族的 .s 与 S1 后基线**逐字节一致**，0x73/0x90/ljmp 表零出现——默认策略（D4）的可执行验证；
- **H4（资格谓词负例）**：N < 4、表项数（Range = High−Low+1）> 86、密度 < 10%（optsize < 40%）、i64 cond 各自回退比较链（无表、无 0x73/0x90、无 fixup_mcs251_j16）；**rev3 增两项（阻断①矩阵条目）**：**optsize 函数属性 + Range > 86**、**minsize 函数属性 + Range > 86** 各自回退比较链——上游 base 判据在 size 属性下短路硬帽（TargetLoweringBase.cpp:1810，P9-C/P9-D 实测建表），覆写后的独立检查必须不随属性放行；对照正例：optsize + 密集 Range ≤ 86 仍建表（密度 40 档生效，证明覆写只删短路、不误杀 optsize 路径）；
- **H5（几何闸，rev3 按 §3.2.4 三道归属重写）**：
  - H5-L1：编译期跨度断言行为入库（H5b 单元级，注明 E3 下不可达、防御性）；
  - H5-L2：lld 整表跨度检查（S4 新增）按 H5a 三分例验收——边内正例过 / 跨 0xFFFF 负例显式报错（报错文案含表列符号）/ 恰压边界正例过；
  - H5-L3：现役逐项 J16 兜底不回归——现状（S4 前）跨窗口用例即触发 `"J16 bank overflow"`（:3335-3336），实测入库；HOME J16 校验（LinkerCore.cpp:3059-3064）与既有 lld 22 测试不回归。

## 5. 测试矩阵（按切片分族；每正例同跑 llc -O0 与 llc -O2）

表一：S1 族（llvm/test/CodeGen/MCS251/，链出口）

| 文件 | 覆盖 |
|---|---|
| switch-chain-dense3.ll | §3.1.2 六金样入库基线（N=3 六形态 -O0/-O2；FileCheck pin cmp 常量与 ejmp 目标） |
| switch-chain-matrix.ll | §3.1.3 超阈值矩阵（4/8/16 case × i8/i16/i32；-O0 线性 / -O2 pivot 上界 pin） |
| switch-chain-negbounds.ll | 负/非零下界（32 位常量形态 + eq 链语义 pin） |
| switch-chain-defaults.ll | default 空/非空两变体（判派结构同构 pin） |
| switch-char-sparse.ll | 稀疏 char case 走链 |
| switch-neg-attribute.ll | `-fno-jump-tables` 属性 → 强制链（属性门语义 pin，与目标门取交集） |
| indirectbr-expand.ll | BRIND=Expand 后 llc 行为 pin（G5） |

表二：S2+S3 族（全部在 `-mllvm -mcs251-jump-tables` 下；另各配一份无 flag 的 H3 对照 RUN 行）

| 文件 | 覆盖 |
|---|---|
| switch-jt-ljmp-table.ll | 密集 i32：jt 表（`02 hi lo` 逐项）+ `mov dptr,#jt; jmp @a+dptr` dispatch（H1 golden 字节） |
| switch-jt-header.ll | 非零/负下界：header 减法 + SETUGT 上界 BRCOND 形状 pin；出界→default |
| switch-jt-mixed.ll | 密集段建表 + 稀疏段走链混合派发（§3.2.5 条 2） |
| switch-jt-isa.ll | 0x73/0x90 指令级 lit（汇编/编码/objdump 三方） |
| switch-jt-pred-neg.ll | H4 负例六件（N<4 / 表项数>86 / 密度 / i64 cond / **optsize+Range>86 / minsize+Range>86**）→ 回退链；附 optsize+密集≤86 建表对照（H4 rev3 增项） |
| switch-jt-crossbank-neg.ll + lld | H5a 三分例（边内过 / 跨 0xFFFF 显式报错 / 恰压边界过，`--area-start=CSEG` 定位，§3.2.4 条 6）+ L2 跨度检查（S4 新增）+ L3 逐项 J16 现状报错实测入库；HOME 校验不回归 |

golden/端到端（validation/ 既有管线）：

| 项 | 命令骨架 |
|---|---|
| demo43/62 -O0 与 -O2 编译 | clang --target=mcs251 -c/-S（探针记录 P2 命令，加 `-mllvm -mcs251-object-format=elf`）→ S1 后无崩溃；S3 后再对拍链/表两版 .s |
| QEMU 对拍（H2） | mcs251-lld + crt.o + qemu-system-mcs251：链版/表版判派输出一致 |
| llc 直读 IR | switch4-u8-O0.ll 于 llc -O0/-O2 均不崩（G2） |

opt 管线：每正例同时跑 llc -O0 与 llc -O2 两档（本缺口最初即由"两档都崩"定义），新增 IR 不得引入 LLVM IR 层变换差异（管线为既存 DAG 管线，无新 pass）。

## 6. 回归预核（实施必须不破的族 + 当前基线）

实测于 2026-09-14（命令与输出见探针记录 P8）：

| 族 | 基线 |
|---|---|
| llvm/test/CodeGen/MCS251 全量（150 文件，149 lit tests） | **149/149 过**（/home/liu/build-mcs251，lit -q 退出码 0） |
| lld/test/MCS251（23 文件，22 lit tests） | **22/22 过**（/home/liu/build-mcs251-lld） |
| clang/test/CodeGen mcs251 子集（66 文件中 28 lit tests） | **28/28 过**（/home/liu/build-mcs251-s1，--filter mcs251） |
| clang/test/CodeGen 通用本底 | 6151 tests / 522 既有失败（8.49%，与 MCS251 无关的环境本底；实施验收只要求 mcs251 子集不回退） |

敏感面提示：S1 的 cl::opt 分派位于共享构造器，理论上只影响 BR_JT/BRIND 资格——现有 149 测试无 switch 用例（grep 证实仅 xdata-placement.ll/xdata-runtime-banks.ll 注释含 "switch" 字样），预期零扰动；S3 触碰 AsmPrinter/MC 时以 `bit-object.ll`、`elf-oseg.ll`、`asxxxx-obj.ll` 三族为对象格式回归哨兵。

## 7. 风险

- R1（案 a）：-O0 大 switch 尺寸线性膨胀。缓解：案 b-1 opt-in 后 O(1) dispatch；demo 真实子集 4 case，实测可控；S5 评估 -O0 分片。
- R2（案 b-1）：dispatch 覆写 dpl/dph 与 DPTR 生命期冲突。rev2 缓解升级为规范：§3.2.2 六条（caller-saved 事实、无跨调用义务、Defs/Uses 钉位、A/B 不分配、DPXL 无交互、PSW 无跨 dispatch 依赖）+ -verify-machineinstrs + H2 QEMU 对拍。
- R3（案 a/案 b 共同）：BRIND Expand 后 indirectbr 的 Legalizer 行为未实测——S1-G5 强制入库，不由本稿断言。clang 层 computed goto 目前先被 A4 v1 identity 拒绝，llc 直读层是唯一暴露面。
- R4（案 b-2）：动 DATA 通道语义或新号段——PM 未开闸，禁止（§3.3 边界陈述）。
- R5（-O0 也建 JT）：由 D4 裁定消解——默认关闸（资格位 Expand）下任何优化档都不建表；opt-in 后 -O0 建表属预期行为（E1 使能位是显式动作）。

## 8. 已裁定事项（无开放决策）

- D1（已裁定）：案 a + 案 b-1 两切片都做（§3.0.1）。
- D2（已裁定，否）：b-2 不做；未开闸前禁止（§3.3）。
- D3（已裁定）：编译期资格检查（谓词 E1–E6）+ 不合格回退比较链；lld 仅几何兜底、越限显式报错（§3.2.4/§3.2.6）。（rev3 实现锚点细化：资格检查 = 后端覆写 isSuitableForJumpTable 的独立谓词，不经上游 max-jump-table-size；几何闸 = §3.2.4 L1 编译期断言 + L2 lld 显式跨度检查（S4 必增，现役无）+ L3 逐项 J16 兜底——裁定内容不变，仅落地归属写明）
- D4（已裁定）：默认关闸；`-mllvm -mcs251-jump-tables` 显式 opt-in（§3.0.3、§3.1.1）。
- D5（附带发现，不属本缺口，无待决事项于本稿）：默认 REL 输出下 demo 43/62 全文件编译先撞 "v2 object identity … requires ELF"（A4 白名单）；demo 完整改写验收依赖 REL/ELF 输出策略，属另一缺口，另行处置。

## 9. 与已冻结裁定的边界核对

- 不动 ELFRelocs/MCS251.def 号段（0..11 冻结）：案 b-1 的新增仅是 MC 层 fixup kind（fixup_mcs251_j16），ELF 重定位复用现役 R_MCS251_J16=7。
- 案 b-1 表项形状 = HOME 3 字节 ljmp + J16@offset1 的同构复用（LinkerCore.cpp:3059-3064 校验逻辑不变，仅新增产生方）。
- 遵守 DESIGN.md:1012-1023"单表必须单列"：本设计只报 16 位表路线；24 位表路线（R_MCS251_24 表项 + 跨 bank）明确不在本稿范围（0x73 语义为 PC-bank 16 位，24 位表项喂 0x73 会静默错 bank，禁止）。
- §3.2.1 定稿序列与 SDCC gold（gen.c:12313-12455）同构：加法交给 0x73 硬件 16 位求和，软件零进位处理。
