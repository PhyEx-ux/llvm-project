# X3 放置链接实施任务书（XDATA/CODE 切片第三片）

**日期**：2026-09-12。**上游任务书**：`XDATA-CODE-SLICE-TASK.md` X3 行 + Alice X2 审查六点补充（任务书 §3 引文）。
**门禁**：实施完成 → 抽验 → Alice Review APPROVED → 提交。git 身份只用 `PhyEx-ux <1073584672@qq.com>`。

## 0. 现状实测（2026-09-12 调查，实施前复核一遍）

X1（7df7fcbf6）/X2（e04872b1a）已落库。**当前缺口**：

1. `MCS251TargetObjectFile::SelectSectionForGlobal`（`llvm/lib/Target/MCS251/MCS251TargetObjectFile.cpp:26`）与 AsmPrinter 自定义全局发射器（`MCS251AsmPrinter.cpp` 约 1140-1190 行）**完全没有地址空间意识**：所有 IR 可变全局（含 AS3、AS4）走 DSEG 可变路径（`.mcs251.dseg` NOBITS + XINIT v1 记录）。
2. X1 实测 IR：`@DEVICEDESC = addrspace(4) global [18 x i8] c"..."`——**AS4 全局是 IR 可变全局**（TR18037 地址空间不蕴含 const；X1 的只读纪律在 Sema 层）。因此 `char code DEVICEDESC[18]` 今天会落进内 RAM DSEG，而非 CODE ROM。
3. X2 装载假定 AS3 全局有 24 位 canonical 地址（MOVADDR32 + hi8/mid8/lo8 通道，`MCS251ISelLowering.cpp:2299`），但 AS3 全局实际被放进 16 位 DSEG——后端语义与放置互相矛盾，X3 必须闭合。
4. lld 已有：`.mcs251.XSEG*`（可写 NOBITS）→ Region XSEG；顺序分配 + 24 位 rangeFits 溢出 + 重叠检查 + `--area-start=XSEG` 必填 + `s_XSEG/l_XSEG` 边界符号（`lld/MCS251/LinkerCore.cpp:2124-2152, 2244-2260`）。**缺**：容量配置来源、末字节/跨界/错 bank/截断门禁测试、XDATA 初始化记录通道、指针初值检查。
5. XINIT v1 记录格式（DSEG 专用）：`u16 地址(大端), u16 对象大小, u16 载荷大小, 载荷`（lld `validateXInit()` 与 bit 合成路径 `LinkerCore.cpp:1857-1864` 均按大端写/读；以 lld 解析代码为冻结真值）。XINIT 载荷中目前**只有整数**——`isSupportedMutableInitializer`（`MCS251AsmPrinter.cpp:421`）拒绝指针初值；RO 路径同样拒绝"initializer relocations (pointer tables)"（:484 注释）。
6. lld 驱动现有选项先例：`--iram-size`、`--stack-size`、`--area-start=AREA=ADDR`（`Driver.cpp:390-455`）。

## 1. 放置规则（冻结裁定）

| 全局形态 | 放置 | 初始化图像 |
|---|---|---|
| AS3 定义（`__xdata`，含 `const __xdata`） | `.mcs251.XSEG`（NOBITS，ALLOC\|WRITE） | 新 `.mcs251.xdata_init` 记录（见 §2） |
| AS3 extern-only TU | 不发射 | 无 |
| AS4 定义（`__code`，IR 可变） | CSEG 只读路径（PROGBITS 在 CODE 空间，同现有 RO 发射） | 就地字节 + 重定位 |
| AS4 extern-only TU | 不发射 | 无 |
| AS0 可变（现状） | DSEG + XINIT v1 | 现状不变 |

- `const __xdata` 仍进 XSEG：const 是写纪律，存储空间由 AS3 决定；**禁止**静默跨空间搬进 ROM（会改变对象地址语义）。
- AS4 未初始化/暂定定义 → CSEG 零图像（ROM 中零填充合法）。
- AS4 全局的 STT_OBJECT/size 语义照抄现有路径。AS4 写入在 X2 已 fail-closed，本片不重做。
- 对象大小上限：XSEG 对象沿用"大小须 16 位内"吗？**不**——XDATA 记录尺寸字段 u16 不变，但对象可以 >64K（24 位空间）。裁定：XSEG 对象大小仍限 u16（65535），>64K 的单对象超出语料与板级现实，拒绝并诊断；窗口内偏移仍 16 位（DPTR 形态所限）。成文记录此边界。

## 2. XDATA 初始化记录 v1（新节，格式冻结）

新节 `.mcs251.xdata_init`（+`.mcs251.xdata_init.` 前缀），SHT_PROGBITS，SHF_ALLOC，Region=`XDATA_INIT`（CODE 类 ROM 区，同 XINIT 的区域阶级）。

记录格式（镜像 DSEG XINIT v1 稀疏形态，地址拓宽为 24 位）：

```
u8  bank     ; canonical 地址 bits[23:16] —— 正是 DPXL 要装载的值
u16 window   ; canonical 地址 bits[15:0]，大端（与 XINIT v1 的 u16 字节序一致，以 lld 解析为准）
u16 object_size
u16 payload_size
payload bytes（payload_size==0 表示"仅清零"）
```

- lld 侧：`s_XDATA_INIT`/`l_XDATA_INIT` 边界符号（沿用 `s_XSEG` 先例；reserved-symbols 同步登记）。**目的地址校验**：每条记录的 [bank:window, bank:window+object_size) 必须完整落入某个已分配 XSEG 区间（含末字节；跨界=错；不同记录目的区间重叠=错；bank:window 算术在 24 位内做，不许截成 16 位比较）。
- CRT 消费循环属 X4（crt 在 validation 树，不在编译器仓）；本片冻结格式并写 `XDATA-CODE-DESIGN-SUPPLEMENT.md` 新章节。
- lld 对 XDATA_INIT 区只做：CODE 类分配（reserveCode 已有 24 位+重叠）、记录解析与目的校验、边界符号。不合成字节。

## 3. 指针初值与重定位（Alice 补充③）

- 初始化器支持扩到**指针叶子**：`&global`（AS0/AS3/AS4 目标均可）+ 常量 addend 折叠（`GEP null`/`inttoptr`/`ptrtoint` 等任意表达式代数一律继续拒绝——与现有保守支持检查同风格）。
- 指针存储宽度按 DataLayout（AS3/AS4 均 32/8：4 字节容器、24 位有效值）。发射（X3-R2 更正，2026-09-12）：容器是**大端 32 位图像，其低 24 位是 canonical 有效地址**——字面零的最高有效字节（位 [31:24]，不属于有效地址）置于容器偏移 0，R_MCS251_24 字段占容器偏移 1..3（bank 在 1、窗口大端在 2..3；`applyVectorJumps` 的 EJMP 3 字节目标是冻结真值）。链接后符号 0x011234 序列化为 `00 01 12 34`；X2 装载序列按大端读满 4 字节，b1→DPXL、b2→DPH、b3→DPL，重组出 0x011234（原"低 3 字节字段 + 第 4 字节恒 0"的表述会得到 `01 12 34 00`，装载端重组出 DPXL=0x12、窗口=0x3400，属错编译，已修复）。
- **门禁**：`R_MCS251_16`（含 J16）命中"定义于 XSEG 的符号"→ 硬错误 `XDATA symbol <name> truncated to 16 bits (use the 24-bit relocation channel)`。LO8/MID8/HI8/24 通道放行。此检查放 applyRelocations 处，fail-closed，无逃生开关。
- 同理 XINIT/xdata_init/RO 载荷中的指针初值全部走 24 位通道；DSEG 里存的 AS3 指针初值=canonical 24 位值（X2 装载序列按 bank/window 分裂，运行时自洽）。

## 4. 容量配置（Alice 补充②）

新驱动选项 `--xdata-size=N`（字节；先例 `--iram-size`）。缺省=不设（仅 24 位 rangeFits，向后兼容）。设了则：所有 XSEG 分配区间总体须落入 `[area-start(XSEG), area-start(XSEG)+N)`，违反=硬错误，错误信息带对象名与区间（`XDATA capacity [lo,hi) exceeds --xdata-size=N in <section>`）。

## 5. lld 测试矩阵（Alice 补充④，全部 lit 化）

1. **末字节成功**：area-start=0x010000，size=0x100，对象占 [0x010000,0x010100)——链接通过，`s_XSEG/l_XSEG` 正确。
2. **跨界失败**：同配置，对象占 [0x010000,0x010101)——硬错误（24 位 rangeFits 不变；再加 --xdata-size=0x100 的同形用例，错误信息指向容量）。
3. **错 bank**：对象 A 在 bank 01，用户 `--area-start=XSEG=0x020000` 再引一个 bank 01 符号的 24 位初值 → 由"指针初值须落 XSEG 分配区间"检查兜住（初值区间 [0x01xxxx] 与分配区间无交=硬错误）。另配 R_MCS251_16 截断负例（§3 门禁）。
4. **重叠**：两个 XSEG section 经 `--area-start=.mcs251.XSEG.a=...` 命中同区间 → 现有重叠检查触发（补显式用例）。
5. **DSEG/栈 H 独立性**（补充⑤）：XSEG 大对象不进 DSEG 分配、`l_DSEG`/栈高水位/`--stack-size` 交互不变（现有 stack-and-areas.test 加 XSEG 变体）。
6. **xdata_init 正/负例**：合法记录分配、末字节、跨界、双记录重叠、bank 不符、载荷 size 不一致。
7. **指针初值**：DSEG 对象存 AS3 指针、AS4 表存 code 指针（字符串表回归 X1 形态）、R_MCS251_16 截断负例。

## 6. 后端测试

- llc：AS3 定义 → `.mcs251.XSEG` NOBITS + xdata_init 记录字节级断言（含 bank/window 拆分、清零型 6+3 字节、载荷型）；AS4 定义 → CSEG RO 图像；`const __xdata` → XSEG；extern-only 不发射；>64K 单对象拒绝；指针初值 24 位通道 fixup 断言（hi8/mid8/lo8 或 24 字段，按实现留证）。
- 现有 lit 全绿基线：clang mcs251 45/45、llvm 141/141（X1 收官口径）不许回退。

## 7. 文档（Alice 补充①⑥）

`XDATA-CODE-DESIGN-SUPPLEMENT.md` 增补：
- canonical ↔ DPXL 区域(bank) ↔ 窗口偏移 的换算成文（X2 运行时分裂 + X3 链接期 24 位通道 + 禁 16 位截断门禁，三层职责）。
- 容量配置来源（--xdata-size）与 area-start 的关系。
- xdata_init v1 记录格式冻结表。
- DPXL 保持协议与 MOVX 窗口关系（引用既有章节，不重写）。
- XSEG 单对象 64K 边界裁定（§1）。

## 8. 构建与验收命令

```bash
ninja -C /home/liu/build-mcs251 llc            # 后端树
ninja -C /home/liu/build-mcs251-lld mcs251-lld # lld 树
ninja -C /home/liu/build-mcs251-s1 clang       # clang 树（回归）
cd /home/liu/LLVM_STC32/MCS251/llvm && ../build-mcs251/bin/llvm-lit -q llvm/test/MCS251   # 路径以树内既有调用为准
cd /home/liu/LLVM_STC32/MCS251 && ./build-mcs251-lld... # lld lit: lld/test/MCS251
```

（lit 调用先看 `lld/test/MCS251/lit.cfg.py` 与既有 test 的 RUN 行，用对应构建树二进制；不要新建构建树。）

## 8a. 增补裁定（2026-09-12 协调员，实施中途下发）：64K 窗口跨界

**问题**：XSEG 线性分配可让对象骑跨 64K bank 边界；XDATA 初始化记录单 bank 字节描述不了跨界对象。X2 运行时按字节 24 位寻址不受影响。

**路线 A 裁定**：
1. AS3 定义全局每对象一节 `.mcs251.XSEG.<符号名>`（不再单一大节）。
2. lld XSEG 分配器不跨界规则：cursor 到窗口末尾剩余 < 节大小时，跳到下一 bank 起点（64K 对齐）再分配；洞计入 l_XSEG 跨度，不进 DSEG/栈。
3. belt-and-braces：xdata_init 记录区间跨 64K 边界 → 硬错误（含手造对象，双保险）。
4. 测试补：贴窗口末尾成功（末字节=0xXXFFFF）；差一字节跳 bank 成功（地址断言）；手造跨界记录被拒。
5. AS4/CSEG 不引入此规则。
6. DESIGN-SUPPLEMENT 增补"64K 窗口边界与 XSEG 分配对齐"小节。

## 9. 禁区

- 不动 X2 装载序列/指令选择；不动 X1 Sema；不动 generic 层（P1-2 纪律）。
- 不做 CRT 运行时循环（X4）；不做 e2e（X4）；不做官方语料矩阵（X5）。
- 不引入 `--allow-*` 逃生开关；一切门禁 fail-closed。
- 工作树不干净时先报告，不得混入无关变更。
