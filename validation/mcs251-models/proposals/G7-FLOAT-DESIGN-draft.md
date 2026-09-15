# G7：f32 缺口（demo 36 / 38）调查设计（草案 rev-1）

**日期**：2026-09-15（rev-1，落实 Alice 设计评审 7 组 CHANGES REQUESTED）。
**状态**：设计提案（待 Alice 复审 → PM 裁定后实施；本文不授权实施）。
**工作基线**：minimal-isr @ 076cb61ed；G1/AS4/br_jt/G2(B-S1..S3) 已提交在案。
**证据目录**：/home/liu/LLVM_STC32/GAP-G7-PROBES/（49 探针 + rev-1 新增 4 件：
k2_demo36_u16（.c/.raw.ll/.t0.err）、demo36_equiv_oracle（.c/.result.txt）、
rev1_ir_intrinsic_neg（.ll/.t1.err）、rev1_ir_unknown_target_intrinsic
（.ll/.t1.err））与 GAP-G7-PROGRESS.md rev-1 节。

**rev-1 要点**（对应 Alice 意见 1-7）：
① demo 36 证据改真 u16（unsigned short）探针重证；② TFPU 机制按手册
35-TFPU-*.md 重写——DMA 外设协处理器 + `MOV DMAIR,#N` 触发，0x1C..0x30 是
DMA 命令码而非 opcode；③ demo 36 等价性补穷举证明；④ S3 补完整序列规范；
⑤ P-4 论证按 clang Targets/MCS251.cpp 实读修正；⑥ S2 范围按 AsmPrinter
实读闭合；⑦ 裁定表整理（D5 转正确性约束、S2∥S3、测试补强）。

---

## 0. 结论与裁定摘要

G1-4 记账的 "f32 2" 指 demo 36 与 demo 38，两者根因不同，**不应作为一个编译器战役合并处理**：

- **demo 36**：`u16 *= 0.625` 需要 窄类型+无符号 f32 转换（连接集刻意不含；
  Keil 方言下直接产生 `uitofp i16→f32` / `fptoui f32→i16`，见 §1.2）。
  推荐裁定 D1=**改写规避**：语料语义本就是 ×10 缩放整数（注释自证），改写为
  纯整数运算（32 位宽中间值），零编译器工作；等价性已穷举证明（§2.1）。
- **demo 38**：三个独立子缺口叠加——(i) 全局 `float cfl1=3.9` 初始化器被 llc
  数据门拒；(ii) sin/cos/tan/atan/sqrt 无 math.h 时 C99 拒隐式声明（T0 首因）；
  (iii) 该 demo 本意是测 **TFPU 硬件单元**。TFPU **不是 CPU 指令**：手册
  35-TFPU-*.md:13"TFPU 由专用直接内存访问 DMA 控制。所有算术运算都是通过
  将运算指令写入称为 DMAIR 控制寄存器来启动的"、:21 红字注"向 DMAIR 寄存器
  写入指令码，只能使用立即数寻址方式的指令 MOV DMAIR,#N"。软浮点 math 库
  会偏离 demo 意图。推荐裁定 D2=**TFPU builtin→IR intrinsic→DMA 触发序列**
  路线（§2.3），D3=TFPU 双目算术同批连接（TPIN 时序窗口语义要求）。

## 1. 现状与证据

### 1.1 连接集（已在案，不改）

`llvm/lib/Target/MCS251/MCS251ContractCheck.cpp` 的 f32 白名单：四则
（:294-301）、FNeg（:336）、**比较经七个 helper 谓词组合支持全部 FCMP 谓词**
（hasMCS251ConnectedF32Compare :209-248：OEQ/UNE/OGT/OGE/OLT/OLE 六个
FCMP3_PRED 谓词 helper + UO，TRUE/FALSE 及 UEQ/ONE、ULE/UGT 等对偶谓词由
helper 组合或取反覆盖）、有符号 i32↔f32（:411-425）。刻意缺席：无符号转换、
窄类型转换、f64、math intrinsic（RC-5 :345-395）。runtime 侧
`mcs251_float.h` 明令"不得添加 …math helper"。
**常量/死代码豁免（RC-6 :271-280）**：全常量运算（SelectionDAG 常量折叠将
消去）与只写向死局部的结果（isEffectivelyDead）不触发拒绝——下述负例探针
均为非常量活跃值，不受豁免影响；正例中常量浮点表达式即使写了也不被拒，
复核探针时须区分。
探针印证：a/b/c/e/f/g/h 系列过；i/j（无符号）、u/v/k2（窄类型，见 §1.2）
与 math intrinsic 全拒，冻结文案逐字命中。

### 1.2 demo 36 证据（rev-1：真 u16 重证）

`src/36-一线制温度传感器 DS18B20 测温/main.c:169,174`：
`Temperature *= 0.625;`。原语料 `typedef unsigned int u16` 在 Keil C251 是
16 位，但在本目标 int=i32——原探针 k_demo36_shape 实际只证明了无符号
（UIToFP）缺口，**不能充当窄转换证据（Alice 意见 1）**。改写语料用的
`mcs251_type_compat.h:58` 是 `typedef unsigned short u16`。rev-1 新探针
**k2_demo36_u16**（unsigned short，含 :169 负分支与 :174 正分支原形态）命中
同一冻结文案：
`MCS251 contract violation: f32 conversion is not in the connected libcall subset`
（clang 契约层，-O0 即拒）。其原始 IR（k2_demo36_u16.raw.ll:28-30）显示缺口节点正是
——负分支的复合赋值直接产生 i16↔f32 转换，正分支则先把操作数提升到 i32 再
走 i32↔f32（该分支已被有符号转换放行）——窄类型与无符号一次占全的是负分支：
`uitofp i16 → float` / `fptoui float → i16`——窄类型与无符号一次占全。
旁证：u_narrow_sign（short↔f32，窄+有符号）、v_ushort2f（unsigned short→f32）、
sim36/sim36b（demo36 规范化形态）同样命中。注释原文"0.0625 * 10，保留1位小数
点"：**源语义即温度×10 的整数**，浮点只是书写便利。

### 1.3 demo 38 证据

`src/38-TFPU…/sample.c`：
- :74-76 `float cfl1=3.9; float cfl2=5.1; float cfl3;` → llc 数据门
  `defined global data requires … integer initializer`（探针 n/o/p）。
- :114-117 四则链：软浮点已连接（探针 a/b/c 过），非缺口。
- :118-122 `sin/cos/tan/sqrt/atan(cfl3)`：无 `<math.h>`，C99 拒隐式声明
  （探针 l_demo38_shape.t0 首因）；即便引入 math.h，llvm.sin.f32 等 intrinsic
  被 RC-5 拒（可执行负例见 §2.3 P-4 段）。原厂路径是闭源 AI8051U_32_TFPU.LIB
  （sample.c 顶部注释 + 手册 35.8），库内即 DMA 触发序列——不走 Keil 库，
  须按手册模型自生成（情报 §8）。
- :124 `printf("Result=%f\r\n",cfl3)`：B-S1 提升集含 f32、runtime out_float
  在案——非缺口。
- :114/123 `TPIN=0/1`（sbit 翻转）为时序测量窗口：**窗口内运算走软浮点还是
  TFPU 硬件会改变被测量本身**，且优化不得把运算移出窗口（§2.3 序列规范）。

### 1.4 TFPU 硬件单元（手册 35 章 + 归档情报；rev-1 全节重写）

**机制（不是 CPU 指令）**：TFPU 是 DMA 控制的外设式协处理器（35-TFPU-*.md:13；
情报 §9 六条证据链）。操作数/结果在 **PSW(0xD0) 位选中的当前组 R0-R7**（:59，
即数据空间 0x00-0x1F 的 8 字节窗口）。运算经向 SFR **DMAIR（0xED）** 写
**DMA 命令码**启动；:21 红字注（硬性代码生成约束）：**只能用立即数寻址的
`MOV DMAIR,#N`**（编码 75 ED N），其它寻址方式（如先算好 N 再 MOV DMAIR,reg）
无法正常触发。固定映射：**AR（第一操作数）=R4-R7，BR（第二操作数）=R0-R3，
结果回写 R4-R7**；MSB 在 R4/R0、LSB 在 R7/R3（:63-65 等各节图示）；单目
（sqrt/三角/check/转换）只用 AR；comp/check/读状态结果在 R7。

命令码与时钟（35.3 表 :25-55，rev-1 按 35.3 逐项修正）：

| 类 | 命令码 | clk | 说明 |
|---|---|---|---|
| 加/减 | 0x1C/0x1D | 31~40 | BR@R0-R3, AR@R4-R7 → R4-R7（:63-77） |
| 乘/除 | 0x1E/0x1F | 26~34 / 58~67 | 同上（:79-95） |
| sqrt | 0x20 | 50-54 | AR@R4-R7 → R4-R7（:97-104） |
| 比较 comp | 0x21 | **18** | 结果 R7 低 4 位（:106-120） |
| 检测 check | 0x22 | 15 | R7[3:0] 分类码（:122-142） |
| sin/cos | 0x2D/0x2E | 32~270 | AR@R4-R7 → R4-R7（:150-166） |
| tan/arctan | 0x2F/0x30 | 58~258 / 62~175 | 同上（:168-184） |
| f→char/short/long | 0x23/0x24/0x25 | 19~30 / 19~30 / 23~39 | D6 范围外 |
| char/short/long→f | 0x27/0x28/0x29 | 23~33 / 23~33 / 24~33 | D6 范围外 |
| 初始化协处理器 | 0x31 | 2 | **完成后产生异常状态，需软件清除**（:244-249） |
| 清除异常 | 0x32 | 4 | :251-256 |
| 读/写状态寄存器 | 0x33/0x34 | 4 | 经 R7 读写；**位定义手册未给出**（:258-270） |
| 读/写控制寄存器 | 0x35/0x36 | 4 | 经 R7（:272-283） |
| 选系统时钟 / 选 PLL 时钟 | 0x3E / 0x3F | - | :286-296；选定后再写运算命令不改变时钟源（:53 红字注） |

**完成判定（Alice 阻断 2 要求补齐）**：手册**未提供任何完成标志或中断**——
35 章全章无 busy 位/完成位定义；中断源全清单（/home/liu/LLVM_STC32/manuals-md/G144K246/15-中断系统及外部中断.md:13-137，
表中 DMA 源远不止旧系列情报所录的 16 个 P2P 通道）中**无 TFPU 项**——完成位/中断
属未文档化能力，不据此证明硬件不存在；
无 TFPU 中断使能/标志寄存器（情报 §2）。唯一的运行时状态通道是命令 0x33
（读状态→R7），但其位定义手册未给出（情报"需实测厘清清单"）。因此设计模型
取**同步阻塞 + 固定最坏延时**：填 R0-R7 → `MOV DMAIR,#N` → 按该命令 35.3 最坏
时钟数等待（选 0x3F PLL 异步时钟域时按频率比留裕量）→ 读 R4-R7；命令无流水/
队列，须顺序发起（情报 §1"完成判定"段、§5、§9；情报已标注该模型为分析、
非手册原文——S4 硬件实测若发现可轮询位再修订）。

QEMU stc32g144k246 模型对 DMAIR/SFR 0xED 行为的支持未证（G1-0 未决项的
延伸）——列为验收边界而非本片前置。

## 2. 方案空间

### 2.1 demo 36

| 方案 | 内容 | 代价/风险 |
|---|---|---|
| (a) 改写规避（推荐） | `Temperature = (unsigned)raw * 10 / 16`（0.625=10/16），**32 位宽中间值**；或 `((unsigned)raw*5)>>3` | 零编译器工作；与源注释语义一致；等价性证明见下 |
| (b) 连接窄/无符号转换 | runtime 增 __floatunsisf 等 4 helper + 白名单 | 违反 mcs251_float.h 冻结令；为一个可改写站点扩 ABI 面 |
| (c) 保留 gap | 记 f32 conversion 未连接 | demo 36 永不绿 |

**(a) 的等价性证明（rev-1，Alice 意见 3）**——宿主穷举核验脚本
GAP-G7-PROBES/demo36_equiv_oracle.c（结果存 .result.txt）：

1. **不溢出前置**：u16 全域 x*0.625f 最大 65535*0.625f=40959.375，fptoui
   结果 40959 < 65536，原式的 f32→u16 转换永不越界（脚本动态断言）。
2. **穷举相等**：对全部 65536 个输入，原浮点式（u16 路径 `uitofp i16→f32`
   ×0.625f `fptoui`；f64 常量路径；sim36b 的提升 i32 规范化路径）与宽化整数
   式 `(u32)raw*10/16` **逐点相等（mismatch=0）**。理由：i16→f32 精确
   （尾数 24 位），x*5/8 ≤ 327675/8 精确可表示，故浮点积无舍入；fptoui 与
   整数除法同为截断。
3. **截断语义口径**：整数 `/16` 截掉小数（如 9→5.625→5），**并非"无精度
   损失"**（rev-0 措辞错误）——但与原浮点→u16 转换的向零截断完全一致，
   即改写保持 demo 原有精度行为，不引入也不消除误差。
4. **必须宽化（窄乘回绕实证）**：16 位窄乘 `raw*10` 从 **raw=6554** 起
   回绕（6554*10=65540>65535，窄算得 0 而正确值 4096；32768→0/应 20480；
   65535→4095/应 40959）——脚本记窄变体首个分歧行。改写规范：
   **先宽化（u32/i32）再乘除**，两分支同理。
5. **分支覆盖**：:169 负分支（`~T+1` 补码后同式）与 :174 正分支由同一
   raw 全域穷举覆盖（k2 探针含两分支原形态；:169 的值先经 u16 补码再进
   同一乘式）；改写包测试矩阵须两分支各配整数 oracle 对照。

### 2.2 demo 38 子缺口 (i)：全局 f32 初始化器（rev-1：范围闭合）

**现状（AsmPrinter.cpp 实读，Alice 意见 6）**：类型门 `isSupportedMutableType`
（:683-705，i8/i16/i32/4B 指针/递归数组/递归 struct）是**共享递归门**，被
四处使用：AS3/AS4 模块 v1 分类（:622）、AS0 分类回退（:625）、AS0 可变初始
化器门（经 isSupportedMutableInitializer :707-732，于 :2152）、AS3 XDATA
初始化器门（:2252）。发射走 emitMutableInitializer（:741-781，标量
ConstantInt :747-750）；XINIT v1 记录 u16 地址/大小/负载（:2166-2180，大端
字段；零初始化 → HasPayload=0 的"clear only"6 字节记录 :2177-2179）。AS0
float 全局**今天不触发 v1/v2 分类翻转**（非指针标量过 :624 检查），n/o/p
探针死因即 :2152 数据门文案。

**S2 范围裁定（只放行最小面）**：**仅 AS0 可变 f32 标量**。
- 实现不得改 `isSupportedMutableType` 本体（否则 :622/:625/:2252 连带放行
  AS3/AS4 float 与分类面）；在 :2152 调用点引入 AS0 专用判定（如参数化的
  initializer 谓词）接受：既有整数/指针/聚合形态 + **恰 `float` 标量**
  （ConstantFP / ConstantAggregateZero）。
- emitMutableInitializer 增 ConstantFP 分支：IEEE-754 位模式按 u32 大端
  4B 发射（emitIntValue，与既有 i32 初始化器/XINIT v1 大端约定一致）。
- **不扩**：const f32（CSEG RO 路径）、float 数组/struct（递归门内任何
  float 成员）、AS3（__xdata）、AS4（__code）——维持既有冻结诊断文案不变。
- **NaN/Inf/−0 按位模式原样 4B、不做规范化**（见 §6：已从 PM 偏好转为
  正确性约束，非决策项）。
- 零图：`float cfl3;` → 4B DSEG + XINIT clear-only 记录（与 :2166 注释的
  稀疏形态一致），同批。

### 2.3 demo 38 子缺口 (ii)+(iii)：math 函数与 TFPU（rev-1：触发序列路线）

| 方案 | 内容 | 评 |
|---|---|---|
| A. TFPU intrinsic（推荐） | `__builtin_mcs251_tfpu_{sin,cos,tan,atan,sqrt}`（单目）+ `{add,sub,mul,div}`（双目，D3）：builtin → IR intrinsic（llvm.mcs251.tfpu.*，f32 参数/结果）→ ISel 软化后降为**完整 DMA 触发序列**（§2.4）；四则若同批（D3）则 demo 38 的全部浮点表达式可逐个改写为 builtin | 贴合 demo 意图（TPIN 窗口测量的是硬件时序）；序列各步均为既有指令形态（见 §2.4）——无新 ISA 编码，但 `MOV DMAIR,#cmd` 的直接地址立即写（75 ED <cmd>）须补 MC 发射支持（MCS251MCCodeEmitter.cpp:346-349 现仅实现寄存器源写，新增 .td 汇编串不会自动发射该编码），并补 ELF/REL 双路测试；ContractCheck 按 intrinsic ID 白名单（P09 A.1-A.6 先例）；类型验证见下 P-4 段 |
| B. 软浮点 math 库 | runtime 增 _sin/_cos/…（软件实现） | 违反 float.h 冻结令"不得添加 math helper"；TPIN 窗口测的是软件时序，偏离 demo 意图；与既有"仅连接子集"原则冲突 |
| C. demo 内联 asm | asm volatile 内手写装载 + `.byte 0x75,0xED,0x2D` + 延时 + 读回 | 依赖 asm 对物理寄存器约束的支持面（未证）；不可审（绕过全部检查层）；仅作逃生门不推荐 |

**P-4 签名论证修正（rev-1，Alice 意见 5；clang Targets/MCS251.cpp:249-441
实读）**：P-4 记录的域是**对象外链符号**——emitTargetMetadata 的两个符号
环路都显式跳过 intrinsic（EmittedDefinitions :293-297 `F.isIntrinsic()…continue`；
兜底记录环 :423-424 同）。builtin 展开为 IR intrinsic 后**无链接符号、无
签名记录**，"P-4 签名记录（builtin 函数签名）"的 rev-0 说法作废。S3 的类型
安全来自三层：① clang Sema 的 builtin 原型表（BuiltinsMCS251.td）；
② IR intrinsic 声明经 `CGM.getIntrinsic(ID)`（P09 规则：禁止
getOrInsertFunction 按字符串伪造）；③ llc ContractCheck 按 intrinsic ID 的
白名单 + 精确签名核验。S3 须加一条负例：**builtin 名不得泄入
`!mcs251.signatures`**（AST 环若拾到隐式 builtin FunctionDecl 则按
BuiltinID 过滤；P-4 记录只属于真实链接符号）。
**"无 IR intrinsic 直入"的可执行负例（rev-1 已跑通）**：手写 IR
`call float @llvm.sin.f32(float)` 直接喂 llc → `MCS251ContractCheckPass`
以冻结文案拒（rev1_ir_intrinsic_neg.t1.err）：
`LLVM ERROR: MCS251 contract violation: f32/f64 intrinsic operation is not
yet implemented; soft-float runtime is not connected`（RC-5 :345-395）。
连**尚未注册**的名字 `llvm.mcs251.tfpu.sin` 也被同一门拒
（rev1_ir_unknown_target_intrinsic.t1.err）——f32 实参即触发 RC-5 分类。
S3 落地后：白名单按 ID 匹配（P09 A.2 先例），错签名被 IR 校验器/A.2 拒，
负例两条（未注册名 + 已注册名错签名）都入测试。

### 2.4 S3 序列规范（rev-1 新节，Alice 意见 4）

**寄存器映射**（手册 :63-65 图 + RegisterInfo.td:6-19 字节序裁定）：
- AR（第一操作数）= **DR4**（R4=MSB…R7=LSB）；BR（第二操作数）= **DR0**
  （R0=MSB…R3=LSB）。`wr{k}=[r{k+1}低,r{k}高]` 的既有裁定使 32 位 MOV 进
  DR0/DR4 的字节布局与 TFPU 图示**逐字节一致，无需换序**。
- 结果 R4-R7 ← 从 **DR4** 读回；comp/check 结果在 R7。
- R0-R7 是 PSW[4:3] 选组的**当前组**窗口（:59）；R8-R15 不在窗口内。序列
  期间不得切寄存器组（ISel 约束写死；ISR 见下）。

**MachineInstr 伪指令形态**：`TFPU_<OP>`（如 TFPU_SIN）单条伪指令承载整个
序列，操作数 `i32 AR [, i32 BR]` + imm 命令码：
- Uses：`[DR0]`（双目 BR）、`[DR4]`（AR）；Defs：`[DR4]`（结果）；
  **隐式 Defs 全部 R0-R7**（触发期间硬件视为整窗占用）+ 不可建模副作用
  （写 SFR 0xED）——形如 ABI 固定寄存器先例（DPL/DPH 固定落点，
  InstrInfo.td:193-215 的 pinned 写模式）。
- 装载/读回由 RA 按 fixed-reg 约束经普通 mov 完成（DR0/DR4 均为 GPR32
  成员，:228）；**AR 与 BR 窗口物理不相交（DR0 vs DR4），两装载无互斥
  冲突**；与其它活跃值的冲突由整窗 clobber 声明交给 RA 前置迁移/溢出。
- **并行复制/活跃值**：跨伪指令活跃的 R0-R7 值必须先迁出（序列 = 单条
  MachineInstr，RA 后展开，**绝不跨 basic block 活跃**，rev-0 风险条转正
  为硬约束）。

**展开序列（RA 后）**，以 `c = a op b` 为例：
1. `mov DR4, <a>`、`mov DR0, <b>`（双目；单目仅 DR4）；
2. **触发**：`mov 0xED, #<cmd>` ——**直接寻址立即数写**，编码 `75 ED <cmd>`
   （新 pinned .td 指令 TFPU_TRG，地址字面量入指令串，先例 MOV8dpxl
   "mov 0x84, rN" :215；**命令码必须是立即数操作数**，手册 :21 红字注——
   不允许经寄存器/间接寻址写 DMAIR）；
3. **等待**：按该命令 35.3 最坏时钟的固定延时（NOP 链/短循环；0x3F PLL
   异步时钟域按频率比加裕量）——手册无完成位/中断（§1.4），0x33 轮询因
   位定义缺失不采用（登记"需实测"）；
4. **读回**：`mov <c>, DR4`。

**f32 软化后的 lowering 落点**：IR intrinsic 声明为 f32 参数/结果；类型
合法化（SoftenFloatResult/LowerOperation，MCS251ISelLowering.cpp）把操作数
视作 i32 vreg（位模式不动），选为 custom SelectionDAG 节点
`TFPU_<op>(i32, [i32], imm)`，再降为上述 MachineInstr 伪指令；RA 后由
展开 pass（BranchRelaxation 同层）生成 4 步序列。全程不经过 libcall 通路
（软浮点 ABI DPL:DPH:B:A 与 TFPU 窗口无关，rev-0 的"自带寄存器类"表述
落实为 fixed-reg 约束）。

**DMA 状态/初始化/中断约束**：
- 启动一次性序列（用户代码或 CRT hook）：时钟源选择 0x3E/0x3F（:286-296，
  放初始化处）→ 初始化协处理器 0x31（:244-249）→ **清除异常 0x32**（0x31
  完成后会产生异常状态需软件清除，:246——漏清会污染后续 0x33 状态语义）。
  编译器**不自动插入**启动序列（S3 范围），demo 改写包（S4）负责显式调用。
- 无中断可依赖（§1.4）：不注册中断处理、不假设异步完成。
- **ISR 约束**：操作数窗口 = 当前组 R0-R7；寄存器隔离（整组保存/恢复或 PSW[4:3]
  切组）不等于共享 TFPU 引擎可重入——**完整装载—触发—等待—读回期间必须保证
  对 TFPU 的独占，禁止 ISR/嵌套 ISR 在窗口内重入 TFPU**；切组隔离与在途写回的
  硬件行为未实测前不得视为安全（S3 文档 + 测试注记；不实现自动保存，与
  "序列不跨块"约束一致）。

**副作用与 TPIN 顺序（防优化移出测量窗口）**：IR intrinsic 标记
`IntrHasSideEffects`（或 InaccessibleMemOnly 语义），触发指令 TFPU_TRG 亦
标记不可建模副作用；装载/等待/读回与触发**同属一条伪指令**，不存在被拆开
重排的窗口。TPIN 翻转本身是 volatile 位写。测试：O0/O2 两档下
`TPIN=0; …TFPU 序列…; TPIN=1;` 的机器码顺序不变（§4 矩阵）。

## 3. 切片划分（rev-1：S2∥S3）

- **G7-S1（改写包）**：demo 36 两个站点改纯整数（**宽化中间值**，§2.1）；
  38 样例无涉；记账转 "rewrite-resolved"；改写包回归。
- **G7-S2（编译器，小）**：AS0 可变 f32 标量初始化器位模式放行（§2.2，
  共享递归门不动）；测试见 §4。
- **G7-S3（编译器，中，需 D2/D3 先裁）**：TFPU intrinsic 族——IR intrinsic
  注册 + ContractCheck ID 白名单、builtin（Sema/CG）、ISelLowering 软化落点、
  TFPU 伪指令 + TFPU_TRG pinned 指令 + RA 后展开（§2.4）；**不连**转换类
  （0x23-0x29，D6）、comp/check（0x21/0x22，与软浮点比较路径重叠）。
- **G7-S4（端到端）**：demo 38 改写（cfl 站点→builtin、include 修正、启动
  序列 0x3E/0x31/0x32 显式化）+ 全链 + QEMU：若 QEMU 无 DMAIR/TFPU 模型 →
  T2 记 "model-capability 未支持"（同 G1 高槽口径，**只记未验证，不构成本片
  验收**）；T1 **核验完整触发序列编码**为验收底线（§4）。

依赖：**S1 ∥ S2 ∥ S3**（S3 仅受 D2/D3 裁定门控；S2 改 AsmPrinter、S3 改
ContractCheck/ISelLowering/InstrInfo/builtin 表，无共享文件）；**S4 依赖
S2+S3**（demo 38 同时吃初始化器与 builtin 两面）。

## 4. 测试矩阵（摘要）

- **36 改写**：raw 值域抽样（0/1/9/6553/6554/32768/65535）× 宽化整数式对照
  宿主 oracle（demo36_equiv_oracle.result.txt 已给基准值）；**169 负分支与
  174 正分支各一套**；另置一条"禁窄乘"防回归用例（raw=6554 处宽化被撤销
  必须被测试抓到）。
- **S2**：位模式 golden 大端 4B——`3.9f=0x4079999A → 40 79 99 9A`、
  `−0.0 → 80 00 00 00`、`+Inf → 7F 80 00 00`、**qNaN payload 0x7FC00001
  逐位保留**；`float`/`unsigned` 同名共存不混淆；零图（`float cfl3;`）→
  XINIT clear-only 记录；**启动复制路径**（XINIT 负载经 CRT 落 DSEG 后字节
  相等：对象字节核验，QEMU 可用时加运行时 out_float 对照）；**未支持形态
  冻结诊断**——const float、float 数组/struct、`__xdata float`、`__code
  float` 维持原拒文案逐字节不变；ELF/REL 双路、opt 两档；既有
  global-constant/code-placement 全族不破。
- **S3**：每命令 1 正例：核验**完整触发序列**（DR0/DR4 装载 mov 链 +
  `75 ED <cmd>` 触发字节 + 等待 + DR4 读回）+ `-verify-machineinstrs`；
  负例：f64 实参、错签名、未注册名/已注册名错签名（P-4 段两条）、builtin
  名不泄漏进 `!mcs251.signatures`；**O0/O2 求值顺序**（TPIN 窗口内序列不
  被移出/重排）；**寄存器压力**（序列周围 8+ 活跃值 → 溢出走 R0-R7 之外，
  窗口不被踩）；**嵌套调用**（`tfpu_mul(tfpu_sin(x), y)`——AR 窗口重装载、
  中间结果先读回再回填）；**TPIN 顺序**（bit 翻转夹住的序列字节序稳定）；
  ContractCheck 文案冻结。
- **S4**：38 全链 T0/T1；T1 底线 = 每个浮点站点为完整触发序列且**窗口内无
  软浮点 math/算术 libcall 回退**（.text 无对应 call/符号引用）；启动序列
  0x3E/0x31/0x32 存在且顺序正确；T2 按 QEMU 能力如实记（无能力 → 仅记
  "model-capability 未支持"，不算验收失败也不算通过）。
- 回归基线：llvm 167 / lld 23 / clang 拼单 64（实测于 076cb61ed + 在途资产，
  实施时以干净 HEAD 复测）。

## 5. 风险

- QEMU DMAIR/TFPU 行为未证 → S4 的 T2 可能整批 silent（登记，不阻塞；T1
  序列核验为底线）。
- **固定延时模型是手册沉默下的推断**（情报 §1 已标注分析属性）：若硬件
  实测发现可轮询状态位（0x33 位定义待实测），等待策略需修订——S3 展开pass
  把等待步独立成可替换的序列步，降低返工面。
- TFPU 舍入模式手册未定义（情报 §4）——TFPU 路径与软浮点路径的数值可能
  逐位不同；demo 38 改写为 builtin 后窗口内不走软浮点（S4 验收项），跨路径
  混算语义登记"需实测"。
- TFPU 转换命令（0x23-0x29）与现有软浮点转换并存的双路径漂移——本片不连
  （D6），保持软浮点转换为唯一路径。
- R0-R7 整窗 clobber 对窄函数的寄存器压力：极端嵌套下装载/读回次数上升
  （性能问题，非正确性；S3 压力测试覆盖）。
- ISR 内 TFPU 使用需整组保存（§2.4）——文档 + 诊断注记，不自动处理。

## 6. PM 决策点（rev-1：D5 转出为正确性约束）

| # | 决策 | 推荐 |
|---|---|---|
| D1 | demo 36：(a) 改写 / (b) 连接窄无符号转换 / (c) 保留 gap | **(b)——PM 裁定 2026-09-15："坚决解决浮点问题，不绕过"**。连接无符号 i32↔f32 双向（`__floatunsisf`/`__fixunssfsi` 两个 helper；**mcs251_float.h 冻结令"不得添加 unsigned conversion helper"由本裁定解除并登记**）；窄类型 i8/i16 转换经类型提升（zext/sext + i32 版 / f32→i32 + trunc）放行、不加窄 helper；demo 36 语料零改动直编。§2.1 穷举等价性保留为改写可行性的历史记录 |
| D2 | 38 math 暴露：A TFPU intrinsic（builtin→IR intrinsic→DMA 触发序列）/ B 软库 / C asm | A |
| D3 | TFPU 双目算术是否同批（TPIN 窗口语义） | 同批连接 add/sub/mul/div/sqrt + 四三角 = 9 个命令 |
| D4 | QEMU 无 TFPU/DMAIR 模型时 38 的 T2 口径 | 记 "model-capability 未支持"（仅记未验证）；T1 完整触发序列编码为验收底线 |
| D6 | TFPU 转换命令（0x23-0x29）与 comp/check（0x21/0x22）范围 | 本片不连（软浮点转换/比较为唯一路径），登记后续 |

**已转出决策表的事项**：
- **NaN/Inf/−0 存储（原 D5）→ S2 正确性约束**：位模式原样 4B 是硬约束而非
  PM 偏好——软化运行时按 DPL:DPH:B:A 位序读回该 4B，任何规范化都会改变
  被读值（NaN payload 携带信息、−0 参与 ==/符号判断），故"原样逐位"直接
  进 S2 规范与 golden 测试，不作为可选项请求裁定。
- **切片并行性（原依赖行）**：S2∥S3（无共享文件）、S4←S2+S3 属排期事实，
  移入 §3。
