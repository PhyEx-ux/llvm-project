# G8：DSEG 128B 直达页窗口缺口（10 个 demo）调查设计（草案 rev-1）

**日期**：2026-09-15（rev-1 修订：Alice 设计评审 7 组 CHANGES REQUESTED）
**状态**：设计提案（待复审 → PM 裁定后实施；本文不授权实施）。
**工作树**：`/home/liu/LLVM_STC32/MCS251`，HEAD `13d420b7a`（只读；本文不改产品源码）。
**证据目录**：`/home/liu/LLVM_STC32/GAP-G8-PROBES/`（探针脚本 + 原始日志 +
机器可读 JSON）。进度：`/home/liu/LLVM_STC32/GAP-G8-PROGRESS.md`（含 rev-1 修订节）。
**前置材料**：`DF4-EDATA-DESIGN.md`（EDATA 放置底稿）、`TIERS.md`、
`IMPL-G2S4-PROGRESS.md` §G8 前置检查、`DESIGN.md` §B.1/§B.6、手册
`manuals-md/STC32G/ch11-*.md` §11.2.2。

**rev-1 相对 rev-0 的修订摘要**（Alice 7 组）：

| # | 组 | rev-1 处置 |
|---|---|---|
| R1 | 契约缺口表述 | §0.3/§2.3/§3(a2)/§6.2-D2 改为：flex 是**向契约语义收敛的兼容过渡**，不等于直接兑现 Tag13=8；须 PM 裁定 placement 语义（InternalMovable 低窗优先 vs InternalExtended 直接 EDATA） |
| R2 | flex 算法实质缺陷 | §3(a2)/§3(b1) 重写为**失败回退重排**机制 + 候选集 + 逐对象 section 标记；解锁数按 `g8flex.py` 重算（§4 矩阵），"+10" 经复算**成立但依据被替换** |
| R3 | 启动路径漏改 | §2.3/§4-S0/S1：`LinkerCore.cpp:3570-3589` 的 XINIT 目标验证器须扩展 EDATA；u16 walker 可复用；`--edata-end>0xffff` 已被 Driver 拒绝 |
| R4 | b1/b2 收益重算 | §3(b)/(b2)/§4/§6.2-D5：b1、b2 **单独均有真实收益**（b1 清 39/43/45.1/45.2/50；b2 清 39/45.1/45.2/50），删除"单独零解锁" |
| R5 | D4 冲突与决策表 | §3(b1)/§3(d)/§6.2：迁槽限定为失败触发；"冲突 SFR"→"直接寻址误用的兼容风险"；删 BSEG/ISEG 必改与"62 的 DSEG 残余"；D5/D8 重复合并，决策表重编号 D1–D7 |
| R6 | 画像核验 | §1.2（复现输出）/§1.3/§7（P8）：抽查 10/41/44 节与预算一致；只读重链复现 41 `_memcmp` 3-vs-2、43 DSEG.3 12B、44 dseg 196B；62 可容 |
| R7 | 漂移门禁 | §5.4/§6.1/§6.2：三工具在修订期再次漂移（llc 22:16→23:15）；保留"实施前冻结并重跑全批"；"56→66"仅列**待验证预测** |

**口径警告（必须先读）**：`ledger.json`/`TIERS.md` 冻结于 2026-09-15 19:42，而
clang/llc 二进制在测量期间被并行实例重建。**已证实账本与当前二进制不一致**：demo 44
与 demo 82 的 T0 在 19:42 账本记为 gap（addr-space ptr），而本轮 22:06 起的实测为
**pass**（探针 `probe-run.log`）。**rev-1 期间 llc 再次被重建**（见 §5.4）。故本文
TIERS 数字按 19:42 冻结账本引用（作为基线），窗口实测按当前二进制（sha256 见 §5.4）。
实施前必须重冻结三二进制并重跑全批。凡两者冲突处均已标注。

---

## 0. 结论与裁定摘要

1. **真实超窗 demo 为 13 个**（不是 TIERS 记账的 10 个）：`10/11/33/39/41/43/44/
   45.1/45.2/50/55/56/60`。TIERS 的 "DSEG window" 10 行是**首错分类**的产物；
   `41/43/44` 的首错被 CODE/签名协议遮蔽，DSEG 是**次级残余**（§1.2 已复现）。
   `62` 的 DSEG 实际可容（`l_DSEG=0x51`，81B），其 gap 属 CODE 窗口，**不在本缺口
   范围**——rev-0 曾称 S1"另清 62 的 DSEG 残余"，**该表述已删除**（62 无 DSEG 残余）。
2. **128B 上限不是当前 v2 codegen 的硬件必需，而是继承自经典 8051 直接页的
   链接策略**。实测：v2 下所有 DSEG/OSEG/参数槽访问都是
   `mov drN,#<24位绝对地址>` + `mov @drN`（间接 @WR），**没有任何一条 dir8
   直接寻址指令**被用于具名全局或槽。硬件上低 128B 与高 128B 同样可间接访问
   （手册 §11.2.2），0x80–0xFF 的直接寻址被 SFR 占用。因此放宽窗口在**硬件与
   当前访存路径上都可行**；真正的约束是 ABI/契约语义与启动清零范围。
3. **已有机制未接通，但接通性质须裁定（R1）**：v2 契约 `1,2,32,8,1` 的 Tag 13
   `DefaultPlacement=8` = `Placement_InternalExtended`
   （`MCS251Attributes.h:224-241`）在 `DESIGN.md` §B.1 表里是 XSmall
   「默认 AS0 对象放置到 EDATA 区」。**注意语义分层**：`DESIGN.md:525-553`
   （§B.6.1/§B.6.2）明确把放置语义分成两档——
   * `InternalMovable`（Tag=1）：**先低 direct 窗 first-fit，失败才整对象迁 edata**；
   * `InternalExtended`（Tag=8）：**按 edata 分配**。

   本文 D2 推荐的 **flex（低窗失败才迁）在机制上属于 InternalMovable 的分配顺序**，
   **不是** InternalExtended 的直接语义。故本方案**不得表述为"直接兑现 Tag13=8"**：
   它是**在 v2 现行合同（Tag13=8）下，把对象实现从"全部低窗 DSEG"向契约允许的
   EDATA 语义收敛的兼容过渡**。由此产生一个**必须由 PM 裁定的契约问题**（§6.2-D2）：
   是（i）把 flex 作为过渡并**后续把 Tag13 语义改为 InternalMovable**，还是
   （ii）保留 Tag13=8 并实现 InternalExtended 的"直接 EDATA 分配"（对既有镜像
   有布局扰动，见 §4 uncond 列）？**在裁定前，本方案以选项门控的过渡形态存在，
   不宣称合同语义已闭环。**
4. **推荐**：主案 = **方案 (a2) 自动 AS0 EDATA 放置**（复用已声明的
   DefaultPlacement=8 的**过渡接法**，不改指针宽度、不需 AS8/v2 新协议），
   以**失败回退重排**机制清掉全部 10 个 TIERS DSEG 首错行（§4 复算）；
   次案 = **方案 (b1) 非叶参数槽迁 EDATA**，它**单独即清 39/43/45.1/45.2/50**，
   并与主案合取清全部 13 个 DSEG 缺口 demo 的窗口层。DF4 的**显式 AS8**（方案 a1）
   作为长期 ABI 完整路线保留，但**不是**解锁这 13 个 demo 的最短路径（需改写 demo
   源码；§3(a1)）。
5. **诚实边界**：G8 主案使 **10 个 TIERS 记账行**（10/11/33/39/45.1/45.2/50/
   55/56/60）的 DSEG 窗口层可容。`41` 还需 P-4 `_memcmp` 签名 + CODE 窗口；
   `43/44` 还需 CODE 窗口（其 DSEG 残余由 G8 清除，但首错在 CODE）。
   **G8 不等于这三个 demo 变绿**。

---

## 1. 现状与精确画像

### 1.1 窗口的四类占用与实测口径

`[0x0000,0x0080)` 窗口被四类占用（`lld/MCS251/LinkerCore.cpp:2576-2657` 的
`layoutData()` 顺序）：

| 类 | 来源 | 计费方式 |
|---|---|---|
| `REG_BANK_0` | 每对象 `.mcs251.REG_BANK_0`（8B） | overlay 组，按 bank 最大尺寸计**一次**（:2601-2614） |
| `BSEG_BYTES` | v2 CRT 的 16B 位寻址字节保留 | 硬窗口 `[0x20,0x2f]`（:2590） |
| `DSEG` | `.mcs251.dseg`（普通全局）+ `.mcs251.DSEG.<n>`（非叶参数槽区） | first-fit 于 `[DsegStart,DsegEnd-1]`（:2653-2655） |
| `OSEG` | `.mcs251.OSEG.<n>`（叶参数槽区） | overlay 组 first-fit 同窗（:2656） |

**口径修正**：`IMPL-G2S4-PROGRESS.md` 的旧表按「自有 DSEG + runtime 80B +
REG8 + BSEG16 + OSEG4」算术加总，把 OSEG 与 DSEG 当不相交、把 REG/BSEG 当逐
对象累加。实测按 first-fit 布局计（`g8sim.py` 复现真实 lld，18/18 demo 判定
与真实链接一致）。另：旧表用 **-O0 runtime（printf DSEG=80B）**，当前
`drive.py:532` 用 **-O2（printf DSEG=68B）**，故 demo13/48/59 的「超 4B/1B/4B」
在当前链下不成立（实测 120/117/99，均可容）。

### 1.2 真实超窗 demo 与构成（当前二进制实测，字节）

`g8split.py` 按对象 section 归属拆分 GLOBALS（`.mcs251.dseg`）与 SLOTS
（`.mcs251.DSEG.<n>`）；REG/BSEG/OSEG 按 §1.1 计费。**R6 抽查已复核**：
10/41/44 三例预算与 `g8budget.py` 日志逐项一致（10: REG8/BSEG16/OSEG0/
GLOBALS131/SLOTS0=155；41: 8/16/4/101/358=487；44: 8/16/8/1436/66=1534）。

| demo | 合同 | REG | BSEG | OSEG | GLOBALS | SLOTS | 合计 | 超 | T1 首错 |
|---|---|---:|---:|---:|---:|---:|---:|---:|---|
| 10 | v2 | 8 | 16 | 0 | 131 | 0 | **155** | 27 | DSEG |
| 11 | v2 | 8 | 16 | 0 | 131 | 0 | **155** | 27 | DSEG |
| 33 | v2 | 8 | 16 | 4 | 134 | 73 | **235** | 107 | DSEG |
| 39 | v2 | 8 | 0 | 4 | 58 | 64 | **134** | 6 | DSEG |
| 41 | v2 | 8 | 16 | 4 | 101 | 358 | **487** | 359 | P-4 签名（遮蔽） |
| 43 | v2 | 8 | 0 | 4 | 89 | 302 | **403** | 275 | CODE（遮蔽） |
| 44 | v2 | 8 | 16 | 8 | 1436 | 66 | **1534** | 1406 | CODE（遮蔽） |
| 45.1 | v2 | 8 | 16 | 4 | 57 | 61 | **146** | 18 | DSEG |
| 45.2 | v2 | 8 | 16 | 4 | 60 | 65 | **153** | 25 | DSEG |
| 50 | v2 | 8 | 16 | 4 | 53 | 61 | **142** | 14 | DSEG |
| 55 | v2 | 8 | 16 | 4 | 286 | 60 | **374** | 246 | DSEG |
| 56 [ADC-DMA-struct] | v2 | 8 | 16 | 4 | 205 | 56 | **289** | 161 | DSEG |
| 60 | v2 | 8 | 16 | 4 | 281 | 56 | **365** | 237 | DSEG |
| 62 | v2 | 8 | 16 | 4 | 19 | 34 | 81 | 0 | **可容**（CODE 才是真首错） |

对照（已可容，验证口径）：13=120、48=117、59=99、68=100。

**首错遮蔽的实测解除**（`g8unmask.py` / `g8relink.py`；R6 已用当前二进制重链复现）：

```
# 复现命令（只读，私有 build 根）
python3 /home/liu/LLVM_STC32/GAP-G8-PROBES/g8relink.py
```
- `41` → 仍先报 `_memcmp` 参数计数冲突（3 vs 2，`gui.o` vs `printf.o`）；
- `43` → 仍报 `cannot allocate .mcs251.DSEG.3 (gui.o) size 12; largest free 11`；
- `44` → 仍报 `cannot allocate .mcs251.dseg (canfd.o) size 196`；
- `62` → **链接成功**，`l_DSEG=0x51`（81B），确认 **62 无 DSEG 残余、不属 G8**。

即 41/43/44 的 DSEG 超窗是**真实**的次级残余，不是记账假象。原始输出见
`GAP-G8-PROBES/relink.log` / `relink-results.json`（rev-1 新增）。

### 1.3 真实构成：大对象，不是槽

逐对象最大符号（`g8budget.py`，`llvm-readelf -s` 的 Size 列是**十进制**）：

| demo | 最大自有对象 | 大小 | runtime 大项 |
|---|---|---:|---|
| 10 | `_RX1_Buffer[128]`（main.o） | 128 | 无（不引用 printf） |
| 11 | `_RX2_Buffer[128]` | 128 | 无 |
| 33 | `_RX1_Buffer[59]`+`_tmp[50]` | 109 | printf 68 |
| 39 | 9 个 4B 标量 + `_c1.._c3` | 58 | printf 68 + div 8 |
| 41 | `_ColornTab[32]`+`_ColorTab[20]`+`_lcddev[29]` | 101 | printf 68 + 槽 358 |
| 43 | 同上 | 89 | 槽 302 |
| 44 | `_pCan1Rx[576]`+`_pCan2Rx[576]`+`_pstcTx[72]` | 1228 | printf 68 |
| 45.1 | 4×8B BUF | 57 | printf 68 |
| 50 | 4×8B BUF | 53 | printf 68 |
| 55 | 2×`[128]` Buffer + `TX_BUF[8]` | 278 | printf 68 |
| 56 | `_DmaBuffer`（struct） | 192 | printf 68 |
| 60 | `_RX1_Buffer[262]`（裸 `u8` 数组，非 xdata） | 269 | printf 68 |

**关键观察**：10/11/44/55/56/60 的缺口几乎全由**少数几个大数组**造成；
41/43 则由**参数槽区**（358/302B，gui.o 一家占 300+）造成。两类根因不同，
方案须分别覆盖。

### 1.4 与 harness 的关系（G3 已修一类的复核）

- `drive.py:470` 的 `_runtime_for` 已按需链接（G3-S1 诚实修复）。本轮逐 demo
  复核 runtime 选择：**全部有据**（33/39/44/45.x/50/55/56/60 确实 UND `_printf`
  或 `__divulong` 等；10/11/62 不链 printf）。
- `_runtime_for` 仍按**对象**粒度而非**符号**粒度：引用 `printf` 的程序会把
  `printf.o` 整链，其内含的 `_sprintf_PARM_*` 槽区（28B）一并占窗，即便程序
  从不调 `sprintf`。**R4 复算**：删除该 28B 槽后 39/45.1/45.2/50 全部落窗
  （106/118/125/114B，见 §3(b2)）——这是 §3 方案 (b2) 的直接抓手，也是
  45.1/45.2/50 的**唯一**超窗原因（自有 DSEG 仅 57/60/53B）。
- 未发现其他 harness 制造的假失败。10/11 的 `131B` 是**单对象自身**超窗
  （`_RX1_Buffer[128]` + 3 个计数器），与 runtime 无关。

---

## 2. 机制定位

### 2.1 128B 上限的硬件依据（手册亲核）

`manuals-md/STC32G/ch11-存储器…CHIPID.md` §11.2.2（PDF 页 599，书页 559）：

> 内部可 edata-RAM 共 4K 字节，4K 字节低端的 256 字节与 8051 的 256 字节 DATA
> 完全兼容，可分为 2 个部分：低 128 字节 RAM 和高 128 字节 RAM。**低 128 字节
> 的数据存储器与传统 8051 兼容，既可直接寻址也可间接寻址。高 128 字节 RAM
> （在 8052 中扩展了高 128 字节 RAM）只能间接寻址。特殊功能寄存器分布在
> 80H~FFH 区域，只可直接寻址。**

同节：`edata` 变量「单时钟进行 32 位/16 位/8 位读写」；同章 §11.2.1 表：
XSmall 模式「默认变量类型 = edata」。硬件框图（ch01 §1.1.3.1）：STC32G12K128
的 **EDATA (4KB)** 与 **XDATA (8KB)** 是独立模块；G144K246 板级
`EDATA_END := 0x3fff`（16KB，见 `boards/stc32g144k246.mk`）。

**结论**：经典 8051 的「低 128B 直接寻址」是 `dir8` 指令的寻址宽度限制，
而 **MCS-251 的 edata 是高 128B 也可间接寻址的连续 4KB 空间**。0x80–0xFF 的
直接寻址被 SFR 占用，与 RAM 无关。

### 2.2 当前 codegen 的访存路径（决定放宽是否可行）

实测探针（`/tmp/g8*.c`，clang `--target=mcs251-unknown-none -O0 -fmcs251-keil`
`-Xclang -mcs251-memory-contract=1,2,32,8,1`）：

| 对象 | 生成代码 | 说明 |
|---|---|---|
| `int g_counter; int g_arr[40];` | `mov dr0,#...` + `mov @dr0` / `mov @dr0+0x0001` | 24 位绝对地址 + 间接 |
| `unsigned char gb;`（i8 全局） | `mov @dr4` | **i8 也走间接，不用 dir8** |
| 非叶参数槽 `_caller_PARM_2` | `mov dr12,#_caller_PARM_2`（LO8/MID8/HI8 重定位）+ `mov @dr12` | 24 位绝对地址 + 间接 |
| 字面量 `*(volatile u8*)0x30`（v2） | `mov dr4,#0x0030` + `mov @dr4` | v2 下字面量也不用 dir8 |
| 字面量 `*(volatile u8*)0x30`（v1） | `mov 0x30, r0`（dir8） | 仅 v1 兼容布局（ProgramAS=0）走 `LegacyDirect` |
| AS6 SFR | `mov 0x80, r0`（dir8） | SFR 专用路径，与 DSEG 无关 |

代码位置：`MCS251ISelLowering.cpp:2706-2712` 的 `LegacyDirect` 条件
（`AllowDirect && AddressSpace==0 && PtrVT==i32 && ProgramAS==0`）；v2 契约的
`ProgramAS=4`（`MCS251TargetParser.cpp:37-56` 的 `getLayoutDesc`），故
`LegacyDirect` 恒假。槽访问经 `MCS251ISelLowering.cpp:3524-3531`
`parameterSlot()` 走 ExternalSymbol，24 位重定位
（`R_MCS251_LO8/MID8/HI8`，实测 19/19 组）。

**结论**：v2 全部窗口占用者（全局、槽、OSEG）都是 24 位绝对地址 + 间接访问，
**放宽窗口不需要新指令、不需要新重定位、不改变任何指针宽度**。这正是
`DF4-EDATA-DESIGN.md` §9.2 公式 `H = max(0x100, 全部内部占用 exclusive end)`
所预设的模型。

### 2.3 上限的代码位置与放宽机制现状

| 事实 | 位置 | 现状 |
|---|---|---|
| DSEG 硬上限 `0x80` | `lld/MCS251/LinkerCore.cpp:2644-2648` | `DsegEnd` 仅当 `--iram-size` **收窄**时小于 0x80；无任何参数可放宽 |
| `--iram-size` 默认 128 | `LinkerCore.h:31`；`Driver.cpp:390-394` | 实测 `--iram-size=256` 对 demo10 **无效**（仍报 `window [0x0000,0x0080)`）；`--iram-size=64` **收窄**为 `[0x0000,0x0040)` |
| ISEG 窗口 `0x100` | `LinkerCore.cpp:2659-2678` | 存在但**无编译器发射者**（`grep ISEG llvm/lib/Target/MCS251/` 为空） |
| **无 EDATA 区域** | `classifySection` `:455-589` | Region 集合 = CSEG/XINIT/XDATA_INIT/HOME/VECS/BOOT/OSEG/DSEG/REG/BSEG/BIT_BANK/ISEG/SSEG/DATA_ABS/XSEG，**无 EDATA**（`grep '"EDATA"' lld/MCS251/LinkerCore.cpp` 为空，R6 复核） |
| `--edata-end` | `Driver.cpp:385-389`；`LinkerCore.h:32` | 只进栈容量公式（`:3004-3007`），**不构成放置窗口**。实测 demo10：`--edata-end=0xff/0x100/0x1000/0x3fff` 四种取值**均不改变 DSEG 失败**；demo62 反向：`--edata-end` 降到 `0x500` 以下即触发 `stack capacity is less than 1024 bytes`（`LinkerCore.cpp:3003-3005`），证实该参数只影响栈门禁 |
| **XINIT 目标验证器仅收低窗** | `LinkerCore.cpp:3570-3578` | 见下 |
| 全局发射无条件 DSEG | `MCS251AsmPrinter.cpp:2160` `switchSection(TLOF.getDSEGSection())` | 分类器 `MCS251TargetObjectFile.cpp:44-50` 对一切 `!GV->isConstant()` 返回 `DSEGSection` |
| 槽区选择 | `MCS251AsmPrinter.cpp:1444`（`arg_size()<2 && !isVarArg()` 直接返回）；`:1451-1456`（`Area = Leaf ? "OSEG" : "DSEG"`） | 非叶 → DSEG，叶 → OSEG；两类都占同窗 |

**XINIT 目标验证器缺口（R3，必须纳入 S0/S1 切片）**：`LinkerCore.cpp:3570-3578`
（rev-0 记的 `:3546` 已含在 `:3545-3591` 整块内）在布局后校验每条 XINIT 记录的
目标地址必须落在某个已分配切片内，而条件**只列举三个 Region**：

```cpp
if ((D->Region == "DSEG" || D->Region == "DATA_ABS" ||
     D->Region == "BSEG_BYTES") && ...)
```

新 `EDATA` Region 若不加入该白名单，凡 EDATA 目标记录都会被
`XINIT destination is not within one DSEG slice` 拒绝——即"编译/链接通过、
启动清零校验失败"的隐蔽断层。故 S0/S1 必须把 `EDATA` 加入该条件，并分别验证
**清零（`PayloadSize==0`）与非零复制（`PayloadSize==ObjectSize`）两条路径**。

**u16 walker 可复用、无 >64K 合法 EDATA**：CRT 的 `__mcs251_globals_init`
（`validation/mcs251-firmware/crt-selfstart.asm:88-124`；v2 镜像
`crt-selfstart-v2.yaml:117-127`）逐记录读 u16 目标再 `mov @wr8` 清零/`mov @wr20`
复制，天然支持分散 EDATA；实测 demo10 的 `.mcs251.xinit` 记录为
`u16 target, u16 size, u16 payload`，目标由 `R_MCS251_16` 重定位写入
（`llvm-readelf -r` 显示 4 条 `R_MCS251_16` 指向 `_TX1_Cnt`/`_RX1_Cnt`/
`_B_TX1_Busy`/`_RX1_Buffer`），**无需新 walker**。边界 `Destination+ObjectSize>
0x10000` 拒绝（`:3560-3561`）不构成风险：Driver 已在 `Driver.cpp:387-388` 拒绝
`--edata-end>0xffff`，故 EDATA 地址上限 ≤ 0xffff，**不存在合法的超 64K EDATA**。
此条写入本文以消除疑虑。

**身份与实现不一致（R1：性质须裁定，不得简化）**：`MCS251AsmPrinter.cpp:2024`
用 `Contract->DefaultPlacement` 渲染 Tag 13；`MCS251Attributes.h:224-241`
定义 `Placement_InternalExtended=8` 即 `MemoryModelProfile_XSmall = {32, 8}`。
当前 v2 全批合同第 4 字段正是 **8**。但 `DESIGN.md:525-553` 区分：Tag=1
（InternalMovable）走"低窗优先、失败才迁"，Tag=8（InternalExtended）走"按
edata 分配"。本方案的 flex 属于前者。**故：对象身份声明的是 InternalExtended，
本文机制实现的是 InternalMovable 的分配顺序；这是一次向契约语义收敛的过渡，
是否被接受为"Tag13=8 的等价实现"须由 PM 裁定（§6.2-D2），不得在稿中径称
flex 直接兑现 Tag13=8。**

### 2.4 目标板与 QEMU 的 EDATA 模型

- QEMU `include/hw/mcs51/stc32g.h:23-24`：`STC32G_EDATA_BASE=0x000000`、
  `STC32G_EDATA_SIZE=16KiB`；`:25-26` XDATA 在 `0x010000`、128KiB。
  `hw/mcs51/stc32g.c:96` 把 edata 作为连续 RAM 挂在 0 起。
- 即 QEMU 中 `[0x100, 0x4000)` 与 `[0,0x100)` 是**同一块连续可读写 RAM**，
  EDATA 窗口的 QEMU 验收不需要新机器模型。

---

## 3. 方案空间

各方案的成本用 `g8sim.py`（复现真实 lld 的 first-fit，18/18 判定一致）与
**`g8flex.py`（失败回退重排，rev-1 新增）**评估。**rev-0 的致命缺陷（R2）**：
rev-0 用"把 GLOBALS/SLOTS 整类从窗口删除"来模拟迁移，这**不等于** D2 所述的
flex（低窗失败才迁）。按现输入顺序逐 section 独立模拟，39/45.1/45.2/50 的
**全局先放置成功、后续槽才失败**，删除模型由此**高估**了 (a2) 单独效果。
rev-1 因此显式规定下列机制，并按它重算。

### 迁移机制（rev-1 形式化，取代 rev-0 的"删除"模型）

在 `layoutData()` 的 DSEG/OSEG 序列中规定**失败回退重排**（deterministic）：

1. 按 `AllSections` 输入顺序对 `Region=="DSEG"` 的 section 做低窗 first-fit
   `[DsegStart,DsegEnd-1]`，随后放 OSEG overlay 组；
2. 任一 section（或 OSEG 代表）无法放置时，从**候选集**中选一个迁移：
   * 若失败者本身是候选 → 迁它；
   * 否则迁**剩余候选里最大的**（并列取输入序在前者）；
3. 把选中 section 整体 first-fit 到 EDATA 窗口 `[max(0x100,DsegStart), EdataEnd]`
   （整对象不拆分，`DESIGN.md` §B.6.3），随后**从头重跑**第 1–2 步；
4. 候选集耗尽仍失败 → 链接失败（不静默、不退化）；EDATA 放不下 → 链接失败。

**候选集与逐对象标记（R2）**：迁移单位是**一个输入 section**（一个 TU 的
`.mcs251.dseg`，或一个函数的 `.mcs251.DSEG.<n>`）。谁标记"可迁 EDATA"：

- **编译端（AsmPrinter）按 section 打标**：在 `emitGlobalVariable` 的可变分支
  （`:2160`）与 `emitParameterSlots`（`:1456`）发射时，对**无显式 AS 的 AS0**
  可变对象/非叶槽 section 置一个能力标记。实现形态二选一，须在 S0 定：
  （i）新 ELF section flag（如 `SHF_MCS251_EDATA_MOVABLE=0x20000000`，与
  `SHF_MCS251_OVERLAY=0x10000000` 同族）；或（ii）新 v2 能力位/放置协议字段。
  注意 `classifySection`（`:533-538`）目前对 DSEG 要求
  `Flags == (SHF_ALLOC|SHF_WRITE)` **精确相等**，新增 flag 会被拒——S0 必须同步
  放宽该判据只对标记位做掩码比较。
- **lld 按标记识别**：`Region=="DSEG" && HasEDataMovableFlag` 才进候选集；
  `__data`(AS1 strict direct) 对象、v1 对象、XINIT/absolute/overlay 一律**无标记、
  永不迁移**（AS1 的门禁另见 `MCS251ISelLowering.cpp:1316-1327`：常量地址
  ≥0x80 直接 fatal）。
- 若 PM 选择（ii），则同时需要在 A4 身份注册表登记，且 lld 的
  `V2ComparedTags`（`:842-871`，Tag 13 故意不参与跨对象比较）须复核。

### 方案 (a1)：DF4 显式 AS8 `__edata`（DF4 底稿完整化）

- **内容**：`__edata` → AS8 静态对象 → `.mcs251.edata.*` → lld 独立 EDATA
  窗口 first-fit。DF4 底稿已给出完整协议：对象分类表（§2.2）、
  `GlobalPlacementDesc`（§3.1）、最小 v2 对象/引用协议（§4）、
  连续清零 `[0x100,H)`（§6）、栈公式（§9.2）。
- **缺口（DF4 底稿对 G8 的不足）**：
  1. DF4 §0.1 明确「**首批只新增显式 `edata`/AS8 静态对象**；普通 AS0 对象
     不因本批次自动迁移」。而 G8 的 13 个 demo **全部是普通 AS0 对象**——
     demo 源码写的是裸 `u8 RX1_Buffer[...]`（无 `xdata`/`edata` 限定）。
     故 (a1) 对 G8 **零解锁**，除非同时改写 13 个 demo 源码（越界：并行实例
     拥有 `mcs251-demos-rewritten/src/`，且改写语料不在本缺口授权内）。
  2. DF4 §11 首批总量 13–22 人日，主要成本在最小 v2 身份/对象引用协议
     （7–12 人日）——而 v2 身份**已在 G3/W4 落地**（实测 e_flags=0x102、
     恰一 `.mcs251.attributes`、Tag 28 签名阵列在案）。DF4 §0.3/§16 的
     "v1 现状" 已过期，成本估计需重估。
  3. DF4 未讨论**非叶参数槽区**（`.mcs251.DSEG.<n>`）是否可迁——这是 41/43
     的主因（358/302B），也是 DF4 分类表 §2.4 未列的一类。
- **ABI 影响**：新增 AS8 指针类型（16 位）；P-4 签名需为 AS8 参数登记
  （`hasOrdinaryPointerABI` 已含 AS8，`MCS251ISelLowering.cpp:3537-3548`）；
  A4 身份需登记新 placement 或新能力位；值 ABI 槽语义不变（槽仍 4B）。
- **风险**：高（新协议面 + 源级限定）。**结论：不作为 G8 主案。**

### 方案 (a2)：自动 AS0 EDATA 放置（推荐主案）

- **内容**：让**普通 AS0 可变全局**在低 128B 放不下时（按上述失败回退重排）
  整体落入 EDATA 区 `[0x100, EdataEnd]`。对象仍为 **AS0、4B 指针**，
  **不新增地址空间、不改指针宽度、不改 P-4 签名**。
- **为什么这是最短路径**：访问路径已是 24 位间接（§2.2）；QEMU EDATA 连续
  （§2.4）。需要动的只有四处：
  1. **lld 窗口/Region**：新增 `EDATA` Region + `classifySection` 分支 +
     `layoutData()` 的失败回退重排 first-fit 于
     `[max(0x100,DsegStart), EdataEnd]`；`H` 已由 `reserve()` 的 `StackH`
     自动包含（`:2173`），栈公式 `First=align_up(H,16)+16` 自动生效。
  2. **lld XINIT 验证器**：`LinkerCore.cpp:3570-3578` 白名单加入 `EDATA`
     （R3；否则 EDATA 目标记录被拒）。
  3. **AsmPrinter**：`emitGlobalVariable` 的可变分支（`:2160`）不再无条件
     `getDSEGSection()`，改由分类结果选 `.mcs251.dseg` 或新 `.mcs251.edata`，
     并按 §3 机制打"可迁"标记；XINIT 记录不变（仍 u16 目标，EDATA 在 00 段内）。
  4. **CRT**：**无需改动**——`__mcs251_globals_init` 的 XINIT 稀疏清零已覆盖
     任意目标（`:89-124`），新 walker 不必要。唯一边界是 XINIT 的 u16 目标
     上限（`:3560`），而合法 EDATA ≤ 0xffff（Driver 已拒超限）。
- **与 DF4 的关系**：这是 DF4 §2.5「编译期阈值放置」与 §5.5「flex 迁移」的
  **收窄特例**（仅 AS0、仅静态、仅链接期失败回退 first-fit），DF4 原列为
  「后续批次」。G8 建议**提前并收窄**。
- **契约性质（R1，重要）**：本方案**不是**直接实现 Tag13=8 的
  InternalExtended 语义（"按 edata 分配"），而是实现 InternalMovable 的
  "低窗优先、失败才迁"分配顺序（`DESIGN.md:525-553`）。它在**合同仍声明 Tag13=8
  的前提下运行**，属于**向契约语义收敛的兼容过渡**。由此产生 §6.2-D2 的裁定需求：
  * 若裁定为「过渡正当，后续把 Tag13 语义改判/新增 InternalMovable」→ 本方案成立；
  * 若裁定为「Tag13=8 必须按 InternalExtended 直接 EDATA」→ 须改走
    §4 的 `uncond` 列（**对既有可容 demo 有布局扰动**），并重评回归面。
  **在裁定前不得宣称合同语义已闭环。**
- **ABI 影响**：无签名变化；无指针宽度变化；值 ABI 槽语义不变。
  唯一语义变化：AS0 对象地址可能 ≥ 0x100——而 AS0 是 4B canonical 24 位容器
  （`DESIGN.md` §B.1），合法。**注意**：`__data`（AS1 strict direct）对象
  必须继续拒绝迁出（`MCS251ISelLowering.cpp:1316-1327` 已门禁 AS1<0x80），
  EDATA 放置仅作用于无显式 AS 的已标记 AS0。
- **风险**：中。主要是 (i) 现有 v1 链/历史资产的可复现性——须以合同或选项
  门控，默认关闭或仅 v2；(ii) SDCC 互操作：跨模块槽共享假设
  （`MCS251ISelLowering.cpp:3892`「leaf functions share the same OSEG storage」）
  不受影响，但若迁非叶槽则需重审（见 b1）；(iii) map/边界符号口径需同步。

### 方案 (b)：槽区压缩 / 非叶槽迁 EDATA

- **b1 非叶参数槽区迁 EDATA（R4/R5 修正）**：`AsmPrinter.cpp:1456` 把非叶槽从
  `DSEG` 改为可按标记迁移的 section。槽访问已是 24 位间接（§2.2），
  **无需改 ISel/ABI**。**rev-0 称其"单独不足以清任何 demo 的首错"错误**：
  按 `g8flex.py` re-layout，**b1 单独即清 `39/43/45.1/45.2/50` 五例**
  （41 仍需全局迁出；10/11/33/55/56/60 的失败者是全局、非槽，故 b1 无助）。
  其中 43 的 DSEG.3（12B，gui.o）在 `g8relink.py` 已实测为真实首错（CODE 遮蔽下）。
  b1 仍是 41 变绿的必要条件之一。
  **触发条件限定（R5）**：迁槽**只在低窗 first-fit 失败时**发生（同一 flex 机制），
  不在发射端无条件改区。无条件把非叶槽指向 EDATA 会改变当前已通过 demo 的镜像，
  与 §5.3/§6.3 的"逐字节不变"直接冲突，故明确禁止。
- **b2 runtime 变参家族按需拆槽（R4/R5 修正）**：`printf.o` 内含
  `_printf_PARM_2..7`（24B）与 `_sprintf_PARM_2..8`（28B）两块独立槽区。只引用
  `printf` 的程序（33/39/44/45.1/45.2/50/55/56/60/13/48/59/68）白付 28B。
  按符号粒度拆对象（或让 lld 只分配被引用符号所在槽区）可回收 28B。
  **R4 复算**：删除该 28B 槽后，**39=106、45.1=118、45.2=125、50=114（均 FIT）**
  ——即 **b2 单独清 39/45.1/45.2/50 四例**，**不是 rev-0 所称的"零收益/仅增余量"**。
  b2 与 S1 对这四例**重叠**（S1 也清它们），故 b2 的增量价值在于
  **作为不引入 EDATA 窗口的替代路径**（若 PM 否决 flex 而保留低窗策略，b2 仍能
  清这四例），代价是触 runtime 源 `mcs251_printf.c`（归并行实例所有，只读）
  且 §4.7 冻结的槽面（printf 槽 6×4B、sprintf 槽 7×4B、Tag 28）不得改动——
  **拆分对象不改槽面，只改链接粒度**，但需确认 Tag 28 签名按对象发布不产生冲突。
- **b3 槽合并/复用**：非叶槽不可跨调用树复用（需 survive 嵌套调用，
  `AsmPrinter.cpp:1449-1450` 注释）；叶槽已 overlay。进一步压缩空间有限，
  且动 ABI 冻结面。**不建议**。

### 方案 (c)：runtime 按需链接（B-S4 已做）——剩余真实构成

G3-S1 已实施按对象按需链接（`drive.py:470-529`，含 Alice R3 的
`und -= defs` 修复）。本轮复核：**runtime 选择全部有据**（§1.4）。
剩余超窗**不是** harness 假失败：10/11 与 runtime 无关；45.1/45.2/50 的
超窗源于 printf.o 的 sprintf 槽（b2 可回收）；其余由自有大对象造成。
**结论：(c) 已用尽，无进一步空间。**

### 方案 (d)：直接放宽 DSEG 窗口到 `0x100`（经典 8052 256B）

- **内容**：把 `DsegEnd` 从 `0x80` 改为 `0x100`（或由 `--iram-size` 放宽）。
- **实测解锁**（`g8flex.py` d 列）：10/11/33/39/45.1/45.2/50 共 **7**；
  **不解** 41/43/44/55/56/60（需要 0x400/0x1000 级）。
- **为什么不如 (a2)**：
  1. **直接寻址误用的兼容风险（R5 修正措辞）**：0x80–0xFF 的 `dir8` 编码在既有
     约定里指向 SFR（`MCS251ISelLowering.cpp:2447-2469`「Direct constants <= 0xff
     retain the established SFR convention」）。v2 codegen 目前不用 dir8 访问 RAM
     （§2.2），故**并不存在物理冲突**——风险在于**未来任何 dir8 优化、手写 asm 或
     SDCC 互操作把 0x80–0xFF 的 RAM 地址误按 SFR 直接寻址**。rev-0 称"冲突 SFR"
     过强，rev-1 降级为"直接寻址误用的兼容风险"。
  2. 它只解决 7 个，而 (a2) 解决 10 个且语义更干净、上限更高（到 EdataEnd）。
  3. **删除 rev-0 的"须同时改 ISEG 窗口与 BSEG 窗口"**：核查后无依据——
     BSEG_BYTES 是硬窗口 `[0x20,0x2f]`（`:2590`），与 DsegEnd 无关；ISEG 有自己的
     窗口 `[IsegStart,IsegEnd)`（`:2659-2678`），放宽 DSEG 不要求改它，且 ISEG
     无发射者。**rev-1 已删该论断（未证明）。**
- **结论：备选，不推荐。**

### 方案对比（rev-1，`g8flex.py` 复算；括号内为 FIT 的缺口 demo 数）

| 维度 | (a1) 显式 AS8 | **(a2) AS0 EDATA flex** | (b1) 非叶槽迁 EDATA | (b2) 变参拆槽 | (d) 窗口→0x100 |
|---|---|---|---|---|---|
| 缺口 demo 中 DSEG 层 FIT（单独） | 0（需改源码） | **11**（10/11/33/39/44/45.1/45.2/50/55/56/60；含 10 个 TIERS 行） | **5**（39/43/45.1/45.2/50） | **4**（39/45.1/45.2/50） | **7**（10/11/33/39/45.1/45.2/50） |
| 严格"仅迁失败 section、不重排" | 0 | 7（10/11/33/44/55/56/60） | 3（45.1/45.2/50） | — | — |
| (a2)+(b1) 合取 | — | **13**（含 41/43/44 的 DSEG 层） | — | — | — |
| 无条件直放 EDATA（`uncond` 列） | — | 全部缺口可容，但**扰动全部已通过 demo 的布局** | — | — | — |
| 新地址空间 | AS8（16 位） | 无 | 无 | 无 | 无 |
| 指针宽度变化 | 有 | 无 | 无 | 无 | 无 |
| P-4 签名变化 | 需登记 AS8 | 无 | 无 | 无 | 无 |
| lld 改动 | 大（新协议） | 中（新 Region+窗口+**XINIT 验证器**） | 小（复用） | 小 | 小 |
| 源码/产物改动 | 需 `__edata` 限定 | 无 | 无 | runtime 拆分 | 无 |
| 硬件依据 | 充分 | 充分 | 充分 | 充分 | 充分（但见直接寻址误用风险） |
| 契约语义 | 新增 AS8 | **过渡，须 D2 裁定** | 同 (a2) | 不动契约 | 不动契约 |
| 风险 | 高 | 中 | 中 | 低 | 中 |

### 推荐

**主案 (a2) + 次案 (b1)**，均以「EDATA 窗口」为共同基础设施：
先落地 lld 的 EDATA Region + 窗口 + **XINIT 验证器扩展** + AsmPrinter 的 section
选择与**可迁标记**（(a2) 的 1+2+3），再把非叶槽区（b1）指向同一 Region。
DF4 的 (a1) 显式 AS8 与连续清零作为**后续 ABI 完整化路线**保留，不与 G8 抢主路径。
**D2 的契约裁定（§6.2）是本方案放行的前置条件。**

---

## 4. 切片划分（并行-串行边界）与解锁映射

`g8flex.py` 逐方案实测（"FIT" = 按失败回退重排落窗成功）。**rev-1 修正**：
rev-0 的"删除整类"模型给出的 (a2)=10/11/33/39/44/45.1/45.2/50/55/56/60 在
re-layout 机制下**恰好重现**，但该重现**必须依赖本文明示的失败回退重排**；
若实现退化为"仅迁失败者、不重排"（strict 列），(a2) 只剩 7 个、(b1) 只剩 3 个。

| 切片 | 内容 | 依赖 | 解锁 demo | 并行性 |
|---|---|---|---|---|
| **S0** | lld `EDATA` Region + `classifySection`（含新能力标志的掩码判据）+ 窗口 `[max(0x100,DsegStart), EdataEnd]` + **XINIT 目标验证器加入 `EDATA`** + **`LinkerCore.cpp:466-469` 的 `Common` ALLOC-flags 白名单同步放宽**（否则新能力标志在该处先被 "unsupported ALLOC section flags" 拒绝——仅改 DSEG 的 flags 精确比较不足）+ map/边界符号；**暂不接任何发射者** | 无 | 无（纯基础设施） | 串行前置 |
| **S1** | AsmPrinter **先按既有分类发射（仍落 DSEG）并打"可迁"标记**；仅当链接期低窗放置失败回退时，lld 把带标记 section 迁为 `.mcs251.edata`（与 §3 的"仅对 `Region=="DSEG"` 执行低窗尝试/候选识别"一致）；CRT 复用现有 XINIT walker（验证清零与非零复制两路） | S0 | **10, 11, 33, 39, 45.1, 45.2, 50, 55, 56, 60**（全部 10 个 TIERS DSEG 首错 demo）；另清 **44** 的 DSEG 残余 | 与 S2 并行（不同文件块） |
| **S2** | 非叶参数槽区按标记可迁 EDATA（(b1)），**仅低窗失败时触发** | S0 | **39, 45.1, 45.2, 50**（四者 DSEG 为首错，S2 单独即变绿候选）；**43** 的 DSEG 残余（其首错为 CODE，须 S3 才可评估）；为 41 变绿的必要条件 | 与 S1 并行 |
| **S1+S2** | 合取 | S1,S2 | **41/43/44 的 DSEG 层**（仍需 S3+S4 才变绿） | — |
| **S3** | CODE 窗口（41/43/44/62 的首错） | 独立缺口 | 使 43/44 的首错转为可评估；62 变绿 | 与 S1/S2 并行（另一缺口） |
| **S4** | P-4 `_memcmp` 签名冲突（41） | 独立缺口 | 41 的签名层 | 与 S3 并行 |
| **S5（可选）** | runtime 变参家族按符号拆槽（(b2)） | 只读 runtime 源 | **单独清 39/45.1/45.2/50**；亦可为 13/48/59/68 增余量 | 可作为 S1 的替代/精简路径，须 PM 定（§6.2-D5） |

**解锁映射（G8 单独口径，rev-1 复算）**：

| demo | 现 T1 | G8 后 | 备注 |
|---|---|---|---|
| 10, 11 | gap | **pass（T1）** | 单对象 131B；S1 即可 |
| 33 | gap | **pass** | S1（自有 122B 迁 EDATA） |
| 39 | gap | **pass** | S1 或 S2 或 S5；需 re-layout（strict 单迁不足） |
| 45.1, 45.2, 50 | gap | **pass** | S1 或 S2 或 S5（三者皆可单独清） |
| 55, 56, 60 | gap | **pass** | S1（大对象 274/193/269 迁 EDATA） |
| 41 | gap | **gap（转 CODE/签名）** | 需 S1+S2 清 DSEG 层；首错前移到 P-4/CODE |
| 43 | gap | **gap（转 CODE）** | S2 清 DSEG 层；首错前移到 CODE |
| 44 | gap | **gap（转 CODE）** | S1 清 DSEG 层；首错前移到 CODE |
| 62 | gap | **gap（CODE）** | DSEG 本就可容；不属 G8 |

**净效果（预测，非验收承诺）**：G8 主案使 **10 个 TIERS 记账行**
（10/11/33/39/45.1/45.2/50/55/56/60）的 DSEG 窗口层可容，且把 41/43/44 的首错
前移到各自的独立缺口。就 T1 账面而言，若这 10 行在重跑中全部转为 pass，则
T1 由 56/78 行 → **66/78 行（+10）**。**该 "+10" 仅为待验证预测**：它依赖
（i）重冻结后的工具/语料与本次模拟同源，（ii）S1+S2 的实施忠实于本文的
失败回退重排与标记协议。**在 §5.4 门禁完成前不得作为已证明的验收承诺。**

---

## 5. 测试矩阵

### 5.1 编译器与对象（llc/clang）

- AS0 可变全局：≤128B 且低窗可容 → 仍落 DSEG（不改变既有布局）；
  低窗不足 → 落 EDATA；地址 ≥ 0x100 后 24 位重定位（LO8/MID8/HI8）正确。
- **可迁标记**：v2 下无显式 AS 的 AS0 可变全局/非叶槽带标记；`__data`(AS1)、
  显式 AS2/AS3/AS4/AS6/AS8/AS9、v1 对象**不带标记**。
- **AS1 `__data` 对象必须继续落低窗**，即使 EDATA 充足也拒绝迁出
  （沿用 `MCS251ISelLowering.cpp:1316-1327` 门禁）。
- i8/i16/i32、数组、struct、含 padding 对象；align 1/2/4 的对齐。
- 负测：对象 > EdataEnd-0x100 时链接失败（不退回、不静默截断）。
- v1 兼容合同下行为不变（自动迁移仅 v2 或显式选项）。

### 5.2 lld

- EDATA 窗口边界：`[0x100, EdataEnd]` first-fit；`EdataEnd=0xff` 时窗口为空
  （合法，退回失败）；对象整体放置不拆分；宽整数算术。
- **失败回退重排**：确定性复现（同一输入两次链接布局一致）；候选耗尽即失败；
  迁移候选选择规则（失败者是候选则迁之，否则迁最大剩余候选）有单测。
- **XINIT 目标验证器（R3）**：EDATA 目标记录被接受；非 EDATA/非 DSEG/DATA_ABS/
  BSEG_BYTES 目标仍被拒（负测）；**清零路径（payload=0）与非零复制路径
  （payload=size）各一正测**；`Destination+Size>0x10000` 仍拒。
- 与 DSEG/OSEG/REG/BSEG/BIT_BANK/DATA_ABS 共用账本；`H` 含 EDATA 占用，
  栈公式 `First=align_up(H,16)+16`、`Capacity>=1024` 生效（正例/1023 负例）。
- `--edata-end` 语义：仍不重解释为 DSEG 大小；EDATA 窗口上界取该值；
  `>0xffff` 继续被 Driver 拒绝（既有行为，回归锚点）。
- map：新增 `s_EDATA/l_EDATA`；`l_DSEG` 语义不改为「内部 RAM 最末地址」。
- 负测：EDATA 与绝对预留/noinit/保留洞相交。

### 5.3 启动与运行（QEMU 窗口验收）

- **QEMU 验收（本缺口核心）**：demo 10/11/33/39/45.1/45.2/50/55/56/60 全链
  （T0→T1→T2）跑 `qemu-system-mcs251 -M stc32g144k246` 30s 窗口，
  预期 `T2-silent`（LED/无模型外设）或 `T2-output`（有 UART 者）。
  10/11/33/55/60 有串口输出，应可拿到真实 banner/回显。
- **清零正确性**：QEMU 预置非零 RAM（EDATA 区）后启动，证明 XINIT 稀疏记录
  对 EDATA 目标真实清零；访问对象首/中/末字节 + 动态索引。
- 栈哨兵：EDATA 抬高 `H` 后栈不覆盖对象；SPX 精确。
- **对照（与 §6.3 一致，R5）**：**低窗可容的 demo（13/48/59/68）布局必须
  逐字节不变**。此约束只在"迁移由失败触发"时成立——故 S1/S2 均**禁止**
  无条件迁移；任何"直接按 DefaultPlacement 放 EDATA"的变体都须单列为 D2 的
  另一选项并重评回归面。

### 5.4 回归基线（rev-1 实测）

| 测试族 | 结果（rev-1 实测，22:5x–23:2x） |
|---|---|
| `llvm/test/CodeGen/MCS251` | **172/172 pass**（rev-0 记 170；测试数在漂移期增长，见下） |
| `lld/test/MCS251` | **23/23 pass** |
| `clang/test/CodeGen/mcs251*.c`（30） + `clang/test/Sema/mcs251*`（25） | **55/55 pass** |
| `clang/test/Driver/mcs251.c` | **1/1 pass** |
| demo 三档（TIERS 19:42 冻结，78 程序行） | T0 72 pass/6 gap；T1 56 pass/16 gap/6 skip；T2 56 silent/22 skip |
| demo 三档（demo 级 67 非排除） | T1 46 pass/16 gap/5 skip |

复现命令（llc 漂移后重跑，仍全绿）：

```
/home/liu/build-mcs251-s1/bin/llvm-lit llvm/test/CodeGen/MCS251
(cd /home/liu/build-mcs251-lld/tools/lld/test && \
  /home/liu/build-mcs251-s1/bin/llvm-lit -q MCS251)
/home/liu/build-mcs251-s1/bin/llvm-lit clang/test/CodeGen/mcs251*.c clang/test/Sema/mcs251*
```

**二进制身份与漂移（R7，已更新）**：
- `clang-24` sha256 `94704193…47b2`（22:19，与 rev-0 一致）；
- `llc` **`479695f1…e4ac`（23:15）——与 rev-0 记录的 `7b8a4133…c8eb0`（22:16）不一致**：
  llc 在 rev-1 修订期间**再次被并行实例重建**；
- `lld` `7bde9532…bbf5`（09-15 03:04，与 rev-0 一致）。

**漂移门禁（R7）**：三工具 SHA256 中 clang/lld 与 rev-0 指纹一致，但 llc 已变。
**不能追认漂移期间所有测量同源**。本次 R6 的只读重链（`g8relink.py`）与关键
section 抽查在**新旧两版 llc 下复现结果一致**（41 `_memcmp` 3-vs-2、43 DSEG.3
12B、44 dseg 196B、62 `l_DSEG=0x51`；10/33/39/41/43/44/45.x/50/55/56/60 的
section 尺寸逐条相同），但这**只证明所抽查项对本次漂移不敏感**，不足以追认
全批同源。**实施前门禁不变**：冻结三工具 + runtime/CRT 与语料，重跑全批
（含三条测试族与 demo 三档），门禁未过不得开工。

---

## 6. 风险与 PM 决策点

### 6.1 风险

| 风险 | 判断 | 缓解 |
|---|---|---|
| AS0 地址越 0x100 的既有假设 | 中高：任何隐含「AS0 全局 <0x100」的代码/测试/固件都可能失效 | 全仓 grep `0x80`/`0x100` 假设；以合同或选项门控；保留低窗优先（flex） |
| v1 历史资产可复现性 | 中：自动迁移若默认开启会改变历史链布局 | 仅 v2 默认开启，v1 保持字节不变（回归锚点） |
| SDCC 互操作 | 中：跨模块槽/全局约定 | 迁槽（b1）需单列互操作评审；迁全局（a2）风险较低 |
| 清零范围 | 中：EDATA 目标须进入 XINIT 稀疏记录并被验证器接受 | 复用现有 XINIT 稀疏清零；**S0 必须先扩展 `:3570-3578` 白名单**；EDATA 区不依赖 NOBITS 语义 |
| map/边界符号口径 | 低中：新增 region 改变 map 行 | 新增 `s_EDATA/l_EDATA`，不改既有符号语义 |
| 身份与实现一致性 | **中（R1 升级）**：Tag13=8 声明 InternalExtended，flex 实现的是 InternalMovable 顺序 | 不得径称已兑现；须在 A4 身份文档与 PM 裁定（D2）中明确过渡性质 |
| **契约裁定未决** | 高：D2 未裁定前，语义归属悬空 | 本方案以选项门控的过渡形态存在；裁定前不宣称闭环 |
| 二进制漂移 | **高（已再次发生）**：llc 在 rev-1 期间由 22:16 重建为 23:15 | 实施与验收必须重冻结三二进制 + runtime/CRT + 语料并重跑全批（§5.4） |
| 越界：改 demo 源码 | 高（纪律）：并行实例拥有 src/ | (a2) 零源码改动；禁止为 G8 改写语料 |

### 6.2 PM 决策点（R5：D5/D8 合并后重编号 D1–D7）

| ID | 决策 | 建议 |
|---|---|---|
| **G8-D1** | 主案选 (a2) 自动 AS0 EDATA，还是 (a1) DF4 显式 AS8，还是 (d) 窗口→0x100？ | **(a2)**；(a1) 保留为后续 ABI 完整化；(d) 备选 |
| **G8-D2** | **契约语义（R1）**：flex 是 InternalMovable 的分配顺序，合同 Tag13=8 却是 InternalExtended。裁定：(i) 认 flex 为过渡、后续改判/新增 InternalMovable；还是 (ii) 保留 Tag13=8 并实现"直接 EDATA 分配"（对既有镜像有布局扰动）？ | **先 (i) 过渡**：flex 对既有可容 demo 零扰动，回归面最小；(ii) 须单列并重评回归面。**本裁定是放行前置条件** |
| **G8-D3** | 自动迁移的启用门控：默认开 / 仅 v2 / 显式选项？ | **仅 v2 默认开**；v1 与显式选项保持关闭，保历史资产逐字节可复现 |
| **G8-D4** | 是否把非叶参数槽区（b1）纳入同一批？ | **纳入**：与主案共享 EDATA 基础设施；它**单独即清 39/43/45.1/45.2/50**，是 41 变绿的必要条件，并把 43/41 的槽区移出窗口。**触发限定为低窗失败**（禁止无条件迁槽，否则违反 §5.3/§6.3） |
| **G8-D5** | runtime 变参家族拆槽（b2）是否本批？ | **可选/延后，但收益不再是零**：b2 **单独清 39/45.1/45.2/50**（与 S1 重叠）。若采纳 (a2) 则增量价值低；若 D2 裁定走 (ii) 或否决 EDATA 窗口，b2 是不引入新窗口的替代路径。须权衡触 runtime 源（并行实例所有）的越界风险 |
| **G8-D6** | EDATA 窗口上界取 `--edata-end` 还是固定值？ | **取 `--edata-end`**（板级已给 0x3fff），与栈公式共用同一数值 |
| **G8-D7** | 41/43/44 的 CODE/P-4 残余是否在本缺口内解决？ | **否**：单列缺口；G8 只负责把首错前移，诚实登记 41/43/44 不因此变绿 |

### 6.3 放行原则（建议）

> 在 v2 合同下，普通 AS0 可变静态对象在低 128B DSEG 窗口放不下时，可整体迁入
> 板级 EDATA 窗口 `[0x100, --edata-end]`；迁移必须由**低窗 first-fit 失败**触发，
> 采用确定性失败回退重排，且只作用于带"可迁"标记的 AS0 section。对象保持 AS0 与
> 4B 指针宽度不变，访问继续经 24 位绝对地址 + 间接 @WR，启动清零继续经 XINIT
> 稀疏记录（其目标验证器须先接受 EDATA Region）。显式 `__data`（AS1 strict
> direct）对象仍必须位于 `[0,0x80)`，即使 EDATA 充足也不得迁出。所有内部占用
> （含 EDATA）计入向上栈高水位 `H`，剩余容量不足 1024B 时链接拒绝。低窗可容的
> 镜像布局必须逐字节不变。**本原则的契约语义归属待 §6.2-D2 裁定。**

---

## 7. 证据索引

| 编号 | 位置 |
|---|---|
| P1 | `GAP-G8-PROBES/g8probe.py` + `probe-run.log` + `probe-results.json` |
| P2 | `GAP-G8-PROBES/g8full.py` + `full1.log` / `full2.log`（完整 lld stderr） |
| P3 | `GAP-G8-PROBES/g8budget.py` + `budget1.log` / `budget2.log`（逐对象 section/symbol） |
| P4 | `GAP-G8-PROBES/g8split.py` + `split-results.json`（GLOBALS/SLOTS 拆分 + runtime 引用） |
| P5 | `GAP-G8-PROBES/g8sim.py` + `sim-results.json`（rev-0 first-fit；判定 18/18 与真实一致） |
| P6 | `GAP-G8-PROBES/g8unmask.py`（放大 CODE 窗口解除首错遮蔽） |
| **P7** | **`GAP-G8-PROBES/g8flex.py` + `flex.log` / `flex-results.json`（rev-1：失败回退重排模拟；none/strict/relayout 三机制 × a2/b1/b2/d/uncond）** |
| **P8** | **`GAP-G8-PROBES/g8relink.py` + `relink.log` / `relink-results.json`（rev-1：41/43/44/62 只读重链复现）** |
| M1 | `manuals-md/STC32G/ch11-存储器32-16-8位访问-…CHIPID.md` §11.2.1/§11.2.2（PDF 页 597–599） |
| M2 | `manuals-md/STC32G/ch01-overview.md` §1.1.3.1（EDATA 4KB/XDATA 8KB 框图） |
| S1 | `lld/MCS251/LinkerCore.cpp:455-589`（`classifySection`，无 EDATA Region） |
| S2 | `lld/MCS251/LinkerCore.cpp:2644-2657`（DSEG `0x80` 硬上限 + OSEG 同窗） |
| S3 | `lld/MCS251/LinkerCore.cpp:2659-2678`（ISEG `0x100` 窗口，无发射者） |
| S4 | `lld/MCS251/LinkerCore.cpp:2166-2174`（`reserve()` 与 `StackH`） |
| S5 | `lld/MCS251/LinkerCore.cpp:2995-3008`（栈公式与 1024B 门禁） |
| **S6** | **`lld/MCS251/LinkerCore.cpp:3545-3591`（XINIT 稀疏记录验证；`:3570-3578` 目标白名单仅 DSEG/DATA_ABS/BSEG_BYTES——R3 缺口）** |
| S7 | `lld/MCS251/LinkerCore.h:31-32`；`Driver.cpp:385-394`（`IramSize`/`EdataEnd`，`>0xffff` 拒绝） |
| S8 | `llvm/lib/Target/MCS251/MCS251AsmPrinter.cpp:1438-1518`（`emitParameterSlots`，`:1456` OSEG/DSEG 选择） |
| S9 | `llvm/lib/Target/MCS251/MCS251AsmPrinter.cpp:2156-2180`（可变全局无条件 `getDSEGSection()` + XINIT 记录） |
| S10 | `llvm/lib/Target/MCS251/MCS251TargetObjectFile.cpp:44-50`（`!isConstant()` → DSEG） |
| S11 | `llvm/lib/Target/MCS251/MCS251ISelLowering.cpp:2706-2712`（`LegacyDirect` 需 `ProgramAS==0`）；`:1316-1327`（AS1 范围门禁） |
| S12 | `llvm/lib/Target/MCS251/MCS251ISelLowering.cpp:3524-3531`（`parameterSlot` ExternalSymbol 路径） |
| S13 | `llvm/lib/TargetParser/MCS251TargetParser.cpp:37-56`（`getLayoutDesc`，v2 `ProgramAS=4`） |
| S14 | `llvm/include/llvm/BinaryFormat/MCS251Attributes.h:224-241`（`Placement_InternalExtended=8` = XSmall；`:226-241` 五模式表） |
| S15 | `llvm/lib/Target/MCS251/MCS251AsmPrinter.cpp:2024`（Tag 13 用 `Contract->DefaultPlacement`） |
| S16 | `validation/mcs251-models/DESIGN.md` §B.1（5 模式表，:115）/§B.2（AS 表）/§B.6（:525-553，DSEG→edata 闭环；**区分 InternalMovable 低窗优先 vs InternalExtended 按 EDATA**） |
| S17 | `validation/mcs251-elf/runtime/crt-selfstart-v2.yaml:117-127`（BOOT + XINIT walker 镜像） |
| S18 | `validation/mcs251-firmware/crt-selfstart.asm:88-124`（`__mcs251_globals_init` 稀疏清零，u16 目标） |
| S19 | `include/hw/mcs51/stc32g.h:23-26`；`hw/mcs51/stc32g.c:96`（QEMU EDATA 连续 16KiB） |
| S20 | `mcs251-demos-rewritten/tools/drive.py:92-103`（area args，`--edata-end=0x3fff`） |
| S21 | `mcs251-demos-rewritten/tools/drive.py:470-529`（按需链接，G3-S1 已实施） |
| D1 | `validation/mcs251-models/proposals/DF4-EDATA-DESIGN.md`（§0.1 范围 / §2 分类 / §5 分配 / §6 清零 / §9 栈 / §11 批次） |
| D2 | `IMPL-G2S4-PROGRESS.md:109-135`（§G8 前置检查旧口径） |
| D3 | `mcs251-demos-rewritten/TIERS.md`（19:42 冻结基线） |
