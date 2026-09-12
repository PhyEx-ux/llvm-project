# 运行期 AS3/AS4 指针设计方案甲（`isAddressSpaceSupersetOf` 覆写）

**日期**：2026-09-13。**状态**：设计冻结待用户拍板开放项，**未实施**。
**基线**：`minimal-isr`，`HEAD=d7bcc8f14`。
**来源**：Alice 设计（agent_26e9b825，结论经消息交付，协调员落盘）。
**第一原则（用户裁定）**：符合 C 语言要求优先于 Keil 兼容。

---

## 0. 设计结论（一句话）

**方案甲可以实施，但不能描述成“加一个覆写就能让四个 demo 完成链接”。** 必须拆开四项独立能力：

1. **语言转换**：声明 32 位 AS0 对 AS4 的包含关系，由 Clang 标准机制处理隐式转换。
2. **运行期表示转换**：实现 AS4↔32 位 AS0 的受限、等宽 `addrspacecast`；不得顺带开放其他 CODE 转换。
3. **对象只读语义**：地址空间转换本身不保留不可写权限，必须独立选择 const 或目标扩展 UB 契约。
4. **完整 demo ABI**：41/42/43 使用**第三**指针参数，82 使用**第二**指针参数；现有 v1 静态槽门禁**独立阻塞**它们。

**Alice 推荐**：
- 采用已拍板的标准覆写机制，仅对 **32 位 AS0** 建立 AS4 包含关系。
- 写路径优先采用**真正的隐含 const 语义**；若必须接受真正的非 const `u8 *` 接口，则另行批准“CODE 对象只读、经任何指针修改均为目标扩展 UB”的兼容契约，**不能声称这是 ISO C 的 const 规则**。
- AS3 **不在本片自动连带开放**；为 82 设独立、必须覆盖读写的后续切片。
- 不改变 v1 身份语义，不借 `ProgramAS` 或优化消除绕过静态槽门禁；完整 demo 链接依赖 v2 ABI/身份发布。
- 本片**不落 CP-A 新重定位**；仅开放运行期地址转换，新出现的带 `addrspacecast` 静态初值保持 fail-closed。

---

## 1. 语义裁定与 C/TR 合规性

### 1.1 精确的包含关系（建议冻结的规范文字）

> 在采用 32/8 默认指针表示的 MCS251 内存模型中，实现声明 AS0 通用数据指针能够表示 AS4 CODE 数据对象的地址，并能够通过统一寻址读通道访问该对象。对于地址空间转换及对象身份，AS4 数据对象同时可由 AS0 指针指称；这种包含关系不改变对象的 CODE 放置、只读属性或函数调用身份。

**这不是**“Flash 是 SRAM 的天然子集”，也**不是**“地址都放得进 32 位，因此任何空间都能互转”。

依据：

| 依据 | 位置 | 支持的结论 |
|---|---|---|
| 统一 24 位编址，8M 数据＋8M 程序 | `manuals-md/G144K246/12-存储器-全球唯一ID号CHIPID.md:5` | 地址可处于统一数值域 |
| Flash 用户程序区 `FC:2800..FF:FFFF` | 同文件 `:54-60` | AS4 CODE 对象具有可表示的程序地址 |
| Small/XSmall/Large 的 AS0 为 4B、覆盖 24 位地址域 | `DESIGN.md:253-261` | 32 位 AS0 可承载 CODE bank |
| AS4 为 32/8、CODE 只读数据及函数地址 | `DESIGN.md:283-294` | 表示与用途分别定义 |
| AS0/AS4 的 far 地址分类使用同一 DR 路径 | `MCS251ISelLowering.cpp:2047-2073` | 对合法 CODE 数据读取无需另设通道 |

**注意**：手册 MD 本身存在转写矛盾（`:15` 的 code 范围与 `:24/:58-60` 不一致），设计以表格及程序区明确范围为准，不把可疑转写行作地址边界依据，也不把整个 24 位数值域当作实际存在的存储器。

**合规标记**：TR 允许的 implementation-defined 选择；ISO C 未规定具名地址空间。

### 1.2 TR18037 的边界（WG14 N1275 §5.1.2-5.1.4，PDF 29-30）

- 实现必须定义地址空间两两之间的关系。
- 具名空间不必是 generic 的子集。
- A 为 B 的子集时，A 中对象同时位于 B。
- 不同地址空间指针间的赋值要求源空间为目标空间的子集。
- 显式向较小空间转换，若非空源指针实际不指向目标空间，行为未定义。
- §5.1.4：标准库仍使用 generic 空间，具名空间为其子集时才允许相应传参。
- §5.1.2：函数不是对象，不能从具名对象地址空间限定规则推导函数类型规则。

因此：
1. 必须真正声明**语言层面的包含关系**，不能只说“地址容器恰好能存下”。
2. 包含关系允许转换，但**不会自动添加 `const`**，更不会自动识别任意被调函数是否写入。
3. AS4 同时被 LLVM 用作程序指针空间，**不意味着 TR 授权函数指针与对象指针混用**。

### 1.3 `isAddressSpaceSupersetOf` 返回规则

令 A 为目标空间、B 为源空间。

| A / B | 返回值 |
|---|---:|
| `A == B` | `true`（保留标准自反规则） |
| `LangAS::Default` 与目标编号 AS0 的等价拼写 | `true` |
| A=AS0、B=AS4，且 AS0 为 32 位 | `true` |
| A=AS0、B=AS4，且 AS0 为 16 位 | `false` |
| A=AS4、B=AS0 | `false`（不允许隐式逆转换） |
| A=AS0、B=AS3 | 本片 `false`；独立批准后才改 |
| AS3↔AS4 | `false` |
| AS4↔AS1/AS2/AS8/AS9 | 本片不新增关系 |
| AS5/AS6/AS7 与其他空间 | 不新增关系 |
| 其他不同空间、其他语言空间枚举 | 保持 `false` |

自反返回 `true` **不代表目标支持该空间**；AS5 等能力门禁仍必须拒绝（与 `DESIGN.md:303-307` 一致）。

**建议实现**（放入 `clang/lib/Basic/Targets/MCS251.h` 的 `MCS251TargetInfo`）：

```cpp
bool isAddressSpaceSupersetOf(LangAS A, LangAS B) const override {
  if (A == B)
    return true;

  const auto IsAS0 = [](LangAS AS) {
    return AS == LangAS::Default ||
           (isTargetAddressSpace(AS) &&
            toTargetAddressSpace(AS) == 0);
  };

  if (IsAS0(A) && IsAS0(B))
    return true;

  return IsAS0(A) &&
         getPointerWidth(LangAS::Default) == 32 &&
         isMCS251CodeAddressSpace(B);
}
```

**注意**：
- 不可将数值 `4` 直接强转为 `LangAS`；目标空间有枚举偏移（`AddressSpaces.h:77-114`）。
- 不对任意语言 AS 无条件调用数值映射后比较，否则可能把 OpenCL 等不同语义误认为 AS0。
- 不读取 `-fmcs251-keil`；裸词只是拼写入口，不是转换权限开关。
- 不按“目标是否 const”决定 hook 返回值（CVR 检查属标准类型兼容机制，hook 也没有 pointee 类型信息）。

标准基类位置：`TargetInfo.h:513-518`。使用点由 `SemaExpr.cpp:8590、9475、9693` 继续处理，**不修改标准诊断，不新增 Sema 特判来吞错**。

### 1.4 常规 C 约束全部保留

包含关系不是类型兼容性的万能许可：显式 `const`/`volatile` 不能隐式丢弃；pointee 类型兼容性、对齐、有效类型规则仍成立；不能把一级指针包含关系机械提升为任意 `T **` 转换；指针算术仍受同一数组对象、one-past 及结果可表示性约束；普通函数指针与对象指针不能隐式混用；标准字符串字面量仍按既有普通 C 规则处理，不因该 hook 全部改为 AS4。

**合规标记**：符合 C；AS 转换部分由 TR 扩展。

### 1.5 AS3 是否连带

**建议：不随 AS4 自动开放，单列 A-XDATA 切片。**

理由不仅是两者同为 32/8，还包括不同访问语义：
- AS3 当前由 MOVX@DPTR、DPXL 维护路径处理；转为 AS0 后将改走 DR 路径。
- 不能沿用“AS4 本来就走 DR，因此转换零成本且读取等价”的证明。
- 需证明相同 canonical 地址经 MOVX 与 DR 指向相同 XDATA 单元，覆盖 load、store、bank 边界、宽访问拆分、相关寄存器状态，以及 DMA 前后的可见性。

代码明确分离两者（`MCS251ISelLowering.cpp:2065-2073、2488、2638`）。

**82 不是只读案例**：`82-CANFD使用DMA收发测试/canfd_dma.c:124-131` 中 `CANFD_Set_DMA_Buff` 通过第二参数**写入** DMA 缓冲区（调用见 `:156、172、185` 等）。批准后 AS3→AS0 可独立声明为 TR 允许的 implementation-defined 包含关系，但 DMA、设备同步和寄存器协议属 **C 未规定的目标硬件契约**，不得用地址空间转换替代。

---

## 2. 写路径风险与处置

### 2.1 核心漏洞

转换后的 AS0 指针不再在类型中携带 CODE 地址空间：

```c
unsigned char __code table[4];
unsigned char *p = table;
*p = 1;              // 若隐式转换通过，此处 LHS 已是 AS0
```

- 第一处赋值在方案甲下通过地址空间检查；
- 第二处 LHS 是 AS0，`CheckCodeStore`（`SemaMCS251.cpp:155-169`）不再识别；
- 后端 `checkDataAddressSpace`（`MCS251ISelLowering.cpp:2020-2023`）只检查当前访存 AS，无法从普通 AS0 store 恢复来源。

**所以不能承诺“CODE 写仍全部编译期拒绝”。**

### 2.2 三条候选路线

| 路线 | 语义及优点 | 限制 | 合规裁定 |
|---|---|---|---|
| **(a) `__code` 真正隐含 const** | 类型系统保留只读性；AS4→`const T *` 正常；丢弃 const 被诊断；显式去 const 后修改 const 对象为 UB | 不能无诊断传给真正的 `T *`；影响 typedef、数组、指针对象、声明合并及 AST | const 行为符合 C；关键字隐含 const 是目标扩展，非 TR 自动要求 |
| **(b) 后端/链接期检查 Flash 写** | 可抓部分直接符号、常量地址、优化后可追溯写入；适合防御 | 普通目标文件不能证明动态指针来源；链接器无法可靠推断任意被调函数及汇编效果 | 检查机制 C 未规定；不能声称静态分析完整 |
| **(c) CODE 对象不可修改，跨空间别名写定义为 UB** | 保留非 const `T *` 参数兼容；读路径零成本；无强制运行期开销 | AS0 动态写可能生成机器码而无诊断；须完整公布来源丢失后的边界 | 目标扩展行为，C 未规定；不能冒称 ISO C const-UB 直接适用 |

**对 (c) 的必要修正**（不建议写成“硬件写 Flash 无效，所以符合 C”）：

> MCS251 `__code` 数据对象在其有效生命周期内不可由普通 C 存储操作修改。通过任何地址空间的别名、转换后指针或被调函数尝试修改该对象，均不属于实现支持的有定义程序。Flash 编程必须使用独立的目标接口及协议。

原因：普通 store 不是合法 Flash 编程协议；不能保证每个器件/映射/控制状态都表现为“静默无效”；UB 不能承诺运行时一定不改变存储、一定触发异常或一定安全继续执行；AS0 中的普通可变 RAM 对象仍须可写。

### 2.3 推荐及真实 demo 更正（重要）

**C 合规优先的推荐是 (a)**，保留既有 CODE store 门禁，并增加有限的 (b) 防御。

**实测纠正了输入中的简化描述**：41/42/43 三个接口的图像参数**均为 `const unsigned char *`**：
- `41-.../GUI/gui.h:20`、`42-.../GUI/gui.h:20`、`43-.../GUI/gui.h:20`（定义分别在 `GUI/gui.c:727、724、724`）。

因此 **(a) 不会阻塞这三个已确认的图像调用**。

但“隐含 const”与“任意无 const CODE 对象可无诊断传给真正的 `u8 *`”两项要求**不能同时满足**。若后者也是硬需求，必须选 (c) 并接受其扩展 UB 边界；不能通过忽略 discarded-qualifier 诊断假称符合 C。

### 2.4 编译期/运行期结果对照

| 操作 | 推荐 (a) | 兼容 (c) |
|---|---|---|
| CODE 数组传 `const T *` 并读取 | 接受，保持 const | 接受 |
| CODE 数组传真正的 `T *` | 丢弃 const，必须诊断 | 接受转换；被调方不得修改对象 |
| 直接 AS4 写、RMW | 保留 CODE/const 诊断 | 保留 CODE 诊断 |
| 写 builtin 的直接 CODE 目的参数 | 继续拒绝 | 继续拒绝 |
| 经普通 AS0 局部变量间接写 | 隐式获得非 const 指针的路径受诊断；显式去 const 后写为 UB | 可能编译通过，目标扩展 UB |
| 手写 AS4 store IR | 后端继续拒绝 | 后端继续拒绝 |

对合法、已初始化、在对象范围内且类型相容的 CODE 读取，AS4→32 位 AS0 后的访问是正确的（地址表示和 DR 读通道相同）。**“完全正确”不包括越界、非法整数造指针、丢失 volatile、错误库函数实现或未支持 ABI。**

---

## 3. 实施切片（未来计划，本轮不执行）

### A0：冻结语义与发布范围
**文件**：`DESIGN.md`——B.1 `:253-261`（32 位 AS0 对 AS4 的实现声明；16 位模型不包含）、B.2 `:283-307`（只读语义、特殊 AS 不继承能力）、B.4 `:363-381`（只允许已声明关系的隐式转换；跨 AS 仍用 `addrspacecast`）、D.4 `:1125-1144`（CODE 等宽转换与 RAM 收窄规则分离）、D.5 `:1146-1176`（运行期转换不自动开放静态参数槽和混链）、CP-A `:801、1733-1738、1826`（保持存储初值与运行期地址形成的边界）。
**语料范围声明（必须写入冻结文字，防后续误读）**：41/42/43/82 在本设计中**仅作地址空间模型/兼容性验收语料**，不是教学 demo 线范围。2026-09-11 已裁定官方 demo 教学线走**重写**（82 CANFD 在教学线为 won't-do）。两线不冲突：本设计引用这 4 个 demo 是为验证 AS4/AS3 指针转换能力，不构成教学线范围回摆；教学线的验收锚点仍是改写套件。

**验收**：空间关系、写权限、ABI、重定位四张能力表不矛盾。
**风险**：把“兼容转换”误写成“对象类型改为 AS0”或“AS0 全域可写”。

### A1：标准前端包含关系
**改动**：`clang/lib/Basic/Targets/MCS251.h` 新增 `isAddressSpaceSupersetOf`（§1.3 实现）；不改 `SemaExpr.cpp` 普通兼容性逻辑；不改 `DiagnosticSemaKinds.td` 标准地址空间诊断。
**验收**：v1 p0:32、v2 p0:32 的 AS4→AS0 初始化/赋值/首参/返回/比较；v2 p0:16 仍拒绝；AST 隐式转换种类正确；未优化 IR 保留 `addrspacecast`；`__code`/裸词 `code`/`address_space(4)` 语义一致；host target、OpenCL 检查无误放行。
**风险**：关系方向写反、LangAS 数值混用、CVR 丢失、嵌套指针错误放行。
**X1 影响**：更新“所有跨空间隐式转换均拒绝”的表述，不改变其余类型约束。

### A2：CODE 只读语义
**推荐 (a) 的改动面**：
- `clang/lib/Sema/SemaType.cpp`：`BuildAddressSpaceAttr`（`:6592` 起）、`HandleAddressSpaceTypeAttribute`（`:6671-6766`），用一致的目标限定 CODE 类型构造规则覆盖关键字与数值属性。
- `clang/lib/Sema/SemaMCS251.cpp`：`CheckCodeStore`（`:155`）保留；`CheckMCS251CodeSpaceBuiltinCall`（`:506-542`）保留并测试；`AdjustMCS251StringLiteralPointerInit`（`:545` 附近）核查 AS4 上下文的限定保持。

**不得**只给某个 `VarDecl` 临时打 const 标记却让 typedef、指针 pointee 和属性拼写绕过。特别注意：`char __code *p`（只读所指字符）vs `char * __code p`（只读位于 CODE 的指针对象，不自动使其指向字符只读）；函数不能被添加对象 const 语义。

现有 builtin 检查用 `IgnoreParenImpCasts()`，通常仍可透过新增隐式转换看见原始 CODE 目的表达式；但 `:522-524` 的“隐式转换从不改变地址空间”注释将不再成立，需更正。该检查仍不能追踪已存入普通 AS0 变量的任意别名。

**验收**：直接写、RMW、builtin 写、typedef、post-star、数组、声明合并、显式去 const 后的边界。

**验收补充（限定符组合 × 声明形式 笛卡尔积测试表，必做）**：A2 是**本设计最大的执行风险**（`SemaType` 的类型构造是全局路径，改动波及 typedef/声明合并/属性拼写）。仅列枚举项不足以兜底——`char __code *` vs `char * __code` 这类正交性 bug 在 Sema 里通常靠矩阵发现。要求：

- 轴 1（限定符组合）：`__code`（AS4）、`__code const`、`const __code`、`__code volatile`、`__code const volatile`、无 `__code`（AS0 对照）。
- 轴 2（声明形式）：裸声明 `T __code x;`、typedef `typedef T __code U;`、typedef 的 typedef、指针 pointee `T __code *p;`、指针对象 `T * __code p;`、数组 `T __code a[N];`、数组元素、函数参数、函数返回、结构体成员、`extern` 声明与定义合并（含限定符不一致的合并诊断）、跨 TU 声明合并。
- 轴 3（拼写）：`__code`、`-fmcs251-keil` 裸词 `code`、`address_space(4)` 数值属性（三者语义必须一致）。
- 每条格点断言：只读性归属（是 pointee 只读还是指针对象只读）、IR 里 global/load/store 的 AS、const 是否出现在正确层级、诊断种类。
- 特别验证 A2 注释需更正的 `SemaMCS251.cpp:522-524`（“隐式转换从不改变地址空间”不再成立）。

**追加格点（Alice 复核新增）**：
1. **typedef 展开后的 const 层级**：`typedef char __code CodeChar; typedef CodeChar *CodeCharPtr; CodeCharPtr p;` 对比 `typedef char *CodePtr; CodePtr __code p;`——断言 const 位于 pointee 还是 pointer object，**不能仅看未展开的 typedef 声明**。
2. **隐式 const 与显式 const 的重复/冲突**：`const __code T`、`__code const T`、`const T __code` 的重复 const 诊断/接受规则；`volatile` 与隐式 const 组合；typedef 已含 const 而声明处再次显式 const；**不允许把重复限定符误报为地址空间冲突，也不允许悄悄丢失显式 `const`/`volatile`**。
3. **函数类型专门负例/回归**：`__code` 对象限定**不能**推导出函数类型的相同规则（TR N1275 §5.1.2：函数不是对象），不能因统一类型构造路径而给函数类型添加对象只读语义。

**A2 切片（Alice 复核裁定）**：
- **A2a 类型构造与 AST/IR**：地址空间、隐式 const、显式 CVR、typedef 展开、post-star、数组及声明合并。**A2a 是 A1 的直接依赖**。
- **A2b 诊断收敛与既有门禁**：discarded qualifier、CODE store、builtin 写目的、直接/间接写路径、重复/冲突限定符、OpenCL/MS/z-OS 回归。在 A2a 类型语义稳定后进行。
- **A2c 跨 TU 与 ABI 观察**：extern/定义一致性、函数参数/返回、未优化 IR 和分 TU 行为。可与 A3 的微型 ABI/e2e 测试并行，但必须使用已冻结的 A2 类型规则。

**冻结要求**：

> A2 **不以枚举测试项作为完成标准**；每个笛卡尔积格点必须给出预期 AST 类型、诊断类别和 IR 地址空间。允许使用生成式测试或等价参数化测试实现，但**不得因工作量删减轴或以单一代表例替代**。

**X1-X4 影响**：X1 部分诊断可能由 CODE 专用错误变为或伴随 const 错误；X3 的 CODE 字节图像及 CSEG 放置不得变化。

**§2.1 字符串字面量交互（Alice 复核新增）**：`AdjustMCS251StringLiteralPointerInit` 的冻结验收须明确区分：字符串字面量作为 AS4 来源传给 `const char *`；字符串字面量传给 AS0 `char *`；显式 `const`、隐式 const、普通 C 字符串字面量规则三者叠加后的诊断。**不能因为新增隐式 const 而改变普通字符串字面量的既有地址空间或可诊断性**。

若选 (c)：不做隐含 const 类型改动；必须修订外部调用责任说明，不能再保留“被调方编译后总会由 AS4 store 门禁捕获”的承诺。

### A3：受限的后端等宽转换
**文件/函数**：`MCS251ISelLowering.cpp` 的 `LowerAddrSpaceCast`（`:1040-1103`）；`checkDataAddressSpace`（`:2020-2041`）原则上不变；`parseAddress`/`LowerLoad`/`LowerStore` 仅回归验证。

**不建议直接把 AS4 加入 `IsFarRAM`**（该集合参与 i32 等宽互转、i16→i32 扩展、i32→i16 常量收窄；简单加入会连带开放 AS4↔AS3/AS9、Near→CODE、低地址 CODE 常量收窄等未批准组合）。建议在原 RAM 规则前添加独立 CODE 分支：
1. AS4→AS0，源、目的均 i32：返回原 DAG 值。
2. AS0→AS4，源、目的均 i32：支持显式转换的表示直通；非空值是否实际指向 CODE 由其有效性契约约束。
3. 涉及 AS4 的 i32→i16、i16→i32、AS4↔其他空间：本片明确拒绝。
4. 原 `IsNearRAM`、`IsFarRAM={0,3,9}` 规则保持不变。

逆向显式转换不意味前端逆向隐式赋值合法，也不授权以 AS4 指针写入对象。

**验收**：手写动态 IR 强制命中 `LowerAddrSpaceCast`（不能只测已折叠的全局地址）；`-O0/-O2` 比较直接 AS4 load 与经 AS0 load 的访存字节序列；保持 24 位地址和 bank，无新增 runtime tag/查表/截断；i16 CODE 转换仍拒绝；AS5/6/7 仍拒绝；直接 AS4 store IR 继续命中后端门禁；优化后跨 AS 指针保留正确别名关系，不产生“不同 AS 必定 NoAlias”推断。

**验收补充一：别名分析升格（O2 行为对拍，防静默错编译）**。原设计只有一句“不产生不同 AS 必定 NoAlias 推断”——这不够。上游 `TargetTransformInfo`/`BasicAA` 的假设一旦在 `-O2` 触发错误 NoAlias，症状是**静默错编译**（不是链接错），正是本项目 ContractVerifier 战役里最痛的一类。

**Alice 复核后的上游事实（读源码确认，取代原“不同 AS 默认 NoAlias”的未限定说法）**：
1. `BasicAliasAnalysis::DecomposeGEPExpression`（`llvm/lib/Analysis/BasicAliasAnalysis.cpp:627-639`）会对 `bitcast`/`addrspacecast` 尝试剥除（源为标量指针且源/目标 index width 相同），剥除后继续对底层对象和 GEP 偏移分析——**不会仅因地址空间编号不同就直接 NoAlias**。
2. `ValueTracking.cpp:7231` 的 `getUnderlyingObject` 同样穿过 `addrspacecast`；`:8116-8117` 明确认为“不改变 bit representation”的 `addrspacecast` 可视作 no-op 剥除。
3. `ScopedNoAliasAA.cpp` **未发现**按地址空间编号产生 NoAlias 的专门规则（其结论来自 scope metadata）。

因此**当前上游代码不支持“BasicAA 对不同 AS 默认 NoAlias”这一未限定说法**；但升格理由仍成立——MCS251 的目标语义可能与通用 LLVM 对等宽 `addrspacecast` 的 no-op 假设不同，且错误结果是静默的。修正后的处置：

> 先以当前分支实际 LLVM 源码和 IR 结果为准，验证 BasicAA、ValueTracking 及启用的其他 AA/TTI 组件如何处理 AS4↔AS0 `addrspacecast`。若 MCS251 的非标准存储语义、metadata、目标变换或未来上游变更导致错误 NoAlias，**优先收紧/修正 IR 与目标合法化**，只有存在明确可证明的目标规则时才增加目标 AA 钩子；**不得为了“让测试通过”凭空加入地址空间级 NoAlias/MayAlias 规则**。

**目前没有证据表明必须在 `MCS251ISelLowering`/`MCS251TargetMachine` 增加 `getTgtMemIntrinsic` 或 alias 规则**（`getTgtMemIntrinsic` 是目标内存 intrinsic 描述入口，不是一般指针地址空间别名关系的首选修复点）。除非后续测试证明某个 MCS251 专用 intrinsic 的读写效果未被 AA 正确建模，否则不列为本片必做。

**§2.2 no-op 假设与 DataLayout 一致性（Alice 复核新增，必做）**：上游会在相同 index width/相同 bit representation 前提下穿过 cast。MCS251 必须确保 DataLayout、pointer size、index size 和实际 lowering 对 AS0/AS4 的承诺**始终一致**；否则 AA 认为 no-op 而后端实际改变地址解释，会产生比普通 NoAlias 更严重的错误。A3 必须增加：IR DataLayout 检查；AS0/AS4 pointer size 与 index size 检查；`addrspacecast` 前后实际寄存器/字节序列对照；**非等宽或未来带 tag 的表示变化时，禁止沿用当前 no-op 假设**。

**测试构造（Alice 复核要求）**：不能只比较两次独立 load。应构造有可观察依赖的跨空间读写——AS4 读与转换后 AS0 读/写位于同一函数；用 `noinline` 辅助函数和可观察返回值；让 AS0 写值影响随后 AS4 读取，并反向构造；多字节、交替读写、不同 GEP 偏移；增加标准原型 `memcpy` 场景（AS4 来源与 AS0 目的同时存在）；对比 `-O0/-O2` IR/汇编及 QEMU 逐字节。**正式测试应避免用 `volatile` 掩盖 AA 问题**；必须确认不是因 `const`/`readonly`/`noalias`/`invariant.load`/全局常量折叠导致的假阳性。

**注意**：采用路线 (a) 后，**直接修改真实 CODE `const` 对象本身不应作为“合法 alias 行为”测试**；应测试合法的 AS4 读取与 AS0 读取/转换路径，以及明确属于目标扩展契约的写路径；若要测试跨空间写入，必须在测试说明中标注其目标扩展/UB 前提，不能以 ISO C 定义行为作为对拍依据。

**验收补充二：libc/runtime 原语传染面（TR §5.1.4 的落地）**。设计已引用 TR §5.1.4（标准库使用 generic 空间），但**推论没有落到测试矩阵**：包含关系一开，mcs251rt 原语收到 CODE 来源指针即变成合法调用。依据表说 AS0/AS4 far 读同走 DR 通道、理论上零成本，但 **§4.1 正例缺“libc 原语 src=转换后 CODE 指针”的端到端用例**。

**协调员核查两套运行时后的精确事实（原文“memcpy(dst, code_ptr, n)”的举例不准确，按下述修正）**：

| 运行时 | 位置 | 能定义的指针原语 | 原因 |
|---|---|---|---|
| **语料垫片（标准原型）** | `mcs251-corpus-matrix/shim/shim-libc.c` + `shim/include/string.h:12,21` | **仅单指针原语**：`memset`、`strlen`、`strchr`、`atoi`、`abs`/`labs` | 注释（`shim-libc.c:1-12`）明说：后端 ABI **只接受参 0 的指针**，第二及以后的指针参数被拒——**双指针族（memcpy/strcmp/strcpy）与可变参族（printf/sprintf）根本无法定义，其 demo 调用点也在 llc 被同样 fail-loud 拒绝** |
| **私有 ABI 运行时** | `validation/mcs251-runtime/src/mcs251_libc.{h,c}` | `memset`、`memcpy`/`strcpy`/`memcmp`（**setter + 全局 uint32 槽**两步约定）、`strlen` | `mcs251_libc.h:13-26,28-36`：为绕过后端“指针静态槽拒绝”，第二指针经全局 `uint32_t` 槽 + `(uintptr_t)` 往返；非重入、中断交错可破坏 |

**因此方案甲实际改变的 libc 面**（Alice 复核精确化措辞）：

> 方案甲直接改变的是“**能够接收 AS4 来源指针的 generic/AS0 入口**”这一语言与运行期转换能力。当前立即可测的标准原型原语主要是**单指针原语**；双指针族在语料垫片中**无法定义**，在私有 runtime 中则由 **setter+整数槽 ABI 绕行**，均**不得归因于 AS 转换本身**。私有 ABI 整数往返必须作为**独立的目标扩展/技术债验证**，不得作为标准 `addrspacecast` 语义的替代证明。

具体：
- **立即可测**（v1 契约下已能编译）：`strlen`、`strchr`、`atoi`、`memset`（AS0 目的）——单指针原语，正是方案甲让 `code` 字符串字面量/数组传参的直接受益面；覆盖 `code` 数组与字符串字面量两种来源；
- **v1 下不可编译，须等 A4**：`memcpy`/`strcpy`/`strcmp`/`printf %s`（后者另属 runtime 范围外，`mcs251_libc.h` 范围声明：printf/sprintf/putchar/math.h 不在本内存原语 runtime 交付范围内）——撞的是**指针静态槽/可变参数 ABI**（与 A4 同源），**不是 AS 转换问题**，报告中不得记为方案甲缺陷；
- **私有 ABI 路径**：`memcpy_set_src`/`strcpy_set_src`/`memcmp_set_src` 的 `(uint32_t)(uintptr_t)src` 往返是**不经过 `addrspacecast` 的绕行**——方案甲下可能因 AS4/AS0 当前等宽同表示而“碰巧正确”，但那是实现事实而非语言语义保证，且绕过 A2/A3 的 AS 检查。须验证并标记技术债（未来引入地址空间 tag 或 A4 发布后应迁移到显式 `addrspacecast`/标准原型）。**不得成为前端或后端推广整数绕行的先例**。

**runtime 盘点（Alice 复核）**：`validation/mcs251-runtime/src/` 另有 `mcs251_printf.c`、`mcs251_float_*.c`、`mcs251_bitutil.c`——按范围声明列入“范围外/不作为本片验收对象”，不记为缺陷。**未发现 ISR/hwframe 文件涉及指针参数**；但 `mcs251_libc.h` 已指出 ISR 交错会破坏全局槽，故 **ISR 交错负例/技术债说明必须保留**（§2.5）。

**据此的 A3 验收要求**：

- 两套运行时**分别**测试，不得合并成一个“libc 通过”：
  1. **语料垫片**：至少覆盖 `strlen`、`strchr`、`atoi`，以及 `memset` 的 AS4 来源/AS0 目的组合；覆盖 `code` 数组和字符串字面量；
  2. **私有 runtime**：覆盖三个 `_set_src` 的整数槽往返，并明确记录非重入与中断交错限制；
- 两套均区分 `-O0`/`-O2`/链接/QEMU 逐字节；私有 runtime 已知的 `-O2` PC-rel branch out of range（`mcs251_libc.h:40`）标注为既有约束，非方案甲回归；
- 负例：写类原语的 CODE 目的仍拒绝；
- **风险提示**：这一面不补会以“库函数静默行为异常”形式暴露，比编译错误更难定位。

**§2.5 全局槽 ISR 交错（新增）**：这不是方案甲新引入的问题，但 AS4 来源扩大后更容易被触发。报告须明确：该 ABI 非重入；ISR 交错可能覆盖源地址槽；“单线程 QEMU 通过”**不等于** ISR 安全；若本片不修复，列为**已知技术债和使用前提**，而不是默认为 runtime 端到端完整通过。

**§2.6 `addrspacecast` 与 `inttoptr/ptrtoint` 的语义边界（新增）**：本片支持的是**受限、显式的 AS4↔AS0 `addrspacecast`**；**不新增“先转整数再转指针”作为合法化手段**；私有 ABI 仅记录既有技术债；若未来表示带 tag，必须迁移至显式转换或正式 ABI。

**§2.7 `const` 传播与函数 ABI 类型兼容（新增）**：隐式 const 若落到参数 pointee 类型，必须检查跨 TU 声明、定义和函数类型兼容。**“声明侧隐式 const、定义侧无 `__code`”不能仅因底层 ABI 均为 32 位就被视为可安全合并**。类型兼容、诊断和 ABI 等宽应**分别验收**。

**验收补充二·追加实证（协调员核查 runtime 源码后新增，风险高于原文假设）**：

mcs251rt 的现存实现有**私有 ABI**，与 AS 转换直接冲突：

- `validation/mcs251-runtime/src/mcs251_libc.h:13-26`：`memcpy`/`strcpy`/`memcmp` 采用「**setter + 全局 uint32 槽**」两步私有调用约定——调用方先调 `<fn>_set_src(src)` 把源指针写入全局 `uint32_t` 槽，再调主函数传目的指针。
- 其原因见 `:28-36`：**后端拒绝指针类型的静态槽参数**（"static pointer parameters are not supported"），也**拒绝全局指针变量**，所以第二指针只能经 `uint32_t` + cast 往返。
- `mcs251_libc.c:36-47`：`g_memcpy_src` 存 `(uint32_t)(uintptr_t)src`，函数内 `(const uint8_t*)(uintptr_t)g_memcpy_src` 还原。

**这产生三个必须在本设计内裁定的问题**：

1. **`uintptr_t` 往返会丢弃地址空间**：AS4 指针经 `(uintptr_t)` 变整数再变回 `const uint8_t*`（AS0），是一条**不经过 `addrspacecast` 的绕行**——方案甲下它可能悄悄工作（因为 AS4/AS0 地址表示相同），但那是**巧合而非语义保证**；且它会绕过 A2/A3 的任何 AS 检查。
2. **库原语实际收到的参数早就是 AS0**：所以 `memcpy(dst, code_ptr, n)` 在**当前实现**下走的是「AS4 指针 → uint32 槽 → AS0 指针」路径，其正确性依赖「AS4 与 AS0 表示相同」这个隐含假设——方案甲把这个隐含假设**显式化**，因此本设计必须同时验证该路径。
3. **两个 runtime 的差异要先对齐**：`validation/mcs251-runtime/src/`（私有 ABI，槽式）与语料垫片 `mcs251-corpus-matrix/shim/`（**标准 C 原型** `void *memcpy(void *dst, const void *src, size_t n)`、`size_t strlen(const char *s)`，见 `shim/include/string.h:12,21`）是**两套不同的运行时**。测试矩阵必须明确各测哪一套，不能混为一谈。

**据此追加的 A3 验收要求**：

- 分别对两套运行时做端到端：**标准原型套**（语料垫片/RISCV 式）直接验证 `memcpy(dst, code_ptr, n)` / `strlen(code_str)` 的 AS4→AS0 实参转换；**私有 ABI 套**验证 `memcpy_set_src(code_ptr)` 的整数往返路径，并**记录其为已批准的目标扩展**（或改造成显式 `addrspacecast`）。
- 两套都须 `-O0/-O2` + QEMU 逐字节对拍；`-O2` 已知限制见 `mcs251_libc.h:40`（“-O2 循环展开导致 PC-rel branch out of range”）——该限制须在报告中如实标注是哪一套的哪个函数，不能算方案甲引入。
- 若发现私有 ABI 的整数往返在方案甲下**语义不可靠**（例如未来引入地址空间 tag），须在本设计标记为技术债并给出迁移方向（显式 `addrspacecast` 或静态槽 ABI 发布后改用标准原型）。

### A4：ABI 与 v1/v2 身份
**当前能力**：

| 能力 | 状态 |
|---|---|
| AS4 指针首参及返回 | lowering 已接受该普通指针 AS |
| 4B canonical 指针序列化 | 已有实现 |
| 后续指针静态槽 | lowering 路径存在，但受 `ProgramAS==4` 控制 |
| 明确 v1 契约 | 不能使用后续指针静态槽 |
| v2-shaped 模块输出含一般 AS4 指针能力的 ELF | 仍受身份门禁拒绝 |

代码依据：`MCS251ISelLowering.cpp:3007-3057`（`hasOrdinaryPointerABI`/`checkParameterType`/`checkParameter`）、`:3105、3281`（`AllowStaticPointers`）、`:3155-3166、3314-3320`（静态槽指针装载/序列化）、`:3461-3462`（指针返回 AS 检查）；`DESIGN.md:1084-1089、1148-1167`。

**身份门禁裁定**：`MCS251AsmPrinter.cpp:444-450` 在明确 ASLayoutVersion 1 时直接返回兼容。因此不能笼统说“AS4 指针一定要求 v2”；准确说法是：
- **明确 v1 契约**可通过对象身份检查，但**不绕过**调用 lowering 的静态指针槽拒绝；
- **v2-shaped 请求降级为 v1** 时，签名/指令/常量递归检查仍拒绝一般 AS3/AS4 指针能力（同文件 `:163-180、510-536`）；
- 不能只在返回类型外包一个 AS0 cast，就认为其内部 AS4 来源对身份门禁不可见。

**四个 demo 的后续阻塞**：41/42/43 图像是**第三参数**；82 是**第二参数**。所以 **v1 的 `1,1,32,8,1` 不足以完成这四个原接口的稳定、跨 TU 编译链接验收**（内联恰好消除调用不算 ABI 支持）。

**推荐实施边界**：A1-A3 可在 v1 首参/返回微型用例独立验收；完整四 demo 验收依赖 v2 参数槽及对象身份正式发布；不修改 `isV1ObjectCompatible` 伪装新 ABI；不把 `AllowStaticPointers` 在 v1 无条件置真；不改指针参数为整数参数作为通过手段。若要求本轮完成完整 demo 链接，应将 A4 升格为独立 ABI 发布任务（覆盖 `LowerFormalArguments`/`LowerCall`/`LowerReturn`、AsmPrinter 静态槽定义/`classifyModule`、`lld/MCS251/LinkerCore.cpp` 的输入身份与混链验证、完整函数签名/槽宽/caller-callee 字节序/DSEG-OSEG/递归与 ISR 约束）。

### A5：放置、地址形成与 CP-A
**不需要改变**：AS4 对象仍留在 CODE/CSEG，不因参数转换搬到 RAM（`MCS251AsmPrinter.cpp:1407、1447-1448、1563`；`DESIGN.md:423-439`）。

**三种地址用途必须分开**：

| 用途 | 本片处理 |
|---|---|
| 动态 AS4 指针值转 AS0，再传参/读取 | 运行期等宽转换，不需要新重定位 |
| 函数内形成 `&code_table[offset]` 并传递 | 使用既有地址形成重定位；必须验证 bank/addend |
| 静态存储的指针初值，含 AS4→AS0 `addrspacecast` | 不顺带开放；保持明确拒绝，或另做 CP-A 切片 |

**保留的现状细节**：X3 已允许部分精确指针初值叶子经 `00` 高字节＋`R_MCS251_24` 序列化（`MCS251AsmPrinter.cpp:242-309、682-713`），但其叶子解析**明确排除 `addrspacecast`**（`:247-250`）。因此方案甲产生新的静态 cast 初值可能在 Sema 通过后遇到合法化/对象发射拒绝——这是必须测试和公布的阶段边界。

**建议不在本片落 CP-A**：不扩大 X3 叶子白名单“剥掉 cast”绕过身份；不把 X3 序列化协议宣称 CP-A 已完成；不将 12 号重定位暗中扩为 CODE 数据指针；不将 18 号函数入口重定位用于普通 CODE 数据；不提前分配 19 号。CP-A 方向已批准，开放的是**本轮是否实施、实际号码和完整契约**（`DESIGN.md:1733-1738、1806-1807、1826、1842`）。

---

## 4. 测试矩阵

### 4.1 前端正例
内存契约：v1 p0:32、v2 p0:32；p0:16 为负例对照。拼写：`__code`、`-fmcs251-keil` 下 `code`、`address_space(4)`。构造：CODE 数组退化传 AS0 参数；CODE 指针初始化/赋值/返回 AS0 指针；AS4 与 AS0 相等/不等比较；合法条件表达式共同类型；同一对象经转换后的指针差和 one-past 比较。限定：`const` 保留、`volatile` 保留、路线 (a) 的真正非 const 参数必须诊断。IR：转换前 AS4、目标 AS0、合法 `addrspacecast`（非同宽 `bitcast` 或整数绕行）。

**函数指针**：本片没有“CODE 函数指针转普通数据指针”的正例；仅回归已有直接调用、同类型函数指针传递。

### 4.2 必须维持的负例
AS0→AS4 隐式赋值；AS4→p0:16（不截断 bank）；AS3↔AS4；AS5/AS6/AS7→普通 generic；直接 CODE store/RMW/asm 写输出；builtin 直接 CODE 写目的参数；丢弃显式 const/volatile；不安全多级指针转换；pointer＋pointer、非法函数指针算术；无包含关系空间之间的指针减法；新的静态 AS4→AS0 cast 初值（CP-A 未批准前拒绝）；v1 后续指针参数；不支持的 v2 对象能力。

**两个限制须写清**：①不同数组间的 AS0 指针减法通常是**运行语义 UB**，编译器不必静态识别并拒绝，不能写成保证诊断的测试；②同一 CODE 数组的 AS4 指针与合法 AS0 别名之间算术，不应仅因“原先 AS 不同”继续拒绝。

### 4.3 X1 负例的精确变化
**AS4-only 方案甲本身，没有发现现有显式 `expected-error` 转换负例必然从拒绝变接受。** 不能为凑迁移清单而宣称存在这样的测试。当前实际负例集中在 AS3（`clang/test/Sema/mcs251-xdata.c:62-69`、`mcs251-xdata-code-poststar.c:40-41`）——AS4-only 下全部**不变**；若独立批准 AS3→AS0 才需相应更新（并补干净正例，不能以屏蔽无关 warning 伪造“无诊断接受”）。

**不得削弱**的 X1 覆盖：`mcs251-code.c:25-39`（CODE 写）、`mcs251-code-store-builtins.c`、`mcs251-code-store-host-gate.c`、OpenCL/MS/z-OS 检查入口。这属于**新增正式包含关系后的预期演进**，不是取消安全边界。

### 4.4 四个非 USB demo（分四级报告，不能合并成一个“通过”）

> **范围声明（与 A0 同一口径，Alice 复核要求在此重复）**：本节的 41/42/43/82 **仅作为地址空间模型、转换能力、ABI 和存储链验收语料**，不代表官方教学 demo 线的交付范围，也不改变 2026-09-11 关于教学线重写及 82 CANFD won't-do 的裁定。**A1-A3 通过不等于教学 demo 恢复原样**；完整 demo 链接还受 A4/v2、AS3 或其他独立门禁约束。
>
> 表列名“验收语料”（非“demo”），以减少“产品 demo 交付”的误读。

| 验收语料 | 转换验收 | 额外门禁 |
|---|---|---|
| 41 | `gImage_qq` AS4→AS0 const 第三参数；3200B 图像逐字节保留 | D.5/v2；矩阵另记 ISR vector slot 阻塞 |
| 42 | 同上 | D.5/v2 |
| 43 | 同上 | D.5/v2 |
| 82 | AS3→AS0 第二参数，CPU 写入后读取/DMA 消费一致 | 独立 AS3 读写证明、D.5/v2；矩阵另记 bit 对象阻塞 |

四级：①前端（有关 TU 无未批准转换错误，其他错误原样列出）；②后端对象（`-O0/-O2`，含 `noinline`/分 TU ABI 对照，排除内联偶然绕过）；③链接及字节链（Clang→IR→llc ELF→lld→objdump/readobj→HEX/BIN；图像仍位于 CSEG；指针 bank 和静态槽最高零字节正确；41/42/43 图像载荷逐字节一致；82 的 XSEG 分配及 CPU 写入载荷正确；失败链接不留新可用固件）；④运行（QEMU 可测的地址读取/缓冲区写入；屏幕显示、CANFD DMA 若模拟器未覆盖必须标真机未验收，不能由反汇编替代）。

**只做 A1-A3 时，正确结论应是“转换切片通过，整 demo 仍受 ABI/其他能力阻塞”**，而不是四 demo 已全绿。

### 4.5 回归门槛
Clang 全部 MCS251 测试；LLVM MCS251 **141/141**；lld MCS251 **19/19**；BinaryFormat **397**；既有 e2e（`mcs251-xdata-e2e/build.sh`、`check-bytes.py`、ISR/bit/runtime/放置与启动初始化链）。重点保持：`code-store-error.ll`、`pointer16-addrspacecast.ll`、`call-v2-addrspace.ll`、`xdata-dpxl-mix.ll`、`xdata-o2-order.ll`。

（上述数字是回归基线，不是本轮执行结果。）

---

## 5. 待用户拍板清单 → **已拍板（2026-09-13，用户同意 Alice 四项推荐）**

四项决策已冻结：

1. **AS3 不连带开放**——本片只建立 32 位 AS0 对 AS4 的包含关系；AS3/AS0 与 AS3/AS4 保持拒绝；82 CANFD 仅作后续独立 AS3/XDATA/DMA 切片的验收语料，不纳入本片能力承诺。
2. **写路径采用 (a)：`__code` 真正隐含 const**——只读性进入类型系统；丢弃 const 按普通 C 规则诊断；**不能同时承诺无诊断传给真正的非 const `T *`**；后端/链接期检查只作补充防御，不宣称完整来源追踪。
3. **CP-A 本轮不落地**——只实现运行期、受限、等宽 AS4↔AS0 `addrspacecast`；不新增静态指针初值重定位；新的含 `addrspacecast` 静态初值继续 fail-closed；既有精确初值叶子规则不回退。
4. **转换能力切片优先**——A1-A3 先完成前端包含关系、只读类型语义、后端等宽转换及首参/返回微型 e2e；不修改 v1 ABI，不以整数改签名、不以 inline 偶然消除规避静态槽门禁；完整 41/42/43/82 原接口链接属后续 v2/ABI、AS3 或其他独立切片。

**实施起点与顺序（Alice 复核裁定）**：从 **A0 冻结文字和测试契约**开始；**A1 完成后启动 A2a**；**A3 的后端受限转换与 AA/运行时测试框架可并行推进**；**A2b/A2c 在类型语义稳定后收敛**；**A4 与 A5 最后按 ABI/静态地址形成依赖独立排队，A4 不阻塞 A1-A3 的转换能力验收**。

（Alice 复核对四点补充的逐点裁定：libc 传染面——成立，含措辞与 runtime 盘点的修正；A2 笛卡尔积——成立且充分，追加 typedef 展开/const 冲突/函数类型三组格点并切分 A2a/b/c；别名分析升格——升格成立但“提供目标 alias 规则”修正为“优先收紧 IR 与合法化，有明确可证明规则才加钩子”，并附上游源码事实；教学线区分——成立，§4.4 重复声明并把列名改为“验收语料”。）

---

## 6. 最终架构裁定

**方案甲没有不可回避的地址表示或读取通道障碍；正确的实现入口就是 `TargetInfo::isAddressSpaceSupersetOf`。**

但以下三组概念必须始终分开：
- **地址空间包含关系，不等于介质可写性。**
- **运行期等宽转换，不等于静态指针初值协议。**
- **指针值能传递，不等于 v1 已支持后续指针参数 ABI。**

C 合规的硬边界是：**真正隐含 const 与无诊断传给真正非 const 指针不能兼得**。本次实证表明，三个 CODE 图像调用已经使用 const 参数，安全路线可以覆盖它们；剩余完整链路的主要独立问题是 D.5/v2，以及 82 必须单独证明的 XDATA 写通道。
