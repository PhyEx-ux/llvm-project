# P09 `typedef bit BOOL`（Keil 兼容）实施任务书草案

**作者**：P09 切片调查员。**日期**：2026-09-12。
**状态**：设计任务书草案，供派单与拍板；不是实现完成或测试通过声明。
**调查基线**：工作树 `/home/liu/LLVM_STC32/MCS251`，分支 `minimal-isr`，HEAD `36d82d197`（本轮 `git rev-parse` 亲核）。全部 file:line 断言均为本轮盘上读取后写入；行号是当前工作树锚点，重定位时按函数/定义名匹配。
**证据标签**：[S] 本轮源码亲读；[R] 本轮用 REPORT.md 记载的工具链实测复现（clang 路径 `/home/liu/build-mcs251-s1/bin/clang`，见 `/home/liu/LLVM_STC32/mcs251-corpus-matrix/run-matrix.py:66`）；[C] 本轮对原始语料自行扫描实测；[U] 既有文档/台账已核基线引用；[D] 本任务书设计裁定建议。

## 0. 编号澄清（先读，防止两套 P09 混淆）

本任务书的 **P09** 是 **X5 语料矩阵缺口编号**（`/home/liu/LLVM_STC32/mcs251-corpus-matrix/REPORT.md:28,42`：大类 "BOOL=typedef bit（P09）"，tag `bit-obj-P09` = "BOOL=typedef bit 等 bit 对象代码生成（P09 待做）"）。它与 `BIT-DECISION-20260911.md` 第 P09 条（"位符号句柄表示"，i8 GlobalVariable 占位 + 专用 consumer/记录）是**两套不同编号体系**：后者是 2026-09-11 bit 战役十项拍板之一，其裁定的句柄表示恰好是本缺口实施方案的地基（见 §3）。BIT-TASK-BREAKDOWN.md 的任务卡编号（BT00–BT17）是第三套。本文一律用 "X5-P09" 指语料缺口、"拍板-P09" 指句柄表示裁定、"BTnn" 指任务卡，避免歧义。

## 1. 现状调查（全部 file:line 证据）

### 1.1 已落地部分：类型系统与受控固定位（M1 + M2 前半，均在 HEAD 祖先提交内）

git 历史：`c032488a6`（bit M1，Sema/AST/sbit/OMP-ACC 边界）→ `efec288ac` + `ee0477163`（bit M2，受控固定位 CodeGen + fail-closed 门）→ …… → HEAD `36d82d197`。后端 S0/S1 与 BT12–14 lld 侧见 `0aa879d47`/`c3ce58c13`/`eba50ce45`（BIT-DECISION.md:20 登记）。[S]

**类型表示（专用 BuiltinType 路线已在 M1 落地，无需再裁定）**：

- `clang/include/clang/AST/BuiltinTypes.def:62-73`：`UNSIGNED_TYPE(MCS251Bit, MCS251BitTy)`，紧跟 `Bool` 之后放置，注释明确 "A distinct target boolean scalar type, not a typedef of '_Bool'"，且借 Bool..Int128 连续区间获得整数分类，boolean 特定站点显式 opt-in。
- `clang/lib/AST/ASTContext.cpp:1296-1297`（InitBuiltinType）、`:2030`、`:2222`（canonical/打印分支）、`:8303-8311`（getIntegerRank：介于 _Bool 与 char）、`:12481-12483`（getTypeSize = 1 bit）。
- `clang/lib/AST/Type.cpp:2475-2491`：`hasBooleanRepresentation()` 与 `getScalarTypeKind()` 都把 MCS251Bit 映射为布尔语义（STK_Bool），复用 CK_IntegralToBoolean 转换站点。
- `clang/include/clang/AST/TypeBase.h:2626,9259`：`isMCS251BitType()`。
- 序列化：`clang/lib/Serialization/ASTReader.cpp:8057`（独立类型 ID 往返，不复用 Bool）；PCH 测试 `clang/test/PCH/mcs251-bit.c` 已存在。
- mangling：`clang/lib/AST/ItaniumMangle.cpp:3281`。

**拼写与开关**：

- `clang/lib/Basic/IdentifierTable.cpp:310-328`：`__bit` 由 `LangOpts.MCS251Bit` 控制（目标无条件，仅 C）；裸 `bit`/`sbit` 仅 `-fmcs251-keil`（`LangOpts.MCS251Keil`）且仅 C。`xdata`/`code` 同构于 `:329-348`。`interrupt` 在 `:301-308`。
- `clang/include/clang/Basic/LangOptions.def:310-312`：`MCS251Keil`/`MCS251Bit`/`MCS251AddrSpaces` 三开关。
- `clang/include/clang/Basic/TokenKinds.def:697-716`：`TESTING_KEYWORD(__mcs251_bit/__mcs251_sbit/__mcs251_xdata/__mcs251_code)`，冻结 token、按开关注册受控拼写。
- `clang/lib/Basic/Targets/MCS251.cpp:72-77`：`adjust()` 在 mcs251 目标上无条件置 `Opts.MCS251Bit=1` 与 `Opts.MCS251AddrSpaces=1`（裸词仍需 `-fmcs251-keil`）。

**受控固定位机制（已覆盖位置，BT04/BT05 语义）**：

- `clang/lib/Sema/SemaMCS251.cpp:42-131`：固定位身份 = `sbit` 声明携带的 `MCS251BitAddressAttr` 常量或 `__builtin_mcs251_bit_lvalue(ICE)`；剥括号/隐式转换/_Generic/__builtin_choose_expr 前向恒等；`:107-112` 同地址即别名。
- `clang/lib/Sema/SemaMCS251.cpp:588-645`：builtin 入口（OMP/ACC 构造内先拒、C++ 拒、参数必须单 ICE、全宽 APSInt 检查负数与 >0xFF、地址 0 合法；结果 `volatile __bit` 的 LValue）。
- `clang/lib/Basic/BuiltinsMCS251.td:30-36`：`__builtin_mcs251_bit_lvalue`，CustomTypeChecking，Prototype `"__bit(...)"`。
- `clang/lib/Sema/SemaDecl.cpp:6443-6503`：旧式 `sbit name = addr;` / `sbit name = SFR^k;` 声明解析（隐式 volatile、文件作用域限定 `:6455-6458`、重声明地址一致性 `:6481-6495`）；`clang/lib/Parse/ParseDecl.cpp:1941,5971,6861-6900` 解析入口。
- `clang/lib/Sema/SemaMCS251.cpp:671-1446`：ControlledBitChecker——求值感知遍历，执行 §7.5 强制表：仅 `X^=1`/`X=!X` 且结果丢弃 → CPL；其他复合赋值/自读/++/-- → `err_mcs251_bit_rmw_unsupported`；CPL 结果被使用 → `err_mcs251_bit_lvalue_result_used`；含短路、死分支折叠、语句表达式、asm、捕获区域等完整求值模型。
- `clang/lib/CodeGen/CGMCS251Bit.cpp:42-93`（CodeGen 侧同构地址提取）、`:97-105`（`LValue::MakeMCS251Bit`，volatile bit 类型）、`:118-130`（read → `llvm.mcs251.bit.read` i1 再 zext）、`:132-171`（store：常量→单次 set/clear，动态值→单次采样+分支一次写，刻意不用 MOV bit,C）、`:173-177`（toggle → `mcs251_bit_toggle`）。
- 入口路由：`clang/lib/CodeGen/CGExpr.cpp:3641-3647`（带 Attr 的 DeclRef → `EmitMCS251ControlledBitLValue`）。
- OMP/ACC 构造边界（拍板-P08 Option B）：`clang/lib/Sema/SemaMCS251.cpp:1517-1795`（22 项入口 + `typeCarriesMCS251Bit` 类型粒度判定 `:1613-1638`）。

**后端（S0/S1 + BT12/13/14 lld 侧，均已实现）**：

- 指令/分支/intrinsic lowering：`llvm/include/llvm/IR/IntrinsicsMCS251.td:42-53`（`int_mcs251_bit_read/set/clear/toggle`，**ImmArg i32 常量地址**——这点是本缺口的要害，见 §3.2）；`llvm/lib/Target/MCS251/MCS251ISelLowering.cpp:475-492`（intrinsic 分派）。测试 `llvm/test/CodeGen/MCS251/bit-instructions*.mir`、`bit-branches*.mir`、`bit-intrinsics*.ll`、`bit-carry-deps.mir`。
- 持久位对象句柄（拍板-P09 裁定形态，已实现）：`llvm/lib/Target/MCS251/MCS251BitObject.h:34-36`（`isBitObjectGlobal` = 结构属性 `mcs251-bit-object`，非名字测试）；`llvm/include/llvm/BinaryFormat/MCS251Bit.h:55-90`（8 字节记录布局 version/kind/init/caps/symbol_reference；R_MCS251_BIT_REF=10、R_MCS251_BITADDR8=11，已在 `llvm/include/llvm/BinaryFormat/ELFRelocs/MCS251.def:27-28` 注册）、`:131`（属性名）。
- 逃逸门禁：`llvm/lib/Target/MCS251/MCS251ContractCheck.cpp:480-614`——句柄仅许 verified keepalive 注册用途；**`:549-597` 明文注释 "there is no symbolic bit-access intrinsic yet (the frontend handle contract P09 is not frozen), so a call or operand-bundle use is always an escape today"**。即：后端能**发**位对象记录、能**链接分配**，但**没有任何 intrinsic 能消费句柄**，前端因此无从生成对持久位对象的访问。
- AsmPrinter 发记录：`llvm/lib/Target/MCS251/MCS251AsmPrinter.cpp:120-133,1059-1115`（单 `.mcs251.bit` 节、8 字节/记录、模块序）、`:870-914`（MCInstLower 从机器操作数取回句柄 GV）、`:975,1258`。测试 `llvm/test/CodeGen/MCS251/bit-object.ll`（含手写 IR 的定义/引用/internal 形态）与 `bit-object-use.mir`。
- lld 侧：`lld/MCS251/BIT-OBJECT-CONTRACT.md`（**2026-09-10 冻结的 v1 输入契约**）；`lld/MCS251/LinkerCore.cpp:41-48`（节名）、`:93`（记录结构）、`:386-404`（BITADDR8 字段校验）、`:695-702`（节合法性）；跨 TU 分配与初始化测试 `lld/test/MCS251/bit-allocation.test`（66 RUN，0/7/8/127、129 位耗尽、extern 合并、跨 TU 共 byte）、`bit-init.test`（65 RUN，mask/value 合成、XINIT 排序、池只预留一次——**走的是既有 crt-irq "owned 16B 全清零 + walker" 的 S1 子集**）、`bit-protocol-errors.test`。
- CRT：`crt-bit.yaml` **尚不存在**（`validation/mcs251-elf/runtime/` 只有 crt-irq/crt-selfstart/crt-xdata-init-walker）。当前 bit-init 走 P06 裁定的 "S1 固定 owned 全清零子集"；外部 owner 邻位保留仍待 BT14 新 profile。[S]

### 1.2 未落地部分：X5-P09 缺口本体（Clang CodeGen 对 bit 对象/ABI 的 fail-closed）

全部为 `err_codegen_unsupported` 家族（`clang/lib/CodeGen/CodeGenModule.cpp:2160-2180` ErrorUnsupported），诊断文案实测见 §1.3：

| 句法位置 | 门禁位置（file:line） | 当前诊断文案 |
|---|---|---|
| 自动/静态局部 bit 变量 | `clang/lib/CodeGen/CGDecl.cpp:211-225` | cannot compile this MCS251 bit object yet |
| 文件作用域 bit 全局（含 tentative） | `clang/lib/CodeGen/CodeGenModule.cpp:6528-6540` | cannot compile this MCS251 bit global yet |
| 函数 bit 形参 | `clang/lib/CodeGen/CGDecl.cpp:2692-2705` | cannot compile this MCS251 bit parameter yet |
| bit 返回类型（定义；声明-only 不触发） | `clang/lib/CodeGen/CodeGenModule.cpp:5622-5630` | cannot compile this MCS251 bit return type yet |
| 调用点 bit 实参 | `clang/lib/CodeGen/CGCall.cpp:5286-5293` | cannot compile this MCS251 bit argument yet |
| bit 别名（alias attr） | `clang/lib/CodeGen/CodeGenModule.cpp:4738-4745` | cannot compile this MCS251 bit alias yet |
| 持久/静态 bit 对象的 DeclRef 使用 | `clang/lib/CodeGen/CGExpr.cpp:3648-3657` | cannot compile this MCS251 bit object yet |

类型边界处已就绪的一半：`clang/lib/CodeGen/CodeGenTypes.cpp:412-422`——表达式内 `MCS251Bit` → **i1**，对象/ABI 边界（ConvertTypeForMem，`:104` 起）→ **规范化 i8 载荷**。即 BT10 需要的 "值 i1 / 边界 i8" 分离已在类型层就位，缺的是上面七张门的打开与配套 lowering。

### 1.3 当前准确诊断（逐形态实测，[R]）

工具：`/home/liu/build-mcs251-s1/bin/clang -cc1 -triple mcs251-unknown-none -std=c11`（± `-fmcs251-keil`）`-emit-llvm-only`，单形态单文件（ErrorUnsupported 后续发射停止，混合文件只见首错）。逐条见 /tmp/p09-probe/run.sh 复现脚本（本任务书附件性脚本，未入库）。

**CodeGen fail-closed（= X5-P09 缺口）**：
`BOOL l; l=1;`（自动局部）→ "cannot compile this MCS251 bit object yet"；`static BOOL l;`（局部静态）同；`BOOL g;`（未用 tentative）→ "cannot compile this MCS251 bit global yet"；`BOOL g=0/1;`、`const BOOL g=1;`、`volatile BOOL g;` 全部 → "bit global"；`void f(BOOL b){}` → "bit parameter"（无函数体亦同）；`BOOL f(void){...}` → "bit return type"（`BOOL f(void);` 纯声明**通过**）；`g(2)`（int→bit 实参）与传 bit 值实参 → "bit argument"；`extern BOOL g;`（纯声明）**通过**；`BOOL (*fp)(void);`（函数指针签名含 bit 值，拍板-P08 允许）**通过**；`typedef bit BOOL;` 与 `typedef __bit BOOL;` 均**通过**（typedef 本身从不触发发射）。

**Sema 层既有 fail-closed（本役维持，不动）**：
`&l` → "cannot take the address of an MCS251 'bit' object"（`clang/lib/Sema/SemaExpr.cpp:15153-15154`）；`sizeof(BOOL)` → "sizeof of MCS251 'bit' type is not allowed"（`:4532-4533`/`:4831-4832`）；`BOOL a[4];` → "array of MCS251 'bit' is not allowed"（`clang/lib/Sema/SemaType.cpp:2101-2102`）；`struct S { BOOL b; };` → "MCS251 'bit' is not allowed in a struct or union field"（err_mcs251_bit_field）；`BOOL *p` → "cannot form a pointer to MCS251 'bit' type 'BOOL' (aka '__bit')"（`SemaType.cpp:1852-1853`）；`BOOL xdata g;` → "MCS251 'bit' cannot be declared in a non-default address space"（`:6625-6626,6780-6781`）；`_Atomic BOOL` → "atomic MCS251 'bit' type is not allowed"（`:10524-10525`）；非 const 初始化器 → 标准 "initializer element is not a compile-time constant"。

**开关边界（合规性对照）**：
不开 `-fmcs251-keil` 且在 mcs251 目标：`__bit` 可用（`BOOL g;` 走 bit global 门）；裸 `bit` → "unknown type name 'bit'"。非 mcs251 目标（x86_64 实测）：裸 `bit` → "unknown type name 'bit'"，`__bit` 亦为普通标识符（MCS251Bit 未置）。C++（-x c++）：`__bit` → "unknown type name '__bit'"（拍板-P08 C-only）。均与主干 clang 行为一致（普通标识符回退）。[R]

### 1.4 受控位机制已覆盖/未覆盖的位置小结

已覆盖（fixed 形态，经 intrinsic）：赋 0/1 常量、动态值赋、读值、`if(b)`/`if(!b)` 条件、丢弃结果 `^=1`/`=!b` toggle、同位别名判定、volatile 访问保序。测试：`clang/test/Sema/mcs251-bit-lvalue.c`、`mcs251-bit-rmw.c`、`mcs251-bit-fixed-identity.c`、`clang/test/CodeGen/mcs251-bit-fixed-ref.c`、`mcs251-bit-e2e-bytes.c`（字节级端到端）、`mcs251-bit-unsupported-gates.c`（上表七门+toggle 结果使用的回归钉）。[S]

未覆盖（即 X5-P09 要补的）：一切**符号位对象**访问（局部/全局/静态变量、形参副本、返回值物化、实参归一化），以及持久位对象上的 §7.5 强制表扩展（当前 ControlledBitChecker 只认固定位身份，`clang/lib/Sema/SemaMCS251.cpp:100-102` 的 `isMCS251ControlledBitLValue` 仅识别 Attr/builtin 两种）。

## 2. 用法实证（62 个受阻 demo 的 BOOL 句法位置统计）

### 2.1 计数口径先行澄清

- `matrix.json`/REPORT.md 的 "BOOL=typedef bit（P09）| 63" = `bit-obj-P09`(55) + `bit-rmw`(8) **多归因求和**（`gen-report.py:178-182` 的 `sum(n for t,n in tag_counter...)`，REPORT.md:172 自注 "可多归因"）。demo 32 同时带两 tag，**去重后为 62 个**。[C]
- 其中 **41 个 demo 仅被 `bit-obj-P09` 单一阻塞**——X5-P09 落地即全数解除编译档；14 个还叠 `as-cast-gap` 等（多数是 USB 类与 QSPI/DMA 大 demo）；**7 个 `bit-rmw` 单独阻塞 demo（02/45.1/45.2/47/49/79/80）不是 X5-P09 产品缺口**：其报错是已实现且拍板冻结的受控位 RMW 拒绝（"read-modify-write '=' of a controlled MCS251 bit is not supported"，§2.2/§7.5），matrix 的改写层未做 `X = ~X → X = !X`（`run-matrix.py` 仅有 sfr/sbit/include 级改写），而 mcs251-demos-rewritten 已实证该改写可用（ledger 8 条 `bit-rmw` note）。这 7 个的解锁路径是**语料改写层补齐**，不是本任务书范围。[C]

### 2.2 频次表（[C]，62 个受阻 demo 的**原始**源码，`/home/liu/LLVM_STC32/STC32G144K246-DEMO-CODE/`，gb18030 去注释正则扫描，语句级计数，含少量不可避免的边界误差；方法与脚本可复核）

| 句法位置 | 频次 | 备注 |
|---|---:|---|
| 赋值 LHS（`flag = …`，含 `=0/=1/=表达式`） | 707 | 全距主导形态 |
| 普通变量声明（文件作用域 `bit B_1ms;` 为主） | 273 | 41 单阻塞 demo 的主因 |
| 调用实参（按值传 bit 变量） | 237 | 多与下一行形参对应 |
| `if(B)`/`while(B)`/`for(;B;)` 裸条件 | 216 | |
| `extern bit …`（头文件跨 TU 声明） | 89 | 主要在 uart.h/canfd.h 族 |
| 函数形参声明 `(bit flag,…)` | 85 处 / **4 个 demo**（37、63、64、83） | |
| `if(!B)` 取反条件 | 49 | |
| 比较 `B == / !=` | 40 | |
| bit 返回类型函数 | 54 处 / **3 个 demo**（74-MSC scsi.h、83 chart.h、84 flash_spi.c） | |
| 声明带初始化器 | 25 | 值分布：常量 0×22、常量 1×9、多声明符逗号初始化 2 |
| `typedef bit BOOL;` | 12 处 / **11 个 demo** | **别名拼写全部恰为 `BOOL`**，无其他别名 |
| `&&`/`||` 逻辑运算数 | 21 | |
| `static bit …`（函数内/文件级静态） | 8 处 / 6 个 demo | LIN 族 `static bit Lin1SendMsg=0;` |
| `B = ~B`（持久位对象自 RMW） | 16 | **集中 demo 83**（PlotMode/TriSlope/ADCRunning 等）——按 BIT-TASK-BREAKDOWN §2.2 "b=~b 持久位左值首期拒绝"，**P09 落地后仍拒**，需语料改写 `!` 形态或临时变量 |
| `B = !B` | 3 | 合法 toggle 形态 |
| 位变量作数组下标 `a[B]` | 2 | demo 83 `PlotModeTxt[PlotMode]`，bit→int 提升即可 |
| `return B;` 返回 bit 变量 | 1 | 74-MSC `return fFlashOK;` |
| **`&B` 取地址** | **2** | **仅 demo 37**（科学计算器）：`num_input(a,n,&symbol1,&count2)`，接收方为 `uchar *symbol1` 参数（statistics.c 实测签名），函数体内 `(*symbol1)=1` 写回 |
| 多声明符 `bit a=0,b=0,c=0;` | 13 条语句 | demo 32/37/83 等 |
| `bit *` / bit 数组 / bit 结构成员 / `sizeof(bit)` / bit 函数指针 | **0** | 语料负证据（与 BIT-FIRST-CLASS-INCREMENT §2.1 扫描结论一致；不宣称宏展开后全程序证明） |

补充口径（mcs251-demos-rewritten，[U]）：README §4 记全语料 249 个 bit 变量、42 个 bit 函数/参数（几乎全在 USB demo 83）；ledger 70 条 `bit → unsigned char` 降级 note、688 条本地 sbit 改写、8 条 `X = ~X → X = !X` 改写。BT06 三桶（`validation/mcs251-dialect/type-compat.json`）：官方 DEF.H 75 名中 68 映射、6 external、**1 拒绝 = BOOL**（disposition "rejected"，理由 "bit-object code generation is a follow-up slice (BT06 P09)"）——X5-P09 落地后该桶翻正为 `typedef bit BOOL;`。

### 2.3 设计覆盖判定与 fail-closed 拒绝表

**只需覆盖实证出现的形态**（§2.2 前 17 行）：文件作用域/静态/自动局部对象（含多声明符、const/volatile、0/1 与常量表达式初始化器）、extern 合并、按值形参与实参、bit 返回、条件/比较/逻辑/取反/下标/算术提升（表达式语义 M1 已做，CodeGen 表达式侧 i1 已通）、`!`-toggle。

**取地址的明确处置（AS5 门禁联动）**：`&B` 产生 bit 指针，属 DESIGN.md B.2（`:309`，AS5 "不定义可接受的普通指针 ABI，首期显式拒绝"）与三层门禁范围。现有 Sema 拒绝（`err_mcs251_bit_address_of`）**保持不变**；X5-P09 不开放任何 bit 指针。demo 37 的两处 `&symbol1` 进入拒绝表（该 demo 本身还有 as-cast-gap/corpus-undeclared 等多阻塞，不因 X5-P09 全解锁）；其 Keil 原意（经 `uchar*` 写回标志）建议语料层改写为返回值或 `uchar` 变量，不进产品。

**fail-closed 拒绝表（含诊断与自测要求）**——以下形态维持现状拒绝，每项必须有 lit 负例（多数已有，标注现状）：

| 形态 | 诊断 | 测试现状 |
|---|---|---|
| `&bit`、`bit*`（含 typedef/typeof 间接） | err_mcs251_bit_address_of / err_mcs251_bit_pointer | 已有（mcs251-bit-errors.c 等） |
| bit 数组、参数数组衰变 | err_mcs251_bit_array | 已有 |
| 结构/联合成员、位域 | err_mcs251_bit_field | 已有 |
| `sizeof/alignof/offsetof(bit)` | err_mcs251_bit_layout_query | 已有 |
| `_Atomic bit`、`__atomic/__sync` 族 | err_mcs251_bit_atomic（原子族另按 BT15 扩） | 已有（atomic 项） |
| `bit xdata`（语料 2 处，科学计算器 `bit xdata f_z`） | err_mcs251_bit_addrspace | 已有；BT15 要求两处语料单列负例 |
| 持久位对象 `B=~B`/`B+=…`/`++B`/其他自读 RMW | err_mcs251_bit_rmw_unsupported（**需把身份判定从"固定位"扩到"持久位对象"**） | **缺**：现 checker 只认固定位（§1.4），P09 必须新增 |
| 持久位 CPL 结果被使用 | err_mcs251_bit_lvalue_result_used（同上需扩展） | **缺**（对象形态） |
| varargs 位参、无原型 bit 调用、多参 bit 间接调用、COMMON/weak/TLS bit | 拍板-P08/P07 排除集 | 部分已有，BT15 补 |
| C++ 任何 bit 拼写 | "unknown type name"（P08 C-only） | 已有（mcs251-xdata-code-cxx.c 同构 + IdentifierTable 注释） |
| 浮点/指针 ↔ bit 转换 | 随浮点设计另案（增量稿 §5） | 缺（语料无此形态，低优先） |

## 3. 方案设计

### 3.1 类型路线裁定：**沿用已落地的专用 BuiltinType（候选 A），不重开**

三个候选的实际状态：**A 专用 BuiltinType**——M1 已实现（§1.1），全链路（AST/Sema/序列化/mangle/值语义）已在盘上；**B _Bool 别名加放置规则**——被 BuiltinTypes.def:65-69 注释显式否定，且会继承 _Bool 的取址/布局/截断行为，违背 BIT-TASK-BREAKDOWN §2.1 "不是 _Bool typedef"；**C unsigned _BitInt(1)**——同上被否定。**裁定：X5-P09 不是"引入类型"的役，而是"打开 CodeGen"的役**。类型层零改动（或仅错误信息微调），工作量集中在 §3.2–3.4。这与拍板 P08（"`__bit` 为核心，裸 `bit` 沿用 `-fmcs251-keil`"）和拍板 P01/P02/P09 完全一致，无冲突。

### 3.2 后端前置：**符号位访问 intrinsic 族（本役第一张卡，协议冻结后才能动前端）**

现状要害：`llvm.mcs251.bit.read/set/clear/toggle` 是 `ImmArg<i32>` **仅接受常量地址**（`IntrinsicsMCS251.td:42-53`），无法指向待链接位对象；`MCS251ContractCheck.cpp:584-593` 明文 "no symbolic bit intrinsic consumes a handle yet"，任何 call 用途都是逃逸。[S]

设计裁定建议 [D]：
1. 新增**符号族** intrinsic（命名建议 `llvm.mcs251.bit.obj.read/set/clear/toggle`，签名 `[i1]←(ptr AS0)` / `void←(ptr AS0)`，参数即句柄 GV；**不复用** ImmArg 常量族，两族并存），效果模型与常量族相同（IntrHasSideEffects，无内存窄化属性——常量族文件头 :20-32 的保守效果论证逐条沿用）。
2. 命名/编号若涉及能力记录，按 P07 走版本化登记；intrinsic ID 属 LLVM IR 层，不需要 ELF 编号，但 **MCS251ContractCheck 的 call-逃逸白名单必须同步**（`:589` 的拒绝改为"仅接受已登记符号族 intrinsic 直呼"）。
3. lowering 复用常量族全部机器路径（`ISelLowering.cpp:475-492` 分派 + AsmPrinter `:870-914` 句柄→符号 + `R_MCS251_BITADDR8` 字段）；`CGMCS251Bit.cpp:110-111` 的 symbolic assert（"a later M2 slice"）即本卡解除点。
4. 验收对齐 BIT-TASK-BREAKDOWN BT03 的负例清单：坏签名/伪 intrinsic/句柄经 GEP/cast/initializer 逃逸仍在 `-disable-verify` 下拒绝；优化（O0/O2/Os、instcombine/GVN/LICM/DCE/内联）不改访问次数与排序。

### 3.3 前端 CodeGen 裁定（按句法位置；全部为打开 §1.2 七门并接上 §3.2 符号族）

**P-1 持久/静态位对象（全局、文件级 static、函数内 static）**——语料主导形态（273 声明 + 89 extern）：
- 定义：产拍板-P09 句柄（默认 AS0、i8、结构属性 `mcs251-bit-object`、初始化器限常量 0/1 的 `ConstantInt`，由既有记录通道带走 init 值）；`llvm.used`/verified keepalive 保活（MCS251ContractCheck.cpp:599-607 已定义 keepalive 常量白名单，前端按其形态产出）。
- 访问：DeclRef（`CGExpr.cpp:3648` 门）改走 `EmitMCS251ControlledBitLValue` 的符号分支——`LValue::MakeMCS251Bit(Addr, Symbolic=true, …)`（`CGMCS251Bit.cpp:103-104` 已有 Symbolic 槽位），load/store/toggle 复用既有 §7.5 强制表发射。
- extern/TU 合并：extern 声明产句柄声明（无记录），lld 跨 TU 解析（bit-allocation.test 已验）；同 TU tentative 合并 M1 已过 Sema。
- **Sema 同步项**：ControlledBitChecker 的身份函数从"固定位地址"扩展为"固定位地址 ∪ 持久位对象声明"（去括号后 DeclRef 指向 bit 类型 VarDecl 即受控），使 §2.3 拒绝表的 `B=~B`/CPL-结果使用条款对对象生效；`B^=1`/`B=!B` 丢弃结果 → 单次 CPL 的认可路径同样生效。
- 初始化器求值：非常量初始化器已由 Sema 拒（§1.3）；`= x`（整型常量表达式）按"非零→1"归一后进记录 InitValue 字段（0/1 之外值 AsmPrinter/契约已拒）。

**P-2 自动局部与形参副本（BT11）**：不占 BSEG，SSA/寄存器承载；O0/压力下 1B 规范化 alloca（内容 0/1），不暴露源取址。形参入口尽早读 ABI 位置成调用私有值。`CGDecl.cpp:211-225` 门分流：自动 → 本路径；static → P-1。

**P-3 值 ABI（BT10，拍板 P01 已冻结，照实现）**：首源参数为 bit → 完整 DPL（i8，0/1，bit7:1 为零）；第二个及以后 bit → 原源参数序号的 `_callee_PARM_n` 1B 静态槽；bit 返回 → 完整 DPL。**实现落点已存在**：CC_MCS251/RetCC_MCS251 i8→DPL（`llvm/lib/Target/MCS251/MCS251CallingConv.td:31-42`）；静态槽命名 `_PARM_n`（`MCS251ISelLowering.cpp:3027-3031`）与 AsmPrinter `:1197,1386`；Clang 侧只需 ConvertTypeForMem（已产 i8）+ CGCall/CGDecl 门打开 + 归一化（宽值先 "非零→1" 再压 i8，杜绝 trunc 奇偶——BT09 验收要求）。跨 TU 签名核对（bit vs u8 不因同宽视为兼容）按 BIT-TASK-BREAKDOWN §2.5 的 ABI 签名记录（**该记录的前端发射属本役 P-4，若 PM 决定后置则先以文档声明同版编译器约束**）。
- 注意：**DESIGN.md D.2/D.5 现行文字（"合格"限定为非 struct、非 bit，DESIGN.md:1131；"struct、bit、varargs…不因存在静态槽就自动受支持"，DESIGN.md:1233）写于 bit 拍板之前**，与拍板 P01 冲突。按优先级（用户最新裁定 > DESIGN.md，BIT-TASK-BREAKDOWN §0.2），**拍板 P01 胜出**；本役包含一项文档同步：在 DESIGN.md D.2/D.5 加指针注记（不改其普通参数语义），显式声明 bit 参数/返回由 P01 独立裁定。

**P-4 位对象 ABI 签名记录 / 能力发布（可切至后续卡）**：BIT-TASK-BREAKDOWN §2.5 的签名记录（版本/源参数 bit 位置/返回身份）前端发射。若首轮 X5 复测仅用同版 clang 编译全部 TU，可延后；跨版本/第三方混链场景前必须落。

**明确不做**（沿用 BIT-TASK-BREAKDOWN §10 与 §2.1 拒绝表）：bit 指针/数组/成员/布局查询/原子/浮点指针转换/bit xdata/物理局部打包/carry 返回/位银行/静态槽自动重入/Keil-SDCC 裸混链。

### 3.4 与 AS5 三层门禁、X1 机制、标准 C 合规的边界

- **AS5**：X5-P09 不新增任何普通 AS5 指针类型或 p5 规格 DESIGN.md B.2（:380-383）三层门禁（Sema `err_mcs251_bit_pointer/address_of`；IR 契约 `MCS251ContractCheck.cpp:111` AS5 注记 + 位对象逃逸门 `:480-614`；后端 `checkDataAddressSpace`）全部原样保留；受控位 intrinsic 是唯一通道（DESIGN.md:383 "未来 bit 支持须使用另行冻结的契约"——§3.2 即该契约）。新增符号族 intrinsic 是**受控通道扩展**，不是 AS5 解禁：验收负例必须含"普通 `ptr addrspace(5)` 仍三层拒绝"回归。
- **X1 对齐**：`__bit`/`bit` 与 `__xdata`/`__code` 共用同一开关模式（目标无条件核心拼写 + `-fmcs251-keil` 裸词 + C-only，IdentifierTable.cpp:310-348 连续两段）；X5-P09 不改这一分层，`bit xdata` 冲突按 `err_mcs251_bit_addrspace` 继续拒绝（X1 的 AS 放置与 bit 的"无可寻址表示"正交）。
- **标准 C 合规（硬边界）**：未开 `-fmcs251-keil` 且不写 `__bit` 时，mcs251 目标行为与改动前完全一致；非 mcs251 目标任何拼写下与主干 clang 逐字节一致（`bit`/`__bit` 均为普通标识符，§1.3 已实测；`clang/test/CodeGen/mcs251-bit-cross-target.c` 钉此）。`_Bool`、`unsigned _BitInt(1)`、整数提升（int16/int32 两模型，`MCS251.h:24-37`）零改动。C23 `_Bool`/true/false 语义不受影响；bit 是纯目标扩展类型。
- **OMP/ACC 构造边界**：拍板-P08 Option B 的 22 项入口与 `typeCarriesMCS251Bit`（含 P-1 新对象形态经 `carriesMCS251BitCapability` 的 VarDecl 分支已覆盖 bit 类型对象，SemaMCS251.cpp:1645-1656）**无需改动**即拒对象形态；验收加一条"bit 对象进构造仍拒"回归。

### 3.5 Keil 语义对照与裁定建议

| 维度 | Keil C251 | 本项目（已拍板/本役建议） | 对齐裁定 |
|---|---|---|---|
| 值域 | 0/1，非零→1 | 同（M1 已实现，probe 实测 IntegralToBoolean） | **完全对齐** |
| 存储 | 可位寻址 RAM（bdata 位空间，128 位） | lld `.mcs251.bit` 池 + backing 0x20-0x2F（BIT-OBJECT-CONTRACT §1 与 Keil 物理模型一致） | **完全对齐**（物理布局同 8051 先例） |
| 取址/数组/成员/sizeof | 非法 | 非法（同诊断族） | **完全对齐** |
| 参数/返回 ABI | **carry 传递**（SDCC 亦 bits 银行/carry，BIT-FIRST-CLASS-INCREMENT §4.1 实录） | **DPL 完整 i8 + `_PARM_n` 1B 静态槽**（拍板 P01，2026-09-11 用户批准） | **显式简化、不二进制兼容**——Keil/SDCC 裸混链明确不承诺（P01 原文"无 carry/bits 银行、无与 SDCC/Keil 裸混链"）；源级兼容、二进制级不兼容，任务书只需重申不重开 |
| 静态槽重入 | Keil overlay 亦非重入 | 同样不自动重入（P01 明示用户责任） | 对齐（含代价声明） |
| volatile | 默认非 volatile | L1/sbit 隐含 volatile（拍板 P02），普通 bit 按声明 | **刻意加严**（ISR 共享语义），已在 P02 拍板，语料兼容（不改合法程序行为） |
| `bit xdata` | 非法 | 拒绝 | 对齐 |
| 局部 bit | （Keil 分配位空间，细节随版本） | SSA/1B spill，不物理打包（拍板 P05） | **显式简化**：运行语义等价（0/1、每次调用独立），布局不同；语料无依赖布局的用法（无取址/无数组） |

**总裁定建议**：源级语义完全对齐 Keil（这是 62 demo 的实际需求），ABI 层显式简化并已由 P01/P05 合法化。任务书不再产生新的 Keil 差异点。

## 4. 测试矩阵草案（P-0..P-4 各卡自带，此处为全集骨架）

**轴 1 句法位置** × **轴 2 拼写（`__bit` / 裸 `bit`(需 -fmcs251-keil) / `typedef bit BOOL` / `typedef __bit BOOL`）** × **轴 3 修饰（const/volatile/static/extern/初始化器 0/1/常量表达式/多声明符）**：

| # | 用例 | 期望 |
|---|---|---|
| T1 | 三拼写 × 全局/局部/static/extern 声明 | IR 有句柄或 i8 alloca；诊断零 |
| T2 | 全局/static 带初始化器 0、1、`(2)`、`(x&&y)` 折叠 | 记录 init 0/1；(2)→1 归一 |
| T3 | 多声明符 `bit a=0,b=1;` | 每个声明符独立句柄/记录 |
| T4 | extern 声明 + 他 TU 定义（跨 TU） | lld 合并单槽；bit-allocation 回归 |
| T5 | 赋值：常量 0/1、动态整型、另一 bit、`!B`、经 if 分支 | 单次 set/clear 或单采样单写；CPL 认可路径 |
| T6 | 条件 `if(B)`/`if(!B)`/`while`/`for`、比较、`&&`/`||`、下标、算术 | JB/JNB 单测试；提升语义真值表 |
| T7 | 形参：首参/后续参/无名参/多 bit 混合 9 参、bit+i16/i32/指针 | DPL 全字节 0/1；`_PARM_n` 原序号；高 7 位零哨兵 |
| T8 | 返回 bit / `return B;` / 返回动态值 | DPL=0/1；不经 carry |
| T9 | 函数指针含单 bit 值签名（语料 0 但拍板允许） | 放行且 ABI 同上 |
| T10 | O0/O1/O2/O3/Os × int16/int32 × volatile/非 | volatile 次数/顺序不变；非 volatile 保守可保留 |
| T11 | ISR 共享：ISR 置位主循环可见、不回滚 | QEMU/模型（复用 bit-init.test 的 CRT 序） |

**拒绝表自测**（每行 = §2.3 表一项，`-verify` 断言诊断 ID 与文案；已 listed 现状列"已有"者复用并补对象形态变体）：`&B`（含 `(BOOL*)0` 转换、typeof 链）、`bit*`、`bit[]`、结构成员/位域、`sizeof/alignof`、`_Atomic`、`bit xdata`、`B=~B`/`B=B+1`/`++B`（**新增：对象形态**）、CPL 结果被使用（对象形态）、varargs/无原型/多参间接、COMMON/weak/TLS、C++ 全拼写、非目标拼写、`__atomic_*(B)`、memcpy(B)。IR 层：普通 AS5 指针三层拒绝回归、句柄经 load/store/GEP/cast/ptrtoint/initializer/call 非白名单逃逸（`-disable-verify` 仍拒）、伪 intrinsic、错误能力记录。链接层：bit-protocol-errors.test 全量回归 + 第 129 位、owner 冲突。

**语料验收**：X5 matrix 重跑（`run-matrix.py`，只读语料）——预期 `bit-obj-P09` 单阻塞 41 demo → tier≥1；组合 14 demo 的 P09 归因归零；7 个 `bit-rmw` demo 维持现状（另行语料改写任务）；demo 37 的 `&symbol1` 两处转挂拒绝（不视为回归，README §8 G4 口径更新）。mcs251-demos-rewritten 的 70 条 `bit→unsigned char` 降级可回滚复测（建议另立语料任务，不阻塞本役）。

## 5. 风险、工作量、涉及文件清单

### 5.1 切片与工作量（建议派单序；符号 = BIT-TASK-BREAKDOWN 卡）

| 卡 | 内容 | 量级 | 依赖 |
|---|---|---|---|
| P-0 协议冻结 | 符号 intrinsic 族签名、效果、契约白名单、（若延后 P-4 则同声明）ABI 签名记录范围——BT00 式登记，PM 签 | S/M | 无 |
| P-1a 后端符号族 | IntrinsicsMCS251.td 新四 intrinsic + ContractCheck 白名单 + ISel/AsmPrinter 符号分派（多为复用） | M | P-0 |
| P-1b 前端持久对象 | CGDecl/CodeGenModule/CGExpr 门打开 + 句柄发射 + keepalive + Sema 对象 RMW 扩展 | L | P-1a |
| P-2 自动局部/形参副本 | CGDecl 自动分支 + spill 复用 | M | P-1a（可并行） |
| P-3 值 ABI | CGCall/CGDecl 参数/返回归一化 + 跨 TU 测试（后端 DPL/静态槽已在） | M | P-1a |
| P-4 能力记录 | ABI 签名记录前端发射（可后置，同版编译器约束期） | S/M | P-3 |
| 集成 | BT15 负例补齐 + BT16 矩阵 + X5 复测 + demos-rewritten 回滚抽样 | M | P-1..P-3 |

总体 **L（约 3–5 个工程日规模的实现 + 等量验证）**；最大单项是 P-1b 的门迁移与 §7.5 对象扩展。

### 5.2 涉及文件清单（改动点；行号为当前锚点）

后端：`llvm/include/llvm/IR/IntrinsicsMCS251.td`（:42-53 后新增符号族）；`llvm/lib/Target/MCS251/MCS251ContractCheck.cpp`（:480-614 白名单化 call 拒绝）；`llvm/lib/Target/MCS251/MCS251ISelLowering.cpp`（:475-492 分派 + 符号路径）；`llvm/lib/Target/MCS251/MCS251AsmPrinter.cpp`（符号位指令 BITADDR8 关联，:870-914 大概率只读复用）；新 `llvm/test/CodeGen/MCS251/bit-intrinsics-obj.ll` 等。
前端：`clang/lib/CodeGen/CGDecl.cpp`（:211-225,2692-2705 门分流）；`clang/lib/CodeGen/CodeGenModule.cpp`（:5622-5630,6528-6540 门 + 句柄发射）；`clang/lib/CodeGen/CGExpr.cpp`（:3648-3657 门）；`clang/lib/CodeGen/CGCall.cpp`（:5286-5293 门 + 归一化）；`clang/lib/CodeGen/CGMCS251Bit.cpp`（:110-111 symbolic 解除 + 符号 load/store）；`clang/lib/Sema/SemaMCS251.cpp`（:100-112 身份扩展至对象）；`clang/lib/Sema/SemaExpr.cpp`/`SemaType.cpp`（预计零改动，仅复测）。
文档：`validation/mcs251-models/DESIGN.md` D.2/D.5 指针注记；`validation/mcs251-dialect/type-compat.json` + 生成头（BOOL 桶翻正）；`mcs251-corpus-matrix/REPORT.md` 复测再生成。
测试：`clang/test/Sema/mcs251-bit-type.c`（扩对象正例）、`mcs251-bit-errors.c`/`mcs251-bit-rmw.c`（对象形态负例）、新 `clang/test/CodeGen/mcs251-bit-objects.c`、`mcs251-bit-abi.c`；`clang/test/CodeGen/mcs251-bit-unsupported-gates.c`（门逐项改期望或迁移）。

### 5.3 风险

1. **七张门是唯一的失败通道**：迁移期间任何一条路径漏改即退化为 byte 对象（违背拍板-P09"不静默 byte 化"）。缓解：门测试先行改写为"必须走句柄/i8"的正断言，ErrorUnsupported 全删前先逐门灰度。
2. **契约白名单放开的尺度**：call 用途白名单只认 intrinsic ID 直呼（`getIntrinsicID()`），不做名字前缀测试（ContractCheck :586-588 已警示）。防止伪同名声明混入。
3. **Sema 对象 RMW 扩展的求值模型**：ControlledBitChecker 已有完整求值感知框架，扩展点集中（身份函数 + asFixedBitAssignment），但短路/死分支/语句表达式的对象版本需全量负例，否则 demo 83 的 16 处 `~` 会漏拒或误拒合法 `!` 形态。
4. **静态槽序号**：`_PARM_n` 按源参数序号，混合参数（bit+非 bit）时 Clang 层必须保序，后端 `:3027-3031` 已按 index；跨 TU 混合签名在无 P-4 记录期靠同版编译器纪律，复测矩阵要含两 TU 交叉。
5. **与 demos-rewritten 的 70 条降级回滚**是语料侧动作，若与本役同期执行需独立分支台账，避免把语料命中率当产品通过率（BIT-TASK-BREAKDOWN §0.3 纪律）。
6. **crt-bit.yaml 仍缺**：P06 的外部 owner 邻位保留能力（新 CRT profile）不在本役（S1 全清零子集已验）；若语料出现"依赖邻位保留"的初始化场景会暴露——62 demo 扫描未见（初始化器仅 0/1 常量，池内自有位），风险低但要在 README 缺口清单登记。

## 6. 与三份既定决策文档的关系

### 6.1 沿用（无修改采纳）

- **BIT-DECISION-20260911.md**：P01（值 ABI：DPL/静态槽/返回/不混链）、P02（volatile 分层）、P05（不物理打包、耗尽硬错）、P08（C-only、拼写分层、排除集、OMP/ACC 构造边界）、**P09 拍板（句柄表示 = 本文 §3.3 P-1 的直接依据）**、P10（验收板型口径）。全部直接沿用，本文不产生新拍板项冲突。
- **BIT-FIRST-CLASS-INCREMENT.md**：§1 三种承载/一种值类型（本文 §3.3 的对象/自动/受控三路分流即其实现切分）；§3 分配责任（lld 侧已实现，前端只发需求）；§4 ABI 表（P-3 逐行照抄）；§5 语义边界表（§2.3 拒绝表的母集）；§2 语料统计（29 处 typedef bit BOOL 与本文 12 处/62-demo 子集口径不同但相容——本文是受阻 demo 的精确子集）。
- **BIT-TASK-BREAKDOWN.md**：§2 共同语义冻结区全部；§3.2 切片表（本文 P-1b/P-2/P-3 即 S2+S3 的前端半边）；§4/§5 对应卡片的 file:line 改动点（多数锚点本轮复核仍在或可按函数名重定位，如 `SemaChecking.cpp:3049` 现为 `SemaMCS251.cpp:588-645` 的 builtin 入口、`ASTBitCodes.h:914` 现为 `ASTReader.cpp:8057` 对应读取点——**行号漂移不构成推翻**）；§8 拍板表已被 2026-09-11 用户批准。

### 6.2 新增（本文贡献、旧文档没有的）

1. **X5 语料缺口的精确画像**：62（非 63）去重口径、41 单阻塞 demo 清单、句法位置频次表（§2.2）、`bit-rmw` 8 demo 属"已实现刻意拒绝"的定性（§2.1）——三份旧文档只有 211 文件/631 次与 307 函数/94 文件的旧口径。
2. **后端就绪度盘点**：BT12/13/14 的 lld/AsmPrinter 侧已完成、唯缺符号消费 intrinsic 的结论（ContractCheck :549-597 原文佐证），把实施焦点从"全栈新做"收缩为"协议卡 + 前端开门"。
3. **持久位对象的 §7.5 扩展需求**（Sema 身份函数扩对象）——旧卡片 BT04/BT09 隐含但未点名对象形态的 RMW 判定归属。
4. **DESIGN.md D.2/D.5 与拍板 P01 的文字冲突登记**及指针注记方案（§3.3 P-3）。
5. demo 37 `&symbol1` 的 AS5 处置实证（§2.3）。
6. `crt-bit.yaml` 缺席与 S1 子集边界在 62 demo 上的风险评估（§5.3.6）。

### 6.3 推翻（无）

本文**不推翻任何已拍板裁定**。两处文字级修正均非推翻：
- REPORT.md "63" 计数 → 62 去重（多归因求和口径说明，REPORT.md 自注已兼容）；
- DESIGN.md D.2/D.5 的 "非 bit 合格" 措辞 → 按裁定优先级规则（BIT-TASK-BREAKDOWN §0.2 "用户最新裁定 > 本文接口 > 增量稿 > 方言前端 §7"）由 P01 拍板覆盖，DESIGN.md 加注记不改其普通参数契约。

## 附：本轮证据快照（供复核）

- 分支/HEAD：`minimal-isr` @ `36d82d197`（`git rev-parse` 亲核）。
- 编译复现：`/home/liu/build-mcs251-s1/bin/clang`（run-matrix.py:66 登记路径），脚本 `/tmp/p09-probe/run.sh`（31 形态诊断矩阵）。
- 语料扫描：`/tmp/p09-probe/scan_positions.py`（62 demo、439 文件、gb18030 去注释正则；`&&`/多声明符假阳性已修正）。
- 关键只读输入：`mcs251-corpus-matrix/build/matrix.json`（逐错误文本）、`mcs251-demos-rewritten/ledger.json`（1882 条改写中 bit 族 70+8 条）、`validation/mcs251-dialect/type-compat.json`（BOOL 唯一 rejected）、`lld/MCS251/BIT-OBJECT-CONTRACT.md`（v1 冻结）。
