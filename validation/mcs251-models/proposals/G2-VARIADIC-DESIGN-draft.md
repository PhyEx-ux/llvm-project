# G2 一般变参（variadic）缺口：调查报告与 B1 设计稿（修订版 R3）

状态：**REVISED / 设计定稿待实施授权**。路线已由 PM 裁定：**直接做 B1
（静态槽延续式真变参 ABI）**；改写消除路线（A）不再考虑；runtime
printf/sprintf **必须迁移为变参定义**（不再是决策点）。本版按 Alice 设计
评审 CHANGES REQUESTED 四条阻断完成 R2 修订，并按 Alice 第三轮复审两条
阻断完成 R3 修订：①va_list 改为 {来源槽区基址, 偏移} 二元组表示并给出
可发射消费语义与真实 clang 定义路径（§4.3.3-5）；②runtime 迁移按现源码
事实纠错（sprintf 现定义无 buf 参数；§4.7）。R2 修订内容：ABI 规范化到
可实施（§4.3）、cap 跨 TU 契约（§4.4）、runtime 迁移定稿（§4.7/§6 S4）、
切片依赖与验收写死（§6）。修订探针基线：工作树 minimal-isr @
2e08e94ae（2026-09-14），clang=/home/liu/build-mcs251-s1/bin，
llc=/home/liu/build-mcs251/bin，lld=/home/liu/build-mcs251-lld/bin/
mcs251-lld，探针产物 /tmp/g2var-probe2/（R2）、/tmp/g2var-probe3/（R3，
均不入库）。

---

## 0. 结论速览（R3 更新）

| 问题 | 一句话结论 |
|---|---|
| 路线 | **B1 已裁定**（PM 2026-09-14）：真变参 ABI，静态槽延续式；方案 A 仅存档（§3），不再实施 |
| 槽布局 | 实测钉死：**连续 1B 粒度、无对齐垫**——`_PARM_n` 按符号寻址、全部访问 Align(1)（§4.3.1，探针 A 实证 4B 槽落在偏移 0x1） |
| 槽宽 | 变参延续槽**统一 4B**：默认提升后实参只可能是 i32/f32（目标 double==f32）/4B 指针（§4.3.2）；固定槽宽度沿现状（1/2/4B） |
| cap | **N=6，编译器内固定常量，无命令行选项**（§4.4）：语料单点最大 6 + runtime 引擎恰 6 槽（探针 R 逐字节实证），余量以">6 调用方编译期硬错"的 fail-closed 形式提供 |
| va_list | **{来源函数槽区基址, 字节偏移} 二元组**（8B 局部对象）：基址指针值携带来源槽区身份，va_list 可传给任意普通辅助函数消费（R3 钉死，§4.3.3-5）；`__builtin_va_list` 经既有 `BuiltinVaListKind` 机制定义（无 getVAListDeclaration 接口），va_arg 经 `MCS251ABIInfo::EmitVAArg` 覆盖在 clang CodeGen 发射 if-chain（R3 探针 V 实证该 IR 形态今日 llc 可选） |
| runtime | 迁移定稿（§4.7，R3 按现源码纠错）：printf 改 `void printf(const char*, ...)`（固定参 7→1，槽 `_PARM_2..7` 不变）；**sprintf 现源码是 `fmt+6` 定参、无 buf 参数（:638-652 实读）**，改 `void sprintf(char *buf, const char *fmt, ...)`（固定参 7→2，buf 由调用方提供，变参槽 `_PARM_3..8`，get_arg/槽声明按函数分支改读）；删具名 a0..a5 与镜像回存；P-4 记录 bit3 0→1、param_count 7→1 / 7→2 |
| 切片 | S1→S2→S3→S4 依赖图与逐片验收写死（§6），无开放项 |

---

## 1. 现状实测（探针，2026-09-14）

### 1.1 Sema/clang 层：普通变参无任何拒绝

探针 `/tmp/g2var-probe/p1-vararg-int.c`（`int sum_ints(int n, ...)` 内
va_arg int，另含变参调用点）：

```
clang --target=mcs251-unknown-unknown -S -emit-llvm -O0  → exit 0
```

IR 含完整变参形态：`define i32 @sum_ints(i32 noundef %0, ...) addrspace(4)`、
`llvm.va_start/va_end`、`va_arg ptr %3, i32`、调用侧
`call i32 (i32, ...) @sum_ints(i32 3, i32 10, i32 20, i32 30)`。
混合 char/指针变参（p3-mixed.c）同样 clang exit 0。
仅声明+调用（本 TU 无定义，p4-callonly.c）clang 同样通过。

结论：**clang/Sema 对普通（非 bit）变参声明、定义、直接调用、间接调用一律
放行**；`__builtin_va_list` 现走 MCS251.h:218-221 的 `CharPtrBuiltinVaList`
（即 `typedef char* __builtin_va_list`，R3 探针 T 实测 `sizeof(va_list)==4`），
且 `clang/lib/Basic/Targets/MCS251.h:172-176` 现有注释自认
"va_arg is fully usable on this target for ordinary types"——该表述仅对
clang 层成立，见 1.3。

### 1.2 唯一的 Sema 冻结拒绝：N13（bit 变参签名）——与一般变参不同层

- 声明侧：`clang/lib/Sema/SemaType.cpp:5464-5474`（`EPI.Variadic` 且参数或
  返回含 bit → 拒绝；注释引 P09 §6.3 N13）。
- 调用侧（typedef/typeof 间接签名）：`clang/lib/Sema/SemaMCS251.cpp:1594-1601`
  （`CheckMCS251BitCallForm`）。
- 诊断定义：`clang/include/clang/Basic/DiagnosticSemaKinds.td:12780-12787`，
  `err_mcs251_bit_call_unsupported`，类别串冻结（"variadic"/"no-prototype"/
  "multi-argument indirect"）。
- 实测文案（p2-n13.c，`int f(int a, __bit b, ...);`）：

```
p2-n13.c:3:1: error: MCS251 bit call form 'variadic' is not supported
```

N13 的边界（P09 §4 冻结值 ABI 推论）：bit 值 ABI 给每个源参数唯一
DPL/`_PARM_n` 位置，`...` 没有槽位。**本文方案 B 不触碰 N13**（见 §4.6）。

### 1.3 llc 层：一般变参唯一拒绝点，文案唯一（B1 实施对象）

`llvm/lib/Target/MCS251/MCS251ISelLowering.cpp` 共 4 处同一文案的
`report_fatal_error`：

| 位置 | 函数 | 触发条件 | B1 处置 |
|---|---|---|---|
| :3330-3332 | `LowerFormalArguments` | 定义侧（普通 CC 入口处） | 摘除 |
| :3473-3475 | `LowerCall` | 调用侧（`CLI.IsVarArg`，含 call-only 与间接） | 摘除；`IsVarArg && !IsDirect` 新增后端拒绝（双保险，§4.4.3） |
| :3692-3694 | `CanLowerReturn` ISR 分支 | ISR 且变参 | **维持**（T05 冻结） |
| :3699-3704 | `CanLowerReturn` 普通分支 | **首个触发点**（FLI 早于 LowerFormalArguments） | 摘除 |

文案唯一（4 处逐字一致）：

```
LLVM ERROR: minimal MCS251 backend does not support variadic functions
```

实测三种形态均落到同一文案：定义+自调（p1）、call-only（p4）、间接调用
（p5，需 `-mcs251-object-format=elf`；非 ELF 模式先被既有的 v2 身份门
拦住，属另一道闸；R2 探针 pA 首次 llc 未加该旗即复现此门）。

后端能力现状：grep 全后端 `VASTART/VAARG/VAEND/VACOPY` **零命中**——即
`LowerVASTART` 等 hook 从未实现，va_list 无任何表示。另查 `ISD::TRAP`
后端零命中（R3 探针 V3 纠正：`llvm.trap` 经通用 legalizer 展开为
`_abort` libcall 而非 "Cannot select"，但 runtime 无 abort 提供者——
§4.3.6 的溢出停机仍需新增 1 条 `VARARG_HALT` 伪指令，不借道 libcall）。

### 1.4 P-4 签名线格式：已变参就绪（零改动）

`P4-SIGNATURE-PROTOCOL-FREEZE.md`（已冻结）：Tag 28 role bit3 = variadic，
`param_count` 仅计固定形参，跨对象比较要求两侧 bit3 一致（:97-98）。
比较实现 `llvm/lib/BinaryFormat/MCS251Signatures.cpp:208-266`
（`compareRecords`）：bit3 分歧 → `records '_X' disagree on variadic-ness
(bit3 differs)`；param_count 分歧 → `records '_X' have a parameter count
conflict (pathA vs pathB): N vs M`。reader 侧跨对象比较
`lld/MCS251/LinkerCore.cpp:1902-1955`（`validateSignatureSet`）。
writer 已实现：`clang/lib/CodeGen/Targets/MCS251.cpp:224-235`
（`Role_Variadic`、`ParamCount = NoProto ? 0 : getNumParams()`——固定形参；
`RetBit` 仅记返回**是否 bit**，void/int 等返回类别不入记录）。
探针 IR 实证：`!4 = !{!"_sum_ints", i32 9, i32 0, i32 0}`
（9 = bit0 定义 + bit3 变参；ret=0；唯一固定参数 non-bit）。
记录粒度覆盖"本 TU 声明或定义的全部外部链接函数"（P4 冻结文件）——
**仅声明的调用 TU 同样携带记录**，跨 TU 比较因此成立。

结论：**若解冻变参，P-4 线格式、writer、lld reader 均无需改动**。

### 1.5 静态槽机制现状（B1 的承重结构）

- 槽符号：`parameterSlot()`（MCS251ISelLowering.cpp:3245-3251）=
  `\1<mangled callee>_PARM_<n>`，n=源序（首源参数占 DPL/DPTR/DPL:DPH:B:A
  寄存器、**无槽**；第二源参数 = `_PARM_2`）。目标 `UserLabelPrefix="_"`
  （MCS251.h:48）。
- 调用方写槽：`LowerCall`（:3544 CALLSEQ_START 后、:3560-3570）串行写完
  所有槽（`Outs[I] → _PARM_(I+1)`，I 从 1 起）；4B 指针先
  `canonicalizePointer32`（:3564-3565）；全部 store `Align(1)`。
- 被调方读槽：`LowerFormalArguments`（:3403-3413）入口先于任何调用逐个
  load（`Align(1)`）；i32 指针槽同样 `canonicalizePointer32`（:3410-3411）。
- 槽发射：`MCS251AsmPrinter.cpp:1280-1336`（`emitParameterSlots`），
  仅对 **IR 固定形参**（`F.args()` 跳过首个）逐参发射，槽宽 =
  `TypeStoreSize(ArgTy)`（:1321；i8→1B、i16→2B、i32/f32/普通指针→4B），
  `emitZeros` 占位（:1332）、非 local 发 `MCSA_Global`（:1324-1325）、
  ELF 加 STT_OBJECT+size（:1327-1331）；区选择：叶函数 OSEG（可重叠）、
  非叶 DSEG（:1289-1294）。**门 `if (F.arg_size() < 2) return;`
  （:1282-1283）是 B1 槽补发必须修改的点**（printf 恰好 1 个 IR 固定形参）。
- 槽引用声明：AsmPrinter :1338-1348 把代码引用到的槽符号发 Global 声明。
- 运行时 printf/sprintf 现状（`validation/mcs251-runtime/src/mcs251_printf.c`，
  R3 探针 S 只读复核）：两定义均为**固定 7 参 `fmt + uint32_t a0..a5`、
  均无 buf 参数**（printf :626-635、sprintf :638-652），体内镜像回存
  （:629-632、:641-642）；sprintf 输出走全局 `g_sprintf_buf[24]`
  （:40-42）+ `g_output_mode` 分流（:77、:82-92），验收读取口
  `sprintf_reset/len/getc/str`（:44-69）；格式化引擎经 `get_arg`
  （:469-481）读 `printf_PARM_2..7` / `sprintf_PARM_2..7`（extern
  volatile uint32_t，:464-467）——即运行时已经按"静态槽延续"的私有约定
  消费变参，上限 6 槽（引擎 `ai < 6u` 门）。
- demo 调用方原型（rewrite 包冻结口径，`rewrite.py:640-671`）：就地声明
  `int printf(const char *fmt, ...);`、`int sprintf(char *buf, const char
  *fmt, ...);`——**已是变参原型**；runtime 无公共 printf 头
  （`mcs251_libc.h:11` 明示不含 printf/sprintf/putchar）。

### 1.6 R2 新探针：槽位分布实测（钉死 B1 续写语义）

探针产物 `/tmp/g2var-probe2/`（clang -S -emit-llvm -O0 +
`llc -mtriple=mcs251-unknown-unknown -mcs251-object-format=elf -filetype=obj`，
`llvm-readelf -sW` 读符号表）。

**探针 A（固定参混合签名，pA-def.c/pA-def.o）**：
`long mix(long a, char b, long c, short d, const char *e, int f, char g, int h)`
（首参 long 走 DPL:DPH:B:A 寄存器，其余 7 参走静态槽），实测符号表：

```
Num  Value    Size Type   Ndx Name
  1  00000000    1 OBJECT GLOBAL 5 _mix_PARM_2   ← char b
  2  00000001    4 OBJECT GLOBAL 5 _mix_PARM_3   ← long c，4B 槽落偏移 0x1（奇地址）
  3  00000005    2 OBJECT GLOBAL 5 _mix_PARM_4   ← short d
  4  00000007    4 OBJECT GLOBAL 5 _mix_PARM_5   ← const char* e
  5  0000000b    4 OBJECT GLOBAL 5 _mix_PARM_6   ← int f
  6  0000000f    1 OBJECT GLOBAL 5 _mix_PARM_7   ← char g
  7  00000010    4 OBJECT GLOBAL 5 _mix_PARM_8   ← int h
```

结论（探针 A 钉死三条）：
1. **连续 1B 粒度、零对齐垫**：1B 槽后紧跟的 4B 槽落在偏移 0x1（奇地址），
   2B 槽落 0x5、0xf——证明契约里**不存在对齐规则**；
2. 槽按**符号**寻址（`_PARM_n` 名字即地址），双方（caller 写 :3566-3568 /
   callee 读 :3404-3407）只对符号、不对偏移做假设，布局由 AsmPrinter
   `emitZeros` 串行累积唯一确定 → **槽宽冻结即可保证跨 TU 一致**；
3. 4B 槽 1B 对齐访问是既有合法化路径（i32 load/store → 4×i8 大端组合，
   LowerCall :3561 注释"same measured big-endian layout as SDCC"），
   已被 149/149 基线覆盖，非新增风险。

调用方侧同探针（pA.s）实测 caller 以既有串行写槽形态写
`_mix_PARM_2..8` 全部 7 槽。

**探针 R（runtime printf 现状对象，pR.o）**：
`mcs251_printf.c` 以目标链编译（-O1），实测：

```
_printf_PARM_2..7   offset 0x0,0x4,0x8,0xc,0x10,0x14   size 4 each   （DSEG sec 5）
_sprintf_PARM_2..7  offset 0x0,0x4,0x8,0xc,0x10,0x14   size 4 each   （DSEG sec 6）
```

结论：现状固定 7 参定义的 6 个 u32 槽 = **6×4B 连续**——与 B1
"1 固定参 + 6 个 4B 延续槽"的布局**逐字节相同**（R3 纠错：此等价性
**仅对 printf 成立**；sprintf 迁移后为 2 固定参 + 6 延续槽 = 7 槽
28B，§4.7）。

**探针 B（变参 IR 形态，pB.ll）**：
`int sum(int n, ...)` + 多形态调用点，clang -O0 实测：

```
define dso_local i32 @sum(i32 noundef %0, ...) addrspace(4)
  call void @llvm.va_start.p0(ptr %3)        ← va_list 局部对象地址
  %12 = va_arg ptr %3, i32                   ← intrinsic 形式
call ... @sum(i32 noundef 2, i32 noundef 10, i32 noundef 20)   ← char 实参 'x' 已提升 i32(120)
call ... @sum(i32 noundef 1, i64 noundef 287454020)            ← i64 原样到 IR
call ... @sum(i32 noundef 1, ptr noundef byval(%struct.S) align 1 @gs) ← 聚合→byval
```

结论：IR 层变参调用实参已完成默认提升（Sema/CodeGen 标准路径），
i64/聚合分别以 i64/byval 到达后端（今日由 `checkParameter`
:3292-3306 fatal 兜底，B1 前移到 Sema，见 §4.4.3）；`llvm.va_start` 收
`ptr`（va_list 对象地址）（R3 更新：va_list 定为 8B 二元组结构体后此
形态不变；va_arg 改由 clang `MCS251ABIInfo::EmitVAArg` 发射、不再走
intrinsic，后端工作收敛为 VASTART/VAEND/VACOPY 三 hook + VAARG 显式
拒绝，见 §4.3.3-5）。

---

## 2. demo 需求画像（分水岭证据，R1 结论不变）

### 2.1 六 demo 变参调用点全清单（改写副本，含 GBK 解码后逐点核对）

| demo | TU | 活跃 printf 调用点 | 变参实参谱（每点） | 定义侧变参 |
|---|---|---|---|---|
| 13-8个串口 | main.c | 1 | 0 个（纯字符串） | 无 |
| 48-LIN2从机 | main.c:168,177,178,179 | 4 | 0..1 个（`isr` u8→int） | 无 |
| 50-LIN双从 | main.c:166,175,176,177,186,195,196,197 | 8 | 0..1 个（`Read_ID1/2`、`RX1/2_BUF[i]`） | 无 |
| 59-DMA-UART | 8 个 TU（UART1.c 1 + UART.c 各 2） | 15 | 0..1 个（`i`、`i--`）；`printf("DMA buffer full.\r\n",i--)` 为无格式符的多余实参（C 允许） | 无 |
| 60-DMA-I2C | I2C.c | 11 | 0..1 个（`j`、`DmaRxBuffer[i]` char→int） | 无 |
| 68-IO休眠唤醒 | main.c:198,208 | 2 | 0..1 个（`ioIndex`） | 无 |
| **合计** | | **41** | 每调用点变参实参 **≤1** | **0** |

关键负证据（六 demo 与全语料双重核查）：

- 全改写语料 `grep va_list|va_arg|va_start|va_end` → **零命中**；
- 全语料 demo 自写 `...` 函数定义 → **零命中**（唯一 `...` 命中是注释）；
- 全语料变参实参谱：仅整型标量（u8/u16/u32/char，默认提升后为 int）；
  **无 `%s`、无指针变参实参、无浮点变参实参**；
- 全语料单调用点变参实参最大数 = 6（RTC 时钟 demo 的
  `printf("Year=20%d,...Second=%d\r\n", RTCYEAR..RTCSEC)`）；
- printf 调用所在上下文：均在 main/主循环路径（抽查 59：UART2_int ISR
  内无 printf），无 ISR 内变参调用点。

### 2.2 受影响面口径（与 G1-4 记账对账）

- G1-4 残余 gap 归因 G2 的六 demo：13,48,50,59,60,68
  （`/home/liu/LLVM_STC32/G1-4-PROGRESS.md:198-211`，其中 59 记
  "24 站点 18 槽全 Legal"为该 demo 多 TU 站点数）。
- 防吞并核对：全 67 demo 中 variadic 形态共 **15** 个（上 6 +
  33,35,39,46,55,56,57,67,85；G1-4-PROGRESS.md:215）。改写包 README §8
  G2 行单列 9 个（README.md:332），两者相加恰为 15，口径一致。
- scan.json 六 demo 各 TU 均有 `printf: true` 语料标记。

### 2.3 改写包历史裁定核查（分水岭核心，已核清）

`mcs251-demos-rewritten/tools/rewrite.py:640-671`（`fix_missing_includes`）：
demo 就地声明**变参原型**（`int printf(const char *fmt, ...);`）、T0 通过、
T1 记产品缺口（G2），记 `include` note；不把调用点改写成运行时的私有定参
ABI。README.md:238-239 同口径。

**改写未引入、也未消除该形态**：原始语料（GBK 编码）本身携带 printf 调用
（实测：`STC32G144K246-DEMO-CODE/13-.../main.c` GBK 解码后第 69 行即
`printf("STC32G UART Test Programme!\r\n");`，六 demo 逐一成立；原始语料
面向 Keil C251，其 C 库 printf 本就是变参）。改写副本仅补了就地声明。

**分水岭判定（已被 PM 裁定吸收）**：需求真实保留；B1 实施后六 demo 以
源码零改动直链迁移后的变参 runtime printf。

---

## 3. 方案 A：改写消除（已否决，存档）

**PM 裁定（2026-09-14）：不采用。** 本节仅存档 R1 调查结论，防止重复
论证：

- 仅改声明为运行时定参原型不可行（C 无默认实参，0/1 实参混存必硬错）；
- 操作形态曾列为 A1（按实参个数分发定参入口 `printf0..printf6` +
  rewrite.py `printf-arity` 规则改写 41 处调用点 + 四处记账更新）；
- 否决理由：产品能力不变（自写 `...`、va_list 仍被拒），源码偏离官方
  语料 41 处，且 PM 判定产品需要 C 语言级变参能力。

---

## 4. 方案 B1：静态槽延续式真变参 ABI（R2 定稿）

### 4.1 设计目标与不变量

1. 固定形参路径**零改动**（P09 §4：首源参数 DPL 完整 i8/其余源参数
   `_callee_PARM_n` 原源序静态槽）；
2. P-4 Tag 28（bit3/固定形参计数）线格式、writer、lld **零改动**；
3. N13（bit 变参签名）、ISR×变参（T05）维持冻结拒绝；
4. runtime mcs251_printf.c 迁移后的对象级槽布局（R3 纠错，探针 R）：
   **printf 逐字节不变**（6×4B 连续，`_PARM_2..7` 偏移 0x0..0x14）；
   **sprintf 现定义无 buf 参数（fmt+6 定参），迁移为
   `sprintf(buf, fmt, ...)` 后变 7 槽**（`_PARM_2`=fmt + 6 个延续槽
   `_PARM_3..8`，7×4B=28B，较现状 +4B），get_arg/槽声明随之改读
   `_PARM_3..8`（§4.7）。

### 4.2 源语言层语义（提升与允许集）

- 变参实参按 C 标准默认提升：char/short → int（i32）。目标
  `DoubleWidth=32` 且 `DoubleFormat=IEEEsingle`（MCS251.h:32-35 实测），
  标准的"float→double 提升"在本目标退化为 f32→f32 恒等——**不存在
  8 字节浮点槽，无 ABI 分叉**；语料实测零浮点变参实参（§2.1）。
- 变参实参/`va_arg` 目标类型的**允许集**（提升后）：
  i8/i16/i32（含枚举/位段提升结果）、f32、普通数据/CODE 指针
  （地址空间 ∈ {0,1,2,3,4,8,9}，沿 `hasOrdinaryPointerABI`
  MCS251ISelLowering.cpp:3255-3271 既有边界）。
- 拒绝集（三条路径全拒绝，文案/时机见 §4.4.3）：聚合/union、i64
  （`long long`）、非常规地址空间指针、bit（N13 既有）。

### 4.3 ABI 规范（可实施级）

#### 4.3.1 槽布局与对齐（阻断 1-A）

- 槽符号命名：`<mangled callee>_PARM_<n>`（`parameterSlot`
  :3245-3251 既有函数原样复用，含 `\1` 防二次 mangle 前缀）。
- **槽宽冻结表**：

| 类别 | 固定形参槽 | 变参延续槽 |
|---|---|---|
| i8（char/_Bool/…） | 1B | **不存在**（提升为 i32） |
| i16（short） | 2B | **不存在**（提升为 i32） |
| i32（int/long/枚举） | 4B | 4B |
| f32（float/double，二者同型） | 4B | 4B |
| 普通指针（数据/CODE） | 4B | 4B |

- **对齐规则：无**。实测钉死（§1.6 探针 A）：槽区为连续 1B 粒度字节流，
  各槽符号由 AsmPrinter 按 IR 实参顺序 `emitZeros(槽宽)` 串行累积定义，
  4B 槽可落在任意字节偏移（实测偏移 0x1）；双方全部 load/store 显式
  `Align(1)`（:3406-3407、:3566-3568）。**正确性不依赖对齐**——寻址是
  纯符号式的，任何一侧都不计算槽内偏移。
- 字节序：槽内多字节值 = DataLayout 大端（与 SDCC 同源，
  LowerCall :3561 注释 + 既有 4B 槽合法化路径，探针 A 覆盖）。
- 指针槽：写入前 `canonicalizePointer32`（A 字节置 0 规范化，
  caller :3564-3565 / callee :3410-3411 既有处理，变参路径同）。

#### 4.3.2 槽位推进算法（阻断 1-B）

设被调方固定形参个数 `F = F.arg_size()`（IR 事实，双方一致由 P-4
param_count 跨对象比较保证），变参实参 0 起 k：

- **caller（LowerCall）**：变参实参 k 写槽
  `_PARM_(F + 1 + k)`；每实参恰占 **1 槽 / 4B**（提升后类型统一 4B，
  §4.3.1 表）。因 IR 调用实参在 `Outs` 中按源序扁平排列且既有循环已实现
  `Outs[I] → _PARM_(I+1)`（:3562-3569），**该循环零改动即天然实现延续
  编号**（固定槽 2..F 与变参槽 F+1.. 是同一序列的两段）。写时序并入
  CALLSEQ_START..CALL 既有串行区间（:3544 起），先于首参寄存器建立。
- **callee（va_arg）**：见 4.3.5，每次推进恰 1 槽（4B），与实参源类型
  宽度无关（提升已消灭 1B/2B 实参）。
- **cap 溢出（"第 7 槽"）**：不存在溢出区——调用方超过 6 个变参实参在
  编译期硬错（§4.4.1），被调方 va_arg 越界运行期确定性停机（§4.3.6）。
  运行时对象里**永不出现**第 7 延续槽。

#### 4.3.3 va_list 表示（阻断 1-C，R3 重设计：携带来源槽区身份）

**R2 的"槽位号"表示被 Alice 第三轮复审否决**：纯槽位号不携带来源函数
身份，`va_list` 传给辅助函数后，辅助函数内的 va_arg 会读**辅助函数自己的**
`_PARM_n` 槽。R3 定稿：va_list 必须是 **{来源函数槽区基址, 字节偏移}
二元组**，基址**指针值**即来源槽区身份。

**(a) `__builtin_va_list` 的真实定义路径（R3 纠错：本树无
`getVAListDeclaration` 接口）**

R2 所引 `getVAListDeclaration()` 覆盖不是本树 TargetInfo 的现存接口。
本树的真实机制（R3 逐处实读）：

- 类型种类枚举：`clang/include/clang/Basic/TargetInfo.h:339-388`
  （`enum BuiltinVaListKind`，含 `CharPtrBuiltinVaList=0`…`XtensaABIBuiltinVaList`）；
  纯虚入口 `TargetInfo.h:1052`（`virtual BuiltinVaListKind
  getBuiltinVaListKind() const = 0`）。
- MCS251 现状：`clang/lib/Basic/Targets/MCS251.h:218-221` 返回
  `CharPtrBuiltinVaList`（`typedef char* __builtin_va_list`；R3 探针 T
  实测 `sizeof(va_list)==4`）。
- 类型构造：`clang/lib/AST/ASTContext.cpp:10425-10448`
  （`CreateVaListDecl(Context, Kind)` 按 Kind 分发到各
  `Create*VaListDecl`；先例 `CreateSystemZBuiltinVaListDecl`
  :10281 = struct×[1] 形态、`CreateXtensaABIBuiltinVaListDecl` :10389），
  缓存入口 `ASTContext::getBuiltinVaListDecl()` :10451-10458。
- 头文件链（**零改动**）：`__stdarg___gnuc_va_list.h:12`
  （`typedef __builtin_va_list __gnuc_va_list;`）→
  `__stdarg_va_list.h:12`（`typedef __builtin_va_list va_list;`），
  宏映射 `__stdarg_va_arg.h:14-20`、`__stdarg_va_copy.h:11`——新 Kind
  落地后 stdarg.h 全套自动成立。

**设计（B-S2 落地）**：在 `TargetInfo.h:338-388` 枚举尾新增
`MCS251BuiltinVaList`，MCS251.h:218-221 改返回之；`ASTContext.cpp`
switch 新增 case，构造：

```c
typedef struct __va_list_tag {
    void *__base;        /* 4B：来源函数变参槽区基址（来源槽区身份） */
    unsigned int __off;  /* 4B：下一待取字节偏移（0,4,8,...,20） */
} __va_list_tag[1];
```

- 单元素数组（`[1]`）形态使 `va_list ap` 在表达式中衰减为"指向 8B
  对象的指针"，且禁止按值结构体拷贝（强制走 va_copy，SystemZ 同型
  先例 :10281-10335）；
- 目标 DataLayout 全类型 1B 对齐（R3 探针实测
  `target datalayout = "E-...-p:32:8:8:32-...-i32:8-..."`）→ 结构体
  恰 8B、无填充：`__base`@0，`__off`@4；
- `va_list` 局部对象 8B，DSEG 帧内；作参数传递时按指针传（衰减）。

**(b) 消费语义：基址值即身份（R3 探针 V 实证可发射）**

`__base` 在 va_start 时被物化为**本函数**首变参槽
`_thisFunc_PARM_(F+1)` 的地址值（后端经 `parameterSlot` 既有机制：
`getExternalSymbol("\1"+func+"_PARM_"+n)`，MCS251ISelLowering.cpp
:3245-3251——槽符号本就是一等 SDValue，:3403-3413 被调方读槽 /
:3544-3570 调用方写槽同源）。此后一切 va_arg 只做
`addr = __base + __off` 的**指针算术 load（Align(1)）**，不再查任何
符号：

- **va_list 传给普通辅助函数**：`void helper(va_list ap)` 形参即
  `ptr`（指向 owner 的 8B 对象）；helper 内 va_arg 经该指针读出
  `__base`——基址**值**指向 owner 的槽区，与 helper 自身的
  `_helper_PARM_n` 完全无关。逐级转发（helper→helper2）同理，只移
  指针。R3 探针 V 的 `@consume(ptr %ap)` + `@own_setup` 组合即此
  语义，今日 llc 全部 select 成功（v2e.o，146B）。
- **owner 侧基址物化**：符号地址作值 store（`store ptr @_own_PARM_2`
  形态）R3 探针 V `@own_setup` 实证今日可选（52B）；后端实现时
  LowerVASTART 直接以 `parameterSlot(MF 符号, F, DAG)` 为 store 值。

**(c) IR 形态样例（R3 探针 V 全文实测，/tmp/g2var-probe3/v2e.ll →
v2e.o 编译通过）**

```llvm
; owner（va_start 展开后的效果；B-S2 由 LowerVASTART 以 MIR 等价实现）：
define void @own_setup(ptr noundef %ap) addrspace(4) {
entry:
  store ptr @_own_PARM_2, ptr %ap, align 1        ; __base = 本函数 _PARM_(F+1) 地址
  %offp = getelementptr inbounds i8, ptr %ap, i32 4
  store i32 0, ptr %offp, align 1                 ; __off = 0
  ret void
}

; 辅助函数消费（身份在 __base 值里，helper 不查任何 _PARM_ 符号）：
define i32 @consume(ptr noundef %ap) addrspace(4) {
entry:
  %offp = getelementptr inbounds i8, ptr %ap, i32 4
  %off  = load i32, ptr %offp, align 1
  %ok   = icmp ule i32 %off, 20                   ; 5*4：第 6 槽仍合法
  br i1 %ok, label %load, label %halt
halt:
  call void @mcs251_halt()                        ; B-S2 实装 = VARARG_HALT
  unreachable                                     ; 停机臂不回流 → 结果无 PHI
load:
  %base = load ptr, ptr %ap, align 1
  %addr = getelementptr inbounds i8, ptr %base, i32 %off
  %v    = load i32, ptr %addr, align 1            ; 4B 槽，既有 4×i8 大端组合
  %offn = add i32 %off, 4
  store i32 %offn, ptr %offp, align 1             ; 推进 4B（1 槽，类型无关）
  ret i32 %v
}
```

窄类型/浮点读法（同探针实测可选）：`va_arg(ap,char/short)` = load i32 +
`trunc`（提升互逆，§4.3.5 旧条目语义保留）；`va_arg(ap,float)` = 直接
`load float, align 1`（位型同槽）；指针 = `load ptr, align 1`（caller
写槽时已 canonicalizePointer32，A 字节已规范，读侧无需再处理）。

**(d) R2 "if-chain 槽选择"条目作废**：{基址,偏移} 表示下**不存在**
逐槽符号 if-chain；值上下文的多终点能力以探针 V 的 PHI 变体
（`@consume_phi`：`%r = phi i32 [ %v, %load ], [ 0, %entry ]`，246B）
实证可发射兜底——若未来停机臂需回流 join 块，phi 形态今日即被 llc
支持。运行期越界仍为确定性停机（§4.3.6）。

#### 4.3.4 va_start（阻断 1-C）

- 源级 `va_start(ap, last)` → IR `call void @llvm.va_start.p0(ptr %apaddr)`
  （发射点 `clang/lib/CodeGen/CGBuiltin.cpp:898` `EmitVAStartEnd`，
  :3360 `BI__va_start`；探针 B 实测形态不变，%apaddr = 8B va_list
  对象地址，`EmitVAListRef` `CodeGenFunction.cpp:2711`）。
- 新增 `LowerVASTART`：经 %apaddr 向对象 store 二元组——
  `__base = parameterSlot(本 MF 符号, F, DAG)`（ExternalSymbol 作值，
  探针 V `@own_setup` 实证可选）、`__off = 0`。`F` =
  `MachineFunction::getFunction().arg_size()` 编译期可得；F=0
  （C23 `void f(...)` 形态）时基址 = `_PARM_1`，公式无特例。
- `F=1` 时基址 = `_PARM_2`——与 runtime printf 现状"首变参 =
  `_printf_PARM_2`"一致（探针 R）；`F=2` 时 = `_PARM_3`——与迁移后
  sprintf 一致（§4.7）。

#### 4.3.5 va_arg（阻断 1-C，R3：clang CodeGen 发射，非后端 intrinsic）

**发射入口（R3 纠错）**：`va_arg` 不再走 `llvm.va_arg` intrinsic。链路
实读：`CGExprScalar.cpp:6219` → `CodeGenFunction::EmitVAArg`
（`CGCall.cpp:6956-6970`）→ `CGM.getABIInfo().EmitVAArg(...)` :6969 →
默认 `DefaultABIInfo::EmitVAArg`（`ABIInfoImpl.cpp:77-82`）调
`EmitVAArgInstr`（:404-447）发射 `llvm.va_arg`。**设计：在
`MCS251ABIInfo`（`clang/lib/CodeGen/Targets/MCS251.cpp:51-53`，现仅
bit 分类）覆盖 `EmitVAArg`**，按 §4.3.3(c) 样例用 IRBuilder 直接发射
guard 分支 + load + 推进 store（允许任意控制流，无 SelectionDAG
多块值展开负担）。允许集内逐类型：

1. `i32/f32/指针`：§4.3.3(c) 样例原形（f32 直接 float load；指针 ptr
   load，A 字节 caller 侧已规范化）；
2. `i8/i16`：load i32 + `trunc`（与默认提升互逆）；
3. **推进与停机**：`__off += 4`；越界臂发射 VARARG_HALT 等价停机形态
   （探针样例中以注册外部 `@mcs251_halt` 占位，实装为 VARARG_HALT，
   §4.3.6）+ `unreachable`；结果值装载不经过 DPL（DPL
   仅首参传入与返回值两条既有通道）。

后端残留 `llvm.va_arg` 的兜底（手写 IR / 其他前端漏网）：B-S2 给
ISD::VAARG 挂显式拒绝 `report_fatal_error`（文案 E，§4.3.6 表），
不用 generic "Cannot select" 兜底。
`va_end(ap)` → `llvm.va_end` → `LowerVAEND` = no-op（保链）；
`va_copy(dst,src)` → `llvm.va_copy`（`CGBuiltin.cpp:3365-3368` 实测
发射点）→ `LowerVACOPY` = 8B 二元组经两指针整体复制（含"已部分
消费"状态；探针 V 证明两字段 store/load 形态可选）。
重入约束：变参槽为静态存储，与全部固定槽同属非重入约束类（既有约束，
非新增）；叶变参函数槽入 OSEG（可重叠）但叶函数无调用，槽生命期安全；
非叶变参函数槽入 DSEG，嵌套调用不覆盖（AsmPrinter :1287-1294 既有
区选择逻辑原样适用）。

#### 4.3.6 溢出/越界语义汇总（阻断 1-D，时机+文案写死）

| 情形 | 时机 | 行为 | 文案/编码 |
|---|---|---|---|
| 调用点变参实参 > 6 | **编译期，Sema**（调用表达式路由点） | error（硬错，非 warn） | §4.4.1 文案 A |
| **IR 调用侧变参实参 > 6**（Sema 漏网路径：手写 IR / 旧编译器对象源码重编 / 前端 bug） | **编译期，llc**（`LowerCall` 既有串行写槽循环 :3562-3569 处，`IsVarArg` 时对 `Outs.size() - NumFixedArgs > 6` 计数检查，NumFixedArgs = 被调 IR 固定形参） | report_fatal_error（不产出对象） | **文案 F（冻结）**：`LLVM ERROR: MCS251: variadic call passes more than 6 variadic arguments (Sema cap gate missed this call; recompile the caller with a current compiler)` |
| no-prototype 调用点实参-1 > 6 | 编译期，Sema（同点，语法可数） | error | 同上 |
| 间接（经函数指针）变参调用 | 编译期，Sema（B-S1） | error | §4.4.3 文案 C；后端 `IsVarArg && !IsDirect` report_fatal_error 双保险 |
| **残留 `llvm.va_arg` 到达后端**（clang 已改走 EmitVAArg，§4.3.5；手写 IR 漏网） | 编译期，llc（B-S2：ISD::VAARG 挂显式 Custom + LowerOperation → report_fatal_error，不用 generic "Cannot select"） | report_fatal_error | **文案 E（冻结）**：`LLVM ERROR: MCS251: llvm.va_arg is not supported; va_arg is lowered by clang CodeGen (MCS251ABIInfo::EmitVAArg)` |
| va_arg 目标类型 ∈ 拒绝集 | 编译期，Sema（BuildVAArgExpr） | error | §4.4.3 文案 D |
| 变参实参类型 ∈ 拒绝集（直接调用） | 编译期，Sema（同路由点逐实参查） | error | §4.4.3 文案 D；后端 `checkParameter`（byval/split/i64）fatal 兜底不变 |
| 运行期 va_arg 越过第 6 延续槽 | **运行期**（编译期不可数：循环 va_arg） | `VARARG_HALT` 死循环停机 | 编码 `sjmp self`（0x80 0xFE）；QEMU 哨兵负例进 S2 验收（§6） |

R3 探针 V3 纠正一条 R2 事实：`llvm.trap` 并非"Cannot select"——本后端
经通用 legalizer 把它展开为 `_abort` **libcall**（探针 V3 实测到达 v2
helper-ABI 门才被拦）；而 runtime 现源码无 abort 提供者（实读
mcs251-runtime/src 全目录零命中），且 libcall 形态引入调用帧、依赖
runtime 符号。**VARARG_HALT（`sjmp self`，2 字节、无调用帧、无 runtime
依赖）决定维持**，不借道 llvm.trap。

设计立场：**编译期可数的溢出一律硬错**（调用点实参个数永远是语法
事实），**编译期不可数的越界一律确定性停机**——两类都不存在静默数据
损坏路径。

### 4.4 cap 跨 TU 契约（阻断 2）

#### 4.4.1 cap 终值与依据：**N = 6，编译器内固定常量**

1. **语料实测上限恰 6**：全语料单调用点变参实参最大数 = 6（RTC 时钟
   demo，§2.1），六 demo 单点 ≤1——N=6 覆盖 100% 语料；
2. **runtime 引擎消费能力恰 6**：`get_arg` 6 分支（idx 0..5）、引擎
   `ai < 6u` 门（mcs251_printf.c:563 起 9 处）——迁移后的 printf 定义
   补发的 6 个延续槽与现状对象布局逐字节相同、sprintf 补发 6 延续槽
   （共 7 槽，探针 R + §4.7 纠错）。**零余量即零死槽**：
   若取 N=8，每个变参定义多付 2×4B DSEG（G8 RAM 预算直接压力），且第
   7/8 槽超出引擎消费能力，形成"能存不能取"的语义陷阱；
3. **余量以 fail-closed 形式提供**：>6 的调用在调用方编译期硬错
   （文案 A，§4.4.2），用户得到的是冻结的明确诊断而非静默截断或链接
   期悬符号——"未来需要更多实参"的扩展路径 = 显式 ABI 修订（见 4），
   不是预留死槽；
4. **无命令行选项**：R1 曾列 `-fmcs251-vararg-slots=N`（§4.2 决策点 #3）
   ——**废除**。选项化会制造"同一链接里 caller/callee N 不一致"这一
   P-4 无法观测的 ABI 分叉（Tag 28 不编码 N）。N 是编译器常量，双方
   独立编译必然一致；若未来修改 N，必须同步 bump P-4 `call_abi
   minor`（`compareRecords` 尾部已比较，LinkerCore 硬错拦旧对象），
   该要求写入文案 A 的开发者注释。

#### 4.4.2 调用方 >cap 诊断（冻结）

- 层：**clang/Sema**（调用表达式构造前的目标检查路由点
  `clang/lib/Sema/SemaExpr.cpp:7318-7322`——N13-N15 同一既有的单一
  路由点；实现为 `SemaMCS251` 新方法，登记于
  DiagnosticSemaKinds.td，N 系延续编号，建议 N16..N18，冲突则顺延，
  **文案以下列为准**）。
- 文案 A（冻结，error）：
  `MCS251 variadic call exceeds the fixed 6-slot variadic ABI cap (%0 variadic arguments given)`
- 检查对象：原型变参调用（`Args.size() - NumParams > 6`）与
  no-prototype 调用（`Args.size() - 1 > 6`，按 §4.3.6 表）。
- 实测预期形态：
  `d.c:5:3: error: MCS251 variadic call exceeds the fixed 6-slot variadic ABI cap (7 variadic arguments given)`

#### 4.4.3 三条路径的拒绝矩阵（冻结；聚合/i64/非常规指针 + 间接调用）

| # | 路径 | 层/时机 | 诊断（冻结文案） |
|---|---|---|---|
| C1 | **间接变参调用**（函数指针类型含 `...`，无论实参个数） | Sema，调用路由点（同 4.4.2 位置；`IsIndirect` 已在该点可得），B-S1 落地 | 文案 C：`MCS251 variadic call form 'indirect' is not supported (static slots require a named callee)` |
| C2 | 间接变参调用（若 C1 漏网——如经 `__builtin` 绕行的 IR） | llc 后端双保险：`LowerCall` 摘除既有 VA 门后新增 `IsVarArg && !IsDirect` → `report_fatal_error`，文案与 C1 一致 | `MCS251 variadic call form 'indirect' is not supported (static slots require a named callee)` |
| D1 | 变参实参 ∈ 拒绝集（直接调用逐实参；提升后类型判定） | Sema，同路由点 | 文案 D：`MCS251 variadic argument must be a promoted scalar (i8/i16/i32/f32) or ordinary data pointer` |
| D2 | `va_arg` 目标类型 ∈ 拒绝集 | Sema，`BuildVAArgExpr`（`clang/lib/Sema/SemaExpr.cpp:17461`）新插 MCS251 检查 | 文案 D（同上；语义覆盖"读"路径） |
| D3 | 拒绝集类型到达后端（兜底，现状已有） | llc `checkParameter`/`checkParameterType`（:3273-3306） | 既有 fatal 文案不变 |

非常规指针的判定沿 `hasOrdinaryPointerABI`（AS ∈ {0,1,2,3,4,8,9}，
:3255-3271）；聚合含 `byval/sret/byref` 到达形态（`checkParameter`
:3298-3301 兜底）；bit 走 N13 既有拒绝（不新增）。

#### 4.4.4 跨 TU 论证：caller 与 callee 各自独立满足 cap

- caller TU 只需要：变参原型（语法可见）+ N 常量 → 实参个数检查、
  槽写编号 `_PARM_(F'+1+k)`（F'=caller 原型固定形参个数）；
- callee TU 只需要：自身定义 + N 常量 → 槽补发 `_PARM_2.._PARM_(F+6)`
  （F=定义固定形参个数）、va_arg if-chain；
- 两侧编号一致**不需要**任何跨 TU 传递：槽是符号寻址（§4.3.1），链接期
  同名符号重定向即完成配对；`F'=F` 由 P-4 Tag 28 `param_count`（仅固定
  形参）跨对象比较保证（compareRecords，bit3+count 一致才可链）；
- N 是编译器常量 → 不存在"两侧 N 不同"的状态；将来 N 变更经 call_abi
  minor 门拦截旧对象（§4.4.1-4）。
- 结论：**无跨 TU 契约传递问题；契约的两半（写槽/补槽）由符号命名规则
  + P-4 param_count 比较各自独立闭合。**

### 4.5 llc/clang 改动面清单（R2 定稿，R3 更新）

| 点 | 改动 |
|---|---|
| `CanLowerReturn` :3699-3704 | 摘除普通分支 IsVarArg fatal；ISR 分支 :3692-3694 维持 |
| `LowerFormalArguments` :3330-3332 | 摘除；固定形参路径不变；`F` 常量记录到 MF 供 VA hook 用 |
| `LowerCall` :3473-3475 | 摘除；变参实参经既有串行写槽循环天然延续（§4.3.2，循环零改动）；新增 `IsVarArg && !IsDirect` 拒绝（C2）；新增变参实参计数 >6 拒绝（文案 F，§4.3.6） |
| 新增 | `LowerVASTART/LowerVAEND/LowerVACOPY`（§4.3.4-4.3.5；LowerVASTART 经 `parameterSlot` 物化基址）；`ISD::VAARG` 显式拒绝（文案 E）——**无 LowerVAARG**（va_arg 由 clang EmitVAArg 发射） |
| 新增 | `MCS251ISD::VARARG_HALT` 伪指令 + InstrInfo 编码（`sjmp self`；不借道 llvm.trap→_abort libcall，§4.3.6 R3 纠正） |
| AsmPrinter `emitParameterSlots` | 变参定义补发 6 个 4B 延续槽 `_PARM_(F+1).._PARM_(F+6)`；**门 :1282-1283 改为 `arg_size() < 2 && !isVarArg`**（printf 形态恰 1 个 IR 固定形参）；区选择（OSEG/DSEG）与 Global/STT_OBJECT 发射原样复用 |
| clang TargetInfo + ASTContext | `TargetInfo.h:338-388` 枚举尾增 `MCS251BuiltinVaList`；MCS251.h:218-221 改返回之；`ASTContext.cpp:10425-10448` switch 增 case（struct×[1] 形态，§4.3.3(a)）；同步 MCS251.h:172-176 注释更新 |
| clang CodeGen | **`MCS251ABIInfo::EmitVAArg` 覆盖**（Targets/MCS251.cpp:51-53；§4.3.3(c)/§4.3.5：guard 分支 + load + 推进，IR 形态探针 V 实证）；va_start/va_end/va_copy 发射路径零改动 |
| clang Sema | 新诊断 N16/C1、N17/文案 A、N18/D1+D2（§4.4）；登记 DiagnosticSemaKinds.td |
| P-4 / lld | **零改动**（1.4 已就绪；验证性正负例进 S3，R3 探针 P6 已实测 bit3 分支诊断） |

### 4.6 仍维持冻结的拒绝（不解冻清单）

- bit 在变参签名：N13 全套维持（声明侧 SemaType.cpp:5464-5474、调用侧
  SemaMCS251.cpp:1594-1601）——bit ABI 不定义 `...` 槽语义（P09 §4）；
- ISR 变参：维持 `CanLowerReturn` ISR 分支拒绝（T05 冻结：ISR 零参）；
- `va_arg(ap, __bit)`：随 N13 族拒绝（MCS251.h:172-176 注释记录的既有
  gap 不借本批放大）。

### 4.7 runtime printf/sprintf 迁移（阻断 3，R3 按现源码纠错定稿）

**R3 纠错（Alice 第三轮阻断 2）**：R2 误记 sprintf 现定义为
`sprintf(char *buf, const char *fmt, a0..a5)`。现源码实读（R3 探针 S，
只读核对于 /home/liu/LLVM_STC32/MCS251/validation/mcs251-runtime/src/
mcs251_printf.c）：**printf 与 sprintf 都是 `fmt + a0..a5` 固定 7 参、
均无 buf 参数**（printf :626-635、sprintf :638-652）；sprintf 的输出
目标是**全局缓冲区** `g_sprintf_buf[24]`（:40-42，SPRINTF_BUFSZ=24）+
`g_sprintf_pos`，经 `g_output_mode`（:77）与 `out_char`（:82-92）分流，
外加验收读取口 `sprintf_reset/len/getc/str`（:44-69，repo 内无本文件
之外的使用者，R3 全树 sweep 实证）。迁移必须**新建** buf 参数语义。

**改法（逐点，R3 定稿）**：

1. **定义点**：`validation/mcs251-runtime/src/mcs251_printf.c`
   - printf :626-635：`void printf(const char *fmt, uint32_t a0..a5)` →
     `void printf(const char *fmt, ...)`。固定形参 7→1，fmt 走 DPL
     首参通道，6 个变参延续槽 = `_printf_PARM_2..7`——**对象级布局
     与现状逐字节相同**（探针 R：6×4B 连续 @0x0..0x14）；
   - sprintf :638-652：`void sprintf(const char *fmt, uint32_t a0..a5)`
     → `void sprintf(char *buf, const char *fmt, ...)`。固定形参
     **7→2**：buf = 首源参数（DPL 通道，无槽）、fmt = 第二源参数 →
     `_sprintf_PARM_2`（4B 指针槽），6 个变参延续槽 =
     **`_sprintf_PARM_3..8`**（对象 7 槽 28B，较现状 24B **+4B**）；
   - **删除具名 a0..a5 与镜像回存**（:629-632、:641-642）：目标侧槽由
     caller 后端在调用前写入（LowerCall 既有机制）+ 本对象 B1 槽补发
     提供（§4.5），回存的"两端同源"机制（:454-458 注释）随迁移退役。
     **宿主侧注意**：回存原为宿主 oracle 编译同源文件时物化槽值而设
     （:456-458 注释自认），迁移后宿主桥 shim 改用 va_start/va_arg
     把变参物化进同名全局（S4 实装点，勿漏）；
   - **get_arg 与槽声明按函数分支改读（R2"原样保留"作废）**：
     `extern volatile uint32_t printf_PARM_2..7`（:464-465）不变；
     `sprintf_PARM_2..7`（:466-467）改为 **`sprintf_PARM_3..8`**；
     `get_arg` if-chain（:469-481）printf 分支不变，sprintf 分支
     k==0..5 → `sprintf_PARM_3..8`；引擎 `ai < 6u` 门（:563 起 9 处）
     不变；标识符无前导下划线的约定注释（:461-463）不变；
   - **buf 语义落实（buf 由调用方提供）**：sprintf 体内把 buf 存入
     新 `static char *g_out_buf`，`out_char` 的 mode-1 臂改写
     `g_out_buf[g_sprintf_pos++]`；截断+强制 NUL 策略维持（内部上限
     24B 语义与今日一致，如实记录为非标准行为）；`g_sprintf_buf[24]`
     删除（-24B）、`g_out_buf` 新增（+4B），本 TU DSEG 净 -20B；
     `sprintf_reset/len/getc/str` 随全局缓冲退役（或改带 buf 参数，
     repo 内无外部使用者，不改任何调用方）；
   - 文件头注释（:454-460）改记："变参定义（G2 B1），printf 槽 =
     `_PARM_2..7`、sprintf 槽 = `_PARM_3..8`，cap 6，buf 调用方提供"。
2. **头文件原型**：runtime **不新增** printf 公共头（`mcs251_libc.h:11`
   "不含 printf/sprintf/putchar"口径维持）；demo 侧就地声明
   （rewrite.py:654-656 的 `int printf(const char *fmt, ...);` /
   `int sprintf(char *buf, const char *fmt, ...);`）即为最终原型，
   **零改动**——R3 核验：demo 声明的 sprintf 形态本就含 buf，恰为
   迁移后的定义签名。既有的 void 定义 vs int 声明返回类型差异：P-4
   `ret` 仅记返回是否 bit（writer :228-232），不入记录、不拦链接、
   demo 从不使用返回值——维持现状，不在本批扩大范围（如实记录）。
3. **P-4 Tag 28 记录变化**：`_printf` 记录 `role: definition`、
   `bit3: 0 → 1`、`param_count: 7 → 1`；`_sprintf` 记录
   `role: definition`、`bit3: 0 → 1`、**`param_count: 7 → 2`**；
   线格式、writer、lld **零改动**。
4. **对既有固定参调用方的兼容/重编要求**：
   - demo 调用方源码**零改动**（声明本就变参；全语料 grep 实证不存在
     定参 printf/sprintf 原型调用方——固定 7 参签名仅存在于 runtime
     定义自身）；
   - 旧 runtime 对象（printf/sprintf bit3=0，count=7/7）与新调用方
     （bit3=1，count=1/2）在 lld 处 bit3+param_count 双重硬错
     （§4.8 P2/P3/P6 负例）→ **runtime 必须全量重编**，且任何持旧
     runtime 对象的链接必被拦下（fail-closed，无静默混链路径）；
   - demo 测试冻结 harness（`run-tests.py:24-27` bin-frozen 的
     llc/QEMU）随本批刷新为含 B1 的构建。
5. **Oracle 侧**：t1 三方 oracle（run-tests.py:6-22，Oracle-A=gcc 宿主）
   中宿主 printf/sprintf 本就是 C 标准变参（宿主 sprintf 签名本就
   `sprintf(buf, fmt, ...)`）——迁移使 DUT 与 Oracle-A 的两个函数
   **参数形态首次完全对齐**；S4 验收以 QEMU 串口字节比对为准（§6）。

**P-4 交互正负例（进 S3 验收矩阵，§6；P2/P6 诊断文案为 R3 探针实测
原文，探针 P6 = /tmp/g2var-probe3/p6a.o+p6b.o → mcs251-lld）**：

| # | 场景 | 预期 |
|---|---|---|
| P1（正） | 变参定义（迁移后 runtime：bit3=1,count=1）× 变参声明调用 TU（demo 形态：bit3=1,count=1；`int`/`void` 返回均 non-bit 不入比较） | lld 链接通过 |
| P2（负） | 变参定义 × 固定参声明（`void printf(const char*, uint32_t×6)`：bit3=0,count=7）——**count 先拦** | lld 硬错（R3 探针实测格式，以 count 1 vs 7 代入）：`mcs251 linker: error: <现行对象>: MCS251 signatures: records '_printf' have a parameter count conflict (<首见对象> vs <现行对象>): 1 vs 7`（compareRecords 先比 count，MCS251Signatures.cpp:240-243） |
| P3（负） | 旧 runtime 对象（bit3=0,count=7）× demo 变参调用对象（bit3=1,count=1）——"忘重编 runtime" | 同 P2 硬错（count 先报）；若 count 凑同则由 bit3 分支拦，其文案与格式即 P6 |
| P4（正） | 固定参一致性：`long mix(long,char,long)` 定义 × 同原型调用（count=3、bitmap 全 0 双侧一致） | 链接通过 |
| P5（负） | 固定参计数不一致：定义 3 固定参 × 声明 2 固定参（均非变参） | lld 硬错：`parameter count conflict: 3 vs 2` |
| P6（负，**R3 新增独立负例**：**固定参数数相同、仅 bit3 不同**） | `void f(int, ...)` 声明 TU（role=10：bit1 声明+bit3 变参，count=1）× `void f(int)` 声明 TU（role=2，count=1）——两侧 count 与 bitmap 全同，绕开 count 分支直击 bit3 分支 | lld 硬错（**探针 P6 实测原文，exit 1**）：`mcs251 linker: error: p6b.o: MCS251 signatures: records '_f' disagree on variadic-ness (bit3 differs) (p6a.o vs p6b.o)`（compareRecords bit3 分支 :254-260；证明 P2/P3 不是 bit3 语义的唯一防线） |

（P6 可行性注：声明 TU 不含变参定义/调用，今日 llc 即可出 v2 对象——
探针 p6a/p6b 的 IR 记录实测 `!{!"_f", i32 10, i32 0, i32 0}` /
`!{!"_f", i32 2, i32 0, i32 0}`，param_count 由 bitmap 字段数导出，
两侧同为 1。）

### 4.8 影响面汇总（B1，R3）

| 组件 | 影响 |
|---|---|
| clang Sema | 新诊断 3 条（文案 A/C/D，§4.4）；N13/N14/N15 不动 |
| clang CodeGen | 仅 va_list 类型声明覆盖（§4.3.3）；va_* 发射路径零改动（探针 B） |
| llc | §4.5 表 7 点 |
| P-4 Tag 28 | 零改动（runtime 记录值变化：printf 7→1、sprintf 7→2、bit3 置位，属数据非格式） |
| lld | 零改动（S3 正负例验证；P6 bit3 分支诊断已探针实测） |
| runtime | §4.7 迁移（printf/sprintf 定义签名、删镜像回存、sprintf get_arg/槽声明改读 `_PARM_3..8`、buf 经 `g_out_buf` 进引擎、全局 `g_sprintf_buf` 退役、宿主桥 shim 改 va_arg） |
| demo 包 | 源码零改动；六 demo T1 解除后记账更新 |
| DSEG/OSEG 预算 | printf 变参定义槽区 +0B（与现状同 24B）；sprintf +4B（28B=7 槽）；`g_sprintf_buf` 退役使 runtime TU 净 -20B（§4.7）；G8 的 10/11 window 缺口核算列入 S4 前置检查 |

---

## 5. PM 裁定记录（R1 决策点已全部闭合，无开放决策）

| R1 # | 决策点 | 裁定（2026-09-14，PM） |
|---|---|---|
| 1 | 路线 | **B1**（解冻一般变参，静态槽延续式）；A 不再考虑 |
| 2 | （若含 A）推翻改写口径 | 不适用（A 否决） |
| 3 | cap N 与越界语义 | **N=6 编译器固定常量**、无选项；调用侧 >6 = Sema error（§4.4）；va_arg 越界 = 确定性停机（§4.3.6） |
| 4 | float 变参 | f32 4B 槽允许（目标 double==f32，标准提升恒等，§4.2；语料零浮点变参实参） |
| 5 | 间接变参调用 | Sema error（C1）+ 后端 fatal 双保险（C2），B-S1 落地 |
| 6 | runtime printf/sprintf 迁移 | **必须迁移为变参定义**（§4.7 定稿） |
| 7 | N13 维持冻结 | 维持（bit 无 `...` 槽语义） |
| 8 | 切片授权 | §6 切片与门槛定稿，待 PM 按片授权实施 |

---

## 6. 切片与完成门槛（R2 定稿，R3 更新；依赖单向，逐片独立可回退）

**依赖图**：`S1（clang 诊断） → S2（llc B1 核心） → S3（跨 TU + P-4） → S4（runtime + 端到端）`
（严格线性：S1 不触碰 llc；S2 依赖 S1 的 fail-closed 前提——无诊断授权
不放大能；S3 依赖 S2 的单 TU 能力；S4 依赖 S3 的链接契约验证。）

| 切片 | 内容（定死） | 独立验收门槛（全过才收片） |
|---|---|---|
| **B-S1 诊断先行**（仅 clang） | 新增 3 诊断：文案 A（>cap 直接/no-prototype 调用）、C1（间接变参）、D1+D2（聚合/i64/非常规指针：实参与 va_arg 目标）；lit 负例矩阵落地 | **负例矩阵**（每例 S1 前 llc 前红/S1 后绿）：①直接调用 7 变参实参→A；②no-prototype 8 实参→A；③函数指针变参调用 0/1/多实参各一→C1；④聚合变参实参→D1；⑤`long long` 变参实参→D1；⑥AS5/AS7 指针变参实参→D1；⑦`va_arg(ap, struct)`→D2；⑧`va_arg(ap, long long)`→D2；⑨`va_arg(ap, AS7 ptr)`→D2；⑩N13 回归锚（bit 变参仍拒）。门槛：新 lit 全过；Sema 23/23 + CodeGen 27/27 不回退；llvm 侧零改动（149/149 天然不回退） |
| **B-S2 后端能力**（llc + clang TargetInfo/CodeGen） | 摘 3 处 fatal（ISR 分支除外）；VA 三 hook（VASTART/VAEND/VACOPY，§4.3.4-5）+ ISD::VAARG 显式拒绝（文案 E）+ LowerCall 变参计数门（文案 F）；VARARG_HALT 伪指令；AsmPrinter 槽补发（门改 :1282-1283）；`MCS251BuiltinVaList` kind（TargetInfo.h 枚举 + ASTContext case）+ `MCS251ABIInfo::EmitVAArg` 覆盖 | **单 TU 全类型 golden + va_arg 语义**：①lit golden（`-mcs251-object-format=elf` + CHECK）：变参定义+调用，实参谱 {i8,i16,i32,f32,ptr}×{0,1,6 个}，va_arg 目标 {i32,i8 截断,i16 截断,f32,ptr}，断言槽补发符号/大小/区（DSEG 非叶、OSEG 叶）与写槽/读槽序列；②**va_list 传普通辅助函数、辅助函数内 va_arg 的 golden**（探针 V 形态落 lit：身份经 `__base` 值传递、helper 不引用自身 `_PARM_` 符号）；③QEMU 字节哨兵：单 TU `sum`/格式化程序串口输出 golden（含 char→int 提升、指针实参解引用）+ 邻槽不踩/首参 DPL 不变哨兵（P09 §4.3 方法论）；④**va_arg 越界负例**：循环 va_arg 至第 7 次 → QEMU 停机哨兵（PC 死循环 + 超时）；⑤ISR×变参仍拒（负例保持）；⑥间接变参后端双保险 C2 触发（IR 级 lit）+ 手写 IR 文案 E/F 触发 lit；⑦llvm CodeGen/MCS251 149/149 不回退 |
| **B-S3 跨 TU 与 P-4**（依赖 S2） | 双 TU golden；P-4 正负例 §4.7 矩阵；>cap 双 TU 场景 | ①定义 TU+调用 TU 双 TU 出对象、lld 链接通过、QEMU 输出与单 TU golden 一致；②P-4 矩阵 P1 过、P2/P3/P5 各得冻结硬错文案、P4 过（§4.7 表）；③>cap：调用 TU 编译期报文案 A（不产出对象即止）；④lld 既有 v2-object-identity + crt-v2 全过；⑤旧 runtime 对象 × 新调用方 = P3 硬错（重编要求可执行性证明） |
| **B-S4 runtime/端到端**（依赖 S3） | §4.7 迁移六点全落地（printf/sprintf 定义签名、sprintf 槽改读 `_PARM_3..8`、buf 经 `g_out_buf` 进引擎、删镜像回存+宿主桥 va_arg shim、注释、重编+harness 刷新）；六 demo 解除 | ①runtime 重编后 lld 无 P-4 冲突；②**六 demo T0+T1 pass**（源码零改动）；③**端到端 QEMU 字节输出**：六 demo 串口 transcript 与预期逐字节相等（覆盖 0/1/6 实参、%d/%u/%x/%s/f32、`%%` 字面）；④sprintf 调用方 buf 内容字节级验证（含截断+NUL 策略与现状一致）；⑤三个新增格式引擎单测 kernel 过三方 oracle（Oracle-A gcc / DUT，run-tests.py 管线）；⑥既有 49 个 pass demo 不回退（全量回归）；⑦G8 预算核算过（sprintf +4B、runtime TU 净 -20B，§4.8）；⑧G1-4 记账/README §8/TIERS 更新（G2 行解除，15 demo 归零） |

横切纪律（沿 R1 不变）：每片独立可回退；fail-closed（无诊断授权不得
静默放行新形态）；Tag 28/身份门禁行为不变；v1 资产零接触；探针中间
产物不入库。

---

## 7. 基线数字（2026-09-14 实测，minimal-isr @ 2e08e94ae 工具链）

| 套件 | 命令范围 | 结果 |
|---|---|---|
| clang test/Sema mcs251-* | llvm-lit（build-mcs251-s1） | **23/23 PASS** |
| clang test/CodeGen mcs251-* | 同上 | **27/27 PASS** |
| llvm test/CodeGen/MCS251 | llvm-lit（build-mcs251） | **149/149 PASS**（与 P-4 实施记录一致） |

（探针中间产物：R1 在 /tmp/g2var-probe/，R2 在 /tmp/g2var-probe2/，
R3 在 /tmp/g2var-probe3/，均不入库。）

---

## 8. 证据索引（file:line，均为本文实测核对过的锚点）

**现状与拒绝点**
- llc 拒绝 4 点：`llvm/lib/Target/MCS251/MCS251ISelLowering.cpp:3330,3473,3692,3699-3704`
- N13：`clang/lib/Sema/SemaType.cpp:5464-5474`；`clang/lib/Sema/SemaMCS251.cpp:1567-1607`（CheckMCS251BitCallForm，N13 :1594-1601 / N15 :1602-1606）；
  `clang/include/clang/Basic/DiagnosticSemaKinds.td:12780-12787`
- Sema 调用路由点（C1/A/D1 插入点）：`clang/lib/Sema/SemaExpr.cpp:7305-7322`（:7321 既有 BitCallForm 调用）
- va_arg Sema 插入点（D2）：`clang/lib/Sema/SemaExpr.cpp:17454,17461`（ActOnVAArg/BuildVAArgExpr）
- va_list 无覆盖自认：`clang/lib/Basic/Targets/MCS251.h:172-176`；目标默认 xsmall/v2（契约 :52-53）与 `UserLabelPrefix="_"`：`clang/lib/Basic/Targets/MCS251.h:48`
- 目标 double==f32：`clang/lib/Basic/Targets/MCS251.h:32-35`

**静态槽机制（B1 承重结构）**
- 槽符号：`MCS251ISelLowering.cpp:3245-3251`（parameterSlot）；
  caller 写槽：`:3544,3560-3570`；callee 读槽：`:3403-3413`；
  槽发射/槽宽/区选择/门：`MCS251AsmPrinter.cpp:1280-1336`（门 :1282-1283、槽宽 :1321、区 :1289-1294）；
  槽符号外部声明：`MCS251AsmPrinter.cpp:1338-1348`
- 类型边界：`MCS251ISelLowering.cpp:3255-3271`（hasOrdinaryPointerABI）、`:3273-3306`（checkParameterType/checkParameter 兜底）

**R2 探针（/tmp/g2var-probe2/，2026-09-14 实测）**
- 探针 A（槽分布/无对齐垫）：pA-def.c → pA-def.o readelf 实测
  `_mix_PARM_2..8 = (0x0,1)(0x1,4)(0x5,2)(0x7,4)(0xb,4)(0xf,1)(0x10,4)`（§1.6）
- 探针 R（runtime printf 现状 6×4B）：pR.o readelf 实测
  `_printf_PARM_2..7`/`_sprintf_PARM_2..7` 各 4B @0x0..0x14（§1.6）
- 探针 B（变参 IR 形态/提升/i64/byval）：pB.ll 实测（§1.6）

**P-4 与 lld（零改动证据）**
- writer 变参位/RetBit：`clang/lib/CodeGen/Targets/MCS251.cpp:224-235`
- 比较语义（bit3/count 冻结文案）：`llvm/lib/BinaryFormat/MCS251Signatures.cpp:208-266`
- 跨对象比较与覆盖规则：`lld/MCS251/LinkerCore.cpp:1902-1955`（validateSignatureSet）
- 冻结条文：`proposals/P4-SIGNATURE-PROTOCOL-FREEZE.md:97-98,146-151`
- 值 ABI 冻结：`proposals/P09-BIT-CODEGEN-DESIGN.md` §4；指针槽/间接多参背景：`proposals/A4-STATIC-PTR-PARAMS-DESIGN.md` §1.1

**runtime 迁移（B-S4 对象；R3 探针 S 只读复核）**
- 定义点/镜像回存：`validation/mcs251-runtime/src/mcs251_printf.c:626-635,638-652`（回存 :629-632,:641-642；**两定义均为 fmt+6 定参、无 buf**）
- sprintf 全局缓冲区与读取口：同文件 `:29-69`（SPRINTF_BUFSZ=24 :40、g_sprintf_buf :41、sprintf_reset/len/getc/str :44-69）、`g_output_mode` :77、`out_char` :82-92
- get_arg/槽声明/引擎 6 槽门：同文件 `:454-460,461-467,469-481,563起(9处)`
- runtime 头口径：`validation/mcs251-runtime/src/mcs251_libc.h:11`
- demo 变参原型：`mcs251-demos-rewritten/tools/rewrite.py:640-671`；`mcs251-demos-rewritten/README.md:238-239,332,342`
- demo QEMU 三方 oracle/冻结 harness：`validation/mcs251-demo-test/run-tests.py:1-27`

**R3 va_list 定义与发射机制（阻断 1 逐处实读锚点）**
- `BuiltinVaListKind` 枚举/纯虚入口：`clang/include/clang/Basic/TargetInfo.h:339-388,:1052`
- MCS251 现状返回 CharPtr：`clang/lib/Basic/Targets/MCS251.h:218-221`
- 类型构造 switch/缓存：`clang/lib/AST/ASTContext.cpp:10425-10448,:10451-10458`（SystemZ struct×[1] 先例 :10281、Xtensa :10389）
- stdarg 零改动链：`clang/lib/Headers/__stdarg___gnuc_va_list.h:12`、`__stdarg_va_list.h:12`、`__stdarg_va_arg.h:14-20`、`__stdarg_va_copy.h:11`
- va_arg 发射链：`clang/lib/CodeGen/CGExprScalar.cpp:6219` → `CGCall.cpp:6956-6970`（:6969 ABIInfo 入口）→ `ABIInfoImpl.cpp:77-82`（DefaultABIInfo::EmitVAArg）→ `:404-447`（EmitVAArgInstr = llvm.va_arg）
- MCS251 覆盖点：`clang/lib/CodeGen/Targets/MCS251.cpp:51-53`（MCS251ABIInfo）、`:287-289`（工厂）
- va_start/va_copy 发射点：`clang/lib/CodeGen/CGBuiltin.cpp:898,:3360,:3365-3368`；`EmitVAListRef`：`CodeGenFunction.cpp:2711`
- 基址物化机制（槽符号作值）：`llvm/lib/Target/MCS251/MCS251ISelLowering.cpp:3245-3251`（parameterSlot=getExternalSymbol）、`:3403-3413`（callee 读槽）、`:3544-3570`（caller 写槽）

**R3 探针（/tmp/g2var-probe3/，2026-09-15 实测）**
- 探针 V（va_list {base,off} 消费 IR 形态可发射性）：v2e.ll → v2e.o llc 全过（@consume 146B / @consume_phi 246B 含 PHI / @consume_char、@consume_float / @own_setup 符号地址作值 52B；§4.3.3(c)）
- 探针 T（现 va_list 形态）：t-valist.c 实测 `sizeof(va_list)==4`（CharPtrBuiltinVaList，§1.1）
- 探针 V3（llvm.trap 形态）：v3-trap.ll 实测 llvm.trap → `_abort` libcall 展开（非 Cannot select）；runtime 无 abort 提供者（mcs251-runtime/src 零命中）→ VARARG_HALT 维持（§4.3.6）
- 探针 P6（P-4 独立负例）：p6a.o（role=10,count=1）× p6b.o（role=2,count=1）→ mcs251-lld exit 1，`records '_f' disagree on variadic-ness (bit3 differs) (p6a.o vs p6b.o)`；对照 P2 格式实测 `parameter count conflict (p2b.o vs p2a.o): 1 vs 2`（§4.7 矩阵）

**需求画像与记账**
- G1-4 记账：`/home/liu/LLVM_STC32/G1-4-PROGRESS.md:198-215`；`/home/liu/LLVM_STC32/G1-4-PRESCAN.md:235`
- 原始语料含 printf（GBK 实证）：`STC32G144K246-DEMO-CODE/13-8个串口同时使用收发测试程序/main.c`（GBK 第 69 行）
