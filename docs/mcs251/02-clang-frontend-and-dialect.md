# 02. Clang 前端、Keil C251 方言与语义门禁

> **核对源码基线**：`cad7f0ab1f32`  
> **核心源码**：
> - `clang/lib/Driver/ToolChains/{MCS251.h, MCS251.cpp, Clang.cpp}`
> - `clang/lib/Basic/Targets/{MCS251.h, MCS251.cpp}`、`clang/include/clang/Basic/{Attr.td, BuiltinsMCS251.td, LangOptions.def, TokenKinds.def}`
> - `clang/lib/Parse/ParseDecl.cpp`、`clang/lib/Sema/{SemaMCS251.h, SemaMCS251.cpp, SemaDeclAttr.cpp, SemaType.cpp}`
> - `clang/lib/CodeGen/{BackendUtil.cpp, Targets/MCS251.cpp, CGMCS251Bit.cpp, CodeGenModule.cpp, CGBuiltin.cpp}`

---

## 1. Driver 工具链与命令行选项

### 1.1 `MCS251ToolChain` 默认行为
- 独立裸机交叉工具链（`isBareMetal() = true`, `isCrossCompiling() = true`, `HasNativeLLVMSupport() = true`），关闭 `errno` 默认设置、PIC/PIE、profiling 与 unwind 表。
- **默认 `-ffreestanding`**（除非显式传 `-fhosted`）；系统头路径仅包含 Clang 自身的 `<ResourceDir>/include` 独立头文件，绝不拉入宿主系统头或默认 libc。
- **Driver 工具集成（`clang/lib/Driver/ToolChains/MCS251.{h,cpp}`）**：
  - **一键链接（`MCS251Linker`）**：默认链接器 `getDefaultLinker() = "lld"`，`clang --target=mcs251-unknown-none main.c ... -o fw.elf` 自动查找并调用 `lld -flavor mcs251`（或 `mcs251-lld`），透传 `-L`、`-l`、`-T`、`-u`、`-e`、`-s`、`-Wl,...` 与对象/静态库输入；在 `-flto`/`-flto=thin` 下同时按优化级别透传 `--lto-O0..3`；
  - **静态库归档（`MCS251StaticLibTool`）**：`clang --target=mcs251-unknown-none --emit-static-lib ... -o libfoo.a` 自动调用 `llvm-ar rcsD` 生成确定性归档；
  - **汇编工具（`MCS251Assembler`）**：默认开启集成汇编器（`IsIntegratedAssemblerDefault() = true`，`-cc1as` 经 `MCS251AsmParser` 直接把 `.s`/`.S` 编为 ELF `.o`）；显式传 `-fno-integrated-as` 时调用外部 `llvm-mc -triple=... -filetype=obj`；
- **默认对象格式**：`-mcs251-object-format` 默认即为 **`elf`**（不再需要手传 `-mllvm -mcs251-object-format=elf`）；仅旧 `.rel` 兼容链需显式传 `-mllvm -mcs251-object-format=rel`。

### 1.2 全部 MCS-251 编译器选项

| 选项 | 生效层 | 默认值 | 行为说明 |
|---|---|---|---|
| `-fmcs251-keil` / `-fno-mcs251-keil` | Driver + CC1 | `false` | 启用 Keil C51/C251 裸方言语法（非 `mcs251` 目标传参硬报错）。注：`Options.td` 帮助文本仅提了 `interrupt N`，实际同时开放裸 `bit`/`sbit`/`xdata`/`code` 及 `using N` 诊断 |
| `-mcs251-memory-model=<m>` | Driver | `xsmall` | 取值 `tiny` / `xtiny` / `small` / `xsmall` / `large`，由 Driver 翻译为 5 元组 `-mcs251-memory-contract=...` |
| `-mcs251-memory-contract=v,t,a,p,e` | CC1 | `1,2,32,8,1` | 底层数值契约，传给非 `mcs251` 目标或非法五元组时硬报错；`BackendUtil` 以 `+mcs251-memory-contract=v-t-a-p-e` 转交给后端 |
| `-Xclang -target-feature -Xclang +int16` | CC1 | `-int16` (ILP32) | 仅支持 `+int16`/`-int16`（无 `-mint16` 别名），未知 feature 硬报错；处理后清空特征列表，不写入 IR `target-features` |
| `-g*`（DWARF / WP4 C1 门禁） | Driver + CC1 | `-g0` | **默认 ELF 模式下直接放行 `-g*`** 并在对象中发射 `.debug_*` 节（`R_MCS251_32`）；显式指定 `-mllvm -mcs251-object-format=rel` 或纯文本汇编 `-S` 时凡产生源级调试信息仍硬报错（`build with -g0`），`-Rpass`/`-fstack-usage` 维持 `LocTrackingOnly` |
| `-ffunction-sections` / `-fdata-sections` | Driver + CC1 | `false` | ELF 模式下将普通函数与普通全局变量拆为独立输入节（并拆分对应 `XINIT`），配合 `mcs251-lld --gc-sections` 裁剪未引用代码与数据 |
| `-fcommon` | Driver + CC1 | `-fno-common` | 显式传 `-fcommon` 时，暂定外部 `bit` 定义会在 CodeGen 显式报错拒收 |

---

## 2. C 类型系统、预定义宏与内置类型

### 2.1 类型宽度与预定义宏（`MCS251.h` / `MCS251.cpp`）
- **整型与指针**：`short` = 16 位；`int` 默认 32 位（`+int16` 下为 16 位）；`long` = 32 位（固定）；`long long` = 64 位（仅允许布局声明与编译期折叠，运行期算术被拒）；默认指针宽度由契约决定（32 位，或 `tiny`/`xtiny` 下 16 位），`getMaxPointerWidth()` 恒为 32；`size_t` = `unsigned long`，`ptrdiff_t`/`intptr_t` = `signed long`，`wchar_t` = `signed short`。
- **浮点**：`float`、`double`、`long double` 全部为 **32 位 IEEE-754 binary32**（对齐 1B）。注意在 C 类型系统中 `double` 与 `float` 仍为不同类型（TFPU 内建严格要求 `float`）。
- **`_BitInt(N)`**：`hasBitIntType() = true`，硬上限 **`getMaxBitIntWidth() = 32`**（即使传 `-fexperimental-max-bitint-width=64` 亦无法突破）；后端另拒绝非 `{8,16,32}` 位宽的 `_BitInt` 函数形参。
- **C11 原子支持、内联汇编寄存器名与宏**：已移除 `__STDC_NO_ATOMICS__`；`MaxAtomicPromoteWidth = 0`（保持 1B 原生对齐，避免触发全局对齐报错）、`MaxAtomicInlineWidth = 32` 且覆写 `hasBuiltinAtomic` 放行 1B 对齐的 1/2/4B 内联原子（`__atomic_always_lock_free(1|2|4, 0) == true`，`>4B` 在 `CGAtomic.cpp` 中直接路由至通用 `__atomic_{load,store,exchange,compare_exchange}` libcall 且不合成 `i64`，同时抑制 MCS-251 上无意义的 `-Watomic-alignment` 错位告警）。`MCS251TargetInfo` 注册了 `getGCCRegNames`/`getGCCRegAliases`/`validateAsmConstraint`（支持 `r0..r15`/`wr0..wr14`/`dr0,dr4,dr12`/`a`/`b`/`dpl`/`dph`/`dptr`/`dpxl`/`psw`/`r`/`a`/`b`/`d` 等寄存器与约束）。预定义宏：`__mcs251__`、`__MCS251__`，仅在 `+int16` 下定义 `__MCS251_INT16__`。

### 2.2 一等位类型 `MCS251Bit`（`__bit` / `bit`）
- 独立内置无符号标量类型 `BuiltinType::MCS251Bit`（紧随 `Bool` 之后，**绝非 `_Bool` 或 `_BitInt(1)` 的 typedef**）。
- `getIntWidth = 1`、`hasBooleanRepresentation = true`（布尔语义：`val != 0` 归一化为 `1`，不做低位截断）；内部 ABI/内存载体 8 位对齐 1B，但在源码层显式禁止 `sizeof`/`alignof`/`_Alignof`、`&` 取地址、指针、数组、结构体成员与 `_Atomic`。
- 整数提升等级与 `Bool` 同级（低于 `char`），算术表达式中提升为 `int`。

### 2.3 变参类型 `__builtin_va_list`（栈游标 ABI）
- `MCS251BuiltinVaList` 构造为单元素结构体数组（32 位 AS0 为 8 字节、`__off@+4`；16 位 AS0 为 6 字节、`__off@+2`）：
  ```c
  typedef struct __va_list_tag {
    void *__base;        // 指向被调方入参栈区锚点（变参区上界）
    unsigned long __off; // 固定 32 位向下步进偏移（+int16 下仍固定 4B）
  } __builtin_va_list[1];
  ```

---

## 3. 词法、语法扩展与 Keil 关键词矩阵

### 3.1 条件关键词注册表（`TokenKinds.def`、`IdentifierTable.cpp`）

| 内部 Token | C 模式（无 `-fmcs251-keil`） | C 模式（有 `-fmcs251-keil`） | C++ 模式（`-x c++`） |
|---|---|---|---|
| `kw___mcs251_interrupt` | *(普通标识符)* | `interrupt` | `interrupt`（开启 `-fmcs251-keil` 时生效） |
| `kw___mcs251_bit` | `__bit` | `__bit`, `bit` | *(普通标识符)* |
| `kw___mcs251_sbit` | *(普通标识符)* | `sbit` | *(普通标识符)* |
| `kw___mcs251_xdata` | `__xdata` | `__xdata`, `xdata` | *(普通标识符)* |
| `kw___mcs251_code` | `__code` | `__code`, `code` | *(普通标识符)* |

### 3.2 关键语法规则（`ParseDecl.cpp`）
1. **Keil `interrupt` 后缀**：`void timer0(void) interrupt 1 { ... }` 或 `interrupt (48 + 1)`。必须附着在普通函数声明符上，直接规约到与 `__attribute__((interrupt(N)))` 相同的 `MCS251Interrupt` 属性路径。C 模式下 `-fmcs251-keil` 会自动将紧邻 `interrupt` 的空参数列表 `void f() interrupt 1` 规范化为 `void f(void)` 原型。紧随其后的 `using N` / `__using N` 报专属不支持错误并安全恢复。
2. **旧式 `sbit` 声明**：`sbit P32 = P3 ^ 2;` 或 `sbit FLAG = 0x20;`。在文件作用域与块作用域均被拦截（块作用域由 Sema 报 `err_mcs251_sbit_requires_static`），禁止数组/指针/函数/前导属性，顶层 `^` 被解析为 `SFR_BASE ^ BIT_INDEX` 定位语法而不影响普通 C 异或表达式。
3. **`__xdata` / `__code` 位置**：既支持声明说明符位置（`const char code tab[]`、`unsigned char xdata *p`），也支持 `*` 后指针限定符位置（`char * xdata p` 修饰指针对象自身）。
4. **函数后缀与 WP4 B4 已识别但显式拒绝的 Keil 拼写**：
   - **已支持**：`__reentrant`（全模式）及 `-fmcs251-keil` 下的裸 `reentrant` 作为函数后缀被解析器直接接受（现行默认栈 ABI 本身即完全可重入）；
   - **显式拒收**（同时保留它们作为普通函数/变量名而不误杀，如 `int __at(int);` 仍合法）：
     - 声明符位置：`__idata`、`__pdata`、`__near`、`__far`、`__at(...)`；
     - 声明符后缀位置：`_at_ <addr>`（**明确拒收，绝不静默转换为 `mcu_place_at`**）；
     - 函数后缀位置：`__banked`，以及 `-fmcs251-keil` 下的裸 `using N`。

---

## 4. 目标属性完整参考（`Attr.td` / `SemaDeclAttr.cpp`）

| 属性节点 | 合法拼写 | 适用对象 | 参数与核心约束 |
|---|---|---|---|
| `MCS251Interrupt` | `__attribute__((interrupt(N)))`；`-fmcs251-keil` 下的 `interrupt N` | 函数 | 必须为原型化非变参 `void(void)`；槽号 `N` 按 G1 127 槽表（`0..126`）校验（拒绝保留槽与系统槽）；施加 `mcs251_intrcc` + `noinline` + `llvm.used`；须写在首次声明，禁调用/取地址/重槽/外部 `inline`/`weak`/`alias`/`naked`/`always_inline`/`ifunc`/显式 CC |
| `MCS251ISRPriority` | `__attribute__((mcs251_isr_priority(P)))`、`[[mcu::isr_priority(P)]]`（C++11 & C23） | MCS-251 中断函数 | 整型常量 `P ∈ [0..3]`；仅允许附着在带 `MCS251Interrupt` 的函数上且重声明优先级不得冲突；CodeGen 发射 IR 函数属性 `"mcs251-isr-priority"="<0..3>"`，后端发射非 ALLOC `.mcs251.isr.priority` 节（4B BE 记录 `{u16 slot, u8 pri, u8 0}`，零扰动现有 24B `.mcs251.isr`） |
| `MCS251BitAddress` | *(隐式内部属性，由 `sbit` 声明创建)* | 文件作用域变量 | 类型为 `volatile __bit`；位地址 `[0, 255]`（若为 `BASE^IDX`，要求 `BASE ∈ [0x80,0xFF]`、`BASE % 8 == 0`、`IDX ∈ [0,7]`）；禁止与普通变量重声明冲突 |
| `MCS251XDataAddressSpace` | `__xdata`；`-fmcs251-keil` 下的 `xdata` | 类型 | 等价 `address_space(3)`；禁止自动局部存储、函数返回类型带空间限定、与 `bit` 组合 |
| `MCS251CodeAddressSpace` | `__code`；`-fmcs251-keil` 下的 `code` | 类型 | 等价 `address_space(4)`，并通过 `buildMCS251CodeQualifiedType` 对对象类型**自动隐式附加 `const`**（对数字拼写 `address_space(4)` 同样生效）；初始化 AS4 指针的字符串字面量自动重标类型为 AS4；写操作由 Sema 拦截 |
| `MCS251PlaceAt` | `__attribute__((mcu_place_at(A)))`、`[[mcu::place_at(A)]]`（C++11 & C23） | 静态变量 / 函数定义 | 24 位整型常量表达式 `0..0xFFFFFF`；要求本 TU 内有完整非零定义；禁止自动存储、`bit`、依赖类型、与 `bind_at` 混用、重声明地址冲突、同存储类地址重叠、以及未修饰 `_Z` 前缀保留名；按整条重声明链最大对齐检验 |
| `MCS251BindAt` | `__attribute__((mcu_bind_at(A)))`、`[[mcu::bind_at(A)]]`（C++11 & C23） | 外部声明（变量 / 函数） | 绑定外部已固定实体的 VMA；要求外部链接、无初值/无函数体/无暂定定义、完整非零对象类型；不产出存储但会强制物化 IR 声明并挂入 `llvm.compiler.used` |
| `MCS251Retain` | `__attribute__((mcu_retain))`、`[[mcu::retain]]`（C++11 & C23） | 已 `place_at` 定义的实体 | 仅允许用于 `place_at` 定义；不可用于 `bind_at`、纯声明或非固定放置定义；TU 结束时自动附加 `RetainAttr` 并进入 `llvm.used` |
| `MCS251NoInit` | `__attribute__((noinit))` *(仅 GNU 拼写)* | 已 `place_at` 定义的变量 | 仅允许用于**无任何初始化器**的 `place_at` 对象定义（不可与 `bind_at` 或初值联用），抑制 `XINIT`/`XDATA_INIT` 生成 |

> **G11 实体身份与稳定名（Rev 8 §8.1 / Rev 9）**：`CodeGenModule` 在 TU 结束时按规范为每个放置实体计算 `stable_symbol`（外部符号直接取修饰/原名；文件内静态加 `<TU>.<FNV1a32_8hex>.<E(D)>`；函数内静态局部加宿主名与源序编号），硬性限制 `<= 255` 字节（超长报 `err_mcs251_stable_symbol_too_long`），并发射 IR 属性 `"mcs251-place"="0x<hex>,<data|xdata|code>,<object|function>,<owned|bind>,<flags>"` 与 `"mcs251-stable-symbol"`。

---

## 5. 目标内建函数（`BuiltinsMCS251.td`）

全部采用 `CustomTypeChecking`（绕过隐式参数转换，防止窄化或 `double→float` 静默转换掩盖错误）：

1. **`__builtin_mcs251_bit_lvalue(addr)`**：
   - 仅限 C 模式；要求 1 个完整精度整型常量表达式参数 `0..255`；
   - 返回 **`volatile __bit` 左值（`VK_LValue`）**，CodeGen 降低为 `llvm.mcs251.bit.{read,set,clear,toggle}` 固定位内建。
2. **9 个 TFPU DMA 数学协处理器内建（G7 S3）**：
   - 一元：`__builtin_mcs251_tfpu_{sin,cos,tan,atan,sqrt}(float)`；
   - 二元：`__builtin_mcs251_tfpu_{add,sub,mul,div}(float, float)`；
   - 严格校验每个实参为**精确 `float`**（显式拒收 `double`、整数与指针），CodeGen 将 `float` bitcast 为 `i32` 位模式后调用 `llvm.mcs251.tfpu.*` 再 bitcast 回 `float`。
3. **2 个全局中断临界区内建（`EA` = `IE.7`, 位地址 `0xAF`）**：
   - `unsigned char __builtin_mcs251_ea_save_disable(void)`：原子测试并清零 `EA`（`jbc 0xAF`），返回关中断前的原 `EA` 状态（`0` 或 `1`）；
   - `void __builtin_mcs251_ea_restore(unsigned char saved)`：当且仅当 `saved != 0` 时置位 `EA`（`setb 0xAF`），不触碰 `IE.0..6`，支持嵌套与 ISR 内调用；供 `mcs251_atomic.c` 与底层驱动使用。

---

## 6. Sema 与 BackendUtil 核心门禁

### 6.1 受控位 RMW、位对象 CodeGen 与 OpenMP/OpenACC 禁区
- **受控位 RMW（`ControlledBitChecker`）**：作用于所有固定位引用（`sbit`、`__builtin_mcs251_bit_lvalue`）与**静态存储 `bit` 对象**（自动局部与形参 `bit` 走普通值语义，可被 `-O1+` 提升至 SSA）：
  - 允许常量赋 `0/1`（分别降至 `clear`/`set`）及运行期赋布尔值（条件分支到 `set`/`clear`，绝不发射 `MOV bit,C`）；
  - 允许**丢弃结果值**的 `X ^= 1` 与同身份 `X = !X`（降至单条 `CPL` 指令 `toggle`）；
  - 消费翻转表达式结果（`y = (X ^= 1)`）或其他复合赋值/自增减/自读改写（`X = ~X`、`X++`）一律在 Sema 报错；
  - 持久化静态 `bit` 对象发射为 AS0 `i8` 全局句柄（附 `"mcs251-bit-object"` 属性，进 `llvm.used`），读写全部走 `llvm.mcs251.bit.obj.*`；`bit` 函数形参与返回值走 `i8` 直接值 ABI（调用方 `!=0`→`zext i8`，被调入口 `icmp ne 0`→`i1`）；
  - 拒收 `bit` 参与固定变参签名（N13）、无原型调用、TLS/weak/common/section/alias、block 捕获与复合字面量存储（多参数间接调用含 `__bit` 与 `va_arg(ap, __bit)` 现已放行）。
- **OpenMP / OpenACC 禁区（P08 Plan B）**：`SemaMCS251` 维护可嵌套的 RAII 限制栈，在任何被前端处理的 OpenMP/OpenACC 指令参数、clause、关联语句/声明、捕获、块签名与纯类型输入中，递归禁止一切 MCS-251 位能力（无论是否求值）。

### 6.2 CODE 只读保护与 AS4 单向超集
- `CheckCodeStore` 拦截向 AS4 的赋值、复合赋值、`++`/`--` 与内联汇编输出操作数；
- `CheckMCS251CodeSpaceBuiltinCall` 维护约 70 项写内存内建表（`memcpy`/`memset`/`strcpy`/`_chk`/`bcopy`/`bzero`/`va_start`/MS `_Interlocked*` 等），禁止目标指针落在 AS4；
- `MCS251TargetInfo::isAddressSpaceSupersetOf` 仅开放 **AS4 → 32 位 AS0** 单向隐式转换（不包含 AS3 `__xdata`，不适用于 16 位 AS0 的 `tiny`/`xtiny`）。

### 6.3 聚合 ABI、变参门禁与 `EmitVAArg`（Route B）
- **聚合分类（`MCS251ABIInfo`）**：
  - `classifyReturnType`：空记录忽略；1B/2B/4B 平凡聚合经 `ABIArgInfo::getDirect(i8/i16/i32)` 直接走寄存器返回；3B、`>4B`、含柔性数组或非平凡聚合走 `ABIArgInfo::getIndirect(1, /*ByVal=*/false)`（隐藏 `sret` 指针）；
  - `classifyArgumentType`：空记录忽略；C/平凡可拷贝聚合走 `ABIArgInfo::getIndirect(1, /*ByVal=*/true)` 按值压栈。
- **变参门禁（`SemaMCS251`）**：已解除多参数间接调用、间接变参调用、6 变参硬上限及结构体/联合体变参限制；仍拒收 `>32` 位整数（`long long`）与非普通地址空间（`{0,1,2,3,4,8,9}` 之外）指针进入变参实参或 `va_arg` 目标类型。
- **向下步进 `EmitVAArg`**：Clang IR 直接展开 `va_arg`（不发 `llvm.va_arg`、无 6 槽上限或停机分支），兼容 8B（32 位 AS0）与 6B（16 位 AS0）`va_list`，支持标量（自动按目标 `int` 宽度适配 `+int16`）、结构体/联合体（直接返回栈中地址）与 `va_arg(ap, __bit)`（读提升整型槽后 `icmp ne 0` + `zext i8` 归一化）。

### 6.4 WP4 默认正确失败（Sema + BackendUtil 延迟接管）
1. **Sema 源级核心门禁**：
   - **A4/A5/A6 原子家族（现已全面支持）**：放行 C11 `_Atomic` 类型与变量、`<stdatomic.h>`、`__c11_atomic_*`、`__atomic_*`、`__sync_*`、原子围栏与锁无关查询；`CheckMCS251CodeSpaceBuiltinCall` 对向 `__code`（AS4）目标地址的原子写/RMW/out 槽仍保持 Fail-Closed 拒收；
   - **A2 静态绝对地址指针初值**：拒收静态存储初始化器中的非零无基址整数转指针常量；
   - **A7 多参数间接调用（现已全面支持）**：随默认栈传参 ABI 落地，Sema 与后端均已放开 `>= 2` 参及变参函数指针间接调用；
   - **A8 弱定义（普通实体已放行）**：Sema 放行普通函数与变量的 `__attribute__((weak))` 定义（供 ELF 链接弱覆盖与 COMDAT），但 ISR、Bit 对象、G11 固定放置以及 REL 对象发射仍保持 Fail-Closed 拒收；
   - **D1 Computed Goto**：拒收 `&&label` 与 `goto *`；
   - **A9 文件级内联汇编（ELF 模式放行）**：Sema 与后端放行 ELF 模式下的顶层 `asm(...)` 与函数内联汇编（经 `MCS251AsmParser` 组装编码）；REL 对象输出路径对函数/模块内联汇编保持 Fail-Closed 拒收。
2. **BackendUtil 接管 IR 契约检查（防 CrashRecovery 堆栈转储）**：
   - 为避免后端 `report_fatal_error` 触发 Clang 内部崩溃恢复（exit 70 + bug 报告横幅），`TargetParser` 提供全局延迟标志 `setContractCheckDeferred(true)` 与注册回调 `verifyModuleContractMessage`；
   - **普通编译管线**：`BackendUtil::emitAssembly` 在优化前跑结构检查（`CheckArithmetic=false`），优化后/代码生成前跑算术检查（`CheckArithmetic=true`），违规通过 `DiagnosticsEngine`（`err_fe_mcs251_contract_violation`）干净退出（rc=1，无产物残留）；
   - **ThinLTO 管线**：在导入前预检，并在 `PostImportModuleHook`（结构）与 `PreCodeGenModuleHook`（算术）挂接双门禁，确保跨模块导入定义同样受控。
3. **P-4 元数据发射**：`MCS251TargetCodeGenInfo::emitTargetMetadata` 遍历全 TU AST 与编译器生成的外部 libcall 声明，按最终修饰名排序发射 `!mcs251.signatures` 供后端写入 Tag 28。

---

## 7. 内置 STC32 / MCS-251 资源头文件（`clang/lib/Headers/`）

`clang-resource-headers`（`mcs251-resource-headers`）在 `<ResourceDir>/include` 中默认随编译器分发 5 个开箱即用的头文件（无需额外 `-I` 参数，兼容 C11/C23/Keil 方言/`+int16`/C++17）：

| 头文件 | 作用与内容 |
|---|---|
| `<mcs251_intrins.h>` | MCS-251 编译器原生能力封装：`MCS251_SFR`（默认 AS6，显式定义 `MCS251_LEGACY_V1_SFR` 时回退 AS0）、`MCS251_XFR`、`MCS251_XDATA`/`CODE`/`BIT`/`REENTRANT`（C++ 自动映射至 `address_space(3/4)`）、G11 放置宏（`MCS251_PLACE_AT`/`BIND_AT`/`RETAIN`/`NOINIT`）、ISR 宏（`MCS251_ISR`/`MCS251_ISR_PRIO`）与 `__mcs251_isr_priority_init()` 声明、`EA` 原子临界区（`mcs251_ea_save_disable`/`restore`/`enable`/`disable`）、内联汇编 `_nop_()` 与 `NOP1()`–`NOP40()` / `NOP(n)`、TFPU 单精度浮点内联封装及位掩码操作宏 |
| `<stc32g.h>` | STC32G（STC32G12K128 / 8K64 / 12K64）设备头：116 个 AS6 直寻址 SFR（`0x80..0xFE`，遵循安全裁定省略 `RSTCFG@0xFF`）、418 个 XFR（`0x7E0000..0x7EFFFF`）与 `EAXSFR()`/`EAXRAM()`、103 个硬件可位寻址 SFR 位（C 模式经 `__builtin_mcs251_bit_lvalue` 降为硬件位指令，省略编译器托管的 `PSW`/`ACC`/`B`）、176 个非位寻址控制 SFR 具名位掩码、51 个合法中断向量号（与 `MCS251ISR.h` 对齐）及板级内存常量 |
| `<stc32g144k246.h>` | STC32G144K246 全量设备头：118 个 AS6 直寻址 SFR、1,717 个 XFR（含修复官方头双右括号笔误的 `CPU_CRR_*`/`IO_WUP_PRD`/`CPU_STOP_CNT*`）、104 个硬件位宏、191 个非位寻址 SFR 具名位掩码、全部 109 个 G1 合法中断向量号（`0..126`，剔除保留/系统槽）及板级内存常量 |
| `<stc32.h>` | 统一总入口头：默认包含 `<stc32g.h>`，定义 `STC32G144K246`（或 `STC32G_144K246`）时自动切换 `<stc32g144k246.h>`；提供 `STC32_*` / `stc32_*` 统一别名，以及按需开启的 Keil 定宽类型别名（`-DSTC32_ENABLE_KEIL_TYPES`：`u8`/`u16`/`u32`/`s8`/`s16`/`s32`/`BYTE`/`WORD`/`DWORD`/`BOOL`） |
| `<stc32_gpio.h>` | 引脚、硬件位地址与字节掩码关系抽象头：以 `STC32_PIN(port, bit)`（含全部 64 个具名引脚 `STC32_P00`–`STC32_P77`）作为单一真源，编译期零开销推导端口 SFR（`P<n>`/`P<n>M1`/`P<n>M0`）、扩展 XFR（`PU`/`NCS`/`SR`/`DR`/`IE`）、字节位掩码 `STC32_PIN_MASK` 与 C 模式硬件位左值 `STC32_PIN_BIT`，提供 4 种模式配置及电平读写/翻转与电气特性宏 |
