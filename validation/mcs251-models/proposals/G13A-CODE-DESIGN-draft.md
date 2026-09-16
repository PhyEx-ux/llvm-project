# G13a：CSEG/CODE 窗口预算缺口（7 个 demo）调查与设计（草案 **rev-2**）

**日期**：2026-09-16（rev-2：按 Alice 二审意见小范围收尾，只动三处——41 归因
证据链、版本证据分层、v2 静默归因软化；rev-1 见同文件历史；rev-0 见同文件历史）
**状态**：设计提案（待复审 → PM 裁定后实施；本文不授权实施）。
**工作树**：`/home/liu/LLVM_STC32/MCS251`，本轮实测时 HEAD `92bcfe599`（13 个 P-4
相关脏文件；只读，本文不改产品源码、未执行 git 写命令）。
**证据目录**：`/home/liu/LLVM_STC32/GAP-G13A-PROBES/`。进度与基线：
`/home/liu/LLVM_STC32/GAP-G13A-PROGRESS.md`。版本与命令行证据：该目录
`results.json`（含 S0 冻结清单、快照链与 rev-2 证据强度分层）。
**前置材料**：README.md §G13a、TIERS.md、`G8-DSEG-WINDOW-DESIGN-draft.md`、
`XDATA-CODE-DESIGN-SUPPLEMENT.md` §7、`validation/mcs251-elf/SPEC.md` §5.1、
`manuals-md/G144K246/12-存储器-全球唯一ID号CHIPID.md`、
`manuals-md/G144K246/21-IAP-EEPROM.md`、`manuals-md/STC32G/ch21-IAP-EEPROM.md`。

**口径警告（rev-2，证据强度分层）**：产品二进制在会话内外持续被并行实例重建，
工具版本**未严格冻结**。各实验的版本证据强度分层如下，不得互相代证：

- **六例链接结果（44/58/62/82/42+strlen/43+strlen，另有 41-stub）**：布局
  区间与 rc 已逐例核实（`results-r1.json`）。版本完整性 = **运行后单点哈希**
  （rev-1 各例结束后才读 clang/llc/lld 哈希），**非全程冻结**；rerun6.py 已
  于 rev-2 改为运行前+运行后各读一次并断言一致（该脚本改动未重跑，属后续
  运行的门禁）。
- **41 归因测量（attribution-41.json）与 rev-1 e2e（out/e2e-r1/）**：当时
  **未逐次记录工具哈希**，版本未严格冻结（见进度文件登记）。
- **rev-2 e2e 重跑（out/e2e-r2/）**：断言全 PASS；工具哈希单点落盘
  （clang `f4845a6c…`、llc `24d23a94…`、lld `26568a51…`，为漂移后一代），
  且每次 QEMU 运行保留退出状态与 stdout/stderr 日志。
- 实验完成后 llc/lld 相对 rev-1 漂移为 `24d23a94…`/`26568a51…`（已另行做
  82/62/44 的跨代重链抽验 rc=0，见 §3-S1）。rev-0 的 `results.json` 登记的
  `3530b34f/90f09c79/7dc8252f` 在写入时即已过期，且 rev-0 的
  FE8000/FE9000 产物**不再被本文引用**。实施前必须按 S0 重冻结并重跑全批；
  任何哈希不得追认过期产物。

---

## 0. 结论与裁定摘要

0.1 **窗口定义**（Alice 已确认，原文见 §0.6 引文块）：TIERS 的"CODE window
30976B"不是 lld 常量，而是 drive.py `AREA_ARGS` 配方产物；lld `layoutCode()`
按区域游标顺排、`reserveCode()` 做范围/重叠检查；SPEC §5.1 明文布局是构建层
配置。

0.2 **真硬件依据**：STC32G144K246 ROM 246K = `FC:2800-FF:FFFF`，FF bank = 64K
code 区，`FC:2800-FE:FFFF` = 182K ecode；**FE bank 数据放置的读路径依据在
G144 本型号手册 ch21（印 710-711/PDF 744-745）："使用 MOV 方式可以读全部
FLASH 区域"**，并给出 DRx=基地址 FC:2800+目标地址 的构造；K128 上 FE bank 是
EEPROM（MOV 可读、MOVC 不可读，STC32G ch21）。**放行范围强制限定 G144K246
验收 profile**（§6.2-D1）。

0.3 **方案主结论（rev-1 定稿候选配方）**：XINIT/XDATA_INIT 迁 FE bank，
**`XINIT=0xFE0000`（槽 640B）、`XDATA_INIT=0xFE0280`**，CSEG/BOOT/VECS/HOME
不动，CSEG 窗口 = 63744（IRQ）/65024（selfstart）。rev-0 的 256B 间隔配方
（`XDATA_INIT=0xFE0100`）已被证伪：demo 82 XINIT 270B 与 XDATA_INIT 14B 双非空
重叠 14B（`CODE overlap for .mcs251.xinit`），demo 44 XINIT 276B 同理无退路。
rev-1 配方按全树 XINIT 扫描上界（76 demo/87 section，per-demo 总量最大
276B）×2 余量与 64807B 映像止于 FE bank 内两条约束联合推导（§3-S1）。
**同一配方重跑 6 例全 rc=0**，逐例 map 区间见 §3-S1 表（含 82 双非空无重叠）。
S1 零产品源码改动；S2 是**构建层**改动（strlen 已存在于
`mcs251-runtime/src/mcs251_libc.c:63-72`，拆 TU/调构建粒度，非新增实现）。

0.4 **41 归因（rev-2 修正表述）**：一审观察到的 `_memcmp` 3v2 冲突源于**旧
缓存不一致**；全量重建运行时缓存后重测，当前 printf.o 与 gui.o 均为三参一致
（Tag 28 实测：`_memcmp` role=2/param_count=3），**冲突不再存在**——但本次
cache-manifest 未保存变化前态（before/after 字典相同），**精确的世代/头部
演进归因不做主张**（rev-1 的"记录消失/旧 2 参头"表述**撤回**：rev-1 探针
读取了不存在的 `.mcs251.signatures` section，已改为按冻结格式解码
`.mcs251.attributes` Tag 28，实测落盘 `signature-decode-r2.json`）。重建后
41 的首错为 `CODE address overflow in .text`，属**真窗口问题**（分流结果
(c)，进 S3/S4 账）。lld 实测 41 完整 CSEG 需求
**76084B**（含 printf 运行时 14035B CSEG 贡献、171B strlen 探针、11B
putchar 探针；去掉 putchar 探针即 76073B），对 63744 窗口缺口 12340/12329B。
`memcmp` 3v2 不属于零参豁免、由 BinaryFormat 严格拒绝是正确行为
（`MCS251Signatures.cpp:248-268` 仅对双方 ParamCount==0 的 bit2 差异放行）。

0.5 **两组证伪（rev-1 修正表述）**：
- llc codegen 档位不是杠杆：默认即 -O2（`llc.cpp:124-129` `cl::init('2')`；
  gui `.text` 默认与显式 -O2 哈希相同），该结论不依赖样本统计。
- clang IR 级 -O2 反增：**样本 = demo41 的 5 个 TU（LCD/gui/main/sys/test）**，
  56944B→84486B；机理 = 无可观察副作用的忙等循环被删除（`sys.c:22-35` 局部
  变量空循环 → `sys.O2.ll:6-14` 两个 delay 只剩 `ret void`），不是调度"饿死"。
  rev-0"7 个 demo 均反增"**撤回**。结论保留为：不可全局切 -O2 扩窗；
  **经审查的逐 TU 优化未被禁止**。

---

## 0.6 Alice 已确认成立的三项核心结论（原文保留）

> 1. **30976B是配方产物，不是lld常量。**
>    [drive.py:92–103](/home/liu/LLVM_STC32/mcs251-demos-rewritten/tools/drive.py#L92)决定起点；
>    [LinkerCore.cpp:2351–2390](/home/liu/LLVM_STC32/MCS251/lld/MCS251/LinkerCore.cpp#L2351)负责范围、重叠和对齐；
>    [SPEC:407–415](/home/liu/LLVM_STC32/MCS251/validation/mcs251-elf/SPEC.md#L407)明确是构建层配置。
>    共享头实际路径为
>    [/home/liu/LLVM_STC32/MCS251/llvm/include/llvm/BinaryFormat/MCS251ISR.h:321–336](/home/liu/LLVM_STC32/MCS251/llvm/include/llvm/BinaryFormat/MCS251ISR.h#L321)，
>    不是`lld/MCS251/MCS251ISR.h`。向量assert与IRQ BOOT下限保留，单纯迁移数据区域不要求修改这些产品约束。
>
> 2. **两套CRT在寻址机制上支持迁移。**
>    [IRQ CRT:124–161](/home/liu/LLVM_STC32/MCS251/validation/mcs251-elf/runtime/crt-irq-v2.yaml#L124)、
>    [selfstart CRT:240–277](/home/liu/LLVM_STC32/MCS251/validation/mcs251-elf/runtime/crt-selfstart-v2.yaml#L240)
>    都有HI8/MID8/LO8源地址重定位；
>    [walker:70–89](/home/liu/LLVM_STC32/MCS251/validation/mcs251-elf/runtime/crt-xdata-init-walker.asm#L70)
>    确实使用MOV @DR，而非MOVC。机制相容不等于当前实验已覆盖两套CRT。
>
> 3. **G13a与G13b的两种上限并不矛盾。**
>    58/62是非零载荷ROM映像从FF9000溢出24位顶端；demo85的`0x21000`是XDATA RAM总跨度，
>    采用全零对象的clear-only记录，不生成同等大小ROM载荷。
>    [G13B稿:23–28、229–238](/home/liu/LLVM_STC32/MCS251/validation/mcs251-models/proposals/G13B-XDATA-DESIGN-draft.md#L23)
>    对此区分正确。G13a需修正的是溢出十进制数，不是改判为G13b记录上限问题。

---

## 1. 现状与精确画像

### 1.1 复现（基线配方，rev-0 会话实测；类别与 TIERS 登记一致）

| demo | 组 | CRT | T1 首错 |
|---|---|---|---|
| 41 | - | crt-irq-v2 | `MCS251 signatures: records '_memcmp' … 3 vs 2`（**rev-2 已归因为旧缓存不一致，见 §1.5**） |
| 42/43 | - | crt-selfstart-v2 | `CODE overlap for .text` |
| 44 | - | crt-irq-v2 | `CODE overlap for .text` |
| 58/62 | TFT / - | crt-irq-v2 | `CODE address overflow in .mcs251.xdata_init` |
| 82 | - | crt-irq-v2 | `CODE overlap for .text` |

### 1.2 窗口差值表（字节数；基线配方 30976/32256）

| demo | CRT | CSEG 需求 | Δ基线窗口 | XINIT | XDATA_INIT | rev-1 配方窗口 | Δ | 结论 |
|---|---|---|---|---|---|---|---|---|
| 41 | irq | **76084**（实测，§1.5） | -45108 | 104 | 0 | 63744 | -12340 | 真窗口问题：S3+S4 叠加（§3-c） |
| 42 | self | 56960（含 strlen） | -24704 | 86 | 0 | 65024 | +8064 | S1+S2 解（rc=0 实链） |
| 43 | self | 56814（含 strlen） | -24558 | 86 | 0 | 65024 | +8210 | 同上 |
| 44 | irq | 34252 | -3276 | 276 | 0 | 63744 | +29492 | S1 解（rc=0 实链） |
| 58 | irq | 5049 | +25927 | 20 | **64807** | 63744 + 映像入 FE | ✓ | S1 解（rc=0 实链） |
| 62 | irq | 5662 | +25314 | 20 | **64807** | 同上 | ✓ | 同上 |
| 82 | irq | 38812 | -7836 | 270 | 14 | 63744 | +24932 | S1 解（rc=0 实链） |

注：42/43/58/62/82/44 的 CSEG 需求在 rev-1 重跑 map 中由 lld 直接给出
（`l_CSEG`，`results-r1.json`），与 rev-0 inventory 模拟值一致；41 的 76084
是 §1.5 探针实测。41 完整需求含 strlen（rev-0 的 75902 未计）与 putchar
提供者。

### 1.3 构成要点（rev-1 口径修正）

- **printf 运行时两种口径分开列**：函数字节（FUNC 符号和）**13993B**
  （`_format_engine` 8572 + `_out_float` 4469 + `_get_arg` 471 + `_sprintf`
  196 + `_out_char` 150 + `_out_str` 84 + `_printf` 51）≠ CSEG 贡献
  （`.text` section）**14035B**。链接预算按后者记账。
- 常量表（41）：8972B ROM 表（`_tfont16` 1972、`_asc2_1608` 1536、
  `_asc2_1206` 1152、`_tfont24` 592、`_tfont32` 520、test `_gImage_qq` 3200）。
- TFPU NOP 链：7 demo 无一使用 f32 运算，不构成因素。
- P-4 Tag 28：签名记录不占 CODE 窗口；41 的 memcmp 首错已按 §1.5 归因为
  旧缓存不一致（当前输入三参一致）。
- **新发现（本轮）**：demo 41 引用 printf 却**未定义 putchar**（44/82 自带）。
  这是 41 的第三隐藏层——即使 G13d 与窗口问题解决，按需闭包仍会以
  `undefined symbol: _putchar` 失败。 putchar 提供者归入 S2/D4 运行时切片。

### 1.4 xdata_init 映像的构成与精确溢出算术（rev-1 修正）

`.mcs251.xdata_init` 64807B = X4 记录头 7B + `_IMG_DATA` 64800B（NOBITS，
SPI-DMA.o，`object_size=payload_size=0xFD20`）。基线配方从 `0xFF9000` 放置：

```text
0xFF9000 + 64807 = 0x1008D27
0x1008D27 - 0x1000000 = 0x8D27 = 36135B 溢出（rev-0 的 36071B 为算术错误，撤回）
```

FE 容量口径区分（勿混）：
- FE 整 bank：65536B；
- rev-0 定稿 `0xFE0100→0xFF0000`：65280B；
- rev-1 定稿 `0xFE0280→0xFF0000`：**64896B**；
- 65024B 是 **selfstart 的 FF CSEG 窗口**（`0xFF0200→0x1000000`），与 FE 无关。
- 58/62 的 20B XINIT 位于 XINIT 槽内、64807B 映像止于 `0xFEFFA7`（余 89B）；
  该账已含在 64896B 容量内，不再从容量中重复扣除 XINIT。

**Walker 16 位总长约束（显式）**：XDATA_INIT walker 以 `mov wr4,#l_XDATA_INIT`
载入总长（`crt-xdata-init-walker.asm:66`），XINIT walker 同构
（`crt-selfstart.asm:91` `mov wr4,#l_XINIT`）——WR4 为 16 位，**单表上限
65535B；65536B 表的端到端可用性未验证**，任何容量承诺不得引用 65536B。

### 1.5 demo 41 归因实验（rev-2 修正；Alice 二审 item 3）

顺序：同步并行修复（工作树已含标准 3 参 memcmp：`mcs251_libc.h:65-70`、
`mcs251_libc.c:74-75`）→ **全量重建运行时缓存**（`tools/crt/v2` 下 18 个
对象全部重建，逐对象哈希入 `cache-manifest.json`；rev-1 的 objects 清单漏列
printf.o 与 nop-helper.o，rev-2 已补列，16→18）→ 分层重测 41。

- **签名载体与实测（rev-2 关键修正）**：签名记录承载于
  `.mcs251.attributes` 的 **Tag 28**（冻结格式），**不存在**
  `.mcs251.signatures` section——rev-1 探针用 readelf 读该节并把未命中记为
  "(none)"，产生"记录消失"的错误结论，**撤回**。rev-2 探针
  （`rebuild41.py --decode-only`）按冻结格式严格解码 Tag 28（参考
  `llvm/lib/BinaryFormat/MCS251Signatures.cpp` 的 decode 语法；解码失败一律
  报错，不得记"(none)"），实测落盘 `signature-decode-r2.json`：
  - `tools/crt/v2/rt/printf.o`（SHA256 `239b6db7…`）：10 条记录，
    **`_memcmp` role=2 / param_count=3**；
  - `build-41r1/gui.o`（SHA256 `65aac835…`）：70 条记录，
    **`_memcmp` role=2 / param_count=3**。
  printf.o 哈希与 cache-manifest.json 的 before/after 条目相同；当前
  `printf.ll:1264-1277` 仍含 `_memcmp` 签名元数据（编译器身份
  `92bcfe599…`），如实登记。
- **归因表述（rev-2 定稿）**：一审观察到的 3v2 冲突源于**旧缓存不一致**；
  当前重建后 printf.o 与 gui.o 均为三参一致，**冲突不再存在**——但本次
  cache-manifest 未保存变化前态（before/after 字典相同），**精确的世代/
  头部演进归因不做主张**。
- 重测 41 首错 = `CODE address overflow in .text` → 分流结果 **(c) 真窗口
  问题，进 S3/S4 账**。(a)（memcmp 3v2 残留）与 (b)（零参/原型性层）均未
  出现（零参豁免属 P-4 在途修复的 reader 侧语义，`MCS251Signatures.cpp:
  248-268`，writer 保留源级 bit2 见 `MCS251.cpp:393-408`——IMPL-P4Z-PROGRESS
  :15-18 的早期方案与现行代码不同，引用在途进度不能替代对现行语义的核对）。
- **预算实测**：探针专用加宽窗口（`CSEG=0xFC2800`，仅测量用）+ 同一 FE 数据
  配方 + strlen/putchar 探针提供者 → rc=0，`l_CSEG=0x12934`=**76084B**。
- 三种结果的事前处置路径均预写并成立：(a)→缓存纪律；(b)→P-4 覆盖确认；
  (c)→本文 S3/S4 账。实测落在 (c)。
- 归因边界：`memcmp` 3v2 是**非零参数个数冲突**，不属于零参 bit2 豁免；
  BinaryFormat 对其严格拒绝是冻结协议的正确行为，不是缺陷。
- 版本口径：该归因测量运行于 rev-1 会话（当时工具代），**运行时未逐次记录
  工具哈希，版本未严格冻结**；测量值（76084B/首错文案/Tag 28 解码）随 S0
  重跑复验。

---

## 2. 机制定位

### 2.1 lld 侧（`lld/MCS251/LinkerCore.cpp`，与 rev-0 一致处从简）

- 区域游标/重叠/对齐：`layoutCode()`/`reserveCode()`（:2351-2390）；VECS 合成
  与 BOOT 下限断言在 `MCS251ISR.h`，纯数据区迁移不触碰。
- ROM gate：`checkFlashGate()` 由 `--flash-base/--flash-size` 驱动，保持可选
  （:3319-3320）；**门禁还检查 CODE 类区域的配置起点必须落在窗口内——包括
  零长度区域（:3332-3338）**，即开启门禁会拒绝某些原本可接受的配方，
  回归面待全批重验后再报（D3）。
- FE 配方下门禁相容性：HOME/VECS/BOOT/CSEG/XINIT/XDATA_INIT 全部起点
  （含 `0xFE0000/0xFE0280`）均落在 `[0xFC2800, 0x1000000)` 内。

### 2.2 硬件侧（per-part，rev-1 重写引文）

- **G144K246（验收 profile 机型）**：
  - ch12 存储器映像：FE 属 182K ecode（印 266-267/PDF 300-301）。
  - **本型号读路径直接依据：ch21（印 710-711/PDF 744-745，
    `manuals-md/G144K246/21-IAP-EEPROM.md:250-267`）："EEPROM 本质上是复用了
    FLASH 空间，所有的 FLASH 空间都可以运行代码（包括 EEPROM 区域）"；
    "使用 MOV 方式可以读全部 FLASH 区域"；DRx 构造 = 基地址 `FC:2800h` +
    EEPROM 目标地址（手册例：读 EEPROM 1234h 须置 DRx=`FC:3A34h`）。**
    不再借 K128 论证 G144 的读路径。
  - **G144 自身擦除风险（手册 :252 警告）**：EEPROM 从 `FC:2800` 起向上规划；
    "如果 EEPROM 范围包括了程序区域，需要小心擦除时候的地址设定，如果擦除掉
    代码部分会导致运行卡住/跑飞等问题"。**保护初始化镜像的要求适用于 G144
    本型**：ISP 的 EEPROM/IAP 操作区设置不得覆盖 FE 数据窗口，IAP 擦除不得
    以 XINIT/XDATA_INIT 区为目标；镜像烧录链必须保护这两个区域。
- **STC32G12K128**（ch21 PDF 912/书 872，`manuals-md/STC32G/ch21-IAP-EEPROM.md:
  187-207`）：EEPROM 自 `FE:0000` 起（红字：无论设置多少，始终从 FE:0000
  开始）；MOV 读取用 DRx=`FE:xxxx`；**"STC32G 系列和 STC8 系列不一样，不能
  使用 MOVC 读取 EEPROM"**。K128 上 FE 配方与板级 EEPROM 配置直接冲突，
  **不能仅凭"未占用 EEPROM"自动取得与 G144 相同的容量与烧录承诺**。
- **CODE banking**：无此机制（N/A），不变。
- 机制注：两套 CRT 的 XINIT/XDATA_INIT 消费 walker 均为 `MOV @DRx` 24 位读
  （`crt-selfstart.asm:88-125`、`crt-xdata-init-walker.asm:62-89`），
  **不依赖 MOVC**，FE 放置与消费指令相容。

### 2.3 QEMU 侧（rev-1 限界表述）

- QEMU 确实执行了 DR 读路径，但**不是独立硬件证明**：
  - `helper.c:446-463`：DR 间接读进入普通 24 位数据读（`mcs251_load8`）；
  - `stc32g.c:59-64、102`：246K 窗口整体 `memory_region_init_rom` 直接映射；
  - `cpu.c:808-841`：无针对 FE 的独立读取资格校验。
  因此 PASS 仅证明"**链接重定位 + walker 与该模型假设相容**"；真机 ISP 烧录、
  EEPROM/IAP 保护配置、真实总线读权限是 **D2 真机验证项**。
- 覆盖实证（§3-S1）：rev-1 全部落盘 `out/e2e-r1/`，rev-2 脚本调整后重跑
  落盘 `out/e2e-r2/`（断言全 PASS）：各 8 次链接 / 8 次 QEMU 运行 /
  2 对 transcript 逐字节相等 / 2 对 link 级验证 / 1 项 BOOT 下限负控；
  rev-2 起每次 QEMU 运行保留退出状态（`.qemu.rc`）与 stdout/stderr 日志
  （`.qemu.out/.qemu.err`）及工具哈希单点记录（`tool-hashes.txt`）。
  rev-0"61 条 transcript"的主张**撤回**（当时实际落盘 2 条）。
- **新发现（本轮登记，rev-2 软化归因）**：v2 合同镜像在本 QEMU 模型下
  **运行/串口静默，BASE 亦复现**（e2e 固件与探针固件均复现；四份 v2
  BASE/FE serial 均为空）。**根因待定位**：v1/v2 selfstart CRT 的原始 BOOT
  字节与符号表相同**不足以排除**最终重定位、入口或运行态差异；空 serial
  亦**不能区分**"正常运行未输出/加载失败/异常退出"。rev-2 e2e 重跑记录
  的退出状态为 rc=124（跑满 40s 窗口被 timeout 终止，QEMU 进程无异常退出，
  stderr 仅含 timeout 终止说明），可排除 QEMU 加载层失败，但静默根因仍未
  定位。该既有现象单独立项跟踪；本文的 v2 矩阵证据止于链接+map 层。

---

## 3. 方案空间

### (a) 窗口扩大

**S1：FE-bank 配方（rev-1 定稿候选；最终冻结值留待实施轮）**

- 配方（drive.py `AREA_ARGS`/`AREA_ARGS_IRQ`，探针以环境开关
  `MCS251_DRIVE_RECIPE=g13a-fe640` 验证，默认行为未变）：
  `--area-start=XINIT=0xfe0000 --area-start=XDATA_INIT=0xfe0280`；
  CSEG/BOOT/VECS/HOME 不动。可选同批打开
  `--flash-base=0xfc2800 --flash-size=<246K>` ROM 门禁。
- **推导（两条硬约束联立）**：
  1. XINIT 槽 ≥ 2× 全树 per-demo 实际最大值。`xinit-scan.json`：全 rewritten
     demo 树 87 个 `.mcs251.xinit` PROGBITS section / 76 个 demo，per-demo
     总量最大 258B（旧世代对象）；当前工具代 7 demo 实测最大 **276B**
     （demo 44）。→ 槽 ≥ 552B，取 **0x280=640B（2.32×）**。
  2. 当前最大映像 64807B 必须整体留在 FE bank：`XDATA_INIT ≤ 0xFE02D9`。
     → `0xFE0280`，容量 64896B，映像余 89B。
  - rev-0 的 256B 间隔被 demo 82（XINIT 270B + XDATA_INIT 14B 双非空重叠
    14B）证伪；"XINIT 恒小于 256B"不是通用前提。
- **有效域与升级规则（显式）**：配方对"per-demo XINIT 总量 ≤ 640B 且映像
  ≤ 64896B"成立；越界时按实例确定性重推导（`XDATA_INIT = 0xFF0000 − 映像`
  两遍链接法）或走 (b)/D6 `--area-end`，不做静默放大。
- **同一最终配方重跑 6 例（全部 rc=0，map 实测区间；无 FE8000/FE9000 旧产物）**：

  | 例 | CRT | rc | XINIT 区间 | XDATA_INIT 区间 | l_CSEG |
  |---|---|---|---|---|---|
  | 44 | irq | 0 | [0xFE0000, 0xFE0114) | 空 | 34252 |
  | 58-TFT | irq | 0 | [0xFE0000, 0xFE0014) | [0xFE0280, **0xFEFFA7**) | 5049 |
  | 62 | irq | 0 | [0xFE0000, 0xFE0014) | [0xFE0280, **0xFEFFA7**) | 5662 |
  | 82 | irq | 0 | [0xFE0000, 0xFE010E) | [0xFE0280, 0xFE028E)（**双非空，无重叠**） | 38812 |
  | 42+strlen | self | 0 | [0xFE0000, 0xFE0056) | 空 | 56960 |
  | 43+strlen | self | 0 | [0xFE0000, 0xFE0056) | 空 | 56814 |
  | 41-stub（分解探针） | irq | 0 | [0xFE0000, 0xFE0056) | 空 | 62177 |

  证据：`results-r1.json`（逐例完整 lld 命令行 + rc + map 区间；工具哈希为
  rev-1 运行后单点记录——六例**布局与 rc 已核实**，版本完整性为单点证据、
  非全程冻结，见文首口径警告）、`build-r1/*/demo.map`。42/43/41-stub 的
  提供者均为探针对象（`strlen-rt.c` 171B、`printf-stub.c` 139B，本轮以当前
  工具重编）。
- **跨代抽验**：实验完成后 llc/lld 漂移（`24d23a94…`/`26568a51…`），用新
  二进制对 82/62/44 重链 rc=0 且 map 区间逐字节一致——配方不脆弱，但
  S0 全批重跑仍是实施前置。
- **QEMU 行为保持矩阵**（探针 `xinit-fe-e2e.sh`，全部自动断言）：
  - A 对（v1 链 xdata-e2e + clear-only 毒化）：base 与 FE 双跑终端
    `XDATA-E2E-PASS` 且 transcript **逐字节相等**（cmp 断言）；
  - B 对（v1 链，**非空 XINIT 28B 迁移 + 非空 XDATA_INIT**，校验初始化
    DATA 变量逐字节值）：双跑 `XINIT-FE-PASS`、逐字节相等、FE map
    `s_XINIT=0xfe0000`、`l_XINIT=0x1c>0`——非空 XINIT 搬运实证；
  - C/D（v2 selfstart / v2 irq 矩阵）：链接级断言（rc=0、FE map 区间、
    l_XINIT>0）+ QEMU 运行记录；串口静默为 §2.3 所述**根因待定位的既有
    v2 现象**（BASE 布局亦静默；退出状态与日志已保留，rc=124 跑满窗口），
    不计入本方案证据；
  - 负控：IRQ CRT 配 BOOT=0xFF0100 被拒（`CODE overlap for .mcs251.BOOT`，
    向量区 0xFF0003-0xFF03FB 保留），下限机制工作正常。
- ABI/身份影响：无（同 rev-0 论证：对象世代、签名协议、记录格式、边界符号
  语义不变；仅 `s_XINIT/s_XDATA_INIT` 数值随布局变化）。
- 风险：真机未验（D2）；G144 板级 EEPROM/IAP 配置与镜像保护要求见 §2.2；
  映像 >64896B 的未来 demo 走升级规则。

**S2：`_strlen`（与 41 的 `_putchar`）运行时切片——构建层改动**

- **现状修正**：strlen 已实现于 `validation/mcs251-runtime/src/mcs251_libc.c:63-72`
  （171B 探针对象即按其同构实现）。S2 = **从既有文件拆出独立 TU/调整构建
  粒度**（使按需闭包可单独拉入），**不新增实现、避免重复定义**。构建层改动
  属产品源码改动——**撤回 rev-0"S1+S2 零产品源码改动"表述，改为：
  S1 零产品源码、S2 构建层**。
- 41 的 `_putchar` 缺口（§1.3）并入本切片的归属讨论（D4）。
- 闭包规则：`_strlen` ∈ UND → 拉入 str TU（与 div/printf 闭包同构）。

**(b) xdata_init 外移的其他形态（备选/升级路径，不独立立项）**

- XINIT 留 FF、映像独占 FE（`XINIT=0xff8000 + XDATA_INIT=0xfe0000`）：容量
  65536B——**受 walker 16 位 WR4 总长限制，单表实际上限 65535B，且 65536B
  端到端未验证（§1.4 显式约束）**。仅适用于映像 >64896B 且 CSEG 不满窗场景。
- `--area-end`（顶端锚定）：新 lld 特性，**D6 独立立项；本轮放行不包含其
  实现，也不得把它当作 S1 容量承诺的补丁**。
- S5：CSEG 尾段入 ecode（不变，后备）。

**(c) 代码缩减（41 专项）**

- **S3：printf 运行时拆出 `_out_float`（−4469B 函数字节）**。现状
  `mcs251_printf.c:613-625`：`%f/%F`、`%g/%G`、`%e/%E` 分支**直接调用
  `out_float`**——只拆 TU 会保留 UND、闭包仍拉入浮点对象，不拉则链接失败。
  **必须同时提供无浮点引擎**：
  - 默认完整实现（浮点格式化可用）；
  - 仅当**可证明**程序无浮点格式时才条件编译裁剪（如 `-DMCS251_PRINTF_NO_FLOAT`
    配方开关，链接期格式串校验兜底）；
  - 判定规则必须覆盖 **`%f` 与 `%g`**、**精度格式**（`%.2f`）与**非字面量
    格式串**（运行期拼串）：无法证明无浮点格式时一律保留完整实现；
    仅扫描"`%f` 是否出现"不保语义。
- **S4：大常量表 XDATA 化（−8972B，待验证收益）**：收益未计变换后新增取数
  代码、AS4 指针/跨 TU 兼容、XDATA 容量与记录头重记账；独立 DF 设计稿。
- 41 账（S3 收益只扣一次）：`76084 − 4469 − 8972 = 62643`，对 63744 窗口
  **余 1101B**（若按无 putchar 探针口径 76073：`62632`，余 1112B）——
  未计 S4 变换后新增取数代码，且 S4 是待验证收益，**不据此宣布 41 闭环**。
- 证伪项（修正表述，§0.5）：clang IR -O2（demo41 样本反增、忙等环被删）；
  llc 默认 -O2（证伪保留，不依赖样本统计）；TFPU NOP 链；静态槽写序列。

**(d) CODE banking**：N/A（不变）。

### 方案对比（rev-1）

| 方案 | 产品改动 | 解锁 | 41 | 风险 |
|---|---|---|---|---|
| S1 FE 配方（g13a-fe640） | **零**（harness+文档） | 44/58/62/82（rc=0） | 缺口 -12340 | 真机未验（D2） |
| S1+S2 | S2=构建层切片 | 6/7（rc=0） | 不解 | 低（合并面协调） |
| +S3 无浮点引擎拆分 | runtime 中切片 | — | -4469，仍 -7871 | 低-中 |
| +S4 表 XDATA 化 | llc 新特性（DF 级） | 41（若收益成立） | 余 ~1101，**待验证** | 中（ABI 面） |
| S5 CSEG2 ecode | lld 新区域 | 41（等价） | 闭环且宽松 | 中 |
| 接受残余 | 无 | 6/7 | 41 记 gap | 0 |

---

## 4. 切片划分与解锁映射

| 切片 | 内容 | 主要落点 | 依赖 | 解锁 |
|---|---|---|---|---|
| S0 | 工具/源码/CRT/运行时缓存四类冻结 + 全批重跑 | 惯例 | — | 全部 |
| S1 | FE-bank 配方（g13a-fe640）+ SPEC/TIERS 重冻结 + QEMU/真机验证 | drive.py 配方段（harness）、SPEC 文档 | S0 | 44、58、62、82 |
| S2 | strlen/putchar 运行时切片（构建层）+ 闭包规则 | `validation/mcs251-runtime` 构建粒度、drive.py `_runtime_for` | S1；与 runtime 在途改动协调 | 42、43 |
| S3 | printf `_out_float` 拆分 + **无浮点引擎**（§3-c 规则） | `validation/mcs251-runtime` | S0 | 41 部分预算；全族余量 |
| S4 | 大常量表 XDATA 化（**待验证收益**） | llc（独立 DF） | S1+S3；PM D5 | 41（待验证） |
| S5（后备） | CSEG2 ecode | lld + 烧录链 | PM D5 | 41（等价） |

并行边界：S1 零产品源码；S2/S3 同在 runtime 目录须串行或分目录；S2 构建层
改动与"零产品码"表述已切割（§3-S2）；S4/S5 各走独立设计稿。

---

## 5. 测试矩阵

### 5.1 lld
现有 24 项回归（布局参数不变式）+ FE 配方 7 demo 判定进 TIERS 重冻结。
门禁语义检查含 :3332-3338 的空区域起点规则（D3）。

### 5.2 harness
- FE 配方下 6 例 rc=0（本轮已实测，§3-S1 表）；41 保持 gap，备注更新为
  "CODE 窗口（-12340B，待 S3+S4/S5）"（P-4 首错已归因为旧缓存不一致，
  §1.5，不再并列）。
- 全批 78 程序行在 S0 重冻结后重跑：**全批重验前不宣称回归面**（含 ROM gate
  开启后的 :3332-3338 新拒绝面，D3）。
- xdata-e2e、a3 族 e2e 全套在 FE 配方重跑（探针矩阵已覆盖 v1 行为保持）。

### 5.3 启动与运行
- QEMU：§2.3 的限界表述 + e2e 矩阵（8 run，rev-1 `out/e2e-r1/`、rev-2 重跑
  `out/e2e-r2/` 含逐运行退出状态与日志）；62 FE 镜像 40s 窗口无崩溃
  （g13a-fe640 下复测 rc=124、0 串口字节，LED 型预期）。
- 真机（D2）：144K246 板烧 FE 配方镜像，验证 MOV@DRx 读 FE 与向量表不受
  影响；**板级 ISP EEPROM/IAP 设置不覆盖 FE 数据窗口、IAP 擦除不触及镜像**
  （§2.2 G144 警告）；K128 不在本配方放行范围。

### 5.4 基线
rev-0 会话实测（当时工具代）：CodeGen/MCS251 175/175、lld 24/24、
clang CodeGen 31/31、Sema 25/25、Driver 1/1。本轮按"产品二进制以现状实测、
不重建不追认"原则未重跑 lit；**实施轮 S0 必须在重冻结二进制上重测全基线**。

---

## 6. 风险与 PM 决策点

### 6.1 风险
- R1 真机未验 + per-part：QEMU 证据限界（§2.3）；放行范围强制限定
  G144K246 验收 profile；G144 自身 EEPROM/IAP 配置的擦除风险与镜像保护
  要求（§2.2）写入配方文档。
- R2 映像余量：64896−64807=89B；升级规则显式（§3-S1），不静默放大。
- R3 合并面：S1 只动 AREA_ARGS；S2/S3 在 runtime 构建粒度，须与在途
  runtime 改动协调；TIERS/账本重冻结全局协调。
- R4 41 预算按当前工具代实测（76084B）；codegen 演进会移动该数，S3/S4
  立项前重测。
- R5 二进制漂移：本轮内部已发生两次；S0 四类冻结 + 逐例哈希是强制门禁。
- R6（新）v2 合同 QEMU 运行/串口静默（BASE 布局亦复现）：**根因待定位**
  （§2.3；CRT 字节相同不足以排除重定位/入口/运行态差异，空 serial 不能
  区分未输出/加载失败/异常退出），单独立项；在定位前，v2 链的行为证据
  止于链接+map 层。

### 6.2 PM 决策点
- **D1**：FE 配方（XINIT=0xFE0000、XDATA_INIT=0xFE0280）放行为新验收
  profile（SPEC §5.1 表 + TIERS 重冻结）。**per-part 差异文档化为强制项**：
  放行范围限定 G144K246 验收 profile 及其板级 Flash/EEPROM 占用前提；
  K128 不自动获得同等承诺；G144 自身 EEPROM/IAP 配置警告与镜像保护要求
  随配方文档化。
- **D2**：真机验证前置还是并行（建议并行：QEMU 证据已按 §2.3 限界表述，
  真机烧录/保护配置排期）。
- **D3**：ROM 门禁是否随 S1 默认打开（建议打开，作为 profile 默认项）。
  **删除 rev-0"零回归面/风险 0"表述**：门禁的空区域起点检查（:3332-3338）
  会拒绝原来可接受的某些配方，**全批重验后再报回归面**。默认传参不改变
  lld 自身的可选语义（:3319-3320）。
- **D4**：strlen（及 41 的 putchar）运行时切片归属与闭包规则（建议并入
  runtime 族下一切片；S2 为构建层改动）。
- **D5**：41 处置：G13d 落地与缓存重建后，旧缓存不一致已消除（当前输入
  三参一致，§1.5），41 的真首错即 CODE 窗口；建议顺序 S3（含无浮点引擎）
  → 再议 S4（待验证收益）/S5。
- **D6**：`--area-end` 顶端锚定独立立项；**本轮放行不包含其实现**，亦不得
  作为 S1 容量承诺的补丁。

### 6.3 放行原则（建议）
S1 零产品源码、证据链含双布局 transcript 逐字节保持与 6 例实链闭环；
S2 为构建层改动（非零产品面），与 S1 分项记账；S3 起为产品改动，各走独立
设计稿与评审。

---

## 7. Alice 二审 10 条判定与 rev-2 处置表

rev-1 曾按一审 9 条列表；二审（2026-09-16）改为 10 条逐项核验，判定 7 条
闭合、3 条未闭合。下表为**二审 10 条判定 + rev-2 处置**（本轮只动
第 2、3、10 三处，其余保持 rev-1 内容）。

| # | 二审判定 | 二审核验摘要 | rev-2 处置 |
|---|---|---|---|
| 1 新配方推导 | **已闭合** | `xinit-scan.json` 按 demo 求和确为 87 条/76 键、旧世代最大 258B（demo44 的 276B 为当前工具代实测、含运行时贡献）；§3-S1 已分清两种口径，`2×276=552≤640` 相容；有效域与越界不静默放大已明确 | 本轮未改动 |
| 2 六例同配方重跑 | **未闭合**（布局部分通过） | 六例成功状态与落盘 map 一致（82 双区间分账清楚）；但 rerun6.py 只在各例结束后读工具哈希，不能证明全程无跨代；results.json 对 attribution-41 的引用不成立；e2e/S0 记录非完整工具冻结 | rerun6.py 改为**运行前+运行后各读一次哈希并断言一致**（脚本改动未重跑，属后续运行门禁）；本文证据分层（文首口径警告）：六例=布局与 rc 已核实，版本完整性=运行后单点哈希、非全程冻结；results.json 引用修正；41 测量与 e2e 在进度文件注明"版本未严格冻结，实施 S0 须重跑"；撤回"全部实验单一工具代、逐例冻结完整"式表述 |
| 3 41 归因 | **未闭合** | 成立部分：真首错记录（CODE overflow）、76084B 测量、三路处置、putchar 归 D4、3v2 非零参不豁免（`MCS251Signatures.cpp:248-268`）；不成立部分：rev-1 探针读取不存在的 `.mcs251.signatures`（实际警告 could not find section）得出"记录消失"错误结论；Tag 28 实测 `_memcmp` role=2/param_count=3；manifest before/after 字典相同、无变化前态；objects 漏列 printf.o 与 nop-helper.o | rebuild41.py 改为**按冻结格式解码 `.mcs251.attributes` Tag 28**（失败即报错，不得记"(none)"），实测落盘 `signature-decode-r2.json`：printf.o（`239b6db7…`，10 条记录）与 gui.o（`65aac835…`，70 条记录）的 `_memcmp` 均为 role=2/param_count=3；撤回"记录消失"；归因改写为"3v2 冲突源于旧缓存不一致，当前两者三参一致、冲突不再存在；manifest 未保存变化前态，精确世代/头部演进归因不做主张"（§0.4/§1.5）；cache-manifest objects 补列 16→18；printf.o SHA256 与清单 before/after 相同、当前 `printf.ll:1264-1277` 仍含 `_memcmp` 元数据（编译器身份 `92bcfe599…`）如实登记 |
| 4 预算重算 | **已闭合** | `FF9000+64807`→`1008D27` 超 36135B；FE 容量 65280/64896 分账；58/62 终点 FEFFA7 余 89B；41 双口径缺口 12340/12329B；S3/S4 只扣一次余 1101B；76084B 保持"含 11B 探针 putchar"口径 | 本轮未改动 |
| 5 QEMU 证据收敛 | **已闭合** | 双 PASS+cmp 断言、落盘 A 对各 35B/B 对各 26B 逐字节相等、BOOT 负控存在；准确计数 8 link/8 run + 1 准备链接 + 1 负控链接 | 本轮未改动断言结构；按下条第 10 项补运行态记录并重跑（`out/e2e-r2/`，仍全 PASS） |
| 6 per-part | **已闭合** | G144 ch21 印 250-267 直接支持 MOV 读全部 FLASH/FC2800 构造/擦除风险；K128 ch21:187-207 支持 FE 起址与 MOVC 限制；D1 强制限定 G144K246 | 本轮未改动 |
| 7 S2/S3/S4 | **已闭合** | strlen 拆 TU 与 S1/S2 产品改动分账明确；S3 补无浮点引擎及 %f/%g、精度、非字面量规则；S4 标待验证、不提前宣布 41 闭环 | 本轮未改动 |
| 8 实验与 D3/D6 表述 | **已闭合** | 只读解码确认 5TU `.text` 和 56944/84486B；忙等删除机理、llc 默认 -O2 证伪保留、D3/D6 边界落实 | 本轮未改动 |
| 9 三项核心结论原文 | **已闭合** | 与一审原文去排版差异后逐字一致，未篡改 | 本轮未改动 |
| 10 评审外新发现处置 | **未闭合** | putchar、v2 静默、工具漂移已分项登记、S0 前置清楚；未闭合点是 v2 根因的过强定性——原始 CRT 字节相同不能排除最终重定位/入口/运行态差异，空 serial 不能区分正常运行未输出/加载失败/异常退出 | §2.3/§6.1-R6 归因软化："v2 合同镜像运行/串口静默，BASE 亦复现；**根因待定位**"；`xinit-fe-e2e.sh` 不再丢弃 QEMU 输出与退出状态：stdout/stderr 日志与退出码逐运行落盘（rev-2 重跑 `out/e2e-r2/`：4 个 v2 run 均 rc=124、serial 0B，stderr 仅 timeout 终止说明；可排除 QEMU 加载层失败，静默根因仍未定位），全部断言仍 PASS |

**新增（评审外发现，单独立项，登记保留）**：(i) demo 41 缺 putchar 提供者
（§1.3，D4）；(ii) v2 合同镜像 QEMU 运行/串口静默（§2.3，BASE 亦复现，
**根因待定位**）；(iii) 会话内二进制两度漂移，S0 四类冻结升级为强制门禁
（§6.1-R5）。

**rev-2 范围声明**：本轮仅修复二审点名的三处（#2 版本证据分层、#3 41 归因
证据链、#10 v2 静默归因），不改动配方推导、预算、per-part、S2/S3/S4 等
已闭合内容，也不新增方案承诺；#2/#3/#10 的遗留事项（S0 重跑、v2 根因定位）
保持单独立项状态。
