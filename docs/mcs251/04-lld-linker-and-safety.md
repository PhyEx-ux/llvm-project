# 04. 专用静态链接器 `lld/MCS251` 与静态安全审计

> **核对源码基线**：`cad7f0ab1f32`  
> **核心源码**：`lld/MCS251/{Driver.cpp, LinkerCore.h, LinkerCore.cpp, LinkerScript.*, LTO.*, PlacementReport.*, PlacementVerify.*, PlacementVerifyMain.cpp, CMakeLists.txt}`、`llvm/tools/llvm-readobj/ELFDumper.cpp`、`lldb/source/Plugins/ABI/MCS251/*`

---

## 1. 架构设计与构建产物

因 MCS-251 需要哈佛多空间独立占用检查、按输入切片 first-fit 分配 DSEG、OSEG 同址覆盖、CODE 稀疏空洞（不补零）以及面向微控制器的静态安全门禁，项目在 `lld/MCS251/` 下实现了独立的链接器 flavor（入口命令 `mcs251-lld` 或 `lld -flavor mcs251`），故意不把目标规则散布进通用 `lld/ELF`。

`lld/MCS251/CMakeLists.txt` 构建两个独立产物：
1. **`lldMCS251` 库**（合入 `lld`）：
   - `Driver.cpp`：递归 `@response-file` 展开、命令行解析、ET_EXEC 写出（保留实际 v1/v2 `e_flags`）、`--map-json`、`.mcs251.placement.positions` NOTE 序列化、`--link-facts` 文本写出、事务性原子发布；
   - `LinkerScript.{h,cpp}`：GNU 风格链接脚本解析器（`MEMORY`、`SECTIONS`、`ENTRY`、`EXTERN`、`INPUT`/`GROUP`、`SEARCH_DIR`、`INCLUDE`、`PROVIDE`、`KEEP`、`RESERVE_DATA` 与表达式求值器）；
   - `LTO.{h,cpp}`：Full LTO 与进程内 ThinLTO 链接期集成（自动识别 `.bc` 直接输入与 `.a` 内 bitcode 成员，执行 MCS-251 IR 契约与 Tag 28 跨 TU 签名校验并内存编译为原生 ELF `.o` 喂入 `LinkerCore`）；
   - `LinkerCore.{h,cpp}`：零驱动依赖的纯核，负责输入校验、身份/签名检查、哈佛布局、重定位、门禁、ISR 优先级解析与 `__mcs251_isr_priority_init` 合成、G11 合并与 WP2 重入分析；
   - `PlacementReport.{h,cpp}`：G11 放置报告文本 v1 序列化。
2. **独立校验可执行文件 `mcs251-placement-verify`**：仅链接 `LLVMObject` 与 `LLVMSupport`，**故意不链接 `lldMCS251`**，确保审计校验器与链接器不共享解析/合并逻辑。

---

## 2. 完整命令行选项与环境变量（`Driver.cpp`）

> **响应文件与脚本支持**：`Driver.cpp` 在参数解析前支持递归 `@response-file` 展开（GNU 引号/转义规则、相对嵌套路径解析、循环包含检测与发布路径冲突矩阵保护），并支持 `-T` / `--script` GNU 风格链接脚本（显式命令行参数优先级高于脚本默认值）。

| 选项 | 形式 | 默认值 | 说明 |
|---|---|---|---|
| `-o` | `-o <p>`, `-o=<p>` | `<首个输入>.elf` | 输出 `ET_EXEC` ELF 路径（Intel HEX 由后续 `llvm-objcopy -O ihex` 生成） |
| `-L` / `--library-path` | `-L <dir>`, `-L<dir>`, `--library-path=<dir>` *(可重复)* | 无 | 添加静态库搜索目录（按传入顺序供后续 `-l` 搜索） |
| `-l` / `--library` | `-l <name>`, `-l<name>`, `-l:<file>`, `--library=<name>` *(可重复)* | 无 | 在 `-L` 目录中搜索 `lib<name>.a`（或 `-l:<file>` 精确文件名）并按当前 `--whole-archive` 策略作为静态库加载；未找到时硬报错 |
| `-s` / `--strip-all` | `-s`, `--strip-all` | `false` | 显式关闭输出符号表（与 `--keep-symbols` 互逆，兼容标准 Clang 链接透传） |
| `@<file>` | `@<rspfile>` *(可嵌套)* | 无 | 递归展开响应文件（支持相对嵌套路径、循环包含报错，且响应文件受发布路径冲突矩阵保护） |
| `-T` / `--script` | `-T <ld>`, `-T<ld>`, `--script=<ld>` *(可重复)* | 无 | 解析 GNU 风格链接脚本（`MEMORY`/`SECTIONS`/`ENTRY`/`EXTERN`/`INPUT`/`GROUP`/`SEARCH_DIR`/`INCLUDE`/`PROVIDE`/`KEEP`/`RESERVE_DATA` 与 `ORIGIN`/`LENGTH`/`ALIGN`/`DEFINED` 表达式）；CLI 选项优先覆盖脚本设定 |
| `--map` | `--map <p>`, `--map=<p>` | 无 | 确定性文本布局 Map（区域、切片、洞、符号、栈门禁诊断；`--keep-symbols` 时追加 `FUNC` 行） |
| `--map-json` | `--map-json <p>`, `--map-json=<p>` | 无 | 确定性结构化 JSON 布局文件（schema v1，包含 `e_flags`/`entry`/`areas`/`sections`/`symbols`/`stack`/`flash_gate`/`xdata_size`/`bits`/`reentrancy`/`isr_priorities`），参与事务性原子发布 |
| `--lto-O0..3` | `--lto-O0\|1\|2\|3` / `-plugin-opt=O0..3` | `--lto-O2` | 控制输入含 LLVM bitcode（`.bc` 或 `.a` 内 bitcode 成员）时的 Full LTO / 进程内 ThinLTO 优化级别；纯 ELF 链接零开销直通 |
| `--area-start` | `--area-start <A>=<N>` *(可重复)* | 无（存在区域必填） | 禁止同区重复。24 位区：`HOME`, `VECS`, `BOOT`, `CSEG`, `XINIT`, `XDATA_INIT`, `BITINIT`, `XSEG`, `.mcs251.XSEG*`；16 位区：`DSEG`, `ISEG`, `.mcs251.DATA.*`（支持十进制 / `0x`） |
| `--iram-size` | `--iram-size <N>` | `128` | 限定 DSEG（上限 `0x80`）与 ISEG（上限 `0x100`）搜索窗口，决定 `l_IRAM` |
| `--edata-end` | `--edata-end <N>` | `0x0fff` | 闭区间上界 `0..0xffff`；同时控制**栈容量上限**与 **G8 自动 EDATA 迁移窗口 `[max(0x100, DsegStart), EdataEnd]`**（G12K128=`0x0fff`，G144K246=`0x3fff`） |
| `--xdata-size` | `--xdata-size <N>` | `0`（关闭） | 板级 XDATA 容量（`1..0x1000000`），启用后约束全部 `XSEG` 与 `FIXED`-XDATA 落在 `[area-start(XSEG), +N)` |
| `--flash-base` / `--flash-size` | 成对传入 | 关闭 | 启用 Flash ROM 门禁（存在 G11 `FIXED`-CODE 时**强制要求**）；校验所有配置 CODE 起点与全部非空 CODE 节落在 `[base, base+size)` |
| `--reserve-data` | `--reserve-data <start>,<size>` *(可重复)* | 无 | 显式保留内部 16 位 DATA 区间，参与避让与栈高水位计算 |
| `--stack-size` | `--stack-size <N>` | `0`（自动取最大连续空洞） | 显式指定 `SSEG` 大小（须 `>= max(SSEG 输入)`） |
| `--stack-gate` | `--stack-gate` | **始终开启** | 栈门禁强制启用；传 `--no-stack-gate` 直接硬报错 |
| `--isr-reentrancy` | `--isr-reentrancy[=off\|warn\|error]` | **`error`** | WP2 静态参数槽冲突严重度（`--no-isr-reentrancy` 等价 `off`）；不决定分析是否执行，也不改变传统函数级 candidate 警告级别 |
| `--isr-reentrancy-coverage` | `--isr-reentrancy-coverage=allow\|require` *(仅 `=`)* | `allow` | 覆盖度策略：`require` 在未声明 IRQ 或存在任一未知事实（8 类 Unknown）时失败；禁止与 `--isr-reentrancy=off` 联用 |
| `--isr-preemption-config` | `--isr-preemption-config=<p>` *(仅 `=`，至多 1 次)* | 无 | 外部中断抢占契约，行语法 `preempt <isrA> <isrB>`（支持 `#` 注释）；每个名字必须解析为不同已注册 ISR |
| `--isr-priority` | `--isr-priority=<sym\|slot>=<0..3>` *(可重复)* | 默认优先级 `0` | 覆盖已注册 ISR 的硬件优先级（优先级高于源码 `mcs251_isr_priority` 属性）；自动按 `pri(B) > pri(A)` 推导抢占对并合成 `__mcs251_isr_priority_init` 的 STC32G `IP`/`IPH`/`IP2`/`IP2H`/`IP3`/`IP3H` 初始化代码 |
| `--no-auto-isr-preemption` | `--no-auto-isr-preemption` | `false`（默认自动推导） | 关闭基于硬件优先级自动向 WP2 注入抢占对的行为 |
| `--link-facts` | `--link-facts=<p>` *(仅 `=`)* | 无 | 输出版本化**纯文本**链接事实文件（`MCS251-LINK-FACTS 1`，**非 JSON**），供外部预算/审计工具消费 |
| `--keep-symbols` | `--keep-symbols` | `false` | 向输出 `ET_EXEC` 追加 `.strtab`/`.symtab`，并在 map 中输出 `FUNC` 行（默认关闭以冻结发布资产哈希） |
| `--placement-manifest` | `--placement-manifest <p>` *(可重复)* | 无 | 外部 G11 固定放置清单，保留词法绝对路径与 1-based 物理行号供溯源与审计 |
| `--placement-report` | `--placement-report <p>` *(至多 1 次)* | 无 | 输出 G11 放置报告 text v1；自动隐含 `--keep-symbols` 与定位 NOTE 收集（不自动执行校验器） |
| `--verify-placement` | `--verify-placement` | `false` | 隐含 `--keep-symbols` 与定位 NOTE 收集；在私有临时目录先调用外部 `mcs251-placement-verify` 全部通过后再原子发布输出 |
| `--print-input` | `--print-input` | `false` | 转储解析后的输入节/重定位/符号/放置记录并提前退出 |
| `--whole-archive` / `--no-whole-archive` | 位置敏感开关 *(可重复)* | 关闭 | 切换后续 `.a` 静态库（经典与 Thin Archive）的提取策略：开启时无条件拉入全部成员，关闭时按未定义符号不动点按需提取 |
| `--gc-sections` / `--no-gc-sections` | 布尔开关 | **`false`** | 启用节级死代码/死数据垃圾回收（配合 `-ffunction-sections`/`-fdata-sections` 与链接脚本 `KEEP(...)`）；已支持与 G11 放置审计模式（`--placement-report`/`--verify-placement`）联用（`FIXED` 与 `SHF_GNU_RETAIN` 节强制保活并受校验器复核） |
| `--print-gc-sections` | `--print-gc-sections` | `false` | 在启用 `--gc-sections` 时向 stderr 打印每个被回收的输入节名称与来源 |
| `-u` / `--undefined` | `-u <sym>`, `--undefined=<sym>` *(可重复)* | 无 | 强制将符号视为未定义引用（触发 `.a` 成员按需提取，并作为 `--gc-sections` 根集保留） |
| `-e` / `--entry` | `-e <sym>`, `--entry=<sym>` | 无 | 指定额外入口符号作为 `--gc-sections` 可达性根并写入 `ET_EXEC` 入口地址 |
| **显式报错项** | `--no-stack-gate`, `--oformat=ihex` | — | 硬报错拒收（栈门禁不可关闭；HEX 由 `llvm-objcopy -O ihex` 生成） |

**环境变量**：
- `MCS251_PLACEMENT_VERIFY`：显式指定 `mcs251-placement-verify` 路径（未设置时仅搜索当前可执行文件同级目录，故意不查 `PATH`）；
- `MCS251_PLACEMENT_DUMP` / `MCS251_P6_TRACE`：仅用于测试桩的内部诊断钩子。

---

## 3. 16 步链接管线与身份/签名门禁

`Linker::run`（`LinkerCore.cpp:8526-8648`）严格按序执行：

1. **输入加载、静态库按需提取与单对象检查（`loadFile` / `loadArchive`）**：
   - 支持经典与 Thin Archive `.a`（`!<arch>\n` / `!<thin>\n`）：处于 `--whole-archive` 作用域内的 `.a` 无条件加载全部成员；否则解析归档符号表并按当前已加载对象的未定义符号集合执行不动点按需提取（支持 `-u` 种子与跨库回溯）；
   - 拒绝 `SHT_REL`、非 ELF32BE `EM_MCS251`、`sh_addr != 0`、非法对齐（仅 `.mcu.fixed.*` 允许 `align > 1`）与未知非 ALLOC 节；接受 `STT_FILE` 符号、`SHT_GROUP`（`GRP_COMDAT`）节与非 ALLOC `.debug_*` 调试节；
   - 对含 `R_MCS251_BITADDR8` 的代码节立即运行**重定位前**全节指令解码（`validateBitAddrStream`）；仅在无定义符号、无重定位时忽略大小为 0 的空 PROGBITS `.data`。
2. **IRQ 模式判定**：任一输入含 `.mcs251.isr` 即启用 IRQ 模式，禁止输入自带 `VECS` 并自动补齐 `VECS=0xff0003`。
3. *(若 `--print-input` 则在此返回)*
4. **跨 TU 身份校验（`validateIdentitySet`）**：
   - **严禁 v1 与 v2 对象混链**；
   - v1 对象拒收一切 v2 专属特性（`EDATA_MOVABLE`、`XSEG_SPLIT`、`.mcu.fixed.*`、`.mcs251.placement*`）；
   - v2 对象仅支持 32 位 AS0 的 **`Small (32,1)`** 与 **`XSmall (32,8)`**，**允许二者混链**（逐字段比对 22 个必需 Tag 时排除 `default_placement` Tag 13 与 Tag 24 的 placement 原子），但拒绝 `Tiny`/`XTiny`/`Large` 及任何非零 `RequiredCapabilitiesLo/Hi` 并集。
5. **G11 放置合并（`mergePlacement`）**：合并所有输入 `.mcs251.placement`/`.names` 与 `--placement-manifest`，建立 `PlacementNames` 与 `FixedInfo`。
6. **COMDAT 去重与全局符号解析（`deduplicateComdats` + `resolveSymbols`）**：
   - 对 `SHT_GROUP` (`GRP_COMDAT`) 组按组签名保留首个输入成员、丢弃后续重复组内全部成员节；
   - 支持普通符号的 `STB_WEAK` 解析（强定义覆盖弱定义、多弱定义保留首个、未定义弱引用解析为 `0`），并自动剪除被覆盖/丢弃弱数据对应的 `XINIT`/`XDATA_INIT` 初始化记录；
   - 保持对 ISR、Bit 对象与 G11 固定放置的弱定义/COMDAT Fail-Closed 拒收，同时拒收 `SHN_COMMON`、重复强定义及用户定义保留边界符号。
7. **P-4 函数签名校验（`validateFileSignatures` + `validateSignatureSet`，v2 专属）**：确认每个外部 `STT_FUNC` 及被代码重定位引用的 `STT_NOTYPE` 在**本对象自身**的 Tag 28 表中都有记录；跨 TU 对同名函数执行**全两两比较**（适配零参 K&R `bit2` 非传递豁免）。
8. **Bit 身份与 BT14 Profile 校验（`buildBitIdentities` + `validateBitProfileSet`）**。
9. **IRQ 身份、硬件优先级初始化合成与 127 槽向量表合成（`resolveISRPrioritiesAndSynthesizeInit` + `synthesizeIRQVectors`）**：合并 `.mcs251.isr.priority` 与 `--isr-priority` 覆盖值，推导抢占对注入 WP2（除非指定 `--no-auto-isr-preemption`）并在配置了非零优先级或显式引用时合成 `___mcs251_isr_priority_init`（写 STC32G `IP`/`IPH`/`IP2`/`IP2H`/`IP3`/`IP3H`）；生成 `0..126` 槽（`[0xFF0003, 0xFF03FB)`，步长 8B）：109 个 Legal 槽为 4B `PROGBITS`（`0x8A EJMP` + `R_MCS251_24` 指向已注册 ISR 或 CRT 默认处理函数）+ 4B `NOBITS` 空洞；16 个 Reserved 与 2 个 System 槽为 8B `NOBITS` 空洞。
10. **放置冲突检查与可选 `--gc-sections` 可达性裁剪（`validatePlacementResolution` + `gcSections`）**：
    - 当显式启用 `--gc-sections` 时（已支持与 `--placement-report`/`--verify-placement` 联用），以 `HOME`/`VECS`/`BOOT`/入口 `-e`、已注册 ISR、`-u` 符号、脚本 `KEEP(...)`、G11 `FIXED`/`SHF_GNU_RETAIN` 与 Bit 对象为根集沿重定位图做 BFS，丢弃不可达输入节及仅服务于死数据的 `XINIT`/`XDATA_INIT` 条目（`--print-gc-sections` 打印裁剪明细）。
11. **哈佛双域布局（`layoutCode` → `layoutData`）**。
12. **IRQ 保留区间与 CRT 校验**：强制 `VECS=0xFF0003`、非合成代码不侵入向量窗、`HOME=0xFF0000` 为 3B `0x02 LJMP` 精确落到 CRT `BOOT >= 0xFF0500`、默认入口为精确 4B `C2 AF 80 FE`（`clr EA; sjmp .`）。
13. **Flash ROM 门禁（`checkFlashGate`）**。
14. **未定义符号检查、重定位应用、重定位后位流复核、`XINIT` / `XDATA_INIT` 协议校验与 `.debug_*` 调试节拼接重定位（`linkDebugSections`）**：多 TU `.debug_*` 按同名节拼接并应用 `R_MCS251_32`/`R_MCS251_24`/`R_MCS251_16` 重定位（指向已 GC 丢弃节的引用写入 tombstone `0`）。
15. **WP2 地址级 ISR 重入诊断（`diagnoseIsrReentrancy`）**（同步收集 `--link-facts`）。
16. **符号表收集、定位信息收集（`collectPositions`）与 Map 构建**。

---

## 4. 哈佛分区布局、G8 自动迁移与 G13b 拆分

### 4.1 CODE 空间布局顺序（`layoutCode`）
1. **`FIXED` CODE**（G11 固定代码/常量，要求同时开启 Flash 门禁）；
2. 按输入文件与节序号线性推进各区游标：`HOME`、`VECS`、`BOOT`、`CSEG`（含 `.text*` 与 `.rodata*`）、`XINIT`、`XDATA_INIT`（后续在 `layoutData` 阶段还将追加合成的 `BITINIT`/`.mcs251.xinit.bit`/`.mcs251.xdata_init.synth`）。`NOBITS` 片段推进地址但不写入 `Image`，天然保持稀疏空洞不补零。

### 4.2 DATA 与 XDATA 空间布局顺序（`layoutData`）
1. `--reserve-data` 显式保留区；
2. **`FIXED` AS0-DATA**（G11）；
3. **`DATA_ABS`**（`.mcs251.DATA.<id>`，须配同名 `--area-start`）；
4. **`BSEG_BYTES`**（`[0x20, 0x2F]` 内 first-fit）；
5. **`REG_BANK_0..3`**（固定于 `Bank * 8`，同组取 `max <= 8`）；
6. **`BIT_BANK`** 覆盖组（`[0x20, 0x2F]` 内按组 `max` first-fit）；
7. **Bit 槽分配与初始化表合成（`allocateBitSlots`）**：
   - 固定 RAM 位引用（`0x00..0x7F`）独占其宿主字节 `0x20 + (bit >> 3)`（SFR 位 `0x80..0xFF` 不占 RAM）；
   - 自动位定义从 bit 0 起按位紧凑打包进未封锁字节（可跨 TU 共享同一宿主字节），耗尽时打印逐字节占用详情并失败；
   - **BT14 位感知 CRT**（含 `.mcs251.bitprofile`）：要求 `--area-start=BITINIT`，合成 `.mcs251.bittable`（每自动宿主字节 3B：`{addr, ~mask, val & mask}`），供 `__mcs251_bit_init` 邻位无损 RMW 初始化；
   - **旧 S1 路径**：若存在初值为 1 的位，追加 7B `.mcs251.xinit.bit` 记录至 `XINIT` 末尾。
8. **G8 确定性失败回退迁移循环（`DSEG` + `EDATA` + `OSEG`）**：
   - 低直达页窗口：`[DsegStart, DsegEnd)`（上限截断于 `0x80`）；EDATA 窗口：`[max(0x100, DsegStart), EdataEnd]`；
   - 保存当前 `DataUsed`/`StackH` 快照，循环尝试：放置 `Migrated` 与显式 `EDATA` 到 EDATA 窗口 → 剩余 `DSEG` 在低窗口 first-fit → 全部 `OSEG` 作为单个覆盖组按 `max` 尺寸在低窗口 first-fit；
   - 任一步失败时选择牺牲者（优先选本次失败且带 `SHF_MCS251_EDATA_MOVABLE` 的非空 `DSEG` 节，否则选尚未迁移的最大候选节，平局按输入顺序）加入 `Migrated` 并从快照重启；无候选可迁时打印完整空洞与占用报告并失败；
   - 迁移成功节的 `Region` 改写为 `"EDATA"`，并合成 `s_EDATA`/`l_EDATA`。
9. **`ISEG`**（`[IsegStart, cap 0x100)` first-fit）与 **`SSEG`** 覆盖组（默认取窗口内最大空闲连续洞，或用 `--stack-size` 指定）。
10. **XDATA 独立空间**：先放 **`FIXED` XDATA**（`<= 65535B`、不可跨 64K 边界，不推进 `XsegCursor`）；再顺序放 **`XSEG`**：
    - 未标记普通 `XSEG`：单节 `<= 65535B`，当前 64K 窗口余量不足时跳至下一 64K 边界；
    - **G13b 拆分 `XSEG`（`SHF_MCS251_XSEG_SPLIT`，v2 专属）**：突破 65535B 上限且连续跨 64K 放置，链接器强制要求 `--area-start=XDATA_INIT`，按 `len = min(rem, 0xffff, window_tail)` 贪心合成 7B 纯清零 v1 记录到 `.mcs251.xdata_init.synth`（同时拒绝任何输入 `XDATA_INIT` 与拆分节重叠）。
11. **合成边界符号**：`s_DSEG=0`、`l_DSEG`（`[0,0x80)` 内总占用字节数）、`l_IRAM`、各区 `s_<AREA>`/`l_<AREA>`，以及按需合成 `__mcs251_stack_base`。

---

## 5. 四大静态安全门禁与 WP2 重入检测

### 5.1 栈容量门禁（`__mcs251_stack_base`，SPEC §6.3）
仅当输入对象存在对 `__mcs251_stack_base` 的未定义引用时触发（门禁不可关闭）：
```text
H          = max(0x100, 所有非空内部字节寻址 DATA 切片的 end)
first_byte = align_up(H, 16) + 16
SPX        = first_byte - 1
capacity   = (edata_end + 1) - first_byte
```
`capacity < 1024`（或无符号下溢）直接报错中止；通过则定义 `SHN_ABS` 符号 `__mcs251_stack_base = SPX`。

### 5.2 Flash ROM 门禁与 XDATA 容量门禁
- **Flash 门禁**（`--flash-base` + `--flash-size`）：检查全部已配置 CODE 区域起点（含空区）与全部非空 CODE/FIXED-CODE 节均落在 `[FlashBase, FlashBase + FlashSize)`；`XSEG` 不受此门禁约束；
- **XDATA 门禁**（`--xdata-size`）：检查全部非空 `XSEG` 与 `FIXED`-XDATA 节落在 `[area-start(XSEG), +XdataSize)`。

### 5.3 Bit 模块双遍 `.text` 指令流解码
凡含 `R_MCS251_BITADDR8` 的代码节，在加载期与重定位后均须从偏移 0 到节尾完整通过 `mcs251InstrLen()` 解码（已包含 classic 1B `MOVX` 对 `0xE0`/`0xF0`），且每条 `BITADDR8` 必须精确命中 8 条位操作码（`D2/C2/B2/92/A2/20/30/10`）的操作数字节 `+1`；因此后端自动将位模块的只读常量迁至 `.rodata`。

### 5.4 WP2 地址级静态参数槽重入检测（`diagnoseIsrReentrancy`）
默认开启且已证冲突直接导致链接失败（`--isr-reentrancy=error`）：
- **Route B 栈 ABI 下的可重入性**：在默认栈传参 ABI 下，编译器产出的多参函数不再生成静态 `*_PARM_*` / `OSEG` 槽位，ISR 与前台共享同一多参函数时天然可重入（`0 slot(s), 0 address conflict group(s)`，直接通过 `--isr-reentrancy=error`）。`lld` 仍保留完整 WP2 检测逻辑，持续兜底审计遗留/手工汇编对象中可能出现的静态参数槽冲突。
- **分析对象**：反汇编全部非空 CODE 节构建指令边界图，识别 `DSEG`/`OSEG`/`EDATA` 中全部 `*_PARM_<digits>` 参数槽（及缺少带大小槽符号的匿名 `OSEG` 节）和调用边，以 `(ObjectId, Shndx, SymIndex)` 作为唯一身份；
- **5 级证据分类（`ReentrancyEvidence`）**：
  - `0 EvAddressRef`（数据重定位指向代码，非调用）
  - `1 EvUnverified`（无法确认指令边界/操作码的引用）
  - `2 EvVerifiedControl`（可信边界上 `ecall 0x9A` / `ejmp 0x8A` 的 `R_MCS251_24`）
  - `3 EvVerifiedAccess`（完整的 2 指令 24 位地址装载对 `7E <nib=8> MID8 LO8` + `7A <nib=C> HI8` 且三者指向同一参数槽地址）
  - `4 EvContract`（封闭枚举的冻结 CRT 复位/启动链契约 v1：精确 3B `HOME` + 精确 `0x106`B `crt-irq*.yaml` 模板与 12 个锚点）
- **冲突判定**：按「同槽跨上下文」与「不同槽字节区间重叠」两类，分别对 **ISR vs 前台（Kind=0）** 与 **ISR vs ISR（Kind=1）** 逐对评估；仅当至少存在一次确认的调用方写入（`Role=1`）、槽范围已知、所有访问函数区间已知、且满足正向可达性（前台须由复位链 `FgRooted` 正向可达；双 ISR 须在 `--isr-preemption-config` 中声明可抢占）时判为 **Proven**（记录首对决胜证据 `SlotA/SlotB` 与 `DecidingRootsA/B`）。旧函数级共享提示仅保留为 `candidate` 警告，永不自动升级为错误。
- **8 类 Unknown 与 4 种 Coverage 状态**：记录 `ECALLr (0x99)` 间接调用、无重定位 `ecall`、未归属控制目标、不可解码代码节、无函数归属槽访问、无大小函数、非控制指令控制重定位、不完整槽访问序列；汇总为 `0 undeclared` / `1 unchecked` / `2 open` / `3 closed`。
- **`--link-facts` 导出格式**：纯文本行协议（头为 `MCS251-LINK-FACTS 1` / `format text 1`），记录 `obj`/`sec`/`sym`/`isr`/`edge`/`contract`/`icall`/`ctx`/`cap`/`area`/`reentrancy`/`unknown`/`conflict*`/`candidate`，身份编码为 `ObjId:SecIdx:SymIdx`。

---

## 6. 输出布局、原子发布、G11 独立校验与 `llvm-readobj`

### 6.1 `ET_EXEC` 布局与事务性发布（`Driver.cpp`）
- **稀疏段映射**：将 `Result.Image` 连续字节合并为最多 `0xFFFD` 个 `PT_LOAD`（`PF_R|PF_X`, `p_align=1`）与 `.mcs251.load.<I>` 节；输出的 `ET_EXEC` 忠实携带输入身份的 `e_flags`（v2 对象输出 `0x102 = EF_MCS251_ABI_V2 | EF_MCS251_BACKEND_NATIVE`，v1 对象输出 `0x1 = EF_MCS251_ABI_V1`）。
- **仅追加元数据（保持普通链接与固件镜像字节稳定）**：依次为 `.shstrtab` → （若 `--keep-symbols`）`.strtab` + `.symtab` → （若输入含 DWARF）非 ALLOC `.debug_*` 节（不进入 `PT_LOAD`，`-g` 与 `-g0` 经 `llvm-objcopy -O ihex` 产出的 Intel HEX 固件逐字节一致）→ （若审计模式 `--placement-report`/`--verify-placement`）`.mcs251.placement.positions`（`SHT_NOTE`, type=3, version=1，含所有已加载对象/静态库成员 SHA-256 与每个存活合法 ALLOC 节的最终切片表）。
- **原子发布与冲突矩阵**：先检测所有输出（含 `.tmp`、文本 map、JSON map、报告、link-facts）与所有输入（对象、清单、抢占配置、响应文件、链接脚本）是否指向同一物理文件；审计模式先在私有临时目录产出并运行 `mcs251-placement-verify`；提交阶段以 `O_EXCL` 写 `.tmp`、校验大小、建立同目录 `.pubbk.*` 备份、按 `map → [map-json] → [report] → [facts] → output` 顺序原子重命名并在任一步失败时逆序回滚。

### 6.2 G11 放置报告与独立校验器 `mcs251-placement-verify`
- **报告格式（text v1，`PlacementReport.cpp`）**：纯 ASCII、无表头/注释、` | ` 分隔、LF 换行、按 `(stable, file, sym, section)` 字节序排序，特殊字节按 `\xHH` 转义；每个合并记录的每个独立 `(path, elf_name)` 来源占一行。
- **独立检验 V1–V18 + 6 项定位载体检查（`PlacementVerify.cpp`）**：从磁盘独立重读输出 `ET_EXEC`、全部输入 `.o` 及 `.a` 静态库成员（通过 `--archive-member-offset=<N> --object=<archive>` 独立解析原始成员字节）、清单、map 与板级窗口参数，支持 `--gc-sections` 审计（允许非 `FIXED`/非 `SHF_GNU_RETAIN` 死节被回收，但对任何缺失的 `FIXED`/`SHF_GNU_RETAIN` 节继续 Fail-Closed），端到端复算 `H_source`（V4）、独立执行 P-5 语义合并（V6）、双向核对报告与 `H_report`（V7–V9）、核对定位 NOTE 的输入 SHA-256 与全量存活 ALLOC 覆盖、校验 `.symtab`/尺寸/对齐/板级窗口/全空间无重叠（含动态节与初始化表）/retain/`noinit` 不被 `XINIT`/`XDATA_INIT` 覆盖（V16）/bind 重定位（V17）/owned CODE 字节与 J16 bank（V18）。

### 6.3 `llvm-readobj` 与 LLDB MCS251 支持现状
- **`llvm-readobj`（`ELFDumper.cpp`）**：完整支持 `EM_MCS251`（`"MCS251 (experimental)"`）、`EF_MCS251_ABI_V1`/`EF_MCS251_ABI_V2`/`EF_MCS251_BACKEND_NATIVE` 头标志、`SHT_MCS251_ATTRIBUTES (0x70000003)` 节类型名、12 种重定位名、3 个目标节标志（`p`=OVERLAY, `e`=EDATA_MOVABLE, `x`=XSEG_SPLIT）、`--notes` 区分 `NT_MCS251_ABI`（32B v1 ABI）与 `NT_MCS251_PLACEMENT (1)` / `NT_MCS251_PLACEMENT_NAMES (2)` / `NT_MCS251_PLACEMENT_POSITIONS (3)`，以及 `-A` / `--arch-specific` 读取实际 `Contents[0]` `FormatVersion` 并结构化展开 Tag 28 `function_signatures`（`Name`/`Role`/`ParamCount`/`BitParams`/`RetBit`/`CallABI`）。
- **LLDB 源码接入（`lldb/`）**：已在 `ArchSpec.{h,cpp}` 注册 `eCore_mcs251`（大端、24 位地址空间、指令长度 1..6B）与 `ELF::EM_MCS251`，并新增 `lldb/source/Plugins/ABI/MCS251/ABISysV_mcs251.{h,cpp}`（DWARF 寄存器映射、Route B CSR 集合与向上增长 3B `ecall` 栈帧展开计划）。注：默认工具链构建启用 `-DLLVM_ENABLE_PROJECTS="clang;lld"`，需开启 `lldb` 项目时自动编入。
