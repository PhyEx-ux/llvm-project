# MCS251 bit/sbit 方言支持冻结任务书

**作者：Alice。日期：2026-09-10。**

**状态：可派单的设计规划；不是实现完成或测试通过声明。** 本文冻结任务边界、依赖与验收方法；§8 的用户/PM 决策经 BT00 登记后，相关接口才进入实现冻结。未决协议不得由执行者自行选值。本文不包含实现代码。

**本次交付限制：只新建本文；不修改既有文件，不构建，不运行产品测试，不 git add/commit。** 以下“改动点”“新建测试”“命令族”均指后续获授权的实施任务，不是本次操作。

## 0. 使用纪律、证据与计数

1. 共 **18 张卡：BT00–BT17**，含 1 张冻结/资源卡、17 张工程/验证卡。旧增量稿的 BT0–BT4 是批次，不是本文任务编号。
2. 优先级：用户最新裁定 > BT00 签收的本文接口 > bit 一等类型增量稿 > 方言前端 §7 > 早期兼容包。冲突按 §9 显式处理，不在实现中暗改。
3. 每卡必须携带本文 §1–3、§7–10；每卡自验后由 Alice review。问题退回属主，不跨卡补产品代码，不降低断言换 PASS。
4. 行号为本轮工作树读取锚点，不是假定 HEAD 行号；重定位时同时匹配函数/定义名。新文件无既有行号，标“新建”，并给既有接入点。
5. 只读核证根目录为 `/home/liu/LLVM_STC32/MCS251`。ISR 模板中的 Windows 路径、WSL 启动命令、历史构建树不适用于本轮环境，不能原样复制。
6. 后续构建树、源码快照、并行度、串口和设备由 PM 分配。共享文件串行交接；独立 build 目录不解除共享源码屏障。禁止擅自重配构建、终止他人进程或改 QEMU。
7. 证据标签：**[S] 本轮源码读取**；**[U] 用户提供的已核基线/语料统计**；**[D] 本文设计**；**[P] 待实现与测试**。本文没有新增 [Q] QEMU PASS 或 [H] 真机 PASS。
8. 后续每卡交付绝对路径清单、实际命令/退出码、工具与输入 sha256、逐项通过/失败、未执行项及 Alice review 请求。测试输出放 PM 指定的仓库外绝对目录，不污染旧资产。

## 1. 基线核对与三层责任

### 1.1 已存在与未存在

| 基线 | 精确证据与任务含义 |
|---|---|
| 仅有 CLRC，没有完整位指令族 | [S] `/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCS251InstrInfo.td:223`；`/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCTargetDesc/MCS251MCCodeEmitter.cpp:412`。CLRC 是清 carry，不是 CLR bit。 |
| 没有目标 AsmParser | [S] `/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251` 无 AsmParser 子目录；不得把 llvm-mc 汇编正例作为可用测试入口。采用 MIR→对象、yaml2obj、对象字节检查。 |
| 普通 AS5 三层拒绝 | [S] `/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCS251ISelLowering.cpp:1502–1536` 的 checkDataAddressSpace（用户锚点 1493–1527 已漂移）；`/home/liu/LLVM_STC32/MCS251/llvm/lib/CodeGen/MCS251ContractVerifier.cpp:359–375`；`/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCS251AsmPrinter.cpp:788–796`。新增受控能力不删除普通 AS5 禁令。 |
| 无 p5，无位 intrinsic | [S] `/home/liu/LLVM_STC32/MCS251/llvm/lib/TargetParser/MCS251TargetParser.cpp:9–31`；`/home/liu/LLVM_STC32/MCS251/llvm/include/llvm/IR/Intrinsics.td:2920–2937` 无 MCS251 include。不把 DataLayout 对未列 AS 回退 p0 当 p5 ABI。 |
| Keil 开关已存在，仅登记 interrupt | [S] `/home/liu/LLVM_STC32/MCS251/clang/lib/Basic/IdentifierTable.cpp:300–308`；`/home/liu/LLVM_STC32/MCS251/clang/include/clang/Basic/TokenKinds.def:688–694`；`/home/liu/LLVM_STC32/MCS251/clang/include/clang/Basic/LangOptions.def:310`。不新造 -fkeil-c251。 |
| 前端 builtin 表为空；无目标 ABI 分类器 | [S] `/home/liu/LLVM_STC32/MCS251/clang/lib/Basic/Targets/MCS251.h:103–105`；`/home/liu/LLVM_STC32/MCS251/clang/lib/CodeGen/CodeGenModule.cpp:118–128` 走默认分派；现无 `/home/liu/LLVM_STC32/MCS251/clang/lib/CodeGen/Targets/MCS251.cpp`。 |
| 后续静态参数槽实际已实现 | [S] `/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCS251ISelLowering.cpp:2369–2382,2391–2409`；`/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCS251AsmPrinter.cpp:586–641`。叶函数 OSEG overlay，非叶 DSEG。CallingConv.td:18–24 的“未实现多参”注释已过时。 |
| BSEG_BYTES 与 BIT_BANK 是字节预留/overlay，不是单位槽 allocator | [S] `/home/liu/LLVM_STC32/MCS251/lld/MCS251/LinkerCore.cpp:232–244,1172–1239,1466–1492`。必须增加跨 TU 位身份与占用账本。 |
| IRQ CRT 拥有且无条件清零全部 16B | [S] `/home/liu/LLVM_STC32/MCS251/validation/mcs251-elf/runtime/crt-irq.yaml:58–74,107–130`。新 allocator 不能再次 reserve 16B；保留外部 backing 时不能仍使用此无条件清零流程。 |
| XINIT 已允许真实分配的 BSEG_BYTES | [S] `/home/liu/LLVM_STC32/MCS251/lld/MCS251/LinkerCore.cpp:1879–1929`。这不是 mask 初始化或位 allocator 已完成。 |
| relocation 9 已占用 | [S] `/home/liu/LLVM_STC32/MCS251/llvm/include/llvm/BinaryFormat/ELFRelocs/MCS251.def:13–22` 的 R_MCS251_ISR_REF=9。位 relocation 不得复用 9。 |
| int 默认 32，+int16 才 16 | [S] `/home/liu/LLVM_STC32/MCS251/clang/lib/Basic/Targets/MCS251.h:24–37,111–112`。bit 整数提升必须覆盖两种 C int 模型。 |

### 1.2 分工矩阵

| 能力 | 后端可做（含 LLVM/MC/lld/CRT） | Clang 极薄目标接入 | 头文件垫片 |
|---|---|---|---|
| 位原生指令、carry/PSW、分支距离 | BT01–03 | 不选择机器码 | 无 |
| L1 固定受控位左值 | intrinsic/pseudo/最终边界校验 | BT04：builtin ID、受控左值身份、Sema、直接 CodeGen | BT06：位名展开为 builtin，不生成普通指针 |
| 旧式 sbit 声明 | 复用 L1 | BT05：真实 token/声明解析，归约同一受控引用 | 提供 SFR 地址描述与官方名字；不能用宏吞掉声明 |
| bit 一等类型 | 复用整数值寄存器与 byte ABI | BT08–11：类型、转换、ABI 分类、局部对象路径 | 仅别名/typedef；禁止宏伪装成 u8/_Bool |
| 固定 RAM backing、静态 bit | BT07、12–14：注册、relocation、跨 TU allocation、初始化 | 发受控描述/句柄，不定物理位槽 | 提供显式 ownership 配置，不私分位号 |
| 测试与能力发布 | BT15–17 | C→IR 正负闭环 | 语料覆盖可统计，不把命中率当通过率 |

“极薄”指目标专属、最小必要的语言接入，不等于只改 parser 两行。一个可 typedef、可传参但不可取址/布局查询的独立类型，必须修改 AST/Sema/CodeGen 和序列化边界。

## 2. 共同语义冻结区 [D]

### 2.1 一种值类型，三种承载

- `__bit` 是 MCS251 C 核心拼写；裸 `bit`、`sbit` 只在 `-fmcs251-keil` 下激活，其他目标/语言模式不被抢占标识符。
- 静态存储期 bit：一个不可取址的逻辑对象，由 lld 分配 RAM 位槽。
- 自动局部/形参副本：调用私有的逻辑值；SSA/寄存器，必要时 1B、内容 0/1 的 AS0 栈槽，不物理打包。
- L1/sbit：固定位置的受控引用，不分配独立对象；读取得到普通 0/1 值，不能传递引用。
- 独立 bit 类型不是 `_Bool` typedef，也不是 `unsigned _BitInt(1)`；内部可复用布尔转换机制，但不能继承错误的截断、取址或布局规则。
- 整数转 bit：零→0，任意非零→1，包括 2、-1、最高位；bit→int 零扩展，整数提升遵循当前 int16/int32。
- 自动值支持正常整数提升后的算术/比较/逻辑、短路及赋回规范化；`~b` 是提升后的按位反，不是 toggle。浮点、指针↔bit 转换不在本役支持集。
- typedef/cv/extern/static 与函数签名保留 bit 身份；同 TU tentative declaration 正常合并。首期不实现跨 TU COMMON 合并，`-fcommon` 产生的 bit COMMON 必须诊断，推荐生产使用 `-fno-common`。
- 取址、指针、数组（含参数数组衰变）、聚合字段/位域、sizeof/alignof/offsetof、_Atomic、TLS、memcpy/memmove 位对象均拒绝，含 typedef/typeof 间接构造。函数指针“签名含 bit 值”不是“指向 bit 对象的指针”，前者按已支持普通调用范围放行。
- `bit xdata` 及任意冲突 address_space/placement 拒绝；不静默忽略 xdata。

### 2.2 L1 强制表与语法认可边界

接口保持 `__builtin_mcs251_bit_lvalue(unsigned_constant_bit_address)`。参数必须 ICE，先按完整 APSInt 检查负数和范围，再缩窄。地址 0 合法；动态位地址禁止。

| 源操作 | 首期保证/限制 |
|---|---|
| 赋 0/1 或可折叠为常量的整数 | 规范化后恰一次 CLR/SETB；不读目标位 |
| 独立动态值赋位 | RHS 求值一次；实际路径恰一次目标位写；分支 SETB/CLR，不依赖 MOV bit,C |
| 读位值 | 一次 MOV C,bit 采样，再无额外位读取地物化 0/1 |
| 直接条件 if(b)/if(!b) | 恰一次 JB/JNB 测试；长跳转用保持测试次数的 trampoline |
| 丢弃结果的 b^=1、b=!b | 同一受控位置，恰一次 CPL；先在 AST 识别，不先发 read |
| 常量/独立动态赋值的结果被使用 | 复用已知/已计算的赋值值，不补读 volatile 目标 |
| CPL 更新结果被使用 | 硬拒绝；不能 CPL 后补读伪造原子返回值 |
| b=other_bit | 源读一次、目的写一次，不是原子复制 |
| b=~b、b=b+1、++b、其他自读复杂 RMW | 持久/受控位左值首期拒绝，建议显式采样到临时普通值后赋回；自动调用私有值按正常规则 |
| if(b) b=0 | 一次测试加路径上的一次 CLR；禁止自动 JBC 合并 |
| JBC | BT01–02 仅编码/机器语义探针；公共 test-and-clear builtin 后置 |

同一引用判定采用去括号后的受控固定地址/规范声明身份，不凭变量名、源字符串、可选优化器 CSE；不同别名表达式的额外推断首期不承诺。持久 bit 的强制更新表与固定 L1 共用。

### 2.3 volatile、内存与寄存器效果

- 固定 L1/sbit 隐含 volatile，不允许去掉；普通 bit 是否 volatile 由声明决定。首版可保守保留非 volatile 持久访问，不承诺 DCE/CSE 优化收益；不能把它在源语言上变成 volatile。
- 有序程序路径上的 volatile 访问次数与顺序在 O0/O1/O2/O3/Os 不变；源语言正常短路与不可达路径不产生运行访问。不禁止等价内联本身，只禁止改变动态路径上可观察次数。
- 第一版位 intrinsic 采用覆盖普通内存的保守 read/write effects；不得 readnone、argmemonly（固定地址没有指针参数）、inaccessiblememonly、speculatable。有副作用与禁止不安全复制属性的组合由 BT03 用本树 API 固化并验证；不能只加 convergent 就宣称内存语义正确。
- DAG 有 chain，机器指令有 mayLoad/mayStore/side effects 与 backing byte 的保守 MMO；未知 backing 使用 unknown alias，不制造独立虚构内存域。普通 byte access/call 与位 RAM 可别名，不新增 AS5 NoAlias/TBAA 优惠。
- MOV C,bit 定义 carry/PSW 对应状态；采样→物化使用 glue/显式依赖。CPL 是一条目标 RMW；JB/JNB 读，JBC 读写；仅描述真实受影响寄存器，避免过度 clobber 破坏活跃值。
- 普通接口拒绝 ACC/B/PSW 等编译器管理寄存器位及 EA（EA=位地址 AF）；EA 临界区走独立未来接口。SFR 编码有效不等于外设可安全访问，发布仅限审查后的地址/操作类别。
- RAM 位地址 B∈[0,127]：byte=0x20+(B>>3)，index=B&7。SFR B∈[128,255]：byte=B&0xf8，index=B&7。**位 FF 的 backing 是 F8，不是 direct byte FF。** 不访问 RSTCFG 等危险配置作探针。
- 这里的“原子”只指批准运行环境下单指令采样/更新，不是 C11 memory_order、总线事务、DMA/NMI 同步或多操作事务。

### 2.4 值 ABI（待 PM 最终签收，推荐冻结）

| 位置 | 契约 |
|---|---|
| 第一源参数为 bit | 完整 DPL=0 或 1；不只约束 bit0 |
| 第一参数非 bit | 原 DPL/DPH/B/A 分配不变；不把后续 bit 塞进空闲位 |
| 第二及以后 bit | 原源参数序号 `_callee_PARM_n`，每个 1B、0/1；不是第 n 个 bit 的序号 |
| bit 返回 | 完整 DPL=0/1，不经 carry |
| IR 函数边界 | 显式 i8 载荷；内部可用 i1，不能靠 i1+zeroext 假定会得到约定 i8 签名 |
| 多 bit | 不打包，不设 8 个参数上限；合成 9 个及更多测试补语料空白 |
| 形参函数内副本 | 入口尽早读取静态槽，普通活跃值/spill 跨调用保存 |

普通 C/Fast 物理 CC 不改。静态槽保持叶 OSEG/非叶 DSEG 的既有规则；后续参数不因此自动重入。单参 bit 函数指针沿现有单参间接调用；多参间接调用、varargs、无原型 bit 调用首期拒绝。不承诺 Keil/SDCC 裸混链；同版 ABI 的跨 TU bit 签名必须核对，不能将 bit 与普通 u8 因最终同为 i8 而视为源类型兼容。

### 2.5 对象、位句柄与 CRT 方案

**推荐内部句柄路线：不引入普通 AS5 指针。** 固定 intrinsic 取整数 ICE；符号 intrinsic 取受验证的 AS0 符号句柄，句柄只是对象身份，不是 data byte。推荐用带精确目标描述的 i8 GlobalVariable 占位表达链接身份，但禁止普通 load/store/GEP/cast/call/ptrtoint/initializer 逃逸；AsmPrinter 专用分流，不分配该占位的 1B DSEG/XINIT。BT00 必须用优化保活与用途负例证明后才签收；不能仅靠可丢失的 named metadata 管存储语义。

协议任务必须交付以下完整字段/验证，不允许“实现时再补”：

- 每条定义/引用/固定 backing 请求：版本、记录长度、kind、能力、符号关联、0/1 初值、cv/链接身份、owner、保留字段为零。
- 推荐专用非 ALLOC `.mcs251.bit` 记录和专用 RELA 身份引用；定义引用真实具名对象符号，不折叠成 section+addend。位符号没有普通 byte 大小/可解引用地址语义。
- 推荐 `R_MCS251_BIT_REF`（身份关联，零写入宽度）与 `R_MCS251_BITADDR8`（批准位指令的 8-bit 地址字段）分开；名字/数值/字段偏移在 BT00 登记。现有 0–9 不改；本文不擅自分配正式编号。
- BITADDR8 只接合法位对象、零 addend、合法指令字段。普通地址 relocation 指向位对象、BITADDR8 指向普通对象/函数/section/ABS 都拒绝。不得只检查 0..255 后接受任意符号。
- 静态 bit ABI 签名记录覆盖定义/声明/直接引用，版本与原源参数 bit 位置/返回身份可核对。普通无 bit 对象保持兼容；已知 bit 与已知 byte 签名冲突硬错。没有签名的第三方 bit ABI 不获兼容承诺，不能以缺记录推断兼容。
- 外部强定义/extern 正常解析；本地静态以 TU+本地符号身份区分；重复强定义、未定义、初值冲突硬错。weak/COMDAT/alias/ifunc/COMMON/TLS 首期不支持。
- 默认非 overlay 静态位池总量 128；显式外部预留、固定 backing owner 占用、legacy BIT_BANK 先扣除，自动 bit 后分配，普通 RAM 最后分配。固定 RAM backing 整字节保守排除自动 packing；多个固定别名引用同 owner 不重复占用。
- 确定性按已解析的输入次序及符号顺序 first-fit；相同输入顺序结果逐字节重现。换输入顺序可换位槽，但运行语义必须一致，不承诺布局地址稳定。
- 位对象按位跨 TU 共用 backing byte；普通 RAM 账本按整字节唯一 owner 计费。不按函数生命周期 overlay，不把独立对象塞进 BIT_BANK overlay。
- 容量耗尽链接失败，输出请求量/可用量/占用来源/首个未分配符号；不退化为 byte RAM。
- **CRT 所有权选择**：推荐新 bit-aware CRT/profile，旧 IRQ CRT 保持旧行为并与新受控位协议 fail-closed。新 profile 让 lld 对 16B 池只做一次物理预留并进行子分配，CRT 消费 lld 生成的 owner mask/value；外部固定 owner 不被无条件清零。不能“在旧 16B reservation 后再申请 bit bytes”。
- 每个自有 backing byte 合成唯一初始化项：只初始化 owned mask 内的 0/1 初值，保留 mask 外邻位；不能将输入顺序 XINIT 全字节覆盖作为多个 bit 初值合并规则。mask=0 的 byte 不读不写；mask=FF 可直接写 value；部分 mask 的 byte RMW 只用于启动期普通 RAM、普通中断尚未开启时。
- 初始化在 main/用户开放中断前完成；NOBITS 不是上电零。禁止初始化 SFR、用户预留外部 owner 或同域普通对象。新 profile 的 ROM 表读取复用已验证 CRT 路线，但须重新验，不借此开放通用 AS4 load。
- 全局/静态 bit 是程序状态，不纳入 ISR 37B 保存。ISR 对共享 bit 的修改返回后保留；自动值由寄存器现场/本次栈帧保护。不得恢复旧中断设计已撤销的调用闭包检查。

## 3. 依赖顺序与最小切片

### 3.1 建议修正“解锁 34%”

[U] 307 函数/94 文件是 bit/sbit **联合命中**，94/275=34.18%；不是仅 sbit 文件数，更不是一次切片可通过的文件数。裸 bit 函数声明/定义 118 处/38 文件（不是 118 个独立函数）、29 处 typedef bit BOOL、90 处具名 bit 形参，且有自动/静态对象；这些需后续类型/ABI/BSEG。无 bit*/bit[] 只是源文本负证据，不能删除负测；2 处 bit xdata 必须作为预期拒绝单列。

**确认 L1+sbit 优先，但把目标表述改为“优先解除 34.18% 联合命中文件中的固定位访问阻塞”，不承诺全数解锁。** BT16 应拆出 sbit-only、含一等 bit、混合其他阻塞三组，并分别报告 parse/IR/object/link/runtime 通过率。

### 3.2 发布切片

| 切片 | 必须完成 | 明确边界 |
|---|---|---|
| S0 后端开发地基 | BT00、BT01–03；BT15 对应 IR/MIR 子集；BT17 早期指令 control/qualification | 不是源语言发布，不开放普通 AS5；JBC 仅探针 |
| **S1 最小推荐** | BT04–07，加 S0；BT15 L1/sbit 负例、BT16 S1 C→ELF 子集、BT17 已批准 RAM/安全 SFR 子集 | L1 固定位 builtin + 旧式 sbit 解析 + 薄头；不要求 BT08–14。固定 RAM 必须有 owner 登记；SFR-only 可以不等待 RAM 运行发布，但不得把 RAM 门禁解除 |
| S2 调用私有 bit 值 | BT08–11 + BT15/16/17 对应增量 | bit 类型、转换、自动局部、参数/返回；global/static bit 保持“尚未实现”错误直到 S3 |
| S3 持久 bit 完整闭环 | BT12–14 + BT15–17 全集 | 跨 TU packing、专用 relocation、初始化与 ISR 通信；不得只开放无初值 globals 后宣布完成 |

### 3.3 DAG 与并行屏障

- BT00 → BT01 → BT02 → BT03 → BT04 → BT05 → BT06。
- BT00 → BT07；BT07 的结构夹具可独立写，真实发射依赖 BT03/04，S1 集成依赖 BT06/07。
- BT04 后交接公共 AST/CodeGen 文件 → BT08 → BT09 → BT10 → BT11。
- BT00 协议签收、BT03 效果模型 → BT12；BT12 前端发射依赖 BT08/09，完整产品对象验收需 BT10/11。
- BT07 + BT12 → BT13；BT00 CRT 协议 → BT14 资产开发；BT13+BT14 才可结初始化闭环。
- BT15 分阶段写负测，不到最后才补；BT16 按 S1/S2/S3 汇总；BT17 可早做指令 harness，运行对应生产镜像必须等 BT16 对应切片。
- 共享 Lowering/InstrInfo/AsmPrinter/ContractVerifier、Clang SemaExpr/CGExpr/CodeGenModule、lld LinkerCore **各时段唯一属主**。BT01–03、BT07/12–14 在共享文件上串行交接；不得为了并行把 LinkerCore.cpp 同时派给两人。
- BT17 的 ISA 资格证据是 S1 生产发布硬门禁，不是所有开发编码的硬前置，避免“要 builtin 才能探针、要探针才能写 builtin”的环。

旧批次映射：BT0→BT00/协议部分；BT1→BT08–11；BT2→BT01–03/12；BT3→BT07/13/14；BT4→BT15–17。L1/sbit 是插入旧 BT1 之前的新最小纵切。

## 4. 任务卡 BT00–BT07：最小 L1/sbit 切片

### BT00 — 接口冻结、证据基线与资源登记（M，PM/Alice）

**目标**：消除实施者需要猜测的接口；冻结 S1/S2/S3 发布清单与任务唯一属主。

**前置**：精读本文及三份设计；§8 用户/PM 选择可逐切片签收，不要求为 S1 先批准全部 S3 细节。

**file:line 级改动点（后续登记入口）**：
- `/home/liu/LLVM_STC32/MCS251/llvm/include/llvm/BinaryFormat/ELFRelocs/MCS251.def:13–22`：登记新号，不动 ISR_REF=9。
- `/home/liu/LLVM_STC32/MCS251/llvm/lib/TargetParser/MCS251TargetParser.cpp:9–31,78–85`：登记“bit 能力独立、p5 不增、五字段原义不改”的兼容原则，不机械扩展 ExecutionContract。
- 新建 `/home/liu/LLVM_STC32/MCS251/llvm/include/llvm/BinaryFormat/MCS251Bit.h` 的后续规格：记录字节偏移/长度/端序、能力版本、owner、ABI 签名、拒绝规则与两个 relocation。
- `/home/liu/LLVM_STC32/MCS251/validation/mcs251-elf/runtime/crt-irq.yaml:107–145`：登记旧 asset/profile 与新 bit-aware profile 的互斥/兼容矩阵，不能复用旧资产身份。

**可测验收/矩阵**：独立 fixture 能表达一个固定 RAM 请求、一个 SFR 请求、一个 bit 定义+extern、一个函数签名；每字段都有合法值/未知值/缺失值测试责任卡。lit/字节：BT12/13/14 的 reader 与 writer golden 结构在编码前独立签收。端到端：核定同源工具路径及最小链接命令，无运行 PASS 要求。记录哪些待拍板项阻塞哪张卡；协议表不得留“任选”。

**明确不做**：本卡不实现 parser/allocator，不冻结未经手册/探针核对的 ISA 字节，不擅自重定义 ELF v1 note 或发布 v2 对象能力。

### BT01 — 位原生指令定义、效果与 MC 编码（L，后端）

**目标**：新增 SETB/CLR/CPL/MOV C,bit/JB/JNB/JBC 的真实机器形式，精确立即数/size/flags；CLRC 回归不变。

**前置**：BT00 S0 签收；隔离位指令手册/独立 oracle 及安全地址清单。ISA 未核形式只进入探针，不公开 builtin。

**file:line 级改动点**：
- `/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCS251InstrInfo.td:223` 附近增加 bit8 operand 与操作定义；分支 destination 推荐统一保持 operand0，适配现有 relaxation。
- `/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCTargetDesc/MCS251MCCodeEmitter.cpp:412`：编码立即数检查、地址字节与相对分支字段，禁止先 uint8 截断再验范围。
- `/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCS251InstrInfo.cpp:260–304`：登记分支类别和真实 size；必须同时更新 getInstSizeInBytes，不只 TD Size。
- `/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCS251RegisterInfo.td`（既有寄存器定义入口）：补充/核对 carry 与 PSW 的精确 Use/Def/别名；不能把 CLRC 既有效果缺口带进新指令。
- 新建 `/home/liu/LLVM_STC32/MCS251/llvm/test/CodeGen/MCS251/bit-instructions.mir`。

**可测验收/矩阵**：lit 正例 MIR→ELF→dump .text，按独立手册/oracle golden 比较完整字节。经典兼容编码候选是 SETB D2 B、CLR C2 B、CPL B2 B、MOV C,bit A2 B、JB 20 B rel、JNB 30 B rel、JBC 10 B rel；这是待核候选而非本轮已验事实，须核本目标 Binary/native 前缀规则再冻结。测 B=0,7,8,127,128,255 编码（危险 SFR 仅静态编码），-1/256/宽常量必须错。字节 size=2/3 的候选由实际批准形式确定；机器 verifier 覆盖隐式寄存器操作数。端到端 ISA 运行交 BT17，不能以打印助记符代替字节验收。

**明确不做**：AsmParser、通用 disassembler、MOV bit,C 生产 lowering、L2 byte-mask 优化、JBC C 接口。

### BT02 — 位分支与长分支松弛（M，后端）

**目标**：JB/JNB/JBC 路径、位地址操作数和 rel8 距离正确，长分支不重复采样/清位。

**前置**：BT01；位指令真实 size 已确认。

**file:line 级改动点**：
- `/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCS251BranchRelaxation.cpp:183–208,223–258,270–282`：扩展批准 bit branch；保持取分支原条件和 opcode，尤其不能把 JBC 简单反相。
- `/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCS251InstrInfo.cpp:260–275,282–304`：size/branch 分类共用。
- `/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCTargetDesc/MCS251AsmBackend.cpp:101,218–235` 与 `/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCTargetDesc/MCS251ELFObjectWriter.cpp:36–63`：PC8 字段偏移与相对下一指令基准；不把 bit 字段当 displacement。
- 新建 `/home/liu/LLVM_STC32/MCS251/llvm/test/CodeGen/MCS251/bit-branches.mir`。

**可测验收/矩阵**：lit 测 rel8=-128,-127,0,127，超界 -129/128；CFG 分支正确松弛成一次原 bit branch+EJMP trampoline，MIR-only 外部符号越界明确报错而非截断。对象解码独立计算 PC-next+signed(rel8)；前后向与两出口均检查。JBC 的 0 路径不清/不跳，1 路径清且跳；长跳也只有一次测试清除。端到端 BT17 双路径哨兵。

**明确不做**：启用通用 analyzeBranch/branch-folder、新的链接期通用 relaxation、不经审查的 JBC 反条件变换。

### BT03 — 位 intrinsic、DAG lowering 与效果模型（L，后端）

**目标**：用独立操作 ID 表达 read/set/clear/toggle/test，形成不依赖优化猜测的合法机器路径。

**前置**：BT00 intrinsic 接口签收，BT01/02；符号形式的产品发射后续交 BT12。

**file:line 级改动点**：
- 新建 `/home/liu/LLVM_STC32/MCS251/llvm/include/llvm/IR/IntrinsicsMCS251.td`；接 `/home/liu/LLVM_STC32/MCS251/llvm/include/llvm/IR/Intrinsics.td:2920–2937`。
- `/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCS251ISelLowering.cpp:1502–1536,1744,1843`：在专用 intrinsic 路径实现，不改普通 load/store 的 AS5 错误。配套 Lowering.h/ISelDAGToDAG 的目标节点分派经属主登记。
- `/home/liu/LLVM_STC32/MCS251/llvm/lib/CodeGen/MCS251ContractVerifier.cpp:924–992`：目标入口检查 intrinsic ID、签名、常量、能力、地址/用途；即使通用 verifier 关闭也执行。
- `/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCS251AsmPrinter.cpp:644–661`：残留受保证 pseudo/非法机器路线最终拒绝。
- 新建 `/home/liu/LLVM_STC32/MCS251/llvm/test/CodeGen/MCS251/bit-intrinsics.ll` 与 `bit-effects.ll`（均位于同一绝对目录）。

**可测验收/矩阵**：lit 正例固定 read→i1、set/clear/toggle→void、test→一次读取条件；固定地址使用 immarg 整数，符号族另名，不将动态整数伪装句柄。O0/O2/Os 与 opt instcombine/GVN/LICM/DCE/内联路径，检查 volatile 访问次数、相对 byte load/store/call 顺序，不被投机。负例动态地址、坏签名、伪 intrinsic、错误能力/受限 SFR、错误 volatile 标志、普通 AS5 继续失败。MIR carry 活跃压力与 clobber 哨兵；端到端物化 0/1 不读第二次目标位。

**明确不做**：LLVM atomic 指令映射、inaccessible-memory 优化、普通 AS5 例外、公共 JBC/MOV bit,C 接口。

### BT04 — L1 builtin 与受控位左值 frontend（L，Clang 极薄）

**目标**：现有 call-expression 语法形成受控 lvalue，直接产生 BT03 intrinsic，不先构造普通指针/byte RMW。

**前置**：BT03 签名/错误稳定；早期 AST 可使用专用受控身份配布尔值类型，不依赖完整 bit 声明，BT08 后统一值类型。

**file:line 级改动点**：
- 新建 `/home/liu/LLVM_STC32/MCS251/clang/include/clang/Basic/BuiltinsMCS251.td`；接 `/home/liu/LLVM_STC32/MCS251/clang/include/clang/Basic/CMakeLists.txt:94–105` 的目标 builtin 生成模式；`/home/liu/LLVM_STC32/MCS251/clang/lib/Basic/Targets/MCS251.h:103–105` 注册 shard，配套 MCS251.cpp。
- `/home/liu/LLVM_STC32/MCS251/clang/lib/Sema/SemaChecking.cpp:3049`：按 builtin ID 检 ICE/受控对象 kind；`/home/liu/LLVM_STC32/MCS251/clang/lib/Sema/SemaExpr.cpp:4435,4745,9675,15023`：布局/赋值/取址拒绝。
- `/home/liu/LLVM_STC32/MCS251/clang/lib/CodeGen/CGValue.h:183–189,286–290`：非普通 Address 的受控 LValue kind。
- `/home/liu/LLVM_STC32/MCS251/clang/lib/CodeGen/CGExpr.cpp:1737–1750,2542,2793`；`/home/liu/LLVM_STC32/MCS251/clang/lib/CodeGen/CGExprScalar.cpp:4067,5470`；`/home/liu/LLVM_STC32/MCS251/clang/lib/CodeGen/CodeGenFunction.cpp:1917`：read/write/toggle/condition 分派，AST 模式在产生自读前识别。
- 新建 `/home/liu/LLVM_STC32/MCS251/clang/test/Sema/mcs251-bit-lvalue.c`、`/home/liu/LLVM_STC32/MCS251/clang/test/CodeGen/mcs251-bit-lvalue.c`。

**可测验收/矩阵**：每行 §2.2 都有正/负 lit；赋值 RHS 带计数副作用只算一次，赋值结果不补读；取址/typeof 构造引用/条件 PHI 选择引用报错，选择两个已读取的普通值允许。builtin 不产生 IR 函数调用或 AS5 ptr；O0/O2/Os 保持 SETB/CLR/CPL 路线。PCH/AST 往返保留受控身份。端到端同地址头宏与直接 builtin 生成等价指令，RAM 正例等待 BT07 owner。

**明确不做**：sbit 声明 parser、完整 bit 类型、普通 _Bool 行为修改、用通用 volatile bitfield 冒充此能力。

### BT05 — 旧式 sbit 声明解析（M，Clang 极薄）

**目标**：在 Keil 模式接受 `sbit name = bit_address;` 和 `sbit name = SFR_BASE ^ index;`，归约 BT04 同一受控位置。

**前置**：BT04；BT00/BT06 约定 SFR_BASE 的静态地址描述形态；不要求完整 sfr 声明 parser。

**file:line 级改动点**：
- `/home/liu/LLVM_STC32/MCS251/clang/lib/Basic/IdentifierTable.cpp:300–308`、`/home/liu/LLVM_STC32/MCS251/clang/include/clang/Basic/TokenKinds.def:688–694`：受开关控制的 sbit token。
- `/home/liu/LLVM_STC32/MCS251/clang/lib/Parse/ParseDecl.cpp:2153,2496,3382` 与 Parser.h：声明专用语法，不改变普通表达式 ^ 规则；参考已有 :6695 Keil 后缀的目标限定/恢复纪律，不复制其函数语法。
- `/home/liu/LLVM_STC32/MCS251/clang/lib/Sema/SemaExpr.cpp:9675` 的受控身份接入；SemaDecl/Attr 的新声明属性由 BT00 登记，地址在规范声明上合并，不储存可执行 initializer。
- 新建 `/home/liu/LLVM_STC32/MCS251/clang/test/Parser/mcs251-keil-sbit.c`、`/home/liu/LLVM_STC32/MCS251/clang/test/CodeGen/mcs251-keil-sbit.c`。

**可测验收/矩阵**：lit 数字/宏/括号 ICE、`P3^2`→B2（若 P3 描述地址 B0）、重复同地址声明/冲突重声明、头文件多次包含、错误后下一声明恢复。index=-1/8、非位寻址 byte base、非静态 base、missing initializer、函数内存储声明/数组/指针/函数形态负例。**只有 sbit initializer 的专用 ^ 是定位语法**；普通 `P3 ^ 2` 保持 C XOR。解析 P3 的 volatile SFR lvalue 宏只能提取已经认证的常量地址 AST/描述，不能求值读硬件，也不能按名字 P3 猜地址。关闭开关时 sbit 仍普通标识符；非目标 Keil 开关按既有规则拒绝。端到端不同名字同地址是别名，不分配两个 bit。

**明确不做**：完整 Keil sfr/sfr16/sfr32/far 声明语法、任意官方头原样透明、源码文本重写、宏删除 `sbit ... = ...`。

### BT06 — 薄兼容头与可审计地址描述（M，头文件垫片）

**目标**：给 S1 提供小而真实的寄存器/位名入口，清楚区分已支持、受限与不存在模型的位。

**前置**：BT04/05；PM 批准首批安全 SFR/address 类别。

**file:line 级改动点**：
- `/home/liu/LLVM_STC32/MCS251/validation/mcs251-dialect/DIALECT-PACKAGE.md:130–175,179–205` 仅作需求锚点；本卡不覆盖旧设计原文。
- `/home/liu/LLVM_STC32/MCS251/clang/lib/Basic/Targets/MCS251.h:103–105` 是功能可用性边界，只读消费，不在头里伪造目标内建。
- 后续新建 `/home/liu/LLVM_STC32/MCS251/validation/mcs251-dialect/include/mcs251_bit_compat.h`、`/home/liu/LLVM_STC32/MCS251/validation/mcs251-dialect/bit-registers.json` 及生成/一致性测试；每个地址保存来源与允许操作类别。

**可测验收/矩阵**：lit 预处理后名字展开为 builtin/认证 SFR 描述，不含 `#define bit unsigned char`、`#define bit _Bool`、普通 AS5 指针或 byte-mask SET。同 builtin/同旧式 sbit 声明两入口 C→IR/对象指令一致。地址描述 round-trip、冲突项/受限 EA/管理寄存器明确错误；生成未知寄存器不静默采用 AS0。端到端至少 RAM 固定位、一个批准 GPIO 位、SCON/TCON 中批准子集；无模型外设静态可解析与运行可验分别记账。

**明确不做**：官方寄存器全集、u8 过渡包、GET/SET 掩码作为透明 sbit 实现、EAXFR 自动开启。

### BT07 — 固定 RAM 位引用登记与 backing ownership（L，后端/lld）

**目标**：S1 不依赖自动位 allocator，也能证明固定 RAM 访问指向合法 owner，普通数据不会占入 backing byte。

**前置**：BT00 固定请求线格式，BT03/04 实际请求；reader 可先用独立 YAML 开发。

**file:line 级改动点**：
- `/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCS251AsmPrinter.cpp:664–729`：收集/发固定 RAM 请求（含纯静态声明保留规则），不能只在存在 MachineFunction 时发必要记录。
- `/home/liu/LLVM_STC32/MCS251/lld/MCS251/LinkerCore.cpp:232–244,279,388,1172–1194,1238`：精确 metadata 白名单、owner 与 reserve 顺序；SFR 请求不占 RAM。
- `/home/liu/LLVM_STC32/MCS251/lld/MCS251/LinkerCore.cpp:2372,2477–2490`：map 与链接流程；未知 owner/缺 backing 在完整链接时拒绝。
- 新建 `/home/liu/LLVM_STC32/MCS251/lld/test/MCS251/bit-fixed-backing.test`。

**可测验收/矩阵**：lit 正例位0/7共用 byte20、位8→byte21、位127→byte2F；跨 TU 相同固定引用登记幂等。重复 owner/冲突 DATA_ABS、--reserve-data、DSEG/ISEG/OSEG/BIT_BANK 拒绝；有意 byte/bit alias 必须同 owner，不能靠巧合地址接受双重分配。已有 CRT 16B reserve 只能认作一个经验证 owner/池，不能重复 reserve；S1 使用旧 CRT 时明确接受其“owned 16B 全清零”语义，不支持外部保留邻位，需此能力时等待 BT14。map 写 byte/bit/owner/初始化策略；端到端固定 RAM 哨兵和普通数据同时正确。

**明确不做**：跨 TU 自动 packing、位符号 relocation、任意绝对 byte alias 自动认领、无 owner 固定位默认放行。

## 5. 任务卡 BT08–BT14：一等 bit 与 BSEG 闭环

### BT08 — 独立 bit 类型、拼写与类型构造边界（L，Clang）

**目标**：引入可 typedef 的目标标量类型，持久/自动/受控身份不被普通 _Bool、unsigned char 或 AS 属性替代。

**前置**：BT04/05 公共文件交接，BT00 类型规则；本卡不先开放未实现 global/ABI CodeGen。

**file:line 级改动点**：
- `/home/liu/LLVM_STC32/MCS251/clang/include/clang/AST/BuiltinTypes.def:62–63`、`/home/liu/LLVM_STC32/MCS251/clang/lib/AST/ASTContext.cpp:1293,2017–2054,2213,8293`：目标类型初始化、分类/提升/rank 与内部载荷大小；源 sizeof 禁令不可被内部 1B 布局查询混淆。
- `/home/liu/LLVM_STC32/MCS251/clang/lib/AST/Type.cpp:2445–2491`：整数/标量分类；核 exhaustive switch，禁止只依赖枚举连续范围误分类。
- `/home/liu/LLVM_STC32/MCS251/clang/lib/Basic/IdentifierTable.cpp:300–308`、`/home/liu/LLVM_STC32/MCS251/clang/lib/Parse/ParseDecl.cpp:3382`、`/home/liu/LLVM_STC32/MCS251/clang/lib/Sema/SemaType.cpp:902,1833,2077,10394`：__bit/bit、DeclSpec→类型、pointer/array/atomic 构造拒绝。
- `/home/liu/LLVM_STC32/MCS251/clang/include/clang/Serialization/ASTBitCodes.h:914`、`/home/liu/LLVM_STC32/MCS251/clang/lib/Serialization/ASTReader.cpp:7913`、`/home/liu/LLVM_STC32/MCS251/clang/lib/Serialization/ASTWriter.cpp:7022`：独立类型序列化，不复用 Bool ID。
- 新建 `/home/liu/LLVM_STC32/MCS251/clang/test/Sema/mcs251-bit-type.c`、`/home/liu/LLVM_STC32/MCS251/clang/test/PCH/mcs251-bit.c`。

**可测验收/矩阵**：lit AST 显示独立类型与 typedef bit BOOL，cv/static/extern、具名/无名参数/返回类型可表达；PCH 往返相同 canonical type。pointer/array/field/bitfield/atomic/sizeof/alignof/typeof 间接构造与 bit xdata 负例，普通 _Bool/bit 标识符不回归。端到端此卡只到 AST/受控 IR smoke，未闭合能力仍有专门“未实现”错误，不因 parser 成功悄悄分配 byte global。

**明确不做**：C++ 重载/mangling ABI、复数/向量 bit、聚合布局、把 bit 数组自动转 byte 数组。

### BT09 — bit 转换、整数提升与表达式 lowering（L，Clang）

**目标**：0/1 语义跨常量求值和运行时一致，自动逻辑值与受控存储操作不混淆。

**前置**：BT08；BT03/04 受控操作。

**file:line 级改动点**：
- `/home/liu/LLVM_STC32/MCS251/clang/lib/Sema/SemaExpr.cpp:851,9675–9714`：提升、赋值与转换合法性。
- `/home/liu/LLVM_STC32/MCS251/clang/lib/CodeGen/CodeGenTypes.cpp:104,355`：值 i1/内部 byte 容器分离，不把任何 i1 global 视为 bit。
- `/home/liu/LLVM_STC32/MCS251/clang/lib/CodeGen/CGExprScalar.cpp:1629,3336,4067,5470`：比较非零、扩展、自动值算术与 L1 受限 RMW 分派；必要的常量求值 AST 节点 switch 先登记属主再改。
- 新建 `/home/liu/LLVM_STC32/MCS251/clang/test/CodeGen/mcs251-bit-conversions.c`。

**可测验收/矩阵**：lit 0,1,2,-1、i8/i16/i32 最高位、整数截断前非零比较；IR 不出现把 2 trunc i1 得0的路径。bit→int16/int32 零扩展；`~0`/`~1` 转 bit 都为1，自动值 `b=~b` 不是 CPL；持久形式按 §2.2 诊断。短路的 RHS 副作用计数、比较/条件/!、算术提升结果对 host 整数参考真值表。负例浮点/指针转换与受控 CPL 结果使用。端到端 BT16/17 对拍，字节目标值始终0/1。

**明确不做**：浮点设计、替换普通 C _Bool 规则、把复杂持久 RMW 静默 byte 化。

### BT10 — 规范化 i8 ABI：DPL/静态槽/返回（L，Clang+后端）

**目标**：实现 §2.4，包括真实 C→IR 签名和 ABI 归一化，保留普通函数物理 CC。

**前置**：BT08/09；BT00 ABI/跨 TU 签名协议签收；独占公共 CodeGenModule/ABI 接入窗口。

**file:line 级改动点**：
- `/home/liu/LLVM_STC32/MCS251/clang/lib/CodeGen/CodeGenModule.cpp:118–128` 与 `/home/liu/LLVM_STC32/MCS251/clang/lib/CodeGen/TargetInfo.h:503`：新增目标分派。
- 新建 `/home/liu/LLVM_STC32/MCS251/clang/lib/CodeGen/Targets/MCS251.cpp`，接同目录上层 CMakeLists；以 `/home/liu/LLVM_STC32/MCS251/clang/lib/CodeGen/ABIInfoImpl.cpp:17–74` 为默认行为参照，非 bit 类型完全沿用原规则，不修改全目标 DefaultABIInfo。
- `/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCS251CallingConv.td:18–41`：修正过时注释、保持 i8→DPL 表；不是新增 bit i1 CC。
- `/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCS251ISelLowering.cpp:2277–2382,2411–2510,2688` 与 `/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCS251AsmPrinter.cpp:586–638`：i8 显式边界、源参数索引、1B 槽及读取链验证，普通代码无无关变更。
- 新建 `/home/liu/LLVM_STC32/MCS251/clang/test/CodeGen/mcs251-bit-abi.c`、`/home/liu/LLVM_STC32/MCS251/llvm/test/CodeGen/MCS251/bit-abi.ll`。

**可测验收/矩阵**：lit 首参/返回、bit+i16/i32/已支持指针、非 bit 首参+后续 bit、9个 bit 参数、无名形参、prototype/definition 匹配、跨 TU 两方向调用；函数边界实际 i8，DPL 和每个槽全字节为0/1。入口先读槽再 call，槽 `_PARM_2/_PARM_4` 不压缩成第几个 bit。负例 bit/u8 重声明、varargs/无原型 bit call、多参间接调用、错误签名记录；普通单参函数指针含 bit 值正例。端到端 DPL 高7位污染哨兵、O0/Os caller/callee 交叉档、非 bit ABI 字节 gold 不变；协议对象检查由 BT12/13 联验。

**明确不做**：carry 返回、bits 传参银行、普通 CC 重设计、静态槽自动重入、Keil/SDCC 二进制互操作包装。

### BT11 — 自动局部 bit、形参副本与 spill（M，Clang+后端）

**目标**：每次调用独立值生命周期；非 volatile 尽量 SSA，O0/压力下允许规范化 byte 栈槽。

**前置**：BT09/10；现有栈/ISR 固定帧回归可用。

**file:line 级改动点**：
- `/home/liu/LLVM_STC32/MCS251/clang/lib/CodeGen/CGDecl.cpp:211,1357–1358,1490,1952`：自动/静态分流与初始化，内部 byte alloca 不暴露源取址。
- `/home/liu/LLVM_STC32/MCS251/clang/lib/CodeGen/CodeGenTypes.cpp:104`：内存载荷规范化，volatile auto 使用普通 byte volatile 次数，不变成 BSEG。
- `/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCS251InstrInfo.cpp:59,97`：复用 GPR8 spill/reload；`/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCS251FrameLowering.cpp:107,173` 只核对普通帧/ISR帧交互，必要修复限定到 bit 引入路径。
- 新建 `/home/liu/LLVM_STC32/MCS251/llvm/test/CodeGen/MCS251/bit-spill.ll`、`/home/liu/LLVM_STC32/MCS251/clang/test/CodeGen/mcs251-bit-local.c`。

**可测验收/矩阵**：lit O0/O2/Os 自动局部无 BSEG 注册，优化后可 SSA；高压力、分支 PHI、跨 helper 活跃、单参递归嵌套副本互不覆盖；byte spill 内容0/1、邻 byte 哨兵不变。volatile auto 每次源读写保留，不承诺物理单 bit 指令。负例源取址/数组/动态布局不因内部 alloca 放行。端到端主程序与 ISR 中各自局部 bit 保存正确，共享持久 bit 不回滚；不把多参递归测试写成静态 ABI 重入保证。

**明确不做**：自动局部 bit packing、局部位银行、帧压缩、增加 ISR 共享内存保存。

### BT12 — 位对象句柄、对象记录与专用 relocation 发射（L，LLVM/MC+Clang）

**目标**：把静态 bit 需求从源声明送到 lld；固定与符号位操作共用效果/机器路径，无普通 byte 假对象。

**前置**：BT00 完整线格式签收，BT03、BT08–11；BT07 固定 owner 协议一致。

**file:line 级改动点**：
- `/home/liu/LLVM_STC32/MCS251/clang/lib/CodeGen/CGDecl.cpp:211`、`/home/liu/LLVM_STC32/MCS251/clang/lib/CodeGen/CodeGenModule.cpp:3706–3711,4458–4463` 的目标身份/延迟发射模式：增加专用静态 bit 声明和函数 ABI 签名，不借 ISR 属性冒充。
- `/home/liu/LLVM_STC32/MCS251/llvm/lib/CodeGen/MCS251ContractVerifier.cpp:924–992`：结构化位句柄用途遍历/保活例外；普通 AS5 类型白名单不变。
- `/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCS251AsmPrinter.cpp:664–738,741–839`：位描述走专用记录，不走普通 GV reserve/DSEG/XINIT；含纯 global TU、extern-only TU、未使用 volatile static。
- `/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCTargetDesc/MCS251FixupKinds.h:34–41`；`MCS251AsmBackend.cpp:90–101,218–235`；`MCS251ELFObjectWriter.cpp:36–65`（后三者完整目录同为 `/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCTargetDesc`）：新增位字段 fixup，身份引用保留真实符号，literal 零宽度与写字段分离。
- `/home/liu/LLVM_STC32/MCS251/llvm/include/llvm/BinaryFormat/ELFRelocs/MCS251.def:22` 后追加 BT00 登记号；新建 `/home/liu/LLVM_STC32/MCS251/llvm/test/CodeGen/MCS251/bit-object.ll`。

**可测验收/矩阵**：lit llvm-as/dis 往返、优化前后句柄身份与必需注册保留；定义/引用/内部静态/签名记录字节与独立 golden 一致；无多余 1B DSEG、普通 pointer payload、CSEG bit data。relocation 指向精确符号，不 section-fold；REL/丢身份汇编生产链明确拒绝。负例共享 ConstantExpr 一路合法/另一路普通 load、GEP、cast、ptrtoint、initializer、函数返回句柄，即使 -disable-verify 也拒绝。端到端两个真实 Clang TU 对象进入 BT13，不能只用手写 YAML 宣称编译器 writer 正确。

**明确不做**：普通 AS5 global 特赦、重定位任意 addend、按名字 bit_ 猜身份、LTO/GC/ICF/archives 新支持。

### BT13 — lld 跨 TU 位槽 allocator 与 relocation 消费（L，lld）

**目标**：解析身份→跨 TU 唯一对象→按位分配→整字节占用→位指令字段填充，形成可审计 map。

**前置**：BT07、BT12；BT14 新池/CRT协议已冻结，实际初始化联验稍后。

**file:line 级改动点**：
- `/home/liu/LLVM_STC32/MCS251/lld/MCS251/LinkerCore.cpp:279,388,582,784`：精确 metadata/RELA 校验，在 resolveSymbols 后建立位身份。
- 同文件 `:1172–1239,1241–1275,1466–1492`：保留/固定 owner/legacy overlay/自动位/普通 RAM 顺序，单个池的物理 reservation 与子槽分开。
- 同文件 `:1692,2372,2477–2490`：应用 BITADDR8、map、链接步骤；必须在最终地址 relocation 前完成位号，并在布局前确定合成初始化表尺寸。
- `/home/liu/LLVM_STC32/MCS251/lld/MCS251/LinkerCore.h` 配套数据结构；新建 `/home/liu/LLVM_STC32/MCS251/lld/test/MCS251/bit-allocation.test`、`/home/liu/LLVM_STC32/MCS251/lld/test/MCS251/Inputs/bit-fixture.py`。

**可测验收/矩阵**：lit 0/7/8/127、8→9位跨 byte、无预留128位成功/第129位失败、预留1 byte后120位成功/121失败；两TU不同对象可共 byte 不共 bit，extern 合并一个槽，同名 internal 不合并。重复定义/未定义/COMMON/未知版本/能力/字段/坏长度/重复reloc/错目标/addend负例。老 CRT 16B owner与新 allocator不能重复预留，legacy BIT_BANK不与新普通位对象隐式 overlay。map包含 symbol→bit/byte/index/owner/初值/占用来源；PT_LOAD 不含非ALLOC元数据；text目标位字节精确。相同输入链接重现，交换TU顺序运行结果不变。端到端 BT12真实对象+BT14 CRT。

**明确不做**：耗尽 byte fallback、按调用图位 overlay、放宽普通 relocation 到位符号、重设计所有 RAM/stack allocator。

### BT14 — 位初值合成与 bit-aware CRT（L，lld/启动资产）

**目标**：零/非零初值都在 main/开放中断前生效，外部 owner/邻位不被旧 CRT 全清零破坏。

**前置**：BT00 新 asset/profile 和初始化线格式；BT13 reader/owner 账本；ROM 读取与旧 ISR CRT 检查资产交接。

**file:line 级改动点**：
- `/home/liu/LLVM_STC32/MCS251/lld/MCS251/LinkerCore.cpp:1879–1929,2015,2477–2490`：初始化目标 ownership 校验、表合成、CRT精确资产/兼容检查，不能把新启动字节假标旧 profile。
- `/home/liu/LLVM_STC32/MCS251/validation/mcs251-elf/runtime/crt-irq.yaml:58–74,107–145,177–194` 只读参照；后续新建 `/home/liu/LLVM_STC32/MCS251/validation/mcs251-elf/runtime/crt-bit.yaml` 和独立 checker。旧 IRQ/selfstart 资产不原位改写。
- 新建 `/home/liu/LLVM_STC32/MCS251/lld/test/MCS251/bit-init.test`；必要公共 profile 常量集中 BT00 的 MCS251Bit.h。

**可测验收/矩阵**：lit 每 byte 单一 mask/value、混合零/一跨TU初值、外部 owner无初始化、mask0无访问、maskFF直接值、部分 mask 保留邻位；冲突 XINIT/重复初始化/目的SFR/坏profile/旧CRT+新位协议负例。对象表端序、长度、地址/relocation与最终 BOOT 指令边界逐字节检查。端到端上电模拟先把 RAM 污染为 A5/5A，进入 main 时自有位等于初值，邻位/外部预留保持原值；不以 QEMU 默认零内存作初值证据。零初始化和ROM非零表读取都要实测；ISR在 main配置/使能之后看到正确共享位。

**明确不做**：通用 CODE load 门禁解除、自动 EA enable、清外设 pending、改37B保存、把所有 backing byte 无条件清零、旧资产身份复用。

## 6. 任务卡 BT15–BT17：门禁、集成、运行资格

### BT15 — 全入口负例与优化防绕过（L，验证；产品修复退属主）

**目标**：每个未支持用途确定失败，不能优化删除非法输入或降级成 byte 代码。

**前置**：分阶段依赖对应实现；S1→L1/sbit，S2→类型/ABI，S3→句柄/对象。

**file:line 级改动点**：
- `/home/liu/LLVM_STC32/MCS251/clang/lib/Sema/SemaType.cpp:1833,2077,10394`、`/home/liu/LLVM_STC32/MCS251/clang/lib/Sema/SemaExpr.cpp:4435,4745,15023` 只读门禁审计。
- `/home/liu/LLVM_STC32/MCS251/llvm/lib/CodeGen/MCS251ContractVerifier.cpp:359–375,924–992`、`/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCS251ISelLowering.cpp:1502–1536`、`/home/liu/LLVM_STC32/MCS251/llvm/lib/Target/MCS251/MCS251AsmPrinter.cpp:788–796` 三层边界回归。
- 新建 `/home/liu/LLVM_STC32/MCS251/clang/test/Sema/mcs251-bit-errors.c`、`/home/liu/LLVM_STC32/MCS251/llvm/test/CodeGen/MCS251/bit-contract-errors.ll`、`/home/liu/LLVM_STC32/MCS251/lld/test/MCS251/bit-protocol-errors.test`。

**可测验收/矩阵**：
- C：&bit、bit*/bit[]/字段/位域、typedef/typeof 链、sizeof/alignof、_Atomic(bit)、__atomic/__sync、memcpy、bit xdata（两处语料独立用例）、普通 AS5 declaration/cast/参数/返回、CPL结果使用、复杂RMW、动态位地址、受限SFR。
- IR/bitcode：纯声明/global-only、普通AS5 i1/i8 load/store/GEP、atomicrmw/cmpxchg/memcpy、嵌套聚合/ConstantExpr、句柄双用途/alias/ifunc、伪属性/错误签名、即使在死函数/恒假分支也先由目标入口拒绝。
- MIR/MC/lld：非法立即数、缺隐式状态、残留pseudo、符号用途逃逸、错 relocation、未知协议、容量耗尽、owner冲突；每个 mutation只改一因素。
- 同一输入在 -disable-verify、O0/O2/Os、asm/ELF 两输出与直接 llc 入口均失败；非零退出和诊断主体都有 CHECK，失败不留下可被误认成成功的新对象。普通 C/AS0/已有 AS/ISR 正例不被误杀。
- 端到端最小坏源绝不能链接得到普通 byte 替代实现。字节负断言不能只是 CHECK-NOT，必须同时有明确失败输出。

**明确不做**：修改优化器通用语义以迁就测试、移除旧 AS5 错误、把 unsupported/XFAIL记PASS、新LTO产品支持。

### BT16 — C→IR→ELF/lld 与语料切片验收（L，集成）

**目标**：把每层局部测试汇成真实编译链，给 S1/S2/S3 独立能力结论。

**前置**：对应切片属主 Alice review；完整结卡需 BT01–15。

**file:line 级改动点**：
- `/home/liu/LLVM_STC32/MCS251/validation/mcs251-models/proposals/BIT-FIRST-CLASS-INCREMENT.md:29–43,115–123`、`/home/liu/LLVM_STC32/MCS251/validation/mcs251-dialect/DIALECT-PACKAGE.md:13–24` 为只读统计锚点。
- `/home/liu/LLVM_STC32/MCS251/validation/mcs251-isr/compile-matrix.py` 为现有编译矩阵入口参照，不原位改写 ISR 资产。
- 新建 `/home/liu/LLVM_STC32/MCS251/validation/mcs251-bit/compile-matrix.py`、`firmware.c`、`helper.c`、`lit.cfg.py`（后三者完整目录同为 `/home/liu/LLVM_STC32/MCS251/validation/mcs251-bit`）。

**可测验收/矩阵**：O0/O1/O2/O3/Os×int32/int16×固定/符号×volatile/非volatile×首参/后槽/返回/自动spill；跨TU caller/callee交叉优化，至少一个含ISR共享位案例。首批显式使用已支持的 memory contract `1,2,32,8,1`；AS0=16及其他模型若未获得对象能力，保持已存在拒绝，不能把源码 layout v2误当ELF ABI v2已支持。每例保存IR、对象section/symbol/reloc、text字节、map、ELF/HEX、预期运行值。语料按解析、CodeGen、链接、可运行四级统计，解释剩余阻塞；307/94、118/38、29、90口径不混加，补9bit参数等合成盲区。正负 lit 全绿与旧 MCS251/Clang/lld/ISR 回归实际计数，历史68/68不当今天完整总数。端到端由真实C发对象，不用手写IR替代；字节ABI与位指令数量同时检查。

**明确不做**：为通过语料删除 xdata/interrupt/位语义、把语法命中当运行PASS、更新旧gold掩盖普通ABI变化、产品修复跨属主。

### BT17 — QEMU/实板位语义、初始化与竞争 qualification（L，验证）

**目标**：独立验证 ISA、真实编译镜像、共享位与调用私有值；分别给出模型和每块板的能力。

**前置**：控制 harness 可在 BT00 后准备；ISA 探针依赖 BT01/02，生产镜像依赖 BT16 对应切片。实板需 PM 明确设备/端口/烧录资产与授权。

**file:line 级改动点**：
- `/home/liu/LLVM_STC32/MCS251/validation/mcs251-models/proposals/DIALECT-FRONTEND-DESIGN.md:849–893` 的 DF-P00–P09/P15 为只读探针需求锚点。
- `/home/liu/LLVM_STC32/MCS251/validation/mcs251-isr/qualify.py`、`rsp.py`、`qtest.py`（后二者完整目录同为 `/home/liu/LLVM_STC32/MCS251/validation/mcs251-isr`）为已存在 transport 参照，不修改 QEMU/ISR 产品资产。
- 后续新建 `/home/liu/LLVM_STC32/MCS251/validation/mcs251-bit/qualify.py`、`qualification.test`、`board-results.json`（后三文件完整目录同为 `/home/liu/LLVM_STC32/MCS251/validation/mcs251-bit`）。实际运行日志在仓库外。

**可测验收/矩阵**：
1. lit self-test 验证 transport、超时/错误分类、独立字节/寄存器 oracle，不代替QEMU运行。
2. 先无待测位指令 control；测试期间不打印，先快照再输出。独立已验证byte路线初始化/观察RAM，避免同一待测指令既生成期望又验证自身。
3. RAM 128位逐位：SETB/CLR初值0/1、CPL两次恢复、MOV C,bit精确0/1及carry/PSW、JB/JNB/JBC两路径/前后向/短边界/长跳；邻bit/邻byte哨兵不变。危险SFR只静态编码，不为追求256地址覆盖去扫硬件。
4. SFR只测批准的GPIO/TCON/SCON子集，区分latch与引脚采样及SFR/RAM同数字地址；模型不支持外设不能“读0”算正确位语义。EA/PSW/ACC/B普通C接口负例保持。
5. S2：DPL全字节、后槽、跨调用/压力spill、自动局部嵌套副本；S3：跨TU packing、污染RAM后的零/非零初始化、mask保留邻位、ISR置共享volatile bit后主程序可见且不回滚。
6. 竞争正负对照：已认证ISR在同backing byte改变另一个bit；位指令路线保持两个更新；显式byte RMW在受控load/store之间注入可重现丢失。不能直接改PC到ISR冒充受理。模型支持时用真实指令边界注入；模型不支持精确时序记缺项，实板独立测，不推导DMA/NMI原子性。
7. 重复更新/跨调用/ISR组合至少10000次，计数、目标bit、邻位、SPX不漂移；有限超时与完成标记并用，不使用 icount，超时不是PASS。
8. 每例记录工具/QEMU/image/map/板型指纹、命令、字节、逐项快照与判定。G12/G144等每块实际可用板分别结案；无板 `BLOCKED_NO_BOARD`，模型缺项 `MODEL_UNSUPPORTED`，未执行 `NOT_RUN`，不混为PASS。静态层PASS不替代硬件资格。

**明确不做**：改QEMU让探针通过、无授权烧录、危险Flash/RSTCFG扫描、EA临界区API、L2外围RMW等价优化、全工程最大栈或重入安全认证。

## 7. 每任务测试责任总矩阵与整体 DoD

### 7.1 测试责任速查（所有新增测试均 [P]）

| 任务 | lit正例 | lit负例 | 端到端 | 字节/指令级硬断言 |
|---|---|---|---|---|
| BT00 | 协议golden审批 | 未知字段责任登记 | 工具/输入基线 | 字段偏移/长度/编号唯一 |
| BT01 | MIR→obj全族 | 范围/隐式效果 | BT17 ISA | 完整text与真实Size |
| BT02 | 近/远两边 | rel8超界/坏CFG | 路径哨兵 | PC-next+displacement，一次测试 |
| BT03 | intrinsic/优化effects | 签名/动态地址 | read物化 | carry依赖/无byte RMW |
| BT04 | L1强制表/PCH | 取址/复杂RMW | C→位指令 | RHS一次/不补读 |
| BT05 | sbit两种initializer | base/index/重声明 | 同builtin结果 | 不读P3，定位值正确 |
| BT06 | 头生成/预处理 | 受限/未知描述 | 安全SFR子集 | 无u8/AS5指针垫片 |
| BT07 | owner/别名共用 | 冲突/缺owner | RAM+普通数据 | backing整byte唯一计费 |
| BT08 | AST/typedef/PCH | 派生/布局/atomic | AST→IR分阶段 | 独立类型，不假global |
| BT09 | 转换/提升/短路 | 浮点/指针/持久RMW | 真值表 | 非零比较，不trunc奇偶 |
| BT10 | ABI/多参数/跨TU | 签名/varargs/间接多参 | caller/callee | DPL/槽完整0或1 |
| BT11 | SSA/局部/压力 | 源布局/取址 | 嵌套/ISR副本 | 1B spill，无BSEG |
| BT12 | IR/对象往返 | 句柄逃逸/错reloc | 真C两TU对象 | 非ALLOC记录/精确symbol |
| BT13 | packing/128位 | 129位/owner/格式 | 真实对象链接 | 位字段/占用/map/PT_LOAD |
| BT14 | mask/初值 | 旧CRT/冲突XINIT | 污染RAM启动 | mask外保留/表与BOOT |
| BT15 | 普通能力回归 | 全入口/关闭verify | 不产成功对象 | 无静默byte fallback |
| BT16 | 全优化/语料分组 | 预期拒绝与剩余阻塞 | C→ELF/HEX | ABI+指令+reloc联合 |
| BT17 | harness self-test | timeout/control污染 | QEMU/逐板 | 寄存器/邻位/编码快照 |

### 7.2 后续测试执行方法

PM 先选择同一源码快照构建的 clang、llc、opt、llvm-as/dis、llvm-readobj、llvm-objcopy、yaml2obj、FileCheck 与 mcs251-lld；执行者记录各工具绝对路径。不混用旧构建树 lld 和新头生成的 writer。

- Clang：`%clang_cc1 -triple mcs251-unknown-none -std=c11`，按用例附 `-fmcs251-keil`、显式 memory contract、`-target-feature +int16`；Sema 用 `-verify`，CodeGen 分未优化IR与优化IR。
- LLVM：llvm-as/dis 往返；opt分结构/优化门禁；llc用 `-verify-machineinstrs`、显式目标/contract和 `-filetype=obj -mcs251-object-format=elf`。MIR选择与卡对应的真实pipeline入口并写出隐式操作数，不硬抄ISR旧RUN。
- MC字节：llvm-objcopy dump .text，独立oracle核整段字节/reloc/目的地址；不要求不存在的AsmParser。YAML专测reader畸形输入，不替代Clang writer正例。
- lld：明确HOME/BOOT/CSEG/初始化表的已批准区域、ROM/IRAM/stack与owner配置；普通链接与 --print-input 分开，后者不等于完整分配验证。
- lit运行构建树中对应的Clang/LLVM/lld目标套件；QEMU/串口qualification显式运行，不放入默认lit自动接触设备。

### 7.3 整体 Definition of Done

完整 bit/sbit 支持只有同时满足以下条件才可关闭本役；S1/S2单独发布必须注明尚未支持集合。

- [ ] BT00 相关决策、线格式、唯一属主已签收，无实现者自选的协议值。
- [ ] BT01–14每卡Alice review；L1+sbit、独立类型/转换/ABI/自动值、持久位分配/初始化全部接通。
- [ ] BT15全部正负测试有实际命令/退出码；普通AS5禁令、取址/派生/atomic/bit xdata未被解除。
- [ ] O0/O1/O2/O3/Os与int16/int32规定矩阵通过；volatile次数/顺序不变，复杂受控RMW明确拒绝。
- [ ] DPL与静态槽规范化0/1、源参数序号、跨TU身份一致；不宣称静态槽重入或第三方ABI兼容。
- [ ] 专用relocation/位0/127/128容量/129失败、跨TUpacking、extern/internal/重复定义/未知协议全部验收。
- [ ] BSEG池只有一次物理占用；固定owner/legacy overlay/普通RAM/stack互斥；map可追溯每一位。
- [ ] 零/非零初值在污染RAM条件下正确，CRT不覆盖外部owner或mask外邻位；新旧profile混用明确失败。
- [ ] QEMU支持的必测项实际通过；每个拟发布硬件profile的必需指令/初始化/竞争子集有实板证据。无板可记“静态/模型完成，实板阻塞”，不能勾完整硬件DoD。
- [ ] 语料分组给出实际可编译/可链接/可运行比例，2处bit xdata拒绝不是回归；不冒称94文件全解锁。
- [ ] 旧普通CC、ELF/REL、AS门禁、ISR保存/RETI、RAM/stack与已有测试无未解释回归；新增测试与旧基线分别计数。
- [ ] 未实现/模型缺项、实板未跑、ABI不兼容、volatile非事务原子与用户同步责任随发布保留。

## 8. 需用户/PM拍板事项（与实现细节分开）

用户要求的是规划，本轮不阻塞等待问答。下列推荐不是冒称用户已批准；BT00按切片签收后执行。

> **2026-09-11 更新**：用户已对本表 P01–P10 全部"同意推荐"拍板，含 P08 的 Option B 范围修订登记。逐项裁定原文见同目录 `BIT-DECISION-20260911.md`。

| 编号 | 需决定什么 | Alice推荐 | 阻塞 |
|---|---|---|---|
| P01 | bit值ABI最终形态 | DPL完整0/1；后续原序号1B静态槽；返回DPL；无carry/bits银行、无裸混链 | BT10/S2 |
| P02 | volatile与非volatile语义 | L1/sbit隐含volatile；普通bit按声明；首版保守effects可不消除非volatile持久访问，ISR共享由用户显式volatile/同步 | BT03/04/S1 |
| P03 | sbit“原样透明”的范围 | 支持本役两种旧式声明及正常读写/条件/认可toggle；允许替换为薄兼容头；**不承诺未经改动的整个Keil官方头** | BT05/06/S1 |
| P04 | 管理寄存器/EA是否开放 | 普通接口首期拒绝；EA未来专用save/restore，其他按精确寄存器效果另案。若用户要求EA原样sbit，必须独立扩卡，不能偷偷放行 | S1兼容范围 |
| P05 | 位槽耗尽与局部packing | 128位扣除预留后耗尽链接硬错；自动局部只SSA/byte spill，不fallback/overlay | BT11/13 |
| P06 | CRT池所有权与旧资产兼容 | 新bit-aware CRT/profile、lld单次池预留+子分配、mask/value初始化；旧CRT与新受控位协议拒绝混用，S1固定owned全清零子集可先验 | BT07/13/14/S3 |
| P07 | 对象协议/ABI标识发布方式 | 保持现有ELF身份与ISR编号；新增版本化精确能力记录、位/函数签名；旧reader拒绝未知节/reloc。若兼容检查证明必须升ABI版本，转PM另案，不顺手建设通用v2 | BT00/12/13 |
| P08 | 支持的C语言形态 | __bit为核心，裸bit/sbit沿用-fmcs251-keil；首期C，排除C++/varargs/无原型bit调用/COMMON/weak；函数指针含单bit值签名允许 | BT05/08/10 |
| P09 | 位符号句柄表示 | 推荐无AS5的受验证AS0身份占位+专用consumer/记录；先验证优化保活/防逃逸。正式内建名/编号/记录字节由PM登记、Alice审查 | BT00/03/12 |
| P10 | 验收板型与放行证据门槛 | PM明确首批板和安全SFR清单；模型与逐板分别PASS；无板只完成静态/模型层，不宣称硬件原子保证 | BT01/06/17与生产发布 |

工程惯例无需再问：REL不承载新能力、普通AS5保持拒绝、没有AsmParser就用MIR/对象测试、未知协议fail-closed、测试输出放仓库外、共享文件串行。

## 9. 三稿冲突、需修订项与本轮新发现

这里只记录本任务书采用的修订，**本次不修改三份旧稿**。

| 来源及旧条款 | 冲突/过时点 | 本任务书处理 |
|---|---|---|
| `/home/liu/LLVM_STC32/MCS251/validation/mcs251-dialect/DIALECT-PACKAGE.md:164–169,183–201` | u8宏/byte-mask过渡、普通i1 load/store、global/local都BSEG，容易被当真语义 | 自我修订：本役不交u8过渡；固定/持久位用专用intrinsic；自动值SSA/1B spill；跨TU由lld，不由FrameLowering分局部位 |
| 同稿 `:53,71,93–108` | 建议p5与MCS251_BIT AS5宏、TargetDataLayout.cpp/旧Python linker落点 | 不增p5/普通AS5；实际布局在MCS251TargetParser.cpp，生产链接在lld/MCS251/LinkerCore.cpp |
| 同稿 `:335–337` | “bit不走普通i16 promotion”易与一等类型增量冲突，且曾写int默认16 | bit正常提升到当前C int；现默认32、+int16为16，普通_Bool/_BitInt不改 |
| `/home/liu/LLVM_STC32/MCS251/validation/mcs251-models/proposals/DIALECT-FRONTEND-DESIGN.md:519–536,612,635,961,987` | L1固定常量、无旧式sbit解析；allocator/bit参数返回后置 | L1第一切片保留常量；BT05显式扩旧式声明范围；S2开放bit值ABI但不开放位引用ABI；S3增加符号句柄、allocator、relocation |
| 同稿 `:610–620` 与 `/home/liu/LLVM_STC32/MCS251/validation/mcs251-models/proposals/BIT-FIRST-CLASS-INCREMENT.md:18,68,111` | L1一切操作不删除与普通bit按声明volatile的边界不够细 | 固定volatile强保证；普通bit源限定不变，首版允许保守effects；不承诺非volatile对ISR可见；持久RMW小保证集与自动值普通运算分离 |
| 前端稿 `:123–159,402–404` | 旧时点AS忽略/verifier未接入/global拒绝叙述不能当今天状态 | 现有checkDataAddressSpace/contract入口及AS0 global DSEG/XINIT已存在；本役是在fail-closed地基增受控路线，不重复DF0整个战役 |
| 前端稿 `:768–771` | 旧ISR闭包静态槽禁令已与最新最小ISR方向不符 | 不恢复已撤销安全分析；静态槽异步重入由用户负责，全局bit不进37B保存 |
| bit增量 `:59–67` 与现CRT | “复用BSEG账本/不覆盖预留”未解释CRT已经占满且全清16B | 必须区分物理池预留与bit子分配；BT07固定owner，BT14新profile/mask初始化，防双预留和启动覆盖 |
| bit增量 `:81–94` 与当前Clang | 推荐i8 ABI但代码实际走DefaultABIInfo；只改td不够 | BT10新增target ABI分类入口，显式C签名i8及归一化，保持非bit默认行为 |
| 用户统计与早稿收益表 | 94/275联合命中不等于sbit-only，更不等于全文件可执行 | 接受优先顺序，修正收益承诺；BT16提供分组实际漏斗 |
| 本轮源码新发现 | CallingConv.td多参注释过时；分支松弛假设operand0；size还有C++独立表；relocation9被ISR占用 | BT10修注释；BT01/02显式适配两张size/branch表；BT00新号登记，不撞ISR |
| ISR模板历史环境/测试 | Windows路径、旧RUN/AS0与AS4保活修订不能原样套用 | 只借卡片体例/审查纪律；本役按当前绝对根路径、显式contract和实际pipeline入口设计测试 |

进一步澄清：sbit兼容parser只在initializer里解释定位 `^`，既不是普通C重载，也不是读一个SFR后计算XOR。只有字节SFR垫片不可能独自提供这种声明透明性。源类型不可取址与内部身份句柄可被LLVM consumer引用是两回事，必须有明确防逃逸校验。

## 10. 明确不做总清单

- 不写实现代码、不运行构建/产品测试、不修改既有文件、不git add/commit（本次规划交付）。
- 不把bit/sbit降级为u8/_Bool宏；不以byte-mask RMW替代L1保证。
- 不开放普通AS5指针/global/load/store/GEP/cast/参数/返回；不新增p5 pointer ABI。
- 不支持bit指针/数组/聚合字段/位域/布局查询、_Atomic/LLVM原子对象、memcpy位存储、浮点/指针↔bit、bit xdata。
- 不实现自动局部物理packing、全局传参位银行、carry返回ABI、静态槽自动重入或Keil/SDCC二进制混链。
- 不实现L2普通byte RMW折叠、公共JBC test-and-clear、MOV bit,C主路线、EA临界区/priority策略、自动EA/外设使能或pending清理。
- 不把ACC/B/PSW等编译器管理寄存器位暴露为普通sbit，不因编码可达就授权危险SFR。
- 不做完整Keil官方头原样透明、sfr/sfr16/sfr32/far语法全集、C++方言、寄存器名字全集。
- 不做通用AsmParser/disassembler、REL新协议、archive懒提取、LTO/GC/ICF/weak/COMDAT/COMMON支持、一般ELF v2或全部内存模型底座重构。
- 不重设计普通CC/ISR37B/RETI/栈，尤其不保存恢复全局位RAM抹掉ISR通信。
- 不以QEMU未建模外设、上电全零、超时、手动跳PC或读零结果伪造运行/原子性证据；不做未授权烧录、危险Flash配置、NMI/DMA或全工程安全认证。

**最终建议：先交可审计的L1+sbit纵切，再交一等值ABI，最后交跨TU位池与初始化。每个切片都带负测和真实C→对象闭环，不让“能解析”“能打印位助记符”“16B已预留”中的任何一项替代完整语义验收。**
