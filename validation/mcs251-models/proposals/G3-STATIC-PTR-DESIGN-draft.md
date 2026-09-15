# G3 设计稿草案：static-ptr ABI 残余缺口——契约迁移与验收收口（demo 41/45.1/45.2/49/61）

- 日期：2026-09-15（调查日，同日二次复核；同日 Alice 设计评审修订落实）；分支 `minimal-isr`，HEAD `f362d88c7`。
- **修订记录（Alice 设计评审，2026-09-15）**：§5-S2 结构性阻断补设计为 §5.1 程序分组机制；§3.1 类型
  记法纠错（`ptr@N` → 第 N 参 ptr (AS0)，%N 是参数位编号非地址空间）；§4(a) 引用与 E2 表述修正；
  §7 决策点重分类；§2.1/§3.2/§6.2 变参观测口径改"旧二进制历史观测"；§8 基线按跨族拼单标签重登记。
- 本文为**只读调查产物**：所有 file:line 断言均在盘上源码逐一核验；所有"实测"均在 `/tmp` 复现
  （clang=`/home/liu/build-mcs251-s1/bin`、llc=`/home/liu/build-mcs251/bin`、lld=`/home/liu/build-mcs251-lld/bin`，
  含 A4 W1-W8、P-4、G1、G2/B-S1、br_jt、AS4 聚合初始化）。未改动仓库任何文件；本文是唯一入仓产物（未跟踪）。
- **并发役漂移注记（二次复核发现）**：本役进行中，G2 B-S2 实施役在同一工作树落下了**未提交**的变参实现
  （`git diff` `MCS251ISelLowering.cpp` +155/-12 及配套 .td/AsmPrinter 改动，源码 mtime 04:04 晚于全部三个探针
  二进制：llc 03:34 / clang 03:39 / lld 03:04）。因此：§2.1 的 `MCS251ISelLowering.cpp` 行号以 2026-09-15
  上午盘上工作树为准（静态槽判定点本体未被 B-S2 触碰，仅行号整体后移约 +45 行）；**符号锚点（函数名）优先**，
  行号随并发役继续漂移时以符号检索为准。探针结论均出自当前二进制（变参行为见 §2.1 末与 §3.2 注）。
  lld 侧文件无未提交改动，行号稳定。
- 五 demo 语料（`/home/liu/LLVM_STC32/mcs251-demos-rewritten/`，只读）。

## 0. 结论（一句话）

**"static-ptr ABI"的编译器通路已由 A4 全部建成并获 PM 批准（v2 契约 + CallABIMinor=1），五 demo 仍卡住的直接机械原因只是验收 harness 把合同钉死在已 deprecated 的 v1 兼容契约 `1,1,32,8,1`（`tools/drive.py:55`）；G3 的剩余工作量是零产品源码的 harness/语料/记账切片，其中 demo 61 在迁移+S2 程序分组机制（§5.1）后 T0/T1 可通（QEMU 档尚待验收，不预证全绿），其余四个 demo 迁移后还会暴露已被 A4 §11.2 预登记的 G2 变参次级缺口（41 另受 CODE/DSEG 窗口预算约束；变参画像系旧二进制历史观测，当前构建已含 B-S2 变参实现，验收期望须刷新，见 §3.2 注）。**

## 1. 问题与登记口径

- G1-4 记账（`/home/liu/LLVM_STC32/G1-4-PROGRESS.md` §5 表 + 215-218 行分布）把 T1 残余 gap 归因为
  variadic ×15、static-ptr ABI ×13、bit addr ×3、f32 ×2、addr-space ×2、DSEG window ×2，合计 37
  （=T0 gap 7 + T1 gap 30）。本役五 demo（41、45.1、45.2、49、61）是 static-ptr ×13 中 A4 尚未随役闭合的部分
  （19/21/25/26/40 已由 A4/W7 四档矩阵闭合；43/62 见 §3.4 旁证）。
- 历史口径注意：`README.md` §8 的 G3 行（7 demo：19,21,25,26,40,47,62）是 G1 之前的旧审计行；
  现行有效口径是 G1-4 的 13-demo static-ptr 桶。本文沿用 G1-4 口径。
- A4 正式设计（`A4-V2-OBJECT-IDENTITY-DESIGN.md`，状态"已实施"）已把"41/42/43+G3 随 A4 迁契约
  `1,1,32,8,1 → 1,2,32,8,1`"列入 PM 裁定 #1（2026-09-13，"暂时按照推荐设计"）；PM 裁定 #2 把 v1
  标记为 deprecated、默认路径全 v2、ELF 身份=契约代。**因此本设计稿不是提议新 ABI 能力，而是落实
  已裁定的契约迁移并收口其验收。**

## 2. 现状精确定位（当前 HEAD 实测，全部复核）

### 2.1 拒绝层与文案

- **拒绝在 llc ISel 层，clang 无 Sema 门槛。** 实测：v1 合同下 `clang -Xclang -mcs251-memory-contract=1,1,32,8,1
  -S -emit-llvm` 对含第二参指针的 C 编译通过（exit=0）；`llc -mcs251-memory-contract=1,1,32,8,1` 报
  `LLVM ERROR: MCS251: static pointer parameters are not supported by the compatibility ABI`
  （asm 与 obj 同拒——检查在 ISel，先于输出格式）。
- 判定点（`llvm/lib/Target/MCS251/MCS251ISelLowering.cpp`，2026-09-15 上午盘上行号，见卷首漂移注记）：
  - `checkParameterType()`（:3464-3481）：`if (Index && !AllowStaticPointers) report_fatal_error(...)` 在
    **:3470-3472**；覆盖形参（`LowerFormalArguments` :3618-3620）与调用点实参（`LowerCall` :3815-3825 经
    `CLI.CB->getArgOperand`）。
  - `checkParameter()`（:3484-3496）：legalized 片段侧同一文案在 **:3494-3496**。
  - 开关本体：`AllowStaticPointers = DAG.getDataLayout().getProgramAddressSpace() == 4`（:3618 形参侧、
    :3815 调用侧）——**只有 v2 契约（ProgramAS=4）放行**；首参寄存器通道不受影响；AS 白名单
    `hasOrdinaryPointerABI`（:3446-3461，AS 0/1/2/3/4/8/9）先于此检查，AS5/6/7/>9 仍三层 fail-closed。
- clang 侧 grep 全库无 "static pointer" 诊断（仅上游无关文件命中）；**静态指针参数从未有前端门槛**，
  v1 拒绝按设计就是后端 fail-closed。
- 邻近冻结钉子（G2 B-S2 的在册摘除面，不是 G3 的；**该役未提交实现已在本工作树**，行号与形态均已变）：
  - 变参定义：B-S2 前为无条件拒（HEAD 提交态）；现盘上源码改为形状诊断"必须至少一个命名参数"
    （:3584-3595），≥1 命名参的变参定义已走静态延续槽 ABI。
  - 变参调用：B-S2 前的无条件拒（HEAD 态）在盘上源码 :3738 处注明 **REMOVED**，替换为形状门禁
    （:3783 间接调用形态拒、:3794 变参实参 >6 拒）。
  - `CanLowerReturn`（:3971-3996）：普通变参返回拒已摘（:3988-3996 注明）；ISR×变参拒**冻结保留**
    （:3978-3984）。
- **变参观测口径（Alice 修订）**：`LLVM ERROR: minimal MCS251 backend does not support variadic
  functions`（二次复核实测，exit 134）系**旧二进制历史观测**（03:34 探针构建，不含 B-S2）；**当前构建
  已含 B-S2 变参实现，验收期望须刷新**——S1 全批重跑须用含 B-S2 的重建二进制逐 demo 重取变参层真实
  文案，不得沿用旧二进制的无条件拒文案作验收期望（见 §3.2 注、§6.2）。

### 2.2 A4/P-4 注册面与实现的差距：无

任务书要求核对"CallABIMinor=1、Tag24 placement 分量豁免等与当前实现的差距"——**已无差距，全部落地**：

- A4 W1-W8 提交链：`5ab4f855f`（W1 开放值登记）、`bb5a9900b`（W2+W3+W3b 发射与门禁分流）、
  `6c6a4dc5a`（W4 lld v2 身份分支）、`3687904b5`（W5 readobj）、`3021e507b`（W7+W8 v2 CRT 与验收链）。
  PM 裁定 #1 冻结 **CallABIMinor=1 为"指针静态槽"的唯一表达**（`A4-V2-OBJECT-IDENTITY-DESIGN.md` §2.1、
  文末裁定记录）；裁定 #2（2026-09-13）**v1 deprecated、身份=契约代、默认全 v2**。
- P-4 签名 Tag 28 已冻结并同批实施（`P4-SIGNATURE-PROTOCOL-FREEZE.md`，2026-09-14；lld 三级时机 +
  跨对象逐字段核对）。注意其对 G3 的两个含义：
  1. 签名保证**仅覆盖 v2 对象**（v1 无载体不参与核对）——这使"v1 合同解禁静态指针"方案（§4a）额外
     失去签名一致性防线；
  2. 每条记录含 `call_abi_major/minor` 并与对象身份核对——静态槽函数随 clang 的 `!mcs251.signatures`
     自动进 Tag 28，无手写登记负担（实测：探针对象经 lld 链接即过校验）。
- Tag 24 placement 分量豁免：lld `V2ComparedTags`（`lld/MCS251/LinkerCore.cpp:843-856`）+ 注释
  :1671-1673（Tag 13 与 Tag 24 的 placement 分量允许跨对象不同，只要各对象自身约束可同时满足）——已实现。
- E2 静态槽重入诊断已就位（A4 §6.1 风险表的"当年禁它的原因"的现役对策）：`LinkerCore.cpp:3681-3712`
  （ISR/前台上下文沿直接调用边不动点传播，双上下文可达或 ≥2 ISR 可达即告警，逐定义对象列出参数槽）、
  :4061 起文案；`Driver.cpp:366-372`：`--isr-reentrancy` 默认开、`--no-isr-reentrancy` 显式退出；
  仅告警不影响链接。覆盖边界（间接调用不可见等）随注释公布。
- DSEG 槽窗口：`LinkerCore.cpp:2644-2656`，DSEG 区上限硬编码 128 字节直达页（`--iram-size`
  `Driver.cpp:390-394` 只扩 ISEG，不能扩 DSEG）。这是 §3.3 demo 41 溢出的机制位（G8 族，非 G3 本体）。

### 2.3 最小探针矩阵（`/tmp/g3probe`，全部实测）

探针源：第二参指针 + `int,ptr,int,ptr` 混排 + 非叶调用方。

| # | 链路 | 合同 | 结果 |
|---|---|---|---|
| P1 | clang→llc `-filetype=asm` | 1,1,32,8,1 | **clang 过（无前端门槛）；llc 拒**，文案 §2.1 |
| P2 | clang→llc asm | 1,2,32,8,1 | 过；`_fill_PARM_2: .ds 4`、`_mix_PARM_2/3/4`（4/1/4B 混排）、大端写槽+canonicalize |
| P3 | clang（无 contract flag，默认 xsmall/v2）→llc（无 flag） | 默认 | 过；DataLayout 含 `P4`/`p3:32` 等 v2 串；**默认合同即 v2** |
| P4 | P3→llc `-mcs251-object-format=elf -filetype=obj` | 默认 | 过；`e_flags=0x102`、恰一 `.mcs251.attributes`、叶函数槽落 `.mcs251.OSEG.*`（SHF_MCS251_OVERLAY） |
| P5 | P4 对象 + `crt-selfstart-v2.o` → mcs251-lld | v2 | 链接机制通（探针对象无 `_main`，undefined 为预期） |
| P6 | v2 对象 + v1 `crt.o` | 混 | **拒**：`cannot link a v2 identity object (.mcs251.attributes) with a v1 identity object (..., .note.mcs251.abi)` |
| P7 | demo61 LCM_Test.o（含第 5 参（%4 参数位）ptr 槽 + ISR 58/59）+ `crt-irq-v2.o` | v2 | **链接通**；map 见 `.mcs251.DSEG.*` 槽区分配 |
| P8 | P7 对象再链除法运行时（v2 重建 `mcs251rt_divuint.o`） | v2 | 通——运行时/nop-helper 按 v2 重建后全链闭合 |
| P9 | demo41 gui/LCD/main/sys 四文件 → llc obj | 1,2,32,8,1 | **四文件全过**（gui.o 含 `_swap(ptr,ptr)`、`Draw_Triangel` PARM_2..5 五槽） |

推论：**静态槽 lowering/发射/身份/链接/P-4 全链在当前二进制上可用；v1 合同按冻结设计继续拒绝。**

## 3. 五 demo 需求画像（IR 级，v2 合同逐文件实测）

### 3.1 静态指针签名形态全集

| demo | 函数（IR 实名，形参为真实 IR 类型） | 形态 | 槽 |
|---|---|---|---|
| 41 | `LCD_ReadReg(i32, ptr, i32)` | 第 2 参指针（%1 参数位的 ptr (AS0)） | 4B AS0 |
| 41 | `_swap(ptr, ptr)` | **双指针**（%0/%1 参数位均 ptr (AS0)；首参寄存器+次参槽） | 4B |
| 41 | `LCD_ShowString(i32,i32,i8, ptr, i8)` | 第 4 参（%3 参数位的 ptr (AS0)） | 4B |
| 41 | `GUI_DrawFont16/24/32(..., ptr, i8)` | 第 5 参 ×3（%4 参数位的 ptr (AS0)） | 4B |
| 41 | `Show_Str`/`Gui_StrCenter(..., ptr, i8, i8)` | 第 5 参 ×2（%4 参数位的 ptr (AS0)） | 4B |
| 41 | `Gui_Drawbmp16(i32,i32, ptr)` | 第 3 参（%2 参数位的 ptr (AS0)；code 图像指针经 A3 cast 通路入泛型槽） | 4B |
| 45.1/45.2/49 | `LinSendMsg(u8, ptr)`（`u8 *pdat`，实参 `TX1_BUF` 泛型 AS0） | 第 2 参（%1 参数位的 ptr (AS0)） | 4B |
| 61 | `Show_Str(i16,i16,i16,i16, ptr, i8, i8)` | 第 5 参（%4 参数位的 ptr (AS0)） | 4B |

- **记法说明（Alice 修订）**：五 demo 形参 IR 实测（本日抽查复核：61 `Show_Str`、41 `_swap`/
  `Gui_Drawbmp16`、45.1 `LinSendMsg` 等）均为 `ptr`——AS0 泛型指针、槽宽 4B；`%N` 是 IR 参数位编号
  （`define` 行的 `%0..%k`），**不是地址空间**。此前稿面的 `ptr@1/@2/@4` 记法把参数编号误读为地址空间，
  已全文废止。函数自身的 `addrspace(4)`（v2 ProgramAS）是函数地址空间，与形参指针的地址空间无关。

- 全部形态 ∈ A4 已实现并钉测的 P1-P6 集（第 2/3/4/5 参、AS0 4B、混排、双指针、非叶 DSEG、叶 OSEG）；
  **无 AS1/2/8 短槽、无 AS3/AS4 直接形参槽、无函数指针间接调用（多参间接调用禁令不触及）、无 f32/struct/i64 参数。**
- 是否经 ISR：41/61 用 ISR（槽 59/58，crt-irq 路线；G1-4 已做 41 的 13→59 重映射）；45.x/49 用 ISR（30/31）
  但 `LinSendMsg` 仅前台调用（main.c:180 在 main 内）——E2 告警预期不触发；41/61 由 E2 默认诊断兜底（仅告警）。

### 3.2 v2 迁移后的逐文件实测结果与残余缺口

| demo | v2 llc 结果（逐文件，旧二进制历史观测） | T1 前景 | G3 之外的残余（实测定位） |
|---|---|---|---|
| **61** | 两文件全过 | 分组后 T0/T1 可通（QEMU 档尚待验收，链接成功不预证全绿） | 语料双 `main`+双 `delay_ms`：lld `duplicate definition of _delay_ms`（两个子程序目录各自成程序）；S2 程序分组机制隔离并逐程序驱动（§5.1） |
| 41 | gui/LCD/main/sys 过；**test.c 拒** | 差一步 | test.c `sprintf(ptr,ptr,...)` 变参**调用**拒——**旧二进制历史观测（当前构建已含 B-S2 变参实现，验收期望须刷新）**；另有 CODE/DSEG 窗口（§3.3） |
| 45.1/45.2/49 | **main.c 拒**（栈回溯指向 `@main` 内 printf 变参调用；旧二进制历史观测） | 差一步 | 同上 G2 变参调用层（`printf(const char*, ...)` 声明+调用；IR `call ... @printf(ptr, ...)` 实证，二次复核 45.1/49/45.2 同文案）——同须以含 B-S2 的重建二进制刷新验收期望 |

**注（B-S2 口径，Alice 修订）**：本表的"拒"均为**旧二进制历史观测**（03:34 探针构建，不含 B-S2）；
**当前构建已含 B-S2 变参实现，验收期望须刷新**。这不改变本稿结论：41/45.x/49 的收口同样需要 v2 链
（G3-S1 先行对 G2 是赋能），且 41 另有窗口预算题（§3.3）；变参层的最终验收文案以重建二进制的 S1
重跑实测为准。

**记账修正（重要）**：G1-4 把 41/45.1/45.2/49 记为纯 G3——这是**首错遮蔽**：v1 合同下 llc 先死在静态槽点，
变参层从未到达。v2 探针揭示四者均携带 G2 次级缺口（与 A4 §11.2 对 41 test.c 的预登记一致，本役把 45.1/45.2/49
补进同一登记）。61 是五者中唯一 G3-pure demo。

### 3.3 demo 41 的两个窗口预算缺口（非 ABI，登记+PM 决策）

- **CODE 窗口**：gui.o `.text` 32,844B + LCD.o 7,650B + main/sys ≈ **41.0KB**，而冻结配方 CSEG 窗口
  仅 30,976B（IRQ，0xff0700..0xff8000）/ 32,256B（非 IRQ）→ lld `CODE overlap for .mcs251.xinit`。
  实测把 `--area-start=XINIT=0xfff000 --area-start=XDATA_INIT=0xfff100` 后链接推进到下一个错误
  （DSEG），说明**窗口外移在 lld 侧可行**，但需 QEMU 机型/flash-gate 验证（PM 决策，G8 族预算题）。
  demo 43（旁证）同病（字体大）。
- **DSEG 直达页**：gui.o 非叶槽区 DSEG.0..11 共 **157B** + LCD.o 36B + crt/REG_BANK/BSEG 既有占用，
  窗口 128B，失败时最大空洞 12B → `cannot allocate .mcs251.DSEG.7 ... no free range of 20 bytes`。
  `--iram-size` 不能扩 DSEG（§2.2）。根治需"非叶槽区迁 EDATA"类独立切片（G8 族），G3 不做。

### 3.4 同机制旁证（同一迁移顺带受益，不属本役验收）

demo 43/62（A4 §11.2 登记的 br_jt 与 AS4 聚合初始化缺口已由 `f362d88c7`、`03f9f62eb9` 修复）：
**v2 合同下全部文件 llc obj 过**；62 单 main 双 TU **链接通**；43 同 41 的 CODE 窗口题。即静态槽桶
13 个 demo 的编译层已全部就位，差别只在验收链与各自的次级缺口。

## 4. 方案对比与推荐

三个候选（发射/接收序列均为既有实现，列出以对照"各自要改什么"）：

### (a) v1 兼容合同解禁静态指针 — **否决（冻结面禁止）**

- 要改什么：放开 :3425-3427/:3449-3451 的 `AllowStaticPointers` 条件（或 v1 亦置真）→ v1 对象携带槽。
- 为什么不行：
  1. **明令禁止**：DESIGN.md D.5.1（A4 稿转录："不修改 v1 ABI……不把 `AllowStaticPointers` 在 v1 无条件置真"）；
     `A4-STATIC-PTR-PARAMS-DESIGN.md` §3.7（"v1 行为完全不变"钉死清单 → 回归测试项）同旨。
  2. **PM 裁定 #2 已把 v1 deprecated**：仅显式 v1 合同供既有 29-demo 资产重建；给 v1 加能力与
     "过时"裁定直接冲突。
  3. **v1 无签名防线**：P-4 裁定 #3 收窄——Tag 28 仅 v2 载体；v1 静态槽对象失去跨对象签名核对，
     而 `1,1,32,8,1` 与 `1,2,32,8,1` 的 DataLayout 指针布局不同，混链/错代风险扩大。
  4. 当年禁它的实质理由（静态槽跨上下文重入破坏）并未消失；且 v1 身份无 attributes 载体可表达
     CallABIMinor=1，(a) 失去身份代际组合面。（E2 诊断的执行条件是 IRQ 模式及诊断开关，非 v2 专属；
     此前稿面"(a) 绕开 v2 的 E2 诊断"表述有误，已删。）
- 收益：零（demo 链照样要改 harness 才能吃到）。风险：v1 黄金字节/29-demo 资产回归 + 语义混链。

### (b) 默认 v2 契约迁移（A4 本意，PM 已批）— **推荐**

- 要改什么：**零产品源码**。harness 把五 demo（及全批）从 `1,1,32,8,1` 迁 `1,2,32,8,1`，CRT 换 v2 变体，
  运行时/nop-helper 缓存按新合同重建。
- 发射/接收序列（既有，实测 §2.3-P2/P4）：调用方在 CALLSEQ 内先串行写全部槽（4B 大端、最高字节
  canonicalize 清零）再置首参寄存器 → `ecall`；被调方入口先于任何调用读槽并规范化；叶函数槽
  `.mcs251.OSEG.<n>`（OVR/NOBITS/OVERLAY），非叶 `.mcs251.DSEG.<n>`；重定位沿用
  R_MCS251_MID8/LO8/HI8（无新 reloc 号）。
- P-4 签名记录（既有自动化）：clang 从 CGFunctionInfo 产 `!mcs251.signatures`（含每函数
  `call_abi=2,1`、param_count、bitmap、role），llc 转 Tag 28 随 v2 身份发射；lld 三级核对（对象内关联
  + 跨对象逐字段 + `call_abi` 与 e_flags/attributes 一致）。**静态槽函数无任何手工登记**（P7/P8 实测）。
- lld 侧影响（既有）：e_flags 0x102 分流、`.mcs251.attributes` 逐 Tag 校验、V2ComparedTags 逐字段
  （Tag13/Tag24-placement 豁免在册）、v1/v2 双向混链拒、OSEG overlay 复用与 DSEG 窗口分配零改动；
  E2 重入告警默认开。
- 风险：见 §7（主要是缓存陈旧与全批重跑的基线刷新，均为机械性且失败模式响亮）。
- 与 G2 的顺序：G2 B-S2+ 未落地前 41/45.x/49 的 T1 仍 gap（次级变参，按旧二进制历史观测），**61 两程序
  均无变参，不受该层影响；四档以 S1/S2 后实测为准（QEMU 档尚待验收，链接成功不预证全绿）**；
  反之 G2 的 e2e 验收本身也需要 v2 链——S1 先行对 G2 是赋能不是依赖倒挂。

### (c) 只解五 demo 实际形态的最小子集 — **与 (b) 重合，无独立价值**

五 demo 全部形态 ∈ A4 P1-P6 已实现集（§3.1），"最小子集"不需要任何新 lowering/发射/链接代码——
它退化为 (b) 的一个验收子集（61 先行）。保留为表述上的验收分档，不另立实现。

**推荐：(b)，切片按 §5。**

## 5. 切片（可并行/串行边界）

| 切片 | 内容 | 仓库/对象 | 依赖 | 可并行性 |
|---|---|---|---|---|
| **G3-S1 契约迁移（主线，串行基线）** | `tools/drive.py`：`CONTRACT="1,2,32,8,1"`（:55）；CRT 选择改 v2 变体（self-start → `crt-selfstart-v2.yaml`、ISR → `crt-irq-v2.yaml`，替换 :51/:264 的 v1 路径；`gen-crt-v2.sh` 生成）；运行时与 nop-helper 缓存目录按合同版本隔离（:52-53/:286-303 现缓存 v1 对象，陈旧缓存会触发 P6 混链拒绝——**失败响亮，属预期保护**）；全批重跑，ledger/TIERS 逐 demo 记合同字段 | mcs251-demos-rewritten（语料区，非产品源码） | 无 | —— |
| **G3-S2 demo61 程序分组机制（§5.1，Alice 阻断修订）** | drive.py 新增 `PROGRAM_GROUPS` 程序选择/分组 + 分层记账（T0/T1/T2 按程序组各自驱动；`GAP_MANIFEST` 键按程序分组，不再整 demo 跳档）；语料零改动——两个子程序是**并列完整程序**，不是主+辅助关系，改名会扭曲语义 | 同上（drive.py harness，非产品源码） | 与 S1 无依赖，可并行（改 drive.py 不同区域，可并合） | ✅ |
| **G3-S3 记账与文档收口** | README §8/TIERS/ledger：static-ptr 桶改记"迁移即闭合"；41/45.1/45.2/49 增记 G2 次级缺口（首错遮蔽修正）；41 增记 CODE/DSEG 窗口（G8 族）；43/62 改记"待 S1 重跑确认" | 同上 | S1 重跑数字 | — |
| **（边界外，登记不动）** | G2 B-S2+（变参调用摘除+变参 ABI）解锁 41/45.x/49 的 T1 最后一层；G8 族 DSEG/CODE 窗口解 41（与 43）的链接预算；AS3→AS0 隐式转换另切片（82） | 产品 | —— | —— |

S1 验收分档（沿用 A4 §6 四档，不许合并）：clang → llc v2 obj（e_flags/attributes）→ lld（v2 CRT、
map 槽区、E2 告警审阅）→ QEMU 窗口。**G3 完成判据：61 目标程序组（TFT320240-I8080，§5.1）四档全绿
——QEMU 档以实测验收为准，此前 P7 链接成功不预证全绿；41/45.1/45.2/49 记"G3 层已清除，
残余=G2（+41 窗口）"。**

### 5.1 S2 机制设计：程序选择/分组与分层记账（Alice 阻断修复）

**为什么原 R6/`GAP_MANIFEST` 路线不闭合（结构性阻断）**：drive.py 对任何非空 `GAP_MANIFEST` 项强制
T0 失败——`demo_compile()` 以 `T0 gap: N file(s) compiled ok, M isolated per GAP_MANIFEST` 返回 False
（drive.py:210-215），main() 据此记 T0=gap（:362），随后 **`if ok:`（:365）使 T1/T2 结构性 skip**：
隔离一旦发生，llc/lld/QEMU 永不执行，"61 四档全绿"不可达。`GAP_MANIFEST` 是 demo 级键的"整 demo
跳档"机制，且其跳过日志是 G1 ISR 证据形状（"retains N bare `interrupt` site(s)"），与 61 的重复
定义语义不符。修复为独立程序选择/分组机制（全部改动在 drive.py，非产品源码）：

1. **数据结构**：新增程序组注册表 `PROGRAM_GROUPS`，shape 为
   `{demo: {group_id: {"srcs": [相对 demo 根的 .c 路径]}}}`；61 的登记：
   `PROGRAM_GROUPS = {"61-DMA-LCM液晶屏接口测试": {"TFT320240-I8080": {"srcs": ["TFT320240显示程序-硬件I8080并行接口+DMA刷新/LCM_Test.c"]}, "LCD1602-M6800": {"srcs": ["LCD1602显示程序-硬件M6800并行接口+DMA刷新/LCD1602-LCM-DMA.c"]}}}`。
   未登记的 demo 单组默认、行为不变。`GAP_MANIFEST` 保持 G1 用途，但键从 demo 升维为
   `(demo, group_id)`——文件级隔离只从**本程序组**的编译输入集剔除文件，不再触发整 demo 跳档；
   61 两文件均为完整程序，无需进 `GAP_MANIFEST`，分组本身即隔离。
2. **控制流（drive.py 三个决策点）**：
   - main() 主循环（:357）按 demo 展开为 `(demo, group)` 序列；build 目录改
     `build/<demo>/<group_id>`（:359），使 `demo_link()` 的"链接 build 目录内全部 .ll"（:221）
     天然只链一个程序——`duplicate definition of _delay_ms` 从输入集上消失；
   - `demo_compile()` 接受 `group` 参数，`find_c_files` 结果过滤为组内 `srcs`；**组内全编译通过即
     return True**——:210-215 的强制 False 仅保留给组内 `GAP_MANIFEST` 文件级隔离（降级为"组内
     gap"，不再阻断其他组）；
   - :365 `if ok:` 门禁保持形态但按组求值：每组 T0 pass 各自解锁本组 T1/T2，互不阻塞。
3. **记账字段（分层）**：`rec["programs"][group_id] = {"srcs", "tiers"{"T0","T1","T2"},
   "t0_log"/"t1_log"/"t2_log", "seconds", "contract", "crt"}`（contract/CRT 代字段随 S1 落地）；
   demo 级 `rec["tiers"]` 改为聚合视图（全部组 pass 才记 pass，否则记最差态并在备注列名组）；
   `write_tiers()`（:429）为分组 demo 输出**每程序一行**（如 `| 61 [TFT320240-I8080] | … |`）。
   验收断言"61 四档全绿"落在程序组行上：TFT320240-I8080 为目标验收组，LCD1602-M6800 组同机制
   驱动、如实记录，不冒充也不拖累目标组。

## 6. 测试矩阵

### 6.1 产品 lit（零改动承诺，回归确认）

G3 不改产品源码 → `llvm/test/CodeGen/MCS251`（含 `oseg-errors.ll` 的 v1 N1 钉子
`static pointer parameters are not supported by the compatibility ABI` 双半、`call-error-indirect.ll`、
`elf-v2-identity.ll` 等）、`clang/test`（Sema/CodeGen mcs251）、`lld/test/MCS251` 全部只读复跑，
基线见 §8。S1 落地前后各跑一遍，数字必须不变。

### 6.2 外部链验收（S1/S2 产物，不入 lit）

| 组 | 项 | 期望 |
|---|---|---|
| 61 | 四档全链（crt-irq-v2，TFT320240-I8080 程序组） | T0/T1/T2 逐档实测验收——QEMU 档尚待验收，链接成功不预证全绿；map 断言 Show_Str 第 5 参（%4 参数位）ptr 槽落 DSEG、ISR 58/59 向量表合成；E2 无告警（槽函数仅前台可达，实测 ISR 不调 Show_Str——若告警则审） |
| 41 | 四档（gui/LCD/main/sys，test.c 除外） | 前两档绿、第三档预算阻塞（lld 档 CODE/DSEG 窗口，§3.3）；窗口决策（§7-C2）前如实记"CODE/DSEG 窗口阻塞" |
| 45.1/45.2/49 | clang 档 + llc 档 | clang 绿；llc 档变参验收期望须刷新——旧二进制历史观测为无条件拒文案，当前构建已含 B-S2 变参实现，以重建二进制实测文案分层记录，不误记 G3 |
| 负例 | 陈旧 v1 运行时缓存混入 | P6 文案（混链拒）——验收 S1 的缓存隔离做对了 |
| 负例 | 手写 v1 IR 槽函数 | oseg-errors.ll 原文案（产品钉子保持） |

### 6.3 语料级回归

S1 全批重跑：T0 pass 60 / T1 pass ≥31（+61）为预期方向；29 个既有 pass demo 在 v2 下不得回退
（若有 demo 依赖 v1 特有布局，属新登记缺口而非 G3 回归——逐个实名上报）。

## 7. 风险与 PM 决策点

风险：

| 风险 | 级 | 缓解 |
|---|---|---|
| 缓存陈旧（v1 运行时/nop-helper/旧 .ll 混入 v2 链） | 高发生/低损害 | lld 混链拒绝响亮（P6）；S1 缓存目录按合同版本命名，一步到位 |
| 全批 v2 重跑基线刷新（T2 输出、字节断言、29-pass demo） | 中 | 四档分开记录；差异逐 demo 实名化，禁止静默吞 |
| 41 窗口决策推迟 → 41 长期挂"双残缺口" | 低 | 记账明确 G3 层已清，验收主体转 G2/G8 |
| E2 告警被误读为回归 | 低 | 告警仅文档化限制（D.5 overlay 语义继承），S1 报告附告警清单 |
| `main_test` 式改写扭曲 61 语义 | 低 | S2 选程序分组机制（§5.1，语料零改动） |

决策点（Alice 修订重分类）：

**A. 已裁定（非决策点，落实即可）**：契约迁移范围（v1→v2、v1 deprecated、CallABIMinor=1 唯一表达）
已由 A4 PM 裁定 #1/#2（2026-09-13）裁定——S1 是落实既定裁定，不是新请示。

**B. 实施必做项（非决策点，随 S1/S3 执行）**：

1. **首错遮蔽修正**：把 41/45.1/45.2/49 改记"G3+G2 次级"；README §8 旧行（7-demo G3）标注为历史。
2. **ledger 记合同/CRT 代**：ledger/TIERS 随 S1 为每个 demo（§5.1 起为每个程序组）记录
   contract/CRT 代，防口径再漂移。

**C. PM 决策点（真实取舍，3 项）**：

1. **61 程序组织**：程序分组机制（§5.1，推荐；语料零改动、两程序皆驱动）vs `main_test` 改写
   vs demo 目录拆分。
2. **41（与 43）CODE 窗口预算归属**：是否批准验收链外移 `XINIT/XDATA_INIT`（如 0xfff000/0xfff100）
   并做 QEMU/flash-gate 验证；还是把 41 的链接档验收转挂 G8 预算切片。
3. **EDATA 独立立项（DSEG 直达页 128B 上限，G8 族）**：槽密集 demo（41 需 ~193B）是否立项
   "非叶槽区迁 EDATA"独立切片——本役不做，仅登记。

**D. 新增排期选择（1 项）**：是否捆绑 G2（61 不需要等待；捆绑会拖住已就绪的闭合面）。

## 8. 基线（2026-09-15 实测，HEAD f362d88c7；同日二次复核复现）

| 套件 | 命令 | 结果 |
|---|---|---|
| llvm | `cd /home/liu/build-mcs251 && ./bin/llvm-lit test/CodeGen/MCS251` | **164/164 PASS** |
| lld | `cd /home/liu/build-mcs251-lld && ./bin/llvm-lit /home/liu/LLVM_STC32/MCS251/lld/test/MCS251` | **22/22 PASS** |
| clang CodeGen | `cd /home/liu/build-mcs251-s1 && ./bin/llvm-lit --filter mcs251 …/clang/test/CodeGen` | **28/28 PASS** |
| clang Sema（附） | 同上 `--filter mcs251 …/clang/test/Sema` | **24/24 PASS**（含 `mcs251-vararg-diag.c`，G2/B-S1） |

- clang 基线口径（Alice 修订）：**32 为协调员跨族拼单标签，非 CodeGen 单族计数；28/24/62 为另行测量
  口径，不能用于否定该拼单**。跨族拼单精确测试清单登记：vararg-diag 1 + Sema isr 1 + isr-pch 1 +
  CodeGen mcs251-* 28 + Parser 1 + PCH 1。本役单族复测口径（2026-09-15，二次复核同数）：CodeGen
  `--filter mcs251` **28/28**、Sema **24/24**（含 `mcs251-vararg-diag.c`，G2/B-S1）、全 suite
  `--filter mcs251` **62**（28 CodeGen + 24 Sema + 7 Parser + 1 Driver + 1 PCH + 1 Preprocessor，
  全部 PASS）。
- 构建快照说明：上表与单族复测绑定本役探针二进制快照（clang=`build-mcs251-s1` 03:39、
  llc=`build-mcs251` 03:34、lld=`build-mcs251-lld` 03:04，2026-09-15；盘上源码 mtime 04:04 晚于
  全部二进制，见卷首漂移注记）。跨族拼单若出自不同构建快照（含 B-S2 落地前后），数字不可逐项
  对减；S1 基线刷新时须同步登记所用二进制及其时间戳。
- 五 demo 现状（ledger，G1-4 复核批次）：T0 pass ×5、T1 gap ×5、T2 skip；T1 首错文案均为
  `static pointer parameters are not supported by the compatibility ABI`（41@LCD.ll、45.x/49@main.ll、61@LCM_Test.ll）。

## 9. 涉及文件清单

- 预计修改（实施役，均非产品源码）：`/home/liu/LLVM_STC32/mcs251-demos-rewritten/tools/drive.py`（CONTRACT、
  CRT 选择、缓存隔离、`PROGRAM_GROUPS` 程序分组与分层记账，§5.1）、`tools/rewrite.py`（仅当 S2 选改写路线）、
  `README.md` §8/TIERS.md/ledger.json（S3）、
  可能新增 `validation/mcs251-elf/runtime/` 生成产物的 harness 引用（v2 yaml 已在仓库，只读复用）。
- 明确不动：`llvm/lib/Target/MCS251/**`、`lld/MCS251/**`、`clang/**`、`MCS251Attributes.*`、
  `llvm/test/**`、`lld/test/**`、`clang/test/**`、v1 CRT 夹具（`crt-selfstart.yaml`/`crt-irq.yaml`/`crt.o`）。
