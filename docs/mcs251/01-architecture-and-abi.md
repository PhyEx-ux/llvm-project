# 01. 架构、内存模型、地址空间与对象协议

> **核对源码基线**：`cad7f0ab1f32`  
> **核心源码**：
> - `llvm/lib/TargetParser/MCS251TargetParser.cpp` / `llvm/include/llvm/TargetParser/MCS251TargetParser.h`
> - `llvm/include/llvm/IR/DataLayout.h` / `llvm/lib/IR/DataLayout.cpp`
> - `llvm/lib/Target/MCS251/MCS251RegisterInfo.td` / `MCS251CallingConv.td` / `MCS251TargetMachine.cpp`
> - `llvm/include/llvm/BinaryFormat/{MCS251Attributes.h, MCS251Signatures.h, MCS251ISR.h, MCS251Bit.h, ELFRelocs/MCS251.def}`

---

## 1. 架构与数据表示基础

### 1.1 端序与对齐
- **字节序**：全链路统一为**大端（Big-Endian, `E`）**——多字节内存数据高字节在低地址，多字节寄存器高字节在低编号字节槽（`WRk = Rk(高):R{k+1}(低)`，`DRk = WRk(高):WR{k+2}(低)`）。该结论已经由 688 项 QEMU 矩阵断言及 Phase 11 硬件语义实测锁定（见 `validation/mcs251-endian/ACCEPTANCE.md`、`validation/mcs251-p11/hw-semantics-report.md`）。
- **对齐**：全部标量整数（`i8`/`i16`/`i32`/`i64`）、浮点（`f32`/`f64`）、指针与栈（`S8`）的 ABI 对齐及首选对齐均为 **1 字节（8 bits）**；默认不会在结构体成员、全局对象或函数之间偷偷插入填充字节。
- **物理地址宽度**：MCS-251 CPU 线性物理地址为 **24 位（`0x000000..0xFFFFFF`）**。LLVM 中远指针（AS0 32 位 / AS3 / AS4 / AS7 / AS9）使用 `i32` 容器表达，高 8 位（bits `[31:24]`）规范化为 `0`。

### 1.2 Fork 专用符号修饰模式 `m:s`（`MM_ASXXXX`）
- 源码位置：`llvm/include/llvm/IR/DataLayout.h:137`、`llvm/lib/IR/DataLayout.cpp:676-683`、`llvm/lib/Target/MCS251/MCTargetDesc/MCS251MCAsmInfo.cpp:36-41`。
- 修饰规则：
  - C/IR 层全局符号 `@foo` → 物理符号 `_foo`；`@_foo` → `__foo`（前导下划线不会抑制修饰）；
  - 使用 LLVM `\01` 逃逸（如 `@"\01__mcs251_stack_base"` 或 C `asm("...")` 标签）时保留精确拼写，不额外加下划线；
  - 私有局部符号前缀为 `.L`（`PrivateGlobalPrefix = ".L"`，在 `clang/docs/MCS251.rst` 中描述为 `.L_`）。
- **上游兼容性警告**：`m:s` 是本 fork 独有扩展（带 `UPSTREAM-DIFF(MCS251)` 标记）。**上游原版 LLVM 的 `llvm-as`/`llvm-dis`/`opt`/LTO 会拒绝包含 `m:s` 的 IR 与 bitcode**，与上游工具交换只能在文本汇编或 ELF 对象文件层进行。

---

## 2. 规范 DataLayout 与 5 元组内存契约

### 2.1 三套规范 DataLayout
定义于 `llvm/lib/TargetParser/MCS251TargetParser.cpp:28-33`（`isSupportedLayout` 仅接受这三套字符串）：

| 名称 | 适用版本与 AS0 宽度 | `ProgramAS / AllocaAS / GlobalAS` | 完整 DataLayout 字符串 |
|---|---|---|---|
| **Compatibility (v1)** | `ASLayoutVersion=1`, `AS0=32` | `0 / 0 / 0` | `E-m:s-p:32:8-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8` |
| **Tiny (v2, 16-bit AS0)** | `ASLayoutVersion=2`, `AS0=16` | `4 / 0 / 0` | `E-m:s-p:16:8:8:16-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0` |
| **Small / XSmall / Large (v2, 32-bit AS0)** | `ASLayoutVersion=2`, `AS0=32` | `4 / 0 / 0` | 与 Tiny 相同，仅 `p:32:8:8:32` |

> **关键变化**：在 HEAD 中，无论是 `clang` 还是不带参数的裸 `llc -mtriple=mcs251`，**默认均实例化 `xsmall` v2 契约 `{1, 2, 32, 8, 1}`**（`MCS251TargetMachine.cpp:181`、`clang/lib/Basic/Targets/MCS251.h:61-62`），不再默认落回 v1 Compatibility。

### 2.2 五元组 `MemoryContract` 与模型映射
格式：`{TransportVersion, ASLayoutVersion, AS0PointerBits, DefaultPlacement, ExecutionContract}`，固定 `TransportVersion = 1`、`ExecutionContract = 1`。

| 模型名 (`-mcs251-memory-model=`) | 5 元组契约 | AS0 指针 | `DefaultPlacement` 编码 | 可产出 ASM | 可产出 ELF `.o`（HEAD 现状） |
|---|---|---:|---|---|---|
| *(v1 显式兼容)* | `1,1,32,8,1` | 32 位 | `8` (`InternalExtended`) | 是 | 是（生成已弃用的 **v1** `.note.mcs251.abi`） |
| `tiny` | `1,2,16,1,1` | 16 位 | `1` (`InternalMovable`) | 是 | **禁止**（`MCS251TargetMachine.cpp:234-240` 硬拒全部 16 位 AS0 对象输出） |
| `xtiny` | `1,2,16,8,1` | 16 位 | `8` (`InternalExtended`) | 是 | **禁止**（同上） |
| `small` | `1,2,32,1,1` | 32 位 | `1` (`InternalMovable`) | 是 | **是（A4 注册 v2 profile）** |
| **`xsmall`（默认）** | **`1,2,32,8,1`** | **32 位** | **`8` (`InternalExtended`)** | **是** | **是（A4 注册 v2 profile）** |
| `large` | `1,2,32,3,1` | 32 位 | `3` (`ExternalData`) | 是 | **禁止**（`isRegisteredA4Profile` 与 `lld` 仅放行 `small`/`xsmall`） |

- 组合 `(1, 2, 16, 3, 1)` 属于非法契约（`ExternalData` 需要 32 位远指针）。
- **传输机制（P1-2）**：Clang `BackendUtil` 通过目标特性字符串 `+mcs251-memory-contract=v-t-as0-p-e`（连字符分隔）传入 `MCS251TargetMachine`；该特性与 `llc` 选项 `-mcs251-memory-model=` / `-mcs251-memory-contract=` 三者互斥，禁止静默覆盖。

---

## 3. 地址空间完整实测表（AS0–AS9）

下表严格核对 `MCS251TargetParser.cpp`、`MCS251ContractCheck.cpp`、`MCS251ISelLowering.cpp`、`MCS251TargetObjectFile.cpp` 与 `MCS251AsmPrinter.cpp` 的 HEAD 实现：

| AS | 语义 / C 限定符 | v2 指针宽度 | 数据加载 / 存储指令通道 | `addrspacecast` 规则 | 参与普通指针 ABI / 静态参数槽 | 全局变量定义（`AsmPrinter`） | 关键限制 |
|---:|---|---:|---|---|---|---|---|
| **0** | 默认 RAM（`AllocaAS=0`, `GlobalAS=0`） | 16 (`tiny`/`xtiny`) 或 32 (`small`/`xsmall`/`large`) | 16 位走 `@WR`；32 位走 `@DR+dis16`；栈访问走 `@DR60`/`@DR16`。仅 v1 (`ProgramAS==0`) 对常量字节 `<=0xFF` 保留 `dir8` 歧义 | AS0=16 属 NearRAM `{0,1,2,8}`；AS0=32 属 FarRAM `{0,3,9}`；且 `AS0 <-> AS4` 等宽 32 位直通 | 是（2B / 4B） | **可变**：`.mcs251.dseg`（v2 ELF 附加 `EDATA_MOVABLE`）+ `.mcs251.xinit`（6B 头，最大 65535B）。**只读**：CSEG（`.text` 或位模块 `.rodata`，**禁结构体与全零聚合**；16 位 AS0 禁普通只读） | 亦承载 `"mcs251-bit-object"` 位句柄全局 |
| **1** | `__data`（直寻址内部 RAM） | 16 | NearRAM `@WR` | **转入 AS1 仅允许编译期常量 `[0, 0x80)`**，动态转换拒收；转出可直通 NearRAM 或零扩展至 FarRAM | 是（2B） | **拒收**（仅开放 AS0/AS3/AS4） | 绝不标 `EDATA_MOVABLE` |
| **2** | `__idata`（间接内部 RAM） | 16 | NearRAM `@WR` | NearRAM 间直通；Near→Far 16→32 零扩展；Far→Near 仅常量 `<=0xFFFF` | 是（2B） | **拒收** | — |
| **3** | `__xdata` / Keil `xdata` | 32（有效 24 位） | **专用逐字节 `MOVX @DPTR`**（`0xE0`/`0xF0`）+ **自愈 `DPXL`（SFR `0x84`）**：每次访问前重写 bits `[23:16]`，整串以 DAG Glue 焊接防 `-O2` 乱序 | FarRAM `{0,3,9}` 等宽直通；不作为 Clang AS0 子集（G5 裁定） | 是（4B） | **仅 ELF**（含 `const __xdata`）：`.mcs251.XSEG.<sym>` + `.mcs251.xdata_init`（7B 头）。`>65535B` 全零对象在 v2 ELF 标 `SHF_MCS251_XSEG_SPLIT` 且省略 init 记录 | G11 固定 XDATA 上限仍为 65535B，不拆分 |
| **4** | `__code` / Keil `code` / 函数空间（`ProgramAS=4`） | 32（有效 24 位） | **读走 24 位 `MOV @DR` 统一地址通道**（非 `MOVC @A+DPTR`，借此支持 `code`/`ecode`/`FE:` EEPROM）；**写操作前后端双层 Fail-Closed 拒收** | 仅允许 32 位等宽 `AS4 <-> AS0` 直通（Clang 允许单向 `AS4 → 32 位 AS0` 隐式转换） | 是（4B） | **仅 ELF** 只读 CSEG 镜像（`.text` 或位模块 `.rodata`）；**允许嵌套结构体与全零 ROM 镜像**，自动隐式加 `const` | 函数在 v2 下位于 AS4，在 v1 下位于 AS0 |
| **5** | *(位空间保留未分配)* | 无（DataLayout 无 `p5`） | **全链路拒收** | **拒收** | **否** | **拒收** | 位访问严格走 `llvm.mcs251.bit.*` 与 `bit.obj.*` 内建，禁止普通指针化 |
| **6** | SFR 直寻址（`address_space(6)`） | 16 | **仅 1 字节访问** + 编译期常量地址 **`0x80..0xFE`** → `MOV8di`/`MOV8id`；**`0xFF` 永久拒收** | **拒收** | **否**（`hasOrdinaryPointerABI=false`） | **拒收** | 仅允许作为 `inttoptr(i16 const 0x80..0xFE)` 的直接 `i8` load/store 基址 |
| **7** | *(预留，原设计拟用于 XFR)* | 32（DataLayout 含 `p7`） | **全链路拒收**（`address space 7 is reserved`） | **拒收** | **否** | **拒收** | 实际 STC32G XFR（`0x7E0000..0x7EFFFF`）经由标准 32 位 AS0 `volatile` 指针访问 |
| **8** | `__edata` | 16 | NearRAM `@WR` | 同 AS2（NearRAM） | 是（2B） | **拒收**（不存在 `.mcs251.edata.*` 后端发射路径） | 实际 EDATA 扩容由 G8 机制对 AS0 自动迁移完成 |
| **9** | `__far` 通用数据 | 32 | FarRAM `@DR+dis16` | 同 FarRAM `{0,3,9}` | 是（4B） | **拒收** | — |
| **>=10** | 非法 | 无 | **拒收** | **拒收** | **否** | **拒收** | Clang `BuildAddressSpaceAttr` 与 `ContractCheck` 均拒收 |

---

## 4. 寄存器文件与调用约定

### 4.1 物理寄存器与寄存器类（`MCS251RegisterInfo.td`）
- **大端覆盖层级**：
  - 字节槽：`R0..R63`（定义全 64 槽以保证子寄存器掩码完整）；
  - 字槽：`WR0..WR30`（偶数编号，`WRk = [sub_lo8: R{k+1}, sub_hi8: Rk]`），另含人工 `WR56/58/60/62`；
  - 双字槽：`DR0..DR28`（4 的倍数，`DRk = [sub_lo16: WR{k+2}, sub_hi16: WRk]`），以及 `DR56 (dpx)`、`DR60 (spx)`。
- **可分配寄存器类**：
  - `GPR8`：`R0..R9, R12..R15`（共 14 个；`R10`/`R11` 物理别名 `B`/`ACC`，排除在通用分配之外）；
  - `GPR8Low`：`R0..R7`（用于变长移位循环计数器，防止与被移位寄存器冲突）；
  - `GPR16`：`WR0, WR2, WR4, WR6, WR8, WR12, WR14`（排除覆盖 `R10/R11` 的 `WR10`）；
  - `GPR32`：`DR0, DR4, DR12`（仅 3 个；`DR8` 重叠 `A/B`，`DR16+` 无可编码字节子寄存器）；
  - `GPR32Win`：`{DR12}`（TFPU 操作数窗口外停泊）；`GPR32Rd`：`{DR12, DR0}`（TFPU 结果读回目标，排除 `DR4` 防快照被消除）。
- **保留寄存器**：`DR60 (SPX)`、`DR56 (DPX)`、`DR16`（动态 alloca 帧锚点）、`DPL (0x82)`、`DPH (0x83)`、`DPTR`（`DPL:DPH` 覆盖，`DPX` 低 16 位）、`DPXL (0x84)`、`A (0xE0)`、`B (0xF0)`、`PSW`（虚拟单寄存器模型，统一涵盖 PSW/PSW1 标志位）。
- **调用保存集（`CSR_MCS251`）**：**`CSR_MCS251 = {DR12, WR8}`**（覆盖 `R8..R9` 与 `R12..R15` 共 6 字节，由序言/尾声按字粒度 `mov @dr60+off, wrN` / `mov wrN, @dr60+off` 自动按需保存与恢复）；`OtherPreserved = {DR60, DR16}` 保持栈指针与动态帧锚点。中断函数（`mcs251_intrcc`）返回空 CSR 列表，避免与固定 37 字节 ISR 保存帧重复保存。

### 4.2 函数调用约定（Route B 栈传参可重入默认 ABI）
`CallingConv::C` 与 `CallingConv::Fast` 使用同一套**默认可重入的栈传参 ABI**（完全替代早期 `_fn_PARM_n` / `OSEG` 静态参数槽，`__reentrant` 与 Keil `reentrant` 均被解析器接受并映射到本 ABI）：

1. **第 1 参数（Arg 0）与标量返回值**（低字节优先寄存器列举 `DPL, DPH, B, A`）：
   - 当且仅当存在参数、第 1 参数**不是 `byval` 聚合**、且**不是 `F=0` 零固定参变参函数**时，第 1 标量/指针参数走寄存器通道：
     - `i8` / `i1`（`_Bool`） / `__bit`：`DPL`
     - `i16` / 16 位指针：`DPTR`（`DPL`=低字节，`DPH`=高字节）
     - `i32` / `f32` / 32 位指针：`DPL : DPH : B : A`（`DPL`=LSB，`A`=MSB；32 位指针最高字节 `A` 强制清零）
   - 若函数采用 `sret` 隐藏指针返回聚合，该隐藏 `sret` 指针作为第 1 参数进入 `DPL:DPH:B:A`（16 位 AS0 为 `DPTR`），所有显式参数顺延入栈。
2. **第 2 及后续参数（Arg 1..N）与 `byval` 聚合——向下排列的栈参数区**：
   - MCS-251 硬件栈**向上增长**，`SPX (DR60)` 指向最高已用字节，`ecall` 压入 3 字节返回地址 `[SPX_callsite+1..+3]`（被调入口 `SPX_entry = SPX_callsite + 3`）；
   - **中断安全压栈顺序**：调用方先通过 `ADJCALLSTACKDOWN`（展开为 `inc spx`）抬高 `SPX` 预留 `OutStackBytes`，再从 `SPX_callsite` 向下逆序（按源顺序递减地址）写入各栈参数（`Disp = -(ArgOff + Size) + 1`，多字节大端存放），最后 `ecall` 并在返回后由 `ADJCALLSTACKUP`（`dec spx`）回收；确保任意中断抢占不会覆盖尚未调用的实参；
   - **被调访问几何**：被调函数通过固定帧对象 `ObjOff = -(2 + ArgOff + Size)`（含动态 alloca 压入 `DR16` 时在 `getFrameIndexReference` 自动再减 `4`）读取入参；
   - **彻底无静态参数槽**：编译器不再生成 `_fn_PARM_n` 符号或 `.mcs251.OSEG.*`/`.mcs251.DSEG.*` 参数节，原生支持任意多参数函数指针间接调用（含 `__bit`）、多参数递归、以及 ISR 与前台共用多参函数（零槽冲突）。
3. **结构体 / 联合体按值 ABI（`byval` / `sret`）**：
   - **参数**：空聚合忽略；普通 C / 平凡可拷贝结构体与联合体通过 `byval`（align 1）压栈传递——`<= 16B` 在调用点直接展开逐字节拷贝，`> 16B` 由后端内联 `BYVAL_COPY32` / `BYVAL_COPY16` 字节拷贝循环完成（大型 `llvm.memcpy`/`memmove`/`memset` `> 16B`（`OptSize` 为 `8B`）则调用 `_memcpy`/`_memmove`/`_memset` 运行时）；
   - **返回值**：空聚合忽略；1B / 2B / 4B 平凡聚合强制打包为 `i8` / `i16` / `i32` 直接经寄存器返回；3B、`> 4B`、含柔性数组或非平凡聚合通过隐藏 `sret` 指针返回；LLVM IR 层多值返回（如 `{i32, i32}`）由 `CanLowerReturn` 自动降级为 `sret`。
4. **变参函数（无上限栈游标）**：
   - 支持 **`F >= 0`** 个固定参数（含 C23 `void f(...)`、`F=0` 时全部实参压栈）、**无 6 参硬上限**、支持**变参函数指针间接调用**、支持结构体/联合体按值变参与 `va_arg(ap, __bit)`；
   - `va_list` 结构为 `{ptr __base, i32 __off}`（32 位 AS0 下 8 字节、`__off@+4`；16 位 AS0 下 6 字节、`__off@+2`）；
   - `va_start` 通过 `FIADDR` 计算被调固定帧锚点 `ObjOff = -(2 + FixedStackArgBytes)` 存入 `__base` 并清零 `__off`；`va_arg(ap, T)` 将 `__off` 增加步长（标量/位按提升宽度，`+int16` 下 `int` 为 2B，默认 4B；结构体按 `sizeof(T)`），并从 `base - off_next` 读取。
5. **直接尾调用优化（`TCRETURNdi` / `ejmp`）**：
   - 对满足 `IsDirect && !ISR && !CallerVarArg && !CalleeVarArg && !hasFP && !CallerByVal && !CalleeByVal && OutStackBytes <= IncomingStackArgBytes` 的 `tail call`，直接复用/覆写入参栈槽、恢复 CSR、回收本地帧后发射 4 字节 **`ejmp <callee>`**（不再额外 `ecall` + `eret`）；其余情形（含间接尾调用、超长出参栈、ISR 内）安全回退为普通 `ecall` + `eret`。
6. **中断调用约定 `CallingConv::MCS251_INTR`（编号 128，`mcs251_intrcc`）**：
   - 签名强制 `void()`、非变参、无直接调用与取地址；保存 37 字节固定帧（不叠加 CSR 保存）并以 `RETI (0x32)` 返回。

---

## 5. 目标文件格式、重定位与二进制线协议

### 5.1 双目标格式（`-mcs251-object-format=elf|rel`）
- **`elf`（默认生产格式）**：**ELF32 Big-Endian (`ELFCLASS32`, `ELFDATA2MSB`), `ET_REL`, `SHT_RELA`**（显式加数，禁止 CREL），实验性机器号 **`EM_MCS251 = 0x9999` (39321)**。`llc -filetype=obj` 与 `clang -c`/一键链接均默认产出 ELF；纯文本汇编 `-filetype=asm` / `-S` 默认保持干净的 ASxxxx 汇编文本（仅显式传 `-mcs251-object-format=elf` 时开启 ELF 文本模式）。
- **`rel`（显式兼容格式，须传 `-mcs251-object-format=rel`）**：ASxxxx `XH3` 文本目标（`MCS251RELObjectWriter.cpp`，`MaxTPayload=13`），仅支持传统 `CSEG`/`.mcs251.xinit`/`.mcs251.dseg`/`OSEG`/`DSEG`/`REG_BANK_0`；明确拒收 v2 属性、跳转表 `J16`、ISR、位对象、XDATA/CODE 全局、符号指针初值、DWARF `-g`、内联汇编、weak/COMDAT 与 G11 固定放置。
- **目标专用 Section Flags 与标准扩展节**（`llvm/include/llvm/BinaryFormat/ELF.h`）：
  - `SHF_MCS251_OVERLAY = 0x10000000`（`OSEG` / `REG_BANK` / `BIT_BANK` / `SSEG`）
  - `SHF_MCS251_EDATA_MOVABLE = 0x20000000`（G8：允许 DSEG 在低 128B 窗口满时迁移至 EDATA，仅 v2）
  - `SHF_MCS251_XSEG_SPLIT = 0x40000000`（G13b：允许 `>65535B` 全零 XSEG 跨 64K 连续放置并由链接器合成清零记录，仅 v2）
  - `SHF_GNU_RETAIN = 0x00200000`（仅限 G11 `.mcu.fixed.*`，同时允许 `EI_OSABI = ELFOSABI_GNU`）
  - `SHT_GROUP` / `SHF_GROUP`（`GRP_COMDAT`）：支持普通函数与普通全局变量的跨 TU COMDAT 去重与 `STB_WEAK` 弱定义覆盖（ISR、Bit 对象与 G11 固定放置保持 Fail-Closed 拒收）
  - `.debug_*` 非 ALLOC 调试节：ELF `-g` 模式下由后端发射、由 `mcs251-lld` 拼接重定位并作为非 ALLOC 尾节输出（不进入 `PT_LOAD`，不影响固件代码与数据镜像）

### 5.2 全部 13 种 ELF 重定位（`ELFRelocs/MCS251.def`）

| 编号 | 名称 | 写入宽度 | 语义与检查规则 |
|---:|---|---:|---|
| `0` | `R_MCS251_NONE` | 0B | 无操作 |
| `1` | `R_MCS251_16` | 2B BE | `V = S + A` 须在 `[-0x800000, 0xFFFFFF]`，写低 16 位；**F9 门禁拒绝 XDATA 类目标** |
| `2` | `R_MCS251_24` | 3B BE | `0 <= V <= 0xFFFFFF`，写 24 位大端；**F10 门禁要求 XDATA 目标落在所属对象 `[Lo, Hi]` 内** |
| `3` | `R_MCS251_LO8` | 1B | `V` 的 bits `[7:0]`（范围检查同 `R_MCS251_16`） |
| `4` | `R_MCS251_MID8` | 1B | `V` 的 bits `[15:8]` |
| `5` | `R_MCS251_HI8` | 1B | `V` 的 bits `[23:16]` |
| `6` | `R_MCS251_PC8` | 1B | `S + A - (P + 1)`，要求有符号 8 位 `[-128, 127]` |
| `7` | `R_MCS251_J16` | 2B BE | CODE 控制转移（F9/F11），要求 `bank((P+2)&0xFFFFFF) == bank(V)`；用于 `LJMP` / `BR_JT` 跳转表 |
| `8` | `R_MCS251_J11` | 2B | `AJMP` (`0x01`) / `ACALL` (`0x11`)，检查 2K 页匹配并合入低 11 位（`lld` 支持，后端当前不主动发射） |
| `9` | `R_MCS251_ISR_REF` | 0B | 仅限 `.rela.mcs251.isr` 记录 `+12` 处（`addend=0`），关联同对象 `STT_FUNC` |
| `10` | `R_MCS251_BIT_REF` | 0B | 仅限 `.rela.mcs251.bit` 记录 `+4` 处（`addend=0`），关联 `STT_OBJECT` 位句柄 |
| `11` | `R_MCS251_BITADDR8` | 1B | 仅限可执行 `PROGBITS` 中 8 条经典位指令的操作数字节 `+1` 处（占位须为 0，`addend=0`），写入已解析的 8 位位地址 |
| `12` | `R_MCS251_32` | 4B BE | **仅限非 ALLOC DWARF `.debug_*` 节**（ALLOC/普通元数据节的 32 位符号 fixup 仍硬报错）；写入 32 位大端地址/节偏移，`lld` 与 `llvm::object::RelocationResolver` 均支持解析（已 GC 裁剪的目标写入 tombstone `0`） |

### 5.3 ELF 对象身份：v1 vs v2（`MCS251Attributes.h`）
- **v1 对象身份（已弃用，仅显式契约 `1,1,32,8,1` 产出）**：
  - `e_flags = 0x00000001`，禁止含 `.mcs251.attributes`；
  - 必须含且仅含 1 个 52 字节 `.note.mcs251.abi`（`SHT_NOTE`, align 4, `namesz=7 "MCS251\0"`, `descsz=32`, `type=1`, 8 个大端 u32 固定为 `{1, 1, 0, 2, 0x0000F3FF, 7, 0, 0}`）。
- **v2 对象身份（全部 v2 契约的默认 ELF 产物，A4 W1–W8）**：
  - `e_flags = 0x00000102`（低字节=版本 2，bit8=后端原生标记），禁止含 `.note.mcs251.abi`；
  - 必须含且仅含 1 个 **`.mcs251.attributes`**（`sh_type = 0x70000003`, 非 ALLOC, `sh_flags = 0`, `sh_addralign = 1`）：
    - **17 字节大端外层信封**：`FormatVersion='A'(0x41)` + `VendorSize:u32be` + `"MCS251\0"` (7B) + `ScopeTag=1 (File)` + `ScopeSize:u32be`；
    - **自描述记录格式**：`Tag:ULEB128`, `TypeFlags:u8`（bit7=`Critical`, bits6:0=`ValueType`: `1=U32(BE)`, `2=UTF8`, `3=MIX`, `4=BYTES`）, `Length:ULEB128`, `Value:byte[Length]`；
    - **22 个必需 Tag（全部 `Critical=1`）**：
      - `4 object_protocol_version=2`、`5 call_abi_major=2`、`6 call_abi_minor=1`（静态指针参数槽唯一载体）、`7 register_parameter_variant=3`、`8 general_register_set=0x0000F3FF`、`9 int_bits=32`、`10 long_bits=32`、`11 as0_pointer_bits=32`、`12 as_layout_version=2`、`13 default_placement=1|8`、`14..17 init/placement/stack/function_contract_version=2`、`18..19 required_capabilities_lo/hi=0`、`20 abi_options=0`、`24 memory_model_profile=MIX(as0, placement)`、`25 code_model_profile=1`、`26 code_pointer_bits=32`、`27 object_protocol_minor=0`、`28 function_signatures (VT_BYTES)`；
      - 可选保留位 `21..23 reserved0..2` 若出现必须为 0。

### 5.4 P-4 跨 TU 函数签名记录（Tag 28，`MCS251Signatures.h`）
- 嵌入在 v2 `.mcs251.attributes` 的 `Tag_FunctionSignatures (28)` 中（`TypeFlags = 0x84`, `Critical | VT_BYTES`）。
- **注意字节序差异**：外层属性信封为大端，但 **Tag 28 内部多字节整数严格为小端（Little-Endian）**：
  - 头 4 字节：`version:u8 = 1`, `flags:u8 = 0`, `count:u16le`（空表为 `01 00 00 00`）；
  - 每条记录：`name_off:u32le`, `role:u8`（bit0=`definition`, bit1=`declaration`, bit2=`no_prototype`, bit3=`variadic`）, `param_count:u8`, `bit_bitmap:ceil(param_count/8)B`（标记 `bit` 参数位置）, `ret:u8`, `call_abi_major:u8`, `call_abi_minor:u8`；
  - 尾部：NUL 结尾字符串池。
- **K&R 零参豁免规则（2026-09-15 修订）**：当双方 `param_count == 0` 时允许 `bit2 (no_prototype)` 不同（兼容 `int main()` 与 `int main(void)`），因此链接器做同符号名全两两比较而非仅比首项。

### 5.5 ISR、Bit 与 G11 专用元数据节
| 节名 | ELF 类型 / 对齐 | 协议版本与记录格式 | 关联重定位 |
|---|---|---|---|
| `.mcs251.isr` | `SHT_PROGBITS`, align 4, 非 ALLOC | `ProtocolVersion = 2`，每条 **24 字节大端**，`RequiredCaps = 0x0001`；每个 ISR 定义成对发射 `RK_ISR_ENTRY(1)` + `RK_ISR_REGISTER(2)`；127 槽 profile（`0..126`：109 Legal、16 Reserved `{7,13,22,23,32..35,81,92..95,100,101,113}`、2 System `{14,15}`） | `R_MCS251_ISR_REF (9)` 于记录 `+12` |
| `.mcs251.bit` | `SHT_PROGBITS`, align 4, 非 ALLOC | `ProtocolVersion = 1`，每条 **8 字节大端**（`ver=1, kind=1(自动)/2(固定), init=0/1, caps=1, 4B 0`） | `R_MCS251_BIT_REF (10)` 于记录 `+4` |
| `.mcs251.bitprofile` | `SHT_PROGBITS`, align 4, 非 ALLOC | BT14 位感知 CRT 声明（16 字节大端，表明 CRT 实现 `__mcs251_bit_init` 掩码 RMW 而非全清 16B） | 无 |
| `.mcs251.placement` | `SHT_NOTE`, align 4, 非 ALLOC | G11-B：owner `"MCS251\0\0"`, `type = 1`，大端放置记录（含地址、空间、实体类型、归属、flags、`H_source` SHA-256 低 32 位及函数区间大小 fixup） | 符号/区间 fixup |
| `.mcs251.placement.names` | `SHT_NOTE`, align 4, 非 ALLOC | G11-B：owner `"MCS251\0\0"`, `type = 2`, `association_version = 1`，按 `record_index` 映射精确 ELF 符号名 | 无 |
| `.mcs251.placement.positions` | `SHT_NOTE`, align 4, 非 ALLOC（仅输出 `ET_EXEC` 审计模式） | G11-D2：owner `"MCS251\0\0"`, `type = 3`, `position_version = 1`，记录所有输入对象原始 SHA-256 及每个 ALLOC 输入节的最终片区地址映射 | 无 |
