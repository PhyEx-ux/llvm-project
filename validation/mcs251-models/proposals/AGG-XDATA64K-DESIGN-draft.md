# AGG-XDATA64K 双缺口设计稿 r2 —— demo 58 聚合常量 global / demo 85 XDATA 64K 上限

状态：设计稿 r2（2026-09-15，r1 评审修订版）。纯调查 + 设计稿，未改任何产品源码。
工作树 `/home/liu/LLVM_STC32/MCS251`（HEAD `13d420b7a`）。
本文为未跟踪草案，不入 git；进度台账 `/home/liu/LLVM_STC32/GAP-AGG85-PROGRESS.md`；
探针 `/home/liu/LLVM_STC32/GAP-AGG85-PROBES/`（仓库外）。

口径说明：本次调查期间 llc 被并行构建重建（r1 记录 3 次：md5 `a17d279b` →
`b688f021` → `af147109`；r2 会话中又变为 `b19068c7`）。两缺口的失败层与文案在
每次重建后复测一致（r2 关键复现亦已在 `b19068c7` 上重测，见 §A.9/§B.9/§6.3）。
基线数字（§A.9/§B.9）标注了在飞影响。

**r2 修订说明（评审 7 组 CHANGES REQUESTED，逐条落实；本节即改动索引）**

| # | r1 主张 | r2 结论 | 位置 |
|---|---|---|---|
| 1 | oracle hash `d3df611a…`/`5f70bf18…` | **事实错误**：`5f70bf18…` 是 1024 个零字节的 sha256，`d3df611a…` 是 360 个零字节的 sha256（oracle 取错范围）。正确值 `94004b12…`/`797d6daa…`；"AS0 packed struct 被拒、AS4 通过、两表字节相同"结论成立；两表仅被读取 | §A.5、§A.4.1 |
| 2 | B 改写"即时解锁、逐字节语义等价" | **板级不可行**：改写后已不满足原绝对地址，且 `_xRAM2b`/`_xRAM3` 越过板/QEMU 可用区；`--xdata-size=0x21000` 链接拒绝。降级为**仅无物理容量门禁的 T1 探针**；板级可行方案另裁 | §B.5 Option A、§B.5.1、§3.2 |
| 3 | eRAM 显式 AS8 归 DF4-X | **应为 DF4-P0/L1/C1/Z1/R1**；16074B 从 0x100 放置得 H=0x3fca、First=0x3fe0、Capacity=32B ⇒ **确定违反 1024B 栈下限**；B-P2c 改**拒绝用例** + 新增缩小对象正例（≤15088 B） | §B.4.1、§B.4.3、§B.6.2、§B.7.2 |
| 4 | "两对象共 0x21000=135168 B" 后称">64K 被拒" | 对象总量 = `0x20ffe` = **135166 B**（`0x21000` 是含两空洞的布局跨度）；全文">64K"改"**≥64K（>65535 B）**" | §B.2.4、§3.1 |
| 5 | O09 字面"大于 64K" | 覆盖不到恰好 65536 B ⇒ 独立项须**显式扩展至该边界**；区分形态 (a) 大记录跨 bank vs (b) 连续逻辑对象 + 多条单窗 v1 记录 | §B.4.1 O09、§B.5 Option B、§B.6.2 |
| 6 | edata `effective_count=0` | ledger.json 中 edata 记录为 `count=1, effective_count=1`（far 为 2/2）；已同步证据口径；基线数字保留并注明**测试族未重跑** | §B.3.2、§B.9 |
| 7 | T2 未建立 | **维持未建立**，不改 | §B.5、§3.5 |

修订后的复现命令与输出均在 §A.5 / §B.5.1 / §B.4.3 / §B.2.4 就地给出；
探针落 `/home/liu/LLVM_STC32/GAP-AGG85-PROBES/{rw58,bv85,demo85,agg58,as8}/`。

---

## 0. 一句话结论

| 缺口 | demo | 台账记载 | 失败层 | 首错文案（摘要） | 推荐 |
|---|---|---|---|---|---|
| A | 58 [OLED-SSD1306] | T0 pass / T1 gap `aggregate global` | llc AsmPrinter AS0 只读门（clang 三层全放行） | `defined global data requires a byte-aligned read-only CSEG ... aggregates ... are not supported` | 语料内 2 行改写（两表加 `__code`），**零产品改动**；两表仅被读取；维持 AS4 §9.1 的 AS0 冻结 |
| B | 85 内部148K SRAM | T0 pass / T1 gap `XDATA 64K limit` | llc AsmPrinter AS3 记录字段上限（lld 同款门在其后） | `__xdata global 'xRAM1': object size 65536 does not fit the 16-bit XDATA record limit (65535 bytes; ...)` | 改写**仅作 T1 探针**（板级不可行：越可用区）；板级方案 = XDATA 尺寸适配 + `eRAM` ≤15088 B 或归能力侧；能力侧 **eRAM 归 DF4-P0/L1/C1/Z1/R1**，**≥64K（>65535 B）** 半分离为独立项（O09 须扩至 65536 B 边界） |

两缺口**都不在**任何测试族的失败项里（§A.9/§B.9 基线 167/167 + 61/61 + 23/23；
r2 未重跑测试族，数字沿用 r1 快照）。

---

# 第一部分：缺口 A —— demo 58 [OLED-SSD1306] 聚合常量 global

## A.1 问题

demo 58 的 OLED 组（`DMA-SPI刷新OLED12864显示屏程序，SSD1306驱动，0.96寸`）
T0 通过、T1 失败，台账分类 `aggregate global`。TFT-ST7789 组另有 T0 gap
（非 8 对齐 SFR bit 门），与本缺口无关。

## A.2 证据：精确定位

### A.2.1 失败层与拒绝点

- **层**：llc `MCS251AsmPrinter`（不是 clang）。
- **拒绝点**：`llvm/lib/Target/MCS251/MCS251AsmPrinter.cpp:2104-2117`
  （`Reject` lambda；常量臂 `:2106-2111`）。
- **调用点**：同文件 `:2187-2189`
  ```cpp
  if (!isSupportedROInitializer(Init, DL, /*AllowZeroImage=*/false,
                                /*AllowStructs=*/false))
    Reject();
  ```
  `AllowStructs=false` 是 AS4-AGGREGATE 切片（commit `03f9f62eb`）的定稿作用域裁定。
- **门函数**：
  - `isSupportedROType` `:899-917`：struct 子句仅当 `AllowStructs` 时可达（`:908-915`）。
  - `isSupportedROInitializer` `:919-968`：先类型资格（`:926-927`）后值分派。
- **进程行为**：`report_fatal_error` ⇒ abort，rc=134（不是干净退出）。

### A.2.2 失败文案（原文，完整）

```
LLVM ERROR: MCS251: defined global data requires a byte-aligned read-only CSEG
i8/i16/i32 scalar or nonempty initialized integer array of any alignment
(emitted byte-aligned); mutable data, zeroinitializers, custom sections, TLS,
weak/COMDAT, aggregates and initializer relocations are not supported
```

### A.2.3 复现（最小 → 全模块）

```bash
CL=/home/liu/build-mcs251-s1/bin/clang
LLC=/home/liu/build-mcs251/bin/llc
FLAGS="-mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -mcs251-object-format=elf"

# 形式 1：直接构造触发形态（PROBES/agg58/z1.c）
#   unsigned char const C[] = { <320×0x01>, <40×0x00> };
$CL --target=mcs251-unknown-none -std=c11 -O0 -fmcs251-keil \
    -Xclang -mcs251-memory-contract=1,2,32,8,1 -S -emit-llvm z1.c -o z1.ll   # rc=0
#   @C = dso_local constant <{ [320 x i8], [40 x i8] zeroinitializer }>, align 1
$LLC $FLAGS -filetype=obj z1.ll -o z1.o                                    # abort

# 形式 2：语料头文件原样（PROBES/agg58/inc.c，软链语料 ASCII-10x24.h）
#   @ASCII10x24 = dso_local constant <{ [320 x i8], [40 x i8] }>          # abort

# 形式 3：整模块（demo 58 OLED 的真实 IR）
$LLC $FLAGS -filetype=obj \
  /home/liu/LLVM_STC32/mcs251-demos-rewritten/build/58-DMA-SPI驱动显示屏/OLED-SSD1306/OLED128x64-SSD1306-SPI-DMA.ll \
  -o /dev/null                                                              # abort
```

### A.2.4 逐层归属（实测）

| 层 | 结果 | 证据 |
|---|---|---|
| clang Sema | 放行 | 探针 `-fsyntax-only` rc=0 |
| clang CodeGen 常量发射 | 放行（产出聚合常量） | `-S -emit-llvm` rc=0；`build/58-.../OLED-SSD1306/*.ll` 实物 |
| llc ISel/Verifier | 放行 | 无诊断 |
| **llc AsmPrinter** | **唯一拒绝点，故意 fail-closed** | `:2187` → `:2106-2111` |

## A.3 形态成因（clang 侧，实测钉死）

`clang/lib/CodeGen/CGExprConstant.cpp` 的 `EmitArrayConstant` `:1394-1453`：

- `:1411-1412` `TrailingZeroes >= 8` → 进入双数组 packed struct 改写；
- `:1418` `CommonElementType && NonzeroLength >= 8`：前半为 `[N x i8]` 常量数组；
- `:1431` 后半为 `ConstantAggregateZero`，类型 `[M x i8]`（`FillerType` 由 `:1429-1430` 得）；
- `:1450` `StructType::get(CGM.getLLVMContext(), Types, /*isPacked=*/true)`。

**触发规则实测**（`PROBES/agg58/`，p0:32 契约）：

| 非零头长 | 尾零数 | IR 形态 | AS0 结果 |
|---|---|---|---|
| 任意 | 0..7 | 扁平 `[N x i8]`（尾零写实） | 过 |
| 20 | 8, 9, 40 | `<{ [20 x i8], [M x i8] zeroinitializer }>` | **拒** |
| 2 | 20 | `<{ i8, i8, [20 x i8] zeroinitializer }>` | **拒** |
| 7 | 20 | `<{ i8 ×7, [20 x i8] zeroinitializer }>` | **拒** |
| 8 | 8/20 | `<{ [8 x i8], [M x i8] zeroinitializer }>` | **拒** |

**关键**：改写触发只要求尾零 ≥ 8，**不要求两半都 "大"**；头 <8 时退化为
标量前缀 + 零数组，仍是聚合结构，仍被 AS0 门拒。含隐式补零的声明
（`unsigned char const C[24] = { <20 个元素> };`，尾零 4）不触发；尾零 8 触发。

**对照（同形态不同 AS）**——同一份 IR 只改对象 AS：

| 变体 | IR | llc |
|---|---|---|
| `unsigned char const C[]`（AS0） | `<{ [320 x i8], [40 x i8] zi }>` | **拒** |
| `__code unsigned char const C[]`（AS4） | `addrspace(4) constant <{ [320 x i8], [40 x i8] zi }>` | **rc=0** |
| `__xdata unsigned char const C[]`（AS3） | `addrspace(3) constant <{ [320 x i8], [40 x i8] zi }>` | **rc=0** |
| `unsigned char C[]`（AS0 mutable） | `global <{ [320 x i8], [40 x i8] zi }>` | **rc=0** |

⇒ **缺口精确等于"AS0 只读通道的聚合子句缺失"**：AS4 已支持（AS4-AGGREGATE 切片）、
AS3 mutable 通道早已支持、AS0 mutable 通道早已支持；只有 AS0 只读被冻结。

## A.4 demo 需求子集

### A.4.1 语料实际形态

`build/58-DMA-SPI驱动显示屏/OLED-SSD1306/OLED128x64-SSD1306-SPI-DMA.ll`：

| 行 | global | IR 类型 | 源 | llc |
|---|---|---|---|---|
| L6 | `@ASCII6x8` | `[774 x i8]` | `ASCII6x8.h:3` | 过 |
| L7 | `@HZK16` | `[256 x i8]` | `HZK16.h:4` | 过 |
| **L8** | `@ASCII10x24` | `<{ [320 x i8], [40 x i8] zeroinitializer }>` | `ASCII-10x24.h:11`（360 值，尾 40 个 0x00） | **拒** |
| L9 | `@gImage_picture1` | `[1024 x i8]` | `picture1.h:10`（1024 值，无尾零） | 过 |
| **L10** | `@gImage_picture2` | `<{ [948 x i8], [76 x i8] zeroinitializer }>` | `picture2.h:10`（1024 值，尾 76 个 0x00） | **拒** |
| L11-14 | `SPI_TxAddr`/`B_TxCmd`/`SPI_TxCnt`/`B_SPI_DMA_busy` | AS0 mutable 标量 | 主文件 | 过 |
| L15-16 | `@CmdTmp`/`@DisTmp` | AS3 | 主文件 | 过 |

源侧全部为**普通 `const`，无 `code`/`__code` 限定**。

**两表仅被读取（评分项 1，支持 A 改写 + 维持 AS0 冻结）**：主文件
`OLED128x64-SSD1306-SPI-DMA.c` 中两张表各仅出现一次且均为读——
`:310 p = (u16)chr * 30 + ASCII10x24;`（取基址）、
`:506 for(i=0;i<1024;i++) DisTmp[i] = gImage_picture2[i];`（读元素）；
改写后的 `rw58/oled.ll` 对两符号只有 `getelementptr`/`load`
（`:529`、`:1020`），**无 store 目标为这两符号**
（`grep -nE "store.*(ASCII10x24|gImage_picture2)" rw58/oled.ll` 无输出）。
⇒ 放只读 CODE 空间与用法一致，**A 改写不改变可观察行为**。

### A.4.2 全语料扫查（102 个 `.ll`）

| 形态 | 出现 | 位置 |
|---|---|---|
| **AS0 常量聚合 global** | **2 处** | **仅 demo 58 OLED 组**（`ASCII10x24`、`gImage_picture2`） |
| AS4 常量聚合 global | 6 个文件 | 41/42/43 `gui.ll`、61 两组 `LCM_Test.ll`（已被 AS4-AGGREGATE 接纳） |
| AS0 全零常量数组（`zeroinitializer`） | 0 处（语料内无） | —— |

### A.4.3 最小覆盖子集

**2 个 global，2 个头文件**。形态极窄：

```
<{ [N x i8], [M x i8] zeroinitializer }>   // 一层 packed struct
                                            // 两段全 i8 数组
                                            // 第二段全零
                                            // align 1
```

**不属于**本缺口的相邻形态：全零常量数组 `const u8 Z[16]={0}` →
`zeroinitializer` → 同一条 AS0 文案，但属既有冻结子句 `zeroinitializers`
（探针 `agg58/r3`，实测 abort）。产品方案若走 Option B，须显式决定是否连带
放开此项（默认：不）。

### A.4.4 语料惯例对照（支持改写规避）

| demo | 字库表声明 | 形态 |
|---|---|---|
| 41/42/43 | `struct FONT_1206 code asc2_1206[]`（`font/font.h:14`） | **显式 `code`** |
| 61 | `unsigned char code asc2_1206[95][12]`（`font.h:47`） | **显式 `code`** |
| **58 OLED** | `unsigned char const ASCII10x24[]` | **无 `code`** |

⇒ 58 的同族字库表在语料内本就带 `code`；58 OLED 的写法是**惯例偏差**，
补齐 `__code` 是回归惯例，不是新发明。

## A.5 方案空间

### Option A（推荐）：改写规避 —— 两表加 `__code`

**实现路径**（语料内 2 行）：

```c
/* ASCII-10x24.h:11 */  __code unsigned char const ASCII10x24[]={...};
/* picture2.h:10   */  __code unsigned char const gImage_picture2[1024] = {...};
```

**实测（`PROBES/rw58/`，全链）**：

| 步 | 结果 |
|---|---|
| clang（语料真实 T0 flags） | rc=0 |
| IR | `@ASCII10x24 = dso_local addrspace(4) constant <{ [320 x i8], [40 x i8] }>`；`@gImage_picture2` 同形 `addrspace(4)` |
| llc（`1,2,32,8,1` + elf） | rc=0，`.text` 9575 B |
| lld（`oled.o` + `crt-irq-v2.o`，`AREA_ARGS_IRQ`，即 drive.py 同款命令） | rc=0，`l_CSEG=0x2567`（9567 B < 窗口 30976 B）、`l_DSEG=0x0038`、`l_XSEG=0x0405` |

> 注：drive.py 的 T1 依赖解析为空集（`oled.o` 无未定义符号，IRQ 模式下
> `printf_ASCII_text` 是本地函数而非 `printf`），故只需 `oled.o` + CRT。
> 加 `printf.o` 会引入 `__divulong_PARM_2` 未定义（需按 `_runtime_for` 的
> 传递闭包补 `divulong.o`/`modulong.o`），与本缺口无关。

**字节恒等 oracle（重做，主验证）**：
同一张表分别按"现支持通道"（AS0 扁平数组，手工把 clang 的拆分还原为
`ConstantDataArray`）与"AS4 `__code` 通道"各发一份 ELF 对象，取符号
`Value`/`Size` 对应的 `.text` 区间逐字节比对。

> **r1 oracle 的缺陷（已修正）**：r1 给出 `d3df611a0ed2e328…`（ASCII10x24）
> 与 `5f70bf18a0860070…`（gImage_picture2）——**这两个值是空/零填充区的哈希，
> 不是表数据的哈希**：`sha256(360 × 0x00) = d3df611a0ed2e328b050d285287637c6
> 0643ba96ec09e4aaefaad7f2cd114b77`，`sha256(1024 × 0x00) =
> 5f70bf18a086007016e948b04aed3b82103a36bea41755b6cddfaf10ace3c6ef`。
> r1 脚本对 gImage_picture2 取的 1024 B 恰好落在符号区外的零填充，故"相同"
> 是零对零的平凡相同（对 ASCII10x24 的 360 B 亦然）。两表**都不是全零**
> （实测非零字节数：360 B 表 266 个、1024 B 表 529 个），故 r1 的相等是
> oracle 取错范围的产物，不构成有效证据。

**正确 oracle 与结果**（`PROBES/rw58/sym_sha256.py`；按 `readelf -sW` 的
符号 `Value/Size` 与 `readelf -SW` 的节 `Off` 定位真实字节区间）：

| 表 | 符号区间 | AS0 扁平 | AS4 `__code` | 结论 |
|---|---|---|---|---|
| `_ASCII10x24` | `.text` off `0x1bdf`, 360 B | 360 B `sha256 94004b1228d93074…` | 360 B `sha256 94004b1228d93074…` | **相同** |
| `_gImage_picture2` | `.text` off `0x2147`, 1024 B | 1024 B `sha256 797d6daac90b2a39…` | 1024 B `sha256 797d6daac90b2a39…` | **相同** |

完整复现（两份对象字节恒等，含符号区外无关字节）：

```bash
python3 /home/liu/LLVM_STC32/GAP-AGG85-PROBES/rw58/sym_sha256.py \
  demo58/flat3.o=_ASCII10x24  demo58/flat3.o=_gImage_picture2 \
  rw58/oled.o=_ASCII10x24     rw58/oled.o=_gImage_picture2
# demo58/flat3.o _ASCII10x24      sec=.text off=0x01bdf size= 360 sha256=94004b1228d9307476801ba3bd735c867550d24f5451c1a26aa5c048f95aec0f
# demo58/flat3.o _gImage_picture2 sec=.text off=0x02147 size=1024 sha256=797d6daac90b2a39604a87896200c0dabffbcc2deb2387977ce61ca289644671
# rw58/oled.o    _ASCII10x24      sec=.text off=0x01bdf size= 360 sha256=94004b1228d9307476801ba3bd735c867550d24f5451c1a26aa5c048f95aec0f
# rw58/oled.o    _gImage_picture2 sec=.text off=0x02147 size=1024 sha256=797d6daac90b2a39604a87896200c0dabffbcc2deb2387977ce61ca289644671
```

（`flat3.o` = AS0 扁平形态，`oled.o` = 语料 `__code` 改写形态；两者同一符号
同址同大小、sha256 逐字节相同。**修正后结论不变**：AS0 packed struct 被拒、
AS4 通过、两表字节相同——Alice 亲测一致。）

**两表仅被读取（支持 A 改写 + 维持 AS0 冻结）**：
`OLED128x64-SSD1306-SPI-DMA.c` 里两张表各只出现一次且均为读：
`:310 p = (u16)chr * 30 + ASCII10x24;`（取基址）、
`:506 for(...) DisTmp[i] = gImage_picture2[i];`（读元素）。改写后的
`rw58/oled.ll` 对两个符号只有 `getelementptr`/`load`
（`:529` `%22 = getelementptr ... @ASCII10x24`、`:1020`
`%68 = getelementptr ... @gImage_picture2`），**无任何 `store` 目标为这两符号**：

```bash
grep -nE "store.*(ASCII10x24|gImage_picture2)" rw58/oled.ll   # 无输出
```

⇒ 把两表放进只读 CODE 空间（`__code`）与用法一致；**无源码改动需求**，
因此 AS0 只读门的冻结（AS4 §9.1 / §6A.4 R4）维持不变。

**指针引用面**：demo 58 用 `u8 const *p` 承接表地址
（`OLED128x64-SSD1306-SPI-DMA.c:259, 277, 310`）。

- AS0 改写前：`p = (u16)number*6 + ASCII6x8;`，`ASCII6x8` 是 AS0 常量 → 无 cast。
- AS4 改写后：IR 变为 `getelementptr inbounds nuw i8, ptr addrspace(4) @ASCII10x24, i32 %21`
  + `addrspacecast ptr addrspace(4) %22 to ptr`（`rw58/oled.ll:529-530`），
  走 A3 放行的 AS4→AS0 通道。
- 实测 llc/lld 全通过，无 static-ptr ABI 报错（本 demo 的指针经局部变量 `p`
  中转，不进静态参数槽）。

**与冻结面交互**：

| 冻结面 | 交互 |
|---|---|
| AS4-AGGREGATE §9.1（AS0-only） | **不动** |
| AS4-AGGREGATE §6A.4 R4（AS0 文案一字不动） | **不动** |
| `global-constant-error.ll` / `global-data-error.ll` / `global-ro-align-policy.ll` | **不动** |
| A4 v2 身份门（GAS 0/3/4） | AS4 在白名单内，无阻碍（实测） |
| P-4 签名协议 | 无交互 |
| DF4-EDATA | 无交互 |

**风险**：

1. 语料改写把 `const` 对象放进 CODE 空间，运行期不可写 —— 与本 demo 用法
   （只读查表）一致，无行为变化。
2. AS4 路径的字节对齐要求（`:2297-2301` "非数组存储须字节对齐"）：
   本形态是 packed struct（align 1），实测通过。
3. AS4 路径要求 ELF 对象输出（`:2240-2244`），T1 已满足（`-mcs251-object-format=elf`）。
4. `-fmcs251-keil` 下 `__code` 拼写可用（`Parser/mcs251-xdata-code-spellings.c`
   覆盖），语料惯例如 §A.4.4。

**PM 决策点 A-1**：接受语料内 2 行改写？（推荐：接受）

### Option B：产品侧开 AS0 RO 聚合子句（不推荐）

**实现路径**：`:2187-2189` 的 `AllowStructs` 改 `true`（或加更窄的
"仅 clang 合成尾零 struct"子句）；发射器 `emitROInitializer` 的 struct 递归
（`:1004-1013`，AS4-AGGREGATE 产物）可直接复用。

**与冻结面交互（大）**：

- **直接反转** AS4-AGGREGATE §9.1（"struct 子句作用域 = AS4-only"）与 §6A.4 R4
  （"AS0 侧文案一字不动"）。
- 须同步修订 AS4 设计稿 §6A.4、§9.1、§10（精确修订断言）、§12（回归预核）与
  §13（证据命令索引）。
- 须同步修订 AS0 拒绝面测试：`global-constant-error.ll:record.ll`
  （`@g = constant {i8, i16}`）、`global-data-error.ll`、`global-ro-align-policy.ll`
  （`sect.ll`）、`code-cast-static-init-boundary.ll`、`elf-errors.ll`。

**风险**：AS0 只读门一旦接受 struct，"窄口"性质丧失 —— 任意 AS0 struct 常量
都进入接受集，影响面远超 demo 58 的 2 个表。收益/代价严重不成比例。

**PM 决策点 A-2**：是否批准该反转？（推荐：否）

### Option C：clang 侧抑制尾零拆分（不推荐）

**实现路径**：在 `EmitArrayConstant` `:1394` 加 MCS251 目标钩子，对 AS0 只读常量
跳过 `:1412` 的拆分，直发 `ConstantDataArray`。

**与冻结面交互**：改动全目标共享的 `CGExprConstant.cpp`，须目标门控；IR 形态随
目标变化，影响 `clang/test/CodeGen/mcs251-as4-superset-ir.c` 一类 IR 断言测试
（需核实其口径）。为 2 个 global 改共享 CodeGen 路径，风险/收益比差。

**PM 决策点 A-3**：是否需要该备选？（推荐：否；仅在 A-1 被否且 A-2 也被否时启用）

### Option D：`.ll` 拍平（不做）

已验证可行（`demo58/flat3.ll` llc rc=0），但源侧等价做法就是 Option A，
不引入后期 pass。不单独成案。

## A.6 切片

**本缺口不产生产品切片**（推荐 Option A）。若 PM 选 Option B，建议切片：

- `A-AGG-S0`：门加 `AllowStructs` 二次调用点（AS0 + clang 合成形态白名单）；
  只接受"顶层 struct、成员全为 i8/i16/i32 数组或标量、第二成员为全零数组"的窄形态；
- `A-AGG-S1`：文案与测试面修订（AS0 拒绝面 5 个测试 + AS4 设计稿 5 节）；
- `A-AGG-S2`：demo 58 OLED 组 T1 回归 + 41/42/43/61 AS4 回归预核。

## A.7 测试矩阵

### A.7.1 Option A（改写规避）验收

| # | 项 | 断言 |
|---|---|---|
| A-T1 | `ASCII-10x24.h`/`picture2.h` 加 `__code` 后 clang rc=0 | T0 ok |
| A-T2 | IR 两 global 为 `addrspace(4) constant` | `grep` IR |
| A-T3 | llc rc=0 | T1 llc ok |
| A-T4 | lld rc=0，`l_CSEG` < CODE 窗口 | T1 link ok |
| A-T5 | **字节恒等**：改写前后 `ASCII10x24`/`gImage_picture2` 的 `.text` 符号区间 sha256 相同 | 主验证（§A.5 oracle） |
| A-T6 | `ASCII6x8`/`HZK16`/`gImage_picture1`（未改写表）字节不变 | 回归 |
| A-T7 | AS0 拒绝面测试不动且仍 PASS | `global-constant-error.ll` 等 |
| A-T8 | demo 58 TFT-ST7789 组 T0 gap 不变（bit addr，另一缺口） | 台账不变 |

### A.7.2 Option B（产品侧）须新增的单测

| # | 片 | 断言 |
|---|---|---|
| A-B1 | `<{ [N x i8], [M x i8] zeroinitializer }>` AS0 正例 | llc 发射成功，字节 = 拼接图像 |
| A-B2 | 头 <8（标量前缀）AS0 正例 | 同上 |
| A-B3 | 顶层**非数组** struct AS0 | **仍拒**（若采"窄形态"子句） |
| A-B4 | 全零常量数组 AS0 | **仍拒**（`zeroinitializers` 冻结子句） |
| A-B5 | 任意 AS0 struct（`{i8,i16}`） | 按 PM 裁定，默认**仍拒** |
| A-B6 | `code-placement.ll` AS4 正例 | 不回归 |

## A.8 风险与决策点汇总（缺口 A）

| ID | 项 | 推荐 |
|---|---|---|
| A-1 | 语料 2 行改写 | **接受** |
| A-2 | 反转 AS4 §9.1 AS0-only 冻结 | **否** |
| A-3 | clang 目标钩子抑制拆分 | **否**（备选） |
| A-4 | 是否连带放开 AS0 全零常量数组 | **否**（独立冻结子句） |
| A-5 | demo 58 OLED 组 T2 是否要求 | 本调查未主张；T1 通过后可跑 |

## A.9 基线（缺口 A 相关）

> **r2 口径（评分项 6）**：数字沿用 r1 会话末快照，与进度文件一致；二进制 MD5
> 已亲核（§B.9 末行）；**r2 修订未重跑测试族**。

| 族 | 结果 |
|---|---|
| `llvm/test/CodeGen/MCS251`（排除 5 个在飞新增/改动测试，167 文件） | **167 / 167 PASS**（r1 快照） |
| `llvm/test/CodeGen/MCS251`（全量，llc `af147109`） | 172 discovered / 170 pass / 2 fail（`tfpu-intrinsics.ll`、`tfpu-ordering.ll`，并行实例在飞；r1 快照） |
| `clang/test/{CodeGen,Sema,Parser}/mcs251*` | **61 / 61 PASS**（r1 快照） |
| `lld/test/MCS251` | **23 / 23 PASS**（r1 快照） |
| demo 台账 `58 [OLED-SSD1306]` | T0 pass / T1 gap（`aggregate global`）/ T2 skip |

**A 相关复现重测（r2，重建后二进制）**：AS0 聚合常量 abort rc=134、AS4 `__code`
rc=0、demo58 整模块 abort（文案同 §A.2.2）、oracle 逐字节 sha256 相同——均不变。

---

# 第二部分：缺口 B —— demo 85 内部 148K SRAM 读写测试 / XDATA 64K 上限

## B.1 问题

demo 85 T0 通过、T1 失败，台账分类 `XDATA 64K limit`。核心问题：这是链接期
**地址空间**上限（XDATA 16 位寻址 64K），还是编译器/链接器**约束**？与
`DF4-EDATA-DESIGN.md` 的关系如何？结论：**记录格式约束**；eRAM 半归
**DF4 首批五片 `DF4-P0/L1/C1/Z1/R1`**，**≥64K（>65535 B）** 半归**独立项**；
demo 85 级别的"改写即时解锁"**不成立**（板级可用区越界，§B.5.1）。

## B.2 证据：精确定位

### B.2.1 失败层与拒绝点

- **层**：llc `MCS251AsmPrinter` 的 AS3（`__xdata`）放置分支 `emitAddressSpacedGlobal`
  （`:2216-2290`），`GAS == 3` 子分支 `:2247-2285`。
- **拒绝点**：`:2256-2259`
  ```cpp
  if (!Size || Size > UINT16_MAX)
    Bad("object size " + Twine(Size) + " does not fit the 16-bit XDATA "
        "record limit (65535 bytes; XSEG objects never straddle a 64K window)");
  ```
- **镜像门（lld，后置）**：`lld/MCS251/LinkerCore.cpp:2805-2811`
  ```cpp
  if (S->Size > 0xffff) { ... "XSEG section ... exceeds the 64K window: XDATA
    objects are limited to 65535 bytes and never straddle a 64K window boundary"; }
  ```
  另有 `validateXDATAInit` `:3596-3686`：目的单 64K 窗、完整落一个 XSEG 切片、
  记录不重叠。

### B.2.2 失败文案（原文，完整）

```
LLVM ERROR: MCS251: __xdata global 'xRAM1': object size 65536 does not fit the
16-bit XDATA record limit (65535 bytes; XSEG objects never straddle a 64K window)
```

lld 侧同款（若 llc 被绕过或对象来自手写 IR）：
```
mcs251 linker: error: XSEG section .mcs251.XSEG.<name> (65536 bytes) exceeds the
64K window: XDATA objects are limited to 65535 bytes and never straddle a 64K
window boundary
```

### B.2.3 复现

```bash
LLC=/home/liu/build-mcs251/bin/llc
$LLC -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -mcs251-object-format=elf \
  -filetype=obj /home/liu/LLVM_STC32/mcs251-demos-rewritten/build/85-内部148K字节SRAM读写测试/sample.ll \
  -o /dev/null
# → LLVM ERROR: MCS251: __xdata global 'xRAM1': object size 65536 does not fit ...
```

### B.2.4 性质判定：**记录格式上限，不是地址空间上限**

架构地址空间是 24 位（16 MB）。lld 的通用范围检查 `rangeFits(Start, Size, Limit=0x1000000)`
（`LinkerCore.cpp:451-453`）只对越 24 位报 `XDATA address overflow`。

真正的约束来自冻结的**记录 v1 格式**（`XDATA-CODE-DESIGN-SUPPLEMENT.md §7.2/§7.4`）：

| 字段 | 宽度 | 后果 |
|---|---|---|
| `bank` | u8 | 记录只能描述单 bank 对象 |
| `window` | u16 | 窗内偏移 ≤ 65535 |
| `object_size` | u16（1..65535） | **单对象 ≤ 65535** |
| walker | 无 bank 进位 | 见 `crt-xdata-init-walker.asm` 头注 |

**实测反证**（`PROBES/demo85/min85b`）：三个对象共
`0xffff + 0xffff + 0x1000 = 0x20ffe = 135166 B`，lld rc=0：

```
min85b.o:.mcs251.XSEG._xRAM1 0x10000 +0xffff
min85b.o:.mcs251.XSEG._xRAM2 0x20000 +0xffff
min85b.o:.mcs251.XSEG._xRAM3 0x30000 +0x1000
l_XSEG = 0x21000
```

**口径更正（评分项 4）**：`l_XSEG = 0x21000 = 135168 B` 是**布局跨度**
（`s_XSEG=0x10000` 到最高已分配上界），不是对象总字节数。**对象总量 =
`0x20ffe = 135166 B`**，二者相差 2 B，正是两处 bank 跳转留下的洞：
`_xRAM1` 止于 `0x1ffff`（bank 1 尾），`_xRAM2` 自 `0x20000` 起；`xRAM1`
的 `[0x10000,0x1ffff)` 与游标跳 bank 间的 1 B 洞，以及 `_xRAM2` 止于
`0x2ffff` 与 `_xRAM3` 起点 `0x30000` 间的 1 B 洞。**洞计入跨度、不计入对象量**
（与 §B.2.4 上文"洞计入 l_XSEG 跨度"以及 `XDATA-CODE-DESIGN-SUPPLEMENT §7.4`
一致）。

⇒ **总量跨 bank 无碍（顺序分配会跳到下一 bank 起点）；只有单对象 ≥ 64K
（即 > 65535 B）被拒。** 边界实测（`PROBES/bv85/s65535.c`、`s65536.c`）：

```bash
# 65535 B 单对象：过
# 65536 B 单对象：拒（原文见 §B.2.2）
$LLC -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -mcs251-object-format=elf \
  -filetype=obj s65535.ll -o s65535.o   # rc=0
$LLC ... s65536.ll -o s65536.o          # LLVM ERROR: ... 'x': object size 65536 does not fit ...
```

**因此全文把 ">64K 被拒" 更正为 "≥64K（>65535 B）被拒"**：
"64K" 字面（65536 B）恰好落在拒绝集内，语义上是**大于 65535**。r1 的
">64K" 措辞会漏掉恰好 65536 B 这一点，见评分项 5 的 O09 边界讨论。

结论：**编译器/记录格式约束**。lld 的顺序分配甚至特意实现了"跨 bank 跳转"
（`LinkerCore.cpp:2828-2833`），说明设计者已预期总占用 >64K。

## B.3 demo 需求画像

### B.3.1 原始语料

`STC32G144K246-DEMO-CODE/85-内部148K字节SRAM读写测试/sample.c:85-88`：

```c
unsigned char edata eRAM[EDATA_LEN];                    // 16074 B, 0000H~3FFFH
unsigned char xdata xRAM1[XDATA1_LEN];                  // 65536 B, 01:0000H~01:FFFFH
unsigned char far   xRAM2[XDATA2_LEN] _at_ 0x020000;    // 65536 B, 02:0000H~02:FFFFH
unsigned char far   xRAM3[XDATA3_LEN] _at_ 0x030000;    //  4096 B, 03:0000H~03:0FFFH
```

头部注释另记："项目设置 Target 页面 External Memory - RAM, Start:0x10000,
Size:0x20000"。

### B.3.2 改写后

`mcs251-demos-rewritten/src/85-内部148K字节SRAM读写测试/sample.c:97-102`：

| 原 | 改写 | ledger note（同步后的实际口径） |
|---|---|---|
| `edata eRAM` | 丢弃限定符（AS0 默认） | `storage:edata` ×1 `count=1, effective_count=1` "dropped (default AS0 edata)" |
| `xdata xRAM1` | AS3 | 「`storage:data` ×1（注释内）」：**当前 ledger.json 中 demo 85 无 `storage:data` 记录**（`by_rule` 无此键），文中 `xdata` 保留 |
| `far xRAM2 _at_ 0x020000` | `__xdata xRAM2`（AS3） | `storage:far` `count=2, effective_count=2` "far -> __xdata (AS3/MOVX)" |
| `far xRAM3 _at_ 0x030000` | `__xdata xRAM3`（AS3） | 同上 |
| `_at_ 0x020000/0x030000` | **子句丢弃** | `gap:_at_` ×2 `count=2, effective_count=2` "absolute placement not implemented; clause dropped, object falls in the default area" |

**口径更正（评分项 6）**：r1 写 `storage:edata effective_count=0` 与
`storage:data effective_count=0`，与当前 `ledger.json` 不符。实测
（`python3 -c "import json; ..."`，见下）demo 85 的 `files.sample.c.notes`
中 `edata` 记录为 `count=1, effective_count=1`；`far` 为
`count=2, effective_count=2`；`gap:_at_` 为 `count=2, effective_count=2`；
`by_rule` = `{bitname:4, eaxfr:1, gap:_at_:1, include:2, storage:edata:1,
storage:far:1}`——**无 `storage:data` 键**。故本表已按 ledger.json 现状改写：

```bash
python3 - <<'PY'
import json
d=json.load(open('/home/liu/LLVM_STC32/mcs251-demos-rewritten/ledger.json'))
e=d['demos']['85-内部148K字节SRAM读写测试']
for n in e['files']['sample.c']['notes']:
    if n['rule'] in ('storage','gap'): print(n)
print(e['notes']['by_rule'])
PY
# {'rule': 'storage', 'kw': 'far',   'count': 2, 'effective_count': 2, ...}
# {'rule': 'gap',     'kw': '_at_',  'count': 2, 'effective_count': 2, ...}
# {'rule': 'storage', 'kw': 'edata', 'count': 1, 'effective_count': 1, ...}
# {'bitname': 4, 'eaxfr': 1, 'gap:_at_:': 1, 'include': 2, 'storage:edata': 1, 'storage:far': 1}
```

> `count` vs `effective_count`：ledger 的 `count` 是词法命中数，
> `effective_count` 是去掉注释后的有效数。demo 85 的 `edata` 是源码真实声明
> （`sample.c:102`），故两者均为 1；r1 的 `effective_count=0` 属笔误/旧快照。
> 本更正只改**证据口径引用**，不触碰 ledger.json 本身（语料文件不在本实例
> 可写范围）。

IR（`build/85-.../sample.ll`）：

```
L8  @eRAM  = dso_local global [16074 x i8] zeroinitializer, align 1        ; AS0
L10 @xRAM1 = dso_local addrspace(3) global [65536 x i8] zeroinitializer    ; AS3
L12 @xRAM2 = dso_local addrspace(3) global [65536 x i8] zeroinitializer    ; AS3
L14 @xRAM3 = dso_local addrspace(3) global [4096 x i8] zeroinitializer     ; AS3
```

### B.3.3 分层残余（逐层剥离，实测）

| # | 层 | 现象（原文） | 探针 |
|---|---|---|---|
| R1 | llc AS3 记录上限 | `__xdata global 'xRAM1': object size 65536 does not fit the 16-bit XDATA record limit (65535 bytes; ...)` | 原样 |
| R2 | llc 通过后 → lld DSEG 窗 | `cannot allocate .mcs251.dseg (from l1fix.o): size 16078 bytes, align 1, symbols _index _eRAM; window [0x0000,0x0080): no free range of 16078 bytes; largest free range is 120 bytes` | `l1fix`（仅 65536→65535） |
| R3 | `_at_` 绝对放置未实现 | 子句改写时被丢弃（`gap:_at_`）。**当前顺序放置恰好**给出目标地址（`min85b.map`：xRAM1@0x10000、xRAM2@0x20000、xRAM3@0x30000 —— 与原始 `_at_` 意图一致），但无任何保证；若模块顺序或其它对象变化即漂移 | `min85b.map` |
| R4 | 其它 | 无 CODE 窗口/身份/签名残余（R2 探针全链） | 同上 |

⇒ demo 85 至少叠 **3 层**：记录上限 + AS0 大数组须落内部 RAM（EDATA）+ 绝对放置。

**r2 追加第 4 层（评分项 2/3）**：即使前 3 层都过，**板级可用区/栈容量**仍是
独立阻断——

- **R5 板级 XDATA 容量**：demo 85 的 **XDATA 对象需求 132 KiB（64K+64K+4K，不含 eRAM）**；
  另加 `eRAM` 16 KiB 若挤入 XSEG 后，改写体把 `_xRAM2b`/`_xRAM3` 推到
  `0x30000+` 越出 `[0x010000,0x031000)`（板级跨度 = 0x21000）；`--xdata-size=0x21000` 硬拒
  （§B.5.1）。
- **R6 内部 RAM 栈门禁**：`eRAM` 16074 B 落 EDATA ⇒ Capacity=32 B < 1024 B，
  硬拒（§B.4.3）。

⇒ demo 85 的完整解锁需同时解决 R1–R6，**不是单一改写**。

### B.3.4 板级与 QEMU 模型口径

| 来源 | 内部 RAM (edata) | XDATA | 合计 |
|---|---|---|---|
| 手册 `manuals-md/G144K246/12-存储器-全球唯一ID号CHIPID.md:178` | 16K（理论 64K） | 128K（理论 8M-64K） | **144K** |
| 板级 profile `validation/mcs251-demo-modern/boards/stc32g144k246.mk` | `EDATA_END 0x3fff` | —— | —— |
| QEMU 模型 `liu/qemu-src/include/hw/mcs51/stc32g.h:23-26` | `EDATA_SIZE 16 KiB` | `XDATA_BASE 0x010000 / XDATA_SIZE 128 KiB` | 144K |
| QEMU 另段 | —— | `EXEC_DATA_BASE 0x030000 / EXEC_RAM_SIZE 4 KiB`（可运行 RAM 区） | +4K |

⇒ demo 自称 "148K" 是其自身算术（含 0x030000 的 4K 可运行 RAM 区）；
按"16K edata + 128K xdata"口径为 **144K**。`xRAM3 @0x030000` 落在 EXEC_RAM 区，
与 128K XDATA（0x010000-0x02FFFF）**不重叠**，与原始 `_at_` 意图一致。

**r2 补充（评分项 2）**：`xRAM3` 落在 EXEC RAM 是正确的**原始意图**，但
r1 的改写体（§B.5 Option A）把 `_xRAM3` 顺移到 `0x38000`，**离开了 EXEC RAM**
（EXEC RAM 只到 `0x031000`）。故"148K 算术成立"不蕴含"改写在板上可放置"；
板级可用跨度 `[0x010000,0x031000)` = `0x21000` 见 §B.5.1。

## B.3.5 可用区与容量门禁（r2 新增）

| 量 | 值 | 证据 |
|---|---|---|
| 128K XDATA 可用 | `[0x010000,0x030000)` = 131072 B | QEMU header / 手册 |
| 4K EXEC RAM 可用 | `[0x030000,0x031000)` = 4096 B | 同上 |
| **板级连续可用跨度** | **`0x21000` = 135168 B** | 两者相邻拼接 |
| 门禁开关 | `--xdata-size=0x21000` ⇒ 每个 XSEG 节须完整落 `[0x010000,0x031000)`，违反硬错误 | `LinkerCore.cpp:2849-2868`（§B.11） |
| 缺省行为 | 不设 `--xdata-size` 时**只有 24 位 rangeFits**（16 MB），**不反映板级容量** | `Driver.cpp:400-407`、`X3-IMPL-BRIEF.md` |

⇒ **"link 通过" ≠ "板上可运行"**：探针不设容量门禁时，1×64K 拆 2×32K 的
`split_all` 排布能过链接器，却把两段放到了板外。r2 起所有 demo 侧验收
（§B.6.1、§B.7.1 B-T4a）**必须带 `--xdata-size=0x21000`**。

## B.4 与 DF4-EDATA 设计的关系（只读对照）

### B.4.1 DF4 相关条款

| DF4 条款 | 内容 | 对本缺口 |
|---|---|---|
| §0.1(1) | 首批**只**新增显式 `edata`/AS8 静态对象的 EDATA 放置；普通 AS0 对象不自动迁移 | `eRAM` 是 AS0 ⇒ **不在首批覆盖内**（须语料侧改显式 AS8，见 §B.5 B-EDATA-1） |
| §0.1(2) | 首批不实现按对象大小自动迁移，也不实现链接时 DSEG→EDATA 自动溢出 | 同上（阈值放置属 §2.5/DF4-A，flex 属 §5.5/DF4-F） |
| §0.1(7) / §8 | XDATA 单独实施、独立分配，不计内部栈 H | 本缺口 XDATA 半属 DF4-X |
| §2.2 分类终表 | `AS3 xdata → ExternalData → .mcs251.xdata.*`，标"**后续 XDATA 批次**" | DF4-X |
| §2.2 分类终表 | 显式 AS8 静态对象 → `InternalExtended → .mcs251.edata.*` | **eRAM 转显式 AS8 后落此行** |
| §8.1 | `xdata`→AS3→`.mcs251.xdata.*` NOBITS→lld XDATA 窗→DF3 32 位访存→XDATA 初始化 provider | 与现状 `.mcs251.XSEG.*` 等价，DF4-X 是独立账本化 |
| §8.2 | lld XDATA 分配："保留 bank 不截 16 位；对象整体连续" | **仍不解除单对象 64K** |
| §2.3 首批 AS8 允许集 | i8/i16/i32 标量及数组/结构体、**全零初值**、字节对齐、local/external linkage、无指针初值 | `eRAM` 零初值、全零，形态完全匹配（只差 StorageAS = 8） |
| §6 零初始化闭环 / §9.2 公式 | CRT 在 `[0x100,H)` 连续清零；`H = max(0x100, exclusive end)`、`First = align_up(H,16)+16`、`Capacity = EDATA_END+1-First`；剩余容量 ≥ 1024 B | **16074 B 从 0x100 放置：H=0x3fca、First=0x3fe0、Capacity=32 B ⇒ 确定违反 1024 B 栈下限**（见 §B.4.3） |
| §11 批次表 | `DF4-P0`(身份/记录/CRT 契约) + `DF4-L1`(显式 EDATA 窗/H/栈/清零范围/map) + `DF4-C1`(分类器/AS8 零对象/NOBITS) + `DF4-Z1`(v2 CRT/连续清零/provider 门禁) + `DF4-R1`(首批集成与重冻) | **eRAM 半的归属批次**（不是 DF4-X；DF4-X 是显式 AS3/XDATA 那一半） |
| **§15 O09** | "**大于 64K 对象/任意对齐 —— 首批不顺带开放**"（原文） | 字面">64K"覆盖不到**恰好 65536 B**；独立项须**显式扩展至该边界**（见 §B.5 与下方 O09 边界裁定） |
| §13.2 人日 | 首批 13-22 人日 | 规模参照 |

#### O09 边界裁定（评分项 5）

DF4 §15 O09 的字面是"**大于 64K** 对象/任意对齐"，而编译器/链接器的实际
拒绝阈值是 `> 65535`（即 **≥ 65536**，§B.2.4 实测边界）。两处不重合：
"大于 64K"（`>65536`）会把**恰好 65536 B** 排除在 O09 之外，而该尺寸确被拒。
故：

- **O09 的适用域须显式扩展至包含恰好 65536 B**，写法改为
  "**≥ 64K（> 65535 B）对象**/任意对齐"。
- 由此，demo 85 的 `xRAM1`/`xRAM2`（各 65536 B）**正落在 O09 域内**，
  是独立项而非 DF4-X 的顺带范围。
- 独立项（*XDATA record v2* 或链接器拆分）的立项条件因此不是"≥ 64K+1"
  而是"**> 65535 B**"，与 `object_size` u16 的字段语义完全对齐。

**两种形态必须分开（评分项 5）**：

| 形态 | 描述 | 是否要求 walker bank 进位 / v2 |
|---|---|---|
| **(a) 大记录跨 bank** | 单条 v1 记录描述一个跨界对象（`object_size` 需 > u16 或跨窗） | **是**：须 v2 记录**及** walker bank 进位（两者都要，非择一） |
| **(b) 连续逻辑对象、多条单窗 v1 记录** | 语言层一个对象，实现层拆成多条各自单窗的 v1 记录（每段仍是独立 XSEG 节），各记录自身不跨 bank | **不必然**：不要求 walker 进位或 v2；但**必须保证物理连续**及符号/GEP/重定位语义 |

形态 (b) 实测（`PROBES/bv85/span_tri.*`）：一个逻辑 96 KiB 对象拆三段
32 KiB，链接后三条 v1 记录分别为 `bank=0x01 window=0x0000 size=32768`、
`bank=0x01 window=0x8000 size=32768`、`bank=0x02 window=0x0000 size=32768`
——**逻辑对象跨 bank 1/2，但每条记录自身单窗**，无 v2、无 walker 进位，
lld rc=0：

```bash
# 链接后 .mcs251.xdata_init @0xff9000（14+7=0x15 B 的记录表）：
# 0100 0080 0000 0001 8000 8000 0000 0200 0080 0000 00
#   bank=0x01 win=0x0000 size=32768 -> [0x010000,0x018000)
#   bank=0x01 win=0x8000 size=32768 -> [0x018000,0x020000)
#   bank=0x02 win=0x0000 size=32768 -> [0x020000,0x028000)
```

**但 (b) 的物理连续不是自动保证**（反证 `PROBES/bv85/span64_hole.*`）：
把首段改为 36864 B（`bigLo`）后，顺序分配器在 bank 1 尾放不下第二段，
**跳到下一 bank 起点**，于是 `bigHi` 落在 `0x20000` 而非 `0x19000`，
两段不再相邻：

```
span64_hole.o:.mcs251.XSEG._bigLo 0x10000 +0x9000   # [0x10000,0x19000)
span64_hole.o:.mcs251.XSEG._bigHi 0x20000 +0x8000   # [0x20000,0x28000)  <- 洞 0x7000
```

⇒ 形态 (b) 若要成立，须由后端/链接器**强制相邻**（例如给拆分片段加
"必须连续"约束或显式逐节 `--area-start`）并同时锁定符号/GEP/重定位语义；
仅靠现有顺序分配**不成立**。此外形态 (b) **不能保留语言对象"不跨 bank"
的原裁定**（`XDATA-CODE-DESIGN-SUPPLEMENT §7.4` "XSEG 对象不跨 64K 窗"）——
要么在语言/对象层明确引入"逻辑大对象"概念并重裁该条，要么 (b) 仍归独立项。
本节把 (b) 记为独立项的**候选较轻路径**，但不主张它可免裁定。

### B.4.2 当前实现状态实测（证明不是"小修"）

| 项 | 实测 | 证据 |
|---|---|---|
| clang `edata`/`__edata` 关键字 | **不存在**：`unsigned char edata e[16];` → `error: expected ';' after top level declarator` | `PROBES/as8/edata_kw.c`（r2 新增；r1 误引 `as8/c.c`，该文件实为 `xdata`） |
| AS8 可达路径 | 仅 `__attribute__((address_space(8)))` | 同上 |
| AS8 全局进 llc | **拒**：`module uses an ABI capability outside the registered A4 v2 object identity (... other pointer address spaces ... stay unregistered)` | `PROBES/as8/b.c`,`d.c`（连"模块只有 AS8 全局、无指针参数"也拒） |
| AS8 白名单位置 | `MCS251AsmPrinter.cpp:619-620` `if (GAS != 0 && GAS != 3 && GAS != 4) return false;` | 源码 |
| lld `.mcs251.edata` 支持 | **零**（`grep -rn "mcs251.edata" lld/ llvm/lib/Target/MCS251/` 空） | 源码 |
| `--edata-end` 语义 | 只用于**栈容量**计算（`LinkerCore.cpp:3004-3007`），非 EDATA 分配窗 | 源码 |
| CRT `[0x100,H)` 连续清零 | 不存在（现有 `__mcs251_globals_init` + XINIT/XDATA_INIT walker 是另一路径） | `crt-selfstart-v2.yaml` |

⇒ DF4 的 AS8/EDATA 通路端到端未实现。加上 ≥64K 属 O09 延后项，
本缺口**不存在"独立小修"路径**。

### B.4.3 eRAM 的栈容量核算：16074 B 确定违反 1024 B 下限（评分项 3）

按 DF4 §9.2 公式（与 `LinkerCore.cpp:3002-3006` 的 `StackH` 实现一致）：

```
H        = max(0x100, exclusive end of internal occupancy)
First    = align_up(H, 16) + 16
Capacity = EDATA_END + 1 - First          # G144: EDATA_END = 0x3fff
gate:    Capacity >= 1024
```

`eRAM` 16074 B 从 `0x100` 放置 ⇒ exclusive end = `0x100 + 0x3eca = 0x3fca`：

| 量 | 值 |
|---|---|
| H | `0x3fca` |
| First | `align_up(0x3fca,16)+16 = 0x3fd0+16 = 0x3fe0` |
| SPX | `0x3fdf` |
| **Capacity** | `0x4000 - 0x3fe0 = **32 B**` |

**32 B < 1024 B ⇒ 违反栈容量下限，链接必拒。** 实测复现
（`PROBES/bv85`；用 `--reserve-data` 复刻 16074 B 从 0x100 的高水位，
链路其余为最小可链接集）：

```bash
LLD=/home/liu/build-mcs251-lld/bin/mcs251-lld
RT=/home/liu/LLVM_STC32/mcs251-demos-rewritten/tools/crt/v2
BASE="--area-start=HOME=0xff0000 --area-start=VECS=0xff0003 --area-start=BOOT=0xff0100 \
 --area-start=CSEG=0xff0200 --area-start=XINIT=0xff8000 --area-start=XDATA_INIT=0xff9000 \
 --area-start=XSEG=0x010000 --edata-end=0x3fff"

# 16074 B 内部占用（H=0x3fca）→ 拒
$LLD -flavor mcs251 $BASE --reserve-data=0x100,0x3eca \
  -o /tmp/st1.elf xseg_only.o putchar.o $RT/crt-selfstart-v2.o
# mcs251 linker: error: stack capacity is less than 1024 bytes      rc=1
```

边界与正例（同一公式的实测对照）：

| 内部占用 | H | First | Capacity | 结果 |
|---|---|---|---|---|
| 0 B（基线） | `0x100` | `0x110` | 16112 B | 过（map `stack H=0x100 ... capacity=16112`） |
| **15088 B**（`0x3af0`） | `0x3bf0` | `0x3c00` | **1024 B** | **过**（边界正例，`--reserve-data=0x100,0x3af0` rc=0；map `stack H=0x3bf0 SPX=0x3bff capacity=1024`） |
| 15089 B（`0x3af1`） | `0x3bf1` | `0x3c10` | 1008 B | 拒 |
| **16074 B（`eRAM`）** | `0x3fca` | `0x3fe0` | **32 B** | **拒** |

⇒ **`eRAM` 半不能照原尺寸（16074 B）落 G144 EDATA 并要求栈门禁通过**。
G144 上允许的最大内部占用为 **15088 B**（0x3af0）。因此：

> **语料侧旁证**：demo 85 自带说明（原始 `sample.c` 头注，GB18030）：
> "**edata 建议保留 1K 给堆栈使用**，空间不够时可将大数组、不常用变量加
> `xdata` 关键字定义到 xdata 空间。" 即 demo 作者自己要求 EDATA 留 1 KiB 栈；
> 而 `eRAM` 16074 B 只留 32 B，**与该 demo 自述相矛盾**。这也说明"eRAM 缩小
> 或移出 EDATA"是符合原意的修法，而非本调查臆造。

- `B-P2c`（r1 写"16K 零对象在 G144 窗内成功、H 与栈容量 ≥ 1024 B"）
  **必须改为拒绝用例**：16074 B 显式 AS8 对象 ⇒ `stack capacity is less
  than 1024 bytes` 硬错误。
- **新增缩小对象正例**：≤15088 B（边界值 15088 B 恰得 1024 B）在 G144
  窗内成功且容量 ≥ 1024 B。
- 对 demo 85 本身：若坚持 `eRAM` 16074 B，则**内部 RAM 路线在 G144 上
  不可行**（这独立于"AS8 通路未实现"之外的第二个阻塞）；除非缩小对象、
  或 `edata` 语义改为 XDATA 侧（即 Option A 把 `eRAM` 转 `__xdata`）。

## B.5 方案空间

### Option A（**降级：仅 T1 探针**）：拆 32K + `eRAM` 转 `__xdata`

> **r2 定位变更（评分项 2，最重要）**：r1 把本项写成"即时解锁 / 逐字节语义
> 等价"。**该定位不成立**，两条独立理由：
>
> 1. **改写后不再满足原绝对地址**：原语料用 `_at_ 0x020000`/`_at_ 0x030000`
>    把 `xRAM2`/`xRAM3` 钉在指定地址；改写丢弃了 `_at_`，地址只是**顺序放置
>    的巧合**（§B.3.3 R3），无任何保证。
> 2. **越板/QEMU 可用区**（硬阻断）：改写后 map 的
>    `_xRAM2b = [0x30000,0x38000)`、`_xRAM3 = [0x38000,0x39000)` 落在
>    128K XDATA（`[0x010000,0x030000)`）之外；`_xRAM3` 也不在 QEMU 的
>    4 KiB EXEC RAM（`[0x030000,0x031000)`）内。**这两段没有物理存储。**
>    板级容量门禁实测（Alice 复现一致）：
>
>    ```bash
>    RT=/home/liu/LLVM_STC32/mcs251-demos-rewritten/tools/crt/v2
>    $LLD -flavor mcs251 $BASE --xdata-size=0x21000 -o /tmp/sa1.elf \
>      split_all.o $RT/rt/printf.o $RT/rt/divulong.o $RT/rt/modulong.o \
>      $RT/rt/divuint.o $RT/rt/moduint.o $RT/crt-selfstart-v2.o
>    # mcs251 linker: error: XDATA capacity [0x30000,0x38000) exceeds
>    #   --xdata-size=135168 in .mcs251.XSEG._xRAM2b        rc=1
>    ```
>
>    （`$BASE` 见 §B.5.1；探针对象在 `GAP-AGG85-PROBES/demo85/split_all.o`。）
>    用 `--xdata-size=0x29000`（探针原样给的 `l_XSEG`）才会 rc=0——但那等于
>    把板级容量谎报为 0x29000，实际板/QEMU 没有那么多 RAM。
>
> ⇒ 本改写**不是 demo 解锁方案**，降级为 **"仅无物理容量门禁的 T1 探针"**：
> 它证明"限制只在单对象、拆段后可过编译器/链接器"，**不证明镜像在板上可运行**。

**实现路径**（语料内）：

1. `xRAM1[65536]`/`xRAM2[65536]` 各拆两段 32768（`xRAM1a`/`xRAM1b` 等），
   循环体同步拆（两段都写、任一不符即报错）；
2. `eRAM` 从 AS0 改 `__xdata`（AS3），避开 DSEG 128 B 窗；
3. `_at_` 仍不实现 —— **仅 `min85b` 形态下**顺序放置恰好给出原文目标地址（§B.3.3 R3）；
   **该结论不适用于 `split_all`**（拆分后目标地址改变，Alice 复核）。

**实测（`PROBES/demo85/split_all`，全链；"T1 探针"口径）**：

| 步 | 结果 |
|---|---|
| clang（语料真实 T0 flags + `_inc` + 方言/移植 include） | 0 error |
| llc（`1,2,32,8,1` + elf） | rc=0 |
| lld（`objs + printf/div 运行时 + crt-selfstart-v2.o`，**不设 `--xdata-size`**） | rc=0 |
| lld（同上 + `--xdata-size=0x21000`，即板级 128K XDATA + 4K EXEC RAM） | **rc=1**（`XDATA capacity [0x30000,0x38000) exceeds ...`） |
| map | `_eRAM 0x10000 +0x3eca`、`_xRAM1a 0x13eca +0x8000`、`_xRAM1b 0x20000 +0x8000`、`_xRAM2a 0x28000 +0x8000`、`_xRAM2b 0x30000 +0x8000`、`_xRAM3 0x38000 +0x1000`；`l_XSEG = 0x29000` |

**可用区核算**：

| 对象（改写后） | 区间 | 在板/QEMU 可用区？ |
|---|---|---|
| `_eRAM` | `[0x10000,0x13eca)` | 在（128K XDATA 内） |
| `_xRAM1a` | `[0x13eca,0x1beca)` | 在 |
| `_xRAM1b` | `[0x20000,0x28000)` | 在 |
| `_xRAM2a` | `[0x28000,0x30000)` | 在 |
| **`_xRAM2b`** | **`[0x30000,0x38000)`** | **否**（越 128K XDATA 上界；仅前 4 KiB 属 EXEC RAM） |
| **`_xRAM3`** | **`[0x38000,0x39000)`** | **否**（EXEC RAM 只到 `0x31000`） |

**语义代价（PM 必须知情）**：

- 地址连续性：`xRAM1` 从"连续 64K（01:0000-01:FFFF）"变为"两段 32K，分属
  bank 0x1 与 0x2"；对 demo 的逐字节写读校验**等价**（在探针口径下），
  对依赖连续性的用法不等价。
- "148K 连续"的标题不再字面成立；且**其算术不是连续性承诺**（见下）。
- `_at_` 的绝对地址语义仍缺（当前靠顺序巧合）。
- **物理容量越界**：改写体量（`l_XSEG=0x29000`）超过板级可用（见 §B.5.1）。

**"148K = 16K+128K+4K" 的标题算术**：该算术本身成立（16K edata + 128K xdata
+ 4K EXEC RAM = 148K），**保留**；但必须明确它**不是连续性承诺**，也**不是
同一地址空间内的连续 148K**：三段分属不同物理区（EDATA / XDATA / EXEC RAM），
`xRAM3` 与 128K XDATA 不重叠。r1 由该算术暗含的"连续 148K"读法是错的。

**与冻结面交互**：**零产品改动**；不触碰记录 v1 格式、A4 身份、P-4 签名、DF4 条款。

**T2 状态**：本探针 QEMU 30-90 s 窗内 `serial=0 B`（demo 85 台账 T2 本就是 `skip`）。
同法重放已知良品 `validation/mcs251-xdata-e2e/build/fw.hex` 可出 `XDATA-E2E-PASS`，
说明调用方式正确；未产出字节的原因未继续追。**T2 未建立，不主张（评分项 7，维持）。**

**PM 决策点 B-1（r2 重新表述）**：是否仍把该改写作为 demo 85 的**交付路径**？
（推荐：**否**。仅作为能力侧验证探针；板级可行方案见 §B.5.1。若只求 T1 通过、
不主张板上可运行，可作为"记录格式上限"的演示，但**必须标注越可用区**。）

### B.5.1 板级可行方案的重新裁定（评分项 2）

**板/QEMU 实际可用 XDATA 容量**：

| 区 | 基址 | 大小 | 可用区间 |
|---|---|---|---|
| 128K XDATA | `0x010000` | 128 KiB | `[0x010000,0x030000)` |
| 4 KiB EXEC RAM | `0x030000` | 4 KiB | `[0x030000,0x031000)` |
| 合计可用（连续跨度） | `0x010000` | `0x21000` | `[0x010000,0x031000)` |

⇒ 板级上界 = **`0x21000` 字节**（`--xdata-size=0x21000` 恰为板级真实容量）。
demo 85 的 XDATA 需求 = `xRAM1 65536 + xRAM2 65536 + xRAM3 4096 =
135168 B = 0x21000`，**恰好等于板级可用跨度**（拆段不改变总量：
`4×32768 + 4096 = 135168`）。即板级可用区**零余量**——这正是 r1 排布越界
的根因：一旦 `eRAM` 16 KiB 也进 XSEG，对象总量变 `0x24eca`、布局跨度 `0x29000 > 0x21000`，
`0x30000` 之后的对象即被推出可用区。

> **关键**：demo 85 的 XDATA 需求与板级容量**精确相等**（0x21000 vs 0x21000），
> 没有任何余量。这本身是板级设计事实，须 PM 知情：任何额外 XSEG 对象
> （包括把 `eRAM` 放进 XDATA 的 Option A 做法）都会越界。

**板级可行配置（实测，`PROBES/bv85`）**：把 `eRAM` **移出 XSEG**（它本属
EDATA 语义），只保留 XDATA 半并按板级容量链接：

```bash
# bv85/xseg_only.c: xRAM1a/b, xRAM2a/b 各 32K, xRAM3 4K（无 eRAM）
CL=/home/liu/build-mcs251-s1/bin/clang-24
LLC=/home/liu/build-mcs251/bin/llc
LLD=/home/liu/build-mcs251-lld/bin/mcs251-lld
RT=/home/liu/LLVM_STC32/mcs251-demos-rewritten/tools/crt/v2
BASE="--area-start=HOME=0xff0000 --area-start=VECS=0xff0003 --area-start=BOOT=0xff0100 \
 --area-start=CSEG=0xff0200 --area-start=XINIT=0xff8000 --area-start=XDATA_INIT=0xff9000 \
 --area-start=XSEG=0x010000 --edata-end=0x3fff"

$CL --target=mcs251-unknown-none -std=c11 -O0 -fmcs251-keil \
  -Xclang -mcs251-memory-contract=1,2,32,8,1 -S -emit-llvm xseg_only.c -o xseg_only.ll
$LLC -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -mcs251-object-format=elf \
  -filetype=obj xseg_only.ll -o xseg_only.o
$LLD -flavor mcs251 $BASE --xdata-size=0x21000 --map=bv85.map \
  -o bv85.elf xseg_only.o putchar.o $RT/rt/printf.o $RT/rt/divulong.o \
  $RT/rt/modulong.o $RT/rt/divuint.o $RT/rt/moduint.o $RT/crt-selfstart-v2.o  # rc=0
```

链接 map（与板级容量门禁同时通过）：

```
xseg_only.o:.mcs251.XSEG._xRAM1a 0x10000 +0x8000   # 128K XDATA 内
xseg_only.o:.mcs251.XSEG._xRAM1b 0x18000 +0x8000   # 128K XDATA 内
xseg_only.o:.mcs251.XSEG._xRAM2a 0x20000 +0x8000   # 128K XDATA 内
xseg_only.o:.mcs251.XSEG._xRAM2b 0x28000 +0x8000   # 128K XDATA 内
xseg_only.o:.mcs251.XSEG._xRAM3  0x30000 +0x1000   # 4K EXEC RAM 内
l_XSEG = 0x21000
```

对照：`--xdata-size=0x20000`（只算 128K XDATA，不含 EXEC RAM）会拒
`_xRAM3`（`[0x30000,0x31000)`）；`--xdata-size=0x21000`（含 4K EXEC RAM）通过。
⇒ **板级可行配置 = XDATA 半按 `0x21000` 排布 + `eRAM` 不占 XSEG**。

**eRAM 的板级归属**：`eRAM` 16074 B 属内部扩展 RAM（EDATA），
- 走 DF4-EDATA 会触发 §B.4.3 的栈容量硬错误（16074 B ⇒ Capacity=32 B）；
- 走 Option A 的 `__xdata` 侧又会吃掉 XDATA 容量、把 `_xRAM3` 挤出可用区。

⇒ **eRAM 是独立于 XDATA 64K 的第二个板级阻塞**。板级可行的 demo 85 需
**同时**：(i) `xRAM1/2` 拆段并让全部 XSEG 落 `[0x10000,0x31000)`；
(ii) `eRAM` **缩小**（≤15088 B 才满足栈门禁）或改为不落 EDATA 的用法。
这两点都不是 r1 的"即时解锁"改写所能覆盖，故 **Option A 降级**，
demo 85 的完整板级解锁**归能力侧**（DF4-EDATA 的 eRAM 决策 + 独立项的
XDATA 半边），并**须先做尺寸适配/拆分裁定**。

#### 重新裁定的板级可行路径（三选）

| 路径 | 内容 | 前置/代价 |
|---|---|---|
| **BV-1（推荐）** | XDATA 半：`xRAM1/2` 各拆 2×32K、`xRAM3` 4K，全落 `[0x10000,0x31000)`（§B.5.1 实测）；eRAM 半：**移除该数组及其相关测试**（demo 侧唯一可行形态）。注：**任何保留 eRAM 数组的形态都依赖 DF4**——AS0 进 DSEG（128B 窗，16074B 不可能）、AS8 未实现，故"缩小至 ≤15088B 放进 EDATA"同样依赖 DF4，不属于 BV-1 的 demo 侧解 | 语料侧删除数组（语义代价须 PM 接受：demo 的 SRAM 读写测试项被移除） |
| BV-2 | XDATA 半同 BV-1；eRAM 半**归能力侧**（DF4-EDATA，且要先解决栈门禁：需缩小对象或改板级 `--edata-end`） | 依赖 DF4-P0/L1/C1/Z1/R1；G144 `EDATA_END=0x3fff` 固定时 16074 B 无解 |
| BV-3 | 全归**能力侧切片**：等 XDATA record v2/链接器拆分（独立项）+ DF4-EDATA 就绪后支持（**注意：G144 的栈容量阻断不会随能力落地消失**——EDATA_END 固定时 16074B 仍无解） | 最贵；demo 85 在此期间维持 T1 gap |

⇒ **r2 裁定**：demo 85 的"板级可行方案"**不是 r1 的 Option A**。推荐 BV-1
（组合：XDATA 半尺寸适配 + **移除 eRAM 数组**）作为 demo 侧可行解；若 PM 要求
保留 eRAM（任一尺寸），则 demo 85 的完整解锁归 **BV-2/BV-3 能力侧**（DF4-EDATA），
本调查**不主张 r1 的"即时解锁"**。

### Option B：产品侧 —— 归入 EDATA / XDATA 切片（能力侧推荐，但须拆分）

拆成**两个独立项**，因为它们的成本与归属完全不同：

#### B-EDATA（eRAM 半）：归入 `DF4-EDATA` 切片

- **范围**：AS0 大数组落内部 RAM。DF4 §0.1(1) 首批只覆盖**显式 AS8**，故此处有
  两选：
  - **B-EDATA-1（推荐）**：语料侧把 `eRAM` 改**显式 edata/AS8**（需 clang 先支持
    `edata` 关键字或 `__attribute__((address_space(8)))` 拼写），走 DF4 首批范围；
  - B-EDATA-2：把 AS0 大对象纳入首批（**超出 DF4 §0.1(1)/§0.1(2) 的 PM 裁定**，
    须重新裁定）。
- **归属批次更正（评分项 3）**：eRAM 的显式 AS8 实现落 **`DF4-P0` / `DF4-L1` /
  `DF4-C1` / `DF4-Z1` / `DF4-R1`** 这五个首批片，
  **不是 `DF4-X`**（`DF4-X` = "显式 AS3/XDATA 窗口与独立初始化"，属 XDATA 那一半：
  §11 批次表、DF4 §0.1(7)/§2.2）。r1 在 §B.6.2 把 `B-P1`～`B-P3` 正确归到
  DF4-EDATA，但在 §B.4.1 的条款表里把 eRAM 半的归属错写成 `DF4-X`——已更正。
  逐片对应：

  | 片 | 覆盖 eRAM 所需的 |
  |---|---|
  | `DF4-P0` | 冻结最小 v2 身份/子协议/对象引用记录/CRT 契约（AS8 进 A4 v2 白名单） |
  | `DF4-L1` | 显式 EDATA section 读取/内部账本/窗口/H/栈/清零范围/map |
  | `DF4-C1` | 单一分类器/AS8 零对象/独立 NOBITS/严格符号引用 |
  | `DF4-Z1` | v2 CRT/EA=0/连续清零 `[0x100,H)`/启动 provider 门禁/FLASH 窗口 |
  | `DF4-R1` | 首批集成与重冻结 |

- **前置**（DF4 首批全量）：AS8 进 A4 v2 身份白名单（`:619-620`）、单一分类器
  (`GlobalPlacementDesc`)、TargetObjectFile/AsmPrinter 消费同一分类、
  `.mcs251.edata.*` NOBITS、lld EDATA first-fit（`[0x100,0x4000)` G144）、
  H/栈高水位、CRT `[0x100,H)` 连续清零 + 启动 provider 门禁。
- **规模**：DF4 §13.2 首批 13-22 人日；§11 批次 `DF4-P0/L1/C1/Z1/R1`。
- **风险（已量化，评分项 3）**：`eRAM` 16K 使 H=`0x3fca`、First=`0x3fe0` ⇒
  **Capacity=32 B，确定违反 1024 B 栈下限**（§B.4.3 实测）。G144 上允许的最大
  内部占用为 **15088 B**；`eRAM` 必须缩小或改从 demo 裁剪，否则 DF4-EDATA
  即使全部落地也无法容纳 16074 B。

#### B-XDATA64K（≥64K 半）：**独立项，非 EDATA 切片**

DF4 §15 **O09 已显式延后**："大于 64K 对象/任意对齐 —— 首批不顺带开放"。
**字面"大于 64K"覆盖不到恰好 65536 B**，而实际拒绝阈值是 `>65535`；
故独立项的范围须**显式扩展至该边界**（见 §B.4.1 "O09 边界裁定"）：
适用域 = **`object_size > 65535`（≥64K）**，句式记为
"**≥ 64K（> 65535 B）对象**/任意对齐"。
需要独立设计项（暂称 *XDATA record v2*）：

- **最少改动面**：
  - 记录 `object_size` u16 → u32（或引入"对象跨 bank 的多记录描述"）；
  - walker 加 bank 进位（`crt-xdata-init-walker.asm` 现明写"NO bank-carry handling"）；
  - llc `:2256` 上限与 lld `:2805` 上限同步放开；
  - 24 位地址形成（AS3 已支持，§8.1/DF3）。
- **两种形态分开（评分项 5）**：
  - **(a) 大记录跨 bank**：单条记录描述跨界对象 ⇒ 上列"最少改动面"全需
    （v2 记录 + walker 进位）。
  - **(b) 连续逻辑对象、多条单窗 v1 记录**：每段仍是独立单窗 XSEG 节，
    各记录自身不跨 bank（`PROBES/bv85/span_tri.*` 实测三条记录
    `bank1:0x0000/0x8000` + `bank2:0x0000`，lld rc=0）⇒ **不必然要求 walker
    进位或 v2**；但必须保证**物理连续**与符号/GEP/重定位语义。
    **物理连续非自动**（`span64_hole.*` 反证：首段 36864 B 时第二段被跳到
    `0x20000`，与首段不相邻）⇒ (b) 需后端/链接器强制相邻约束。
    且 (b) **不能保留语言对象"不跨 bank"的原裁定**（§7.4）——须重裁该条
    或把 (b) 仍归独立项。
- **代价**：记录 v1 已**冻结**（§7.2）⇒ 形态 (a) 需重冻协议 v2；
  受影响测试：`lld/test/MCS251/xdata-init.test`、`xseg-allocation.test`、
  `crt-v2.test`；受影响 fixture：`crt-selfstart-v2.yaml:259-277`（BOOT 字节块 +
  `s_XDATA_INIT`/`l_XDATA_INIT` 符号）、`crt-selfstart.yaml`；金样重冻。
  形态 (b) 可**免重冻**，但需新增"拆分片段必须连续"的链接语义。
- **架构问题**：单对象 >64K 意味着 DPXL/DPH/DPL 24 位窗跨界，运行期指针运算
  （`p + 65536`）需 24 位算术（AS3 已实现 24 位语义，见
  `XDATA-CODE-DESIGN-SUPPLEMENT.md` 头的 X2 裁定），但**对象级**的连续寻址语义
  与"对象不跨 bank"的既有裁定（§7.4）直接冲突 —— 需要新的架构裁定。
- **替代方向**：链接器自动拆分大对象（把 1×64K 变 2×32K），**保留 v1 单窗记录形态，
  但须重新裁定语言层"对象不跨 bank"约束**（改由多条单窗记录描述——原裁定不能原样保留，Alice 复核），只在语言/对象层支持"逻辑大对象"。
  **这条更省（免重冻），但需要 lld 的对象拆分语义 + 物理连续保证 + 运行期
  指针跨界处理**，仍非"小修"。即形态 (b) 的工程化。

- **PM 决策点 B-2**：≥64K（>65535 B）单对象是否立项为独立设计项？
  采用形态 (a) 记录 v2，还是形态 (b) 链接器拆分 + 连续性约束？

### Option C：仅改 `eRAM`，保留 64K 对象（不可行）

实测：`l1fix`（只 65536→65535，`eRAM` 仍 AS0）llc 过、lld 死在 DSEG；
若只改 `eRAM` 为 `__xdata` 而 `xRAM1/2` 仍 65536，llc 仍拒。**两半必须都动。**

### Option D：把 `eRAM` 也拆小以留在 DSEG（不可行）

DSEG 窗仅 128 B（`LinkerCore.cpp:2644-2654`，默认 0x80），16K 对象无解；
且 DF4 §0.1(2) 明确首批不做大小阈值迁移。

## B.6 切片

### B.6.1 demo 侧（Board-viable BV-1；Option A 已降级为 T1 探针）

| 片 | 内容 | 退出条件 |
|---|---|---|
| `B-D85-S1` | 拆 `xRAM1`/`xRAM2` 为 2×32768；全部 XSEG 落 `[0x10000,0x31000)` | T0/T1 pass；**且 `--xdata-size=0x21000` rc=0**（板级容量门禁，§B.5.1） |
| `B-D85-S2` | 循环体同步拆（两个半区都写读校验） | T0/T1 pass；语义等价说明入台账 |
| `B-D85-S3` | `_at_` 意图核对（顺序放置是否仍给原文地址）+ 台账 T2 口径 | map 地址记录；**明确 `_at_` 未实现** |
| `B-D85-S4` | `eRAM` **移除该数组及相关测试**（BV-1 形态）；缩小至 ≤15088B 放进 EDATA 属 DF4 依赖路径，不在本切片 | 移除后栈门禁自然通过；**移除的语义代价须 PM 接受** |

> r1 的 `B-D85-S1`（含 `eRAM` 转 `__xdata`）**不满足板级容量**，
> 只可作 T1 探针保留（§B.5）。demo 侧退出条件 r2 起**必须带板级容量门禁**。

### B.6.2 产品侧（Option B，能力）

| 片 | 归属 | 内容 |
|---|---|---|
| `B-P1` | **DF4-P0 / DF4-C1** | AS8 进 A4 v2 身份白名单 + clang `edata`/AS8 拼写 |
| `B-P2` | **DF4-L1 / DF4-R1** | 分类器 + `.mcs251.edata.*` + lld EDATA 窗 + H/栈 |
| `B-P3` | **DF4-Z1** | CRT `[0x100,H)` 连续清零 + provider 门禁 |
| `B-P4` | **独立项** | ≥64K（>65535 B）：形态 (a) XDATA record v2 + walker bank 进位 + fixture 重冻；或形态 (b) 链接器拆分 + 物理连续约束 |
| `B-P5` | 独立项 | `_at_` 绝对放置（与 DF4 §3.1 的"受控绝对"分类相关） |

> 归属更正（评分项 3）：`B-P1`～`B-P3` **不是** DF4-X，而是 DF4 首批五片
> `DF4-P0/L1/C1/Z1/R1`。DF4-X（显式 AS3/XDATA 窗口与独立初始化）对应的是
> **XDATA 那一半**，不是 eRAM（内部 RAM）半。

## B.7 测试矩阵

### B.7.1 板级可行版（BV-1，替换 r1 的 Option A 验收）

| # | 项 | 断言 |
|---|---|---|
| B-T1 | 拆分后每个 XSEG 对象 ≤ 65535 | map 逐条 |
| B-T2 | clang/llc/lld 全 rc=0 | T0/T1 |
| B-T3 | `l_XSEG` 覆盖预期区间（含 bank 跳转洞） | map |
| B-T4 | 拆分前后**逐字节读写的逻辑等价**（两半区覆盖原 64K 全域，索引映射 `i` / `i-32768`）；**注意这是源级逻辑断言，不等于"地址连续"** | 逻辑断言 + 源审；连续性是 BV-1 **放弃**的属性 |
| **B-T4a** | **板级容量门禁：`--xdata-size=0x21000` rc=0** | map 内所有 XSEG ⊆ `[0x10000,0x31000)`；**r1 的 `split_all` 在此项 rc=1** |
| B-T5 | `eRAM` 已移除（BV-1）后无 DSEG 窗/栈门禁失败 | map `l_DSEG` + `stack H/SPL/Capacity` |
| B-T6 | `_at_` 目标地址记录（顺序放置当前巧合满足，**不构成保证**） | map 地址 |
| B-T7 | T2 口径：`skip`（未建立）不得写成 pass | 台账 |
| B-T8 | T1 探针版（r1 Option A）明确标注"**越板级可用区，仅编译器/链接器证据**" | 文档/台账措辞 |

### B.7.2 产品侧须新增

| # | 片 | 断言 |
|---|---|---|
| B-P1a | AS8 身份 | 仅 AS8 全局的模块 llc rc=0（当前拒） |
| B-P1b | AS8 负例 | AS8 对象含非零初值 / 指针初值 → 拒（DF4 §2.3/§12.1） |
| B-P2a | EDATA 分配 | 1/32/128/129/1000/5091 B 零对象（DF4 §12.1） |
| B-P2b | 窗边界 | G12/G144 窗口边界；5091 B 三分支（DF4 §9.3） |
| **B-P2c** | **大对象（拒绝用例，评分项 3）** | **16074 B 零对象在 G144（`--edata-end=0x3fff`）⇒ `stack capacity is less than 1024 bytes` 硬错误**（H=0x3fca/First=0x3fe0/Capacity=32 B，§B.4.3 实测） |
| **B-P2d** | **大对象（正例，评分项 3）** | **≤15088 B 零对象在 G144 成功且 Capacity ≥ 1024 B**；边界值 15088 B 恰得 1024 B（map `stack H=0x3bf0 capacity=1024`） |
| B-P3a | 清零 | 预置非零 RAM 后证明 `[0x100,H)` 全零（DF4 §12.3） |
| B-P3b | 清零区冲突 | 与绝对预留/noinit 相交 → 链接拒绝（DF4 §6.2） |
| **B-P4a** | **≥64K（>65535 B）对象** | **65536/65537/128K 对象按新协议成功**（注意 r1 的 ">64K" 漏 65536） |
| B-P4b | bank 跨界 | 跨 bank 对象的目的地址/指针算术/读写（walker 进位）——**形态 (a)** |
| **B-P4c'** | **形态 (b)** | 连续逻辑对象拆多条单窗 v1 记录：**物理连续** + 符号/GEP/重定位语义；含"拆分片段不相邻"负例（`span64_hole` 反证） |
| B-P4c | 金样重冻 | `xdata-init.test`/`xseg-allocation.test`/`crt-v2.test` + 两 YAML 更新（**仅形态 (a) 需要**） |
| B-P4d | 回归 | 记录 v1 对象仍被 v1 路径接受（不破既有 fixture） |
| B-P5 | `_at_` | 显式地址对象落指定地址；冲突/越窗拒绝 |

## B.8 风险与决策点汇总（缺口 B）

| ID | 项 | 推荐 / 判断 |
|---|---|---|
| B-1 | demo 85 改写（拆 32K + `__xdata eRAM`） | **降级为 T1 探针**；板级不可行（越可用区，`--xdata-size=0x21000` 拒）。板级方案改 BV-1（XDATA 尺寸适配 + eRAM ≤15088 B）或归能力侧（§B.5.1） |
| B-2 | ≥64K（>65535 B）单对象立项 | **是**；形态 (a) 记录 v2 / 形态 (b) 链接器拆分 + 连续约束，**独立于 EDATA 切片** |
| B-3 | `eRAM` 归属 | **DF4 首批五片（DF4-P0/L1/C1/Z1/R1）**，非 DF4-X；且 16074 B 违反栈门禁（须缩小，§B.4.3） |
| B-4 | 是否把 AS0 大对象纳入 DF4 首批 | 须**重新裁定**（DF4 §0.1(1)/(2) 现为"不"）；推荐先走语料侧显式 AS8（B-EDATA-1） |
| B-5 | "148K" 标题口径 | 算术成立（16K+128K+4K），但**不是连续性承诺**；板级/QEMU 物理为 16K edata + 128K xdata + 4K EXEC RAM，三段不连续 |
| B-6 | `_at_` 绝对放置（19 处语料命中） | 与 DF4 §3.1"受控绝对"分类相关；本案不主张。改写丢弃 `_at_` 后地址**不再满足原文绝对地址** |
| B-7 | T2 是否要求 | 本调查未建立；建议维持 `skip` 并如实记录（**评分项 7：维持**） |
| B-8 | O09 边界 | O09 字面"大于 64K"须**显式扩展至恰好 65536 B**；适用域改写为"≥64K（>65535 B）对象"（§B.4.1） |

## B.9 基线（缺口 B 相关）

> **口径（评分项 6）**：以下测试族数字与进度文件一致、三个二进制的 MD5 已亲核
> （见末行）；本次评审修订**未重跑测试族**，数字沿用 r1 会话末快照。r2 只改
> 证据引用口径（edata `effective_count`）与结论，未触碰产品源码。

| 族 | 结果 |
|---|---|
| `llvm/test/CodeGen/MCS251`（排除在飞，167 文件） | **167 / 167 PASS**（r1 快照） |
| `llvm/test/CodeGen/MCS251`（全量） | 172 discovered / 170 pass / 2 fail（在飞 TFPU；r1 快照） |
| `clang/test/{CodeGen,Sema,Parser}/mcs251*` | **61 / 61 PASS**（r1 快照） |
| `lld/test/MCS251` | **23 / 23 PASS**（r1 快照） |
| demo 台账 `85-内部148K字节SRAM读写测试` | T0 pass / T1 gap（`XDATA 64K limit`）/ T2 skip |
| 二进制身份（r2 会话末，亲核） | llc `b19068c7d5f2fb8dbd336fc00f8f3d37`；clang-24 `fde0afd357155ac47b5ad31804fb9083`；lld `66c9d5e3bd4c499c23f47b358f4e0201`。**r1 记载**为 llc `af14710960e53f4bb0a6423d940a377f`、clang-24 `01fa2d7904aac05c60cf372ea59cab3c`、lld `66c9d5e3bd4c499c23f47b358f4e0201`；llc/clang 在会话期间被并行构建重建（lld 未变）。r2 的关键复现（A 的 AS0 拒/AS4 过、B 的 65536 拒/65535 过、demo85 文案）已在**重建后的二进制**上复测，层与文案不变 |

---

# 第三部分：共用结论与建议顺序

## 3.1 两缺口对照

| 维度 | A（demo 58 聚合） | B（demo 85 XDATA 64K） |
|---|---|---|
| 失败层 | llc AsmPrinter AS0 RO 门 | llc AsmPrinter AS3 记录上限（lld 同款门在后） |
| 文案（原文） | `defined global data requires a byte-aligned read-only CSEG ... aggregates ... are not supported` | `__xdata global 'xRAM1': object size 65536 does not fit the 16-bit XDATA record limit (65535 bytes; ...)` |
| 进程行为 | abort rc=134（`report_fatal_error`） | abort rc=134（`report_fatal_error`） |
| 性质 | **clang CodeGen 合成形态 × AS0 冻结作用域** 的交集 | **记录格式字段宽度**（非地址空间上限）；阈值 **≥64K（>65535 B）** |
| 语料需求 | 2 个 global、2 个头文件、1 种形态 | 3 个对象：2×65536 + 1×4096；另需 16074 B 大数组落内部 RAM（**板级栈门禁不容**） |
| 最小覆盖子集 | `<{ [N x i8], [M x i8] zeroinitializer }>`（AS0 只读） | 单对象 ≤ 65535 + 大数组 ≤15088 B → EDATA |
| 与冻结面 | AS4 §9.1 AS0-only、§6A.4 R4、5 个 AS0 拒绝面测试 | XDATA 记录 v1（§7.2/§7.4）、A4 身份白名单（GAS 0/3/4）、DF4 §15 O09（须扩至 65536 B 边界） |
| 改写规避 | **可行**（2 行 `__code`，字节恒等已实测） | **仅 T1 探针**（越板/QEMU 可用区；`--xdata-size=0x21000` 拒） |
| 板级可行 | 同 A 改写（无容量问题） | XDATA 半落 `[0x10000,0x31000)` + `eRAM` ≤15088 B（BV-1），或归能力侧 |
| 产品侧 | 不推荐（收益/代价严重不成比例） | **eRAM 归 DF4-P0/L1/C1/Z1/R1**；**≥64K（>65535 B）** 半独立立项 |
| 是否"独立小修" | 否（产品侧无窄修） | **否**（AS8 无关键字、身份门拒、lld 无 EDATA 通路、栈门禁不容 16074 B） |

## 3.2 一次读懂的对照：改写规避的字节/布局影响

| 缺口 | 改写 | 物理布局变化 | 镜像字节变化 |
|---|---|---|---|
| A | 2 表加 `__code` | 无（AS0 常量本来就在 CSEG） | **无**（sha256 实测相同：`94004b12…`/`797d6daa…`） |
| B | 拆 32K + `eRAM` 转 `__xdata`（r1 版本） | 有（地址连续性从 1×64K 变 2×32K；`eRAM` 从 DSEG 移到 XSEG）且**越板级可用区** | 有（对象地址重排；数据内容不变）；**但镜像不可运行** |

## 3.3 建议顺序

1. **A**（demo 58）：Option A 改写，2 行，立即可做，零依赖。**建议先做**。
   （A 是 r2 中唯一保持"即时解锁"定位的缺口。）
2. **B**（demo 85）**不再有"即时解锁"路径**：r1 Option A 仅作 T1 探针；
   板级可行需 **BV-1**（XDATA 尺寸适配 + `eRAM` ≤15088 B）或归能力侧，
   PM 须先裁定 eRAM 尺寸变化是否可接受（§B.5.1）。
3. **能力侧**：`eRAM` 半随 **DF4 首批 `DF4-P0/L1/C1/Z1/R1`** 落地（B-3/B-4
   决定显式 AS8 还是 AS0 首批扩展；且须先解决栈门禁）；
   **≥64K（>65535 B）** 半单独立项（B-2），与 EDATA 切片解耦，
   并在立项时把 O09 边界修正为含恰好 65536 B。

## 3.4 证据命令索引

| 编号 | 位置 |
|---|---|
| A-1 | `llvm/lib/Target/MCS251/MCS251AsmPrinter.cpp:2104-2117`（Reject 文案）、`:2187-2189`（AS0 调用点） |
| A-2 | 同上 `:899-917`（`isSupportedROType`）、`:919-968`（`isSupportedROInitializer`）、`:974-1014`（`emitROInitializer`，struct 递归 `:1004-1013`） |
| A-3 | `clang/lib/CodeGen/CGExprConstant.cpp:1394-1453`（`EmitArrayConstant`；拆分判定 `:1411-1412`、`:1418`、`:1429-1431`、`:1450`） |
| A-4 | 探针 `/home/liu/LLVM_STC32/GAP-AGG85-PROBES/agg58/`（形态矩阵 + 阈值）、`demo58/`（整模块因果对照）、`rw58/`（`__code` 改写全链 + 字节 oracle） |
| A-5 | 语料 `mcs251-demos-rewritten/src/58-.../{ASCII-10x24.h:11, picture2.h:10}`；构建 IR `build/58-.../OLED-SSD1306/*.ll:8,10` |
| A-6 | 冻结面测试 `llvm/test/CodeGen/MCS251/{global-constant-error,global-data-error,global-ro-align-policy}.ll` |
| A-7 | 设计稿 `validation/mcs251-models/proposals/AS4-AGGREGATE-INIT-DESIGN-draft.md` §6A.4/§9.1/§10 |
| B-1 | `llvm/lib/Target/MCS251/MCS251AsmPrinter.cpp:2216-2290`（`emitAddressSpacedGlobal`）、`:2247-2285`（AS3 分支）、`:2256-2259`（记录上限） |
| B-2 | `lld/MCS251/LinkerCore.cpp:2805-2811`（XSEG ≤64K）、`:2828-2833`（跨 bank 跳转）、`:3596-3686`（`validateXDATAInit`）、`:451-453`（`rangeFits` 24 位）、`:2644-2654`（DSEG 窗）、`:3004-3007`（`--edata-end` 仅栈容量） |
| B-3 | `validation/mcs251-models/proposals/XDATA-CODE-DESIGN-SUPPLEMENT.md` §7.2（记录 v1 冻结）、§7.4（65535 上限 + 跨 bank 跳转） |
| B-4 | `validation/mcs251-elf/runtime/crt-xdata-init-walker.asm`（walker，头注明写"NO bank-carry handling"） |
| B-5 | `validation/mcs251-elf/runtime/crt-selfstart-v2.yaml:117-127, 230-277, 348-375`（BOOT 块 + `s_XDATA_INIT`/`l_XDATA_INIT`） |
| B-6 | `validation/mcs251-models/proposals/DF4-EDATA-DESIGN.md` §0.1、§2.2-§2.5、§6、§8、§9.2、§11、§13.2、**§15 O09** |
| B-7 | 探针 `/home/liu/LLVM_STC32/GAP-AGG85-PROBES/demo85/`（分层残余 `l1fix`、跨 bank 反证 `min85b`、r1 改写全链 `split_all`、AS8 门 `onlyx`）；`as8/`（AS8/edata 关键字不可达） |
| **B-7b** | **r2 新探针 `/home/liu/LLVM_STC32/GAP-AGG85-PROBES/bv85/`**：板级可行配置 `xseg_only.*`、XDATA 容量与栈门禁 `span64*/span_tri*`、边界对象 `s65535.*`/`s65536.*`；栈门禁用 `--reserve-data=0x100,{0x3eca,0x3af0,0x3af1}` 复刻 |
| B-8 | QEMU 模型 `liu/qemu-src/include/hw/mcs51/stc32g.h:23-26`（`EDATA_SIZE 16 KiB`、`XDATA_BASE 0x010000 / XDATA_SIZE 128 KiB`、`EXEC_DATA_BASE 0x030000 / EXEC_RAM_SIZE 4 KiB`）；板级 `validation/mcs251-demo-modern/boards/stc32g144k246.mk`（`EDATA_END 0x3fff`）；手册 `manuals-md/G144K246/12-存储器-全球唯一ID号CHIPID.md:178` |
| B-9 | 语料 `mcs251-demos-rewritten/src/85-.../sample.c:97-102`；原始 `STC32G144K246-DEMO-CODE/85-.../sample.c:85-88` |
| B-10 | lld 测试 `lld/test/MCS251/{xdata-init,xseg-allocation,crt-v2}.test`；`stack-and-areas.test`（栈门禁 1024 B 边界） |
| **B-11** | **板级容量门禁实现**：`lld/MCS251/LinkerCore.cpp:2849-2868`（`--xdata-size` 逐节检查，错误 `XDATA capacity [lo,hi) exceeds --xdata-size=N`）；`Driver.cpp:400-407`（选项解析） |
| **B-12** | **栈容量实现**：`lld/MCS251/LinkerCore.cpp:3002-3006`（`First=(StackH+15)&~15+16`、`Capacity=EdataEnd+1-First`、门禁错误 `stack capacity is less than 1024 bytes`）；`Driver.cpp:339/362`（`EnableStackGate=true`） |
| **B-13** | **XA oracle 脚本（重做）**：`/home/liu/LLVM_STC32/GAP-AGG85-PROBES/rw58/sym_sha256.py`（符号区间 sha256；r1 的取范围错误见 §A.5） |

## 3.5 边界与未做

- 未改任何产品源码；未使用任何 git 写命令。
- 未跑 `drive.py` 全量跑批：两缺口的改写方案已逐链实测（clang/llc/lld），
  demo 级 T0/T1/T2 台账口径的变更须由 drive.py 正式跑批确认，不在本调查内。
  **r2 修订未重跑测试族**（§A.9/§B.9 数字沿用 r1 会话末快照）。
- **T2 未建立（评分项 7，维持）**：两个规避变体在 QEMU 下均未产出串口字节
  （30-90 s 窗，`serial=0 B`）。同法重放已知良品
  `validation/mcs251-xdata-e2e/build/fw.hex` 可出 `XDATA-E2E-PASS`，
  说明调用方式正确；未产出字节的原因未继续追（demo 自身 UART 初始化与模型外设的
  匹配度），**不构成任何结论**。demo 85 台账 T2 本就是 `skip`。
- **r2 新增边界**：r1 的 Option A 改写（`split_all`）**未被撤销**，但定位改为
  T1 探针；其 map 的 `_xRAM2b`/`_xRAM3` 越板/QEMU 可用区已实测（§B.5）。
  板级可行路径 BV-1 的"eRAM 缩小"为**尺寸适配建议**，语义代价须 PM 裁定，
  本调查不代裁。
- 并行实例在改 `llvm/lib`、`clang/`、`runtime`、`src/82-*`、`src/44-*`：本调查对这些
  路径只读；基线数字已标注在飞影响（llc 重建 3 次；`softfloat-reject.ll` 被改；
  新增 5 个 TFPU/softfloat 测试）。**r2 会话期间 llc 与 clang-24 又被重建**
  （见 §B.9 身份行；关键复现已在重建后复测，结论不变）。
