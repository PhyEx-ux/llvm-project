# MCS-251 端序统一审计报告

> 落盘：PM，2026-09-05。正文来自 Alice（端序统一专项工程师）最终交付，
> 报告由 PM 写入（子代理受"不写报告 .md"纪律约束）。源指纹见
> `transcripts/source-sha256.json`。

## 1. 结论

MCS251/STC32 的多字节内存表示为大端：最高有效字节位于最低地址。此次修复统一了三个互相独立的端序声明入口：

1. Target DataLayout：`e` → `E`。
2. Triple：MCS251 不再被判为小端；不存在的小端架构变体返回 `UnknownArch`。
3. MCAsmInfo：显式声明大端；禁用 sdas251 不支持的 `.long/.quad`，使宽数值常量通过大端 `.word` fallback 发射。

**自定义 load/store、寄存器 ABI、spill/reload、MC 指令编码和 REL 重定位算法保持不变。**

原问题已不再只是理论上的潜伏 bug：保存的旧版 `llc` 在 QEMU 中实际将 `trunc(load i16)` 错误缩窄为读取对象首字节，产生：

```text
LLVM.low16=13
FAIL expected=0x57 got=0x13
```

修复后同一场景返回 `0x57`。两条旧版负对照均精确复现，四条新版正例均通过。

## 2. 原始不一致

原 DataLayout：

```text
e-m:e-p:32:8-i8:8-i16:8-i32:8-n8:16:32-S8
```

修复后的 DataLayout：

```text
E-m:e-p:32:8-i8:8-i16:8-i32:8-n8:16:32-S8
```

除首字符外不改动指针宽度、整数对齐、原生整数宽度或栈对齐。

此前后端自定义内存操作符合大端，但 LLVM core 被告知小端。完整宽度的往返测试因此可能通过；一旦发生依赖端序的访问缩窄或常量解释，优化结果就可能与后端实际内存语义分离。

## 3. 端序解释路径全景

### 3.1 自定义 i16/i32 内存操作

审计文件：`llvm/lib/Target/MCS251/MCS251ISelLowering.cpp`

相关位置：`splitI32ToBytes` / `combineI32FromBytes`（约 335–365 行）；地址分类（约 786–962 行）；`LowerLoad`（约 992–1030 行）；`LowerStore`（约 1067–1107 行）。

裁定：

| 路径 | 实际顺序 | 处理 |
|---|---|---|
| i16 load | `+0` 为高字节，`+1` 为低字节 | 不改 |
| i32 load | 先用 `Bytes[0:2]` 组装高 WR，再用 `Bytes[2:4]` 组装低 WR | 不改 |
| 常量 store | 第 I 个地址提取 `(Size-I-1)*8` 对应字节 | 不改 |
| 非常量 i16 store | 高字节、低字节 | 不改 |
| 非常量 i32 store | ABI helper 拆分后执行 `reverse`，按高到低写入 | 不改 |
| truncating store | 先截取数值低位，再按目标内存宽度的大端次序存储 | 不改 |
| zero-extending load | 先按大端读取原内存宽度，再执行数值零扩展 | 不改 |

每个拆分访问继续保留精确的 MachineMemOperand 字节偏移，并通过 chain 保持既有访问顺序。

QEMU 已覆盖 u16/u32 双向互操作、逐字节检查、截断存储、零扩展读取、volatile 栈往返及非 volatile 窄化读取。

### 3.2 寄存器 lane 与 ABI 不是内存序

审计文件：`MCS251RegisterInfo.td`、`MCS251ISelLowering.cpp`、`MCS251InstrInfo.cpp`。

硬件寄存器关系：WRk 的高字节为 Rk，低字节为 R{k+1}；DRk 的高 WR 为 WRk，低 WR 为 WR{k+2}。`sub_lo*` / `sub_hi*` 描述的是数值有效位，而不是内存地址顺序。

i32 ABI helper 返回：`Parts = [bits7:0, bits15:8, bits23:16, bits31:24]`，`ABI registers = [DPL, DPH, B, A]`。

因此，**不能为了大端内存而反转 `splitI32ToBytes` 或 `combineI32FromBytes`**。内存 store 调用处已有必要转换。

当前 i32 为合法单值类型，自定义调用约定显式传送四个字节，不依赖通用 SelectionDAG 对非法宽整数的多 part ABI 拆分。此次 DataLayout 修正不需要改变既有调用约定。

### 3.3 spill/reload 与栈

审计文件：`MCS251InstrInfo.cpp`、`MCS251RegisterInfo.cpp`。

裁定：i16 spill/reload 使用原生 WR 内存指令；i32 在寄存器分配期间保持完整 DR use/def；PEI 展开时，高 WR 位于偏移 `+0`，低 WR 位于偏移 `+2`。此顺序已经符合大端，不改。

Phase 11 完整 QEMU 回归及新增 volatile i16/i32 栈往返均通过。

### 3.4 AsmPrinter 标量、聚合与字节数据

审计文件：`MCS251AsmPrinter.cpp`、`llvm/lib/CodeGen/AsmPrinter/AsmPrinter.cpp`。

必须区分两类路径：

**定义全局数据**：当前 `MCS251AsmPrinter::emitGlobalVariable` 明确拒绝带定义的全局数据。该保护避免尚未实现的数据 area 支持把对象静默发射到 CSEG。此次保持拒绝，不以端序修复名义开放全局数据功能。

**函数 prefix/prologue 常量**：这类常量可以进入通用 AsmPrinter 常量发射路径，即使定义全局数据仍被禁止。因此，MC 数据端序问题今天已经可达，并非完全属于未来工作。

通用发射行为：标量整数交给 streamer 的数值发射接口；数组和结构体按元素/字段顺序递归，使用 DataLayout 决定大小和 padding；宽整数、浮点位模式分块存在 DataLayout 端序分支；原始字节串不因端序声明而反转。

新增 prefix 测试覆盖 packed 聚合中的 i16、i32、i24、i64 和 `[2 x i16]`，共 21 字节。文本与 object 路径均经链接和 QEMU 逐字节回读。

### 3.5 MCAsmInfo 同时影响 asm 与直接 object 数据

审计文件：`llvm/include/llvm/MC/MCAsmInfo.h`、`llvm/lib/MC/MCAsmStreamer.cpp`、`llvm/lib/MC/MCStreamer.cpp`、`MCS251MCAsmInfo.cpp`。

发现：

1. MCAsmInfo 基类默认 `IsLittleEndian = true`。
2. `MCAsmStreamer::emitValueImpl` 在缺少宽度 directive 时，用该值决定分块顺序。
3. **`MCStreamer::emitIntValue` 直接读取 MCAsmInfo 端序并构造 object 字节。** 因此，即使 MCS251AsmBackend 已声明大端，也不能代替 MCAsmInfo 的修正。

旧版 prefix object 实际包含小端数值数据：`57 13 EF CD AB 89 C3 B2 A1 ...`；新版对应开头为：`13 57 89 AB CD EF A1 B2 C3 ...`。

此外，旧 MCAsmInfo 继承的 `.long/.quad` 不受 sdas251 支持。旧版 prefix 文本汇编经 sdas251 实测返回 2。

处理：`IsLittleEndian = false`；保留 `.word`；`Data32bitsDirective = nullptr`；`Data64bitsDirective = nullptr`。宽数值常量从而回退为受支持的 directive，并按最高有效分块优先发射。3 字节数值通过 `.word` 加 `.byte` fallback 发射。

### 3.6 ASxxxx `.word/.3byte`

审计文件：`sdcc-upstream/sdas/as251/mcs251mch.c`、`mcs251pst.c`。

源码证据：assembler 初始化 `hilo = 1`；支持 `.word`、`.3byte`；不支持本次涉及的 `.long/.quad` 拼写。

独立 provider 使用 `.word 0x1357` 和 `.3byte 0xa1b2c3`。QEMU 回读 `13 57 A1 B2 C3`，与源码结论一致，并非仅凭 assembler 接受语法作判断。

### 3.7 指令编码与重定位

审计文件：`MCS251MCCodeEmitter.cpp`、`MCS251InstPrinter.cpp`、`MCS251AsmBackend.cpp`、`MCS251RELObjectWriter.cpp`。

裁定：`put16` 显式写高字节再低字节；`putExpr24` 显式写最高地址字节再写低 16 位；位移与多字节立即数的编码由 ISA 固定；`MOVADDR32` 的 asm 路径通过 `.db` 拼写真实 MOV/MOVH 指令字节以表达 byte-of-24 relocations（不是对象常量发射）；AsmBackend 的 fixup 写入显式按大端执行；REL 的 `XH3`、地址字段及 relocation 字段显式按 MSB-first 输出。

以上路径不查询 DataLayout，不改编码算法。本次 O0、O2 各自的 asm/obj 完整链接地址→字节映射相同；QEMU 执行均通过。

### 3.8 LLVM core 中的端序解释

主要审计文件：`DAGCombiner.cpp`、`SelectionDAG.cpp`、`LegalizeIntegerTypes.cpp`、`SelectionDAGBuilder.cpp`、`ConstantFolding.cpp`。

端序敏感类别包括：load 缩窄后的地址调整；store→load 转发的字节偏移与位偏移换算；相邻 load/store 合并及字节排列识别；常量内存读取和数值重建；常量 memcpy 展开的多字节物化；宽整数 load/store 合法化的高低 part 地址安排；通用调用值拆分/合并；scalar/vector bitcast、常量向量和部分 known-bits 推理。

此次不修改通用算法，而是修正其输入的目标事实。

执行覆盖包括 load 缩窄、store→load 转发、常量字节数组读取、标量字节/半字提取和跨结构体字段读取。其余类别为源码审计范围，不宣称全部获得新增 QEMU 独立用例覆盖。

### 3.9 Triple

审计文件：`llvm/lib/TargetParser/Triple.cpp`。

原先 MCS251 在小端架构列表中。修正分类后还必须修正变体查询，否则 `getLittleEndianArchVariant` 会落入未处理分支。

最终行为：

| 查询 | 结果 |
|---|---|
| `isLittleEndian()` | false |
| `getBigEndianArchVariant()` | mcs251 |
| `getLittleEndianArchVariant()` | UnknownArch |

新增单元测试同时检查上述行为和完整 DataLayout 字符串。

## 4. 已知限制与非目标

此次未改变或解决：LLVM 32-bit 指针槽与 SDCC 24-bit 指针对象的尺寸差异（**不能据本次 scalar 互操作结果宣称指针数据对象可直接互换**）；定义全局数据的 area 分配和发射；通用 constant pool、jump-table/block-address 数据支持；多参数 SDCC OSEG ABI、varargs 或不支持的返回类型；atomic 和已有 sign-extending-load 限制；SFR-direct 与同数值地址间接 edata 访问的区别；符号宽数据的通用 fallback/重定位支持；完整 clang 结构体/位域 ABI 验收；真实硅片复验（本报告执行证据来自指定 QEMU machine）。
