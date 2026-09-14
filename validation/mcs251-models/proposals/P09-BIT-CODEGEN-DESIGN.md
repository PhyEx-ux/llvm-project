# X5-P09 bit 对象代码生成正式实施设计（Keil `typedef bit BOOL` 兼容）

**日期**：2026-09-12。**状态**：设计交付、待 Alice Review 与 PM 签收；未实施，不构成实施授权。
**工作树**：`/home/liu/LLVM_STC32/MCS251`。用户指定基线 `minimal-isr @ 36d82d197`；本轮禁止 git 操作，故不声称自行核验分支/HEAD。
**本轮操作边界**：只读输入与源码，唯一写入本文；不修改 DESIGN.md、源码、测试、语料或台账，不构建、不运行产品测试。
**证据标签**：[S] 本轮盘上读取；[U] 用户给定或任务书中的调查/语料证据；[D] 本文规范；[P] 后续获授权后实施/验证。本文无新增模型或硬件 PASS。
**编号**：X5-P09 为语料缺口；拍板-P09 为句柄表示裁定；P-0…P-4 为本文切片；BTnn 为 bit 战役任务卡。四者不得混用。

---

## 0. 设计结论、优先级与能力边界

**结论：沿用已落地的独立 MCS251Bit 类型，在既有位对象记录/分配通道上补齐符号 consumer，再分别接通持久对象、调用私有值、显式 i8 值 ABI。不是重新设计类型，也不是删除七条诊断就完成。**

必须分开四项能力。

| 能力 | 本片契约 | 不得推导出的能力 |
|---|---|---|
| 语言值 | 独立 `__bit`，整数零→0、非零→1，表达式 i1 | `_Bool` 别名、任意浮点/指针转换 |
| 持久对象 | AS0 i8 身份句柄、四个专用 intrinsic、ELF 位记录 | 可取址 byte 对象、普通 AS5 指针 |
| 调用私有值 | SSA/寄存器，必要时规范化 1B AS0 栈槽 | BSEG 局部 packing、位银行、共享静态局部副本 |
| 值 ABI | 第一源参数/返回完整 DPL，后续 bit 原序号 1B 槽 | carry ABI、后续指针槽开放、静态槽自动重入、Keil 裸混链 |

### 0.1 规范优先级

用户本次要求及既定最新裁定 > 已签收 BIT-TASK-BREAKDOWN 接口 > BIT-FIRST-CLASS-INCREMENT > 更早方言设计。拍板记录 P01/P02/P05/P07/P08/P09/P10 直接沿用；BIT-TASK-BREAKDOWN 的早期“未存在”调查不是当前实现状态。

输入按序读完：P09-BIT-TYPE-DESIGN → BIT-DECISION-20260911 → BIT-FIRST-CLASS-INCREMENT → BIT-TASK-BREAKDOWN → DESIGN.md B.2/D.2/D.5；另读 RUNTIME-AS-PTR-DESIGN-A 作为结构参照。后者的其他切片决策、不同日期/HEAD 不外推为本片授权。本片采用相同纪律：先给规范和边界、再给实施切片、测试、拍板事项、架构结论。

### 0.2 承重锚点 [S]

以下行号均为本轮读取的工作树锚点；后续按函数名重定位。为缩短表格，**R 唯一定义为绝对根 `/home/liu/LLVM_STC32/MCS251`**，`R/…` 是该绝对路径的缩写，不是另一个工作目录。

| 已核事实 | 源码锚点 |
|---|---|
| 独立 MCS251Bit 已存在，不是 `_Bool` typedef | `R/clang/include/clang/AST/BuiltinTypes.def:65–73` |
| 表达式 i1；内存容器转换另行处理，不能据此保证 ABI 已对 | `R/clang/lib/CodeGen/CodeGenTypes.cpp:104–145,417–421`；默认 ABI 分类仍须专用覆盖，`R/clang/lib/CodeGen/ABIInfoImpl.cpp:17–75` |
| 固定 intrinsic 为 i32 ImmArg，read 返回 i1，其余 void | `R/llvm/include/llvm/IR/IntrinsicsMCS251.td:19–32,40–53` |
| 句柄结构属性与使用门禁，当前无符号 consumer | `R/llvm/lib/Target/MCS251/MCS251BitObject.h:9–19,34–36`；`R/llvm/lib/Target/MCS251/MCS251ContractCheck.cpp:480–614` |
| 特殊 LValue 已留 Symbolic 槽；当前 symbolic assert 未解除 | `R/clang/lib/CodeGen/CGValue.h:535–548`；`R/clang/lib/CodeGen/CGMCS251Bit.cpp:97–177` |
| Sema 身份当前仅固定地址；求值感知框架已存在 | `R/clang/lib/Sema/SemaMCS251.cpp:42–131,671–1446` |
| CodeGen toggle 当前只比较 ConstantInt，不接受符号身份 | `R/clang/lib/CodeGen/CGExprScalar.cpp:5500–5556` |
| i8 物理 CC→DPL，后续槽按原参数 Index+1 | `R/llvm/lib/Target/MCS251/MCS251CallingConv.td:31–40`；`R/llvm/lib/Target/MCS251/MCS251ISelLowering.cpp:3027–3034,3057–3089,3137–3147` |
| 8B 记录/两个 relocation/属性名已有冻结定义 | `R/llvm/include/llvm/BinaryFormat/MCS251Bit.h:49–87,120–131`；`R/lld/MCS251/BIT-OBJECT-CONTRACT.md` §3–7 |
| 定义有 kind-1 记录；extern 无记录，不发空节 | `R/llvm/lib/Target/MCS251/MCS251AsmPrinter.cpp:1062–1123` |

### 0.3 修正调查摘要中的过宽表述（不推翻拍板）

1. “七张门”是任务书主表，不是穷尽清单。另有 `R/clang/lib/CodeGen/CGExpr.cpp:6543–6551` 的**间接 bit 返回调用门**，P-3 必须迁移；`:6029–6030` 的 compound literal 门与 `R/clang/lib/CodeGen/CGBlocks.cpp:798–799` 的 block capture 门仍保留。
2. 七门中的 **alias 不开放**。BIT-TASK-BREAKDOWN §2.5 已排除 alias/ifunc；“全部开门”只能解释为支持形态的分流，不是逐条无条件删除。
3. “固定位 ∪ bit VarDecl”是**身份域**，不等于全部对象都套物理 CPL 限制。自动值例外受增量稿 §5、任务书 §2.1/2.2 保护，详见 §2.3。
4. 符号 read 接入不只改 `LowerOperation` 分派，还须覆盖 i1 结果合法化 `ReplaceBitReadResults`；当前固定路径见 `R/llvm/lib/Target/MCS251/MCS251ISelLowering.cpp:1170–1256`。
5. 外部 owner 邻位保留、新 CRT profile、ABI 签名检查不是“后端全部完成”。`crt-bit.yaml` 本轮目录核对不存在；v1 记录中也没有函数签名字段。分别登记 §5、§8，禁止误记 S3 完整关闭。

---

## 1. P-0 协议冻结文本：符号位访问 intrinsic

本节为 P-1a 及全部前端开门的**前置契约**。Alice 签收后实现者逐条照办；本轮文本冻结不授权编码。

### 1.1 名称、ID、完整 LLVM 签名 [D]

采用建议的 **独立 obj 族**，不重载、不复用固定 i32 ImmArg 族，不提供额外的 write(value)、test、volatile-flag 或动态地址接口。

| LLVM 正式名字 | TableGen 定义/生成 ID | LLVM FunctionType |
|---|---|---|
| `llvm.mcs251.bit.obj.read` | `int_mcs251_bit_obj_read` / `Intrinsic::mcs251_bit_obj_read` | `i1 (ptr addrspace(0))` |
| `llvm.mcs251.bit.obj.set` | `int_mcs251_bit_obj_set` / `Intrinsic::mcs251_bit_obj_set` | `void (ptr addrspace(0))` |
| `llvm.mcs251.bit.obj.clear` | `int_mcs251_bit_obj_clear` / `Intrinsic::mcs251_bit_obj_clear` | `void (ptr addrspace(0))` |
| `llvm.mcs251.bit.obj.toggle` | `int_mcs251_bit_obj_toggle` / `Intrinsic::mcs251_bit_obj_toggle` | `void (ptr addrspace(0))` |

TableGen 使用固定 `llvm_ptr_ty`（AS0 opaque pointer，**不是** `llvm_anyptr_ty`），四项 properties 均精确为 `[IntrHasSideEffects]`。不带 ImmArg；GV 本身是合法常量操作数，但不是整数立即数。四项均非 vararg、非 overload，恰一个参数。典型 IR：

```llvm
declare i1 @llvm.mcs251.bit.obj.read(ptr)
declare void @llvm.mcs251.bit.obj.set(ptr)
declare void @llvm.mcs251.bit.obj.clear(ptr)
declare void @llvm.mcs251.bit.obj.toggle(ptr)
```

以上仅展示签名，完整 intrinsic 属性由 TableGen/Intrinsic API 生成。声明位于模块的程序地址空间；句柄参数始终 AS0，不能把函数 ProgramAS 和参数 AS 混为一谈。Clang 通过 `CGM.getIntrinsic(ID)` 获取声明，禁止 `getOrInsertFunction` 按字符串伪造。

语义：read 对该持久 RAM 位采样一次，产生 i1；set/clear 分别一次 SETB/CLR，不先读取其逻辑旧值；toggle 一次 CPL，不提供旧值/新值返回。符号对象只分配 RAM 位 0…127，不借此调用族指向 SFR 或固定 ABS 引用；后者沿既有固定协议。位号由链接解析，句柄不是将要计算的 byte 地址。

**版本/号码**：LLVM intrinsic ID 是生成枚举，不承诺稳定数字，不占 ELF relocation 号。已有 `.mcs251.bit` v1、capabilities=1、BIT_REF=10、BITADDR8=11 不改变；不重解释 ISR 编号或一般 ABI note。P-4 的函数签名扩展不塞进本族参数或 v1 保留位。

### 1.2 效果模型：逐条沿用常量族保守论证 [D]

依据 `R/llvm/include/llvm/IR/IntrinsicsMCS251.td:19–32`，必须同时满足下表，而不是只贴一个 side-effect 标志。

| 常量族理由 | 符号族冻结处理 |
|---|---|
| bit backing 与普通 RAM/edata 重叠，触及真实内存 | 符号对象同样位于普通 RAM；未知最终 backing 不构成独立内存域，允许与普通 byte access/call 相互影响 |
| 不得 IntrNoMem | 四项均不得 `memory(none)`/readnone；unused read 仍不能被 DCE 当纯函数删掉 |
| 不得 IntrReadMem/IntrWriteMem 缩窄 | **read 也按可能读写任意内存**；三写项也不标 writeonly。硬件实际指令读写类别与 IR 保守上界分开 |
| 固定族没有 pointer，不能 argmemonly | 符号族虽有 ptr，但它是身份而非 backing 地址；仍不能 IntrArgMemOnly，也不能参数 readonly/writeonly/readnone 或 dereferenceable/noalias 指针存储承诺 |
| 不得 inaccessiblememonly | backing 可被普通 byte 访问，绝非 inaccessible memory |
| 不得 speculatable；访问次数/顺序受保护 | 不投机、不跨有序内存访问重排，不将两个动态访问 CSE 成一次；保持分支/短路语义 |
| 未设窄化属性即 worst-case effects | 使用 `[IntrHasSideEffects]`，不添加效果窄化；生成的其他通用默认属性不当成原子性证明 |
| read i1，由 carry/PSW 采样后物化 | DAG 有 chain；MOV C,bit→物化依赖保留 glue/真实 PSW Use/Def，不能中途被 clobber，也不能再次读位 |

P02 分层仍有效：固定 L1/sbit **源语言隐含 volatile**；普通持久 bit 只按声明决定 volatile，符号族首版保守到连非 volatile 访问也可保留，**这不是给源对象添加 volatile**。自动 volatile bit 走普通 volatile byte，不使用 obj 族。

机器层沿用原 bit 指令的 mayLoad/mayStore/side effects 与链。需要 MMO 时采用真实已知 backing 或 unknown alias 的保守 MMO，**不得以句柄 GV 的 1B 身份作为可 disjoint 的内存位置**，不得引入 bit 专属 TBAA/alias.scope/noalias/invariant.load。有序动态路径上的次数、相对 ordinary load/store/call 顺序是验收对象；内联、展开等变换只要保持该路径的动态事件语义可接受，不以禁止一切静态复制替代证明。不仅加 convergent 就宣称安全。

### 1.3 ContractCheck 白名单算法（完整而非名字测试）[D]

在模块级目标入口、优化前及最终后端入口保留校验；关闭通用 verifier 不关闭本契约。检查分成两个方向，缺一不可。

**A. 从 intrinsic 声明/调用检查 consumer**（防没有标记 GV 的坏实参漏检）：

1. 遍历模块中 Function 和 CallBase；仅按 `Function::getIntrinsicID()` 的四个**精确枚举 case**识别本族。不使用 `startswith("llvm.mcs251.bit")`、contains、白名单名字数组替代 ID；不得先 `stripPointerCasts()` 把间接调用洗成直呼。
2. 对上述 ID，即使声明未被使用，也核验 `FunctionType` 精确等于 §1.1、非 vararg、只是声明而非定义，函数 CC 为 C，函数地址空间符合当前 ProgramAS。生成 ID 不足以证明签名正确：仍须显式检查返回/参数类型和参数数目。
3. 唯一允许调用节点为 **CallInst**，`getCalledOperand()` 就是该 Function，`getCalledFunction()` 非空，调用点 FunctionType 与声明一致，CC 为 C，参数数目为 1；拒绝 invoke/callbr、间接/alias/ifunc/cast callee、tail/musttail 调用及 operand bundles（包括句柄同时在参数0和 bundle 出现）。
4. `getArgOperand(0)` 必须**直接 dyn_cast 为本模块 GlobalVariable**，AS0，携带精确 `MCS251Bit::BitObjectAttrName`，并通过 §2.1 的完整对象结构校验。拒绝 ordinary GV、alloca、函数参数、null、undef、poison、inttoptr、GEP（包括零 GEP）、cast、select、PHI、alias、固定 ABS 伪句柄。不得对操作数剥 cast 再接受。
5. 声明及调用点不得附加与 §1.2 相悖的窄化 memory effects/speculatable/noalias/dereferenceable 等承诺；验证**有效的声明与 callsite 效果**，不能仅查 TableGen 的模板。也不得以 callsite `!alias.scope`/`!noalias` 等为受控存储提供优化优惠。
6. intrinsic 函数身份本身只允许通过上述直呼作为 callee 被使用；取其地址、导出到 initializer、转交其他函数同样拒绝。无用合法声明可以存在。

**B. 从每个句柄的每条 Use 检查逃逸**（防合法路径掩盖非法路径）：

1. 先沿用 `verifyMCS251BitObjects` 的结构校验，不能把 `hasAttribute` 本身当全验证。
2. 遍历 **Use**（不能只看唯一 User）；若 User 是 CallBase，只有 A 全部通过、且当前 Use 正是唯一实参0的直接句柄 Use，才白名单。callee Use 或 bundle Use 绝不豁免。
3. 常量中间节点只沿用 `isFullyKeepaliveConstant`：聚合常量及既有单操作数 pointer-cast registration 路径，**每条后继路径**都须终止于结构正确的 `llvm.used`/`llvm.compiler.used` 根。本片生产者只发 §2.1 的直接 AS0 形态；不扩大常量豁免去接受“cast 后送 obj intrinsic”。
4. `R/llvm/lib/Target/MCS251/MCS251ContractCheck.cpp:599–607` 的全路径要求不变。同一 uniqued aggregate/cast 一路通 llvm.used、另一路 initializer/ret/call，整体拒绝；不能用 visited-set 第一次合法到达就给共享节点永久合法标记。
5. 白名单外所有用途继续拒绝：普通 load/store/atomicrmw/cmpxchg/GEP/cast/ptrtoint/icmp/select/PHI/ret、普通 call、alias/ifunc、非保活 initializer。`llvm.assume`、lifetime、debug 地址 intrinsic 不成为新的句柄 consumer；调试信息不得暴露普通 byte 地址。

伪 intrinsic 的区分：相似前缀/后缀/用户名字不是白名单 ID；即使某个保留名字被 LLVM 识别为 intrinsic，错误签名仍被 A.2 拒绝。测试同时覆盖这两种情形，不能只造一个“未注册名字”就声称覆盖坏签名。源语言未定义行为分支不能替代手写 IR 入口验证：死函数、恒假分支中的非法句柄 Use 在优化移除之前仍须报错。

### 1.4 诊断与负例冻结 [D/P]

目标 ContractCheck 不是 Clang DiagnosticEngine，**没有 Clang diag::ID**。为新失败情形冻结稳定诊断主体（统一前缀仍为 `MCS251 contract violation:`）；lit 以 `not`/既有 fatal-error 路径的 `not --crash` 加 FileCheck 验证非零退出和主体。

| 新类别 | 固定主体 |
|---|---|
| 声明/调用 FunctionType、CC、定义形态错误 | `MCS251 symbolic bit intrinsic: invalid declaration or call signature` |
| 非直接 CallInst、bundle、tail、intrinsic 地址逃逸 | `MCS251 symbolic bit intrinsic: only direct unbundled calls are supported` |
| 非直接已验证句柄参数 | `MCS251 symbolic bit intrinsic: operand must be a direct AS0 bit-object global` |
| 错误效果属性/别名承诺 | `MCS251 symbolic bit intrinsic: incompatible effects or pointer attributes` |
| 句柄被非白名单 call/bundle 使用 | `MCS251 bit object 'NAME': handle must not be used by a non-whitelisted call or operand bundle` |

原有对象类型、初始化器、placement/linkage、instruction escape、constant expression/initializer、alias/ifunc 主体保留。多重非法输入按“结构→声明签名→调用形态→实参→效果→剩余逃逸”顺序诊断；mutation 用例每次只改一个因素，不依赖不稳定的 use-list 顺序。

P-0 验收必须审阅四合法声明/调用、四项坏返回或参数类型、非 AS0 参数、参数过多/少、vararg、函数定义、错误 CC、null/普通 global/dynamic handle、伪 intrinsic、operand bundle 与共享常量双用途 fixture 的预期。fixture 可先只签收设计，P-1a 用真实编译工具证明。

---

## 2. P-1 持久/静态位对象契约

P-1a 实现 §1 的后端消费；P-1b 实现本节前端生产与对象语义，必须成对闭环。

### 2.1 句柄发射、初始化、keepalive [D]

适用：文件作用域定义/tentative、文件 static、函数内 static、extern 引用，含 cv、多声明符及 typedef。**不适用**：sbit 固定引用、自动局部、形参副本、compound literal、TLS、alias/ifunc/weak/COMDAT/COMMON。

规范生产形态如下（`#0` 仅为打印器分配的属性组号，不冻结其数字）：

```llvm
@flag = global i8 1, align 1 #0
@local = internal global i8 0, align 1 #0
@external_flag = external global i8, align 1 #0
@llvm.used = appending global [2 x ptr] [ptr @flag, ptr @local], section "llvm.metadata"
attributes #0 = { "mcs251-bit-object" }
```

1. **精确结构属性** `mcs251-bit-object`（空字符串属性值）；不依赖变量名前缀、section 名或 named metadata。句柄 AS0/i8，定义 initializer 是 `ConstantInt i8 0/1`，alignment=1；无 section/COMDAT/TLS、默认 visibility/DLL storage，外部定义/声明或内部 local linkage。Clang 源 static 使用 internal；IR reader 既有 private 允许集不扩张。
2. 占位统一发 `global`，不因源 const 发 CSEG，不使用 `unnamed_addr`/`local_unnamed_addr`、可合并链接或可丢身份的优化属性。源 const 由 Sema/LValue QualType 保留，不能写；volatile 不通过 LLVM GV 的伪 volatile 标记表达。v1 记录无 cv 字段，不擅增字段；首版保守 consumer 不需要区分 cv wire format。
3. 先让 Sema/常量求值按源语言完整类型计算初始化表达式，再执行 **value != 0 → i1 → zero-extend i8**。`0→0`、`1/2/-1/最高位→1`；不截低位判断，不把 i8 2 交给 AsmPrinter 再归一。无显式 initializer 的静态对象为 0；多声明符各自求值、各自发一条定义。常量逻辑表达式按折叠结果归一，非编译期常量静态 initializer 保留 `err_init_element_not_constant`。
4. 所有**应发射的持久定义**（包括未使用 internal/const/volatile static、纯 global TU、可达定义函数内的 static）强制进入模块及 `llvm.used`，同一规范声明身份只登记一次。不能等普通 deferred-global 引用触发再决定是否生成，否则未用定义先消失。未发射的函数中的 local static 不单独建立运行存在性承诺；一旦函数定义进入本次发射集合，其静态对象按本规则登记，优化不能再删记录。
5. `llvm.used` 为 **appending、AS0、[N x ptr] 常量数组 initializer、section="llvm.metadata"**，元素直接 `ptr @handle`；根本身不生成普通存储或普通使用。用既有合并保活机制追加/去重，不能覆盖 ISR/其他既有 entries。本片只生产 llvm.used，不改用仅 llvm.compiler.used。reader 原本允许后者的规则不变。
6. extern-only 纯声明可以不物化；如物化则为带属性的 external i8 GV，**无 initializer/无定义记录**。不得为未使用的纯 extern 追加 llvm.used 制造虚假的必须解析引用；实际访问直接产生 BITADDR8 即可保留所需外部身份。
7. keepalive 常量路径准确对齐 ContractCheck `isMCS251BitKeepaliveRoot` 与 `isFullyKeepaliveConstant`（`R/llvm/lib/Target/MCS251/MCS251ContractCheck.cpp:493–546,599–607`）。合法根条件是名字集合+appending+array-of-pointers initializer+llvm.metadata；并非只认名字。生产者不用 cast；reader 既有 registration cast 例外不变，也不得外推为普通 cast 合法。
8. 结构有效但 keepalive 与普通 entries 混合时，须保留各自原有门禁；不能为了 bit 绕过其他 global/ISR 验证。无 bit 的原模块不得被额外塞入 bit 属性/元数据。

ELF 发射逐字节沿用 v1：每个定义 `01 01 INIT 01 00 00 00 00`，非 ALLOC PROGBITS `.mcs251.bit`、align4、entsize0，一对象至多一节；定义符号 STT_OBJECT size1、value 为记录偏移。**size1 是冻结符号描述，不是 1B DSEG 存储**。每条 +4 一条零 addend `R_MCS251_BIT_REF`，使用处零填字段加 `R_MCS251_BITADDR8` 指向精确符号；不 section+addend 折叠、不新增记录 kind。extern 的 use 不是 kind-2；kind-2 专指已注册 fixed reference。REL/汇编文本不能完整承载身份记录，维持 ELF-only 的明确拒绝。

### 2.2 extern、同 TU、跨 TU 合并 [D]

- 同 TU 用 `VarDecl::getCanonicalDecl()` 作为对象身份；extern→定义、重复 tentative 合并为一 GV、一条记录、一位。声明合并按 C 的类型/linkage/cv 规则，不按 typedef 名字或最终 i8 等宽合并。
- 生产使用 `-fno-common`；`-fcommon` 下**实际会成为 COMMON 的 tentative bit** 在 Clang 诊断，不偷偷改为 external 强定义来掩盖用户选项。显式初始化强定义、internal static 不因出现 `-fcommon` 一概误拒。
- 跨 TU extern 与一个强定义解析为一位；两个 TU 的同名 internal/local static 不合并；重定义、未定义的实际引用、已知 bit 引用指向 byte 定义硬错。两个 TU 的 tentative 在 `-fno-common` 下是两个强定义，不提供跨 TU COMMON 合并。
- 同 TU 已有类型兼容诊断捕获 bit/u8 冲突。P-4 未完成前，外部函数 bit/u8 **同为 i8 的签名冲突不能指望 lld 自动识别**；签名纪律是约束不是检测能力，见 §5。
- lld 负责位槽和 backing 唯一 owner，Clang 不分配位号。128 位扣除预留/固定 owner/legacy BIT_BANK 后耗尽硬错，无 byte fallback；不做 overlay、LTO/GC/ICF/archive 新支持。

### 2.3 ControlledBitChecker 身份域与强制表适用范围 [D]

应把“Fixed”命名重构为能表达 tagged identity 的语义助手，Sema 与 CodeGen 使用同一规范（不要求跨库共享源码）。

令 `strip(E)` 仅穿透 ParenExpr、ImplicitCastExpr、已解决 `_Generic` 的 selected expression、`__builtin_choose_expr` chosen expression；不因宏文本、变量名或优化器 CSE 推断身份，不把 C 显式值 cast、条件表达式、逗号或 statement-expression 的结果假称引用身份。

```
Identity(E) = Fixed(A)
  若 strip(E) 是经认证的固定 builtin 或携带 MCS251BitAddressAttr 的 VarDecl 引用；
否则 Identity(E) = Object(VD.getCanonicalDecl())
  若 strip(E) 是 DeclRefExpr，decl 为 VarDecl（含 ParmVarDecl），
  且 VD 的 canonical unqualified type 是 MCS251Bit；
否则 Identity(E) = None。

RequiresPhysicalBitRules(E) =
  Identity(E) 为 Fixed
  或 Identity(E) 为 Object(VD) 且 VD 具有 static storage duration。
```

固定分支**优先**于 bit VarDecl 分支，所以两个不同 sbit 名同地址仍别名；Fixed 与 Object 永不仅因数字或未来链接位置相等而等同。Object 身份只以 canonical VarDecl 相等为准；同名遮蔽的两个变量不相同，typedef/cv 不丢身份。`hasGlobalStorage` 判定需排除 TLS 及未支持存储形态（先由声明门禁拒绝）；block-scope extern 是持久对象，不误分自动。ParmVarDecl/普通 auto/register 是身份域成员，但 `RequiresPhysicalBitRules=false`。

**精确接入要求**：`asFixedBitAssignment` 改为按 RequiresPhysicalBitRules 选择；inc/dec 入口同样分流；同位检测使用 tagged equality；`findRead` 检测同一受控目标的已求值值读取。不能只把 `isMCS251ControlledBitLValue` 改成“所有 bit VarDecl 为真”而让现有调用站点无差别应用 RMW 拒绝。赋值 LHS 仅写并非读；数组下标/指针计算中读 B 则计入 RHS 自读。全部 RMW/用值判定仍由现有 full-expression 求值框架作出，不依赖 O2。

### 2.4 固定位与持久对象共用强制表 [D]

以下 B 为 RequiresPhysicalBitRules=true 的可修改左值；const 写先受普通 C 约束诊断。

| 源操作 | IR/机器保证或拒绝 |
|---|---|
| `B=0/1/常量表达式` | 归一后一次 clear/set；无 read |
| `B=independent_dynamic()` | RHS 一次，实际路径一次 set 或 clear；不走 MOV bit,C |
| `r=(B=independent)` | 复用 RHS 归一值，目标不补读 |
| 读 B、比较/逻辑/下标/算术提升 | 每次已求值源读取一次 read；i1 作为普通值参与当前 int 模型提升 |
| 直接 `if(B)`/`if(!B)` | 恰一次 JB/JNB 测试；长跳 trampoline 不加采样；条件专用选择路径须联验，不能把 read 物化再测当作该行完成 |
| `B^=1` 或 `B=!B`，结果丢弃 | 精确同身份识别、一次 toggle→CPL，**识别发生在发 read 之前** |
| 上述 CPL 结果被使用 | `err_mcs251_bit_lvalue_result_used`；不得 CPL 后读回 |
| `B=other`/`B=!other`，不同身份 | 源一次读取、目的一次写，不是原子复制，不误作 CPL |
| `B=~B`、`B=B+1`、`B=B`、其他已求值自读赋值 | `err_mcs251_bit_rmw_unsupported`；`~` 是提升后的按位取反，不能改为 `!` |
| `B+=…`、`B|=…`、`B^=0/2/变量/宽常量低位1`、++/-- | `err_mcs251_bit_rmw_unsupported`；`^=1` 比较完整 APSInt，不先截断 |
| `if(B) B=0` | 一次测试、路径上的一次 clear；不自动合并 JBC |
| 显式采样临时值，再独立写 B | 允许，明确多操作非事务原子；不推导跨语句自读等价性 |

自动局部/形参副本的 `b=~b`、`b+=x`、++/--、消费 `b^=1` 的值按正常目标整数/布尔值规则；不保证 CPL，也不能报上述物理位错误。其源指针/数组/布局/原子禁令仍不变。

### 2.5 求值感知测试规范（对象版必须全量覆盖）[D]

沿用 `R/clang/lib/Sema/SemaMCS251.cpp:805–1092,1175–1445` 的双遍历语义：checkExpr/checkStmt 负责用值；findRead/findReadInStmt 负责自读。两者选分支必须相同。

| 对象 B 的用例/上下文 | 预期 |
|---|---|
| `B = 0 && B; B = 1 || B; B = 1 ? 0 : B;` | 正例；死 RHS 不计自读 |
| `B = c && B; B = c ? 0 : B;` | c 未知，存在被求值自读，RMW 错误 |
| `0 && (B^=1); 1 || (B^=1); if(0){B=~B;}` | 物理 RMW 检查不报错、运行零访问；死分支不是类型/取址禁令豁免 |
| `c && (B^=1); int r=(B^=1); return (B=!B);` | CPL 结果 Used，result_used 错误 |
| `(void)(B^=1); (B^=1,0); for(;c;B^=1){}` | Discarded 正例，真实迭代路径每次一次 CPL |
| `({ B^=1; });` vs `int r=({ B^=1; });` | 前者正例；后者末表达式 Used，result_used |
| `int r=({ B^=1; 0; });` | toggle 非末表达式，Discarded，正例 |
| `B=({ if(0) use(B); 1; });` vs `B=({ if(c) use(B); 1; });` | 前者正例；后者 RMW |
| `B=({ B=0; 1; });` | 嵌套写不是读；两次写按源码保留，不偷报自读，也不宣称整表达式一次写 |
| `_Generic` controlling/unselected、choose 未选项、sizeof 普通整数表达式、constant_p/classify_type | 沿既有 unevaluated 规则；对 `sizeof(B)` 本身的类型门禁仍报 layout_query |
| dynamic_object_size 含被求值 B 运算 | 沿既有动态求值例外，不误当普通 object_size 全跳过 |
| statement expression 的 decl initializer、do/while、return、switch、标签 | 逐条沿现有遍历；do body 至少一次，for 恒假可跳 body/inc，不跳 init/condition |
| asm 输入/`+r`、输出地址计算读取 B | RHS 自读扫描计入；纯 `=r` 裸输出不是读取。此为 scanner 覆盖，不授权新的 inline-asm bit 存储接口 |
| OMP/ACC 构造、clauses、capture、未求值 bit 类型 | 继续由 P08 Option B 独立构造门禁拒绝，不用死分支规则绕过 |

每个 RMW 正/负用例至少实例化 fixed、extern/global、file static、local static；自动/形参实例化对照以证明不被误套表。canonical redecl 与同名遮蔽、不同对象相同初值、sbit 同地址别名、Fixed/Object 混合均须测试。

### 2.6 DeclRef→LValue→符号 consumer 路由 [D]

1. 在 `EmitDeclRefLValue` bit 分支前先处理带 MCS251BitAddressAttr 的固定引用，沿现有 `Symbolic=false`/volatile 路径。
2. 持久 bit DeclRef 取得 §2.1 的唯一句柄 GV，构造 `LValue::MakeMCS251Bit(GV, /*Symbolic=*/true, E->getType())`；传入保留声明限定的 QualType，**不调用 getVolatileType 给普通对象加 volatile**。该 LValue 的普通 Address 为 invalid，不能流向普通 EmitLoad/Store 地址路线。
3. 自动局部/形参则取 LocalDeclMap/普通调用私有 LValue，交 P-2，不构造 Symbolic bit LValue。
4. `EmitMCS251BitAddressOperand` 按 Symbolic 分流：固定返回 i32 ConstantInt；符号返回原 GV `ptr`，绝不 CreateIntCast/ptrtoint，也不按 bit0 计算地址。
5. read/set/clear/toggle 所有选 intrinsic 的站点按 Symbolic 选择 §1 对应 ID；动态 store 两分支也必须选 obj 族。通用 LValue load/store 现有特殊 kind 分派复用。
6. `tryEmitMCS251BitToggle` 的比较同步改为 tagged identity：两个 symbolic 直接 GV 同一身份（规范声明映射），两个 fixed 比完整合法地址；不得继续只 dyn_cast ConstantInt，也不得只比较 LLVM Value 地址而忽略 Symbolic tag。`B=!other` 必须落普通源读取+目的写。
7. 两族后端共用获准指令/链/PSW/分支松弛机制。obj 写以 TargetGlobalAddress（零 offset、直接受验证 GV）送已有 symbolic bit 机器操作数；read 的 i1 type-legalizer 分支同样接受该形式，不让普通 GlobalAddress lowering 将句柄物化为 DR byte pointer。最终 BITADDR8 relocation 保留 symbol，不发普通地址 relocation。MIR 入口与 AsmPrinter 最终防线继续验证符号/offset/操作数字段。

---

## 3. P-2 自动局部、形参副本与 spill

### 3.1 调用私有对象 [D]

- 类型、cv、声明身份与持久 bit 相同，**存储路径不同**。auto/register bit 每次调用独立，不产 mcs251-bit-object、llvm.used、`.mcs251.bit` 或 BSEG 请求。
- O0 使用 align1、AS0 的 i8 alloca；O1/O2/O3/Os 可 mem2reg/SSA。每次有定义的源写入先在原宽度比较非零，再 zext i1→i8；读 i8→`icmp ne i8,0` 得 i1，不能读取未归一宽值后 trunc 得奇偶语义。合法流水中的优化可消除冗余比较，但原始 IR 测试须证明发射点正确。
- 非 volatile local 的算术、PHI、短路、副作用按普通值执行。volatile local 用普通 byte volatile load/store 保留源访问次数；不暗换成物理 CPL 原子保证。
- 无 initializer 的自动对象遵循 C 的未初始化值规则；**不强制自动清零**。“栈槽内容0/1”指已初始化/有定义的写后值，不对读取不定值作新保证。栈自动初始化/调试特殊模式不得引入对未初始化源程序的新运行承诺。
- 1B spill/reload 复用 GPR8/普通帧；无物理 packing、无 bit bank、无基于函数生命周期 overlay。相邻字节哨兵不变；调试地址不能成为源取址通道。

### 3.2 形参入口与存活跨调用 [D]

P-2 提供副本机制；**真实参数入口开门以 P-3 的 i8 ABI 就绪为条件**，不能提前接收旧 i1 签名。入口取得 DPL 或静态槽的 i8 值，尽早比较非零形成调用私有 i1，需存储时 zext 成规范化 i8 alloca。读取必须在可能覆盖静态槽的 helper 调用之前；之后的源 ParmVarDecl 读写只访问私有副本，不重新读写 `_callee_PARM_n`。

外部 ABI 合规输入只能为 0/1；入口 `icmp ne` 是统一解码实现，不是承诺与发送垃圾高位的第三方 ABI 兼容。单参递归/嵌套验证可测副本独立；多参静态槽的异步重入仍为用户责任，不能用“入口早读”推导整个传参事务重入安全。

ISR 中自动值由既有寄存器现场/该次帧保护；**全局/static 位 RAM 不加入 37B 保存恢复**，ISR 修改共享 volatile bit 返回后必须可见、不回滚。

---

## 4. P-3 值 ABI：拍板 P01 照实现

### 4.1 位置与类型契约 [D，沿用已裁定 P01]

| 位置 | 准确规则 |
|---|---|
| 第一**源参数**为 bit | 实际 LLVM i8 参数，完整 DPL=0/1，高7位为零 |
| 第一源参数非 bit | 普通类型现有 DPL/DPH/B/A 分配完全不变，不寻找“第一个 bit”占 DPL |
| 第二及以后源参数为 bit | `_callee_PARM_n`，n 是从1起算的原源参数位置；每个1B、0/1 |
| 返回 bit | 实际 LLVM i8 返回，完整 DPL=0/1，不以 carry/B/A 返回 |
| 多 bit | 各占原位置，不打包，无8个参数上限；测9及更多 |
| 函数内部 | 逻辑 i1，形参私有副本见 P-2 |
| 函数指针 | 已支持普通单参数间接调用可含 bit 值；零参数 bit 返回同样按 i8；多参间接继续拒绝 |

没有新的 bit LLVM calling convention。普通 C/Fast 物理 CC 不改；static slots 的 leaf OSEG/non-leaf DSEG 规则不改。整数混合参数可覆盖 i8/i16/i32；指针混合正例把指针放在**已有支持的位置**（例如第一参数指针、后续 bit）。bit 第一参数+第二指针在未批准后续指针 ABI 的模式中是既有负例，不为凑 T7 正例而放行。

### 4.2 Clang ABI 分类及归一化发射点 [D]

依 BIT-TASK-BREAKDOWN BT10，建立最小 MCS251 target ABI 分类接入（目标 `TargetCodeGenInfo`/`ABIInfo`，新文件建议 `R/clang/lib/CodeGen/Targets/MCS251.cpp`，并接实际分派与 CMake）。**非 bit 原封委托 DefaultABIInfo**，不得修改全目标 DefaultABIInfo 或改变 `_Bool`。

bit 参数/返回的 ABIArgInfo 为显式 direct i8 coercion（不是 i1+zeroext、不是 signext i1、不是 byval/间接对象）。声明、定义、直接调用、函数指针类型与间接调用共同使用这一分类。ABIArgInfo 只解决签名，以下值转换必须在 bit 专属 marshal/unmarshal 路径落实，不依赖通用 coercion 的 byte store/load 侥幸正确。

| 发射点 | 必须产生的逻辑转换 |
|---|---|
| 整数赋 bit、局部初始化、形参实参的源转换 | 原宽度 `icmp ne T value,0` 得 i1；已有 CK_IntegralToBoolean 产 i1 则不重复求值 |
| caller 参数进入 ABIArgInfo i8 boundary | `zext i1 normalized to i8`；在写 DPL/静态槽前完成；不先 trunc 宽整数到 i8/i1 |
| callee 参数入口 | 收完整 i8，一次 `icmp ne i8 incoming,0` 得私有 i1；需 byte 存储再 zext |
| return 表达式 | 对源返回表达式完整求值一次并非零归一；`zext i1 to i8` 后才 `ret i8`/拷 DPL |
| caller 使用 bit 返回 | call i8，结果比较非零得 i1；丢弃返回仍必须 call i8，不退回旧间接门 |
| bit→普通整数 | i1 零扩展到当前 int 或目标整型；不得 sign-extend |

承重 IR 形态（动态 i32 输入）：

```llvm
%nz = icmp ne i32 %x, 0
%abi = zext i1 %nz to i8
call void @callee(i8 %abi)
```

这个顺序使 2、0x80、0x100、0x80000000、-1 都为1。先 trunc 得低位再 zext 是错编译。正式测试同时检查**未优化 IR**的实际 i8 签名、优化 IR 的等价值、机器 DPL 与每个后槽的完整字节。

### 4.3 DPL 高7位零哨兵 [D/P]

独立 MIR/已验证字节观察 harness 在调用前把 DPL 污染为 FE/A5/FF，分别传0/1/2/-1/最高位，经 noinline、跨 TU 的真实 Clang caller/callee：

- callee 在任何 bit→i1 解码**之前**保存完整 DPL 到普通可观察 byte，必须精确为00/01；不能由 callee 的 bool 解码掩盖 caller 高位错误。
- bit 返回的 callee 在独立压力路径污染寄存器后返回0/1；caller 在任何 bool 解码**之前**捕获完整 DPL，必须00/01。
- 后续槽同样在入口解码前观察完整1B；`_PARM_2/_PARM_4` 的邻 byte 哨兵不变，槽不被重编号为“第几个 bit”。
- caller/callee O0↔Os、O0↔O2 交叉档；普通 i8 返回任意0…255与普通 i16/i32 ABI golden 不变。

不得用受测 bit intrinsic 给哨兵初始化再用同一指令作唯一 oracle，也不以读取 DPL bit0 作为通过标准。

### 4.4 主门迁移台账 [S/D]

| 既有站点（R 下） | 卡与替代路线 |
|---|---|
| `clang/lib/CodeGen/CGDecl.cpp:219–223` | P-1b static→句柄；P-2 auto→私有值；固定声明仍非普通对象；排除存储形态先诊断 |
| `clang/lib/CodeGen/CodeGenModule.cpp:6534–6538` | P-1b bit 定义专用发射；同时覆盖非 tentative 的早期/延迟发射入口，不能只改尾部 global-definition 门 |
| `clang/lib/CodeGen/CGExpr.cpp:3645–3652` | P-1b/P-2 按固定→持久→自动三路；DeclRef 不得 fallback byte global |
| `clang/lib/CodeGen/CGDecl.cpp:2702–2703` | P-2+P-3 i8 入口私有副本 |
| `clang/lib/CodeGen/CGCall.cpp:5289–5290` | P-3 参数归一，先检查 varargs/no-prototype 排除集 |
| `clang/lib/CodeGen/CodeGenModule.cpp:5629–5630` | P-3 返回签名/值；覆盖仅声明、定义 |
| `clang/lib/CodeGen/CGExpr.cpp:6543–6551` | P-3 间接 bit 返回调用；不能遗漏 |
| `clang/lib/CodeGen/CodeGenModule.cpp:4742–4743` | alias **保留拒绝**，不迁移为普通存储 |
| `clang/lib/CodeGen/CGExpr.cpp:6029–6030`；`clang/lib/CodeGen/CGBlocks.cpp:798–799` | compound literal/block capture 保留 fail-closed，本片不授权扩形態 |

七门来自同一个 `err_codegen_unsupported`（`R/clang/lib/CodeGen/CodeGenModule.cpp:2160–2179`）；支持门迁移必须把对应旧负测改为**有结构正断言的正测**，alias 等未支持项保留独立负测。不整文件删除 `mcs251-bit-unsupported-gates.c`。

---

## 5. P-4 ABI 签名记录、发布约束与 CRT 边界

### 5.1 P-4 交付范围与前置冻结 [D/P]

BIT-TASK-BREAKDOWN §2.5 要求版本化的源参数 bit 位置/返回身份核对；拍板 P07 不允许靠 i8 同宽冒充源签名兼容。现有 `.mcs251.bit` v1 只有位定义/固定引用，**不是函数 ABI 签名记录**，不能挤入其 kind/init/capabilities 或重解释 BIT_REF。

P-4 完成必须包括：Clang 从未丢源类型的 CGFunctionInfo/FunctionDecl 提取签名；声明/定义/直接引用一致覆盖；持久化的版本化记录 writer；ELF reader 严格验证与跨 TU 核对；独立 golden/mutation 与真实两 TU 测试。逻辑信息最少为协议版本、记录长度/保留位、函数精确符号关联及角色、原源参数数目及每位置 bit/非 bit 身份、返回 bit/非 bit、与已有调用约定/内存 ABI 身份的关联。普通无 bit 定义在参与已知签名核对时也需能表达“已知 byte”，否则不能检查 bit/byte 冲突。

**线格式不在本片凭空宣布已冻结**：公开头、节名、字段字节、relocation/能力编号、未知字段策略必须由 P-4 属主按 P07 单独做协议冻结和 Alice/PM 登记，writer/reader 编码前必须完成；不得由执行者随选数字。此为既定工程协议流程，不再提出“要不要做签名核对”的用户选择。本片 P-0 冻结的是四个 consumer，与 P-4 物理线格式不互为隐含依赖。

P-4 验证规则：同名已知 bit/byte 或参数位置/返回身份冲突硬错；缺记录的第三方不推定兼容；合法普通无 bit 对象保持原协议兼容；未知版本/节/能力/reloc 旧 reader 明确拒绝。若实际证明必须升级一般 ABI 身份，按 P07 转 PM 另案，不偷升 ELF v2。

### 5.2 是否可后置 [待 PM 决定时机，推荐同期]

**推荐 P-4 随 P-3 集成，正式发布前完成**，以满足已冻结跨 TU 身份验收。P-1…P-3 可以在 P-4 完成前进行受控开发和 X5 编译档复测。

若 PM 选择先交开发子集，必须显式登记为对 BT10/整体 DoD 的**阶段性延期，不是规则撤销**：所有 TU 同一快照 clang、同配置、共用经审计原型；禁止第三方/跨版本 bit ABI 混链；已知工具同版不自动证明原型一致；结果只能写“bit CodeGen/i8 ABI 开发子集完成，跨 TU 签名发布门未完成”。不得把口头“同版约束”勾为机器签名检查 PASS，P-4 保持 OPEN。

### 5.3 CRT 当前可验边界

`R/lld/MCS251/BIT-OBJECT-CONTRACT.md` §2/§7 既有路径是 owned 16B 全清零后 walker 应用非零 XINIT，零值来自真实启动清零，不来自 NOBITS/QEMU 默认值。`R/validation/mcs251-elf/runtime/` 本轮目录核对无 `crt-bit.yaml`。

- 本片 C→ELF/模型初始化可先验该已批准 **S1 owned 全清零子集**：RAM 先污染 A5/5A，main 前自有位零/非零初值正确；池只预留一次，不覆盖普通 RAM 分配。
- 外部 owner 邻位保留、mask=0 无访问/部分 mask 保留邻位、新 `.mcs251.bitprofile` 真实 CRT 仍属 BT14；不能用现有 bit-init.test 的成功代替它们。
- 新 profile 旧 CRT 互斥仍遵循 P06；不把“允许 S1 子集先验”解释为旧 CRT 与所有新协议任意混用。缺 walker、缺 XINIT 区、冲突输入 XINIT 等继续负测。
- 本片不自动开启 EA/清外设 pending，不改 ISR 37B，不宣称 DMA/NMI/C11 原子语义；模型与每块实板独立记状态（P10）。

---

## 6. 测试矩阵定稿（规范预期，不是执行报告）

### 6.1 公共运行和断言纪律

后续由 PM 指定仓库外构建/输出目录，同快照 clang/llc/opt/llvm-as/dis/llvm-readobj/llvm-objcopy/yaml2obj/FileCheck/lld；记录绝对工具路径、输入/工具 sha256、命令/退出码与 NOT_RUN。使用已批准 memory contract `1,2,32,8,1` 作主产品矩阵；专测既有兼容入口可用 `1,1,32,8,1`。AS0=16 等尚未获产品支持的模型保留错误，不因本片新增 p5 或绕过对象身份。

1. Sema 文案用 `%clang_cc1 -triple mcs251-unknown-none -std=c11 -fsyntax-only -verify`；GNU statement expression 用既有扩展模式。正例 `expected-no-diagnostics`；Clang CodeGen 正例输出未优化 IR 后 FileCheck，机器/ELF 另验。
2. **`-verify` 仅匹配诊断文字，不直接匹配 diag::ID。** 为满足 ID 级验收，新增测试侧 `MCS251BitDiagnosticTest`（建议位于 `R/clang/unittests/Sema/MCS251BitDiagnosticTest.cpp`，并登记到 lit 可运行的单元测试套件）：DiagnosticConsumer 采集 `Diagnostic::getID()`（API 锚点 `R/clang/include/clang/Basic/Diagnostic.h:1642`），按 fixture 标记的主错误位置与编译阶段，直接 `EXPECT_EQ` 对应 `diag::err_*` 枚举。不要固定会随 TableGen 漂移的数字 ID，不新增虚构的 `-verify-diag-id` CLI。文案 lit 与 ID 测试共用/对应同一编号的最小源 fixture；CodeGen 门用执行到 EmitLLVM 的 frontend action，不能只 run Sema。
3. 本节表中 `V(ID, text)` 表示**两项必做**：lit 注释 `expected-error {{text}}`（含确切源位置/次数）和测试 consumer 的 `EXPECT_EQ(diag::ID, ActualID)`；不是一条现存命令。一个 fixture 一种主要错误，标准级联错误显式列出，不能用 `-verify-ignore-unexpected` 掩盖。
4. 新拒绝项缺专用 diag 时本设计冻结下列 ID/文案，由 P-1b/P-3 目标 Sema 门新增：`err_mcs251_bit_storage_unsupported`（`MCS251 bit storage form '%0' is not supported`）、`err_mcs251_bit_call_unsupported`（`MCS251 bit call form '%0' is not supported`）、`err_mcs251_bit_conversion_unsupported`（`conversion between MCS251 'bit' and %0 is not supported`）。它们是 **[D/P] 新增 ID，不是现存证据**；参数分别使用下表固定类别。非 bit 不触发目标新门。
5. IR 负例为 `not --crash llc ... -disable-verify -filetype=obj -mcs251-object-format=elf -o %t/bad.o ... 2>&1 | FileCheck ...`（fatal-error 路径沿当前测试）；并独立断言没有新可用对象。通用 verifier 开启版也要跑，结构坏 IR 在 LLVM verifier 先报错与关闭 verifier 后目标主体分两个 check-prefix。
6. 对“即使优化可删除也非法”的 IR，在优化前目标入口拒绝；优化后合法 IR 再做目标验证。不能先 opt 删除错误再说后端接受是成功拒绝证明。`opt` 本身未安装目标 pass 时不虚构 pass 名，P-1a 必须接可测试入口或复用目标校验测试 harness，并保留直接 llc 覆盖。
7. 字节正例经 ELF/MIR→对象而非不存在的 AsmParser。符号句柄对象的 asm/REL **是负例**，不能要求其汇编文本正例来绕过记录通道。CHECK-NOT 只能补充正结构或明确失败断言，不能单独证明功能。

承重的断言书写示例（未来测试，非本轮执行）：

```c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fsyntax-only -verify %s
typedef __bit BOOL;
extern BOOL B;
void bad(void) {
  (void)&B; // expected-error {{cannot take the address of an MCS251 'bit' object}}
}
```

对应 ID 单元用例对该标记位置收集的错误执行 `EXPECT_EQ(diag::err_mcs251_bit_address_of, ActualID)`，并断言主错误数量为1。CodeGen正例的指令级断言必须分函数范围，例如 `CHECK-LABEL: define ... @toggle` 后 `CHECK: call void @llvm.mcs251.bit.obj.toggle(ptr @B)`，同时对该函数范围设 `CHECK-NOT: @llvm.mcs251.bit.obj.read`；不能让模块里另一个函数的read误伤负断言。IR坏签名使用独立 split-file片段：

```llvm
; RUN: not --crash llc -mtriple=mcs251 -disable-verify -filetype=obj -mcs251-object-format=elf %t/bad-signature.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=SIG
; SIG: MCS251 contract violation: MCS251 symbolic bit intrinsic: invalid declaration or call signature
; bad-signature.ll 的最小主体（经 split-file 提取）
declare i8 @llvm.mcs251.bit.obj.read(ptr)
```

此例刻意只有未用坏声明，证明检查不依赖函数体/句柄Use。另跑开启通用verifier的前置错误版本；目标诊断不是通用verifier诊断的替代。

### 6.2 正例轴与 T1–T11 定稿

**轴必须实化为格点，不能仅列枚举**：四拼写 `__bit`、`bit`+Keil、`typedef bit BOOL`+Keil、`typedef __bit BOOL`；存储 global/file-static/local-static/auto/param；cv 无/const/volatile/const volatile；优化 O0/O1/O2/O3/Os；int16/int32。非法组合进入 §6.3，不能从生成式矩阵默默删掉。

每个合法格点提供 AST 独立类型/cv/存储身份、零诊断、未优化 IR 承载与访问路线的预期。深层 E2E 至少覆盖每种存储×优化×int 模型；拼写等价性在前端矩阵验证后可共用相同 golden，不得用单一 `BOOL` 例子替代四拼写覆盖。

| ID | 最小输入/补充维度 | 必须写入 lit/字节/运行断言 |
|---|---|---|
| T1 | 四拼写×global/static/auto/param/extern；具名/无名形参 | AST MCS251Bit；持久定义 `CHECK: global i8 ... #` + 属性 + llvm.used；auto `alloca i8`/SSA，无 bit 记录；仅 extern 不发空节；PCH 往返后同一路线 |
| T2 | 静态初始化0、1、2、-1、最高位、可折叠 `x&&y`（x/y 为 ICE） | IR i8 0/1；record hex `01010001`/`01010101`，不能只看打印初值；无 ordinary DSEG/CSEG payload |
| T3 | 多声明符 a=0,b=1,c=2，重复 tentative/extern | 每个规范对象一 GV、一条8B记录、一 BIT_REF；c 初值1；keepalive 去重 |
| T4 | 两真实 C TU，extern+强定义、同名 internal、global-only、unused static | llvm-readobj 精确 STT_OBJECT/record offset；extern use BITADDR8、无 kind2；lld map 单槽/不同 local 槽；交换 TU 顺序运行值不变、同序字节重现 |
| T5 | 常量/动态带计数副作用/另一 bit/赋值结果/`!` toggle | IR `CHECK-COUNT-1` 对单基本块采样；动态 CFG 两臂各一写、路径计数一次；toggle 有一次 obj.toggle 且无 obj.read；赋值结果不补读；ELF relocation/机器位指令数量共同确认 |
| T6 | if/!if/while/for/比较/&&/||/下标/算术提升 | 短路分支 count oracle；bit→int zext，int16/32 真值表；直接条件 MIR JB/JNB、长跳一次测试、无 JBC；不得用多次 read 的 IR 绕过 |
| T7 | 首参/后槽/无名/混合/9及更多 bit/指针首参 | `define/call` 真 i8 签名；`_PARM_2/_PARM_4/_PARM_9` 原序号；每槽1B；DPL和槽完整00/01高位哨兵；后续指针未支持的组合转负例 |
| T8 | bit 返回0/1/动态整数/持久 B/auto b，直接及间接 | `ret i8`、`call i8`，未出现 i1 boundary；返回 B 恰一次 read；返回寄存器 DPL全字节，不以 carry 传值 |
| T9 | 单 bit 参数函数指针、无参数 bit 返回指针，noinline/跨 TU | 真实间接 call i8 参数/返回，与 direct 同 ABI；函数指针不是 bit 对象指针；多参间接负例另钉 |
| T10 | O0…Os×int16/32×fixed/object×volatile/非volatile；寄存器压力/helper/PHI | 优化前后动态 trace 一致；opt instcombine/GVN/LICM/DCE/inline 独立管线；与 byte load/store/call 排序不变；unused read 保守保留；auto可SSA，volatile auto byte次数；spill无BSEG、邻byte哨兵 |
| T11 | ISR置共享volatile对象、主循环观察；ISR/主程序各auto | 污染RAM后初值正确；ISR更新不回滚、37B保存不含位RAM；局部副本/跨调用值正确；模型与硬件分开状态，非volatile共享不声称可见 |

拟新增/扩展测试路径：`R/clang/test/CodeGen/mcs251-bit-objects.c`、`mcs251-bit-local.c`、`mcs251-bit-abi.c`、`mcs251-bit-conversions.c`；`R/clang/test/Sema/mcs251-bit-rmw.c` 对象扩展；`R/llvm/test/CodeGen/MCS251/bit-intrinsics-obj.ll`、`bit-effects-obj.ll`、`bit-abi.ll`、`bit-spill.ll`；真实 C 两 TU/e2e 位于 `R/validation/mcs251-bit/`。本轮不创建这些文件。

### 6.3 拒绝表逐条落到诊断 ID 与 lit 形态

现存 bit ID/文案已核 `R/clang/include/clang/Basic/DiagnosticSemaKinds.td:12727–12740,12766–12795`；旧 CodeGen 门为 `err_codegen_unsupported`，文案模板已核 `R/clang/include/clang/Basic/DiagnosticFrontendKinds.td:525`。

| N | fixture 形态（typedef/typeof 变体同测） | `V(ID, text)` 主断言与归属 |
|---|---|---|
| N01 | `&B`，global/local/param/static；demo37两处最小化 | `err_mcs251_bit_address_of` / `cannot take the address of an MCS251 'bit' object`；Sema |
| N02 | `BOOL *p`、`(BOOL*)0`、pointer 参数/返回、typeof链 | `err_mcs251_bit_pointer` / `cannot form a pointer to MCS251 'bit' type` |
| N03 | bit数组、参数数组衰变 | `err_mcs251_bit_array` / `array of MCS251 'bit' is not allowed` |
| N04 | struct/union字段、bit-field | `err_mcs251_bit_field` / `MCS251 'bit' is not allowed in a struct or union field` |
| N05 | `sizeof(B)`/`sizeof(BOOL)`/`_Alignof(BOOL)` | `err_mcs251_bit_layout_query` / `sizeof ... is not allowed`、`alignof ... is not allowed` 分fixture |
| N06 | `__builtin_offsetof(BOOL, member)` | **标准** `err_offsetof_record_type` / `offsetof requires struct, union, or class type`；offsetof 不能直接对 scalar bit 作布局查询；含 bit 字段的 struct 在N04已拒，不伪称它一定触发 layout_query |
| N07 | `_Atomic(BOOL)`/atomic限定形式 | `err_mcs251_bit_atomic` / `atomic MCS251 'bit' type is not allowed` |
| N08 | `__atomic_load_n(&B,0)`、`__atomic_store_n(&B,1,0)`、`__sync_fetch_and_or(&B,1)` | 主错 `err_mcs251_bit_address_of`；另立直接值形态 `__atomic_load_n(B,0)`、`__sync_fetch_and_or(B,1)`，两者精确钉标准 `err_atomic_builtin_must_be_pointer` / `address argument to atomic builtin must be a pointer`（现存分派 `R/clang/lib/Sema/SemaChecking.cpp:5274–5285,5698–5714`）；不能要求所有原子族都报 bit_atomic |
| N09 | memcpy/memmove(&B,&C,1) | 两处 `err_mcs251_bit_address_of`，`expected-error` 次数2；另立标准原型 memcpy(B,p,1) 的直接bit值→pointer转换fixture，按N19新 `err_mcs251_bit_conversion_unsupported` 拒绝；不把bit存储降成byte copy |
| N10 | `bit xdata f_z` 两处独立语料fixture；`__bit __xdata`及数值AS1/3/4/5/6 | `err_mcs251_bit_addrspace` / `MCS251 'bit' cannot be declared in a non-default address space`；明确拒绝而非忽略placement |
| N11 | global/static `B=~B; B=B+1; B=B; B+=x; ++B; --B; B^=2` | `err_mcs251_bit_rmw_unsupported`，文本含精确运算符；含 §2.5 活/死分支对象矩阵；auto/param为正对照 |
| N12 | `int x=(B^=1)`、return/condition/subscript/statement-expression末值使用 | `err_mcs251_bit_lvalue_result_used` / `the result of a controlled MCS251 bit toggle cannot be used` |
| N13 | bit实参传ellipsis；含bit签名的variadic声明/定义/调用 | 新 `err_mcs251_bit_call_unsupported`，`%0=variadic`；在默认promotion把bit身份抹掉之前检查源实参，不影响普通variadic既有门 |
| N14 | 无原型调用传bit、无原型bit返回函数调用/旧式定义 | 新 `err_mcs251_bit_call_unsupported`，`%0=no-prototype`；使用C11 fixture，不让C23“()即void”掩盖路径；普通无bit按旧规则 |
| N15 | 多参间接调用，签名含bit（并钉noinline） | 新 `err_mcs251_bit_call_unsupported`，`%0=multi-argument indirect`；LLVM层仍另验 `MCS251: multi-argument indirect calls are not supported` |
| N16 | 实际COMMON tentative（-fcommon）、weak/weakref、TLS、COMDAT或显式不支持section/placement | 新 `err_mcs251_bit_storage_unsupported`，`%0=COMMON/weak/TLS/COMDAT/section` 分fixture；后端结构门独立再验。支持的-fno-common/强定义不误拒 |
| N17 | alias/ifunc | bit alias 保留 `err_codegen_unsupported` / `cannot compile this MCS251 bit alias yet`；ifunc沿现存 `err_mcs251_ifunc_unsupported` / `'ifunc' attribute is not supported for MCS251 targets`；手写IR alias/ifunc逃逸另验 |
| N18 | C++裸bit/__bit/typedef拼写；非目标C拼写；MCS251不开Keil裸bit | 标准 `err_unknown_typename` / `unknown type name`；C++直接调用目标bit builtin另验 `err_mcs251_bit_cxx_unsupported`；另有合法标识符命名的host正例，不能抢占名字 |
| N19 | 浮点→bit/bit→浮点、指针→bit/bit→指针，显式及隐式 | 新 `err_mcs251_bit_conversion_unsupported`，`%0=floating-point type/pointer type`；赋值/实参/返回/显式cast均拒，合法整数转换不变。bit*构造先由N02拒绝；不把未来浮点扩展推定已开放 |
| N20 | OMP/ACC构造中对象/类型/捕获/死表达式 | `err_mcs251_bit_omp_construct`或`err_mcs251_bit_acc_construct` + `note_mcs251_bit_construct_here`；22入口原矩阵全回归 |
| N21 | 非constant静态initializer | `err_init_element_not_constant` / `initializer element is not a compile-time constant` |
| N22 | 动态固定位地址、负数/256/超宽值、受限SFR | `err_mcs251_bit_lvalue_not_constant`、`err_mcs251_bit_lvalue_range`；管理寄存器/EA既有安全能力负例维持，符号族不提供绕路 |
| N23 | bit/u8同TU冲突重声明/函数原型 | 变量 `extern __bit b; extern unsigned char b;` 精确钉 `err_redefinition_different_type` / `redefinition of`；函数 `void f(__bit); void f(unsigned char);` 精确钉 `err_conflicting_types` / `conflicting types for`；跨TU函数冲突归P-4而非伪称Sema可查 |
| N24 | bit compound literal、block capture/__block | 保留 `err_codegen_unsupported` 的 `MCS251 bit compound literal`、`MCS251 bit block capture`；__block存储本身在开auto门时须用新storage诊断（%0=__block）或保留专门CodeGen拒绝，不能开放byref byte逃逸；本设计选新storage诊断 |

N08/N23 已分别冻结具体 builtin/变量/函数 fixture 的标准诊断，不能合成“任意错误都算过”的测试；逐fixture通过 consumer 比对唯一主错误枚举，Alice逐项核对。现存标准错误作为拒绝原因不需要新增同义目标诊断。对 `&B` 后的恢复级联只按对应fixture精确登记，不用一个Error吞整组。新N19转换门只检查实际发生的类型转换，不抢占atomic custom-type-checking在转换前作出的N08指针参数错误。

### 6.4 IR/bitcode/MIR/链接层负例矩阵

| L | mutation（每项独立fixture） | 断言形态 |
|---|---|---|
| L01 | 四obj ID坏返回/参数/AS/数量/vararg/CC/定义；unused坏声明 | 普通verifier失败；`-disable-verify`命中 §1.4 `invalid declaration or call signature`，O0/O2/Os相同 |
| L02 | 伪同前缀/后缀函数、ordinary call、callee cast/alias/ifunc、invoke/callbr/tail、operand bundle（含参数0同时合法） | §1.4直呼/非白名单主体；没有标记GV的坏call也必须拒 |
| L03 | null/undef/poison/ordinaryGV/alloca/参数/select/PHI/GEP/cast句柄 | §1.4 direct AS0 global主体或既有instruction escape；cast一律不剥 |
| L04 | 句柄普通load/store/GEP/ptrtoint/icmp/ret、atomicrmw/cmpxchg/memcpy/lifetime/debug-address | 既有 `handle escape` / call主体；每个操作有自己的失败CHECK；合法obj消费不能覆盖这些Use |
| L05 | aggregate/cast共享：一支llvm.used，另一initializer/ret/call；伪根名、非appending/错section/非pointer数组 | `handle must not escape through a constant expression or initializer`；正例准确结构llvm.used往返后仍保留 |
| L06 | i1/i16句柄、i8初值2/255/undef/表达式、错AS/section/weak/COMMON/TLS/visibility | `placeholder must be an i8 global`、`initializer must be the constant 0 or 1`、`unsupported placement or linkage`；global-only也查 |
| L07 | 额外memory(none/read/argmem/inaccessiblemem)、speculatable、noalias/dereferenceable或alias metadata | §1.4 `incompatible effects or pointer attributes`；在预优化目标入口失败，不靠优化是否碰巧未删 |
| L08 | 普通AS5参数/返回/global/cast/嵌套聚合、无load声明、i1/i8访存 | 沿Sema/目标IR/后端三层既有AS5错误，显式失败主体+退出非零；obj AS0不是AS5豁免 |
| L09 | MIR非法符号/非零offset/错操作数、缺PSW隐式状态/残留pseudo；符号obj输出asm或REL | machine verifier/最终AsmPrinter已冻结对应错误；ELF-only `MCS251 bit object requires ELF object output`；不造byte替代 |
| L10 | record版本/长度/kind/init/caps/保留字、reloc缺失/重复/错offset/addend/错symbol/错section、BITADDR8错字段 | 全量重跑 `R/lld/test/MCS251/bit-protocol-errors.test`；每mutation以该测试的准确reader主体FileCheck，保留字段解码前后双检查 |
| L11 | 第129位、预留1B后的第121位、固定owner/legacy BIT_BANK/普通RAM冲突 | 非零退出+耗尽请求量/可用量/首未分配符号/owner主体；正例128位或120位及位0/7/8/127 map/字段字节对应 |
| L12 | 重复强定义、实际未定义引用、BITADDR8→byte/function/unregisteredABS、普通地址reloc→bit | lld准确对应类型/解析诊断，bit-allocation/protocol套件保留；不能仅范围0…255合法就接受 |
| L13 | 缺walker/缺XINIT区域/冲突输入XINIT、未知profile/旧CRT与新profile混用 | 既有bit-init/protocol失败CHECK；新crt-bit邻位保留项目标BT14 OPEN，不记本片PASS |
| L14 | P-4错误签名版本/参数位置/返回身份/已知byte混用/缺第三方记录 | 同期则writer+reader准确失败；后置则NOT_IMPLEMENTED/发布BLOCKED，不能给expected-pass |

上述负例含死函数/恒假分支，以及共享常量Use顺序变化；O0/O2/Os、verify开/关、直接llc/bitcode/MIR入口按能表达该形态的层组合。通用LLVM parser无法表达的结构坏例用单元构造模块，不要求错误文本被更早parser吞掉后仍算目标检查证明。

### 6.5 X5 语料复测口径 [U/D]

调查证据使用 `R/validation/mcs251-models/proposals/P09-BIT-TYPE-DESIGN.md` §2，不宣称本轮重扫/重跑：63是多tag求和、62去重；55个bit-obj含41单阻塞与14组合，另7个bit-rmw单阻塞；demo32双tag不得双算。

1. 固定原始 corpus/matrix 的输入快照、改写层、工具快照与配置再运行 `/home/liu/LLVM_STC32/mcs251-corpus-matrix/run-matrix.py`。**41个仅bit-obj-P09阻塞demo预期全部翻正至既有编译tier≥1**；不是承诺链接/外设运行全过。逐demo记录结果，不用总命中率掩盖个例。
2. 14组合demo中的“缺bit对象CodeGen”归因应归零；可能显露既定拒绝或其他独立缺口，按实际归因列出，不强制整个demo翻正。demo83的16处持久 `B=~B` 应进入RMW拒绝，不篡改为合法产品运算。
3. **7个bit-rmw单阻塞demo 02/45.1/45.2/47/49/79/80维持**。改写 `~→!` 是独立语料任务，不计本片产品解锁，也不以删除拒绝换PASS。
4. **demo37两处 `&symbol1` 转挂拒绝表 N01**，诊断 ID `err_mcs251_bit_address_of`；其余 as-cast-gap/corpus-undeclared/bit xdata 等实际阻塞独立保留。语料可另改返回值或uchar写回，但不是bit指针支持、不在本轮执行。
5. 实证频次仅作优先级：707赋值、273普通声明、237实参、216裸条件、89extern、85形参位置/4demo、54返回位置/3demo；是源码扫描统计而非独立函数数或测试通过数。
6. BOOL桶 (`R/validation/mcs251-dialect/type-compat.json`) 在实际支持链通过后才从rejected转为真实 `typedef bit BOOL`，并联验生成头；本轮不改。`/home/liu/LLVM_STC32/mcs251-demos-rewritten/` 的70条byte降级回滚另卡，不能与原始X5矩阵混分母。
7. 报告分 parse/Sema、IR、object、link、model、每板 hardware；P10下无板记 `BLOCKED_NO_BOARD`、模型未覆盖记 `MODEL_UNSUPPORTED`、未跑记 `NOT_RUN`。有限次数模型成功不宣称硬件原子保证。

---

## 7. 切片实施与 Alice 验收表

全部为后续获授权的任务。依赖顺序 **P-0→P-1a→P-1b；P-2局部值可在P-1a后并行，参数部分与P-3联验；P-3→P-4；集成等待P-1b/P-2/P-3及相应负测**。共享 CGDecl/CGExpr/CGCall/CodeGenModule/Lowering/ContractCheck 各窗口唯一属主，禁止用独立build目录假装解除源码屏障。

| 卡 | 交付范围/主要接入 | Alice逐卡完成判据（全部勾选才结卡） | 不等价的“完成” |
|---|---|---|---|
| **P-0** | §1完整协议、§2句柄与身份、§4ABI、§6测试规范；PM登记范围 | [ ] 四精确ID/签名/效果无任选；[ ] 白名单双向检查和Use级bundle规则；[ ] keepalive全路径例外；[ ] 伪intrinsic/坏签名/效果属性fixture期望已签；[ ] alias/自动RMW/间接返回迁移清单；[ ] P-4/CRT发布缺项登记；[ ] 不改已冻结ELF号 | 只有命名或一份“建议用ptr”的草稿 |
| **P-1a** | IntrinsicsMCS251.td、MCS251ContractCheck.cpp、MCS251ISelLowering.cpp/配套选择/最终MC通道 | [ ] 四obj intrinsic往返；[ ] read i1合法化与write链都接通；[ ] 字节/reloc精确符号零addend；[ ] 直接条件一次JB/JNB与长跳；[ ] 常量族回归；[ ] L01–L09适用项含-disable-verify/死代码全拒；[ ] O0/O2/Os+独立opt效果/普通byte别名测试；[ ] 无普通AS5开放 | 能打印一个SETB，或仅writer记录测试通过 |
| **P-1b** | CGDecl/CodeGenModule专用持久发射，CGExpr/CGMCS251Bit/CGExprScalar，SemaMCS251身份/RMW | [ ] static/global/extern/多声明符/cv/PCH；[ ] 未用定义强制发射+准确llvm.used；[ ] initializer非零→1；[ ] DeclRef Symbolic=true而非普通Address；[ ] 同身份CPL无read，different object非toggle；[ ] §2.5双遍历对象矩阵全跑；[ ] alias/COMMON/weak/TLS/取址/布局等门保留；[ ] 两真实C TU→ELF→lld位/初值闭环，非手写IR替代 | 只放行全局零初始化，或把bit编成DSEG byte |
| **P-2** | CGDecl/局部存储/形参副本/ConvertTypeForMem与普通spill路线 | [ ] auto/register四拼写和cv；[ ] O0 i8 alloca/O2可SSA，无bit记录；[ ] 自动复杂运算不受物理RMW误拒；[ ] 每次已定义写规范化；[ ] volatile byte次数；[ ] 高压力/PHI/helper/单参嵌套与邻byte哨兵；[ ] 参数副本与P-3联验、入口在call前读；[ ] source &/array/layout/__block继续拒 | 本地值能存入byte，但形参仍用共享槽活到helper之后 |
| **P-3** | 最小目标ABI分类器与分派；CGCall/CGDecl/CodeGenModule/CGExpr间接返回；参数/返回marshal | [ ] 声明/定义/direct/indirect实际i8；[ ] 每个§4.2转换点非零再压i8；[ ] 第一源参数与原序号后槽；[ ] 9+bit/混合/无名/caller-callee交叉优化；[ ] DPL高7位零与槽字节哨兵；[ ] varargs/no-prototype/多参间接新ID负例；[ ] 非bit/_Bool CC golden不变；[ ] P-4未完仅开发子集，不勾签名发布 | i1+zeroext，或只测试callee解码后的bool |
| **P-4** | 源ABI签名持久化、reader检查、能力发布；按P07单独线格式登记 | [ ] 编码前完整字段/字节/版本/号段审签；[ ] writer与reader都实现；[ ] 定义/声明/直接引用/返回及原源bit位置；[ ] 已知byte冲突硬错；[ ] 缺记录不默许第三方；[ ] 未知版本/格式/reloc拒绝；[ ] 普通无bit兼容不回归；[ ] 真实跨TU正负及golden；[ ] 不私升一般ABI | 只加named metadata、只写前端记录、或以同版编译器纪律代替检查 |

**集成关闭清单（非另一个实现卡）**：T1–T11规定矩阵、N01–N24 ID/文案、L01–L14适用层有实际结果；Clang/LLVM/lld全部既有MCS251相关套件与ISR/AS/CRT回归无未解释变化；X5按§6.5逐项核对；BOOL兼容桶在实际验收后翻正；CRT/P-4/板级未完保持OPEN。每卡交付绝对路径、实际命令/退出码、工具与输入指纹、PASS/FAIL/NOT_RUN清单，不以历史测试数量冒充本轮总数，不授权跨卡修产品。

---

## 8. PM 拍板清单与冲突登记

### 8.1 真正需要用户/PM选择的事项（仅三项）

| 项 | 推荐 | 另一选择的代价/约束 |
|---|---|---|
| **P-4时机** | **与P-3同一正式发布批次完成**；前端开发/X5编译档可先行 | 如先交P-1…P-3，只准同快照、审计原型的开发子集；登记阶段延期，跨TU签名发布门OPEN，不能宣称完整战役验收 |
| **crt-bit.yaml缺口登记与归属** | **登记BT14独立待办，本片只验已批准S1 owned全清零子集**；正式S3“保留外部邻位”完成前必须补新profile | 若要求本片就宣布完整S3，则需同期加入BT14新CRT/reader/污染RAM邻位实测，延长本片并分配属主；不能用旧profile伪装 |
| **demos-rewritten 70条byte降级回滚排期** | **单独语料卡，P-1…P-3 Alice通过后启动，独立台账/快照，不阻塞产品卡** | 若同期排，仍须独立属主与分母；仅当产品支持的形态实际通过才回滚，拒绝表不能靠改写混记产品能力 |

intrinsic命名/AS0句柄、DPL ABI、volatile层次、自动局部不packing、C-only/Keil开关、AS5拒绝、demo37处置、诊断实现落点均有既定裁定或明确工程默认，不再向用户重问。P-0的正式签收和共享文件排班是工程审批，不是新增产品选择。

### 8.2 既定文本冲突及加注记方案（只登记，本轮不改输入）

| 冲突 | 优先规则/本设计处理 | PM动作 |
|---|---|---|
| DESIGN D.2 `R/validation/mcs251-models/DESIGN.md:1131–1132` 排除bit参数/返回；D.5 `:1233` 列bit未支持 | **拍板P01胜出**；普通参数资格、普通返回/指针语义保持原文，不把bit指针当普通指针 | 后续独立文档同步窗口按下方注记追加，毋须重裁DPL ABI |
| DESIGN D.5 `:1249` 关于overlay/重入“保留并诊断”可能被读成恢复ISR闭包检查 | P01已明确静态槽不自动重入、最小ISR不做闭包安全检查，用户负责；本片不恢复已撤销分析，不修改普通参数语义 | 在bit专属注记中重申，不将旧普通节扩成新bit检查承诺 |
| 调查建议把所有bit VarDecl直接列controlled，与增量/任务书自动值例外 | §2.3区分Identity与RequiresPhysicalBitRules，沿用高优先级既定自动值语义 | 记录工程澄清，不请求推翻自动值拍板 |
| 调查“全部七门打开”包含alias；遗漏间接返回 | alias仍拒、P-3纳入间接返回，§4.4台账约束执行者 | 记录范围澄清，不授权alias |
| P-4可后置建议 vs BT10签名前置/整体DoD | 推荐同期；如延后只能PM登记阶段例外，不取消最终签名核对 | 由§8.1第一项裁定时机 |
| 新CRT完整邻位保证 vs盘上S1初始化子集 | P06允许S1先验，但不等于BT14已完成；§5.3明确隔离 | §8.1第二项登记scope与归属 |

**D.2建议追加的规范注记（不删除/改写普通参数表）**：

> 本节“非 bit”的资格限制仅界定本节普通参数/返回协议，不否定已独立批准的 MCS251 bit **值** ABI。BIT-DECISION-20260911 P01 及 P09-BIT-CODEGEN-DESIGN §4 另行规定：第一源参数为 bit 时以规范化 i8 占完整 DPL，bit 返回为完整 DPL=0/1。这里的 bit 值不是 AS5 指针；普通 AS5 参数/返回继续拒绝。非 bit 普通参数/返回及指针字节序列保持本节原义。

**D.5建议追加的规范注记（不开放后续指针槽）**：

> 已批准的 bit 值 ABI 以原源参数序号 `_callee_PARM_n` 为第二及以后的 bit 各分配1B、0/1静态槽（BIT-DECISION-20260911 P01，P09-BIT-CODEGEN-DESIGN §4）。本例外不开放 bit 指针、普通后续指针静态槽、struct、varargs或多参间接调用；不将后续bit塞入寄存器空闲位。静态槽不自动重入；最小ISR不检查调用闭包安全，使用者承担同步/重入责任。非bit普通参数语义不变。

---

## 9. 最终架构裁定与停止规则

**P-0先冻结，P-1a先让后端安全消费，再让前端发句柄；调用私有值与持久对象不混淆，P-3必须显式i8；P-4与CRT未完成不得被“能编译41demo”掩盖。**

本片提供的是已拍板支持集内的Keil源级兼容，不是Keil/SDCC二进制兼容或完整官方头透明；`typedef bit BOOL` 不能降级为unsigned char/_Bool宏。取址、数组/字段/布局、原子、普通AS5、浮点/指针转换、复杂持久RMW仍在拒绝表。

停止条件：发现需要改变既定ELF编号/一般ABI身份、源语言排除集、自动packing、CRT ownership或ISR闭包策略时，执行者停止相关卡、带最小证据退PM，不自行推翻拍板；合法形态因实现bug失败则退对应属主，不降低测试断言。Alice按§7逐卡审查，PM仅按§8真正未决事项裁定；本设计交付本身不表示任何卡已获实施授权或测试PASS。

> **PM 裁定记录（2026-09-13）**：用户裁定"暂时按照推荐设计"——§8.1 三项按推荐执行（P-4 与 P-3 同一正式发布批次完成，开发子集可先行；crt-bit.yaml 缺口登记 BT14 待办、本片只验已批准 S1 全清零子集；demos-rewritten 70 条 byte 降级回滚为单独语料卡，产品卡通过后启动）。P-0+P-1a 实施已于裁定前按 §5.2 开发子集条款启动。
