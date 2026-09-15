# AGG85 缺口 A 改写语料快照（demo 58 OLED-SSD1306）

**目的**：改写包（`/home/liu/LLVM_STC32/mcs251-demos-rewritten/`）不受 Git 管理，
缺口 A（demo 58 聚合常量 global）的两行 `__code` 改写此前只存在于仓库外工作区。
本目录把 demo 58 OLED 组的改写后源码固化为受控资产，使改动可审计、可复现，
而不是仅凭主树的设计与测试就宣称语料已入库（Alice 2026-09-15 复审要求：
"仅提交主树不等于语料入库"）。

**来源**：`mcs251-demos-rewritten/src/58-DMA-SPI驱动显示屏/DMA-SPI刷新OLED12864显示屏程序，SSD1306驱动，0.96寸/`
（本目录 `src/58-OLED-SSD1306/`），逐字节复制。原始未改写副本见
`/home/liu/LLVM_STC32/IMPL-AGG85-EVIDENCE/pre/`（仅两份被改头文件）。

**改写范围**（AGG-XDATA64K-DESIGN-draft.md r2 §A.5 Option A，Alice 审定）：
仅 2 行，两个头文件各 1 行，把 clang CodeGen 合成尾零聚合常量的两张表放进 AS4：

| 文件 | 行 | 原 | 改写 |
|---|---|---|---|
| `ASCII-10x24.h` | 11 | `unsigned char const ASCII10x24[]={...}` | `__code unsigned char const ASCII10x24[]={...}` |
| `picture2.h` | 10 | `unsigned char const gImage_picture2[1024] =` | `__code unsigned char const gImage_picture2[1024] =` |

- 其余 5 个语料文件（`ASCII6x8.h`/`HZK16.h`/`picture1.h`/`AI8051U.h`/`OLED128x64-SSD1306-SPI-DMA.c`）**零改动**
  （快照目录为逐字节完整复制；`AI8051U.h` 未被 OLED 程序 include，为完整性一并快照）。
- 表内容（360B / 1024B 字模字节）**零改动**。
- 改写性质：补齐 demo 同族字库表的 `code` 惯例（语料内 41/42/43/61 本就带 `code`；
  58 OLED 是无 `code` 的惯例偏差），不是新发明，**零产品改动**。

**文件 md5（冻结值）**：

```
0d0e35f5fcc3e0e5c59dbf97b7d9e05c  AI8051U.h      (unchanged)
9fd6cf5479bb3c4b44bf3a8fdac851f5  ASCII-10x24.h
9804188ed3a335ca5f99490a06135a55  picture2.h
f40a57a3ca94d5e3f996da4024b1d85e  ASCII6x8.h      (unchanged)
d875d1dc3fa989b083258d442159c5b7  HZK16.h         (unchanged)
a0ae240172e181457be8fe445024619f  picture1.h      (unchanged)
a7d53948489324ccdb7915813e74402a  OLED128x64-SSD1306-SPI-DMA.c (unchanged)
```

**验收证据**（详见 `/home/liu/LLVM_STC32/IMPL-AGG85-PROGRESS.md` 与
`/home/liu/LLVM_STC32/IMPL-AGG85-EVIDENCE/`）：

- **T0/T1**：clang rc=0（drive.py OLED 组真实 flags）、llc rc=0、lld rc=0
  （`oled.o` + `crt-irq-v2.o`，IRQ 镜像）；`l_CSEG=0x2567`（9567B < 30976B 窗）、
  `l_DSEG=0x0038`、`l_XSEG=0x0405`。
- **IR**：两 global 为 `addrspace(4) constant <{ [N x i8], [M x i8] }>`。
- **byte-identity（主验证，A-T5）**：AS0 扁平基线（`demo58/flat3.o`）vs 改写后
  `oled.o`，按 `readelf` 符号 `Value/Size` 取真实 `.text` 区间：

  | 表 | 大小 | sha256 |
  |---|---|---|
  | `_ASCII10x24` | 360 B | `94004b1228d9307476801ba3bd735c867550d24f5451c1a26aa5c048f95aec0f` |
  | `_gImage_picture2` | 1024 B | `797d6daac90b2a39604a87896200c0dabffbcc2deb2387977ce61ca289644671` |

  **逐字节相同**。（注意：原稿 r1 的 `d3df611a…`/`5f70bf18…` 是 360/1024 个零字节
  的哈希，oracle 取错范围；正确值如上，见设计 r2 §A.5 与评审组 #1。）
- **A-T6 回归**：未改写表 `_ASCII6x8`(774B)/`_HZK16`(256B)/`_gImage_picture1`(1024B)
  跨基线/改写对象 sha256 相同。
- **整节恒等**：`flat3.o` 与 `oled.o` 的 `.text` 段（9575B）sha256 同为
  `2b6fe745cf53e406453f016b0f53d951d7311b98b85d9f3de643cb5d86474d81`
  —— 改写不改变物理布局与镜像字节。
- **只读确认**：两表在源码/IR 中各只被读取（`ASCII10x24` 取基址；`gImage_picture2`
  逐元素读入显存），IR 内**无任何 store 以两符号为目标**。
- **A-T7**：改写前的 AS0 聚合形态 IR 仍被 llc 拒（`aggregate ... not supported`），
  AS0 冻结面未动。

**本快照不改变的能力侧残余**：缺口 A 的产品侧维持 AS0 只读门冻结
（AS4-AGGREGATE §9.1 AS0-only / §6A.4 R4），**不新开产品切片**。

---

# 缺口 B（demo 85）—— 无语料改动（新定位：仅 T1 探针）

按 AGG-XDATA64K-DESIGN-draft.md **r2 §B.5 / §B.5.1 的重新裁定**，
demo 85 **不作语料改写交付**，因此本快照**不含 85 源码**：

- r1 的"拆 32K + `eRAM` 转 `__xdata`"**定位被证伪**：改写后
  `_xRAM2b=[0x30000,0x38000)`/`_xRAM3=[0x38000,0x39000)` 越过板/QEMU 可用区，
  `--xdata-size=0x21000` 直接链接拒绝。它降级为"**仅无物理容量门禁的 T1 探针**"。
- 板级可行方案（BV-1）需同时 (i) XDATA 半按 `[0x10000,0x31000)` 排布，
  (ii) `eRAM` 缩小到 ≤15088 B 才满足 1024B 栈门禁；`eRAM` 16074B 原尺寸
  在 G144 上**确定违反栈下限**（Capacity=32B）。这属能力侧裁定，未在语料内落地。
- 探针源码与实测矩阵见 `/home/liu/LLVM_STC32/IMPL-AGG85-EVIDENCE/gapB/`。
- demo 85 语料 `sample.c` md5 保持 `157d07137636bab4c118c3f986324101`（未动）。
