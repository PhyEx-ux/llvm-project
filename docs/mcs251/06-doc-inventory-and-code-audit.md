# 06. 文档清理台账、保留文档总索引与代码审计

> **核对源码基线**：`cad7f0ab1f32`  
> **治理结果**：在整合形成本套 [docs/mcs251](./README.md)（7 份系统文档）后，对原仓库 **108 份** MCS-251 相关文档完成了分级处置：
> - **已清理 48 份**：严重滞后入口、已替代或已完整吸收进本手册的设计提案、旧 `SPEC.md` 及早期 `.rel` 历史归档；
> - **保留并同步修正 7 份**：活跃源码/契约/演示目录 README 中与 HEAD 脱节的状态段落已刷新；
> - **原样保留 53 份**：权威真机/QEMU 实测证据报告、有效工具/语料说明、未实施草案与 `t1`/`t2` 用例提取记录。
> - *(另注：历史设计中高频引用的 `validation/mcs251-models/DESIGN.md` 已在更早提交 `a15d032a4c60` 移出 git 追踪。)*

---

## 1. 现存 60 份保留文档分类总索引

### 1.1 活跃契约、运行时与未实施草案（6 份）
| 文件 | 状态 | 说明 |
|---|---|---|
| [lld/MCS251/BIT-OBJECT-CONTRACT.md](../../lld/MCS251/BIT-OBJECT-CONTRACT.md) | **有效（已同步 HEAD）** | `lld` 侧 `.mcs251.bit`、`R_MCS251_BIT_REF(10)`/`R_MCS251_BITADDR8(11)` 与 BT14 `crt-bit.yaml` 契约 |
| [llvm/lib/Target/MCS251/Runtime/README.md](../../llvm/lib/Target/MCS251/Runtime/README.md) | **有效（已同步 HEAD）** | `mcs251rt` 8 个除/余 helper 源码出处、独立实现声明与构建接入说明 |
| [validation/mcs251-elf/runtime/README.md](../../validation/mcs251-elf/runtime/README.md) | **有效（已同步 HEAD）** | 纯 ELF CRT 套件（v1/v2 `selfstart`/`irq` + BT14 `crt-bit`）与 `mcs251rt` 8/8 QEMU 验收记录 |
| [validation/mcs251-models/proposals/G13A-S4-XDATA-TABLES-DESIGN-draft.md](../../validation/mcs251-models/proposals/G13A-S4-XDATA-TABLES-DESIGN-draft.md) | **未实施草案** | demo 41 大常量表 XDATA 化（M1 影源）r2 草案，尚未开工 |
| [validation/mcs251-models/proposals/SEXTLOAD-PA-VERDICTS.md](../../validation/mcs251-models/proposals/SEXTLOAD-PA-VERDICTS.md) | **证据报告** | SEXTLOAD P-A 原语探针 QEMU 验证结论 |
| [validation/mcs251-porting/README.md](../../validation/mcs251-porting/README.md) | **有效（已同步 HEAD）** | `sfr-convert.py`（AS0/AS6/XFR）、`<intrins.h>`、270 `.uvproj` 批转与最小 QEMU 样板 |

### 1.2 受控改写语料、演示工程与集成包（6 份）
| 文件 | 状态 | 说明 |
|---|---|---|
| [validation/mcs251-agg85-corpus/README.md](../../validation/mcs251-agg85-corpus/README.md) | **有效** | demo 58 两行 `__code` 快照与 demo 85 板级容量裁定 |
| [validation/mcs251-bitaddr-corpus/README.md](../../validation/mcs251-bitaddr-corpus/README.md) | **有效** | G6 demo 32/42/58-TFT（7 文件）受控快照 |
| [validation/mcs251-g5-corpus/README.md](../../validation/mcs251-g5-corpus/README.md) | **有效** | G5 demo 82/44 AS3 形参闭合受控快照 |
| [validation/mcs251-xdata-e2e/README.md](../../validation/mcs251-xdata-e2e/README.md) | **有效（已同步 HEAD）** | X4 + A3 146/146 字节解码、-O2 Glue 回归与 QEMU 验收 |
| [validation/mcs251-demo-modern/README.md](../../validation/mcs251-demo-modern/README.md) | **有效（已同步 HEAD）** | 14 项现代 C 演示工程（`.rel` 链；已明确标注 5 项新特性在 HEAD ELF 链已实装） |
| [validation/mcs251-demos/REALHW-GUIDE.md](../../validation/mcs251-demos/REALHW-GUIDE.md) | **有效（实机指南）** | 多型号烧录指南、G12 真机 `SELFTEST-PASS` 闭环、MOVC 仲裁探针 |

### 1.3 ISR 专项、真机探针与硬件/端序证据报告（18 份）
| 文件 | 状态 | 说明 |
|---|---|---|
| [validation/mcs251-isr/asm2elf/README.md](../../validation/mcs251-isr/asm2elf/README.md) | **有效** | `sdrel2elf.py` `.rel→ELF` 桥接工具（E4，36 addend + 11 负例） |
| [validation/mcs251-isr/hwframe/README.md](../../validation/mcs251-isr/hwframe/README.md) | **有效 / 证据** | HWF2 G12K128 Timer0 4B 硬件中断帧探针说明 |
| [validation/mcs251-isr/hwframe/REALHW-20260910.md](../../validation/mcs251-isr/hwframe/REALHW-20260910.md) | **真机权威证据** | G12K128 实测 4B 中断帧 `00 FF 74 02` 与 `SPH` 进位原始抓包 |
| [validation/mcs251-isr/realhw-demo/README.md](../../validation/mcs251-isr/realhw-demo/README.md) | **有效** | T10 G12 单层实机 Demo v1 及 G1 重建声明 |
| [validation/mcs251-isr/realhw-demo/COMPILER-ASSESSMENT-20260910.md](../../validation/mcs251-isr/realhw-demo/COMPILER-ASSESSMENT-20260910.md) | **真机证据（已注 E1–E5 闭合）** | G1 前 10,000 次真机 PASS 评估；E1–E5 已全修，V1/V2 待真机 |
| [validation/mcs251-isr/realhw-demo/REBUILD-20260914.md](../../validation/mcs251-isr/realhw-demo/REBUILD-20260914.md) | **证据报告** | G1 重建前后 22 文件 SHA-256 逐项归因 |
| [validation/mcs251-isr/realhw-demo/G144K246-HIGH-SLOT-TEST-PLAN.md](../../validation/mcs251-isr/realhw-demo/G144K246-HIGH-SLOT-TEST-PLAN.md) | **待执行计划** | G144K246 67 个合法高槽（`52..126`）四阶段实板测试计划（`NOT_RUN`） |
| [validation/mcs251-isr/realhw-demo/branch-range-repro/README.md](../../validation/mcs251-isr/realhw-demo/branch-range-repro/README.md) | **已闭合归档** | E1 相对分支超距根因与 `MCS251BranchRelaxation` 修复档案 |
| [validation/mcs251-isr/v1-psw1-investigation/README.md](../../validation/mcs251-isr/v1-psw1-investigation/README.md) | **有效 / 证据** | V1 三臂 PSW1 调查包（QEMU 3 轮全过，待真机） |
| [validation/mcs251-isr/v1-psw1-investigation/PROTOCOL.md](../../validation/mcs251-isr/v1-psw1-investigation/PROTOCOL.md) | **有效** | V1 真机日志双分支判读协议 |
| [validation/mcs251-isr/v1-psw1-investigation/PSW1-AUDIT.md](../../validation/mcs251-isr/v1-psw1-investigation/PSW1-AUDIT.md) | **证据报告** | T10 路径逐指令 PSW/PSW1 标志副作用审计 |
| [validation/mcs251-isr/v2-clobber-nesting/clobber/README.md](../../validation/mcs251-isr/v2-clobber-nesting/clobber/README.md) | **有效 / 证据** | V2 高寄存器 R16–R31 + DPXL 主动破坏夹具（QEMU 10k PASS，待真机） |
| [validation/mcs251-isr/v2-clobber-nesting/nesting/README.md](../../validation/mcs251-isr/v2-clobber-nesting/nesting/README.md) | **有效 / 证据** | V2 Timer1→Timer0 两级嵌套夹具（QEMU 10k PASS，待真机） |
| [validation/mcs251-tfpu-hw/README.md](../../validation/mcs251-tfpu-hw/README.md) | **有效** | STC32G12K128 TFPU 存在性/状态位/延迟实机探针（反汇编辟谣 `A5 EF`） |
| [validation/mcs251-uartdemo-33m/README.md](../../validation/mcs251-uartdemo-33m/README.md) | **有效** | 33.1776MHz UART1 Demo + `check-encoding.py` 1482 指令验证 |
| [validation/mcs251-endian/AUDIT.md](../../validation/mcs251-endian/AUDIT.md) & [ACCEPTANCE.md](../../validation/mcs251-endian/ACCEPTANCE.md) | **证据报告（2 份）** | 大端统一 688 断言 + Phase 11 21 镜像验收证据 |
| [validation/mcs251-p11/hw-semantics-report.md](../../validation/mcs251-p11/hw-semantics-report.md) | **有效 ISA 事实表** | QEMU 5 项 ISA 语义探针（`CMP DR`、`@DR+dis16`、`ECALL @DR`、`DPX`/`SPX`） |

### 1.4 QEMU 外设基线探针与 T1/T2 用例级记录（30 份）
| 目录 / 文件 | 数量 | 说明 |
|---|---:|---|
| [validation/mcs251-demo-test/t4-probes/ISR-STUB-VERDICT.md](../../validation/mcs251-demo-test/t4-probes/ISR-STUB-VERDICT.md) & [README.md](../../validation/mcs251-demo-test/t4-probes/README.md) | 2 | 证实 `lcall+reti` 失败、`ecall+reti` 通过；10 项 QEMU 外设基线探针 |
| `validation/mcs251-demo-test/t2/*/README.md`（`absent-sentinel`, `display-table`, `gpio-readback`, `sfr-uart-tx`, `uart-echo-poll`） | 5 | 5 个 T2 SFR/GPIO/UART1 QEMU 集成用例说明 |
| [validation/mcs251-demo-test/t1/TEMPLATE.md](../../validation/mcs251-demo-test/t1/TEMPLATE.md) + 22 份 `t1/*/EXTRACT.md`（`adc-key-step` .. `wildcard-recursive`） | 23 | 22 个 T1 双 Oracle 内核提取溯源记录 |

---

## 2. 已清理的 48 份滞后/已替代文档清单

以下 48 份文档已从工作树移除，其有效技术规格与历史差异结论已完整沉淀到本目录 `01`–`05` 与下文第 3 节中（如需追溯历史原文可从 git 历史提交 `cad7f0ab1f32` 检出）：

1. **严重滞后的入口、旧规范与旧说明（5 份）**：
   - `clang/docs/MCS251.rst`（仅覆盖早期 `.rel`，缺失 ELF/模型/方言/G11/WP4）
   - `validation/mcs251-dialect/DIALECT-PACKAGE.md`（早期 2026-09-05 草案，多项选项与 AS 方案已推翻）
   - `validation/mcs251-elf/SPEC.md`（顶标“待审批 / v1 only”，`@response-file`/`--map-json` 与实现不符，有效内容已合入 `01`/`04`/`05`）
   - `validation/mcs251-tiny-impl/README.md`（命令示例直接对 16 位 `tiny` 产出 ELF `.o`，现已被 16 位对象门禁拒收）
   - `validation/mcs251-isr/hwframe/T10-REALHW-DEMO-DESIGN.md`（已落地并拆分为 `realhw-demo/` 与 `v2-clobber-nesting/`）
2. **早期 `.rel` 历史固件与旧 Demo×QEMU 过时文档（9 份）**：
   - `validation/mcs251-firmware/{README.md, STEP4-SELFSTART-DESIGN.md, selfstart-smoke/README.md, selfstart-check/README.md}`
   - `validation/mcs251-demo-test/{DESIGN.md, RESULTS.md, ACCEPTANCE.md, probes/endianness/README.md, t4/timer0-irq-llvm/README.md}`
3. **`validation/mcs251-models/proposals/` 下已替代或已落地的设计提案（34 份）**：
   - **已替代/历史稿（13 份）**：`A4-STATIC-PTR-PARAMS-DESIGN.md`、`BIT-FIRST-CLASS-INCREMENT.md`、`BIT-TASK-BREAKDOWN.md`、`DF4-EDATA-DESIGN.md`（被 G8 替代）、`DIALECT-FRONTEND-DESIGN.md`、`INTERRUPT-DESIGN.md`、`INTERRUPT-SCHEDULING-RECOMMENDATION.md`、`ISR-TASK-BREAKDOWN.md`、`MINIMAL-ISR-SLICE.md`、`P09-BIT-TYPE-DESIGN.md`、`RUNTIME-AS-PTR-PROPOSAL.md`、`X3-IMPL-BRIEF.md`、`XDATA-CODE-SLICE-TASK.md`
   - **已落地且正文/状态已被本手册吸收（21 份）**：`A4-V2-OBJECT-IDENTITY-DESIGN.md`、`AGG-XDATA64K-DESIGN-draft.md`、`AS4-AGGREGATE-INIT-DESIGN-draft.md`、`BIT-DECISION-20260911.md`、`BITADDR-DESIGN-draft.md`、`BRJT-JUMPTABLE-DESIGN-draft.md`、`G1-ISR-PROFILE-EXTENSION-DESIGN.md`、`G11-PLACEMENT-DESIGN.md`、`G13A-CODE-DESIGN-draft.md`、`G13B-XDATA-DESIGN-draft.md`、`G2-VARIADIC-DESIGN-draft.md`、`G3-STATIC-PTR-DESIGN-draft.md`、`G5-ADDRSPACE-DESIGN-draft.md`、`G7-FLOAT-DESIGN-draft.md`、`G8-DSEG-WINDOW-DESIGN-draft.md`、`P09-BIT-CODEGEN-DESIGN.md`、`P4-SIGNATURE-PROTOCOL-FREEZE.md`、`RUNTIME-AS-PTR-DESIGN-A.md`、`SEXTLOAD-DESIGN.md`、`SIGNED-DIV-REMAINDER-DESIGN.md`、`XDATA-CODE-DESIGN-SUPPLEMENT.md`

---

## 3. 历史设计演进与关键改道备忘（原文档 vs HEAD 实情）

以下记录整理过程中核实出的核心架构改道，防止后续维护者按旧习惯误判：

1. **EDATA 扩容改道（DF4 → G8）**：原 `DF4-EDATA-DESIGN` 提出的显式 AS8 全局、`.mcs251.edata.*`、`R_MCS251_PTR16/32` 与 `XINIT v2` 均未落地；现行实现为 **G8**（v2 ELF 对 AS0 `.mcs251.dseg`/非叶 `.mcs251.DSEG.*` 加 `SHF_MCS251_EDATA_MOVABLE`，由 `lld` 失败回退迁移至 EDATA，保留 6B `XINIT v1`）。
2. **G11 审计 NOTE 反转（Rev 6/7 → G11-D2）**：早期 G11 否决在输出 ELF 内嵌审计节，现行 G11-D2 在审计模式（`--placement-report`/`--verify-placement`）强制内嵌 `.mcs251.placement.positions`（type=3）并由 `mcs251-placement-verify` 强校。
3. **TFPU 与 `f32` 全局扩面（G7 → WP5）**：TFPU 9 条内建改用 **`i32` 位模式**（非 `f32`）并拆分为 4 伪指令（`GPR32Win={DR12}` / `GPR32Rd={DR12,DR0}`）+ 直线 NOP 链；`f32` 全局初值经 WP5 A3 开放至全部 AS0/AS3/AS4 可变与只读标量/数组/结构体。
4. **地址空间超集反转与 `__code` 隐式 `const`**：早期草案拟做 `AS3→AS0` 与独立 `const`；现行实现为单向 **`AS4 → 32 位 AS0`**（AS3 拒收），且 `__code`/`code`/`address_space(4)` 自动隐式加 `const`，AS4 读走 24 位 `MOV @DR` 统一通道，位模块 RO/AS4 镜像分流至 `.rodata`。
5. **v2 对象身份扩容与 G2 变参**：P-4 新增 `Tag_FunctionSignatures (28)` 使 v2 `RequiredTags` 从 21 增至 **22**；Clang 实际覆写 `MCS251ABIInfo::EmitVAArg` 展开 `va_arg` 并对 `va_arg(ap, __bit)` 直接 `report_fatal_error`；裸 `llc` 与 `clang` 现统一默认 `xsmall` v2（`1,2,32,8,1`）。
6. **`mcs251-lld` 与原 `SPEC.md` 差异闭合**：`@response-file` 与 `--map-json` 均已实装；`isReservedBoundarySymbol()` 继续保留对用户定义 `__mcs251_stack_base` 的 Fail-Closed 保护。

---

## 4. 审计中发现的现行源码小缺陷（已修复台账）与过时注释

### 4.1 源码级小缺陷 / 工具缺口（已全部修复）
1. **`lld/MCS251/Driver.cpp`（已修复）**：`buildExecutable()` 已改为写出 `Result.OutputEFlags`（v2 链接输出 `0x102`，v1 输出 `0x1`）。
2. **`lld/MCS251/LinkerCore.cpp`（已修复）**：`collectLinkFactsBasic()` 已补齐 `BITINIT`（`space=2`）与 `FIXED`（按 `PlacementClass` 区分 `CODE=2`/`XDATA=1`/`DATA=0`）。
3. **`llvm/tools/llvm-readobj/ELFDumper.cpp` / `ELF.h` / `ELF.cpp` / `ELFYAML.cpp`（已修复）**：已注册 `EF_MCS251_ABI_V2`、`EF_MCS251_BACKEND_NATIVE`、`SHT_MCS251_ATTRIBUTES`，`--notes` 准确区分 `NT_MCS251_ABI` 与 type 1/2/3 placement NOTE，`printMCS251Attributes` 读取实际 `Contents[0]` 并结构化打印 Tag 28 函数签名表。
4. **`lld/MCS251/Driver.cpp`（已修复）**：`--reserve-data` 上界校验已升级为 64 位无溢出检查。
5. **`lldb/` 目标架构接入（源码已接入）**：已注册 `ArchSpec::eCore_mcs251`、`ELF::EM_MCS251` 与 `ABISysV_mcs251` 插件。

### 4.2 源码中残留的过时注释与提示文案
- **Clang**：`clang/lib/Basic/Targets/MCS251.h:145-206`（整段 M1 旧注释仍声称 `bit`/`sbit` 未实现 CodeGen 且变参/`va_arg` 存在放行缺口）；`Attr.td:1092`（仍写 `0-51` 槽）；`Options.td:4540` & `LangOptions.def:310`（`-fmcs251-keil` 帮助仅提 `interrupt N`）；`BuiltinsMCS251.td:28-29`、`SemaMCS251.cpp:1856-1857`、`DiagnosticSemaKinds.td:12842`。
- **LLVM 后端**：`MCS251CallingConv.td:18-24`（仍写静态参数槽未实现）；`MCS251ISelLowering.cpp:1158-1160`（仍写无分支松弛）、L332/336-338 死 `Custom` 动作、L1674 & `MCS251TFPUExpand.cpp:163` 误将 `GPR32Rd` 写成 `GPR32Win`；`MCS251RegisterInfo.cpp:198`（误写 `dr56 anchor`，实际 L200 为 `DR16`）；`MCS251TargetMachine.cpp:243`（CREL 报错文案仍写 ABI v1）；`MCS251AsmPrinter.cpp:2810-2820`（报错文案未同步 `f32`/指针初值）；`MCS251MCAsmInfo.cpp:43`（历史 `CodePointerSize = 2`）；`llvm/lib/Target/MCS251/Runtime/CMakeLists.txt:294`（误称裸 `llc` 走 compat DataLayout）。
- **链接器**：`lld/MCS251/LinkerCore.cpp:76-81`（重复本地重定位常量注释）。
