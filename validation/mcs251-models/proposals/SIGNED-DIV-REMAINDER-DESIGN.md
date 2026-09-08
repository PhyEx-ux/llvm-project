# MCS251 有符号除法/取模 + ELF 运行时算术库设计稿

**v4（2026-09-07，按 Alice 对 v3 复审意见的最终窄修：两处必改 + 三处措辞）**

**作者**：Sakuna（编译器测试/运行时工程师）
**状态**：设计提案（v4 修订版），未实施；未实测语义一律标注实证状态；本稿不改任何源码、不跑构建
**源码基线**：当前工作树（`llvm/lib/Target/MCS251/`、`lld/MCS251/`、`llvm/include/llvm/IR/RuntimeLibcalls.td`、`clang/lib/Driver/`、`clang/lib/Basic/Targets/`、`llvm/lib/TargetParser/`、`validation/`），v2 行号已于 2026-09-07 亲读复核，v3 新增引注同日亲读复核；SDCC 侧证据取自仓库镜像 `sdcc-upstream/build-smoke/device/lib/mcs251-large/`（SDCC 4.6.0，optsdcc 签名与本链 ABI v1 完全一致）
**方法论红线**：语义先 QEMU 实证再信；本稿只含设计与探针计划，不含任何执行结果

**证据分级图例**（v2 新增，Alice 先决裁定要求）：

| 标记 | 含义 |
|---|---|
| 【S】 | 静态代码核验：本轮亲读仓库内源码 / 编译产物 / 测试文件所得，行号可复核 |
| 【Q】 | 历史 QEMU 存档：仓库内归档的历史验收记录，本轮未复测 |
| 【H】 | 真机：本轮及本仓库均**无新增真机证据**；凡存档自述"硬件实测"的记录按【Q】对待，不因本轮引用而升级 |

---

## 0. 修订摘要

### v3 → v4（按 Alice 对 v3 复审意见的最终窄修：两处必改 + 三处措辞；R3/R4 已通过裁定原样不动）

1. **【必改 1】R1 冻结依赖真正闭合**（§6.1、§6.2-1/4、§11.2）：v3 只写"已纳入 §6.1 冻结清单与 §11.2"而两处清单缺项——不能只留交叉引用句。v4 把结果恢复转换依赖补为**可检查规范项**：§6.1 新增第 5 项（适用的冻结 Clang 身份与整数模型；N=16/32 超范围无符号→有符号转换保留低 N 位、按对应有符号值解释的模 2^N 约定，C11 6.3.1.3p3 实现定义；源码注释要求、转换检查方式、合法负结果回归项；工具链或编译条件改变触发重验），§11.2 新增第 6 项构建审计判据。§6.2-4 "全域验收"降格，拆为**全域正确性论证**（冻结转换约定下由无符号幅值 + 商余范围 + 符号恢复规则推导，是论证不是穷举）与**工具链/实现验收**（转换检查 + 边界表 + 固定种子 16 对随机向量对拍，验证实际产物符合论证）；并区分：转换到**无符号**类型的取模是 C 标准保证，超范围转换到**有符号**类型才是需记录的实现定义依赖（§6.2-1 依赖链同步修正）。
2. **【必改 2】R2 删除错误推论**（§6.2-2）：删除"`udiv i32` 落 `_divulong` 不是 `_divuint`（错宽供给 → 静默错译）"的因果结论。替换为准确口径：libcall 由**操作种类与实际宽度**决定；**映射目标等于当前函数时才构成自身调用**；映射到另一 helper 可能形成非预期依赖或违反本方案调用图（由调用图核对拦截），**不单凭宽度不同判定错译**（两 i16 零扩展到 i32、过正确的 i32 除法再截回，对合法除数商是对的）；签名/槽宽/调用约定失配造成的错译须另给依据。红线不放松：八源禁除取余 IR、固定调用图、IR 审计与机器级调用图核对全部保留。
3. **措辞修正**：E30 改为"**写入**经别名改变 DR56 对应位；**读取本身不改写**"；E28 明确 `clang/test/Driver/mcs251.c:16-26` 锁定的是"默认 + 三种显式模型"，不是"四个不同模型"（附录索引同步）；§8.1 i64 探针强调"`zext` 自窄值"只能是构造步骤——单独零扩展窄动态量**没有动态高位**，fixture 必须展示运行期高位参与并影响可见结果。

### v2 → v3（按 Alice 对 v2 的 REQUEST-CHANGES 四项必改）

1. **【R1】结果恢复证明修正 + 移位按"提升后类型"规定**（§6.2）：删除 v2 "超范围转换仅 `0x8000/0x80000000` 且只来自 `(MIN,-1)`"的错误论断——合法输入 `-7/2`→-3（无符号位型 0xFFFD）、`-7%2`→-1（0xFFFF）、`INT16_MIN/1`→-32768（0x8000）都涉及超 int16 正数范围的无符号→有符号转换。采纳修法①：把"依赖本 fork 冻结 Clang 的模 2^N 转换行为"明确记录为**合法输入算法的实现定义依赖**，写入源码注释、冻结管道清单与验收项（§6.2-4、§11.2）；移位红线改为按**整数提升后的实际左操作数类型**检查，移位前完成显式无符号转换（`((unsigned)ux) >> n`），禁止事后截断补救；绝对值恒等式保留并注明依赖链（§6.2-1/3）。
2. **【R2】自递归防线接真实构建链 + 构建契约修正**（§5.1、§6.1、§6.2-2/5、§7.6、§11.2）：写入裁定——**编译器不会因正在编译同名 helper 而避开该 libcall**（体内存活的 `udiv i16` 被 legalizer 映射回 `_divuint`→`__divuint`，本目标关普通尾调用降级 = 真实自身 ECALL）；v2 "包装层出现除法就自递归"精确化为"危险取决于 IR 操作与宽度"（i32 unsigned 落 `_divulong` 不是 `_divuint`），精确原型固定调用边但**不替代 unsigned helper 自身的无除法审计**；补闭环四条（禁令覆盖全部八源、审计实际送 llc 的最终优化 IR、机器级调用图核对、"符号有定义+链接成功"不得当无递归证明）；构建契约两处修正——**裸 triple ≠ 冻结 ABI v1**（当前树 driver 默认已物化 xsmall/v2 契约 `1,2,32,8,1`，完整模型/整数模型/两串 DataLayout/两级优化级钉死不留空占位），`clang -c → llc` 修正为先 `-emit-llvm` 产 IR → 审计 → 再交 llc；i64 审计改 **IR 实体口径**（合法 DataLayout 串就含 `i64:8`，禁止字符串全文搜索；签名检查核对结构与 ABI 属性）。
3. **【R3】SPX 采样点写死 + DR56 别名口径**（§6.3、§8.7）：SPX 区分四时点（caller ECALL 前 / callee 入口即已压 3B 返回帧 / callee ERET 前 / caller 返回后），§6.3 与 §8.7 统一断言 `SPX_before_ERET == SPX_callee_entry`、`SPX_after_call == SPX_before_call`；"DR56 本期任何函数不得触碰"改为"不显式将 DR56/DPX 用作额外算术暂存或帧锚"，明确 ABI 规定的 DPL/DPH 访问及其经物理别名对 DR56 低 16 位的连带影响**不在禁令内**（不能承诺 DR56 全值保留，E30/E31）；DR16 用例补"caller 有动态帧而 helper 无动态帧"组合，不得因八 helper 不触发动态 alloca 就跳过。
4. **【R4】测试有效性与退出门槛**（§8.1、§8.3、§8.4、§8.6、§11.1/11.4）：i64 负探针必须真走到不支持的宽算术路径（动态高位、结果对高位有依赖、不被参数/内存类型限制先截获），锁定"仍需 i64 算术降级时的可诊断拒绝"而非"源 IR 出现过 i64 就必须失败"；UB 隔离——UB IR 探针与合法语义固件**分开运行**，直接调自研 helper 的非法参数观察另标"helper 行为观察"不混称 IR 语义，观察允许返回/终止/超时不预设确定性输出，历史输出自比只记录变化不设兼容门槛，`CHECK-NOT: trap` 明确只是特定 fixture 的代码生成回归检查；§8.6 把"槽大小/节类型"（对象级可证）与"槽访问字节序"（NOBITS 证明不了，由调用方存储+被调方读取+非对称字节值执行级证明）拆开；§11 完成条件显式纳入外部审批与供给门（Oracle-B 八符号独立供给及三方对拍、集成补充规范审批、适用 ELF 前置审批），"本轮无真机证据"是证据等级声明**不是待办**，不要求 §11.4 字面归零。

### v1 → v2（按 Alice REQUEST-CHANGES 执行清单）

1. **【先决】替换 ABI 字节序叙事**：删除 v1 "实测推翻旧存档事实/任务简报假设不成立"的翻案叙事。正确结论是**三种规则并存、各管一层**（§3.2）：① WR/DR 通用寄存器内部 = MSB 在低编号 R 槽；② 调用 ABI 按 DPL/DPH/B/A 枚举 = DPL 装 LSB；③ 内存与 `_PARM_n` 静态槽 = 大端（+0=MSB）。三者互不矛盾，旧表述把②误当成对①③的否定。
2. **冻结八个精确宽度签名 + 无 UB 算法要求**（§6.1）：本 fork int=long=32、short=16，SDCC 源的 `unsigned int` 是 16 位——`unsigned int _divuint(unsigned int, unsigned int)` 原样搬运会生成错误的 32 位接口。逐函数规定 `uint16_t/int16_t/uint32_t/int32_t` 签名、静态宽度断言、IR 签名验收；删除"原八源无除法"论断（`_divsint.c:211` 用 `/`、`_modsint.c:207` 用 `%`）；`(unsigned)(x<0?-x:x)` 是先 signed 取负后转换，INT_MIN 归一化即 C UB——新算法必须在无符号域构造绝对值、显式调 unsigned helper、处理提升/移位宽度/结果恢复；运行时自身禁 i64、禁四种除取余 IR 指令。
3. **重写第 6 节 ABI 表**：纠正栈/clobber/SFR——`_divsint.asm:180-184` 有 `push ar3`（"零栈"错误）；fork C 的自动变量/spill 可进栈（零尺寸 CALLSEQ ≠ 无栈帧）；r8/r9/r12-r15 是 caller-saved，不作"不碰"承诺；DR16 帧锚保存与 SPX 精确恢复单列；"不写任何 SFR"改为"不改无关外设及中断控制状态"；槽计数 24B 裸大小（非 22B），叶 OSEG 组（max=4B）+ 非叶 DSEG（12B），非"八槽全 overlay"；overlay 是 SPEC 规范语义不是可平铺的优化建议。
4. **八个开放项改为明确决策**（§10）：许可证、库归属与两阶段构建、首期交付八个 .o + 显式清单（不交付 .a）、provider 不扩容、集成补充规范先行等逐条落定。
5. **重写 UB/poison 契约**（§8.3）：按 LangRef 官方分类——除数为零 = **UB**（不是 poison）；sdiv/srem INT_MIN÷(-1) = UB（srem 即使数学余数为零）；exact 不匹配才是 poison；不为非法输入新增 trap/返回值/诊断契约；测试三分离；`udiv.ll:78` 注释文案列入更正。
6. **按真实基线重列测试矩阵**（§8）：`divrem-errors.ll` 实为 9 片段/10 条负 RUN（无 i64 片段），放行后 10 条负 RUN 全部翻正、目标 18 条正 RUN；i64 独立负测（含内部构造的 i64 算术路径）；补真正 i8 IR 执行探针；边界表修正（0x8000/0x80000000 非最大值、补 0xffff/0xffffffff、无 -0、2 的幂与跨字节、商 0/1、整除性）；Oracle-B 供给方独立性、禁 variadic printf；新增 ELF 对象级验收与 ABI 执行级验收；成对调用按 call-sequences 口径查 CALLSEQ 内槽存储；库/调用方优化级分别审计。
7. **补出处、构建依赖、对象供给和验收退出条件**（§11）：对象生成 ≠ 归档成功 ≠ 串口一行 PASS ≠ 运行时完成；逐项列出截至本稿尚未实现或未实测的内容。

---

## 1. 摘要（一页版）

1. **现状**：UDIV i16/i32 已接 `_divuint`/`_divulong` libcall；SDIV/SREM/UREM 在 i8/i16/i32 全宽被 `report_fatal_error` 兜底拒绝（`MCS251ISelLowering.cpp:226-229`）。**该文案有误导：UREM（无符号取模）也在拒绝名单里**，报错却只说 "signed division and remainder"——`llvm/test/CodeGen/MCS251/divrem-errors.ll` 用负例把这一行为锁死了（v2 修正：**9 个 IR 片段 / 10 条负 RUN，无 i64 片段**，§1.2）。
2. **运行时库**：今天唯一的"运行时"是 `validation/mcs251-firmware/provider.asm` 里手工移植（harvest）的 SDCC 4.6.0 编译产物（`__divuint`/`__divuint_PARM_2`/`__mulint`/`__gptrget`/`__gptrput`），走 ASXXXX `.rel` 对象、`mcs251_ld.py` 链。**ELF 格式的运行时库不存在**；demo-modern 链只拼 `crt.rel + main.rel + features.rel`，且其除法样例在 -O2 被常数折叠——**demo-modern 从未真正链接/执行过 udiv**（§3 E14）。
3. **裁定 A（选址与源语言，v2 维持）**：运行时库源码放 `llvm/lib/Target/MCS251/Runtime/`（随目标走、随 fork 构建），**源语言用纯 C**。选项 (a) ".S" 在本目标上的实际含义是"ASXXXX 语法 .asm + sdas251"（MCS251 无 AsmParser，llvm-mc 汇编不了任何东西），产物是 `.rel` 不是 ELF，与 ELF 链（lld MCS251，SPEC E1）方向冲突——详见 §5。v2 补充：源码为**公开算法独立实现**（许可裁定见 §10-Q1），SDCC 源/产物只作参考与 oracle。
4. **裁定 B（符号命名，v2 定为终裁）**：**沿用 SDCC 名**（`_divsint/_divslong/_moduint/_modulong/_modsint/_modslong` 六个新符号 + 已有 `_divuint/_divulong`）。理由：ABI v1 的存在理由就是 SDCC 互操作；本 fork 的 `RuntimeLibcalls.td` 机制已为 SDCC 名铺好全部管线。矩阵见 §5.4：**8 个 i16/i32 目标 SDCC 全部有现成逻辑可作参考（算法为公共领域移位-减法，无需发明；但源文本必须独立实现，§10-Q1）；i8 四操作无任何符号（SDCC 前端也把 char 提升到 int），靠 Promote 到 i16 覆盖**。
5. **字节序三层结论（v2 按先决裁定替换）**：① WR/DR 通用寄存器**内部**：MSB 在低编号 R 槽；② 调用 ABI 按 DPL/DPH/B/A **枚举**：DPL 装 LSB；③ 内存与 `_PARM_n` 静态槽：**大端**（+0=MSB）。三层并存、作用域不同，见 §3.2。
6. **验证**：lit 编译层 + QEMU 金标对拍（宿主宽类型期望 vs 串口）+ SDCC oracle（Oracle-B）三方对拍；UB 输入（除零、MIN÷(-1)）与 poison（exact 不匹配）按 LangRef 分类，**不进语义断言**，另立独立会话、只记录、不预设输出形态的观察档（IR 探针与"helper 行为观察"分标，§8.3）；边界表按 v2 修正口径；除语义验收外新增 ELF 对象级与 ABI 执行级两层验收（§8；v3：槽大小/节类型对象级证、槽访问字节序执行级证）。

---

## 2. 问题陈述

### 2.1 后端现状

`llvm/lib/Target/MCS251/MCS251ISelLowering.cpp`：

- `:56-61`（构造器）——UDIV 现有接线：

```cpp
setOperationAction(ISD::UDIV, MVT::i8, Promote);
setOperationPromotedToType(ISD::UDIV, MVT::i8, MVT::i16);
setOperationAction(ISD::UDIV, MVT::i16, LibCall);
setOperationAction(ISD::UDIV, MVT::i32, LibCall);
setLibcallImpl(RTLIB::UDIV_I16, RTLIB::impl_mcs251_divuint);
setLibcallImpl(RTLIB::UDIV_I32, RTLIB::impl_mcs251_divulong);
```

- `:62-71`——SDIV/SREM/UREM 在 i8/i16/i32 全部注册 **Custom**（`UDIVREM/SDIVREM/MULHU/MULHS/UMUL_LOHI/SMUL_LOHI` Expand）。
- `:226-229`——Custom 兜底是一律拒绝：

```cpp
case ISD::SDIV:
case ISD::SREM:
case ISD::UREM:
  report_fatal_error("MCS251: signed division and remainder are not supported");
```

**文案缺陷**：`UREM` 是无符号取模，报错却说 "signed"。任何写了 `a % b`（哪怕全 unsigned）的 C 程序都会中止编译，且诊断误导排障方向。

- `llvm/include/llvm/IR/RuntimeLibcalls.td:2952-2960`——名字注册处（本 fork 的 RuntimeLibcallImpl 机制）：

```td
// The target's '_' prefix turns these C-level names into the SDCC-compatible
// __divuint/__divulong symbols. No other integer runtime helpers are shipped.
def mcs251_divuint : RuntimeLibcallImpl<UDIV_I16, "_divuint">;
def mcs251_divulong : RuntimeLibcallImpl<UDIV_I32, "_divulong">;
def IsMCS251 : LibcallPredicate<[{TT.getArch() == Triple::mcs251}]>;
def isMCS251 : RuntimeLibcallAvailability<(all_of IsMCS251)>;
def MCS251SystemLibrary : SystemRuntimeLibrary<isMCS251,
    (add mcs251_divuint, mcs251_divulong)>;
```

- 标准名对照（`RuntimeLibcalls.td:1140-1160`）：本 fork 的"LLVM 标准"实现名族是 `__divqi3/__divhi3/__divsi3`、`__udivqi3/__udivhi3/__udivsi3`、`__modqi3/__modhi3/__modsi3`、`__umodqi3/__umodhi3/__umodsi3`（qi=8/hi=16/si=32）。**注意 i16 的标准名是 `__udivhi3` 而不是 `__udivsi3`**——上游 gcc 语义里 si 是 32 位。

### 2.2 负例锁定（v2 修正计数）

`llvm/test/CodeGen/MCS251/divrem-errors.ll`【S，2026-09-07 亲读】实为：

- **9 个 IR 片段**：sdiv8/16/32、urem8/16/32、srem8/16/32；
- **10 条负 RUN**（`not --crash llc` + 同一 FileCheck 断言）：sdiv8×(O0,O2)、sdiv16×O0、sdiv32×O2、urem8×O0、urem16×O2、urem32×O0、srem8×O2、srem16×O0、srem32×O2；
- **无 i64 片段**。

v1 "18 条 `not --crash`" 的说法有误。本设计实施后该文件整体翻转：三种操作 i8/i16/i32 放行后 **10 条负 RUN 全部翻正，无一条保留**；替换后的正例目标 = 9 片段 × 两优化级 = **18 条正 RUN**（§8.1）。

### 2.3 正例基线

`llvm/test/CodeGen/MCS251/udiv.ll`（编译级，不链接不执行）已锁定：

- libcall 形状：`ecall __divuint` / `ecall __divulong`、`__divuint_PARM_2`/`__divulong_PARM_2` 静态槽、`eret` 收尾；
- 注释明确双下划线来源："exactly two underscores (the target's global prefix is applied once)"；
- 常数除数在 DAG 折叠（/1、/65535、/2^31、/10 均不发 ecall）；
- 除零：`:78` 现文案 "Division by zero is poison; no trap or helper contract is promised"（`CHECK-NOT: trap`）。**v2 列入更正**：按 LangRef，除数零是 **undefined behavior**（不是 poison）；"无 trap/helper 契约"的断言本身保留，注释措辞待随本设计实施一并更正（§8.1/§8.3）。

---

## 3. 证据基线（已核实；v2 补证据分级与新增行）

| # | 事实 | 出处 | 分级 |
|---|---|---|---|
| E1 | UDIV i8 Promote→i16，i16/i32 LibCall → `_divuint`/`_divulong` | `MCS251ISelLowering.cpp:56-61`；`RuntimeLibcalls.td:2955-2956` | 【S】 |
| E2 | SDIV/SREM/UREM 全宽 Custom，LowerOperation 一律 fatal error；UREM 也在其中 | `MCS251ISelLowering.cpp:62-64`、`:226-229` | 【S】 |
| E3 | i32 ABI 字节序 = DPL,DPH,B,A 低字节在前；`CallingConv.td` 头注自述该结论来自历史硬件/工具链实测（SDCC 4.6.0 + sdas251 + QEMU stc32），本轮未复测——按存档对待 | `MCS251CallingConv.td:3-27`；`MCS251ISelLowering.cpp:471-477`（I32ABIRegs） | 【S 存档 + Q】 |
| E4 | i16 ABI = DPL(低):DPH(高)，即 DPTR；i8 = DPL | `MCS251CallingConv.td:10-15` | 【S 存档 + Q】 |
| E5 | 第二及以后参数走被调方静态槽 `_FUNCNAME_PARM_n`（不在寄存器、不在栈）；`\1` 前缀防二次重整 | `MCS251CallingConv.td:17-20`；`MCS251ISelLowering.cpp:1627-1633`（parameterSlot）、`:1827-1836`（槽存储） | 【S】 |
| E6 | 内存多字节大端（+0 = MSB）：i32 槽存储 = `splitI32ToBytes` 后 `std::reverse`；i16 槽 = 先 hi 后 lo 两个 8 位 store | `MCS251ISelLowering.cpp:1409-1425` | 【S】 |
| E7 | 调用收敛：CALLSEQ 0 尺寸 + glue 链防独立 libcall 槽存储交错；regmask = CSR_MCS251，**可分配寄存器全 caller-saved，仅 DR60(SPX) 跨调用存活**。注意：CALLSEQ 零尺寸 ≠ 被调方无栈帧 | `MCS251ISelLowering.cpp:1743-1762` 注释；`MCS251RegisterInfo.cpp:24-46`、`MCS251RegisterInfo.td:250-259` | 【S】 |
| E8 | 可分配寄存器 = r0-r9, r12-r15（r10/r11 别名 B/A 不分配）；保留 = DR16/DR56/DR60、DPL/DPH/DPTR | `MCS251RegisterInfo.td:50-56`、`:193-211`；`MCS251RegisterInfo.cpp:48-70` | 【S】 |
| E9 | SDCC mcs251-large/smoke 构建树已产出全部 13 个 div/mod 族 `.rel`：`_divuint/_divulong/_divsint/_divslong/_divulonglong/_divslonglong/_moduint/_modulong/_modsint/_modslong/_modulonglong/_modslonglong/_fsdiv` | `sdcc-upstream/build-smoke/device/lib/mcs251-large/`（ls 证实） | 【S】 |
| E10 | **i8 没有任何 SDCC 符号**：mcs251-large 构建无 `_divuchar/_divschar/_moduchar/_modschar`（含 "char" 的只有浮点转换） | 同上目录 grep；`device/lib/_divuchar.c` 等源码存在但 mcs251 构建未产出 | 【S】 |
| E11 | 上游官方 mcs251 库归档清单不含除法对象（`device/lib/mcs251/Makefile.in` OBJ 仅 crt*/atomic 等），所以才有 provider.asm 的手工 harvest | `sdcc-upstream/device/lib/mcs251/Makefile.in:16-24`；`validation/mcs251-firmware/provider.asm:56-79` | 【S】 |
| E12 | provider.asm = harvest 的 SDCC 4.6.0 编译产物，定义 `__divuint`/`__divuint_PARM_2`/`__mulint` 等；注释确认 "arg1 in dptr (dph=hi, dpl=lo), arg2 in static `__<fn>_PARM_2` (big-endian)"、"ECALL pushes the three-byte return frame; return with ERET, not RET"；harvest 槽放 OSEG(OVR,DATA)，`__mulint` 入口 `mov (a+1),dpl / mov a,dph` 即大端+DPL=LSB 直证 | `validation/mcs251-firmware/provider.asm:56-90,113-200`（槽区 :113-123、`__mulint` :131-134） | 【S】 |
| E13 | 真正跑通过 udiv QEMU 执行的只有旧 harness 链：`mcs251-demo-test` 每个用例都链 `provider.rel`；t1/temperature-lookup（161 项表 + 运行期除法插值）在 `post-ec171ddee+muldiv` llc 上"mul/div 已解决（2026-09-06）"，DUT 被独立的 ro-align 限制挡住（PM 裁定不抹除不伪造） | `validation/mcs251-demo-test/run-tests.py:104-164`；`validation/mcs251-demo-test/t1/temperature-lookup/EXTRACT.md` | 【Q】 |
| E14 | demo-modern 链只拼 crt+main+features；`build/features.ll`、`build/main.ll` 中 **udiv 计数为 0**（`60000u/7u` 在 -O2 折叠）→ demo-modern 从未链过 udiv | `validation/mcs251-demo-modern/Makefile:118-146`；`grep -c udiv build/*.ll` = 0 | 【S】 |
| E15 | ELF 方向：E1 提案已定 ELF32/MSB/RELA、`EM_MCS251=0x9999`、`llc -mcs251-object-format=elf|rel`（默认 rel）、lld/MCS251 专用 flavor、OSEG=每叶函数 NOBITS+`SHF_MCS251_OVERLAY`；**状态=待 PM 完整审批，不得进入 E2** | `validation/mcs251-elf/SPEC.md` 头部与第 1 节 | 【S】 |
| E16 | 三方对拍方法论已在用：Oracle-A = 宿主 gcc 真值；Oracle-B = 同 kernel.c 经 SDCC `--c1mode` 目标真值；DUT = fork 链；串口逐行比对 | `validation/mcs251-demo-test/run-tests.py:8-17,388-424` | 【S + Q】 |
| E17 | QEMU 运行器口径：新串口文件、完整终止行检测、超时/提前退出即 FAIL、stderr 归档不过滤 | `validation/mcs251-demo-modern/run-qemu.py`、Makefile `run/check` 目标 | 【S】 |
| E18 | **三层字节序并存**：① 寄存器内部 MSB 在低编号 R 槽（`WRk.SubRegs=[R{k+1},Rk]` ↔ `[sub_lo8,sub_hi8]`，sub_lo8 是 LSB）；② 调用 ABI DPL=LSB（`splitI32ToBytes` 把 sub_lo8 链放 DPL；COPY 展开 sub_lo8→dpl 注释直书）；③ 内存/槽大端（E6/E12） | `MCS251RegisterInfo.td:18-19,41-44`；`MCS251ISelLowering.cpp:471-507,1840-1846`；`MCS251InstrInfo.cpp:173-182,212-220` | 【S】 |
| E19 | **SDCC 大端直证**：`_divuint.asm:119-128` `mov r7,dph / mov a,dpl` 后 x+1←DPL、x+0←DPH；`_divulong.asm:119-139` `r7=dpl,r6=dph,r5=b,r4=a` 后 x[3]←r7…x[0]←r4 | `sdcc-upstream/build-smoke/device/lib/mcs251-large/_divuint.asm`、`_divulong.asm` | 【S】 |
| E20 | **SDCC 有符号包装层用栈**：`_divsint.asm:180-184` `push ar3 / ecall __divuint / pop ar3`——"零栈使用"不成立 | `sdcc-upstream/build-smoke/device/lib/mcs251-large/_divsint.asm` | 【S】 |
| E21 | **SDCC large 槽在 XSEG**：`.area XSEG (XDATA)` + `dpxl` 扩展指针访问 PARM 槽——SDCC large 布局**不证明**同址 overlay（同址注释只存在于 mcs51 small asm 路径源注释里） | `_divsint.asm:55` 及全文件；`_divsint.c:66-67`（mcs51 asm 路径注释） | 【S】 |
| E22 | **SDCC 有符号包装层源码含 `/`、`%` 与 signed 取负**：`_divsint.c:211` `r = (unsigned int)(x<0?-x:x) / (unsigned int)(y<0?-y:y)`；`_modsint.c:207` 同构用 `%`——"原八源无除法"不成立 | `sdcc-upstream/device/lib/_divsint.c`、`_modsint.c` | 【S】 |
| E23 | AsmPrinter 叶/非叶判定：函数体内出现 `MI.isCall()` 或 inline-asm 即非叶；叶槽落 `.mcs251.OSEG.*`（ELF 带 `SHF_MCS251_OVERLAY`），非叶槽落 `.mcs251.DSEG.*` 独立 NOBITS 节 | `MCS251AsmPrinter.cpp:206-248` | 【S】 |
| E24 | ELF 链接器只收 ELF32BE 单对象（`loadFile` 逐文件 `createObjectFile` + `ELF32BEObjectFile` dyn_cast + e_flags 校验），**不认 .a**；`.note.mcs251.abi` 恰好一个、8 字段描述符逐字校验 | `lld/MCS251/LinkerCore.cpp:270-285,287-303` | 【S】 |
| E25 | SPEC 明确排除 archive 懒提取等；等价域扩大必须先修规范并报 PM；OSEG 规范语义 = 全链接所有 `.mcs251.OSEG.*` 同一 overlay 组、组内同址、组占 max；非叶槽独立 DSEG slice 禁 overlay | `validation/mcs251-elf/SPEC.md:84-88,176-192,246-258,281-283` | 【S】 |
| E26 | v2 模型裁定：leaf OSEG 与 non-leaf DSEG 区别保留；参数槽迁 EDATA 须 section 放置能力显式声明且全部访问经可重定位间接路线；overlay 占用按分配组计一次 | `validation/mcs251-models/DESIGN.md:1155-1166` | 【S】 |
| E27 | spill/栈帧存在：`spill-across-call.ll`、`frame.ll`/`frame-o0.ll` 锁定 -O0 spill 经栈槽跨调用存活——被调方（含库函数）可能面对真实栈帧 | `llvm/test/CodeGen/MCS251/spill-across-call.ll`、`frame*.ll` | 【S】 |
| E28 | **driver 默认物化 v2 契约（v3 新增）**：`--target=mcs251-unknown-none` 不带 model 旗标时 driver 默认 `-mcs251-memory-model=xsmall` → cc1 参数 `-mcs251-memory-contract=1,2,32,8,1`（五字段 = transport 1 / layout V2 / AS0 32bit / placement 8 / exec 1）；lit 锁定"默认 + 三种显式模型"的契约映射（**不是四个不同模型**，v4 措辞修正） | `clang/lib/Driver/ToolChains/Clang.cpp:1550-1574`；`clang/test/Driver/mcs251.c:16-26` | 【S】 |
| E29 | **前端/后端 DataLayout 串不同、且均合法含 `i64:8`（v3 新增）**：cc1 默认（契约 `1,2,32,8,1`）产 v2 布局串 `E-m:s-p:32:8:8:32-p1:…-P4-A0-G0`；`llc -mtriple=mcs251` 的 TargetMachine 构造取 `TT.computeDataLayout()` = compat 布局串 `E-m:s-p:32:8-i8:8-…-S8`——审计以字符串全文搜索 `i64` 会命中合法 DataLayout | `clang/test/CodeGen/mcs251.c:10-11`；`clang/lib/Basic/Targets/MCS251.cpp:56-59`；`llvm/lib/TargetParser/TargetDataLayout.cpp:604-608`；`llvm/lib/Target/MCS251/MCS251TargetMachine.cpp:39` | 【S】 |
| E30 | **DPTR 物理别名 DR56（v3 新增）**："dptr aliases the low 16 bits of dr56 (dpx)"，该别名刻意不在寄存器文件建模（两寄存器均 reserved、无分配冲突）；DPL/DPH 是参数与返回值的唯一合法通道，**写入**经别名改变 DR56 对应位；**读取本身不改写** DR56（v4 措辞修正） | `llvm/lib/Target/MCS251/MCS251RegisterInfo.td:129-131` | 【S】 |
| E31 | **帧模型与 DR16/DPX 取舍（v3 新增）**：栈向上生长，ECALL 已压 3B 返回帧，eret 弹 [SPX-2..SPX]、SPX 必须逐位恢复；动态 alloca 函数 hasFP、DR16 锚、epilogue RESTORESP/POPFP；帧锚不用 DPX/DR56 的官方理由 = "DPL/DPH alias it and both argument reads and call-result writes would destroy a DPX anchor" | `llvm/lib/Target/MCS251/MCS251FrameLowering.cpp:3-25,88-96,123-139` | 【S】 |

---

## 4. SDCC 参考实现盘点（mcs251 真实入口约定）

> 证据主体：`C:\Prj\LLVM\MCS251\sdcc-upstream\build-smoke\device\lib\mcs251-large\*.asm`（SDCC 4.6.0 编译产物，optsdcc 行 = `stc32-mcs251 abi-major=1 abi-minor=0 ... reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15`，与 `provider.asm`/`demo.lk.in` 的 `-A` 行逐字相同）。`device/lib/_divuint.c` 等源码里的手写 `__asm` 快路径全部只对 `__SDCC_mcs51` 生效；**mcs251 拿到的是 `#else` 分支的纯 C 实现**，再由 SDCC 编成 .asm——所以 C 源码就是 mcs251 的权威逻辑参考（**v2 措辞：参考与 oracle，不是文本底稿**，§10-Q1）。

### 4.1 符号存在性矩阵

| C 级符号（SDCC 名） | i16/i32 | mcs251-large 构建 | 逻辑参考 |
|---|---|---|---|
| `_divuint` | i16 | `_divuint.rel` 存在 | `device/lib/_divuint.c` C 兜底（16 次移位-减法） |
| `_divulong` | i32 | 存在 | `device/lib/_divulong.c` C 兜底（32 次） |
| `_divsint` | i16 | 存在 | `device/lib/_divsint.c`：符号归一后调 `_divuint`，结果按需取负（**源码用 `/` 与 signed 取负，E22——不可照搬**） |
| `_divslong` | i32 | 存在 | `device/lib/_divslong.c` 同构 |
| `_moduint` | i16 | 存在 | `device/lib/_moduint.c`：独立移位-比较取余循环 |
| `_modulong` | i32 | 存在 | `device/lib/_modulong.c` |
| `_modsint` | i16 | 存在 | `device/lib/_modsint.c`：绝对值调 `_moduint`，**余数符号跟随被除数**（C99 截断除法，与 LLVM srem 语义一致；**源码用 `%`，E22**） |
| `_modslong` | i32 | 存在 | `device/lib/_modslong.c` |
| `_divulonglong` 等 64 位 | i64 | 存在 | 本期不需要（MCS251 无 i64 合法化，§10-Q8） |
| i8 族（`_divuchar` 等） | i8 | **不存在** | SDCC 前端把 char 算术提升到 int——本链同样应 Promote（§7.2） |

**v2 更正**：v1 声称"8 源实现本身不含除法"。事实上四个**无符号/取余**助手的 C 兜底是移位/比较/加减（无 `/` `%`）；但四个**有符号包装层**源码含 `/`、`%` 和 signed 一元取负（E22）。移植时包装层必须按 §6.2 重写（无符号域归一化 + 显式 helper 调用），并以 IR 审计闭环兜底。

### 4.2 入口约定：三层字节序叙事（v2 按先决裁定重写）

> v1 在此写"实测推翻任务简报的 MSB-in-lower-slot 假设"。该翻案叙事不成立：三种规则各管一层、并存不悖。以下逐层给出证据与作用域。

**层① WR/DR 通用寄存器内部：MSB 在低编号 R 槽**【S】

`MCS251RegisterInfo.td:18-19`：`WRk.SubRegs = [R{k+1}, Rk]`，配对 `[sub_lo8, sub_hi8]`；`:41-44` 定义 `sub_lo8` 是 bit0 起 8 位（LSB）。即 WRk 的 **LSB 在 R{k+1}、MSB 在 Rk（低编号）**。这只规定"一个 16/32 位寄存器值在通用寄存器组内部怎么摆"，与调用入口无关。

**层② 调用 ABI 按 DPL/DPH/B/A 枚举：DPL 装 LSB**【S + Q 存档】

- `MCS251ISelLowering.cpp:471-477`：注释 "i32 ABI byte-register order is least-significant byte first: DPL, DPH, B, A" + `I32ABIRegs[] = {DPL, DPH, B, A}`；
- `:479-507` `splitI32ToBytes`：值先拆 lo16/hi16，再各拆 `sub_lo8`（低字节）与 `sub_hi8`（高字节），按下标 0..3 = lo 的低字节、lo 的高字节、hi 的低字节、hi 的高字节依次装入 `I32ABIRegs`——**DPL 拿到全值的 LSB**；
- `:1840-1846`（LowerCall 入参）：`splitI32ToBytes(OutVals[0])` 后逐字节 `RegsToPass.emplace_back(I32ABIRegs[I], Parts[I])`；
- `MCS251InstrInfo.cpp:173-182`：`DPTR ← GPR16` COPY 展开为 `MOV8dpl sub_lo8 / MOV8dph sub_hi8`，注释 "Per the byte order ruling sub_lo8 is the least significant byte, which the ABI expects in dpl"；`:212-220` 读方向同构；
- `MCS251CallingConv.td:3-27` 头注（自述来自历史 SDCC 4.6.0 + QEMU 实测，存档按【Q】对待）与 E13 temperature-lookup 的 udiv 通过记录互为印证。

**层③ 内存与 `_PARM_n` 静态槽：大端（+0 = MSB）**【S】

- 本链：`MCS251ISelLowering.cpp:1409-1425`——i32 槽存储 = `splitI32ToBytes` 后 `std::reverse`（+0 放原 Parts[3]，即 MSB）；i16 槽 = 先 hi 后 lo；
- SDCC 产物：`_divuint.asm:119-128`——入口 `mov r7,dph / mov a,dpl`，随后 `x+1 ← DPL、x+0 ← DPH`（DPL=LSB 落高地址）；`_divulong.asm:119-139`——`r7=dpl(→x[3]), r6=dph(→x[2]), r5=b(→x[1]), r4=a(→x[0] MSB)`；PARM_2 读取同为 `+1` 与 `+0` 两字节、`+0` 为高字节；
- harvest：`provider.asm:113-123` 槽区注释 "(big-endian)"；`:131-134` `__mulint` 入口 `mov (a+1),dpl / mov a,dph`。

**三层合成**：一个 i32 值 `0x11223344` 在寄存器内部按 R 编号递增摆放为 `11 22 33 44`（MSB=0x11 在最低编号 R 槽：DR 拆 `sub_hi16=0x1122→WRk`、`sub_lo16=0x3344→WR{k+2}`，WR 内再拆 `sub_hi8 在低编号 R`）；在调用 ABI 里按 `DPL=0x44, DPH=0x33, B=0x22, A=0x11` 传递（LSB 在前）；写入内存/槽是 `+0=0x11 … +3=0x44`（+0=MSB，大端）。**三层各管一层、互不矛盾，也无"翻案"**：v1 把层②单独抽出来当成对全局的否定，才是错误来源。

**`_divsint` 包装层（编译产物）**——`_divsint.asm:114-216`：读 `dpl/dph` 与 `__divsint_PARM_2` → 被除数/除数分别取绝对值（PSW 位暂存符号）→ 把 y 写入 `__divuint_PARM_2` → `push ar3` + `ecall __divuint` + `pop ar3`（**:180-184，栈使用实锤，E20**）→ 按符号积条件取负 → `eret`。`_modsint` 同构（`_modsint.asm:167-206`，余号随被除数）。

**overlay 同址问题（v2 更正）**：v1 称"`__divsint_PARM_2` 与 `__divuint_PARM_2` 在 SDCC 里是 overlay 同址"。证据不支持：SDCC large 产物的槽在 **XSEG（XDATA，`dpxl` 访问）**，XSEG 无 overlay 语义（E21）；同址注释只存在于 mcs51 small asm 路径的源注释。harvest `provider.asm` 把槽放 OSEG(OVR) 是 validation 侧的布局选择。**正确性不依赖 overlay**：包装层先完整读己槽、再写 unsigned 槽、然后才调用，两种布局下都对。

### 4.3 对本链 ABI 的结论

1. 本链（fork 后端 + provider.asm harvest 体）与 SDCC mcs251-large 构建产物在**寄存器约定上已经同构**——udiv 能通过 temperature-lookup 验收（E13【Q】）本身就是跨实现的一致性证据。
2. 新增 6 个符号不需要任何新约定：同参数位置、同槽命名法（`_FUNCNAME_PARM_2`，含 `Mangler` 前缀一次 → `__moduint_PARM_2` 等）、同 `eret` 收尾。
3. 除零行为：SDCC C 兜底是确定性垃圾值（如 `_divuint` 除零得 0xFFFF，静态读数，未在 QEMU 复测）；LLVM IR 层除零是 UB。两者不冲突，但**对拍向量不得把除零行放进语义断言**（§8.3）。

---

## 5. 设计裁定 A：ELF 运行时库选址与源语言

### 5.1 源语言两案评估

**案 (a)：汇编源（".S"）——不推荐**

MCS251 后端**没有 AsmParser**（`llvm/lib/Target/MCS251/` 目录无 `*AsmParser*`，MC 层只有 AsmPrinter/AsmBackend/ObjWriter）。"用 LLVM-MC 语法的 .S 写运行时库"在本仓库是**空集**。案 (a) 落到实处的唯一形态是 ASXXXX 语法 `.asm` + `sdas251 -plosgffw` → `.rel`。真实代价：

1. **对象格式错位**：产物只能进 `mcs251_ld.py` 链；ELF 链（`-mcs251-object-format=elf` → lld MCS251，E15）吃不到它。双库并存或 ELF 库退回 .rel，都与 E1 方向冲突。
2. **两套汇编器语法**：ASXXXX（`.area/.globl/.ds`）与 LLVM-MC 是两种语言，维护者要同时会。
3. **收益几乎为零**：SDCC 的手写 asm 快路径本来就只服务于 mcs51；mcs251 的权威逻辑是 C 兜底。手写汇编只是把"SDCC 编译 C 的输出"抄一遍——provider.asm 已经证明 harvest 模式可行，但它是**过渡品**，v2 裁定后连 `.rel` 侧也不再扩容它（§10-Q4）。

**案 (b)：纯 C——裁定采纳**

- 来源：8 个函数的算法是公共领域知识（移位-减法/移位-比较），**源文本按公开算法独立实现**（§10-Q1）；SDCC 的 C 兜底与编译产物只作逻辑参考与语义 oracle。
- 符号自举性质（纯 C 案最关键的红利）：C 函数按 SDCC 命名法叫 `_divuint` 等，经本链后端编译时——本链 ABI 对**双参数 C 函数**的处理（参数 1 进 DPL/DPH/B/A，参数 2 存 `mangled(函数名)+"_PARM_2"` 静态槽，`MCS251ISelLowering.cpp:1627-1633`、`:1827-1836`）会**自动**生成 `__divuint`、`__divuint_PARM_2` 全套符号，与 libcall 期望逐字一致。**运行时库和它的调用方是同一个编译器、同一套 ABI，自洽由构造保证**，无需任何汇编胶水。**边界（v3）**：该自洽性只覆盖符号/约定的**生成**，不延伸为"编译器会因正在编译同名 helper 而避开 libcall"——不存在这种回避（裁定见 §6.2-2）：库体内若出现除法 IR 仍会映射回同名符号造成自递归，靠 §6.2-2/5 的闭环防线兜底。
- 质量口径：运行时函数只用已实证的特性（循环、移位、比较、静态数据、ecall/eret——temperature-lookup【Q】与现有 demo 全覆盖）；每个函数带自检向量（§8），自检码**只进验收固件、不进生产对象**（§10-Q3）。
- 无 UB 红线与禁令：见 §6.2（无符号域绝对值、显式 helper 调用、禁 i64、禁四种除取余 IR 指令）。
- 许可证：**已裁定**，见 §10-Q1——独立实现 + Apache-2.0 WITH LLVM-exception；SDCC 源/产物只作参考与 oracle；GPL-2.0-or-later + 链接例外不能概括豁免 LLVM 侧库；禁止逐行改写伪装独立实现。

### 5.2 选址：`llvm/lib/Target/MCS251/Runtime/`（裁定采纳）

| 候选 | 评估 |
|---|---|
| `llvm/lib/Target/MCS251/Runtime/`（采纳） | 源码随目标走：ABI 注释、libcall 名单、RuntimeLibcalls.td 引用都在同一目录视线内；lit 可直接引用源文件做"运行时可被 fork-clang 编译、IR 无除法无 i64"的守门测试；构建所有权与两阶段依赖安排见 §10-Q3/§11.2 |
| `validation/mcs251-runtime/` | 与现 harness 同居，起步快；但 validation 是试验/验收领地，生产运行时放这里会被当成又一份 provider 式临时品。**不采纳** |

组合：源码 + 构建进 `llvm/lib/Target/MCS251/Runtime/`；`validation/` 侧只放**集成验收**（对拍 kernel、向量表、QEMU 驱动，沿用 demo-modern 形态），构建产物（八个 ELF .o）由验收脚本按显式清单消费（§10-Q4）。

### 5.3 设计裁定 B：符号命名终裁

- **SDCC 名（终裁）**：`_divsint/_divslong/_moduint/_modulong/_modsint/_modslong`（+已有 `_divuint/_divulong`）。经目标 `_` 前缀后成为 `__divsint` 等，与 SDCC/harvest 库、`provider.asm`、temperature-lookup 历史验收（E13【Q】）全部直接互链。
- **LLVM 标准名（不采纳）**：本 fork 的标准实现名族是 `__divhi3/__udivhi3/__modhi3/__umodhi3`（i16）与 `__divsi3/__udivsi3/__modsi3/__umodsi3`（i32）（`RuntimeLibcalls.td:1140-1160`）。注意：(1) i16 标准名是 **hi3** 不是 si3；(2) 上游化（给 LLVM 主线提 MCS251）时标准名才有叙事价值，而主线当下根本没有 MCS251 目标。
- **终裁理由**：
  1. **互操作优先**：ABI v1 的定位就是 SDCC 兼容（`-A` 行逐字对齐、mcs251_ld.py 严格校验）。SDCC 名是唯一能让"LLVM 编译的模块直接链 SDCC/harvest 库"的选择；改名等于单方面断桥。
  2. **机制零成本**：本 fork 的 libcall 管线（RuntimeLibcallImpl + 前缀 + `\1` 槽命名法）就是为 SDCC 名搭的；新增 6 条 `def mcs251_* : RuntimeLibcallImpl<...>` + 追加进 `MCS251SystemLibrary` 即完成名字注册，`setLibcallImpl` 一行一条。
  3. **改名成本清单**（若将来倒向标准名）：6+2 条 `setLibcallName`/impl 重定向；`udiv.ll` 全部 CHECK 重写；`provider.asm` 符号重命名或新增转发体；所有已归档 QEMU 验收（temperature-lookup 等）因二进制符号层变化需重跑；混链期 SDCC 目标程序全部断。**现在不付；将来若付也是一次整体迁移（上游化里程碑，§10-Q5），不为 i16/i32 命名不统一的中间态买单。**
  4. 混链期口径（v2 收窄）：SDCC 名不变；**provider 不扩容**（§10-Q4），ELF 链由 `mcs251rt` 供给全部八符号；两链符号面在 udiv 上相同，验收脚本可在同一 kernel 上切链不改源。

### 5.4 宽度 × 符号总矩阵

| 宽度 | LLVM op | RTLIB 枚举 | 后端 action | SDCC 符号 | SDCC 现成逻辑 | 处置 |
|---|---|---|---|---|---|---|
| i8 | udiv | UDIV_I8 | **Promote→i16**（现状已是） | —（无 i8 符号，E10） | 无 | 复用 i16 行 |
| i8 | sdiv | SDIV_I8 | **Promote→i16**（新增） | — | 无 | 复用 i16 行 |
| i8 | urem | UREM_I8 | **Promote→i16**（新增） | — | 无 | 复用 i16 行 |
| i8 | srem | SREM_I8 | **Promote→i16**（新增） | — | 无 | 复用 i16 行 |
| i16 | udiv | UDIV_I16 | LibCall（现状已是） | `_divuint` | 有 | 已接 |
| i16 | sdiv | SDIV_I16 | **LibCall**（新增） | `_divsint` | 有 | 新接 + 移植 |
| i16 | urem | UREM_I16 | **LibCall**（新增） | `_moduint` | 有 | 新接 + 移植 |
| i16 | srem | SREM_I16 | **LibCall**（新增） | `_modsint` | 有 | 新接 + 移植 |
| i32 | udiv | UDIV_I32 | LibCall（现状已是） | `_divulong` | 有 | 已接 |
| i32 | sdiv | SDIV_I32 | **LibCall**（新增） | `_divslong` | 有 | 新接 + 移植 |
| i32 | urem | UREM_I32 | **LibCall**（新增） | `_modulong` | 有 | 新接 + 移植 |
| i32 | srem | SREM_I32 | **LibCall**（新增） | `_modslong` | 有 | 新接 + 移植 |

**结论：必须新写的只有运行时库本体（8 个 ELF 版函数，独立实现）；后端补 6 条接线 + 3 条 i8 Promote；没有任何宽度需要发明新算法（移位-减法是公共领域知识），但源文本必须独立实现（§10-Q1）且满足 §6.2 无 UB 红线。** `UDIVREM/SDIVREM` 维持 Expand（LegalizeDAG 拆成 udiv+urem 两个节点 → 两次 libcall）；`a/b` 与 `a%b` 同现时是两次调用、不共享循环——SDCC 也无融合除取余助手，本期接受此代价（§10-Q6）。

---

## 6. 冻结签名与逐 libcall ABI 表

### 6.1 冻结的八个精确宽度签名（v2 新增，验收硬约束）

**背景**：本 fork clang 的 `int`/`long` = 32 位、`short` = 16 位。SDCC 源的 `unsigned int _divuint(unsigned, unsigned)` 中 `unsigned` 是 **16 位**；把该签名原样搬进本 fork 会得到 32 位参数/返回/槽宽的**错误接口**（PARM 槽 4B 而非 2B、占用 DPL/DPH/B/A 四寄存器）。因此签名必须逐函数钉死宽度：

```c
/* llvm/lib/Target/MCS251/Runtime/ —— 构建契约按 §6.1 冻结清单固定：
   triple + -mcs251-memory-contract=1,2,32,8,1 + 两串 DataLayout + 优化级 */
#include <stdint.h>

_Static_assert(sizeof(int16_t)  == 2, "MCS251 runtime: int16_t must be 2 bytes");
_Static_assert(sizeof(uint16_t) == 2, "MCS251 runtime: uint16_t must be 2 bytes");
_Static_assert(sizeof(int32_t)  == 4, "MCS251 runtime: int32_t must be 4 bytes");
_Static_assert(sizeof(uint32_t) == 4, "MCS251 runtime: uint32_t must be 4 bytes");

uint16_t _divuint (uint16_t x, uint16_t y);   /* -> IR: define i16 @_divuint(i16, i16)  */
uint32_t _divulong(uint32_t x, uint32_t y);   /* -> IR: define i32 @_divulong(i32, i32)  */
int16_t  _divsint (int16_t  x, int16_t  y);   /* -> IR: define i16 @_divsint(i16, i16)   */
int32_t  _divslong(int32_t  x, int32_t  y);   /* -> IR: define i32 @_divslong(i32, i32)  */
uint16_t _moduint (uint16_t x, uint16_t y);   /* -> IR: define i16 @_moduint(i16, i16)   */
uint32_t _modulong(uint32_t x, uint32_t y);   /* -> IR: define i32 @_modulong(i32, i32)  */
int16_t  _modsint (int16_t  x, int16_t  y);   /* -> IR: define i16 @_modsint(i16, i16)   */
int32_t  _modslong(int32_t  x, int32_t  y);   /* -> IR: define i32 @_modslong(i32, i32)  */
```

配套要求：

- **不依赖裸 `int`/`long`/`unsigned`** 做任何接口或中间变量（i16 尤其不能靠裸 int"碰巧"对）；实现内局部变量只用四种定宽类型。
- **静态宽度断言**（如上四条）编译进每个源文件；构建脚本同时断言前端固定选项（§11.2）。
- **IR 签名验收**：每对象的 `.ll`/`.o` 必须逐字匹配上表注释列的 IR 签名（函数名经目标 `_` 前缀成 `__divuint` 等；PARM 槽宽度 2B/4B 对应 i16/i32）——纳入 §8.6 对象级验收。
- **前端/构建管道固定（v3 修正，不留空占位）**："裸 triple" 不构成冻结契约——当前树 driver 默认已物化 xsmall/v2 契约（E28）。冻结清单逐项钉死并归档，任何一项变化 = 签名/ABI 重验收（触发 §8.8）：
  1. **前端命令与契约**：`clang --target=mcs251-unknown-none`，driver 默认 `-mcs251-memory-model=xsmall` → cc1 `-mcs251-memory-contract=1,2,32,8,1`（transport 1 / layout V2 / AS0 32bit / placement 8 / exec 1，E28）；**整数模型**：short=16、int=long=32（与 §6.1 签名宽度互锁；`uint16_t` 参与运算即提升到 32 位，见 §6.2-3）；
  2. **cc1 DataLayout**（v2 布局串，E29，以构建归档的 IR 头逐字为准）：`E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0`；
  3. **llc 侧 TargetMachine DataLayout**（compat 布局串，E29：`MCS251TargetMachine.cpp:39` 经 `TT.computeDataLayout()` 取得，与 cc1 串**不同**——两串都归档进构建记录；IR 审计作用在"实际送入 llc 的那份 IR"上，§11.2）：`E-m:s-p:32:8-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8`；
  4. **优化级**：前端 `-O2`、`llc -O2`（记录进构建归档；两段任一变化 = §8.6 + §8.7 + 语义对拍全量重跑）。
  5. **结果恢复转换约定（v4 补入：v3 只留"已纳入本清单"的交叉引用而清单缺项，本项闭合）**。适用对象：编译八源所用的**冻结 Clang 身份**（§11.2 第一阶段产物；clang 版本号 + 构建 triple/哈希随构建记录归档，验收以归档身份为准）与第 1 项整数模型（short=16、int=long=32）。**约定本体**：N=16/32 的**超范围无符号→有符号转换保留低 N 位、按对应有符号类型（二补码）解释**（即模 2^N 截断——C11 6.3.1.3p3 实现定义行为，本链固定选用该行为）。范围区分：转换到**无符号**类型的取模是 C 标准保证（§6.2-1 的归一化步），**不属本项**；**超范围转换到有符号类型才是本项记录的实现定义依赖**（§6.2-4）。可检查配套项：
     - **源码注释要求**：八源中每处无符号→有符号恢复转换点须带注释，声明"依赖冻结工具链的模 2^N 转换行为（C11 6.3.1.3p3 实现定义），非可移植性质"；
     - **转换检查方式**：§11.2 第 6 项构建审计核对实际送 llc IR 的恢复点形状（同宽 unsigned→signed 转换位型直通、取负在无符号域完成、无超预期的扩展/截断链）；行为面由合法负结果回归项在 QEMU 对拍中验证（§8.2/§8.4/§8.7）；
     - **合法负结果回归项**：§8.4 边界表全部负结果行（含 `-7/2`→-3、`-7%2`→-1、`INT16_MIN/1`→-32768 三个代表值）+ 固定种子 16 对随机向量中的负结果对，强制执行并归档；
     - **重验触发**：工具链身份（clang/llc）变化或第 1-4 项任一编译条件变化 → 本约定重验（转换检查 + 负结果回归项全量重跑），随 §8.8/§11.2 归档更新。

### 6.2 无 UB 算法要求（v2 新增，实现红线）

1. **绝对值在无符号域构造**。SDCC 写法 `(unsigned int)(x < 0 ? -x : x)` 是**先在 signed 域取负再转换**——`x == INT16_MIN` 时 `-x` 就是 C UB。新算法必须：
   ```c
   uint16_t ux = (uint16_t)x;            /* 转换取模，处处有定义 */
   if (x < 0) ux = (uint16_t)(0u - ux);  /* 无符号域取负，处处有定义 */
   ```
   （`0u - ux` 按提升规则在 32 位 unsigned int 域计算后截回 16 位，模 2^16，含 `x == INT16_MIN` 全域有定义。）i32 同构（`0ul - ux` 或 `0u - ux` 按 uint32_t 实际类型定）。**依赖链（v4 修正口径）**：本步全域有定义依赖 §6.1 冻结清单内的整数模型——本 fork 冻结 `int`=32（决定提升域宽与 32 位无符号运算）；本步的 `(uint16_t)`/`(uint32_t)` 转换是**到无符号类型的取模**，C 标准保证、处处有定义，**不是**实现定义依赖（v3 把它列作"冻结 Clang 行为"依赖过宽——需按实现定义记录的仅是超范围转换到**有符号**类型，见 §6.2-4 与 §6.1 第 5 项）。整数模型任一变化即连带重验（§11.2）。
2. **显式调用 unsigned helper（v3 精确化）**。包装层（`_divsint/_divslong/_modsint/_modslong`）对归一化后的无符号值**显式调用各自对应的 unsigned helper**（`_divuint/_divulong/_moduint/_modulong`，同文件 extern 原型、精确宽度）。
   - **自递归裁定（v3 写入）**：编译器**不会**因"正在编译同名 helper"而避开该 libcall——TargetLowering 选 libcall 的依据是 IR 操作与宽度，与被编译函数叫什么无关。`__divuint` 函数体内若存活一个 `udiv i16`（无论来自源码 `/`、内建还是优化器综合），legalizer 会原样把它映射回 `_divuint` → `__divuint`，即**自递归**；本目标关普通尾调用降级，该调用是**真实自身 ECALL**：每层压新的 3B 返回帧（栈持续生长直至耗尽 §6.3 门禁余量），且每层重写同一静态槽 `__divuint_PARM_2`——非重入契约（§6.3）下等于数据自毁。
   - **危险取决于 IR 操作与宽度、以及映射目标与当前函数的关系，不是"包装层里出现除法"**（v2 "包装层出现 / 就自递归"过笼，已修正；v4 再删一处错误因果结论）。libcall 映射由**操作种类与实际宽度**决定，与被编译函数叫什么无关；**映射目标等于当前函数时才构成自身调用**：
     - `_divsint` 调 `_divuint` 是**设计要求的调用边**，不是危险；
     - **构成自递归的形状 = 映射目标等于当前函数**：`__divuint` 体内存活的 `udiv i16` 落 `_divuint`、`__divsint` 体内的 `sdiv i16` 落 `_divsint`、`__modsint` 体内的 `srem i16` 落 `_modsint`、`__moduint` 体内的 `urem i16` 落 `_moduint`（i32 同构）——本目标关普通尾调用降级，是**真实自身 ECALL**，后果见上条裁定；
     - **映射到另一 helper 不是自递归，也不单凭宽度不同判定错译**（**v4 删除 v3 错误推论"udiv i32 落 `_divulong` 不是 `_divuint`（错宽供给 → 静默错译）"**）：映射到另一 helper 可能形成**非预期依赖**或**违反本方案固定调用图**（§6.2-5 的机器级调用图核对与八源禁令负责拦截），但对合法除数其商可以是对的——例：两 i16 操作数零扩展到 i32、经正确的 i32 无符号除法、再截回 i16，合法输入下商正确；若主张签名/槽宽/调用约定失配造成错译，须另行给出该失配在具体调用序列中的依据，不得由宽度差异直接推得；
     - **精确原型签名的价值 = 固定包装层→unsigned helper 的调用边；它不替代 unsigned helper 自身的无除法审计**（见第 5 条——八源禁除取余 IR、固定调用图、IR 审计与机器级调用图核对等红线全部保留，本条口径修正不放松任何红线）。
3. **提升/移位宽度纪律（v3 修正：按提升后的实际左操作数类型规定移位）**。本 fork `int`=32：所有 `uint16_t/int16_t` 运算数先提升为 32 位 `int`/`unsigned int` 再运算。规定：
   - 每步算术结果显式截回目标定宽类型（无符号转换取模，处处有定义）；
   - **移位红线必须检查整数提升后的实际左操作数类型**：声明为 `uint16_t` 的变量进入移位表达式时**已提升为 signed int**——"移位在无符号域"必须靠**移位前**的显式转换达成，写作 `((unsigned)ux) >> n` / `((unsigned)ux) << n`（i32 助手操作数 `uint32_t` = unsigned int，提升后类型不变，仍写显式转换以防御整型模型未来变化）；**靠事后截断补救不合规**——被移出的位已在错误的域里丢失或已成未定义行为，截不回来；
   - 移位计数 n < 32（按转换后的实际域宽检查），算法侧保持每轮至多移 1 位的循环不变量；禁止对 signed 值左移/右移进符号逻辑；比较前不做隐式混号运算。
4. **结果恢复（v3 修正证明与口径）**。商/余取负在**无符号域**完成后经显式转换回有符号类型。**v2 论断作废**："超范围转换仅有 `0x8000/0x80000000` 且只来自 `(MIN,-1)`"是错的——超 int16/int32 **正数范围**的无符号→有符号转换出现在**每一个负结果**上，且全部是合法输入：`-7/2`→-3（无符号位型 0xFFFD）、`-7%2`→-1（0xFFFF）、`INT16_MIN/1`→-32768（0x8000）。修正后的口径（采纳修法①，不另选恢复法）：
   - C 标准把超范围无符号→有符号转换定为**实现定义**（C11 6.3.1.3p3），**不是 UB**；库自身无 UB 的结论不变。区分（v4）：转换到**无符号**类型的取模是 C 标准保证（§6.2-1 的归一化步不受影响）；**超范围转换到有符号类型才是需记录的实现定义依赖**，且它出现在每一个负结果上。
   - **实现定义依赖显式记录（v4：清单闭合 + 论证/验收分层）**：本算法依赖本 fork 冻结 Clang 对超范围无符号→有符号转换的**模 2^N 截断**行为（`(int16_t)0xFFFD == -3`、`(int16_t)0xFFFF == -1`、`(int16_t)0x8000 == -32768`）。该依赖的可检查规范项已补入 **§6.1 冻结清单第 5 项**（冻结 Clang 身份与整数模型、转换约定本体、源码注释要求/转换检查方式/合法负结果回归项、重验触发），构建侧审计判据在 **§11.2 第 6 项**——v3 的交叉引用至此有实项支撑，不再只是指针。验收口径**拆为两层、不得混称**：
     - **全域正确性论证**（论证，不是穷举）：在冻结转换约定成立的前提下，由**无符号幅值 + 商/余取值范围 + 符号恢复规则**推导出"任一合法输入的恢复结果都等于数学商/余按二补码解释的位型"——覆盖全域靠约定前提加推导，不靠逐一枚举输入；
     - **工具链/实现验收**（对实际产物的有限验证）：§11.2 第 6 项转换检查 + §8.4 边界表 + 固定种子 16 对随机向量对拍，验证**实际编译产物**符合上述论证（覆盖上列三个代表值与负结果行）。
     `s == (int16_t)(0u - (unsigned)uq)` 一类的恢复恒等式只在"转换 = 模截断"时成立——这是**对冻结工具链的依赖链，不是可移植性质**。工具链身份或 §6.1 第 1-4 项任一编译条件变化 → 按 §6.1 第 5 项重验触发重验收。
   - `(MIN,-1)` 本身在 IR 层是 UB、无契约（§8.3），不参与本条证明；该输入对的观察归 §8.3 的"helper 行为观察"档。
5. **运行时禁令（v3 修正：闭环四条 + 实体口径审计）**：
   - **禁令覆盖全部八个源文件**（不只包装层）：任何源不得使用 `/`、`%` 运算符与 `long long`/`int64_t`/`uint64_t`；产物 IR 中 `udiv|sdiv|urem|srem` 计数必须为 0——unsigned/mod 助手同样受检（防优化器从除以常数、乘法逆元等形状综合出除法 IR）；
   - **i64 审计是 IR 实体口径，不是字符串口径**：不得以全文搜索 `i64` 字符串代替审计——合法 DataLayout 串本身含 `i64:8`（E29 两串皆是），注释也可提及 i64。审计对象是 IR 实体：函数/全局变量的类型与签名、指令操作数与结果类型、常量表达式、属性中出现的**实际 i64 类型及其运算使用**，排除 DataLayout 字符串与注释。同理，IR 签名验收核对**结构与 ABI 属性**（函数名、参数/返回类型、PARM 槽符号与宽度），不是"define 文本无某字样"式的字符串匹配；
   - **审计对象 = 实际送入 llc 的最终优化 IR**：生产管道 `clang ... -S -emit-llvm` 产出的那份 `.ll` 文件字节级原样受检；**不得为审计另编一份"审计专用 IR"**（优化级/宏/包含路径不同即可能漏检）。管线：`-emit-llvm` → 审计 → 同一批文件原样交 `llc`（§11.2；`clang -c` 的产物已是目标码，不是 llc 的 IR 输入）；
   - **机器级调用图核对（llc 产物上执行）**：4 个 unsigned/mod 助手 ecall 计数 = 0；4 个 signed 包装层只调用各自对应的 unsigned helper（`__divsint→__divuint`、`__divslong→__divulong`、`__modsint→__moduint`、`__modslong→__modulong`）；无自环、无互递归、无清单外运行时依赖；
   - **"符号有定义 + 链接成功"不得当作无递归证明**：链接成功只证明符号闭合；体内是否自我调用只能由上述 IR 审计 + 机器级调用图证明（§8.1 落 lit 守门、§11.2 落构建断言）。

### 6.3 公共 ABI 约定（v2 全面修订）

**全部 8 个符号一致的对外契约**（证据：§3、§4.2；v1 的"零栈""不碰 r8-r15""不写任何 SFR"三条作废）：

| 项目 | 约定 |
|---|---|
| 参数 1 | i16 → DPL(低):DPH(高)；i32 → DPL,DPH,B,A（低→高）；i8 → Promote 为 i16 后同 i16（无 i8 libcall 符号） |
| 参数 2 | 静态槽 `__<fn>_PARM_2`（链接期符号，本链存储为**大端**：+0=MSB）；i16 槽 2 字节，i32 槽 4 字节 |
| 返回 | 与参数 1 同位（商/余数在 DPL:DPH 或 DPL,DPH,B,A），`eret` 结尾 |
| 栈 | **允许使用**。被调方可用栈（自动变量、spill、`push arN` 式保存），前提：(a) 调用方保证 SPX 指向有效栈且剩余容量满足门禁（SPEC：剩余 ≥1024B）；(b) **被调方返回时 SPX 必须与入口逐位相同**——ECALL 压三字节返回帧、ERET 弹出是机器机制，被调方自加的任何 `push` 必须有配对 `pop`（SDCC `_divsint.asm:180-184` push ar3/pop ar3 即实例，E20）。fork C 编译的库函数自动变量/spill 可能进栈（E27）；**CALLSEQ 零尺寸只说明调用指令本身不压参数，不等于被调方无栈帧** |
| 重入性 | **无**（静态参数槽 + 静态/溢出局部；中断程序内调用未定义——与 SDCC `intlong-reent=0` 同口径；DESIGN.md v2 的 overlay 重入/递归/中断诊断保留，E26） |
| clobber | **可自由触碰**：A、B、PSW（CY/OV/N/F 边带位）、DPTR/DPL/DPH/DPS、r0-r9、r12-r15——后端可分配寄存器全部 caller-saved（E7/E8），库体经同一分配器编译，**对 r8/r9/r12-r15 不作、也不需要作"不碰"承诺**（v1 该行错误，作废） |
| 保留寄存器 | DR16（动态帧锚）、DR56（DPX，预留）、DR60（SPX）**不显式用作额外算术暂存或帧锚**。**单列三条**：(a) **SPX 精确恢复（v3 写死采样点）**——四个采样时点：`SPX_before_call`（caller 发出 ECALL 前）、`SPX_callee_entry`（callee 入口，ECALL 已压 3B 返回帧）、`SPX_before_ERET`（callee ERET 前）、`SPX_after_call`（caller 返回后）；断言恒等式：`SPX_before_ERET == SPX_callee_entry` 且 `SPX_after_call == SPX_before_call`（E31：eret 弹 [SPX-2..SPX]，逐位恢复是机器机制要求），作为执行级验收硬断言（§8.7 与本条同一定义）；(b) **DR16 帧锚**——若某函数使用动态帧，DR16 的保存/恢复是该函数自身责任，验收镜像含帧用例核对（§8.7）；(c) **DR56/DPX（v3 修正措辞）**——不显式将 DR56/DPX 用作额外算术暂存或帧锚；**ABI 规定的 DPL/DPH 访问及其经物理别名对 DR56 低 16 位的连带影响不在禁令内**（E30：dptr 别名 dr56 低 16 位；E31：帧锚选 DR16 不选 DPX 的官方理由即此）——因此**不作"DR56 全值保留"承诺** |
| 副作用 / SFR | **不改无关外设及中断控制状态**（v1 "不写任何 SFR" 作废——A/B/PSW/DPTR/DPS 是 ABI 规定必写的 SFR 面）；不使能、不屏蔽中断，不改中断控制器/外设寄存器；PSW 是唯一合法边带输出 |
| 调用方安全性 | 由 CSR_MCS251 regmask 构造保证：可分配寄存器全 caller-saved，仅 DR60(SPX) 跨调用存活（E7）——运行时库 clobber 面再大也不需要后端改动；调用方自己负责跨调用存活值的保存（`spill-across-call.ll` 机制） |

### 6.4 逐符号差异表

| 符号 | 参数槽符号 | 槽宽 | 实现（独立实现，§10-Q1） | 调用层次 | 槽落区（E23 判定） |
|---|---|---|---|---|---|
| `__divuint` | `__divuint_PARM_2` | 2 | 16 轮移位-减法 | 叶 | `.mcs251.OSEG.*` + OVERLAY |
| `__divulong` | `__divulong_PARM_2` | 4 | 32 轮移位-减法 | 叶 | 同上 |
| `__moduint` | `__moduint_PARM_2` | 2 | 移位-比较取余 | 叶 | 同上 |
| `__modulong` | `__modulong_PARM_2` | 4 | 同上 | 叶 | 同上 |
| `__divsint` | `__divsint_PARM_2` | 2 | 无符号域归一 + `ecall __divuint` | 1 层 | `.mcs251.DSEG.*`（无 OVERLAY） |
| `__divslong` | `__divslong_PARM_2` | 4 | 同上 | 1 层 | 同上 |
| `__modsint` | `__modsint_PARM_2` | 2 | 归一 + `ecall __moduint`（余号随被除数） | 1 层 | 同上 |
| `__modslong` | `__modslong_PARM_2` | 4 | 同上 | 1 层 | 同上 |

### 6.5 `_PARM_2` 槽的数据节归属与槽计数（v2 更正）

- **叶/非叶由 AsmPrinter 按机器码实际判定**（E23）：体内有 `call`/inline-asm 即非叶。按 §6.4 的实现形态，四个 unsigned/mod 助手为叶，四个有符号包装层为非叶。
- **槽计数（v2 更正，v1 "~22B" 有误）**：裸大小 = 4 个叶槽（2+4+2+4）+ 4 个非叶槽（2+4+2+4）= **24 字节**。
- **ELF 布局**：四个叶槽各落独立 `.mcs251.OSEG.*`，按 SPEC **同一 overlay 组、组内同址、组占 max = 4B**（E25，`__divulong_PARM_2` 最大）；四个非叶槽各落独立 `.mcs251.DSEG.*` slice，**独立分配、禁 overlay**，合计 **12B**。**理想占用 = 12B 非叶 DSEG + 4B 叶 OSEG 组 = 16B**。**不存在"八槽全 overlay"**（v1 措辞作废）。
- **overlay 是规范语义，不是可悄悄平铺的优化建议**：叶 OSEG 必带 `SHF_MCS251_OVERLAY`（E23/E25），flag 在场即约束链接器同址取 max；"首期平铺"不是合法选项，链接器行为进 §8.6/§8.7 验收。
- **SDCC large 产物槽在 XSEG**（E21）——不构成"SDCC 语义下这些槽同址 overlay"的证据；同址只可能发生在 OVR 区（harvest 布局或 mcs51 small asm 路径）。正确性论证与 overlay 无关（§4.2）。
- **v2 延伸（E26）**：参数槽迁 EDATA 必须由该槽 section 的放置能力显式声明、且全部访问经可重定位间接路线；本期不迁，槽保持 DATA 类 DSEG/OSEG。
- **`.rel` 链（过渡期历史路径）**：harvest `provider.asm` 的槽在 OSEG(OVR)；**v2 裁定 provider 不扩容**（§10-Q4），`.rel` 链不再新增 6 个 harvest 体。

---

## 7. 后端改动清单（实施序）

### 7.1 `llvm/include/llvm/IR/RuntimeLibcalls.td`（名字注册）

在 `:2952-2960` 的 MCS251 块内追加 6 条并扩清单：

```td
def mcs251_divsint  : RuntimeLibcallImpl<SDIV_I16, "_divsint">;
def mcs251_divslong : RuntimeLibcallImpl<SDIV_I32, "_divslong">;
def mcs251_moduint  : RuntimeLibcallImpl<UREM_I16, "_moduint">;
def mcs251_modulong : RuntimeLibcallImpl<UREM_I32, "_modulong">;
def mcs251_modsint  : RuntimeLibcallImpl<SREM_I16, "_modsint">;
def mcs251_modslong : RuntimeLibcallImpl<SREM_I32, "_modslong">;
def MCS251SystemLibrary : SystemRuntimeLibrary<isMCS251,
    (add mcs251_divuint, mcs251_divulong, mcs251_divsint, mcs251_divslong,
         mcs251_moduint, mcs251_modulong, mcs251_modsint, mcs251_modslong)>;
```

### 7.2 `MCS251ISelLowering.cpp` 构造器（`:49-71`）

```cpp
// i8：四个除取余操作全部 Promote 到 i16（对齐 SDCC 前端提升，E10；泛型
// legalizer 对 SDIV/SREM 用符号扩展、UDIV/UREM 用零扩展，正确性由机制保证）。
setOperationAction(ISD::SDIV, MVT::i8, Promote);
setOperationPromotedToType(ISD::SDIV, MVT::i8, MVT::i16);
// SREM/UREM 同上两行。

// i16/i32：SDIV/SREM/UREM 从 Custom 改 LibCall；UDIV 保持现状。
setOperationAction(ISD::SDIV, MVT::i16, LibCall);
setOperationAction(ISD::SREM, MVT::i16, LibCall);
setOperationAction(ISD::UREM, MVT::i16, LibCall);
setOperationAction(ISD::SDIV, MVT::i32, LibCall);
setOperationAction(ISD::SREM, MVT::i32, LibCall);
setOperationAction(ISD::UREM, MVT::i32, LibCall);
setLibcallImpl(RTLIB::SDIV_I16, RTLIB::impl_mcs251_divsint);
setLibcallImpl(RTLIB::SDIV_I32, RTLIB::impl_mcs251_divslong);
setLibcallImpl(RTLIB::UREM_I16, RTLIB::impl_mcs251_moduint);
setLibcallImpl(RTLIB::UREM_I32, RTLIB::impl_mcs251_modulong);
setLibcallImpl(RTLIB::SREM_I16, RTLIB::impl_mcs251_modsint);
setLibcallImpl(RTLIB::SREM_I32, RTLIB::impl_mcs251_modslong);
```

并把 `:62-64` 循环里的 `{ISD::SDIV, ISD::SREM, ISD::UREM}` 从 Custom 名单移除（`UDIVREM/SDIVREM/MULHU/...` Expand 项保留）。

### 7.3 `LowerOperation`（`:226-229`）

删除 `case ISD::SDIV/SREM/UREM: report_fatal_error(...)` 整个分支。该 fatal error 文案（"signed ... not supported"）随 UREM 误拒一并消失，不再有"文案只说 signed"的缺陷。

### 7.4 不需要动的部分（显式确认）

- `LowerCall`/`checkParameter`/`parameterSlot`：双标量参数 + 静态槽机制已被 `udiv.ll` 的 `div_twice`（两个独立 `_divulong` 调用不交错）与 `div16`（i16 PARM 槽）锁死，i8 Promote 落到 i16 后完全复用。
- `MCS251CallingConv.td`、寄存器/reserved/regmask：零改动（§6.3）。
- i8 Promote 现状确认：目前**只有 UDIV i8** 有 Promote（`:56-57`），本次补齐另外三个操作。

### 7.5 测试翻转（v2 按真实基线改写）

- `divrem-errors.ll`（**9 片段 / 10 条负 RUN**，§2.2）整体删除，替换为 `divrem-libcall.ll` 正例：**9 个片段 × O0/O2 = 18 条正 RUN**，逐片段 CHECK `ecall __<fn>` + `__<fn>_PARM_2` + `eret`（形状抄 `udiv.ll`）；保留"常数除数折叠"样例与除零 `CHECK-NOT: trap` 样例（除零注释措辞同步 §8.3 口径）。10 条旧负 RUN **全部翻正、无一条保留为负例**。
- `udiv.ll` 不动（回归锚点）；**但 `:78` 注释文案列入更正**："Division by zero is poison" → 按 LangRef 改为 undefined behavior 表述（随本设计实施一并提交，§8.3）。
- 新增组合用例：同一函数内 `sdiv+srem`、`udiv+urem` 成对出现——验收按 §8.8 的 CALLSEQ 口径（参考 `call-sequences.ll`），两次独立 libcall、槽存储在各自 CALLSEQ 内不交错、寄存器跨调用存活（`div_twice` 先例）。
- 新增 i64 独立负测与 i8 IR 执行探针：见 §8.1。

### 7.6 运行时库（`llvm/lib/Target/MCS251/Runtime/`）

- 8 个 `.c`：`_divuint.c/_divulong.c/_divsint.c/_divslong.c/_moduint.c/_modulong.c/_modsint.c/_modslong.c`——独立实现（§10-Q1），签名与算法红线按 §6.1/§6.2。
- 构建目标 `mcs251rt`：**两阶段依赖**（§11.2）——先用常规 LLVM 构建产出 clang/llc，再以 custom target 用**本构建刚产出的工具链**按三步管道编出**八个 ELF32BE 单对象**：① `clang --target=mcs251-unknown-none -O2 -S -emit-llvm` 8 源 → 8 份 `.ll`（契约/DataLayout/优化级按 §6.1 冻结清单）；② IR 审计（§6.2-5：四指令零命中、i64 实体口径、八源全覆盖，作用在这批 `.ll` 原件上）；③ 同一批 `.ll` **原样**交 `llc -mtriple=mcs251 -O2 -mcs251-object-format=elf -filetype=obj` → 8 个 `.o` + 清单。**v3 修正**：`clang -c` 的直接产物是已目标码化的 `.o`，**不是 llc 的 IR 输入**——旧"clang -c → llc"两步写法作废；机器级调用图核对（§6.2-5）作用于这批 `.o`。
- **首期交付 = 八个 `.o` + 显式对象清单**，不交付归档（§10-Q4：LinkerCore 只收 ELF32BE 单对象，SPEC 排除 archive 懒提取）。
- **生产纯 ELF**；`.rel` 链历史 provider 不扩容（§10-Q4）。
- 每函数自检向量**只编进验收固件**（validation 侧），不进生产对象（§10-Q3）。

---

## 8. 验证计划（v2 按真实基线重列）

### 8.1 L1：lit 编译层（无需运行时符号）

- `divrem-libcall.ll`（§7.5）× O0/O2：9 片段 × 2 = **18 条正 RUN**；ecall 名、PARM 槽、eret、常数折叠、除零不设陷。
- **i64 独立负测（v3 修正有效性与锁定对象）**：不得只测"i64 参数拒绝"（那是 oseg-errors.ll 里静态槽类型限制 `i8/i16/i32` 的另一条既有规则，**原样保留**——负测必须确认自己**不是被这条先截获**：探针的 i64 只出现在寄存器内算术，不落入静态槽/参数位置，失败消息须对准算术降级拒绝）。探针必须**真走到不支持的宽算术路径**：
  - i64 值**动态产生**（volatile/查表供入；"`zext` 自窄值"**只能作为构造步骤**——**v4 强调：单独把窄动态量零扩展成 i64 没有动态高位**，高位恒零、宽算术可被合法折叠，不构成负测路径；须经移位/拼合把运行期值送进高半部才有动态高位），**结果对高位有依赖**（高位参与运算并影响截回窄类型后的可见输出，如 `udiv/sdiv/urem/srem i64` 后 `trunc` 回窄类型再输出）。fixture 的有效性判据 = **运行期高位实际参与运算并影响可见输出**（例：窄值经 `zext` 后移位/拼合构造出高位非零且运行期可变的 i64 量，再参与宽算术）；只展示"窄值零扩展后做宽算术"的 fixture 锁定的是不存在的路径，不合格；
  - 断言 llc 对该路径走**可诊断失败**（fatal error，文案随实现锁定后写进 CHECK），不允许静默错译或无消息 crash；现状行为未实测（§11.4）；
  - 锁定对象是"**仍需 i64 算术降级时的可诊断拒绝**"，**不是**"源 IR 出现过 i64 就必须失败"：若优化器能证明高位恒零并把宽算术完全折叠（如 `zext` 后未经算术即 `trunc`），同一 IR 必须照常编译通过。探针需核对送入 llc 的 IR 中宽算术指令存活（§6.2-5 实体口径），并配"折叠对照片段"（无高位依赖的同形 IR，期望通过）作负-正对照，证明拒绝来自宽算术路径而非 i64 字样本身。
- **真正 i8 IR 执行探针（新增）**：C 的 `int8_t` 运算会提升到 32 位，`sdiv i8` 的 IR 契约与提升后的 C 运算**不是同一契约**。执行级验证需手写 `.ll`（操作数经 volatile/查表进 `sdiv i8 -128, -1` 等极端对）直接过 llc + 链运行时对象在 QEMU 跑（§8.7 载体），不能只用 C kernel 覆盖 i8。
- IR 审计（lit 守门位，v3 精确化）：运行时 8 源**实际送入 llc 的那份 `.ll`**（§6.2-5/§11.2 管道）——`udiv|sdiv|urem|srem` 零命中、i64 按 IR 实体口径零命中（排除 DataLayout 串与注释）；机器级调用图核对落在 §11.2 构建断言（对象级）。
- `udiv.ll:78` 注释更正随本批测试提交（§7.5）。

### 8.2 L2：QEMU 金标对拍（语义铁律）

沿用 demo-modern `check` 口径（E17）：宿主期望 transcript vs 串口逐行，完整终止行 + PASS 收尾，超时/提前退出即 FAIL，QEMU stderr 归档。kernel 形态约束：

- 所有操作数经 `volatile` 变量/查表进入，**禁止常数折叠进断言**（E14 教训）。
- **宿主期望一律用 int64/uint64 计算**（`int64_t q = (int64_t)a / b;` 再范围校验），宿主程序本身不得含任何 UB——x86 宿主上 `INT_MIN/-1` 是 SIGFPE，不能靠宿主直接算；宿主在算之前显式跳过 UB 输入对（它们走 §8.3 观察档，**独立固件/独立会话执行**）。
- **宿主宽类型断言**：每语义行同时断言 `a == q*b + r`、`|r| < |b|`、`r==0` 或 `r` 符号与 `a` 相同（C99/LLVM 截断除法符号律）、`q` 在该宽度有符号/无符号范围内。
- **目标输出用定宽 UART 序列化**（十六进制逐字节定宽），**不依赖 variadic printf——本目标不支持**；三方 kernel 的输出代码路径限定为已实证的串口输出原语。
- 操作数取值要求**非对称字节**（如 0x1234 vs 0x3412 可区分字节序错装）。

### 8.3 UB / poison 契约（v2 按 LangRef 重写；v1 "poison 行（除零、INT_MIN/−1）" 分类有误）

| IR 输入 | LangRef 分类 | 测试处置 |
|---|---|---|
| `udiv`/`sdiv`/`urem`/`srem` 除数 = 0 | **Undefined Behavior**（不是 poison） | 观察档（下） |
| `sdiv` INT_MIN ÷ (-1) | **Undefined Behavior** | 观察档 |
| `srem` INT_MIN % (-1) | **Undefined Behavior**（即使数学余数为 0） | 观察档 |
| `udiv`/`sdiv` 带 `exact` 标志且不整除 | **Poison** | poison 语义另测：只验证含 exact 的合法化路径不被误用（编译层），poison 值不落串口断言 |
| 一切合法输入 | 完全定义语义 | 宿主 + SDCC 对拍（§8.2/8.4/8.5） |

- **不为非法输入新增 trap、返回值或诊断契约**——运行时库、后端与 lit 断言都不承诺除零/MIN÷(-1) 的任何特定结果。**v3 澄清**：`udiv.ll` 的 `CHECK-NOT: trap` 明确只是该特定 fixture 的**代码生成回归检查**（锁"这些输入不诱使后端合成 trap/崩溃指令"），不是除零行为契约，不向其他 fixture 推广、也不构成对执行行为的任何承诺；`udiv.ll:78` 注释措辞更正为 UB（§7.5）。
- **测试三分离（v3 强化隔离与口径）**：
  1. **合法语义行**：宿主 + SDCC 对拍，全量断言（§8.2）；与 UB 探针**不在同一固件、不在同一 QEMU 会话**——同一次执行不保证 UB 探针不污染语义行（挂死、内存写飞、提前终止都会殃及同镜像后续行），UB 观察单独构建、单独运行；
  2. **UB 输入观察行（IR 探针）**：非法输入必须经**手写 `.ll`**（§8.1 路径）真正抵达 IR 层除法指令；独立、显式命名的探针函数（**不得**与语义行同函数混排，防止 UB 输入污染合法化与优化判断），有限超时防挂死；观察结果**允许三种形态：返回某值 / 终止 / 超时**，**不预设确定性输出**（v2 "记录确定性输出"作废）；档案记录实际形态与值（若返回），不与宿主对拍、不建立契约；历史输出自比**只记录变化、不设兼容门槛**——库实现变更导致观察值变化仅归档，不判回归失败；
  3. **helper 行为观察（v3 从"UB 观察"拆出单列）**：验收固件用 C 直接调用自研 helper 传非法参数（如 `_divuint(1, 0)`、`(INT16_MIN, -1)`）属于**绕过 IR 的库函数行为观察**——它观察的是实现定义行为，不是 IR UB 语义，档案中**单独标注、不与 IR 语义混称**；SDCC 侧除零输出（如 `_divuint` 得 0xFFFF，静态读数）归入本类素材，未经 QEMU 复测不得当作已实证事实引用；
  4. **poison 行**（exact 语义）：编译层另测。

### 8.4 边界向量表（v2 修正；每宽度强制全覆盖；i16 用 16 位值、i32 用 32 位值）

v1 错误更正：**0x8000/0x80000000 是无符号最高位置位的值（32768/2147483648），不是无符号最大值**（最大值是 0xffff/0xffffffff，v1 漏列）；**整数类型没有 -0**，"±0" 措辞废除。

| 类别 | 向量（dividend, divisor） | 断言方式 |
|---|---|---|
| 零与一 | (0,1)、(1,1)、(0,x非零)、(x,1)、(1,-1)、(-1,1)、(-1,-1) | 语义断言 |
| 无符号最大位型 | (0x8000/0x80000000, 1/2/3/自身)、(x, 0x8000/0x80000000)（x 取 ±小值、±1、max、min 遍历） | 语义断言 |
| 无符号最大值 | **(0xffff/0xffffffff, 1/2/3/自身/max/min)**、(x, 0xffff/0xffffffff) 遍历 | 语义断言 |
| 有符号极值 | (MAX,1)、(MIN,1)、(MAX,MAX)、(MIN,MIN)、(MAX,MIN)、(MIN,MAX)、(MIN,-1 以外的负除数) | 语义断言 |
| 2 的幂及相邻 | 除数 ∈ {1,2,4,16,128,256,0x8000(作无符号),…} × 邻域 {n-1,n,n+1}（n=2^k，跨 k=1..15/31 采样） | 语义断言 |
| 跨字节边界 | 0x00ff/0x0100/0x7fff/0xfffe/0xffff（i16）；0x00ffffff/0x01000000/0xfffffffe/0xffffffff 等（i32） | 语义断言 |
| 商形状 | 商=0（|a|<|b| 全符号组合）、商=1、商=-1、整除/非整除成对（同 a,b 各断 div 与 rem） | 语义断言 |
| 符号组合 | (+,+)、(+,-)、(-,+)、(-,-) × {商≠0, 商=0, 余=0, 余≠0} | 语义断言 |
| 随机面 | 固定种子 16 对（含经 uint64 折叠的极端分布） | 语义断言 |
| **UB 输入** | (MIN, -1)、(*, 0) | **不进语义断言**：LLVM IR 为 UB（§8.3）。另立观察档（独立固件/独立会话、IR 探针与"helper 行为观察"分标、有限超时、只记录、不预设输出） |

srem 额外锁符号律：`(-7)%2 == -1`、`7%(-2) == 1`（余号随被除数，C99/LLVM 一致——`_modsint` 编译体按此实现，§4.2）；宿主侧断言 `a == q*b + r` 全量复核（§8.2）。

### 8.5 L3：SDCC oracle 对拍（Oracle-B；v2 增独立性约束）

三方口径沿用（E16）：同一 kernel.c（显式宽度 typedef）→ (a) 宿主 gcc = Oracle-A；(b) `sdcc -mmcs251 --c1mode` → sdas251 `.rel` → QEMU = Oracle-B（走 SDCC 自家 `_divuint/_moduint` 族，等于用 SDCC 验我们的向量表和 ABI 理解）；(c) fork 链 = DUT。三方串口逐行一致才转正。（v2 更正：现 runner 实际调用 `sdcc -mmcs251 --c1mode`，**不带 model 旗标**，走 SDCC 默认模型——v1 写的 `--model-small` 与实际不符；模型必须按下行"固定矩阵"显式钉死或显式记录默认值，且与 E9 证据源 `mcs251-large` 构建的关系要写明。）

**v2 独立性硬约束**：

- **Oracle-B 的八 helper 供给方必须独立完整**：现 runner 会把 `provider.asm`（harvest 体）链进目标镜像——harvest/自研对象**不得冒充** SDCC 库供给。Oracle-B 构建必须解析到 SDCC 工具链自带的 `mcs251-large` 库对象；验收脚本需记录符号解析来源（每个 `__div*`/`__mod*` 符号由哪个文件定义）并归档。
- **固定矩阵**：SDCC 版本（4.6.0）、model（现 runner 走 `sdcc -mmcs251` 默认模型；要么显式加旗标钉死，要么把"默认模型"作为固定值记录，并注明 E9 证据源为 `mcs251-large` 构建）、宽度（ABI v1 optsdcc 签名逐字一致）、依赖与符号解析来源全部固定并归档；任一变化 = Oracle-B 重验收。
- 目标侧输出与 §8.2 同口径：定宽序列化、无 variadic printf。

### 8.6 L4：ELF 对象级验收（v2 新增）

对八个 `.o` 逐一（并按显式清单核对齐全性）：

- 函数符号（`__divuint` 等 8 个）与 `_PARM_2` 槽符号的**定义/引用闭合**：每个包装层引用的 unsigned helper 符号有且仅有清单内对象定义；槽符号全局唯一定义、类型 `STT_OBJECT`、size 属性 = 2B/4B 且与 §6.1 签名宽度一致；
- **槽大小/节类型（v3：与字节序拆开，对象级可证）**：槽符号 `STT_OBJECT`、size 属性 = 2B/4B（与 §6.1 签名宽度一致）、落 NOBITS 节。**NOBITS 无载荷，size 证明不了字节序**——PARM 槽是未初始化静态存储，对象文件里不存在可核对的大端序列化字节；
- **槽访问字节序在执行级证明（§8.7 承担，对象级不设此项）**：调用方存储顺序（拆字节次序）+ 被调方读取顺序（组字节次序）+ 执行时非对称字节值（0x1234 型，错装必现形）三者合证；
- `.note.mcs251.abi`：每对象恰好一个，描述符 8 字段与 LinkerCore 校验值一致（E24）；
- 重定位为 RELA；叶函数槽落 `.mcs251.OSEG.*` + `SHF_MCS251_OVERLAY`，非叶落 `.mcs251.DSEG.*` 无 OVERLAY（§6.4/6.5 判定）；
- **诊断验收**：注入重复定义对象与抽走清单对象，链接器必须报错（重复定义/未解析符号），不得静默；
- 链接输入为**逐个 ELF32BE 单对象**（E24），全流程无 `.a`。

### 8.7 L5：ABI 执行级验收（v2 新增）

在 §8.2 语义对拍之外，验收固件必须包含：

- **非对称字节值**全 helper 遍历（0x1234 型）：这是**槽访问字节序的执行级证明载体**（§8.6 拆分后的归属项）——调用方存储 + 被调方读取 + 非对称值三者合证，字节序错装必现形；
- **跨模块调用**：caller 与库不同编译单元、可不同优化级（§8.8）；
- **嵌套 signed→unsigned**：`_divsint→_divuint`、`_modslong→_modulong` 两层 ecall 全链跑通（栈深 2 层 + 返回帧）；
- **SPX 精确恢复（v3：采样点写死，与 §6.3 同一定义）**：四时点采样——caller 侧读 `SPX_before_call`（发出 ECALL 前）与 `SPX_after_call`（返回后）；callee 侧读 `SPX_callee_entry`（入口第一条指令前，此时 ECALL 已压 3B 返回帧）与 `SPX_before_ERET`（ERET 前最后一条指令后）。硬断言：`SPX_before_ERET == SPX_callee_entry`、`SPX_after_call == SPX_before_call`；callee 侧两点之差即被调方自身栈净用量（供栈哨兵刻度）；
- **栈哨兵**：栈区预填模式字，调用后检查哨兵完整（被调方栈用量不越界）；
- **调用后仍活跃值**：caller 中跨调用的 caller-saved 值在调用后重新校验（regmask 契约的执行级证明，`spill-across-call.ll` 机制）；
- **DR16 帧锚用例（v3：组合补全，不得跳过）**：(a) caller 无动态帧、被调 helper 亦无动态帧——基线（八个 helper 均无动态 alloca，属此列）；(b) **caller 有动态帧、helper 无动态帧**——caller 经 `alloca` 触发 hasFP/DR16 锚/RESTORESP 序列（E31），中程调用 helper 后继续 @dr16 相对寻址：证明调用方锚值跨 ecall 完好（helper 不触碰 DR16 是 reserved 寄存器契约的推论，但该推论必须执行级验证——**不能因八 helper 自身不触发动态帧就省略此组合**）；(c) 双方均有动态帧（若实现触发）——嵌套保存/恢复核对。

### 8.8 回归与升版门槛（v2 增优化级审计）

- **成对调用检查按 call-sequences 口径**（E7/`call-sequences.ll:6`）：对 `sdiv+srem`/`udiv+urem` 同现用例，逐 CALLSEQ 核对**槽存储归属**——每个 `__<fn>_PARM_2` 存储必须在该 libcall 的 CALLSEQ 之内完成、不被调度进别的调用的窗口；**不得只数 ecall 个数**。
- **优化级分别审计**：库构建优化级与调用方优化级分别记录；**库优化级任何变化 → §8.6 + §8.7 + 语义对拍全量重跑**（调用方优化级变化 → 语义对拍重跑）。
- 重跑 muldiv-alice temperature-lookup 验收（E13【Q】，唯一真实 udiv 执行记录）确认接线改动无回退。
- divrem-errors 翻转后全 lit 目录绿色；`verify-machineinstrs` 两优化级通过。
- 铁律重申：**任何一行新机器码语义，未在 QEMU 串口实证前不得标注完成**；timeout 124 不是证据。

---

## 9. 风险清单（v2 更新）

| 风险 | 缓解 |
|---|---|
| 许可证（已裁定，§10-Q1）：SDCC device/lib 为 GPL-2.0-or-later + 链接例外 | 独立实现 + Apache-2.0 WITH LLVM-exception；SDCC 源/产物只作参考与 oracle；禁止逐行改写伪装 |
| 运行时由 fork 自身编译（自举）：后端 bug 会同时打穿库与用户码；同名 helper 内存活除法 IR = 真实自身 ECALL 自递归（§6.2-2 裁定） | 库只用已实证特性 + 无 UB 红线（§6.2）+ 八源禁令/实际送 llc IR 审计/机器级调用图核对三重闭环（§6.2-5、§11.2）+ QEMU 自检 + SDCC oracle 三方对拍 |
| 库函数使用栈（E20/E27）而旧设计假设零栈 | §6.3 栈契约 + §8.7 SPX 恢复/栈哨兵验收；调用方栈门禁（剩余 ≥1024B）沿用 SPEC |
| 槽布局误读（v1 "22B/全 overlay/XSEG 证明 overlay"） | §6.5 更正：24B 裸 / 16B 理想 / 叶组 overlay max=4B / 非叶禁 overlay；§8.6 对象级验收核对节与 flag |
| div+mod 成对 = 两次全价调用 | 本期接受（SDCC 同样无融合）；CALLSEQ 内槽存储口径验收（§8.8） |
| E1（ELF/lld）未批，运行时对象格式悬置 | 库源码与格式解耦（构建参数可切），**生产交付口径 = 纯 ELF 八 .o**（§10-Q4）；.rel 期 provider 不扩容，signed/mod 在旧 .rel 链不可用（Oracle-B 用 SDCC 自家库，不受影响） |
| Promote 后 i8 语义回归（SDIV 需符号扩展） | 泛型 legalizer 按操作符符号性扩展（机制保证）+ lit i8 用例 + **i8 IR 执行探针**（§8.1）+ QEMU i8 边界行 |
| i64 降级路径漏测 | §8.1 i64 独立负测（v3：动态高位 + 高位依赖 + 存活核对 + 折叠对照，不止参数拒绝） |

---

## 10. 原"开放问题"裁定记录（v2：全部转为明确决策）

1. **Q1 许可证——已决**：八个实现为**公开算法独立实现**（移位-减法/移位-比较为公共领域知识），库文件头 Apache-2.0 WITH LLVM-exception。SDCC 源码与编译产物**只作参考与语义 oracle**（GPL-2.0-or-later + 链接例外只约束 SDCC 库自身的链接场景，**不能概括豁免**把文本搬进 LLVM 侧库）；**禁止逐行改写伪装独立实现**。独立性的判据：只依据公开算法描述与行为契约书写，不逐行对照 SDCC 文本。
2. **Q2 UB 输入契约——已决**：不建立任何契约（无 trap/返回值/诊断承诺），UB 输入走只记录的观察档（§8.3）。`udiv.ll:78` 注释文案列入更正。
3. **Q3 库归属与构建所有权——已决**：源码进 `llvm/lib/Target/MCS251/Runtime/`；**独立显式 CMake target（`mcs251rt`），不进任何 LLVM 工具的链接图**；**两阶段依赖**防 `llc → Runtime → llc` 环：第一阶段常规构建产出 clang/llc，第二阶段 custom target 以该工具链编库（库不参与第一阶段）；**前端固定（v3 修正）**：`--target=mcs251-unknown-none` + 完整契约清单（模型/整数模型/两串 DataLayout/两级优化级，§6.1——裸 triple 不构成冻结契约），变化即重验收；**自检码只进验收固件**（validation 侧消费），生产对象只含 8 个函数。上游化另案审计（compiler-rt 归属、标准名迁移），不在本期范围。
4. **Q4 首期交付与对象供给——已决**：**首期交付八个 ELF32BE `.o` + 显式对象清单**（逐文件列出、验收脚本按清单传给链接器）。理由：`LinkerCore.cpp:286-303` 只收 ELF32BE 单对象、不认 `.a`（E24）；`SPEC.md:84-88` 明确排除 archive 懒提取——**要支持 `.a` 必须先修规范并报 PM**，本期不做。**生产纯 ELF**；**历史 provider.asm 不扩容**（不补 6 个 harvest 体），`.rel` 链只保留 Oracle-B 用途。
5. **Q5 命名终裁——已决**：SDCC 名（`_divuint/_divulong/_divsint/_divslong/_moduint/_modulong/_modsint/_modslong`）为终裁。若上游化（主线 LLVM）列入路线图，标准名（`__udivhi3` 族）迁移在**上游化里程碑**一次切清、不设别名期（改名成本清单见 v1 §5.2，仍然有效）。
6. **Q6 性能预算——已决**：本期接受 div+rem 同现为两次全价 libcall（SDCC 亦无融合助手）；不做自定义 UDIVREM 组合节点（会破坏 SDCC 互操作对称性）。如后续负载证明需要，另立提案。
7. **Q7 槽 overlay 策略——已决**：叶 OSEG 组**同址取 max 是 SPEC 规范语义**（E25），不是可选优化、不允许"首期悄悄平铺"；非叶 DSEG 禁 overlay；链接器同址行为与 flag 校验进 §8.6/§8.7 验收。槽迁 EDATA 仅按 E26（显式声明 + 全间接访问）另案。
8. **Q8 i64 期翼——已决**：本期范围明确排除 i64 运行时（SDCC `*longlong` 族不移植）；i64 是文档化限制 + §8.1 独立负测（v3 口径：宽算术路径 + 高位依赖，含"折叠对照必须通过"）；oseg-errors.ll 的 i64 参数拒绝（静态槽类型限制）原样保留。

**集成补充规范先行**（Alice 裁定附加项）：本设计的支持域扩大（负例翻正、新对象供给、CRT/初始化链接线接入、overlay 语义沿用、语义验收接入）在实施前以**集成补充规范**形式挂接 `validation/mcs251-elf/SPEC.md` 的等价域：凡涉及等价域扩大（如未来要收 `.a`、新节类型、新链接诊断），必须先扩充规范并报 PM，不得以删用例缩小等价域的方式迁就实现。

---

## 11. 出处、构建依赖、对象供给与验收退出条件（v2 新增）

### 11.1 交付链四段不等式

**对象生成 ≠ 归档/链接成功 ≠ 串口一行 PASS ≠ 运行时完成。** 逐段判据：

| 阶段 | 完成判据 | 不算完成的反例 |
|---|---|---|
| 对象生成 | 8 个 `.o` 存在且过 §8.6 对象级验收、IR 审计零命中 | 只跑了 llc 不看返回值/不验 note 与节 flag |
| 链接成功 | 显式清单内全部对象被 lld MCS251 接受；重复/缺失诊断用例通过 | 只链了 udiv 两个对象；用 .a 蒙混（链接器根本不收） |
| 执行 PASS | §8.2/8.4/8.7 全部向量行通过 + 完整终止行 | 串口出现一行 "PASS" 但向量表未全跑/UB 观察档混入语义行/timeout 后的残行 |
| 运行时完成 | 上三段全绿 + §8.8 回归门槛（temperature-lookup 重跑、lit 全绿、优化级审计归档）+ §11.4 **待办型**条目清零（证据等级声明除外）+ **外部审批与供给门全过**：Oracle-B 八符号独立供给及三方对拍（§8.5）、集成补充规范审批（§10 附加项）、适用 ELF 前置审批（E15，待 PM） | 任何一项"未实测"未归档就宣布完成；任何一扇外部门未过就宣布完成 |

### 11.2 构建依赖（显式声明）

1. 第一阶段：常规 LLVM 构建产出 `clang`、`llc`（含 MCS251 目标）。
2. 第二阶段（v3 修正：补 IR 产出步骤，参数全部钉死）——custom target `mcs251rt` 调用**同一构建树**的第一阶段产物，三步：
   - **产 IR**：`clang --target=mcs251-unknown-none -O2 -S -emit-llvm` 8 源 → 8 份 `.ll`（driver 默认契约 `1,2,32,8,1` 与 cc1/llc 两串 DataLayout 按 §6.1 冻结清单归档进构建记录）；
   - **审计**：§6.2-5 闭环（四指令零命中、i64 实体口径、八源全覆盖、作用在送 llc 的原件上）+ 静态宽度断言（§6.1）随编译生效；
   - **交 llc**：同一批 `.ll` **原样** `llc -mtriple=mcs251 -O2 -mcs251-object-format=elf -filetype=obj` → 8 个 `.o` + 清单文件；随后机器级调用图核对（§6.2-5）作用于 `.o`。
   **`clang -c` 的直接产物是已目标码化的 `.o`，不是 llc 的 IR 输入——不得跳过 `-emit-llvm` 步骤。**
3. `mcs251rt` 不被任何 LLVM 库/工具链接（防 `llc→Runtime→llc` 环，§10-Q3）。
4. 构建时审计断言（v3 精确化）：同第 2 步——四指令零命中；**i64 按 IR 实体口径**（检查函数/全局/指令/常量表达式中的实际 i64 类型与运算使用；排除 DataLayout 串——其合法含 `i64:8`——与注释）；签名检查核对结构与 ABI 属性，而非"define 文本无属性"式字符串匹配；调用图核对落在 `.o` 上（4 unsigned 零调用、4 signed 只调对应 unsigned、无自环/互递归/额外运行时依赖）。
5. lit 用例仅引用 Runtime **源文件**做签名/审计守门，不依赖 `.o` 产物（lit 不跑构建）。
6. **结果恢复转换依赖审计（v4 补入，§6.1 冻结清单第 5 项的构建侧判据；v3 在此缺项）**：
   - **冻结 Clang 身份归档**：编译八源的 clang 版本号 + 构建 triple/哈希记入构建记录，验收以归档身份为准；
   - **源码注释核对**：八源中每处无符号→有符号恢复转换点的规定注释在位（§6.1 第 5 项注释要求）；
   - **IR 恢复点形状核对**：在第 2 步实际送 llc 的 `.ll` 原件上核对——同宽 unsigned→signed 恢复转换为位型直通、取负在无符号域完成、无超预期的扩展/截断链；
   - **负结果回归项强制执行**：§8.4 边界表全部负结果行（含 `-7/2`→-3、`-7%2`→-1、`INT16_MIN/1`→-32768 三个代表值）+ 固定种子 16 对随机向量中的负结果对，在 §8.2/§8.7 对拍中通过并归档；
   - **重验**：clang/llc 身份变化或 §6.1 第 1-4 项任一编译条件变化 → 上述各项全量重验并更新归档。

### 11.3 对象供给矩阵（谁在哪个链供给八个符号）

| 链 | 八 helper 供给 | 状态 |
|---|---|---|
| ELF 链（生产） | `mcs251rt` 八个 `.o`（显式清单） | **未实现**（本期交付物） |
| `.rel` 旧链 | `provider.asm` 仅有 `__divuint`（无 signed/mod，**不扩容**） | 存量；signed/mod 在此链不可用（文档化限制） |
| Oracle-B | SDCC 工具链自带 mcs251-large 库（符号解析来源须归档，§8.5） | 既有能力，独立性验收**未做** |

### 11.4 尚未实现 / 尚未实测清单（截至本稿；v3 修正完成判据：**待办型**条目实施后逐项移出；**证据等级声明**条目——如真机证据缺失——是证据分级事实【H】，不是待办，不要求字面归零，随交付状态如实保留并按图例升级）

未实现：

- 8 个运行时 `.c` 源与 `mcs251rt` 构建目标、显式对象清单；
- 后端 6 条 `setLibcallImpl` 接线 + 3 条 i8 Promote + `:226-229` 分支删除（§7）；
- `divrem-errors.ll` 翻转为 18 条正 RUN、i64 独立负测、i8 IR 执行探针、`udiv.ll:78` 文案更正；
- §8.6/§8.7 两层验收的脚本与验收固件；§8.5 符号解析来源归档。

未实测（本稿只有静态读数或历史存档，无本轮执行证据）：

- 全部语义向量（§8.2/8.4）——包括包装层 6 符号的任何一次真实执行（历史上从未跑过）；
- i8 四操作的 Promote 后执行语义（含 IR 层 `sdiv i8 -128,-1` 观察）；
- UB 观察档（除零、MIN÷(-1)）在自研库上的实际行为形态（返回值/终止/超时，v3 口径：不预设确定性输出）——SDCC 侧除零值（如 0xFFFF）是编译产物静态读数，未复测；
- i64 算术降级路径的当前失败形态（负测文案待锁定）；
- lld 对叶 OSEG 组同址取 max 的实际行为（SPEC 已规定，链接器实现状态归 E1 审批，未实测）；
- 真机：**无任何本轮真机证据**；本稿全部证据为【S】静态核验 +【Q】历史 QEMU 存档（§0 图例）。**（v3 注：本条是证据等级声明【H】图例的静态事实，不属待办；设计实施完成不改变本轮证据等级，也不要求"归零"——只随未来真机实测如实升级图例。）**

---

## 附录 A：证据文件绝对路径索引（v2 更新）

- 后端：`C:\Prj\LLVM\MCS251\llvm\lib\Target\MCS251\MCS251ISelLowering.cpp`（:56-61, :62-71, :226-229, :418-426 makeWord, :471-507 I32ABIRegs+splitI32ToBytes, :1409-1425 大端槽存储, :1627-1633, :1743-1762, :1827-1836, :1840-1846 入参）；`MCS251InstrInfo.cpp`（:173-182, :212-220 COPY 方向）；`MCS251CallingConv.td`（:3-27）；`MCS251RegisterInfo.td`（:18-19, :41-44, :50-56, :108-109, :129-131 DPTR 别名 DR56, :193-211, :250-259）；`MCS251RegisterInfo.cpp`（:24-46, :48-70）；`MCS251FrameLowering.cpp`（:3-25 帧模型与 SPX 逐位恢复、:88-96 DR16/DPX 取舍、:123-139 RESTORESP/POPFP）；`MCS251AsmPrinter.cpp`（:206-248 叶/非叶与槽节）；`MCS251TargetMachine.cpp`（:39 TargetMachine DataLayout 取 `TT.computeDataLayout()`）
- 前端/驱动/DataLayout（v3 新增）：`C:\Prj\LLVM\MCS251\clang\lib\Driver\ToolChains\Clang.cpp`（:1550-1574 mcs251 契约物化，默认 xsmall）；`clang\test\Driver\mcs251.c`（:16-26 默认 → `1,2,32,8,1`，"默认 + 三种显式模型"映射）；`clang\lib\Basic\Targets\MCS251.cpp`（:56-59 布局经 getLayoutDesc 校验）；`clang\test\CodeGen\mcs251.c`（:10-11 DEFAULT=v2 布局串 / COMPAT=compat 布局串）；`llvm\lib\TargetParser\TargetDataLayout.cpp`（:604-608 triple → compat 布局）；`llvm\lib\TargetParser\MCS251TargetParser.cpp`（:26-31 布局串常量、:34-52 getLayoutDesc、:58-89 契约解析与校验）
- libcall 名：`C:\Prj\LLVM\MCS251\llvm\include\llvm\IR\RuntimeLibcalls.td`（:1140-1160 标准名族；:2952-2960 mcs251 块）
- lit：`C:\Prj\LLVM\MCS251\llvm\test\CodeGen\MCS251\udiv.ll`（:78 待更正文案）、`divrem-errors.ll`（9 片段/10 负 RUN）、`oseg-errors.ll`（i64 参数拒绝，保留）、`call-sequences.ll`（CALLSEQ 口径）、`spill-across-call.ll`、`frame.ll`/`frame-o0.ll`
- 链接器：`C:\Prj\LLVM\MCS251\lld\MCS251\LinkerCore.cpp`（:270-285 note 校验；:287-303 loadFile 仅 ELF32BE 单对象）
- SDCC 镜像：`C:\Prj\LLVM\MCS251\sdcc-upstream\build-smoke\device\lib\mcs251-large\`（`_divuint.asm:119-128`、`_divulong.asm:119-139`、`_divsint.asm:180-184` push ar3、`_divsint.asm:55` XSEG）；`C:\Prj\LLVM\MCS251\sdcc-upstream\device\lib\_divuint.c` 等 8 源（`_divsint.c:206-216`、`_modsint.c:207` 的 `/ %` 与 signed 取负）；`device\lib\mcs251\Makefile.in`（:16-24 安装库清单缺口）
- 规范：`C:\Prj\LLVM\MCS251\validation\mcs251-elf\SPEC.md`（:27, :84-88 等价域排除, :154, :176-192 OSEG 规范, :246-258, :281-283）；`C:\Prj\LLVM\MCS251\validation\mcs251-models\DESIGN.md`（:1155-1166 槽/overlay/EDATA v2 裁定）
- 验证：`C:\Prj\LLVM\MCS251\validation\mcs251-firmware\provider.asm`（:113-123 槽区，:131-134 __mulint 字节序）；`validation\mcs251-demo-test\run-tests.py`（Oracle 口径）与 `t1\temperature-lookup\EXTRACT.md`（muldiv 转正记录【Q】）；`validation\mcs251-demo-modern\Makefile`、`run-qemu.py`、`build\*.ll`（udiv=0 证据）；本稿：`validation\mcs251-models\proposals\SIGNED-DIV-REMAINDER-DESIGN.md`
- WSL 侧证据源：`/home/liu/sdcc-src/device/lib/`（上游源）、`/home/liu/build-sdcc/`（SDCC 4.6.0 工具）、`/home/liu/mcs251-provider-moka/harvest/`（harvest 存档）
