# A4 实施任务书草案：v2 ABI 静态指针参数槽（改写包缺口 G3 同项）

- 日期：2026-09-12（调查日）；分支 `minimal-isr`，HEAD `36d82d197`（A3 已落地）。
- 本文为**只读调查产物**：所有 file:line 断言均在盘上源码与当前工具链（`/home/liu/build-mcs251-s1/bin/clang`、`/home/liu/build-mcs251/bin/llc`，2026-09-13 构建，含 A3）上逐一核验；复现实验在 `/tmp` 完成，未改动仓库任何源码。
- 上游依据：`validation/mcs251-models/DESIGN.md`（盘上、不入 git、只读）D.5/D.5.1（1229–1273 行）、B.2/B.2.1（298–386 行）、N.1–N.9（1508–1757 行）、阶段0分项冻结清单（1759 行起）；`proposals/RUNTIME-AS-PTR-DESIGN-A.md` §3-A4（334–355 行）。

## 0. 任务书结论（一句话）

**指针静态槽的 lowering（调用方写槽/被调方读槽/槽发射/槽宽/规范化）在 v2 布局下已经完整实现并通过 asm 路径实测；A4 的真实工作量不在代码生成，而在"发布一个最小 v2 对象身份"**：v2 属性值登记（PM 拍板项）→ `.mcs251.attributes` 生产发射 → `isV1ObjectCompatible`/`classifyModule` 门禁为"纯指针槽 v2 模块"重开 → `mcs251-lld` 接受 v2 身份并做逐字段一致性/混链校验 → v2 CRT 产物。v1（兼容契约）行为完全不变。

范围裁定（沿用 DESIGN.md D.5.1）：
- **不修改 v1 ABI**：兼容契约（`1,1,32,8,1`）下后续指针参数继续拒绝；不以整数改签名；不把 `AllowStaticPointers` 在 v1 无条件置真；不修改 `isV1ObjectCompatible` 伪装新 ABI。
- **首期只覆盖 32 位 AS0 的 v2 契约（XSmall/Small）**；Tiny/XTiny（16 位 AS0）对象路径保持现有硬拒绝（见 §1.6）。
- 多参数间接调用、varargs、struct 参数继续拒绝（现状钉子，见 §3.8）。
- libc 迁回真实双指针签名是**独立后续提交**，不混入 A4（见 §4）。

---

## 1. 现状调查（file:line 证据，全部已核验）

### 1.1 静态参数槽的发射路径

**槽符号命名与两侧引用**（`llvm/lib/Target/MCS251/MCS251ISelLowering.cpp`）：

- `parameterSlot()`（3029–3035 行）：槽符号 = `\1<mangled callee>_PARM_<n>`（`\1` 防二次 mangling），以 `DAG.getExternalSymbol` 进入 DAG；`n = Index+1`，即第二参数是 `_PARM_2`。
- **被调用方读槽**：`LowerFormalArguments`（3187–3197 行）——首参进 DPL/DPTR 或 DPL:DPH:B:A（3159–3182 行），第二参起对每个 `Ins[I]` 发 `DAG.getLoad`，链在入口、先于任何调用；4B 指针经 `canonicalizePointer32`（3194–3195 行 → 1345–1353 行定义，把最高字节 A 规范化为 0）。
- **调用方写槽**：`LowerCall`（3346–3353 行）——`Outs.size()>1` 时先解析被调名（GlobalAddress 3332–3335 行 / ExternalSymbol 3336–3339 行；间接调用多参数 3340–3343 行拒绝："multi-argument indirect calls are not supported"），随后在 CALLSEQ_START 之后、首参寄存器建立之前**串行写完所有槽**（注释 3344–3345 行："Finish every slot store before setting up the first argument registers"），4B 指针先 `canonicalizePointer32`（3348–3349 行）再大端 `DAG.getStore`。

**槽的定义与放置**（`llvm/lib/Target/MCS251/MCS251AsmPrinter.cpp`）：

- `emitParameterSlots()`（1154–1210 行），由 `runOnMachineFunction`（837 行）每函数调用：
  - 触发条件 `F.arg_size() >= 2`（1156–1157 行）；linkage 限 local/external（1158–1160 行）。
  - **leaf 判定**：MachineInstr 扫描 `isCall()||isInlineAsm()`（1163–1167 行）→ leaf 用 `OSEG`、non-leaf 用 `DSEG`（1168 行）。
  - **ELF section**：`.mcs251.OSEG.<FunctionNumber>` 或 `.mcs251.DSEG.<FunctionNumber>`，`SHT_NOBITS`，flags `SHF_ALLOC|SHF_WRITE`（leaf 且 ELF 再加 `SHF_MCS251_OVERLAY`，1170–1174 行）；REL/asm 文本侧同时发 `.area OSEG (OVR,DATA)` / `.area DSEG (DATA)`（1176–1177 行）。
  - **槽符号**：`<fn>_PARM_<n>`，非 local 函数发 `MCSA_Global`（1196–1199 行）；ELF 下加 `STT_OBJECT` + `emitELFSize(SlotSize)`（1201–1205 行）。
  - **槽宽**：`F.getDataLayout().getTypeStoreSize(ArgTy)`（1195 行）——**指针槽宽直接跟随 DataLayout 的指针宽度**，与 D.5 冻结表自动一致（核对见 §2.2）。
  - **槽初值**：`emitZeros(SlotSize)`（1206 行）——NOBITS 下只推进位置计数器，**对象中无字节、无重定位**（详见 §3.1）。
- **跨 TU 槽引用**：仅取函数地址不产生槽依赖；槽符号在被代码真实引用时才声明 `MCSA_Global`（`emitInstruction`，1212–1222 行 + `LocalParameterSlots` 1379–1387 行；测试 `llvm/test/CodeGen/MCS251/elf-oseg.ll`）。
- **槽地址的重定位**：调用方/被调方对槽符号的 `mov dr,#imm16`（opcode 0x7e/0x7a）引用使用既有 `R_MCS251_MID8/LO8/HI8`（实测 `tslot.o` readelf：`_add3_PARM_2`/`_add3_PARM_3` 三件套）——**无需新重定位号**。

### 1.2 指针参数被拒的确切位置与判断条件（v1 路径）

`MCS251ISelLowering.cpp`，两处对称：

- `checkParameterType()`（3057–3074 行，IR 类型侧，同时覆盖被调方形参与调用点 `CB->getArgOperand`）：`Ty->isPointerTy()` 时先查 `hasOrdinaryPointerABI`（3039–3055 行：AS 0/1/2/3/4/8/9 返回 true，其余——含 5/6/7/>9——拒绝"no ordinary register/static-slot ABI"），随后 **`if (Index && !AllowStaticPointers) report_fatal_error("MCS251: static pointer parameters are not supported by the compatibility ABI")`（3063–3065 行）**。
- `checkParameter()`（3076–3090 行，legalized 片段侧）：**`if (Index && Arg.Flags.isPointer() && !AllowStaticPointers)` 同一报错（3087–3089 行）**。

调用点：`LowerFormalArguments` 在 3137–3145 行取 `AllowStaticPointers` 并对形参（3139 行）与 `Ins`（3145 行）调用检查；`LowerCall` 在 3313–3326 行对 `CLI.CB` 实参（3317–3319 行）与 `Outs`（3322–3323 行）调用检查。**该拒绝与输出格式无关：兼容契约下 `-filetype=asm` 同样被拒**（实测，§1.7 复现 4）。

### 1.3 AllowStaticPointers 的真实语义

- 定义处仅两行：`bool AllowStaticPointers = DAG.getDataLayout().getProgramAddressSpace() == 4;`（`LowerFormalArguments`，3137 行）与 `bool AllowStaticPointers = ProgramAS == 4;`（`LowerCall`，3313 行，`ProgramAS` 取自 3269 行）。
- **真实条件**：DataLayout 的**程序地址空间字段**为 4。由布局表（§1.4）可知：`P4` 只出现在两条 v2 终串里；兼容串无 `P4`（ProgramAS=0）。
- **真实用途**：它就是 D.5 的 v1/v2 开关本体——"后续指针参数静态槽"这一能力只在 v2 布局（Tiny/Small 终串，ProgramAS=4）下放行；在兼容布局（v1）下 fail-closed。协调员摘要"条件与 ProgramAS==4 有关"属实；注意它比较的是**模块布局的程序 AS**，与"函数是否放在 AS4"（3123–3129 行的 `FnAS != ProgAS` 检查是另一件事）无关。
- 结论：**"v2 布局下 lowering 已放行"是现状，不是 A4 要新写的部分**；真正拦住 v2 模块出对象的是身份门禁（§1.5/§1.6）。

### 1.4 v2 布局版本开关：设置与读取

数值契约五元组 `TransportVersion,ASLayoutVersion,AS0PointerBits,DefaultPlacement,ExecutionContext`（`llvm/include/llvm/TargetParser/MCS251TargetParser.h:39–47`）：

**布局与 ProgramAS**（`llvm/lib/TargetParser/MCS251TargetParser.cpp`）：
- 兼容串（28–29 行，`...-S8`，无 P4）+ `CompatibilityPointers {AS0:32}`（12–14 行）→ `getLayoutDesc` 返回 `ProgramAS=0`（41–42 行）。
- Tiny 终串（30–31 行）/ Small 终串（32–33 行），`TinyPointers`/`SmallPointers`（16–26 行：AS0 16/32、AS1/2/6/8=16、AS3/4/7/9=32）→ `ProgramAS=4`（47–52 行）。
- `isValidMemoryContract`（80–95 行）：ASLayoutVersion∈{1,2}；v1 只允许 `32/8`（=兼容）；16 位 AS0 不允许 ExternalData。

**设置入口**：
1. **clang driver**：`-mcs251-memory-model=<tiny|xtiny|small|xsmall|large>`，**默认 `xsmall` → 契约 `1,2,32,8,1`**（`clang/lib/Driver/ToolChains/Clang.cpp:1550–1571`；经 `-Xclang -mcs251-memory-contract=` 传 cc1）。
2. **cc1/后端功能串**：`+mcs251-memory-contract=v-t-as0-p-e`（`MCS251TargetMachine.cpp:112–134` 解析；`MCS251TargetParser.cpp:110–118` 拼写登记）。
3. **llc 命令行**：`-mcs251-memory-contract=<五元组>`（63–65 行）与 `-mcs251-memory-model=<名>`（67–70 行 + 76–101 行翻译），二者互斥（163–166 行）；**都未给时 llc 默认物化 `MemoryContract{1,2,32,8,1}`（183 行，= clang cc1 默认的 xsmall/v2）**。
4. **对象格式**：`-mcs251-object-format=rel|elf`（`MCTargetDesc/MCS251MCTargetDesc.cpp:36–43`，**默认 rel**，ELF 为 opt-in）；TargetMachine 侧 `usesELFObjects()`（`MCS251TargetMachine.cpp:217`）。ELF 输出强制 `-filetype=obj`（234–236 行）。

**读取侧一致性**：契约 → DataLayout（`MCS251TargetMachine.cpp:187–202`），模块 DataLayout 与所选契约不符时由 MCS251 IR 契约检查拒绝（`MCS251ContractCheck.cpp:763–781`，实测报 `input MCS251 data layout conflicts with the selected memory contract`）。

### 1.5 v1/v2 对象身份门禁

`llvm/lib/Target/MCS251/MCS251AsmPrinter.cpp`：

- `isV1ObjectCompatible()`（444–541 行）：契约未指定或 `ASLayoutVersion==1` → 直接 v1 兼容（449–450 行）。v2 请求只有 `AS0=32 && DefaultPlacement==8(InternalExtended) && 无 alias/ifunc` 才进入"降级为 v1"扫描（456–459 行）；扫描中**任何函数（含声明）存在 `Index>0` 的指针形参 → `return false`，注释即 "v2-sized static pointer slot"（513–516 行）**；签名/指令/常量里的非 AS0 指针能力同样 v2-only（`hasV1PointerTypes`，163–180 行，仅 AS0 指针通过）。
- `classifyModule()`（1252–1270 行）：不 v1 兼容且 `emitsObjectFile()` → `report_fatal_error("...cannot be represented by the v1 relocatable-object identity; v2 object output is not implemented (the complete identity and subprotocols are not approved)")`。**这就是当前 v2 模块出对象的唯一总闸**（实测见 §1.7 复现 3）；asm 输出不受此闸。
- 16 位 AS0：任何对象输出直接拒绝（`MCS251TargetMachine.cpp:238–243`："MCS251 16-bit pointer ABI cannot emit relocatable objects until the v2 ABI attributes and linker compatibility gate are implemented"）。

### 1.6 v2 对象身份载体现状

- **发射侧**：ELF 流水线只发 v1 身份——`e_flags=EF_MCS251_ABI_V1`（`llvm/include/llvm/BinaryFormat/ELF.h:1046–1047`）+ 逐字节固定的 52B `.note.mcs251.abi`（`MCTargetDesc/MCS251ELFStreamer.cpp:42–68`，descriptor 八字 `{1,1,0,2,0xf3ff,7,0,0}`）。文件头注释（X3-R1，9–25 行）明确：**无 v2 载体；`.mcs251.attributes` 编解码器存在但"无生产调用方"**。
- **编解码器**：`llvm/include/llvm/BinaryFormat/MCS251Attributes.h`（N.1–N.5 常量唯一登记点：SectionName/SectionType 0x70000003/EFlagsV2 0x102/Tag 表 125–155 行/RequiredTags 159–171 行/已裁定值 185–210 行）+ `llvm/lib/BinaryFormat/MCS251Attributes.cpp` + `MCS251AttributesReader.h`（`decode()`，72 行）+ 34 个单测（`llvm/unittests/BinaryFormat/MCS251AttributesTest.cpp`）。**注意 129–151 行大量字段标注 `open: candidate`——值登记是 PM 未决项，实现者不得自行取值**（N.9：仍开放 call ABI major/minor、寄存器变体、对象 minor、子协议版本、能力位、abi_options、code_model_profile）。
- **链接器侧**：`lld/MCS251/LinkerCore.cpp` 要求 `e_flags == 0x1`（38 行 `ABI_FLAGS`；636–641 行检查）且每对象恰一个 52B note 并**逐字段等于 `{1,1,0,2,0xf3ff,7,0,0}`**（596–620 行）——v2 对象会被 fail-closed 拒绝（正确行为，A4 需给链接器新增 v2 分支而非放松 v1 校验）。槽区的分配已就绪：`.mcs251.OSEG.*`（473–478 行，可写 NOBITS overlay）与 `.mcs251.DSEG.*`（490–496 行）分类、OSEG 组整体 overlay 复用最大者地址（1962–1979、2002 行）、DSEG 窗口分配（1989–2001 行）——**指针槽与整数槽共用同一套，链接器分配逻辑无需改动**。

### 1.7 41/42/43/82 报错复现（当前工具链，2026-09-13 构建，含 A3）

链路与工具（`mcs251-corpus-matrix/run-matrix.py:66–78`、`REPORT.md:3–4`）：
`clang /home/liu/build-mcs251-s1/bin/clang --target=mcs251-unknown-none -std=c11 -fmcs251-keil -O0 -Xclang -mcs251-memory-contract=1,1,32,8,1` → `llc /home/liu/build-mcs251/bin/llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -mcs251-object-format=elf -filetype=obj` → `mcs251-lld` + `validation/mcs251-elf/runtime/crt.o` + QEMU `stc32g144k246`。

1. **41/42/43（第三参 code 数组指针）——已复现（真实改写源）**：demo 41/42/43 的 GUI 共用同形签名（`mcs251-demos-rewritten/src/4x*/GUI/gui.h:15–20`：`LCD_ShowString(u16,u16,u8,u8*,u8)` 第 4 参指针、`GUI_DrawFont16/24/32(...,u8*,u8)` 第 5 参指针、`Gui_Drawbmp16(u16,u16,const unsigned char*)` **第 3 参** code 图像指针）。用改写后的 `41-.../GUI/gui.c` 走 T0/T1：clang **编译通过**（A3 后 AS4→AS0 传参放行，IR 为 `ptr addrspacecast (ptr addrspace(4) @gImage to ptr)`）；llc 报 **`LLVM ERROR: MCS251: static pointer parameters are not supported by the compatibility ABI`**。栈定位在 `@Fill_Triangel`：该函数（自身全整参）内部 `call void @_swap(ptr, ptr)`，**调用方侧**检查（`LowerCall` 3317–3319 行经 `CLI.CB` 实参）在第二指针实参处触发；最小用例（t41.c，`SendBytes(i16,i8,ptr)`）同样在 caller `@caller` 处触发。注：`mcs251-corpus-matrix/REPORT.md`（A3 前生成）把它们记为 `as-cast-gap`，A3 落地后该层已通，下一层即本错误——台账口径以本复现为准。
2. **82（第二参为指针）——第一道闸仍在 Sema**：`CANFD_Set_DMA_Buff(const stc_can_tx_t*, uint8_t*)`（`82-.../canfd_dma.c:124`）第二参指针，实参 `&DmaTxBuffer[...]` 为 AS3（`canfd_dma.h:28` `uint8_t xdata DmaTxBuffer[256]`）。实测：**隐式** AS3→AS0 传参仍被 clang 拒（`changes address space of pointer`，A3 只开 AS4→AS0，见 `MCS251ISelLowering.cpp:1077–1085` 的 CODE 分支边界与 DESIGN B.1.1）；把源改成**显式** cast 后 clang 通过、llc 报同一 `static pointer parameters` 错误（最小用例双路实测）。即 82 = "AS3 隐式转换（前端）＋ 静态指针槽（后端）"两道闸叠加，A4 只解除第二道。
3. **v2 契约下的真实阻塞——身份门禁**：同一最小用例改契约 `1,2,32,8,1`：`-filetype=obj` 报 `LLVM ERROR: MCS251: module uses an ABI capability that cannot be represented by the v1 relocatable-object identity; v2 object output is not implemented (...)`（§1.5）；而 **`-filetype=asm` 完整成功**——caller 写 4B 大端槽、callee 读槽并规范化、槽落 `.area OSEG (OVR,DATA)`/`_Fill_PARM_2: .ds 4`（§2.2 有逐宽度实测）。
4. **兼容契约连 asm 也拒**：`-filetype=asm` 同样报 `static pointer parameters are not supported by the compatibility ABI`（§1.2 的检查在 ISel，先于输出格式）。
5. **旁证（G3 台账，7 个 demo）**：`mcs251-demos-rewritten/README.md:253`（G3 行：19,21,25,26,40,47,62）与 `ledger.json` 各 demo `t1_log` 均为同一 `LLVM ERROR: MCS251: static pointer parameters are not supported by the compatibility ABI`（llc 阶段）。

---

## 2. D.5 冻结表转录与核对

### 2.1 表转录（DESIGN.md D.5，1229–1261 行，2026 冻结）

| 后续参数的完整指针类型 | v2 槽宽 | 备注 |
|---|---:|---|
| 默认 AS0，Tiny/XTiny | 2B | 与该模块 p0:16 一致 |
| 默认 AS0，Small/XSmall/Large | 4B | 即使所指对象与槽都在 EDATA，也不能缩成 2B |
| 显式 AS1/AS2/AS8，所有 v2 模型 | 2B | 保留 AS；AS8 在 XSmall 中仍为 2B |
| 显式 AS3/AS9 | 4B | canonical 数据地址，保留 bank 和 AS |
| AS4 CODE/函数指针 | 4B | 保留 CODE bank；函数入口契约另行记录 |
| AS5/AS6 及未开放独立 ABI 的 AS7 | 不分配普通指针槽 | 明确拒绝，不按同宽类型代传 |

配套规则：指针槽用 D.2 的大端内存表示（`high,low` / `00,bank,high,low`）、对齐 1B、4B 槽最高字节为零；槽宽由**完整函数签名**（原始参数次序、LLVM AS、pointer layout、CC、属性）决定，legalization 片段不是新源参数；槽值宽度与槽自身地址宽度是两回事；leaf OSEG / non-leaf DSEG 区别保留；overlay 的重入/递归/中断限制保留；首期多参数间接调用仍拒绝；v1/v2 默认拒绝裸混链。

### 2.2 与 DataLayout 指针宽度的一致性核对（实测）

静态槽宽来源是 `F.getDataLayout().getTypeStoreSize(ArgTy)`（`MCS251AsmPrinter.cpp:1195`），指针 store/load 的 VT 来自 CC 分析（按 DataLayout 指针宽度定型）。当前工具链 `-filetype=asm` 实测（`.ds N` 即槽宽）：

| 契约 | 参数 AS | 实测槽宽 | D.5 要求 | DataLayout 依据（MCS251TargetParser.cpp） |
|---|---|---|---|---|
| `1,2,32,8,1` XSmall | AS0 | 4B（`_Fill_PARM_2: .ds 4`） | 4B ✓ | SmallPointers AS0=32（23 行） |
| `1,2,32,8,1` | AS4（直接 `__code*` 形参） | 4B | 4B ✓ | p4:32（24 行） |
| `1,2,32,8,1` | AS3 | 4B | 4B ✓ | p3:32 |
| `1,2,32,8,1` | AS8 | 2B | 2B ✓ | p8:16（25 行） |
| `1,2,16,1,1` Tiny | AS0 | 2B | 2B ✓ | TinyPointers AS0=16（17 行） |
| `1,2,16,1,1` | AS4 / AS3 | 4B / 4B | 4B ✓ | p4:32 / p3:32（18 行） |
| `1,2,16,1,1` | AS8 | 2B | 2B ✓ | p8:16 |
| `1,1,32,8,1` 兼容 | 任意指针 | **拒绝**（不发射） | v1 不开放 ✓ | ProgramAS=0 → AllowStaticPointers=false |

**结论：lowering 侧槽宽与 D.5 冻结表逐行一致，无需改动；A4 需要把这些形状钉进回归测试（§5）。**

### 2.3 AS5（及 6/7）必须保持拒绝（fail-closed）

DESIGN.md 依据：B.2（298–321 行，AS5 行"不定义可接受的普通指针 ABI……首期显式拒绝普通 AS5 指针及其普通 load/store、参数、返回用途"）；B.2.1.2（336–386 行）"AS5 的拒绝必须是可执行检查，不是 DataLayout 字符串约定"、三层门禁（Sema / IR 契约 verifier / 后端）；D.5 表末行。现有实现位置（三层齐备，均实测）：

1. **IR 契约层**：`MCS251ContractCheck.cpp:104–122` `checkType` 的 AS 白名单 **不含 5**（含 6/7，但见第 3 层）→ `MCS251 contract violation: unsupported pointer address space 5`（clang 后端与手写 IR llc 双路实测均中）。
2. **ISel 层**：`hasOrdinaryPointerABI`（`MCS251ISelLowering.cpp:3039–3055`）case 列表 0/1/2/3/4/8/9 → AS5/6/7 首参/静态槽/返回全拒（3059–3062 行）。
3. **发射层**：`emitParameterSlots` 槽循环 `if (AS == 5 || AS == 6 || AS == 7 || AS > 9) report_fatal_error(...no ordinary static-slot ABI)`（`MCS251AsmPrinter.cpp:1183–1187`）。

A4 不触碰这三层；测试矩阵必须含 AS5（顺带 AS6/AS7/>9）的 v2 拒绝钉子（§5）。

---

## 3. 方案设计：v2 下开放多指针参数

### 3.0 关键定位（决定工作量分配）

由 §1.3/§1.7：`AllowStaticPointers` 已按 ProgramAS==4 放行 lowering，槽发射/读写/宽度已实现并在 asm 路径实测通过。**A4 = 为"含指针静态槽的 v2 模块"打通对象输出与链接**，即"v2 ABI/身份独立发布"的最小可执行切片（DESIGN.md D.5.1 1265–1266 行、B.2.1.2 ABI 表"后续指针参数静态槽｜v1 不开放；须 v2 ABI/身份发布"）。以下 3.1–3.4 是"沿用并钉死既有行为"，3.5–3.9 是新增工作。

### 3.1 槽的发射与放置；槽初值问题

- 沿用 §1.1 全套：leaf → `.mcs251.OSEG.<fn#>`（NOBITS, ALLOC|WRITE|OVERLAY），non-leaf → `.mcs251.DSEG.<fn#>`（NOBITS, ALLOC|WRITE）；符号 `<mangled fn>_PARM_<n>`，非 local 即 global + STT_OBJECT + size。**指针槽不新设 section 类别**——与整数槽同区混排（同一函数的 PARM_2..n 连续预留，AsmPrinter 1179–1207 行单循环完成）。
- **初值：零初始化问题不存在于对象层**——槽区是 NOBITS（对象无字节）或 `.ds`（仅推进计数器），CRT 不做 DSEG/OSEG 初始化，运行前槽内容未定义；语义与 v1 整数槽完全一致：**调用方在 ecall 前必然写槽**（CALLSEQ 内串行链保证，`MCS251ISelLowering.cpp:3328、3344–3353`），被调方入口即读（3187–3197 行）。因此**不需要任何指针槽初值重定位记录，也不涉及 CP-A**（CP-A 管的是"静态存储的指针初值"，DESIGN.md 1806–1812 行 CP-A 落地时机裁定；D.5.1 1271 行"函数内形成 &code_table[offset] 传递使用既有地址形成重定位"）。唯一需要重定位的是**槽自身的地址**引用（既有 R_MCS251_MID8/LO8/HI8，§1.1）。

### 3.2 调用方写槽序列 / 被调用方读槽（现状即规范，写进测试）

实测序列（XSmall，`Fill(u8 v, u8 *buf)`，4B AS0 指针槽）：

- 调用方：物化槽地址到 dr（`mov dr,#imm16`×2，0x7e/0x7a）→ 4B 大端逐字节 `mov @dr4+k, rk`（高字节在前）→ 首参入 DPL → `ecall _Fill`。
- 被调方（leaf）：入口即 `mov dr,#imm16`×2 物化 `_Fill_PARM_2` → 大端读 4B → `mov r0,#0x00` 规范化最高字节（`canonicalizePointer32`）→ 后续按指针使用。
- 2B 槽（Tiny AS0 / AS1/2/8）：单 `i16` store/load，无规范化步骤（D.2："16 位数据指针不要求清零 B/A"）。
- 字节序：槽内大端（D.2 内存表示）；寄存器通道小端序（DPL:DPH:B:A）——两者不可混用，现有实现已分离（splitI32ToBytes 1326–1335 行注释）。

### 3.3 AS4（code）指针参数与 AS0/AS3 的差异（41/42/43 场景）

- **宽度/表示相同**（4B canonical `00,bank,hi,lo`），差异在**取值来源与写槽前的地址形成**：
  - 41/42/43 实际形态是 `const u8 __code img[]`（AS4 对象）传给 `const u8 *`（AS0 形参）：前端生成 `addrspacecast AS4→AS0`（A3 放行），运行期等宽直通（`MCS251ISelLowering.cpp:1073–1076`），随后按 4B AS0 槽写槽——**slot 里存的是 canonical 24 位有效地址，无 AS 标记**（方案甲：无 tag/查表）。被调方读出后经 DR 统一寻址通道读 CODE（`parseAddress` 统一通道，A3 注释 1063–1066 行）。
  - 直接 `__code *`（AS4 形参）：同为 4B 槽，D.2 表"AS4 CODE/函数指针：A 发送为零，保留 CODE bank"；实测发射正常（§2.2）。AS4 形参的 pointee 隐含 const（B.2.1.1），CODE store 门禁不因传参变化。
  - AS3（xdata）形参：4B 槽、保留 bank；**隐式 AS3→AS0 实参转换仍被 Sema 拒**（82 的第一道闸，非 A4 范围）。
- **4B 槽最高字节为零**由写槽前的 `canonicalizePointer32` 强制（3348–3349 行），不是靠初值。

### 3.4 与 A3 转换的交互：传参前 cast 的合法组合表

| 实参值来源 | 形参 AS | 组合状态 | 依据（均已核验） |
|---|---|---|---|
| AS4 指针（code 数组/对象地址） | AS0（32 位模型） | **开放**：隐式转换（B.2.1.2 包含关系 true）或显式 addrspacecast（A3 直通）→ 写 4B 槽 | Sema 实测通过；`MCS251ISelLowering.cpp:1073–1076` |
| AS0（32 位）指针 | AS4 | 显式 addrspacecast 直通；隐式仍拒（包含关系单向） | B.2.1.2 矩阵；`LowerAddrSpaceCast` 同分支 |
| AS3 指针 | AS0（32 位） | 显式 cast IR 级放行（FarRAM 等宽直通，1099–1116 行）；**隐式传参 Sema 拒**（82 现状） | 实测（§1.7-2） |
| AS1/2/8 ↔ AS0 | — | i16 等宽直通（NearRAM 集合）；i16→i32 零扩展、i32→i16 常量收窄按既有规则 | 1099–1131 行 |
| AS4 ↔ AS1/2/3/8/9 | — | **拒绝**（"unsupported address-space cast involving CODE"） | 1077–1085 行 |
| 任意含 AS4 的转换（16 位 AS0 模型） | — | **拒绝**（i32→i16 装不下 CODE bank） | 1077–1080 行；D.4 禁止表 |
| AS5/6/7 任意 | 任意 | **拒绝**（三层 fail-closed） | §2.3 |

A4 不改此表；A4 只把"表的右列结果值进槽"的对象身份打开。**阶段边界必须继续测试并公布**（D.5.1 1273 行）：静态存储的 `addrspacecast` 初值仍 fail-closed——实测：v1 对象路径报 `defined global data requires ... initializer relocations are not supported`（X3 叶子规则：`isSupportedPointerLeaf` 明确排除 addrspacecast，`MCS251AsmPrinter.cpp:242–252` 注释区），v2 对象路径当前先被身份总闸拒绝（A4 后该形状仍属"超范围 v2 能力"，继续 fatal，CP-A 不落地；见 RUNTIME-AS-PTR-DESIGN-A.md §A5）。

### 3.5 核心新增（一）：v2 对象身份的最小发布

按 DESIGN.md N.1–N.9 三轨裁定实现，**只发必需最小集**：

1. **值登记（PM 拍板前置，见 §7）**：`RequiredTags`（`MCS251Attributes.h:159–171`）21 个 tag 逐一定值。已裁定值可直接用（ObjectProtocolVersion=2、GeneralRegisterSet=0xf3ff、IntBits/LongBits=32、CodePointerBits=32、AS0PointerBits=32、DefaultPlacement=8、MemoryModelProfile=XSmall）；**开放项必须先批**：CallABIMajor/Minor（候选 2/0）、RegisterParameterVariant（候选 3）、四个子协议版本（候选 2）、FunctionContractVersion、RequiredCapabilities Lo/Hi、ABIOptions、CodeModelProfile、ObjectProtocolMinor（候选 0）。建议 A4 以"call ABI minor=1 或 RequiredCapabilities 新位"显式表达"含指针静态槽"，禁止隐式缺省。
2. **发射**：给 `MCELFStreamer::createAttributesSection`（`llvm/lib/MC/MCELFStreamer.cpp:564` 起）按 N.2 裁定扩展"已定长的自描述属性体"入口（不改动 ARM/GNU 既有调用字节）；`MCS251ELFStreamer` 在 v2 对象时发 `.mcs251.attributes`（SHT 0x70000003，非 ALLOC，sh_addralign=1）+ `e_flags=0x00000102`，并**停发 v1 note**（N.8："v2 对象不同时发射一份用于骗过旧读取器的 v1 ABI note"）。调用 `MCS251Attributes` 编解码器落生产（首个生产调用方）。
3. **门禁重开**：`classifyModule`/`isV1ObjectCompatible`（`MCS251AsmPrinter.cpp:1252–1270`）从"v2-only 一律 fatal"改为分流：v1 兼容模块路径与字节**完全不变**；v2-only 模块（本片仅限：32 位契约 + 能力不超过"指针静态槽（D.5 表内 AS）+ 已有降级能力"）走 v2 身份发射；其余 v2-only 能力（alias/ifunc、16 位 AS0、未登记能力）继续 fatal。`MCS251TargetMachine.cpp:238–243` 的 16 位对象拒绝**保留**。
4. **工具**：`llvm-readobj` 增加 `.mcs251.attributes` 逐 Tag 展示（N.2 仍开放项）；ObjectYAML/工具负例可选。

### 3.6 核心新增（二）：mcs251-lld 接受 v2 身份

`lld/MCS251/LinkerCore.cpp`：

1. header 分流：`e_flags==0x1` 走既有 v1 note 逐字节校验（596–620 行不动）；`e_flags==0x00000102` 走新 v2 分支（调用 `MCS251AttributesReader::decode`）；其余值拒绝。
2. v2 语义校验：RequiredTags 齐全且 Critical、已裁定字段等值、开放字段等于**已登记值**；**跨对象逐字段一致性**（D.5 混链规则："逐字段比较 note 的兼容项，不得对整个 note descriptor 做 memcmp 禁止"——同布局同 ABI 而放置策略不同不得误拒）。
3. v1/v2 混链默认拒绝（N.8"混入另一代 MCS251 ABI 载体视为矛盾输入，不提供忽略身份的开关"）。
4. OSEG/DSEG 分配逻辑**零改动**（§1.6 已核对：473–478/490–496/1962–2002 行对指针槽同整数槽适用）。

### 3.7 v1 行为完全不变（钉死清单 → 回归测试项）

- 兼容契约（`1,1,32,8,1`）下，任何 `Index>0` 指针形参/实参：继续 `MCS251: static pointer parameters are not supported by the compatibility ABI`（**现有 lit 钉子已在 `llvm/test/CodeGen/MCS251/oseg-errors.ll`**，含 pointer-formal/pointer-call 两半，保持不删不改）。
- v1 对象字节不变：默认 REL/asm/ELF v1 黄金产物指纹不变（N.9 验收第 1–3 条）；`.note.mcs251.abi` 逐字节不变；relocation 0–8 不变。
- 不改 `AllowStaticPointers` 的判定式（ProgramAS==4）；不在 v1 置真（D.5.1 1267 行明令）。
- 不修改 `isV1ObjectCompatible` 的既有判定使其对 v1 返回不同结果；v2 降级路径里 513–516 行的指针槽 v2-only 判定保留（它只决定"是否需要 v2 身份"，不再决定 fatal）。

### 3.8 继续拒绝的能力（范围外，保持现状钉子）

- 多参数间接调用（`MCS251ISelLowering.cpp:3340–3343`；`call-error-indirect.ll`、`oseg-errors.ll` indirect 半）。
- varargs、struct/聚合参数、i64/f64 参数（`oseg-errors.ll` 各半）。
- 16 位 AS0（Tiny/XTiny）对象输出（`MCS251TargetMachine.cpp:238–243`）——A4 明确不解除。
- AS5/6/7 指针参数（§2.3 三层）。
- 静态指针初值（含 addrspacecast 叶子）——CP-A 不落地（D.5.1）。
- overlay 重入/递归/中断交错限制照旧（指针槽继承整数槽限制：OSEG 全组共享地址 + DSEG 不可重入；ISR 与主线共用指针槽函数的交错破坏是**文档化限制**，非本片诊断目标）。

### 3.9 demo 验收路径（A4 完成后的判定实验）

1. **契约迁移**：41/42/43（及 G3 七 demo）的编译链从 `1,1,32,8,1` 迁到 `1,2,32,8,1`（XSmall：AS0 仍 32 位，demo 代码无需改写；正是 `RUNTIME-AS-PTR-DESIGN-A.md` §A4 所说"v1 的 1,1,32,8,1 不足以完成四 demo 验收"的解法）。
2. **v2 CRT**：现 `crt.o` 是 v1 身份（`crt-selfstart.yaml` → yaml2obj，`gen-crt-elf.sh`；readelf 实测 Flags 0x1）。需要一份 `crt-selfstart-v2.yaml`（e_flags 0x102 + `.mcs251.attributes`）与（ISR 链）`crt-irq` 的 v2 变体，否则 v1/v2 混链拒绝。
3. **验收分档**（不可合并为一个"通过"）：编译（llc v2 obj）→ 链接（lld v2 全图 + 混链负例）→ QEMU `stc32g144k246` 运行窗口（沿 `run-matrix.py`/`mcs251-xdata-e2e/build.sh` 模式）。
4. **82 的前置**：需源改显式 cast（或等 AS3 隐式转换的未来切片）——**A4 验收不含 82 的完整链**，只含其"第二指针参数"后端半（最小用例）。

---

## 4. libc 升级预案（独立后续提交，不混入 A4）

现状（`validation/mcs251-runtime/src/mcs251_libc.h:16–50` 私有 ABI 警告 + `mcs251_libc.c:36/57/89` 三个 `static uint32_t g_*_src` 全局槽）：memcpy/strcpy/memcmp 用 "setter + 全局 uint32 槽" 传第二指针——非重入、中断交错可破坏、调用序列二步。消费者：`validation/mcs251-xdata-e2e/src/a3-priv-fw.c`（31–105 行三段调用）。

**迁回真实双指针签名的条件**（全部满足才动）：

1. A4 落地：v2 身份对象可发射、可链接（含 v2 CRT）。
2. **libc 及其全部消费者整体迁 v2 契约链**：`mcs251-runtime` 构建管线的 clang/llc 不传 `-mcs251-memory-contract`（CMakeLists.txt 200–243 行），即默认 xsmall/v2；一旦 libc 带 `memcpy(void*,const void*,uint32_t)` 双指针签名，`mcs251_libc.o` 即含指针槽 → v2 身份 → 与任何 v1 固件/crt 混链被拒。迁移必须连同 crt、a3-priv-fw、rt-acceptance 链一次性切 v2，或保留一份 v1 兼容别名对象。
3. 头部注释陈迹清理：`mcs251_libc.h:33–35` "全局指针变量也被拒绝"已过时（实测：v1 下 `void *gp;` NULL 初值、`char *gp3 = buf;`（AS0 重定位初值）均可出对象；仍拒的是含 AS4 cast 的初值）。升级提交应同步更正文档，避免后续误判。

**步骤建议**（独立 commit 序列，每步可回退）：

- S1（纯声明切换，A4 后）：签名改 `void* memcpy(void* dst, const void* src, uint32_t n)` / `char* strcpy(char* dst, const char* src)` / `int memcmp(const void* a, const void* b, uint32_t n)`；删除三个 setter 与全局槽；`-O0` 管道不变（-O2 PC-rel 超界问题与本迁移无关，勿混）。
- S2：a3-priv-fw 消费点改单调用序列；e2e 字节断言（`a3-libc-bytes.py`）按新 ABI 重录。
- S3：重入/中断交错回归：构造"ISR 中断在 setter 与 call 之间"的旧 ABI 破坏用例，验证新 ABI 下不再破坏；新 ABI 下 ISR 与主线并发调用同一原语的槽交错限制照 D.5 文档化（DSEG 槽不可重入仍存在——主线程两次 memcpy 嵌套时同槽互踩，等价旧行为，但**嵌套不再需要**，单一限制面收窄）。

**回归风险**：IR 审计脚本（`CMakeLists.txt` 生成的 mcs251rt-ir-audit）白名单需容纳新签名符号；rt-acceptance/QEMU 金样字节将变化（预期内）；v1 固件若直接消费 libc 对象将断链（故条件 2 是硬前提）。

---

## 5. 测试矩阵草案

记号：R=拒绝（带精确诊断），A=接受。维度：参数位置 × 指针 AS × 槽宽 × 契约/身份。

### 5.1 正例（A4 后新增，llc `-filetype=asm` 已可先行；`-filetype=obj` 随身份发布解锁）

| # | 位置 | AS（模型） | 槽宽 | 契约 | 期望 |
|---|---|---|---|---|---|
| P1 | 第2参 | AS0（XSmall） | 4B | 1,2,32,8,1 | A；asm/obj 双档；槽 `.ds 4`、符号 `_f_PARM_2` global STT_OBJECT size 4 |
| P2 | 第3参 | AS0 ← AS4 cast 实参 | 4B | 同上 | A；写槽前 canonicalize（最高字节 0）；对应 41/42/43 场景 |
| P3 | 第2参 | AS4 直接形参 | 4B | 同上 | A；断言存的是 code 对象 canonical 地址 |
| P4 | 第2参 | AS3 / AS9 | 4B | 同上 | A |
| P5 | 第2参 | AS1/AS2/AS8 | 2B | 同上 | A；单 i16 槽、无规范化 |
| P6 | 第2–5参混合（int,ptr,int,ptr） | AS0 | 4/2/1/4B 混排 | 同上 | A；槽序连续、leaf=OSEG |
| P7 | non-leaf（函数内再 call） | AS0 | 4B | 同上 | A；槽落 `.mcs251.DSEG.<fn#>`、无 OVERLAY flag |
| P8 | 跨 TU：caller-TU 引用外部声明 | AS0 | 4B | 同上 | A；引用方槽符号 undefined-global，定义方定义；取函数地址不产生槽依赖（沿 elf-oseg.ll） |
| P9 | 首参指针（既有） | AS0/AS4 | 寄存器 | 全部 | A；v1/v2 均可（回归） |
| P10 | Tiny asm 档 | AS0(16)=2B、AS4=4B | 混 | 1,2,16,1,1 | asm A；obj R（16 位对象闸保留） |

### 5.2 拒绝钉子（全部保持现状文案）

| # | 形状 | 契约 | 期望诊断 |
|---|---|---|---|
| N1 | 第2参指针（formal / call 两侧） | 1,1,32,8,1（asm+obj 双档） | `static pointer parameters are not supported by the compatibility ABI`（= oseg-errors.ll 现有钉子，v1 钉死） |
| N2 | 第2参 AS5（含手写 IR 绕过前端） | 1,2,32,8,1 | 契约层 `unsupported pointer address space 5`；同文件再钉 ISel 层 `no ordinary register/static-slot ABI` 与 AsmPrinter 槽层（三层各一） |
| N3 | 第2参 AS6/AS7/AS>9 | 同上 | 同 N2 三层钉 |
| N4 | 多参间接调用 | v2 | `multi-argument indirect calls are not supported` |
| N5 | varargs/struct/i64/f64 参数 | 全部 | 现有 oseg-errors.ll 文案 |
| N6 | 16 位契约 `-filetype=obj` | 1,2,16,* | `16-bit pointer ABI cannot emit relocatable objects ...` |
| N7 | v2 模块（指针槽）旧版工具/未发布期 obj | v1 工具链 | `cannot be represented by the v1 relocatable-object identity`（A4 后此钉转为"超范围 v2 能力仍 fatal"） |
| N8 | 静态初值含 addrspacecast（AS4 源） | v1/v2 obj | X3 叶子拒绝（阶段边界公布项，D.5.1） |

### 5.3 身份/链接测试（lld）

- v2 对象：readobj 逐 Tag 展示 == 登记值集；e_flags=0x102；无 v1 note。
- v1 对象字节与既有黄金完全一致（--file-headers/--sections/--relocations 指纹）。
- v1+v2 混链拒绝；v2 内部同 ABI 不同 DefaultPlacement 的逐字段比较语义（合法共存 vs 非法混链，按 D.5）。
- 缺 RequiredTag / 重复 tag / 未知 Critical tag / 非 Critical 必需项 → 拒绝（codec 单测已覆盖，链接层加策略负例）。
- OSEG overlay：两个 leaf 各带 4B 指针槽 → 同地址复用（沿 allocateOverlayGroup 语义断言 map）。

### 5.4 demo 验收（外部链，不入 lit）

41/42/43（迁 `1,2,32,8,1`）四档报告：clang → llc v2 obj → lld（v2 crt）→ QEMU 窗口；82 仅后端半（显式 cast 最小用例）。G3 七 demo 重跑 `tools/drive.py`（CONTRACT 改 v2）对照 T1/T2。

---

## 6. 风险、工作量估计、涉及文件清单

### 6.1 风险

| 风险 | 等级 | 缓解 |
|---|---|---|
| **开放值登记被 PM 推迟/改值** → 发射与链接无法定稿 | 高（阻塞项，非技术风险） | §7 列最小登记清单先行拍板；codec 单测与合成负例可先行（N.9 允许"先实现结构读取器和合成负例"） |
| v1 黄金字节回归（note/section/reloc 意外变化） | 高 | N.9 验收三条显式入 CI；`elf-oseg.ll`、`asxxxx-obj.ll` 等指纹测试不动 |
| 身份门禁重开误放其他 v2 能力（alias/ifunc/16 位/未登记能力） | 中 | classifyModule 分流白名单化：只放行"指针静态槽 + 既有可降级能力"；其余 fatal 原样 |
| lld 逐字段比较实现成 memcmp 整体比较 → 误拒合法混链 | 中 | 按 D.5"逐字段"规则实现 + 5.3 专项正负例 |
| DSEG 直接页容量（[0x00,0x80) 窗口，G8）在多指针槽大 demo 溢出 | 中 | 4B 槽放大占用；验收时看 map；缓解依赖 B.6 `--edata-end` 延伸，超出 A4 范围则记录缺口 |
| v2 CRT/运行时配套缺位 → demo 链不动 | 中 | §3.9-2 列为 A4 交付物（YAML fixture 变体，工作量小） |
| ISR 与主线共用指针槽函数的交错破坏被误当 A4 新缺陷 | 低 | 文档化限制（继承 v1 整数槽语义）；测试标注 |
| 82 被误纳入 A4 验收（其前端 AS3 闸未开） | 低 | §1.7-2/§3.9-4 明示边界 |

### 6.2 工作量估计（按 §3 分节，人日，单人）

| 项 | 内容 | 估计 |
|---|---|---:|
| W1 | 值登记澄清与设计冻结（PM 互动，非编码） | 1–2 |
| W2 | attributes 生产发射（MCELFStreamer 入口 + MCS251ELFStreamer + AsmPrinter 接线） | 3–4 |
| W3 | classifyModule/isV1ObjectCompatible 分流重开 + 16 位闸保留 | 2–3 |
| W4 | lld v2 身份读取/校验/混链规则 | 4–5 |
| W5 | readobj 展示 + YAML/工具负例（可裁剪） | 1–2 |
| W6 | 测试矩阵 §5 全量（lit + lld test + 单测扩容） | 3–4 |
| W7 | v2 CRT 变体 + demo 链（41/42/43 迁契约、drive.py v2 跑批） | 2–3 |
| W8 | 文档（libc 预案另计；DESIGN/边界公布） | 1 |
| — | **A4 合计** | **17–24** |
| W9 | libc 迁移（独立提交，A4 后） | 2–3 |

### 6.3 涉及文件清单

**预计修改（A4 本体）**：
- `llvm/lib/Target/MCS251/MCS251AsmPrinter.cpp`（classifyModule/isV1ObjectCompatible 分流；emitParameterSlots 不动）
- `llvm/lib/Target/MCS251/MCTargetDesc/MCS251ELFStreamer.cpp`（v2 载体发射）
- `llvm/lib/Target/MCS251/MCS251TargetMachine.cpp`（仅当需要把 v2 身份可达性接给 streamer；16 位闸不动）
- `llvm/lib/MC/MCELFStreamer.cpp` + `llvm/include/llvm/MC/MCELFStreamer.h`（自描述属性体入口，N.2）
- `llvm/lib/BinaryFormat/MCS251Attributes.cpp` / `llvm/include/llvm/BinaryFormat/MCS251Attributes.h`（若需新增已登记值；开放值不擅加）
- `lld/MCS251/LinkerCore.cpp`（+ LinkerCore.h：v2 身份分支、逐字段一致性、混链）
- `llvm/tools/llvm-readobj/`（可选：attributes 展示）
- 测试：`llvm/test/CodeGen/MCS251/`（新增 v2 指针槽正例族、扩 oseg-errors.ll 加 v2-obj 身份钉）、`llvm/test/MC/MCS251/`、`lld/test/MCS251/`（v2 身份/混链）、`llvm/unittests/BinaryFormat/MCS251AttributesTest.cpp`（扩已登记值）
- `validation/mcs251-elf/runtime/`（crt-selfstart-v2.yaml + gen 脚本变体；v1 产物不动）
- `mcs251-demos-rewritten/tools/drive.py`（验收跑批 CONTRACT 开关，仓库外）

**预计不动（钉死）**：`MCS251ISelLowering.cpp`（AllowStaticPointers 判定、槽读写序列、槽宽全部沿用）、`MCS251CallingConv.td`、`MCS251TargetParser.cpp`、`MCS251ContractCheck.cpp`（AS 白名单）、`llvm/lib/Target/MCS251/Runtime/`（除法运行时第二参 i16/i32 槽与本片无关）、`validation/mcs251-runtime/src/`（libc 迁移为 A4 后独立提交）。

---

## 7. 待用户/PM 拍板清单（A4 开工前置）

1. **v2 身份开放值的最小登记集**（N.9 阻塞项）：CallABIMajor/Minor、RegisterParameterVariant、InitProtocolVersion、PlacementProtocolVersion、StackContractVersion、FunctionContractVersion、RequiredCapabilities Lo/Hi、ABIOptions、CodeModelProfile、ObjectProtocolMinor——建议按 `MCS251Attributes.h` 各"candidate"值整体批一次，并**明确"指针静态槽"由哪个字段表达**（推荐：CallABIMinor=1 或 RequiredCapabilitiesLo 定义 bit0）。
2. A4 范围确认：仅 32 位 AS0 v2 契约（XSmall/Small）；Tiny/XTiny 对象闸保留。
3. demo 验收契约迁移（`1,1,32,8,1` → `1,2,32,8,1`）是否随 A4 一并执行（影响 drive.py/run-matrix 跑批基线）。
4. 82 的前端 AS3→AS0 隐式转换是否另立切片（不属 A4；当前需源码显式 cast）。
5. libc 迁移（§4）时机：A4 合并后独立排期。
