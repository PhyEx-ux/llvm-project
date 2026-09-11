# XDATA / CODE 存储空间限定符切片任务书

**日期**：2026-09-12。**状态**：任务书冻结，待 P1-2 通过 Review 后开工（用户裁定：USB demo 不考虑、中断语法搁置）。**提交门禁**：一切代码提交必须 Alice Review APPROVED。

## 1. 目标与范围

官方 demo 语料（101 目录/275 C 文件）画像实测：

- `xdata`：15 文件，形态 `extern BYTE xdata UsbBuffer[256];` 与 `BYTE xdata *pdat`（对象 + 指针两种）。非 USB 用户：flash 读写、UART DMA 类 demo。
- `code`：11 文件，形态 `char code DEVICEDESC[18]`（ROM 常量表；样本多来自 USB 描述符，但关键字本身为通用 ROM 表能力，验收锚点选非 USB 用例）。

**做**：`__xdata`/`__code` 现代限定符（C 合规）+ `-fmcs251-keil` 裸词 `xdata`/`code`（方言，与 bit/sbit 同门）；对象与指针形态；放置与链接闭环；官方源形态 e2e。
**不做**：USB demo 验收、中断语法、`pdata`/`using`（语料零使用）、`far`（后续）、按大小自动迁移（DF4 边界不变）。

## 2. 设计锚点（DESIGN.md 已冻结，实施不得偏离）

| 锚点 | 内容 |
|---|---|
| B.1 表 | AS3=`__xdata` 32/8（24 位有效地址）；AS4=`__code` 32/8（CODE 只读数据+函数地址，**禁止 store**） |
| §353 | 现代接口 `__data/__edata/__xdata/__code/__far` 目标限定 |
| §404-405 | AS3 须"经实证的 XDATA 指令族或 DR 统一寻址"；AS4 须"经实证的 CODE 读取指令族" |
| DF4 | XDATA 独立分配、不计内部栈高水位 H；lld DSEG 上限不因 EDATA/XDATA 扩大 |
| bit 战役先例 | AS5 保留拒绝；方言裸词走 `-fmcs251-keil`；每提交 Alice Review |

## 3. 实施切片

| # | 内容 | 验收 |
|---|---|---|
| X1 前端 | `__xdata`/`__code` 类型限定解析与 Sema（对象/指针/参数/返回/聚合成员）；`-fmcs251-keil` 裸词 `xdata`/`code` 同语义；code 对象只读（写=诊断）；BitInt 式负例（跨空间转换、AS 混用指针算术） | FileCheck 正负例；构造外行为不变 |
| X2 后端 | AS3 load/store lowering（MOVX 族 vs 24 位 DR 统一寻址，按 STC32G 手册实证选型并留证据）；AS4 load lowering（MOVC A,@A+DPTR 族，新指令）；AS4 store 保持拒绝 | MIR 字节精确 + ISel 负例 |
| X3 放置链接 | `xdata` 对象→`.mcs251.xdata.*`（lld XDATA 分配/重叠检查已就绪，接通）；`code` 对象→CODE/XINIT 常量节 | lld test：分配/重叠/溢出 |

**X3 需求补充（Alice X2 审查裁定，必须落实）**：①明确 canonical 地址 ↔ DPXL 区域 ↔ 窗口偏移的转换，不得直接把 XSEG 地址截成低 16 位；②窗口/板级容量配置来源显式化；③范围检查覆盖跨对象、完整区间（含末字节）、重定位与指针初值；④测试含末字节成功/跨界失败/错 bank/重叠（"跨界失败/错 bank"仅指超物理容量或 24 位范围的拒绝场景，不是恢复 64K 窗口门禁——窗口内合法访问必须放行）；⑤不扩大 DSEG、不计入内部栈高水位；⑥DPXL 保持协议（CRT/ISR/外部调用/用户 SFR 写 0x84）与 MOVX 窗口的关系须成文。注意：现有 lld XSEG 检查是 24 位 rangeFits，**不是** 64K 窗口门禁；驱动层 0xffff 检查不覆盖 XSEG。链接器无法检查动态指针——动态可达性由 X2 的后端语义负责，不得推迟给 X3。
| X4 e2e | 官方源形态：`BYTE xdata buf[256];` 读写、`char code tab[]=` 读取；BT06 兼容头扩展（`sfr-convert` 或独立转换，只覆盖非 USB demo 所需） | 双树 llc→lld→objdump 字节链 + QEMU 可测项 |
| X5 语料矩阵 | 官方 101 目录逐 demo 编译矩阵：可编译/可链接/可运行三档清单（USB 类只记编译层） | 报告三档计数 |

## 4. 指令实证（已冻结——证据源：手册全文文本 `/home/liu/LLVM_STC32/liu/stc32g.txt`，80357 行）

| 能力 | 手册证据（行号） | 结论 |
|---|---|---|
| `xdata` XRAM 访问 | L70765-70786：片内扩展 XRAM（C 关键字 `xdata`），汇编经 **MOVX** 族：`MOVX A,@DPTR` / `MOVX @DPTR,A` / `MOVX A,@Ri` / `MOVX @Ri,A`；不影响 P0/P2/RD/WR/ALE 端口信号 | AS3 load/store 主通道 = MOVX @DPTR 族（16 位 DPTR） |
| XDATA 24 位跨页 | L71066-71069：`MOVX A,@Ri` 8 位形态跨 64K 时 XRAM[23:16] 由 **MXAX** 寄存器承载 | 页寄存器方案存在但仅 @Ri 形态需要；首选 @DPTR 避开 MXAX 维护 |
| 251 核 DR 统一寻址 | L80558-80570：**`MOV @DRk, Rm` / `MOV Rm, @DRk`**，XFR（0x7EFE00）位于 XDATA 逻辑区，需 EAXFR 位 | DR 24 位通道服务于 XFR/高端；AS3 首期用 MOVX@DPTR，DR 通道与 ISR 战役既有 DR 语义对齐 |
| CODE 常量读取 | L125052：STC32G **不能用 MOVC 读 EEPROM**（与 STC8 不同），用 MOV 方式、DRx=FE:xxxxh | **MCS-251 核是 24 位统一空间**：CODE 常量读走 `MOV Rm,@DRk`（DR 装载 24 位地址），MOVC 仅为兼容形态；EEPROM 映射 FE: 段是 MOV 方式的既存特例 |

**实施裁定（X2）**：AS3 = MOVX @DPTR 族（DPTR 装载由既有 i16/i24 legalization 服务）；AS4 load = `MOV Rm,@DRk` 24 位统一读（编码经 llc 汇编实测留证，与 ISR 战役 DR 用法对齐）；AS4 store 维持 fail-closed。SDCC oracle 交叉验证（`~/build-sdcc/bin/sdas251`）作为编码旁证。

## 5. 派工顺序（用户裁定）

P1-2 三审 APPROVED 并提交 → 本任务书开工。X1+X2 可并行两实例（前端/后端文件面不重叠），X3 依赖 X2 指令形态，X4/X5 串行收尾。每切片 Alice Review APPROVED 后提交。
