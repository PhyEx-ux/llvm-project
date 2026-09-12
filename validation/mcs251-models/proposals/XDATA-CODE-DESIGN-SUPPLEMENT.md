# XDATA-CODE 设计补充稿：DPXL 管理协议与 MOVX 全 24 位序列（X2-1/X2-2）

**日期**：2026-09-12。**状态**：随 X2 修复实现；正文为冻结补充，改动需新裁定。X2-4 修订（2026-09-12，Alice 复审定稿）改写 §3 的中断保持义务，其余正文不动。X3 增补（2026-09-12，随 X3 修复落地）新增 §7 放置链接裁定（§1-§6 不动）。
**上位文档**：DESIGN.md B.2（AS3=`__xdata` 32/8，"保持完整 24 位有效地址"）、XDATA-CODE-SLICE-TASK.md §3 X3 需求补充⑥。

## 1. 裁定背景

X2 首版采用"phase-1 16 位窗口"实现：MOVX @DPTR 只覆盖 DPXL 复位值 01h 指向的单个 64K 区域，常量地址越窗即拒绝（rc=-6）、运行期指针只取低 16 位。Alice 复审给出反例（`p+65536` 生成 `add wr0,#0x0000` 后 MOVX，bank 静默回绕；常量 0x11234/0x21234 被拒），与冻结契约 B.2 冲突。协调员裁定方向 (a)：**完整 24 位语义**——每条 AS3 MOVX 访问序列显式管理 DPXL。

## 2. 生成的序列（MCS251ISelLowering.cpp，`buildXDATAAddress`/`splitXDATAAddress`/`buildMOVXByteLoad`/`buildMOVXByteStore`）

每个访问字节：

```
mov  rN, #bank      ; 常量 bank：编译期折叠（MOV8ri）
mov  0x84, rN       ; MOV8dpxl：region 写（sdas251 gold 7A 21 84）
mov  dpl/dph, ...   ; 窗口偏移 = 位 [15:0]（MOV8dpl/MOV8dph）
movx a,@dptr / movx @dptr,a
```

- 常量地址：bank 与窗口双双折叠；**bank==01h 也照样发射**（裁定：首期不省略；省略需全函数分析 pass，留待后续）。
- 运行期指针：任何常量偏移以**完整 32 位加法**折入指针（绝不走 i16 加——进位属于 bank 字节）；bank = 结果位 [23:16]（DR 高半字低字节），窗口 = 位 [15:0]。位 [31:24] 污染不进入 DPXL。
- 全局/外部符号：走既有 byte-of-24 重定位通道（MOVADDR32，与 AS9 far 全局同形），bank 字节来自链接器解析的 24 位地址（`(_sym) >> 16` 重定位）——落实 X3 裁定①"不得直接把 XSEG 地址截成低 16 位"。
- i16/i32 对象按字节展开，**每字节独立**计算 bank+窗口（对象跨 bank 边界时逐字节换 bank，不再有跨界拒绝）。

## 3. DPXL 保持协议（X3 需求⑥成文；X2-4 修订中断保持义务）

**核心性质（不变）：后端生成的代码对 DPXL 无保持依赖（自愈式）。** 每条 AS3 访问在 movx 前一步重设 DPXL。

**异步抢占：ISR 及嵌套 ISR 必须保存恢复 DPXL（X2-4 修订，Alice 定稿）。** 首版"ISR 无需保存 DPXL"为错误结论。反例（interrupt-trace.txt）：主程序 MOVX 序列中点（region 已写 01h）被抢占 → ISR 内 AS3 访问重设 DPXL=02h 且不恢复 → 返回后主程序同一序列的 MOVX 命中 02:1234 而非 01:1234（错 bank）。"返回后下一条访问自愈"只覆盖序列边界，覆盖不了序列中点的窗口。因此：

- 编译器生成的 ISR（含嵌套 ISR 的每一层帧）**必须保存并恢复 DPXL**。
- 现有 A6 固定帧已物理覆盖：`push dpx`（FrameSetup 末位）/`pop dpx`（FrameDestroy 首位）存取 24 位 DPX 含 region 字节（证据：irq.s:19-28）。**该保证不得删除。**
- 建模同步（X2-4）：ISR_PUSH_DPX `Uses` 补 DPXL、ISR_POP_DPX `Defs` 补 DPXL（MCS251InstrInfo.td），ISR 入口块 async live-ins 补 DPXL（MCS251FrameLowering.cpp）——调度/优化由此知道 ISR 帧触及 DPXL，帧操作不得被优化删除；回归测试 `xdata-isr-window.ll` 钉住。

三类场景分层陈述（义务不同，不得混谈）：

| 场景 | DPXL 义务 | 依据 |
|---|---|---|
| CRT 启动 | 无义务：生成代码不依赖复位值 01h，无置初值动作 | 自愈式 |
| 普通调用边界（未来 call 战役） | 被调方可任意 clobber，**不纳入 callee-saved**：调用点后第一条 AS3 访问自己重设 region | 自愈式 |
| 异步抢占（ISR/嵌套 ISR） | **必须保存恢复**：编译器生成帧 push/pop dpx 覆盖 DPXL | 上文反例 |

用户 AS6 写 SFR 0x84 与生成序列的交错仍为文档化未定义交互（§5），但该未定义性**不豁免编译器的 ISR 保存恢复义务**。

序内正确性由指令描述符钉住：MOV8dpxl `Defs=[DPXL], hasSideEffects=1`；MOVXALD/MOVXAST `Uses=[DPL,DPH,DPXL]`。效果：(1) 调度器把 region 写当作 movx 的依赖，两条独立 AS3 链的 bank 写不可能跨越对方 movx；(2) hasSideEffects 禁止 MachineCSE 合并相邻同 bank 两次 region 写——合并会把一次 region 写摊到两条 movx 上，序列中点窗口内任何未恢复的 DPXL 改写（手写 ISR 体、用户 SFR 写；编译器生成 ISR 帧已恢复）都会使第二条 movx 错 bank，测试 `xdata-dead-value.ll::twice` 钉住。

## 4. AS9 口径更正（声称更正项）

修复方报告曾把 "AS9 load/store rc=0" 列入**保持拒绝**清单，属口径错误。按冻结 far 设计（DESIGN.md B.2：AS9=`__far` generic，32/8 canonical 24 位数据地址容器；§403：AS0 far/AS9 走 canonical DR 24 位有效地址访问），AS9 load/store 经通用 DR 通道放行且 rc=0 是**正确行为**。更正后清单：

- **far 设计放行**（generic DR 通道，`mov r,@dr`）：AS0(4B)/AS4 load/AS9。
- **保持拒绝**：AS4 store（CODE 只读）、AS5（bit 空间，仅受控位左值机制）、AS7（保留）、其余未编号 AS。
- 仓库内无测试把 AS9 记作拒绝（`checkDataAddressSpace` 实现集 `{0,1,2,3,4,6,8,9}` 与 B.2 一致）；本节为口径的唯一更正记录。

## 5. 遗留边界

- 用户内联/手写对 0x84 的写与后端 AS3 访问的交错为未定义交互（§3）；后端从不在自身序列中间插入他方代码。
- XDATA 访问的**动态可达性**（指针可能指向未分配单元）由后端语义负责（按字节寻址如实生成），放置/重叠检查仍属 X3 链接器契约。
- 尺寸代价（实测，sdas251 汇编字节计）：常量 bank 访问每字节 +2 指令 +6 字节（`mov r,#bank`+`mov 0x84,r`）；运行期无偏移访问每字节 +1 指令 +3 字节（bank 即指针 lane）；运行期带常量偏移另加 DR 常量装载 + `add dr,dr`（+4~7 指令）。Alice 反例 `_runtime`（volatile i8 @p+65536）整函数 9→14 指令、22→37 字节（含旧版被截断而省去的 2 条 ABI lane 装载）。
- 新增指令：MOV8dpxl（编码锚 `xdata-code-bytes.mir::dpxl.mir`）；新保留寄存器 DPXL（HWEncoding 占位 60，bits<6> 上限内，不入任何分配类）。

## 6. 表外调用的 CODE 写边界（首期声明，X1-7 裁定；X1-8/X1-9/X1-10 修订，2026-09-11）

本节为 X1-7 裁定新增，**首期边界声明，不是永久方案**。r7 自修订（2026-09-11）撤回“有且只有三个权威/按构造完备”的结论，保留三层责任边界。下面记录已审计入口、测试配置和排除依据，不宣称对所有语言、运行时及未来 builtin 的穷尽证明。

**成文边界**：凡 `SemaMCS251.cpp` `MCS251BuiltinWriteTable` 之外的调用——无写行的 builtin 与一切非 builtin 外部函数（X1-8 修订：原"非 builtin 外部调用"标题以偏概全）——若实参为 `__code` 指针，即成立 C-const 式契约，**被调方不得经该指针写**。三层各管各的，不得混谈：

**已审计入口与配置（r7，2026-09-11）**：审计输入包括 Builtins.td/目标注册记录、SemaChecking 的自定义签名检查、CGBuiltin 的自定义发射及其下游助手。类型串只描述接口，不完整描述写效果；CustomTypeChecking/IgnoreSignature 占位串不能证明无指针参数。此前的标签/分组计数仅为索引（含嵌套 switch），不是独立 handler 数或覆盖证明。无写行也不证明无写效果；已知固定槽位遗漏须修复，不能用契约层豁免。

- **配置范围**：MCS251 C11/C23（现代限定符、Keil 方言）、可选 MS 扩展/矩阵类型，以及 OpenCL C 1.2/2.0/3.0 的 half 读写对照。OpenCL C 实际可选，`store_half/store_halff` 第 1 参入写位表；不得以 OCL 注册条件推导目标不可达。C++ 保持关键词未注册，host 不启用 MCS251 CODE 检查。这里不承诺完整 OpenCL/HLSL/CUDA/ObjC 应用或运行时支持。half 读允许通过 Sema 并生成有效的未优化 IR，但后端 half→f32 转换尚可能按浮点能力契约拒绝；zos_va_copy 的 i64 长度 intrinsic 也仍受已有后端限制。这些后端限制不作为 CODE 写检查的替代。
- **libcall 发射**：EmitBuiltinExpr 的 libcall 回退与普通 EmitCall 路径已核对。已建模固定目的在 Sema 拒绝 AS4 写；普通外调及变参/分配器状态按下表契约层处理。外部函数体与链接库效果不由 switch 枚举证明。
- **intrinsic 映射**：核对 EmitBuiltinExpr 的 Clang/MS builtin 到 intrinsic 映射、实参转换及目标派发。MCS251 当前架构发射器走 default 返回空；这只排除相应架构助手，不排除通用或语言选定的 emitter。新增目标 intrinsic 映射须重新审计。
- **ObjC 委派**：`objc_memmove_collectable` 为全语言 builtin。MCS251 非 GC 模式按 memmove 语义直接发射保留双方 AS 的 LLVM memmove，不经只接受 AS0 指针的 ObjC 运行时；CODE 仅作源合法，CODE 目的在 Sema 拒绝。内置仍返回声明中的 AS0 `void *`，非默认目的地址的返回转换在 IR 显式使用 addrspacecast。GC 模式不采用该替代，调用明确报告不支持；其它目标仍走原 ObjC runtime。这不是新增 GC ABI。
- **CGAtomic 委派**：AtomicExpr 经 CGExprScalar 进入 CGAtomic，不在 CGBuiltin case 计数内。写表分别检查对象、load/exchange 输出和 compare-exchange expected 回写；GNU/C11/OpenCL/scoped/HIP 家族按实际参数位置复核。纯 load_n、fence/查询不写用户目的。
- **其他助手与边界**：memcpy/memmove 展开、zos 生命周期、MS 原子助手、os_log 及向量/矩阵存储纳入已审计调用链；语言运行时、协程帧、GC、目标专用 emitter 不能仅凭名字归为纯读。未测试语言/运行时配置不在本轮覆盖声明中。新 builtin、LangOpt 或发射助手变更必须重新核查注册、Sema、下游写效果并增加读写正负例。

| 层 | 覆盖 | 手段 |
|---|---|---|
| 源级写检查 | 已建模固定写槽位的 builtin（含 X1-11 half 写） | Sema 诊断，调用点拒绝；发现遗漏须补测试和模型 |
| 契约层（本节） | 表外一切调用（变参写、流句柄、分配器域、外部 callee） | 无诊断；被调方违约即用户责任 |
| 后端访存检查 | 本仓 IR 中保留 AS4 类型的 store | ISel 拒绝（fatal） |

- **审计轨迹**：r1 发现目标 gate、CODE 写入口、post-`*` 与字符串表缺口；r2/r3 扩展写内置及多目的参数；r4/r5 修正占位类型串和扩展门控误判；r6 验证 zos 修复并发现 OpenCL half 写及 ObjC 源 AS 委派缺口；r7 自修并进行正负回归。写位表结合注册、Sema 和实际发射路径维护，纯读对照必须同时检查不误拒及 IR 有效性，而非仅看语法返回码。
- **无固定槽位的写**（scanf 族变参槽、fprintf/fopen 流句柄、malloc/free/strdup 分配器域）：不建模，归入契约——被调方写即违约。
- **契约违约的后果（X1-8 修订：收窄）**：只有当违约写**保留 AS4 类型、未显式转换掉地址空间**的路径才落为 ISel 可见的 AS4 store 并被拒绝（fatal）；显式转到通用空间再写或手写汇编被调方写 CODE，后端不设防，属用户责任。
- **与 §3 ISR DPXL 义务的关系**：C-const 契约约束"谁可以写 CODE 对象"；§3 的 ISR 保存恢复 DPXL 义务约束"窗口寄存器跨异步抢占的保持"。两者对象不同，互不推导、互不豁免、互不冲突。

后续若引入全程序写效果分析或 ABI 级 AS4 写检查，本节由新裁定替换。

## 7. X3 放置链接裁定（2026-09-12，随 X3 修复冻结；DPXL 保存协议见 §3，此处引用不重写）

### 7.1 三层职责表：canonical 地址 / DPXL 区域(bank) / 窗口偏移 / 链接期通道

| 层 | 职责 | 依据 |
|---|---|---|
| canonical 24 位地址（位 [23:0]） | 对象在 XDATA/CODE 空间的唯一地址语义；AS3/AS4 指针的 32/8 容器低 24 位即此值 | DESIGN.md B.2 |
| DPXL 区域(bank) = canonical 位 [23:16] | 运行时窗口基；每条 AS3 访问序列显式装载（自愈式，无保持依赖） | §2/§3 |
| 窗口偏移 = canonical 位 [15:0] | DPH:DPL 形态的 16 位窗口内偏移 | §2 |
| 链接期 24 位通道 | R_MCS251_24 与 HI8/MID8/LO8 写 canonical 值；R_MCS251_16/J16 命中 XSEG 定义符号 = 硬错误（截断门禁，无逃生开关） | X3 门禁 |
| 指针初值容器 | 大端 4 字节：字面零的最高有效字节（位 [31:24]，不属于有效地址）在容器偏移 0；R_MCS251_24 字段在偏移 1..3（bank 在 1，窗口大端在 2..3）。链接后符号 0x011234 序列化为 `00 01 12 34`，装载端重组出 0x011234，绝不是 `01 12 34 00`（后者装载出 DPXL=0x12、窗口=0x3400，属错编译，X3-R2 已修） | X3-R2 裁定 |

### 7.2 `--xdata-size` 与 `--area-start` 的关系

- 缺省（不设）：只有常开的 24 位 rangeFits，向后兼容旧布局。
- 设 N：每个**已分配** XSEG 区间须完整落入 `[area-start(XSEG), area-start(XSEG)+N)`，64 位算术、逐节检查，错误信息带对象名与区间。
- 容量从 `area-start(XSEG)` 起算，**跳 bank 留下的洞计入**（见 7.4）；洞同样计入 `l_XSEG` 跨度。
- 存在 XSEG 节时 `--area-start=XSEG` 必填；逐节 `--area-start=.mcs251.XSEG.<名>=ADDR` 优先于顺序游标，且显式起点不得使节跨 64K 窗（硬错误）。

### 7.3 xdata_init v1 记录格式冻结表（七字节头 + 载荷，全大端）

| 字段 | 偏移 | 宽度 | 字节序 | 语义 |
|---|---:|---|---|---|
| bank | 0 | u8 | — | canonical 位 [23:16]，正是 DPXL 要装载的值 |
| window | 1 | u16 | 大端 | canonical 位 [15:0]（与 XINIT v1 的 u16 字节序一致） |
| object_size | 3 | u16 | 大端 | 对象大小（1..65535） |
| payload_size | 5 | u16 | 大端 | 0 = 仅清零；非 0 必须等于 object_size |
| payload | 7 | payload_size | 目标端字节图像 | 指针叶子按 7.1 的容器规则 |

链接种裁：目的区间 `[bank:window, +object_size)` 必须 24 位内、单 64K 窗、完整落入一个已分配 XSEG 区间（含末字节）、记录间目的不得重叠；节名匹配 `.mcs251.xdata_init` 或 `.mcs251.xdata_init.` 前缀。链接器只放置/解析/校验，不合成字节（CRT 消费循环属 X4）。

### 7.4 XSEG 单对象 65535 上限、64K 跳 bank、洞计入 l_XSEG

- 单对象上限 65535 字节：u16 记录字段所限，且 XSEG 对象不跨 64K 窗（单 bank 记录描述不了跨界对象）；越限 = 硬错误。
- 顺序分配不跨界：游标到窗口末尾剩余不足整节时，跳到下一 bank 起点（64K 对齐）再分配。
- 跳 bank 留下的洞计入 `l_XSEG` 跨度（`l_XSEG` = 最高已分配字节上界 − `s_XSEG`），不进 DSEG、不动栈高水位；同样计入 `--xdata-size` 容量（7.2）。
- 双保险：手造跨窗的 xdata_init 记录、显式跨窗的逐节起点，均为硬错误。

### 7.5 指针初值链接门禁与 one-past 裁定（X3-R3；通道口径由 X3-R8 修订）

- **通道识别**（与普通代码 R_MCS251_24 区分，不得无差别当数据指针；**X3-R8 修订：按目标最终归属判定，不看被引用符号的 STT 类型**）：R_MCS251_24 的目标经解析（同文件直引、同址别名、跨 TU 全局符号表解析、节符号折叠）后**定义节为 XSEG**，即属存储指针通道。指针初值容器的来源节性质有三种：XINIT / XDATA_INIT 记录载荷（只含记录、无代码）、CSEG 只读数据图像（.rodata 类不可执行 ALLOC PROGBITS），以及后端实际产生的形态——MCS251AsmPrinter 把只读全局（含 `__code` 图像与指针容器）发射进 `.text`（可执行只读图像），NOTYPE 目标在该形态下曾被整体跳过校验（X3-R8 缺陷）。普通代码 R_MCS251_24（直接调用/EJMP/跳转表槽）的目标是 STT_FUNC/代码节符号，**永不解析到 XSEG 节**（XSEG 为 NOBITS 数据，16 位通道对 XSEG 符号已是硬错误），故不受门禁影响，维持既有语义（仅 24 位范围检查）；X2 代码地址装载用 HI8/MID8/LO8 字节通道，同样维持。
- **门禁**：目标为 XSEG 定义符号的存储指针，最终值（符号地址 + addend）必须落入 `[所在 XSEG 区间起点, 区间终点+1]`。错 bank（负 addend 溢出对象）、越过对象均为硬错误，无逃生开关。判定字段是**目标定义节的 Region**，不使用 STT 类型；SHN_ABS 符号与合成边界符号（`s_XSEG` 等，无定义节）不属本门禁管辖——canonical XSEG 地址与 CSEG 代码地址共享同一 24 位数值范围（bank 0 XDATA 窗口与小容量 flash 代码可数值重叠），仅凭最终地址落点无法区分合法 CODE 函数指针与越权 XDATA 指针，故不采用"最终地址落入分配账本区间"作为独立触发条件。
- **one-past 裁定：放行**。指向对象末尾后一字节（值 == 区间终点+1）的指针是合法值——C 语义的循环哨兵；不可解引用但可持有与比较。低于起点或高于 one-past 一律拒绝。该裁定由 lld 测试（正例 `ptrpast`、负例 `wrongptr`/`ptrbeyond`；X3-R8 后另负例 `roptr-notype`/`roptr-notype-alias`/跨 TU `notype-use+notype-def`，正例 `notype-use-ok`）与本节共同固定。
