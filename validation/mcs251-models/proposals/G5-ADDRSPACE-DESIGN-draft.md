# G5 设计稿草案：AS3 `__xdata` 指针实参的地址空间缺口（demo 82/44）

- 日期：2026-09-15（调查日）。分支 `minimal-isr`，HEAD `076cb61ed`（"G2/B-S3 - variadic
  signatures across TUs and the value chain"）。
- 本文为**只读调查产物**：不改任何产品源码。所有 file:line 断言均在盘上源码逐一核验；
  所有"实测"在 `/tmp` 复现（clang=`/home/liu/build-mcs251-s1/bin`、
  llc=`/home/liu/build-mcs251/bin`、lld=`/home/liu/build-mcs251-lld/bin`）。
  探针完整留档于 `/home/liu/LLVM_STC32/GAP-G5-PROBES/`（P1..P13、r1..r3、g5dr/g5abi/g5b_mix）
  与 `/tmp/g5cast`（方案 a）、`/tmp/g5demo82`、`/tmp/g5min`、`/tmp/g5min2`（方案 b：82）、
  `/tmp/g5d44`（方案 b：44）。
- **修订 rev1（2026-09-15，Alice 设计评审 CHANGES REQUESTED 六组，逐条先复现后改稿）**：
  修订证据留档于 `GAP-G5-PROBES/rev1/`（`RESULTS-rev1.txt` 汇总：pCanRx 读写漂移与修复
  IR、去注释闭包脚本与输出、min21 最小副本、跨 TU 正/负链 `r3ctu/`、82/44 完整链接日志
  `t1-link/`）。本稿主要修正：(1) §3.2 最小闭包 20→**15**（44 为 17）、编辑计数全面更正；
  (2) §4(b) 撤回"零语义漂移"（归档副本存在一条 AS0 store 漂移，已修复并实测）；(3) §4(c)/§7
  改引 AS3 的正确论证（`RUNTIME-AS-PTR-DESIGN-A.md` §1.5），不再误引 §2.1 的 AS4/CODE 漏洞；
  (4) §4(b)/§5.2/§6.2 修正 Tag 28 签名记录表述并拆分 R3/R4/R6；(5) B3 撤回为漏链产物、
  B1 的 1228B 归属更正为 44 的 main.o、B 阻断口径统一；(6) D1-D7 决策点重整。
- 语料只读复用：`/home/liu/LLVM_STC32/mcs251-demos-rewritten/`（demo 82/44）与
  `.../tools/drive.py`、`ledger.json`。
- **并发役漂移注记**：本役进行中，工作树有 4 项未提交资产（§8），其中
  `validation/mcs251-runtime/src/mcs251_printf.c` 为 B-S4 实施役所改；本役只读该文件。
  产品源码 `llvm/lib/Target/MCS251/MCS251ISelLowering.cpp` 的 mtime=2026-09-15 13:15
  晚于 llc 构建（13:15 同刻，llc 已含）；lld 二进制 03:04、`LinkerCore.cpp` mtime
  2026-09-14 19:36，稳定。**行号随并发役漂移时以符号检索为准。**

## 0. 结论（一句话）

G5 的准确缺口不是"地址空间不可转换"，而是 **clang 前端未把 `__xdata`(AS3) 纳入任何
包含关系**：AS3 写入(store 语义)与 AS4 只读(load 语义)本质不同，2026-09-13 的
`RUNTIME-AS-PTR-DESIGN-A.md`  PM 裁定 #1 已明确"AS3 不连带开放"，因此
**G5 无法在不推翻既有裁定的前提下由"包含关系"路线关闭**；demo 82 的最小覆盖子集是
**AS3 精确形参**（去注释调用图最小闭包 **15** 个函数 / **21** 处 AS 编辑；推荐登记的
机械全量规则为 20 个函数 / 29 处 AS 编辑，rev1 计数修正见 §3.2），
该形态在 v2/P-4 链上 clang+llc 全通；**通道保持是有前提的**——修正版副本（含 §4(b)
pCanRx 修复）后端访存保持 MOVX @DPTR 全 24 位序列、`DmaRxBuffer_Decode` 体内 IR 级
`addrspacecast` 计数为 0，而 rev1 之前的归档副本在该函数内残留**一条经 AS0 的
store 语义漂移**（rev1 撤回"零语义漂移"表述，见 §4(b)），
**属改写可规避**；但即使改写，demo 82 的 T1 仍被**两条独立缺口**挡住
（`.mcs251.dseg` 196B 窗口预算、CSEG `printf` 容量；K&R `void main()` 阻断 B0 已随
改写副本一并消除，B3 经完整链接命令复核后**撤回**为漏链产物——口径统一见 §4 表），
故本稿推荐 **"G5 记为改写规避 + 独立 xdata-DMA 切片另立"** 的双轨处置。

## 1. 问题与登记口径

- 权威定义（`mcs251-demos-rewritten/README.md:335`，§8 G5 行）：
  > `__xdata`/`__code` 指针实参传给泛型指针形参时地址空间不匹配（限定词承载 MOVX/@dr
  > 语义，不能静默丢弃）
  最小证据串：clang `changes address space of pointer`；命中 demo **44, 82**
  （demo 43 的旧记录已失效并在 ledger `corrected_prior` 更正）。
- 三档表口径（`TIERS.md`）：44 与 82 均为 `T0 gap / T1 skip / T2 skip`，备注
  `addr-space ptr`。
- 现存记账不全（本役实测补正）：`ledger.json` 键路径
  `demos["82-CANFD使用DMA收发测试"].t0_log`（2026-09-15 18:00 由并行 G3 役重写后行号
  为 :16729，会继续漂移，以键路径为准）只含
  `canfd_dma.c` 的 **4** 条错误；本役实测同一 flags 下 `main.c` 另有 **4** 条
  （见 §2.2）。**G5 在 demo 82 的实际站点数是 8，不是 4。**
- 与既有裁定的关系：`RUNTIME-AS-PTR-DESIGN-A.md`（状态"已拍板"，2026-09-13 用户同意
  Alice 四项推荐）裁定 #1 明确"**AS3 不连带开放**……AS3/AS0 与 AS3/AS4 保持拒绝；
  82 CANFD 仅作后续**独立 AS3/XDATA/DMA 切片**的验收语料，不纳入本片能力承诺"。
  `validation/mcs251-models/DESIGN.md:272`、`:345`、`:442`、`:1222` 同口径。
  **本稿因此不是"再提议一次方案甲"，而是回答"G5 这条缺口该走哪条路、要不要 PM 改裁定"。**

## 2. 现状精确定位（当前 HEAD 实测，全部复核）

### 2.1 拒绝层与文案

- **唯一拒绝层 = clang Sema 指针赋值兼容性；llc/后端无 G5 门槛。**
  诊断定义 `clang/include/clang/Basic/DiagnosticSemaKinds.td:9497-9509`
  `err_typecheck_incompatible_address_space`；发射点
  `clang/lib/Sema/SemaExpr.cpp:17929`（`AssignConvertType::IncompatiblePointerDiscardsQualifiers`
  分支内 `lhq.getAddressSpace() != rhq.getAddressSpace()`）。
  强制转换版在 `SemaCast.cpp:2719`，但 `IsAddressSpaceConversion` 命中的显式 cast 走
  `CK_AddressSpaceConversion`（`SemaCast.cpp:1559-1560`）**放行**（实测 P2/P4/P6）。
- 未建立包含关系：`clang/lib/Basic/Targets/MCS251.h:85-103` 的
  `isAddressSpaceSupersetOf` 只声明 **AS4→32 位 AS0**；AS3 不在内。因此
  `checkPointerTypesForAssignment` 判定歧义空间不同（AS3 vs AS0），报错。
- 这是**有意**的 fail-closed，不是缺实现：AS3 是**可写**的 XDATA（demo 82 的
  `CANFD_Set_DMA_Buff` 第二参就是写目的，`canfd_dma.c:131-137`），而 `RUNTIME-AS-PTR-DESIGN-A.md`
  §1.5 指出"82 不是只读案例"，且 AS3→AS0 后 store 会从 MOVX @DPTR 改走 DR
  （§4 实测序列），通道等价性**尚未证明**。

### 2.2 demo 82 逐文件实测（drive.py 同款 flags，v2 契约 `1,2,32,8,1`）

命令（`-I` 与 `drive.py:275-291` 同源，输出节选）：

```
$ clang --target=mcs251-unknown-none -std=c11 -O0 -fmcs251-keil -Wall -Wextra \
    -Wno-implicit-int-conversion -Wno-shorten-64-to-32 -Wno-unused-but-set-parameter \
    -Wno-unused-variable -Wno-uninitialized -Wno-incompatible-pointer-types \
    -Wno-int-conversion -Wno-constant-conversion \
    -I. -I.../mcs251-dialect/include -I.../mcs251-porting/generated -I.../mcs251-porting/include \
    -Xclang -mcs251-memory-contract=1,2,32,8,1 -S -emit-llvm <file> -o /tmp/x.ll
```

| 文件 | rc | 错误站点 | 原文（截断） |
|---|---|---|---|
| `main.c` | 1 | `101:16` `CANFD_Init(CAN1)` | `passing '__attribute__((address_space(3))) CANFD_TypeDef *' ... changes address space of pointer` |
| | | `102:16` `CANFD_Init(CAN2)` | 同上 |
| | | `157:120` `DmaRxBuffer_Decode(..., DmaRxBuffer)` | `... address_space(3) uint8_t * ... changes address space of pointer` |
| | | `173:120` 同 157（CAN2 分支） | 同上 |
| `canfd.c` | **0** | — | **无 G5 错误**（`CANFD_TypeDef*` 形参只被 AS3 实参调用在 44，不在 82） |
| `canfd_dma.c` | 1 | `159:32` `CANFD_Set_DMA_Buff(&pstcTx,&DmaTxBuffer[0])` | `address_space(3) uint8_t * ... changes address space of pointer` |
| | | `216:32` `...&DmaTxBuffer[32]` | 同上 |
| | | `229:32` `...&DmaTxBuffer[64]` | 同上 |
| | | `242:32` `...&DmaTxBuffer[96]` | 同上 |

- 关键站点原文（`canfd_dma.c:159`，即任务指定站点）：
  `CANFD_Set_DMA_Buff(&pstcTx,&DmaTxBuffer[0]);//数据写入缓冲区，总共 8+DLC 字节`
  形参定义 `canfd_dma.c:127` `void CANFD_Set_DMA_Buff(const stc_can_tx_t *pcanTx, uint8_t *u8DmaTxBuf)`；
  实参源 `canfd_dma.c:33` `uint8_t xdata DmaTxBuffer[256];`（重写后 `xdata`→`__xdata`，AS3）。
- demo 44 站点（源 `ledger.json` 键路径 `demos["44-CANFD1-CANFD2同时使用收发测试"].t0_log`，
  重写后行号 :8516，以键路径为准；本役未重跑）：`main.c:104/105/149/171/187`，
  形态为 `CANFD_TypeDef*`（`canfd.h:132-133` `#define CAN1 ((CANFD_TypeDef __xdata*)CAN1_BaseAddress)`）。

### 2.3 最小探针矩阵（`/home/liu/LLVM_STC32/GAP-G5-PROBES/`，全部实测）

clang flags：`--target=mcs251-unknown-none -std=c11 -O0 -fmcs251-keil -Xclang -mcs251-memory-contract=1,2,32,8,1 -S -emit-llvm`。

| # | 形态 | 结果 | 层/文案 |
|---|---|---|---|
| P1 | **核心**：`uint8_t xdata B[]; void sink(uint8_t*); sink(&B[0])` | **拒** | clang `P1.log`：`passing '...address_space(3) uint8_t *' ... changes address space of pointer` |
| P2 | 显式 cast `sink((uint8_t*)&B[0])` | **过** | IR `addrspacecast ptr addrspace(3) to ptr`；llc obj exit 0 |
| P3 | 隐式初始化 `uint8_t *p = &B[0]` | **拒** | `initializing ... changes address space of pointer` |
| P4 | `uint8_t code T[]; csink((uint8_t*)&T[0])` | **过** | AS4→AS0 已由 A1 放行 |
| P5 | array decay / `&B[16]` / `B+8` | **全拒**（3 条） | 同 P1 文案 ×3 |
| P6 | `void sink3(uint8_t xdata *p); sink3(&B[0])` | **过** | IR 保留 `ptr addrspace(3)` |
| P7 | 函数指针间接调用 `cb(B)` | **拒**（`cb` 形参经 AS0 形参） | 同 P1 |
| P8 | 存入 AS0 结构字段 `h.p = &B[0]` | **拒** | `assigning ... changes address space of pointer` |
| P9 | return 加宽 `uint8_t *get(){return &B[0];}` | **拒** | `returning ... changes address space of pointer` |
| P10 | `code` 隐式退化（无 cast） | **过**（仅 `discards qualifiers` 警告） | `isAddressSpaceSupersetOf` 已含 AS4 → 警告非错误 |
| P11 | AS4→AS3 `x3(CTab)` | **拒** | `changes address space of pointer`（方向无关一律拒） |
| P13 | 手写 IR `addrspacecast ptr addrspace(3)<->ptr` | **过**（asm exit 0） | 后端 `LowerAddrSpaceCast` `IsFarRAM={0,3,9}` 等宽 passthrough；**preserve AS 语义由前端保证** |

rev1 新增（`GAP-G5-PROBES/rev1/`，评审阻断 1/4 的复现）：

| # | 形态 | 结果 | 层/文案 |
|---|---|---|---|
| P14 | 漂移复现：AS3 形参 + `stc_can_rx_t* pCanRx`（AS0 局部）经显式 cast 接收，`pCanRx->u32ID = reverse4(pCanRx->u32ID)` | 过（编译器无意见） | IR：`addrspacecast ... to ptr` + **`store i32 ... ptr`（AS0）**——写路径漂移到 DR 通道（`rev1/pCanRx-drift/main_archived_as0pCanRx.ll`） |
| P15 | P14 修复：`pCanRx`/cast 一并 `xdata` | 过 | IR：函数体内 `addrspacecast` 计数 0、`store i32 ... ptr addrspace(3)`；asm `mov 0x84,r1`+`movx @dptr,a`（`rev1/pCanRx-drift/p14.*`、`main_fixed_as3pCanRx.ll`） |
| r4 | 跨 TU 正链：TU-A AS3 原型调用 / TU-B AS3 定义，lld+crt | 过 | 链接 OK；被调方 MOVX+DPXL；`DmaTxBuffer` 落 XSEG（`rev1/r3ctu/ok.map`） |
| r4' | 跨 TU 负链：TU-A 按 AS0 原型（本 TU 无诊断）/ TU-B AS3 定义 | **链接零诊断通过** | P-4/Tag 28 无 AS 维度的**跨 TU 盲区实测**（`rev1/r3ctu/mismatch.map`）；同 TU 版（AS0 原型+AS3 实参）仍被 clang 拒（P1 文案） |

附加链路事实：
- llc `-filetype=asm` 对任何含 `__xdata`/`__code` 全局的模块 → `LLVM ERROR: MCS251:
  __xdata global 'DmaTxBuffer': storage requires ELF object output (-filetype=obj
  -mcs251-object-format=elf)`（exit 134，`MCS251AsmPrinter::emitGlobalVariable`）——
  **与 G5 无关的既有约束，记录以免误判**（`drive.py` T1 正确使用了 `-filetype=obj`）。
- 手写 IR 产出 v2 对象需自带 `!mcs251.signatures`，否则 llc 报
  `a v2 object requires '!mcs251.signatures' metadata`（P-4/Tag 28 强制域，正常）。

## 3. demo 82 需求画像

### 3.1 全文 addr-space 用法清单（去注释有效位）

| 位置 | 声明 | AS | 角色 |
|---|---|---|---|
| `canfd_dma.c:33` | `uint8_t __xdata DmaTxBuffer[256]` | AS3 | DMA TX 缓冲，**CPU 写入 / DMA 读** |
| `canfd_dma.c:34` | `uint8_t __xdata DmaRxBuffer[256]` | AS3 | DMA RX 缓冲，**DMA 写 / CPU 读** |
| `canfd_dma.h:30-31` | 同上的 `extern` | AS3 | 跨 TU 声明 |
| `canfd.h:132-133` | `#define CAN1/CAN2 ((CANFD_TypeDef __xdata*)0x7ef400/0x7ef300)` | AS3 | 外设寄存器块（`main.c:101/102` 传入 `CANFD_Init`） |
| `canfd_dma.c:57-58,71-72,102-103,116-117` | `(u8)((u16)&DmaTxBuffer >> 8)` 等 | — | 地址整数截断（**不是**指针转换，无 AS 诊断） |

即：**只有 AS3 一种空间，无 AS4 数据用法**（`code` 未在 82 出现）。

### 3.2 最小覆盖子集（IR 级，v2 合同实测）

G5 在 82 中**表面**只有 3 个被调函数、8 个调用点，全部是"AS3 实参 → AS0 形参"：

| 函数 | 形参（原始） | 需改形参 | 调用点 |
|---|---|---|---|
| `CANFD_Set_DMA_Buff`（`canfd_dma.c:127`） | `const stc_can_tx_t *`, `uint8_t *` | 第 2 参 → `uint8_t __xdata *` | `159/175/188/201/216/229/242`（7 处；`175/188/201` 走 `TX_ADDR_ALIGN==1` 分支，`216/229/242` 走 `else`） |
| `CANFD_Init`（`canfd.c:532`） | `CANFD_TypeDef*` | 第 1 参 → `CANFD_TypeDef __xdata*` | `main.c:101/102` |
| `DmaRxBuffer_Decode`（`main.c:192`） | 第 4 参 `uint8_t *` | 第 4 参 → `uint8_t __xdata*` | `main.c:157/173` |

**但最小集合不是"这 3 个"——AS3 通过 CANx 形参传递闭包强制扩张。** 实测（`/tmp/g5min`，
只改 3 个签名）：`canfd.c` 仍报 `605:17` `CAN_FD_Init(CANx, &stcInit)` 与 `608:16`
`CAN_IntCmd(CANx, CAN_INT_ALL, Enable)`——**AS3 实参经形参 `CANx` 继续下传，凡在闭包内
接收 `CANFD_TypeDef *CANx` 的函数都必须同步改。**

**闭包计算（rev1 修正：去注释后的调用图才是真调用面）。** 初稿以"含注释文本"的调用图
求得闭包 = 20，与"`canfd.c` 里 20 处 `CANFD_TypeDef *` 定义逐名重合"——该重合是**虚增**：
`CAN_SetSTBPrioMode` 的唯一"调用"是注释 `canfd.c:492`（`//CAN_SetSTBPrioMode(CANx, ...)`），
`CAN_TransData` 在 44 的唯一调用也是注释（`44/main.c:146`）。rev1 以去注释调用图
（`rev1/closure/closure.py` + 输出）重算：

- **82 的最小闭包 = 15**（种子 `CANFD_Init`，82 的 `main.c` 活代码只调用它）：
  `CANFD_Init, CAN_AFConfig, CAN_ClrStatus, CAN_EnterNormalComm, CAN_FD_Config,
  CAN_FD_Init, CAN_IntCmd, CAN_SBTConfig, CAN_SWReset, CAN_SetErrWarnLimit,
  CAN_SetRBOvfOp, CAN_SetRBSWarnLimit, CAN_SetRBStoreSel, CAN_SetTransMode,
  CAN_SetWorkMode`。**不在闭包的 5 个**：`CAN_SetSTBPrioMode, CAN_SetData,
  CAN_TransData, CAN_SendData, CAN_ReceiveData`——实测把这 5 个的定义连同其在
  `canfd.h` 的 3 个原型恢复为 AS0 后三 TU 仍 clang 0 错、llc obj 0 错
  （`rev1/min21-82/`）。
- **44 的最小闭包 = 17**（活代码种子 `CANFD_Init + CAN_SendData + CAN_ReceiveData`，
  `44/main.c:104/105/149/171/187`）；不在闭包的 3 个：`CAN_SetSTBPrioMode, CAN_SetData,
  CAN_TransData`。
- **全量 20 是可选的机械规则，不是最小需求**：直接改写 `canfd.c`/`canfd.h` 中所有
  `CANFD_TypeDef *` → `CANFD_TypeDef xdata*` 免去调用图计算，且覆盖"未来取消注释/
  恢复调用"的面（`CANFD_SET_REG8_BIT` 宏面不变）。本稿推荐登记**全量规则**，但计数
  上必须区分两个口径。

**编辑计数（rev1 全面修正；含 §4(b) pCanRx 修复新增的 `main.c` 2 处）**：

| 口径 | 82 | 44 |
|---|---|---|
| **全量 20 规则**（AS 编辑） | **29** = `canfd.c` 20 + `canfd.h` 4 + `canfd_dma.c` 1 + `main.c` 4（`DmaRxBuffer_Decode` 原型+定义、`pCanRx` 声明、cast）；另改 `main(void)` 1 处，**文本合计 30** | **24** = `canfd.c` 20 + `canfd.h` 4（44 的 `main.c` 无 AS 编辑）；另改 `main(void)`，**文本合计 25** |
| **最小闭包**（AS 编辑） | **21** = `canfd.c` 15 + `canfd.h` 1（仅 `CANFD_Init` 原型）+ `canfd_dma.c` 1 + `main.c` 4；文本合计 22（`rev1/min21-82/` 实测 clang+llc 全过） | **20** = `canfd.c` 17 + `canfd.h` 3（`CANFD_Init/CAN_SendData/CAN_ReceiveData`）；文本合计 21 |
| （历史）rev1 前归档副本 | 27 AS / 28 文本——**未含 pCanRx 修复**，非最终形态 | 24 AS / 25 文本 |

全量口径实测：`rev1/rewrite-B2-as3param-82/`（82，clang 三文件 0 错、llc obj 0 错）；
44 沿用 `/tmp/g5d44`（24 处，clang 两文件 0 错、llc obj 0 错；44 无 pCanRx 形态）。

IR 形状（`/tmp/g5min2` + rev1 修正副本 `rev1/rewrite-B2-as3param-82/`，最终补丁）：
`@CANFD_Set_DMA_Buff(ptr noundef %0, ptr addrspace(3) noundef %1)`、
`@CANFD_Init(ptr addrspace(3) noundef %0)`、
`@DmaRxBuffer_Decode(i16, i16, i8, ptr addrspace(3))`；DMA 写体保留 6 条
`store i8 ..., ptr addrspace(3)` → MOVX @DPTR。

**最小覆盖子集一句话**：让"指向 XDATA 的指针"在**闭包内全部形参**处保留 AS3
（82 最小闭包 15 个函数 / 21 处 AS 编辑；推荐登记的全量机械规则 20 个函数 / 29 处 AS
编辑），不需要任何跨空间隐式转换。也就是说——**G5 在 82 上不是"缺转换"，
而是"写法把 AS3 抹平"。**

**另注意（独立于 G5 的新发现）**：两份 `main.c` 都写 `void main()`（K&R 无原型，
`82/main.c:83`、`44/main.c:87`），而 v2 CRT 的 `_main` 是**有原型**记录。P-4/Tag 28
在链接期硬错：`MCS251 signatures: records '_main' disagree on prototype-ness
(one side is K&R no-prototype, the other is prototyped)`。这是**语料/Harness 层**的独立
T1 阻断（同族 demo 61 用的是 `void main(void)`），与 G5 无关，但会被 G5 关闭后立刻暴露。
（rev1 注：该阻断 B0 在两份改写副本中已随 `void main(void)` 编辑一并消除，见 §4 阻断表；
语料级同型位点还有 `76-串口绘图-使用UART1接口/wave.c:60`。）

## 4. 方案空间

### (a) 显式 cast 改写：调用点 `(uint8_t *)&DmaTxBuffer[0]`（保持 AS0 形参）— **不推荐**

- 实测：`/tmp/g5cast`（副本归档为 `GAP-G5-PROBES/rewrite-A-cast/`；6 个 DMA 站点 + `main.c` 3 处）。clang 三文件全过，
  llc obj 全过（`g5c_main.o/g5c_canfd.o/g5c_canfd_dma.o`）。
- IR/码序列：转换 = `addrspacecast ptr addrspace(3) to ptr`；后端
  `LowerAddrSpaceCast`（`MCS251ISelLowering.cpp:1256-1330`，`IsFarRAM` 等宽分支在 :1315-1330）走 `IsFarRAM={0,3,9}`
  等宽原值直通。**被调方随之改走 DR 通道**（实测同一 store 两种通道）：

  ```
  AS0 形参（改写后）:        AS3 形参（保持原样）:
    mov r4, #0x07              mov 0x84, r0        ; DPXL = bank
    mov @dr0, r4               mov dpl, r2 ; mov dph, r1
                               mov a, r3
                               movx @dptr, a
  ```

- 风险：`__xdata` 限定词本意承载 **MOVX/DPXL 通道**（README §8 原话）；改为 DR
  后，"相同 canonical 地址经 MOVX 与 DR 指向同一 XDATA 单元"这一等价性**尚无证明**
  （`RUNTIME-AS-PTR-DESIGN-A.md` §1.5 明确列为独立切片的前置条件）。对 82 的
  DMA 语义（CPU 写入后由 DMA 引擎消费）这是**未验证的行为改变**，且抹掉了
  类型中唯一能恢复来源的信息。**rev1 补充：这不是假设性风险**——初版 (b) 归档副本
  在 `DmaRxBuffer_Decode` 内恰好留下了同型漂移（P14：显式 cast 后 store 落 AS0/DR），
  证明"局部 AS0 指针 + 显式 cast"写法会静默改通道。
- **结论：能编译，但不诚实**——正是 G5 行"不能静默丢弃"要防的事。

### (b) AS3 精确形参改写（全量规则 29/24 处 AS 编辑；最小闭包 21/20）— **推荐（作为 G5 的处置）**

- 实测：**rev1 修正副本** `GAP-G5-PROBES/rev1/rewrite-B2-as3param-82/`（82：`canfd.c` 20 +
  `canfd.h` 4 + `canfd_dma.c` 1 + `main.c` 4，另 `main(void)`）与 `/tmp/g5d44`（44：
  `canfd.c` 20 + `canfd.h` 4，另 `main(void)`）。**两 demo clang 三/两文件全过、
  llc obj 全过**（计数口径见 §3.2）。IR 精确保留：
  - 形参：`@CANFD_Set_DMA_Buff(ptr noundef %0, ptr addrspace(3) noundef %1)`；
  - 访存：6 条 `store i8 ..., ptr addrspace(3)` 全部保留 → 后端吐 MOVX @DPTR/DPXL 全 24 位序列；
  - `main.c` `DmaRxBuffer_Decode` 的局部指针 `pCanRx` **必须一并 AS3 化**
    （`stc_can_rx_t xdata* pCanRx` + `(stc_can_rx_t xdata *)` cast）。初版归档副本把它留在
    AS0（`pCanRx = (stc_can_rx_t*)(u8DmaTxBuf+Offset)`），rev1 复现其 IR 为
    `addrspacecast ptr addrspace(3) to ptr` 后跟 **`store i32 ... ptr`（AS0）**——
    `pCanRx->u32ID = reverse4(pCanRx->u32ID)`（`82/main.c:202`）是**读+写 RMW**，写经
    AS0 落 DR 通道，即 (a) 型语义漂移。修复后实测：函数体内 `addrspacecast` 计数 0、
    `store i32 %21, ptr addrspace(3)`、后端 `mov 0x84,r1`+`movx @dptr,a`（P15、
    `rev1/pCanRx-drift/`）。
- **通道保持（撤回"零语义漂移"表述后的如实版本）**：在**含 pCanRx 修复的修正副本**上，
  被调方全部 XDATA 访存（含 RX 缓冲的 RMW 写）保持 MOVX/DPXL 语义，`DmaRxBuffer_Decode`
  体内 IR 级零 `addrspacecast`、零 AS0 store；初版副本不满足该性质（上一条），故
  "零语义漂移"以修正副本为前提成立，不是 27 处版本的固有属性。
- 依赖：AS3 第 2 参 = **静态槽**，v1 合同下 llc 拒
  `MCS251: static pointer parameters are not supported by the compatibility ABI`
  （实测，`MCS251ISelLowering.cpp:3466-3497` 的 `Index && !AllowStaticPointers`，
  `AllowStaticPointers = ProgramAS == 4` 即 v2）。`drive.py:84` 默认 `CONTRACT_GEN="v2"`，
  **依赖已由 G3-S1 满足**；P-4/Tag 28 会自动带上新签名（`!mcs251.signatures` 从
  CGFunctionInfo 产生，无需手写登记）。
- 代价：**不是 3 处，是 29 处（全量规则）**——AS3 经 `CANx` 形参下传，闭包内 15 个
  （最小）至 20 个（全量）`canfd.c` 定义 + 对应 `canfd.h` 原型要改（§3.2）。`canfd.h`
  与 `canfd.c` 必须同改（否则 `conflicting types for 'CAN_TransData'` 等实测报错）。
- 风险：AS3 形参的**跨 TU ABI 尚无端到端验证收官**（`static-ptr ABI` 的 v2 静态槽
  已有 G3/A4 产物，但 AS3 指针走静态槽的 24 位 bank 字节保留尚未有专门 lit；rev1 已给出
  独立最小跨 TU 验收链 r4，见 §6 测试矩阵 R3）。另：**同签名不同 AS 的 Tag 28 记录不变**
  （rev1 修正，初版此处写反）：字段 `param_count/bitmap/ret/call_abi_*` 不含 AS
  （§5.2），因此第 2 参 AS0↔AS3 的两种编译产物在 P-4 层**完全同记录**——ABI 检查
  看不见 AS 差异，跨 TU 错配是链接期盲区（r4' 实测链接零诊断），AS 一致性只能由
  clang 前端在同 TU 内保证（R4a/R4b 拆分见 §6.2）。

### (c) 放开 superset 到 AS3（改产品，需 PM 新裁定）— **否决（推翻已冻结裁定，收益不足）**

- 需改：`clang/lib/Basic/Targets/MCS251.h:85-103` 增加 `isMCS251XDataAddressSpace(B)` 分支
  （或等价）；后端 `LowerAddrSpaceCast` 已支持 AS3↔AS0 等宽（P13 实测），故后端**几乎零改动**。
- 但必须同步推翻/修订 4 处冻结文字：`DESIGN.md:272/:345/:442/:1222`、
  `RUNTIME-AS-PTR-DESIGN-A.md` 裁定 #1（"AS3 不连带开放"）。
- 测试面：现负例 `clang/test/Sema/mcs251-xdata.c:62-69`（`dptr = pdat`，AS3→AS0）与
  `mcs251-as4-superset.c:57-59`（`cptr = xd`，AS3→AS0）会由 error 变 OK，须补干净正例，
  不能靠屏蔽 warning 伪造成"接受"；`mcs251-xdata-code-poststar.c:40-41`（`d = xp` /
  `xp = p1`，AS0↔AS3 反向）与 AS4↔AS3 负例**不转正**（见下条）。
- **（rev1 更正引用）AS3 的拒绝理由是"通道不同、等价性未证"，不是 AS4 的 CODE 写保护
  漏洞**。初版误引 `RUNTIME-AS-PTR-DESIGN-A.md` §2.1——该节（:137-151）论证的是
  **AS4/CODE** 场景（`unsigned char __code table[4]` 经包含关系转 AS0 后
  `CheckCodeStore` 不再识别、CODE 写保护被绕过），对 AS3 不成立（AS3 本来就可写，
  不存在"写保护被绕过"的问题）。AS3 的准确论证在同文 **§1.5（:120-131）**：
  AS3 由 MOVX@DPTR/DPXL 维护、AS0 走 DR，**AS4"本来就走 DR，因此转换零成本且读取
  等价"的证明不可复用**；须证明相同 canonical 地址经 MOVX 与 DR 指向相同 XDATA 单元，
  覆盖 load、store、bank 边界、宽访问拆分、相关寄存器状态及 DMA 前后可见性——该证明
  缺位，故暂不接受 (a)/(c)。写序列从 MOVX 变 DR 的实测见 §4(a)/P14。**这是 (c) 被
  否决的核心理由，不是工作量。**
- **（rev1 补充）"仅加 AS3→AS0 包含关系"不会使 AS4→AS3 负例转正**：
  `isAddressSpaceSupersetOf`（`MCS251.h:85-103`）的非平凡分支要求 lhs 为 32 位 AS0
  （`IsAS0(A) && ... isMCS251CodeAddressSpace(B)`）；给 AS3 加分支只新增
  **(AS0, AS3) 一对**。AS4→AS3（P11 `x3(CTab)`、`mcs251-xdata.c` 的 `pdat = cp`）
  lhs=AS3 不满足 `IsAS0(A)`，AS3→AS4 亦无分支可命中——两类负例在 (c) 下**仍拒**；
  翻正的只有 AS3→AS0 一族（上条测试面）。
- 另注：AS3 与 AS4 同为 32/8，但**语义方向相反**（AS3 可写、AS4 只读）。把两者并进
  同一个 superset 分支会使"只读包含关系"的类型论证失去简洁性。

### 推荐与三方案关系

**推荐 (b) 作为 G5 的处置登记 + 把 (c) 降级为"独立 xdata-DMA 切片"的候选，
不让 G5 变成 AS3 语义裁定的入口。**

理由：G5 的记账原话是"限定词承载 MOVX/@dr 语义，不能静默丢弃"。方案 (b) **恰恰是
把限定词放回它该在的位置**（形参），成本是语料改写，不触碰任何冻结面；而方案 (c)
是把"丢弃限定词"合法化，需要先证明 MOVX/DR 通道等价——那是另一个战役的入口
（`RUNTIME-AS-PTR-DESIGN-A.md` §3 A-XDATA 已预留）。

**但必须同时登记：demo 82 T1 不因 (b) 转绿。**（rev1 口径统一：上游语料的独立阻断为
B0/B1/B2 三条；**改写副本内 B0 已消**、B3 撤回——82 副本内实剩 **B1+B2 两条**，
44 副本内剩 **B1' 一条**。全部复现命令与输出在 `rev1/t1-link/`。）

| # | 阻断 | 文案（lld 原文） | 归属 | rev1 状态 |
|---|---|---|---|---|
| B0 | 两份 `main.c` 用 `void main()`（K&R），v2 CRT 的 `_main` 有原型 | `MCS251 signatures: records '_main' disagree on prototype-ness (one side is K&R no-prototype, the other is prototyped)` | **P-4/Tag 28 + 语料**（demo 61 已是 `void main(void)` 形） | **上游语料仍在；改写副本已消**（§3.2 计数表内含该编辑，S3 起不得再期望 B0） |
| B1 | 82：`canfd.o` 的 `.mcs251.dseg` **196B** 放不进 80B 空闲窗；44：**`main.o` 的 1228B**（rev1 更正归属：1228B 属 44 的 main.o，`canfd.o` 仍 196B；readelf 44 main.o dseg=0x4cc、canfd.o=0xc4） | 82 `cannot allocate .mcs251.dseg (from canfd.o): size 196 bytes ... largest free range is 80 bytes`；44 `... (from main.o): size 1228 bytes ... symbols _pstcTx _DataSize _msecond _pCan1Rx ...` | **G8 类窗口预算**（与 G5 无关） | 在（完整链接与 demo-only 链接均首报此错或 B2） |
| B2 | 加按需 `rt/printf.o`（+div/mod）后 CSEG 越界 | `CODE overlap for .text`（CSEG `0xff0700..0xff8000` = 30976B；修正副本 text：main 3492 + canfd 15655 + canfd_dma 3626 + printf 14035 = **36808B**，未计 divulong 1456/modulong 1312/CRT——rev1 更新数字） | **CSEG 容量/printf 运行时**（与 G5 无关） | 在（完整按需运行时链首错即此） |
| B3 | ~~运行时对象链缺失~~ | ~~`undefined symbol: __divulong_PARM_2` / `_putchar`~~ | — | **撤回（漏链产物）**：完整命令（demo 3 对象 + 按需 printf.o/divulong.o/modulong.o + crt-irq-v2.o）下这些符号全解——`_putchar` 由 82 的 main.o 定义（readelf：`FUNC GLOBAL _putchar`），`__divulong_PARM_2` 等由 harness 按需引入的除法运行时提供；补链后首错变为 B2（`rev1/t1-link/82-full.log`） |

即：**G5 是 demo 82 T0 的首错，但不是它 T1 的主因。** 只关 G5 会把 82 从
`T0 gap` 变成 `T0 pass / T1 gap`（副本内剩 G8 窗口 + CSEG 两桶），且 **44 的 dseg
缺口（main.o 1228B）比 82（canfd.o 196B）更大**。

## 5. 与既有冻结面/X 系列的交互

### 5.1 X 系列（AS3/AS4 放置、运行时转换 A 片）

- `XDATA-CODE-DESIGN-SUPPLEMENT.md` §2 的 MOVX 全 24 位序列（DPXL 每字节重指向、
  Glue 焊接、ISR 保存 DPXL）**是 (b) 路径的实际执行体**，不需改动；实测序列与文档逐条一致。
- 运行时转换 A 片（`RUNTIME-AS-PTR-DESIGN-A.md` §3 A3）的 `LowerAddrSpaceCast` 已落地
  （`IsFarRAM` 含 AS3），(b) 路径下**经 pCanRx 修复后无任何显式 `addrspacecast` 残留**
  （rev1：修正副本 `DmaRxBuffer_Decode` 体内计数 0）；初版副本曾在读+写路径留有一条
  显式 cast 及其 AS0 store 漂移（§4(b)），已修复，不受"AS3 不连带"约束。
- 放置：AS3 对象进 `.mcs251.XSEG.*`（`MCS251TargetObjectFile.cpp:47-48`，`GV->getAddressSpace()==3`），
  (b) 不改变放置，`DmaTxBuffer` 仍在 XSEG——**这是 (b) 优于 (a) 的直接理由**（(a) 也不改放置，
  但改访问通道）。

### 5.2 A4 / P-4 冻结面

- **A4（v2 身份）**：(b) 要求 v2 合同（AS3 第 2 参 = 静态槽），与 `drive.py:84` 默认一致；
  v1 下 llc 会拒（实测）。这不是新依赖，是既有 G3-S1 迁移的顺带受益。
- **P-4（签名 Tag 28）**：Tag 28 记录字段为
  `param_count / bitmap / ret / call_abi_major / call_abi_minor`（`P4-SIGNATURE-PROTOCOL-FREEZE.md:75-99`），
  **不含地址空间**。推论（须登记为验收缺口；rev1 已把 §4(b) 的相反表述更正为与本节
  一致——"不同 AS 产生不同记录"是**错的**）：
  - 同一函数把第 2 参由 AS0 改成 AS3，Tag 28 记录**不变**（param_count 与 bitmap 一致）；
  - 因此 **P-4 无法捕获"实参 AS 与形参 AS 不匹配"的跨 TU 错配**。AS 一致性依赖 clang 前端
    诊断（本稿的拒绝层）**同 TU 单点保证**；跨 TU 是盲区且 rev1 已实测坐实（r4'：AS0
    原型调用方 + AS3 定义被调方，lld 链接零诊断通过，`rev1/r3ctu/mismatch.map`）。
  - 若未来放开 (c)，P-4 不会提供任何额外防线——这条要写进 (c) 的风险表。
- **P-4 对 G5 站位的直接碰撞（本役新发现 B0）**：P-4 的原型性检查是**硬错**。两份
  `main.c` 的 K&R `void main()` 与 v2 CRT 的 `_main` 原型记录不一致，
  lld 报 `records '_main' disagree on prototype-ness`。**这意味着 P-4 不是"顺带受益"，
  而是给 82/44 的 T1 新增了一道必须先处理的门。** 结论：S1 必须同时把 `main()` 改
  `main(void)`（D7），否则 G5 关闭后首错立刻变成 B0，看起来像"改写把 demo 弄坏了"。
  （rev1 注：两份**改写副本已含**该编辑——`diff` 证实各 +1 文本编辑；上游语料未动，
  S3 的期望清单中 B0 只适用于"未改写语料"路径，不得再对改写副本期望 B0。）
- **PM 裁定 #1/#2**（v1 deprecated、默认全 v2）已使 (b) 的依赖成立，无需新裁定。

### 5.3 存储/链接面

- `.mcs251.xdata_init` v1 记录（`XDATA-CODE-DESIGN-SUPPLEMENT.md` §7.3）对
  `DmaTxBuffer` 零初始化不产生载荷；(b) 不改对象初始化方式。
- XSEG 的 24 位指针初值门禁（同 §7.5）只作用于**存储的指针初值**；82 的 `DmaTxBuffer`
  是数组对象而非指针容器，不受影响。

## 6. 切片（并行-串行边界）与测试矩阵

### 6.1 切片

若 PM 批准走 (b)（**零产品源码**）：

| 片 | 内容 | 依赖 | 可并行 |
|---|---|---|---|
| S1 | 改写器/语料：新增 `xdata-param` 规则（`canfd.c`/`canfd.h` 全量 `CANFD_TypeDef *`→`CANFD_TypeDef xdata*`，加 `uint8_t xdata *` 参数位点与 82 `main.c` 的 `pCanRx`/cast 两处），落 82 的 **29** 处 + 44 的 **24** 处 AS 编辑（rev1 计数，含 pCanRx 修复）；同时把两份 `main()` 改 `void main(void)`（消 B0，文本合计 30/25）；逐条记 ledger note | 无 | 与 S2 并行 |
| S2 | lit 加固：补 AS3 形参静态槽 ABI 的 CodeGen 正例（R2/R3，可直接从 `rev1/r3ctu/` 与 `rev1/pCanRx-drift/p14` 固化）+ 同 TU 声明冲突负例（R4a） | 无 | 与 S1 并行 |
| S3 | harness：`drive.py` demo 82/44 的 T1 期望改为"**B1+B2**（82）/ **B1'**（44）独立阻断"的分层报告（改写副本内 B0 已消、B3 已撤回，均不得再出现在期望中；不得把 82 记成 T1 pass） | S1 | 串行在 S1 后 |
| S4 | 记账：`README.md` §8 G5 行、`TIERS.md`、`ledger.json`（补 `main.c` 4 条 + 改写 note） | S1/S3 | 串行 |
| S5（可选，独立战役） | AS3 superset 切片（A-XDATA）：MOVX/DR 等价性证明、DPXL 保持、DMA 可见性 | **PM 新裁定** | 不属本役 |

若 PM 选 (c)：S1 取消，改 4 处冻结文字 + 上述 S2（负例翻正）+ S3 风险实测
（`-O2` 下 AS0 别名写 CODE/XDATA 的对拍），工作量与风险都显著大于 (b)。

### 6.2 测试矩阵

| # | 用例 | 期望 | 现状 |
|---|---|---|---|
| R1 | 产品 lit 回归（零改动承诺） | llvm 167/167、lld 23/23、clang CodeGen 30/30、Sema 24/24 | **本役实测全绿**（§8） |
| R2 | clang 正例：`void f(uint8_t __xdata *p)` + AS3 实参 | accept，IR `ptr addrspace(3)` | **已入库**（2026-09-15 收口）：`clang/test/CodeGen/mcs251-xdata-param-abi.c`（IR 断言 define/call/GEP/store 全 `ptr addrspace(3)` + 全模块无 `addrspacecast`）；Sema 面沿用 `mcs251-xdata.c:14-16` |
| R3 | **独立最小跨 TU 验收链**（rev1 起 R3 不再依赖 demo 82）：TU-A `extern` AS3 原型调用 / TU-B AS3 定义 → 链接 + 断言（i）`_CANFD_Set_DMA_Buff_PARM_2` 类静态槽为 4B OBJECT 落 `.mcs251.dseg`（真实对象已证：readelf `canfd_dma.o`）；（ii）被调方 store 经 `mov 0x84,r1`（DPXL bank 字节保留）+ `movx @dptr,a`；（iii）AS3 对象落 XSEG | **已入库**（2026-09-15 收口）：`clang/test/CodeGen/mcs251-xdata-param-abi.c` 从 `rev1/r3ctu/` 固化——readelf -s：`_fill_PARM_3` **4B** / `_fill_PARM_2` 1B OBJECT、首参寄存器传无 `_PARM_1`；readelf -S：槽落 `.mcs251.OSEG.*`（5B=1+4，链接期并入 DSEG 窗）、`_DmaTxBuffer` 落 `.mcs251.XSEG._DmaTxBuffer`（0x10）；llc -S：B/DPH/DPL 到达 + `ecall`、被调方 `mov 0x84,rN`+`movx @dptr,a`×2。跨 TU **链接面**（lld 需仓库外 CRT，lit 不载）仍由 `rev1/r3ctu/ok.map` 佐证 | 形状齐 + lit 已入库（链接面留 rev1 证据） |
| R4a | **同 TU**：`canfd.h` 原型 AS3 vs `canfd.c` 定义 AS0 | clang `conflicting types` / AS 诊断（可测层） | **已入库**（2026-09-15 收口）：`clang/test/Sema/mcs251-xdata-param-same-tu.c`——AS3→AS0 / AS0→AS3 双向 + 先定义后重声明 + AS4 变体 + 任意参位均 `conflicting types`；调用点负例（AS0 原型 + AS3 实参）`changes address space of pointer`；一致 AS3 声明组正例控制零诊断 |
| R4b | **跨 TU**：调用方按 AS0 原型编译、被调方 AS3 定义 | **登记为 P-4 已知盲区**（Tag 28 无 AS 维度；rev1 实测链接零诊断，`rev1/r3ctu/mismatch.map`）。不改线格式则无链接期诊断可承诺（D5），仅要求 ledger 记录该盲区 | 盲区已实测坐实；处置待 D5 |
| R5 | 负例保持：AS3↔AS0/AS4 隐式仍拒 | `changes address space of pointer` | 已有 `mcs251-xdata.c:62-69` 等 |
| R6 | 端到端：82 改写后 XSEG 分配 | map 中 `DmaTxBuffer` 落 XSEG，XINIT/XSEG 记录正确（**仅放置面**；rev1 降级——map 不能验收"CPU 写入载荷"的通道正确性，写经 MOVX 落 XDATA 且 DMA 可见须由 R3 的 IR/asm 断言 + A-XDATA 运行时切片覆盖） | 放置面可离线验收；通道面移交 R3/A-XDATA |
| R7 | 原型一致性：K&R `void main()` vs v2 CRT 原型记录 | 硬错（当前）；改写后过 | 本役实测 `prototype-ness` 文案；B0（改写副本内已消） |

## 7. 风险与 PM 决策点

### 风险

1. **(b) 的 ABI 面未端到端验证**：AS3 形参走 v2 静态槽（`_f_PARM_2`）的 24 位 bank
   保留没有专门 lit（R3；rev1 已给出独立最小跨 TU 验收链的全部形状并实测，lit 待固化）。
   若静态槽在链接后丢掉 bank 字节，症状是**静默错 bank**（与 X2-1 修复那类缺陷同型），
   优先级高。
2. **(b) 不使 82/44 T1 转绿**：改写副本内剩 B1（82 canfd.o 196B / 44 main.o 1228B）与
   B2（CSEG）接管首错（B0 已随副本消除、B3 已撤回，§4 表）。若把 G5 关闭写成
   "82 修好"，是记账不诚实。
3. **(a) 的通道语义**：一旦有人为了省事用显式 cast 收尾（**包括函数体内的局部指针**，
   rev1 P14 已实证该形态会静默把 store 挤到 DR 通道），DMA 写通道从 MOVX 变 DR
   且无证明——应在本稿与 ledger 中**明写禁止**。
4. **(c) 的通道等价性缺口（rev1 更正论证）**：AS3(MOVX@DPTR/DPXL) 与 AS0(DR) 通道
   不同，AS4 的读取等价证明不可复用（`RUNTIME-AS-PTR-DESIGN-A.md` §1.5）；
   AS4/CODE 的写保护漏洞（同文 §2.1）**不构成 AS3 的拒绝理由**，AS3 可写、无该漏洞
   形态。且 P-4 不提供 AS 维度防线（§5.2），(c) 必须自带 load/store/bank/宽访问拆分/
   DMA 可见性的等价性证明与实测。
5. **改写闭包的口径风险（rev1 修正）**：只改"看起来报错的 3 个函数"会在
   `canfd.c:605/608` 立刻再报（本役实测）；但闭包必须按**去注释后的活调用图**计算——
   初版"20 个全部在闭包内"是被注释调用虚增的（82 真闭包 15、44 真闭包 17，§3.2）。
   `rewrite.py` 采用**全量 20 机械规则**可同时规避"改一半"与"闭包计算错"两类风险；
   若走最小闭包口径，须以 `rev1/closure/` 的脚本产品为准并随语料重新生成。
6. **记账漂移**：`ledger.json` 键路径 `demos["82-CANFD使用DMA收发测试"].t0_log` 的
   4 条 vs 实测 8 条，且 README §8 的"命中 demo 44,82"
   与 G1-4 的 `addr-space ×2` 口径需对齐；B0 是全新发现，任何清单里都没有
   （另：语料级 K&R `main()` 还有 `76-串口绘图/wave.c:60`，见 D7 范围）。

### PM 决策点（rev1 重整：D1 并入 D4；D2 表述修正；D3 绑定验收；D5 同步 R4；D6/D7 明确范围）

| # | 决策点 | 选项 | 建议 |
|---|---|---|---|
| D1（含原 D4） | G5 的处置路线；是否接受 (a) 作为过渡（仅消 T0 错误） | (a) cast 改写（含"仅作过渡"变体）/ (b) AS3 形参改写 / (c) 放开 superset | **(b)**；**(a) 连过渡也不接受**（语义漂移不可静默，且 P14 证明 cast 型漂移可发生在任何局部指针上）；(c) 另立切片 |
| D2 | 是否现在启动独立 xdata-DMA 切片（A-XDATA） | 是 / 否 | **否（优先级原因，非依赖原因）**——rev1 修正表述：A-XDATA 的通道等价性验证（load/store/bank/宽访问/DMA 可见性）可用最小探针矩阵独立开展，**不依赖**"先清完整 demo 82 的 B1/B2"；完整 demo 只在需要端到端验收语料时才相关，那是充分非必要条件。当前先收 (b) + R2/R3/R4a 更划算 |
| D3 | G5 行是否改记为"改写可规避"（绑定验收条件） | 是（按下述条件）/ 保留 gap | **改记为 gap=改写规避**，验收条件（rev1 绑定）：(i) 修正版 (b) 副本（82 29+1 / 44 24+1 处，含 pCanRx 修复）clang+llc obj 全过（rev1 已实证）；(ii) R2/R3/R4a lit 入库；(iii) ledger 补 `main.c` 4 条 + 改写 note、README/TIERS 同步。备注写明"82 T1 剩 B1/B2、44 剩 B1'（独立阻断）" |
| D4 | （并入 D1） | — | — |
| D5 | P-4 是否补 AS 维度字段 | 是 / 否（前端同 TU 单点） | 建议**登记为 P-4 的已知盲区**，本役不改线格式。**rev1 同步修正 R4 承诺**：不改线格式即无跨 TU AS 错配的链接期诊断可承诺，R4 拆为 R4a（同 TU clang 负例，可测）+ R4b（跨 TU 盲区，仅登记 + r4' 实测佐证） |
| D6 | demo 44 是否随本役改写；范围是否仅限 82/44 | 是（44 同源 24+1 处）/ 否（只改 82） | **是**。范围**天然仅限 82/44**：语料中含 CANFD 源（`canfd.c`/`canfd.h`）的只有这两个 demo，`xdata-param` 规则不触及其它 demo |
| D7 | `void main()` → `void main(void)` 算改写还是语料修正；范围仅限 82/44 还是语料级 | 改写器统一处理（仅 82/44）/ 语料级统一（82/44 + 76/wave.c）/ 保留并记独立缺口 | **改写器统一、本役仅 82/44**（B0 与产品能力无关，同族 demo 61 已是 `void main(void)`）；`76-串口绘图/wave.c:60` 为语料级同型位点，**登记为独立缺口**随其后役处理，不并入本役 |

## 8. 基线（2026-09-15 实测，HEAD `076cb61ed`）

| 套件 | 命令 | 结果 |
|---|---|---|
| llvm | `cd /home/liu/build-mcs251 && ./bin/llvm-lit test/CodeGen/MCS251` | **167/167 PASS** |
| lld | `cd /home/liu/build-mcs251-lld && ./bin/llvm-lit .../lld/test/MCS251` | **23/23 PASS** |
| clang CodeGen | `cd /home/liu/build-mcs251-s1 && ./bin/llvm-lit --filter mcs251 .../clang/test/CodeGen` | **30/30 PASS**（6153 discovered，6123 excluded） |
| clang Sema | 同上 `.../clang/test/Sema` | **24/24 PASS**（1430 discovered，1406 excluded） |

- 探针二进制快照：clang=`build-mcs251-s1` 2026-09-15 10:50、llc=`build-mcs251`
  2026-09-15 13:15、lld=`build-mcs251-lld` 2026-09-15 03:04。
- 与 G3 稿（HEAD `f362d88c7`，llvm 164、lld 22、clang 28/24）的差异来自 G2/B-S2/B-S3
  批次新增 lit（+3 llvm、+1 lld、+2 clang CodeGen），**非本役引入**。
- **工作树未提交资产（4 项，只读，非本役所写）**：
  - ` M validation/mcs251-runtime/src/mcs251_printf.c`（B-S4 役）；
  - `?? validation/mcs251-isr/g3-demo61-four-tier.sh`（G3 役）；
  - `?? validation/mcs251-isr/realhw-demo/release-pre-g1-20260910/`；
  - `?? validation/mcs251-models/proposals/G3-STATIC-PTR-DESIGN-draft.md`（G3 役）。
  本役唯一入仓产物是本文（未跟踪）+ `GAP-G5-PROGRESS.md`/`GAP-G5-PROBES/`（仓库外）。

## 9. 涉及文件清单

- **预计修改（若批准 (b)，均非产品源码；rev1 计数）**：
  `/home/liu/LLVM_STC32/mcs251-demos-rewritten/src/82-CANFD使用DMA收发测试/{canfd.c,canfd.h,canfd_dma.c,main.c}`（**30 处文本编辑**：AS 29 = 20+4+1+4，含 `pCanRx`/cast 修复，另 `void main(void)` 1 处）、
  `src/44-CANFD1-CANFD2同时使用收发测试/{canfd.c,canfd.h,main.c}`（**25 处**：AS 24 = 20+4+0，另 `void main(void)` 1 处）、
  `tools/rewrite.py`（新增 `xdata-param` 规则 + `main()` 原型规则 + ledger note）、
  `tools/drive.py`（S3 分层报告，不把 82 记成 T1 pass；期望 B1/B2，不含 B0/B3）、
  `README.md` §8 G5 行 / `TIERS.md` / `ledger.json`。
- **预计新增（若批准 S2）**：
  `clang/test/CodeGen/mcs251-xdata-param-abi.c`（R2/R3，从 `rev1/r3ctu/` 固化）、
  `clang/test/Sema/mcs251-xdata-param-same-tu.c`（R4a 同 TU 冲突负例），
  可能扩展 `llvm/test/CodeGen/MCS251/xdata-*.ll`（R3 静态槽/bank 字节断言）。
- **明确不动**：`llvm/lib/Target/MCS251/**`、`lld/MCS251/**`、`clang/**`
  （除非 PM 选 (c)：`clang/lib/Basic/Targets/MCS251.h:85-103` + 4 处冻结文字）。
