# MCS251 ELF 与 lld 静态链接规范（E1 提案，ABI v1）

状态：**完整 E1 待 PM 审批；本文不是 E2/E3 已实现声明。审批前不得进入 E2。**

PM 已中期裁定批准：根目录 lld/MCS251 专用 flavor、直接 HEX + map（objcopy 不进
验收依赖）、第2.2节重定位语义修正、第6.3节栈公式。其余规范及 E2 放行仍待完整审批。

【2026-09-06 修正案（用户裁定）】最终输出改为 **ET_EXEC ELF + llvm-objcopy 转 HEX**：
lld 输出可执行 ELF，Intel HEX 由链中 `llvm-objcopy -O ihex` 步骤产生；"直接 HEX
发射器"与"objcopy 不进验收"条款废止。验收锚点=稀疏地址→字节域一致 + QEMU 串口；
HEX 文本排版（分行长度等）委托 objcopy，不要求与旧链文本一致。栈门禁/ROM 门禁
仍属链接器职责不变。

【2026-09-07 修正案（PM 已审批）】**MCS251 ELF 运行时算术库集成补充规范**

状态：**本修正案已获 PM 审批（2026-09-07）；获批前不得据此实施支持域扩大——实施启动
仍须满足下述全部前置门。本案不是运行时已实现或验收通过声明，也不替代适用的 ELF 前置
审批。**

本案依据下列文件；下文"本规范"指本 SPEC，"除法设计"与"模型设计"分别指：
- 除法设计：`validation/mcs251-models/proposals/SIGNED-DIV-REMAINDER-DESIGN.md`，已获 Alice APPROVE 的 v4。
- 模型设计：`validation/mcs251-models/DESIGN.md`，尤其 D.5、F.2 的 N.1–N.9。
- 输入实现依据：`lld/MCS251/LinkerCore.cpp`（逐文件读取 ELF32BE ET_REL 对象，不支持 archive）。

除法设计 §10 八项决策及"集成补充规范先行"裁定原样沿用，不重新开放。本案仅补充运行时
集成维度；**2026-09-06 的 ET_EXEC ELF → llvm-objcopy → Intel HEX 修正案继续有效**，
不恢复直接 HEX 发射器或 HEX 文本排版一致要求。

**1. 支持域扩大与行为边界**

1.1 本规范 §2.3 的支持域扩大至 `udiv/sdiv/urem/srem` 的 i8、i16、i32。仍需算术降低
时，i8 四操作 Promote 到 i16，有符号操作符号扩展、无符号操作零扩展，结果按原宽度恢复；
i16/i32 使用除法设计 §5.4、§6.1 规定的八个 libcall。合法常量折叠不要求保留调用。

1.2 删除旧 `SDIV/SREM/UREM` 的"不支持"fatal 分支及对应 Custom 拒绝路径；旧负例按除法
设计 §7.5、§8.1 翻正。这是对本规范第8、10、11节支持域及诊断基线的明确增补，不授权其他
默认 `.rel`/asm 字节或诊断变化。

1.3 `UDIVREM/SDIVREM` 维持 Expand；div 与 rem 同现接受两次独立 libcall，不新增融合
助手。i64 运行时不在本期范围；仍需 i64 算术降低的路径须可诊断拒绝，已合法折叠消失的宽
算术不因源 IR 出现过 i64 而拒绝。（除法设计 §10-Q6/Q8、§8.1）

1.4 输入语义沿用 LLVM LangRef：四操作除数为零，以及 `sdiv/srem` 的 `(INT_MIN,-1)` 均
为 UB；`exact` 不满足整除条件产生 poison。分类按**原 IR 宽度**判断，不因 i8 Promote 改
成有定义输入。不得新增 trap、返回值、终止方式或诊断契约；特定 fixture 的
`CHECK-NOT: trap` 不是运行行为承诺。（除法设计 §8.3、§10-Q2）

**2. 运行时对象身份与构建所有权**

2.1 首期生产供给为**八个独立 ELF32BE ET_REL `.o` 加显式对象清单**，每对象一个 helper
实现及其必需槽、重定位和元数据。八个精确宽度 C/IR 签名、宽度断言与 ABI 属性核对以除法
设计 §6.1 为规范性引用；不得以裸 `int/long/unsigned` 替换定宽接口。

2.2 沿用 SDCC C 级名称及目标一次 `_` 前缀，形成 `__divuint` 等八个链接符号；不增加
标准名别名。第二参数槽为对应 `__<fn>_PARM_2`，i16 为 2B、i32 为 4B；函数及槽定义、
引用按本规范 §6.1 与除法设计 §8.6 检查。

2.3 源码及构建归属 `llvm/lib/Target/MCS251/Runtime/`，使用公开算法独立实现的纯 C，
许可证为 Apache-2.0 WITH LLVM-exception；SDCC 源与产物仅作参考和独立 oracle。自检码
只进入验收固件。（除法设计 §10-Q1/Q3）

2.4 `mcs251rt` 为独立显式 CMake target：先构建 clang/llc，再用**同一构建树**的工具产
最终优化 IR、审计该批原件、原样交 llc 生成 ELF 对象，随后核对机器级调用图；运行时不进
入任何 LLVM 库或工具的链接图。

2.5 工具身份、模型/整数模型、前后端两串 DataLayout、两级优化条件及结果恢复转换依赖，
按除法设计 §6.1、§11.2 全部冻结、归档。八源禁除取余 IR、禁实际 i64 实体，以及固定调用
图审计均为交付门；"符号闭合、链接成功"不得代替无递归证明。配置或工具变化按该设计重验。

**3. 显式清单格式与消费机制**

3.1 清单为 UTF-8 无 BOM、LF 换行且末行有换行的纯文本，**恰好八行**；每行是一个对象的
规范化绝对路径。整行作为一个路径读取，空格不拆词；不使用引号包裹、注释、空行、通配符、
变量展开或 shell 转义。

3.2 行序固定为下列定义符号对应的对象，且每对象恰好出现一次：
`__divuint`、`__divulong`、`__divsint`、`__divslong`、`__moduint`、`__modulong`、
`__modsint`、`__modslong`。构建层须检查数量、文件存在性、对象唯一性及定义符号对应
关系；不得接受缺项、重复项或清单外替代供给。

3.3 清单是 `mcs251rt` 的声明产物；正式供给以八对象及其审计成功为前提。消费方链接目标
显式依赖清单和全部八对象，不依赖目录扫描或构建完成先后顺序。

3.4 构建层保持既有 CRT、应用及其他输入的冻结顺序，随后按清单行序追加八对象，作为八个
独立路径参数传给 `mcs251-lld`／`lld -flavor mcs251`。不用 shell 拼接；如使用 response
file，由构建层按驱动语法转义，须与直接 argv 的路径及顺序等价。原清单不是新 lld 输入语法。

3.5 首期全部八对象无条件参与链接，不按未定义符号筛选，不引入隐式库搜索、`.a`、archive
懒提取或 GC。本规范 archive 排除条款维持；未来扩大须另修规范并报 PM。清单、最终有序
输入、工具/对象指纹及实际命令随验收归档。

3.6 生产链纯 ELF；历史 provider 不扩容，不为旧 `.rel` 链补入六个新 harvest 体。
Oracle-B 使用独立 SDCC 库供给，不得以历史 provider 或自研 ELF 对象冒充。
（除法设计 §10-Q4、§11.3）

**4. 与 CRT、XINIT 和栈门禁共存**

4.1 helper 与本规范 §9.3 的独立 ELF `crt-selfstart` fixture 正常共链；不导入 `.rel`
CRT，不另供启动入口、不自动注入 CRT、不修改启动指令。现有"设置 SPX →
`__mcs251_globals_init` 遍历 XINIT → 调用 `_main`"顺序保持。

4.2 helper 无需启动初始化的数据时，**不新增 XINIT 记录、初始化调用或清零循环**。
`_PARM_2` 是调用方在对应调用前完整写入的 NOBITS 参数槽，不因其为静态存储就要求上电
清零，也不得依赖 NOBITS 自动清零。

4.3 上述零负担仅指新增初始化工作，不是零代码、零 RAM 或零栈占用承诺。槽、既有规则要求
的银行预留及其他内部存储仍按本规范 §5.2、§6.3 计入布局与栈高水位。

4.4 应用已有非零初值、零初始化和聚合数据时，原 sparse-XINIT-v1 六字节记录、DSEG slice
目的范围校验及遍历机制原样适用；需要清零的对象不得省略 zero record。若后续 helper 确有
初始化数据需求，也只能使用现有协议，不得另建复制表。

4.5 无任何初始化记录时，仍按本规范 §5.1、§6.1 提供配置的 `s_XINIT` 与 `l_XINIT=0`。
栈门禁的符号请求触发方式、容量至少1024B及公式不变；helper 允许正常栈帧，调用前有效栈
与返回后 SPX 精确恢复按除法设计 §6.3、§8.7 验收。

**5. 参数槽与 overlay 沿用**

5.1 叶/非叶按实际机器函数判定；本期四个 unsigned helper 无调用，槽落带
`SHF_MCS251_OVERLAY` 的 `.mcs251.OSEG.*`；四个 signed 包装层只调用对应 unsigned
helper，槽落独立 `.mcs251.DSEG.*`，无 overlay。（除法设计 §6.2、§6.4–6.5）

5.2 本规范 §4.2、§5.2、§6.1 的 OSEG 语义完整适用：**全链接只有既定的同一 OSEG 组，
组内同址、占用取 max**；helper 与其他叶函数共同参与该组，不另建"运行时组"，不把组内
section 平铺求和。

5.3 helper 叶槽子集的 max 为4B，四个非叶槽独立分配合计12B；实际全链接布局还受其他组
成员、保留洞及既有分配顺序影响，不据此承诺整镜像固定占用16B。非叶槽不得 overlay；本期
不迁 EDATA，不引入新节类型或放置协议。

5.4 静态槽的非重入、递归与中断适用限制不变；采用本案不表示新增重入能力或调用图分析
能力。（本规范 §4.2；模型设计 D.5）

**6. 验收附件与退出条件**

6.1 **除法设计 v4 第8节整体作为本修正案的规范性验收附件**，不随后续草稿静默替换。
条款映射如下；引用不免除任何必测项。

| 本案验收面 | 附件条款及本规范衔接 |
| --- | --- |
| 支持域、负例翻正、i64 拒绝与折叠对照 | 除法设计 §8.1；本案第1条 |
| 合法算术语义、边界表、固定种子向量 | 除法设计 §8.2、§8.4 |
| UB／poison 与 helper 行为观察隔离 | 除法设计 §8.3 |
| 宿主、独立 SDCC Oracle-B、DUT 三方对拍 | 除法设计 §8.5 |
| 八对象身份、符号/槽、RELA、清单及链接负例 | 除法设计 §8.6；本规范第3–7节 |
| 槽访问字节序、嵌套调用、SPX、栈哨兵、活跃值与 DR16 | 除法设计 §8.7 |
| CALLSEQ 槽存储归属、优化级及既有回归 | 除法设计 §8.8 |
| CRT、空/非空 XINIT 与 helper 共链 | 上述 §8.6–8.7，加本规范 §6.2、§9.3、第10节 E3/E4 |

6.2 合法语义固件与 UB 观察使用独立固件、独立 QEMU 会话；必须有真实运行期操作数及真正
i8 IR 执行探针，不以常量折叠或 C 整数提升替代覆盖。对象 NOBITS 的槽宽不能证明访问字节
序，须由调用方存储、被调方读取及非对称字节值执行共同证明。

6.3 共链验收至少覆盖空 XINIT，以及应用 initialized/zero/aggregate 数据与 helper 共存；
核对既有初始化、overlay、栈门禁和稀疏装载语义未被改变。链接负例独立构造重复定义和移除
**实际被引用**的 helper，验证现有 duplicate/undefined 诊断；不得仅以清单预检查代替链接
器负例。

6.4 新算术实现与 SDCC oracle 要求合法输入结果一致，不要求独立实现机器码一致；既有 E4
等价域不因此缩减。输出仍为 ET_EXEC ELF，经 llvm-objcopy 生成 HEX，以稀疏地址→字节域及
完整 QEMU 串口验收，禁止填洞迁就比较。

6.5 退出条件完整沿用除法设计 §11：对象/IR/调用图审计、链接与负例、全部语义及 ABI 向量、
回归、Oracle-B 八符号独立供给和三方对拍、本案审批及适用 ELF 前置审批均须通过。对象生成、
链接成功、串口一行 PASS 均不能单独宣布完成；证据等级声明不当作待办清零，也不得把 QEMU
证据升级为真机证据。

**7. 对象版本与兼容门禁**

7.1 **本案对 ELF v1 对象格式零影响**：首期八对象遵守本规范第3节，维持
`e_flags=0x00000001`、每对象恰好一个既定 `.note.mcs251.abi` 及其八字段值、RELA 0–8、
现有 section/符号/XINIT/overlay 编码；不新增身份字段，不改变调用 ABI 或重定位语义。

7.2 前端的 `layout V2` 数值契约不等于 ELF 对象协议 v2；必须按除法设计 §6.1 同时冻结
前后端契约并核对实际产物，不能凭裸 triple 宣称 ABI v1，也不能改写 header/note 伪装兼容。

7.3 **模型设计 N.1–N.9 的 v2 attributes 身份完全不受本案影响**；三轨载体、编码、Tag、
已冻结值域及仍开放登记项均保持。本案不授权发射候选 v2 身份，不授权现有 v1 读取器接收
v2 对象。

7.4 CRT、调用方与 helper 均须通过所属版本身份校验。v1 严格门禁沿用本规范 §3.2；v2
混链门禁引用模型设计 D.5、N.5、N.6、N.8：v1/v2 不裸混链，默认指针16/32域不因接口只有
整数而豁免；兼容性逐字段判断、能力取并集并检查支持性，合法不同 placement 不作整体字节
比较误拒。

7.5 不提供忽略身份、缺载体回退或双代载体冒充兼容的开关；跨 ABI 须另有显式已审计桥接，
本案不新增桥接。未支持版本或不兼容输入须在布局及固件写出前拒绝。

本文规定与现有 `llc → ASxxxx .rel → mcs251_ld.py → Intel HEX` 并行的
`llc → ELF32/MSB/RELA → lld MCS251 → Intel HEX` 路径。实现采用 LLVM MC、
LLVM Object/BinaryFormat、lld Common 的现代接口；不复制 SDCC 链接器实现，
不调用 Python 生产链接器完成 ELF 链接。兼容的是现有合法输入的可观察语义，
不是 ASxxxx 文本语法、内部数据结构或缺陷。

## 1. 审批决策单

| 事项 | 本提案结论 |
| --- | --- |
| ELF 容器 | ELFCLASS32、ELFDATA2MSB、ET_REL、显式加数 SHT_RELA；CPU 地址仍为 24 位 |
| e_machine | 实验性 `EM_MCS251 = 0x9999`（39321）；不是已获分配的正式编号 |
| 版本 | e_flags 低 8 位为对象 ABI 版本 1，其他位为 0；必需 `.note.mcs251.abi` |
| OSEG | 每个叶函数独立 NOBITS section；`SHF_MCS251_OVERLAY=0x10000000`，属于 SHF_MASKPROC，不占 SHF_MASKOS |
| 数据初始化 | DATA 的运行地址只预留 NOBITS；原样承载稀疏 `.mcs251.xinit` 六字节记录协议 |
| llc 选择 | 新增 `-mcs251-object-format=rel\|elf`，默认 rel；仍使用 `-filetype=obj`；不占用 CPU feature / -mattr |
| lld 接入 | 根目录 `lld/MCS251/` 专用 flavor；命令 `mcs251-lld`，等价 `lld -flavor mcs251` |
| 最终输出 | 【2026-09-06 修正案】ET_EXEC ELF 可执行文件 + 可选 map；Intel HEX 由链中 llvm-objcopy -O ihex 产生 |
| 生产链 | 不改变默认 .rel / asm 字节与诊断；不修改 Python 链接器；不修改 demo-modern 工作目录 |
| 验收 | 同 IR、同模块顺序、同板参数：稀疏地址→字节映射及规范化 HEX 文件均完全一致；QEMU 串口逐文件对照 |

新增专用 flavor 是有意选择：本任务需要哈佛分区、按输入切片分配的 DATA、
OSEG 同址及 CODE 的稀疏洞。只新增 `lld/ELF/Arch/MCS251.cpp` 的指令重定位
函数不足以实现这些规则。首期不把目标规则散布进通用 ELF Writer/LinkerScript，
也不承诺 GNU ld 命令行的完整兼容；接受维护一个小型 lld 驱动的成本。

## 2. 依据与边界

### 2.1 源码基线

E1 核对的 HEAD 为 `3338357b983fe7cc001c80115e497888ae258df1`。
工作树中 Python 链接器及栈测试已有其他任务的未提交修改；因此 HEAD 不足以
唯一标识语义。E1 沙盒保存实际读取文件的 SHA-256 与只读副本，E2/E4 开始时
须重新核对差异，不能静默追随活动工作树。

主要依据（仓库绝对路径）为：

- `C:/Prj/LLVM/MCS251/llvm/lib/Target/MCS251/MCS251TargetMachine.cpp`
- `C:/Prj/LLVM/MCS251/llvm/lib/Target/MCS251/MCS251TargetObjectFile.cpp`
- `C:/Prj/LLVM/MCS251/llvm/lib/Target/MCS251/MCS251AsmPrinter.cpp`
- `C:/Prj/LLVM/MCS251/llvm/lib/Target/MCS251/MCTargetDesc/MCS251RELObjectWriter.cpp`
- `C:/Prj/LLVM/MCS251/llvm/lib/Target/MCS251/MCTargetDesc/MCS251AsmBackend.cpp`
- `C:/Prj/LLVM/MCS251/validation/mcs251-ld/mcs251_ld.py`
- `C:/Prj/LLVM/MCS251/validation/mcs251-firmware/crt-selfstart.asm`
- `C:/Prj/LLVM/MCS251/lld/Common/DriverDispatcher.cpp`

当前仓库是 monorepo：lld 位于 `C:/Prj/LLVM/MCS251/lld`，不存在
`C:/Prj/LLVM/MCS251/llvm/tools/lld`。规范构建树仍为 `/home/liu/build-mcs251`。
E1 只做设计、读取与基线测试，不重新配置该共享构建树。

### 2.2 必须澄清的旧格式语义

1. `0x121` 是 `R_BYTE|R_BYT3|R_PAG0`，取 24 位求和值的低字节，
   **不是 R_BIT 位地址重定位**。当前 Python 路径对此不另做 PAG0 检查。
2. `0x800` 是 J16 控制转移的 area 形式；`0x802` 才是 symbol 形式。
   `0x80A` 是 J11 的 symbol 形式，不是普通 24 位符号地址。
3. llc 还实际发射 `0x101/103、0x181/183、0x381/383`（lo/mid/hi），
   不能只实现背景枚举的七个数值。
4. 栈门禁是**剩余可用栈容量至少 1024 字节**，不是
   `__mcs251_stack_base >= 1024`；空数据时现有 SPX 为 `0x010f`。
5. DATA 不是简单拼成一个连续大 `.bss`：每个 DSEG slice 独立 first-fit，
   可能跨越寄存器/位区保留洞。`s_DSEG/l_DSEG` 是兼容性统计，不是物理边界。
6. VECS 内 `.ds 4` 只占地址、不产 ROM 字节；NOBITS/空洞不能换成四个零。

### 2.3 本战役的等价域

必须覆盖当前 llc 支持的 IR、被验收固件依赖的自研 CRT/辅助模块、下述重定位、
HOME/VECS/BOOT/CSEG/XINIT、DSEG/OSEG、寄存器和位字节保留、ISEG/SSEG、
显式绝对 DATA 预留，以及所用 XDATA 预留。合法输入不得因换容器改变布局。

不承诺完整 SDCC 生态兼容：E2–E4 不新增汇编解析器、LTO、动态链接、GOT/PLT、
TLS、异常展开、COMDAT/weak/common、archive 懒提取、GC/ICF、链接松弛、
GNU 链接脚本语言、ASxxxx P/J19/R_BIT 或 GSINIT* 特殊合并。
这些输入/选项明确报错；不“接受但忽略”。若冻结的验收依赖实际需要其中任一项，
必须先扩充规范并报 PM，不得删用例缩小等价域。

## 3. ELF 文件、版本和 ABI 身份

### 3.1 ELF header

| 字段 | 规定 |
| --- | --- |
| EI_CLASS / EI_DATA / EI_VERSION | ELFCLASS32 / ELFDATA2MSB / EV_CURRENT |
| EI_OSABI / EI_ABIVERSION | ELFOSABI_NONE / 0 |
| e_type / e_machine / e_version | ET_REL / 0x9999 / EV_CURRENT |
| e_flags | `0x00000001`；`EF_MCS251_ABI_VERSION_MASK=0xff`，版本号 1；其余保留为零 |
| e_entry / program headers | 0 / 无；这是可重定位对象，不可直接作为程序加载 |
| 地址域 | ELF 容器字段 32 位；已放置 CPU 地址限制为 `0x000000..0xffffff` |
| sh_addralign | 可分配 section 为 1；不偷偷插入函数、常量或全局对齐填充 |

选择 ELF32 不改变 LLVM DataLayout、near/far 指针宽度或调用约定。
符号数值按所属空间解释，不能仅凭数值认定 CODE/DATA。首期输入 section
`sh_addr` 必须为零；固定物理地址属于链接参数，不以 ET_REL sh_addr 偷渡。

官方机器编号表 `https://gabi.xinuos.com/elf/a-emachine.html` 在 E1 查询中未列出
39321 / EM_MCS251；本 fork 的 ELF.h 也未占用。表中 EM_8051=165，
不得沿用当前 throw-away stub 的 EM_8051。**未列出不保证全球无冲突，
0x9999 也不是官方授予的私有编号段。** 本编号限本 fork 实验使用；正式上游化前
申请编号，届时版本化迁移，不能把旧对象悄悄解释为其他架构。

### 3.2 必需的 `.note.mcs251.abi`

SHT_NOTE、无 SHF_ALLOC、sh_addralign=4；每个对象恰好一个 ABI note。
标准 ELF note 头三个 u32 均为大端：namesz=7、descsz=32、type=1
（本 owner 命名空间中的 `NT_MCS251_ABI`）。owner 为 `MCS251\0`，补齐到 4 字节。
32 字节 descriptor 为八个大端 u32：

| descriptor 偏移 | 值 / 意义 |
| --- | --- |
| 0 | 1：对象 ABI 格式版本，与 e_flags 一致 |
| 4 | 1：调用 ABI major |
| 8 | 0：调用 ABI minor |
| 12 | 2：现有寄存器传参调用变体 |
| 16 | 0x0000f3ff：r0–r9、r12–r15 通用寄存器集合 |
| 20 | 0x00000007：bit0 small model、bit1 静态参数槽、bit2 sparse-XINIT-v1 |
| 24 / 28 | 0 / 0，保留 |

该版本还固定 stack-auto/xstack/intlong-reent/float-reent/all-callee-saves=0、
reg-params=1。以后改变这些属性须定义新 ABI，不复用本 note 值。
此 note 是现有调用 ABI 的结构化身份，不照搬 O 记录字符串及 compiler-build
到新 ELF ABI；不得把器件名作为链接兼容性条件。

lld 默认严格要求 header 与 note 完全匹配已支持版本，不提供绕过开关；缺失、
重复、长度/保留位错误、不同 ABI、不同 endian/class/machine 均在布局前失败。
这比旧链可选 strict 模式更严格，不改变合法 strict 输入的结果。

## 4. 分区、section 与稀疏装载模型

### 4.1 标准 section 映射

所有匹配均区分大小写；后缀只是身份，不做字典序排序。输入对象按命令行顺序，
对象内同一区域的输入 section 按 section-header 序号处理。

| ELF section | type / flags | 逻辑区域及含义 |
| --- | --- | --- |
| `.text` / `.text.*` | PROGBITS / ALLOC,EXECINSTR | CSEG，可执行字节 |
| `.rodata` / `.rodata.*` | PROGBITS / ALLOC | CSEG，只读数据；v1 llc 仍将常量留在 `.text` 保持既有顺序 |
| `.mcs251.dseg`、`.mcs251.DSEG.*` | NOBITS / ALLOC,WRITE | DSEG，一个 section 一个独立分配 slice；分别为可变全局、非叶函数参数槽 |
| `.data` / `.data.*` | NOBITS / ALLOC,WRITE | DSEG，已初始化数据的运行存储；初值必须另在 XINIT，不采用 PROGBITS 的隐式 LMA 模型 |
| `.bss` / `.bss.*` | NOBITS / ALLOC,WRITE | DSEG，零初始化运行存储；运行时清零由 XINIT zero record 表达 |
| `.mcs251.OSEG.*` | NOBITS / ALLOC,WRITE,MCS251_OVERLAY | OSEG，叶函数参数槽 |
| `.mcs251.xinit` / `.mcs251.xinit.*` | PROGBITS / ALLOC | XINIT，连续 ROM 协议记录；不属于 `.data` 的连续复制镜像 |
| `.mcs251.HOME[.*]`、`.mcs251.VECS[.*]`、`.mcs251.BOOT[.*]` | PROGBITS 或 NOBITS / ALLOC,EXECINSTR | 对应 CODE 区域；NOBITS 是占位洞，不装载 |
| `.mcs251.REG_BANK_0[.*]` 至 `_3[.*]` | NOBITS / ALLOC,WRITE,MCS251_OVERLAY | 固定寄存器银行预留，各银行内共享地址 |
| `.mcs251.BSEG_BYTES[.*]` | NOBITS / ALLOC,WRITE | 位寻址区占用的字节预留；不是 ELF 位单位地址 |
| `.mcs251.BIT_BANK[.*]` | NOBITS / ALLOC,WRITE,MCS251_OVERLAY | 位银行的字节预留 |
| `.mcs251.ISEG[.*]`、`.mcs251.SSEG[.*]` | NOBITS / ALLOC,WRITE；SSEG 另带 OVERLAY | 内部间接 DATA / 栈区域预留 |
| `.mcs251.DATA.<id>` | NOBITS / ALLOC,WRITE | 00 段显式普通数据预留，供 EDATA/绝对区边界测试 |
| `.mcs251.XSEG[.*]` | NOBITS / ALLOC,WRITE | XDATA 预留，独立地址空间，不算入内部栈高水位 |
| `.note.mcs251.abi`、symtab/strtab、relocations | 非 ALLOC | 元数据，不进入 HEX |

本表的方括号表示可选后缀，不是实际 section 名字。`.data` 的 NOBITS 规定是
明确的目标 ABI 特例：非空 PROGBITS `.data` 必须拒绝，不能直接把初始化字节烧到 RAM
地址，也不能未经协议声明自动生成另一套复制表。通用 MC 可能预建 size=0 的
PROGBITS `.data`；仅当它无定义符号、无重定位、flags为普通 ALLOC|WRITE 时允许
忽略，不因此迫使修改 REL 默认的 section 初始化行为。v1 llc 不把现有全局重新拆成
.data/.bss，否则改变 DSEG slice 粒度和顺序；继续发 `.mcs251.dseg`。

未知非空 ALLOC section、MERGE/STRINGS/TLS/GROUP/COMPRESSED 等未支持属性
均拒绝。非 ALLOC 元数据只允许 ABI note、标准符号/字符串/RELA、已知无语义的
.comment、空 .note.GNU-stack；调试/其他目标元数据不默默接纳。

### 4.2 OSEG 的规范性表达

`SHF_MCS251_OVERLAY = 0x10000000`，在 `SHF_MASKPROC` 内，含义仅在
EM_MCS251 下成立。不用 SHF_MASKOS，因为 overlay 是目标存储 ABI，不是 OS 策略。

- 必须同时满足规范 section 名和 flag；OSEG 无 flag、flag 放在普通 DSEG/代码上均报错。
- 全链接的所有 `.mcs251.OSEG.*` 属于同一个 OSEG overlay 组；每个 section
  的逻辑偏移从零开始，最后具有相同运行基址 B。
- 组预留大小为 `max(sh_size)`，不是 sum；符号地址为 `B + st_value`。
- 不将 section 合并后再加 concat offset；用 `(输入文件, section index)`
  保持身份，两个文件同名 section 不冲突。同一个模块内的多个叶函数也必须同址。
- 非叶函数静态槽仍是独立 DSEG slice，不能因函数名/可达性分析改成 overlay。
- 寄存器银行、BIT_BANK、SSEG 分别是独立的 overlay 组，不与 OSEG 共组。
- 首期不做 LTO 调用图、递归或中断重入分析；原静态参数 ABI 的适用限制原样保留。

如 OSEG 输入大小 2、5、6，则只占 6 字节，三者 PARM_2 可同址；若 DSEG 输入
大小 6、4，则需两次独立分配，绝不可 overlay。

### 4.3 CODE 空洞：保留地址，但不补零

普通 ELF PROGBITS 无法区分“字节 00”与“没有字节”。不新增位图协议，采用标准
PROGBITS/NOBITS 交错 fragments 表达。比如旧 VECS 每槽 4 字节 EJMP 加 4 字节 .ds：
每槽用 `.mcs251.VECS.<n>.bytes`（PROGBITS，size=4）与
`.mcs251.VECS.<n>.hole`（NOBITS，size=4），按 header 序号交替排列。

分区布局对两者都推进地址；HEX 只输出 PROGBITS。这样 VECS 总跨度仍为 64，
实际装载 32 字节，洞的地址不在镜像中。符号引用指向实际所属 fragment，
RELA offset 始终为机器字节偏移；不要沿用 .rel T 行的膨胀索引。
NOBITS 禁止带需写入字节的重定位。XINIT 禁止这种洞，协议遍历要求连续。

## 5. 布局与链接参数

### 5.1 地址空间与数值参数

内部以 `AddressSpace + uint32_t` 表达地址；用检查过的 64 位中间运算计算末端。
CODE、DATA、XDATA 分开做占用检查；BIT 的物理占用通过 BSEG_BYTES 表达。
不得把合法的 CODE 与 DATA 同数值视为冲突。

新驱动参数（设计接口，E3 后可用）：

- `-o <绝对或用户给定路径>`：输出 **ET_EXEC ELF 可执行文件**（2026-09-06 修正案；原 `--oformat=ihex` 默认直出条款废止，HEX 由链中 llvm-objcopy 产生）。
- `--map=<path>`：确定性的区域、每个 slice 的空间/基址/size/装载洞、符号、
  stack high-water/guard/capacity；可同时提供 `--map-json=<path>` 供对拍。
- `--area-start=<AREA>=<number>`：区域起始参数，对应生产链 `-b AREA=expr` 的数值结果。
- `--iram-size=<number>`：对应 `-I`，默认 128，沿用旧链对 DSEG/ISEG 搜索上限的规则。
- `--edata-end=<number>`：inclusive 上界，默认 `0x0fff`，合法值 `0..0xffff`；
  同时接受 `--edata-end <number>`。它只控制栈容量，不扩张 DSEG first-fit 窗口。
- `--reserve-data=<start>,<size>`：显式绝对内部 DATA 预留，参与避让和栈高水位；
  有符号的 `.mcs251.DATA.<id>` 用同名 `--area-start` 放置。
- `--stack-size=<number>`：显式 SSEG 大小；省略/0 且存在有效 SSEG 时保留旧链
  “最大空闲连续区”规则。不得与动态 EDATA 栈容量概念混淆。
- `@response-file`、`--help`、`--version`；v1 数值接受十进制与 0x 十六进制。

不解析 ASxxxx .lk、不支持 `-b` 的任意符号表达式。验证构建层将冻结配置求为
数值并分别喂给两链；顺序相关或重复冲突的区域设置报错，不静默覆盖。

固件惯用布局是构建层配置，**不是目标代码中硬编码的器件布局**：

| AREA | 验收 profile 起始地址 |
| --- | --- |
| HOME | 0xff0000 |
| VECS | 0xff0003 |
| BOOT | 0xff0100 |
| CSEG | 0xff0200 |
| XINIT | 0xff8000 |

驱动对 CODE/XDATA 区域要求显式起始值，连零长度 XINIT 也在验证 profile 中声明。
DSEG 默认搜索起点 0；REG_BANK_n 固定 n*8；BSEG_BYTES 搜索从 0x20 开始。
不接受把显式 CODE 起始值冲突后自动搬到别处；合法基线不变，冲突输入明确失败。

#### 5.1.1 G13a-S1 FE-bank 验收 profile `g13a-fe640`（2026-09-15 冻结，PM D1）

上表基线之外，G13a 实施轮按 `G13A-CODE-DESIGN-draft.md` rev-2 §3-S1 冻结
FE-bank 配方 `g13a-fe640` 为新验收 profile（构建层 `AREA_ARGS` 配方表，
非链接器常量；上表基线布局保留为 `base` 配方供历史复现）：

| AREA | `g13a-fe640` 起始地址 | 窗口 |
| --- | --- | --- |
| HOME / VECS | 不变（0xff0000 / 0xff0003） | - |
| BOOT | selfstart 不变 0xff0100；IRQ 不变 0xff0500 | - |
| CSEG | selfstart 不变 0xff0200；IRQ 不变 0xff0700 | 65024 / 63744 |
| XINIT | **0xfe0000** | 槽 640B |
| XDATA_INIT | **0xfe0280** | 64896B |

- 推导（两条硬约束联立）：XINIT 槽按双口径留余量——旧世代全树扫描
  （`GAP-G13A-PROBES/xinit-scan.json`，87 section / 76 demo）per-demo
  最大 **258B**；当前工具代验收样本 demo 44 链接实测 **276B**
  （`GAP-G13A-PROBES/results-s1.json`，l_XINIT=0x0114，两种口径不
  混用）；按 2×276=552≤640 取槽 0x280=640B；最大映像 64807B（demo 58/62）
  必须整体留在 FE bank → XDATA_INIT=0xFE0280，映像止于 0xFEFFA7（余 89B）。
- **有效域**：per-demo XINIT 总量 ≤640B 且映像 ≤64896B；越界按实例确定性
  重推导（`XDATA_INIT = 0xFF0000 − 映像` 两遍链接法）或 D6 `--area-end`
  （独立立项，不属本 profile 容量承诺），不静默放大。XINIT/XDATA_INIT
  消费 walker 以 16 位 WR4 载入表长（`crt-xdata-init-walker.asm` /
  `crt-selfstart.asm`），单表上限 65535B；65536B 表端到端未验证，不作容量承诺。
- **ROM 门禁（PM D3）**：本 profile 默认传参 `--flash-base=0xfc2800
  --flash-size=0x3d800`（246K）。这是构建层默认参数，不改变 lld 门禁的
  可选语义（仅在两参数齐备时激活）。门禁含空区域起点检查（配置的
  CODE 类区域起点必须落在窗口内，见 `checkFlashGate`）；其新增拒绝面
  已在实施轮全批重验后登记。
- **放行范围（D1，强制）**：仅限 **G144K246 验收 profile** 及其板级
  Flash/EEPROM 占用前提。G144 手册 ch21（印 710-711/PDF 744-745）：
  "使用 MOV 方式可以读全部 FLASH 区域"，DRx = 基地址 `FC:2800h` +
  EEPROM 目标地址；同章 :252 警告——**板级 ISP EEPROM/IAP 操作区设置
  不得覆盖 FE 数据窗口，IAP 擦除不得以 XINIT/XDATA_INIT 区为目标**，
  镜像烧录链必须保护这两个区域。**K128 不自动继承本 profile**（K128
  ch21:187-207：EEPROM 恒自 FE:0000 起、不能用 MOVC 读取）。真机烧录
  验证为 D2 前置项，QEMU 证据限界表述见 G13A 设计稿 §2.3。

### 5.2 DATA 与 overlay 的顺序规则

先登记绝对预留/位字节预留，再按寄存器银行、BIT_BANK、DSEG、OSEG、ISEG、SSEG
的固定类别次序分配。类别内保留输入文件顺序和 section-header 顺序。
使用小型有界占用表/区间集合，不移植旧链接器的全地址空间 bitmap 及 C wraparound。

- DSEG 搜索起点为 DSEG 区域参数，范围上限等同旧链：若 IRAM 值非法/非正或
  起点+IRAM>0x80，使用 0x80；否则使用起点+IRAM。每个非空 slice 从搜索起点
  找首个足够的连续空洞；不是从前一 slice 尾部开始，不先按大小排序。
- ISEG/SSEG 使用 ISEG 搜索起点，上限公式同上但 cap=0x100。
- OSEG 使用 DSEG 窗口，先求 max size，再 first-fit 一次；旧链按增长逐次重分配
  的最终合法结果应与此一致，E3 用不规则洞样例交叉验证。
- BSEG_BYTES 和 BIT_BANK 使用 `[0x20,0x30)`；前者独立占用、后者组内 max overlay。
- REG_BANK_n 按固定地址预留组内最大尺寸；v1 允许规范的银行容量，冲突/越界失败。
- llc ELF 在有参数槽或可变全局时额外发出 size=8 的 REG_BANK_0 NOBITS section，
  与旧 REL writer 合成 A record 的触发条件相同。无这些存储时不额外占 8 字节。
- `.mcs251.DATA.<id>` 的显式高位内部预留不经过低页 DSEG first-fit，仍参与栈检查。
- XDATA 预留不进入内部 DATA 占用表或 SPX 计算；不自动为它生成 ROM 装载字节。

所有同空间重叠仅对同组 overlay 例外；越界、无法分配、未知类别都使链接失败。
对于旧链仅打印 overlap 警告但仍可能输出的非法输入，新链 fail-closed；E4 差异表
必须把这类诊断增强和合法固件字节差异分开，不将旧 bug 当成新 ABI。

## 6. 符号、XINIT 与栈门禁

### 6.1 符号

- ELF 常规 LOCAL/GLOBAL、UNDEF/ABS/section-defined，FUNC/OBJECT/SECTION/NOTYPE。
  全局强定义重复或未定义强引用失败；不允许 weak/common/IFUNC/特殊绑定冒充支持。
- **保持当前外部名称**：通常 C 名 foo 对应 `_foo`；内部 .L 名、IR 的 `\01`
  原名逃逸保持原行为。运行时的 `__mcs251_stack_base` 等是精确链接符号名，
  不再额外添加下划线。
- 每个 section 的符号 st_value 是该 section 机器字节偏移。RELA 可引用 GLOBAL、
  LOCAL 或 STT_SECTION；区域形式不需要独立 ELF reloc 类型。
- 提供兼容边界名 `s_<AREA>`、`l_<AREA>` 以及 `l_IRAM`。CSEG/HOME/VECS/BOOT/XINIT
  中 l 是包含洞的逻辑跨度，s 是区域首地址；OSEG 的 l 是 max，不是 sum。
- `s_DSEG=0`；`l_DSEG` 复现低 `[0,0x80)` 内已占用字节计数，包含银行/位区/
  OSEG 等占用，不是所有 DSEG size 的简单相加，更不是数据高水位。
  `l_IRAM` 与旧链一致：`0 < iram_size <= 256 ? iram_size : 256`。
- 上述合成边界符号由链接器保留，用户定义冲突报错。空 XINIT 仍提供
  `s_XINIT=<配置基址>`、`l_XINIT=0`；无需伪造 ASxxxx 的 .__.ABS. 符号。

### 6.2 XINIT 协议不变

`.mcs251.xinit` 每条记录按大端编码：

```
u16 destination; u16 object_size; u16 payload_size; u8 payload[payload_size];
```

- `destination` 通过 R_MCS251_16 重定位到对应 DSEG slice 的对象地址。
- `1 <= object_size <= 65535`；payload_size 只能为 0 或 object_size。
- payload=0 代表清零，整条记录仍存在且仅占 6 个 ROM 字节；否则是完整目标端序
  初值，包括结构体/数组布局填充。非零 scalar 和聚合保持当前 llc 发射顺序。
- 记录直接拼接，无头、无终止哨兵、无额外对齐；由 `s_XINIT/l_XINIT` 遍历。
  NOBITS 存储本身不使内存清零，清零不能省掉 XINIT record。
- 重定位完成后，只要 `__mcs251_globals_init` 有定义，执行旧链等价校验：
  总长<=65535；头和 payload 连续存在；无截断；destination+object_size<=0x10000；
  完整范围必须落在**一个已放置 DSEG slice 内**，不能仅检查 DSEG 外包矩形。
- 未链接该运行时的对象仍可包含 XINIT；保持旧链的运行时符号触发方式，不擅自
  注入 CRT 或改变启动流程。v1 note 表明其格式为 sparse-v1；未支持协议不能混用。

### 6.3 `__mcs251_stack_base` 的精确公式

保留输入 UNDEF 请求状态。仅当至少一个输入有该符号的未定义引用时启用门禁；
不能在解析期间合成一个符号，再误把它视为用户覆盖。被请求时任何用户定义都失败。
无请求则不合成、不改变现有布局；为兼容旧链，不对完全无请求的用户定义额外触发门禁。

设 H 为所有非空**字节寻址内部 DATA slice**的 exclusive end 最大值与 0x100 的最大值。
包括 DSEG、OSEG、ISEG、SSEG、寄存器银行、位字节银行及绝对 DATA 原始起点+size；
不包括 CODE、XDATA、抽象 bit 地址。不能使用 `s_DSEG+l_DSEG` 替代 H。

```
H          = max(0x100, each_internal_slice_start + size)
first_byte = align_up(H, 16) + 16
SPX        = first_byte - 1
capacity   = edata_end + 1 - first_byte
```

`capacity < 1024` 时报错，不输出固件；检查运算不溢出、不因 unsigned 下溢通过。
成功时定义 ABS `__mcs251_stack_base=SPX`，输出 H/SPX/capacity/edata_end 诊断。
SPX 是第一次向上压栈前的指针，不是第一可用字节。

必测边界：默认 edata_end=0x0fff 时 H=0x100 得 SPX=0x10f、容量3824；
H=0x0bf0 得 SPX=0x0bff、容量1024，H=0x0bf1 拒绝。
edata_end=0x3fff 时相应边界为 H=0x3bf0/0x3bf1。
这不是器件识别规则，具体板端上界只在构建层传入。

## 7. RELA 类型与旧模式映射

### 7.1 通用规则

重定位采用 Elf32_Rela：r_offset 指向**实际输出机器字节**，r_addend 是 signed 32 位
显式加数。`S` 为解析后的符号值，`A` 为显式加数，`P` 为重定位字段首字节的已放置地址。
内部用足够宽的有符号数计算 `V=S+A`，只在类型指定时截取；不从字段字节再读隐式加数。

对 area 形式，以该输入 slice 的 STT_SECTION 为 S，原目标的 section 内偏移加常量
作为 A；symbol 形式直接使用对应符号和常量。不能把“最终 area 基址”当作所有同名
DSEG section 的公共基址。已定义 global 也可保留 symbol 引用，不要求 ELF 符号表
与 .rel 的 S/A 记录逐条同形。

RELA 对象中的整个地址/立即数字段置零；J11 只清编码地址位，保留 opcode 位。
LO/MID/HI 的字段仅一字节，**不需要 .rel 的三字节 placeholder 和链接缩短**。
未知类型、越过 section、重定位命中 NOBITS、重叠写入位均报错。

### 7.2 类型号（v1 分配）

| 号 | ELF 类型 | 宽度 | 操作及检查 |
| --- | --- | --- | --- |
| 0 | R_MCS251_NONE | 0 | 无操作；不得借此掩盖未知 fixup |
| 1 | R_MCS251_16 | 2 | BE16(V & 0xffff)；允许 near CODE 截去 bank，不能错误要求 FFxxxx<=65535 |
| 2 | R_MCS251_24 | 3 | BE24(V)，要求 0<=V<=0xffffff |
| 3 | R_MCS251_LO8 | 1 | V 的 bits[7:0] |
| 4 | R_MCS251_MID8 | 1 | V 的 bits[15:8] |
| 5 | R_MCS251_HI8 | 1 | V 的 bits[23:16] |
| 6 | R_MCS251_PC8 | 1 | 写 S+A-(P+1)，要求 signed 8 位；本地已解分支通常不留下此 relocation |
| 7 | R_MCS251_J16 | 2 | BE16(V)，要求合法24位 CODE目标且 bank(V)==bank((P+2)&0xffffff) |
| 8 | R_MCS251_J11 | 2 | P 指 opcode；写入低11位地址，保留 opcode 低5位；要求 page2K(V)==page2K((P+2)&0xffffff) |

对 R_MCS251_16 与 LO/MID/HI，先约束 `-0x800000 <= V <= 0xffffff`，再取低24位及
所需字段；这允许现有三字节补码负加数和 near 截断，但拒绝不合理的宽地址。
超过该范围而旧链因32位 wraparound输出的输入，归入非法输入差异，不 silently wrap。
如果边界案例发现现有合法表达式超出此定义，E3 前报 PM 扩展，不能带着歧义实现。
XINIT 对 R16 的目的地址另按第6节验证，不因 R16 可截断放松 DATA 协议边界。

J11 写法明确为 `byte[P] = (byte[P] & 0x1f) | ((V >> 3) & 0xe0)`、
`byte[P+1] = V & 0xff`；只允许对应 ACALL/AJMP opcode 家族，字段的两字节必须连续。
J16 的 P 是 opcode 后的两字节地址字段首字节，因此 P+2 正好为下一条指令位置。
对跨边界控制转移不能用 R16 静默代替 J16；类型由指令语义确定。

### 7.3 背景指定七种 .rel 模式逐项映射

| .rel mode | 经源码核实的含义 | ELF 表达 |
| --- | --- | --- |
| 0x000 | 16位 area base + 字段内 offset/addend | R_MCS251_16，STT_SECTION + A |
| 0x002 | 16位 symbol + addend | R_MCS251_16，symbol + A |
| 0x080 | 24位 area base + offset/addend | R_MCS251_24，STT_SECTION + A |
| 0x082 | 24位 symbol + addend | R_MCS251_24，symbol + A |
| 0x121 | 24位 area值选低字节；PAG0 位在当前路径无额外检查 | R_MCS251_LO8，STT_SECTION + A；不是 R_BIT |
| 0x800 | MCS251 J16 area 重定位，检查下一条指令的64K bank | R_MCS251_J16，STT_SECTION + A |
| 0x80A | MCS251 J11 symbol 重定位，检查下一条指令的2K page | R_MCS251_J11，symbol + A；不是24位字段 |

同时必须覆盖：

| .rel mode 家族 | ELF |
| --- | --- |
| 0x101/0x103、0x121/0x123 | LO8，area/symbol |
| 0x181/0x183 | MID8，area/symbol |
| 0x381/0x383，以及已有 CRT 的 0x301/0x303 | HI8，area/symbol |
| 0x802 | J16 symbol |
| 0x808 | J11 area |
| 0x005/0x007 | PC8 area/symbol（若未在汇编期解析） |

0x301/303 由 R_HIB 决定选高字节，即使没有 R_MSB 也成立。
旧 J11 的 T 行有“地址两字节+opcode一字节”的暂存形态，ELF 不复用此形态；
fixture/对象生成必须将 opcode 移回正常两字节机器编码，重定位 offset 相应修正。
非 MCS251_CONTROL 的旧 J11、J19、R_BIT 及其他组合未定义为 v1 合法输入。

## 8. llc 双格式路径与实现切面（E2）

命令接口示意（E2 才实现）：

```
/home/liu/build-mcs251/bin/llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=rel /home/liu/mcs251-elf-alice/E2/input.ll -o /home/liu/mcs251-elf-alice/E2/input.rel
/home/liu/build-mcs251/bin/llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf /home/liu/mcs251-elf-alice/E2/input.ll -o /home/liu/mcs251-elf-alice/E2/input.o
```

- 不以文件扩展名决定格式；不改变 triple 默认 ELF 容器身份；不发明 `filetype=elf`。
- 格式是每次编译的目标级配置，不是指令集 feature，不能被函数 target-features
  中途改变。无选项和显式 rel 必须得到相同对象。elf 与非 obj 输出的组合明确诊断，
  不假装提供新的 ELF 汇编语法；既有 asm 默认仍为 ASxxxx。
- 新增目标内部 ObjectFormat 枚举/只读选项入口，并显式传到 backend、object lowering
  和 streamer 工厂。格式判断不靠动态猜 writer 类型，不使用可变进程全局作链接状态。
- `MCS251TargetMachine::createMCStreamer` 保留现有 REL 分支，仅 elf 分支构造真正
  ELF writer/streamer。MCTargetDesc 提供 ELF 注册/工厂接线，保证 streamer/writer 成对；
  不直接把目前的 REL streamer hook 与新 ELF writer 混配。特别注意
  MCAsmBackend::createObjectWriter 是非虚函数，不能声称重写它即可返回非ELF的REL：
  llc 的 REL 分支继续显式传入旧 writer；ELF 分支显式调用真正 ELF 工厂。
  通用 MC 注册若增加分派，也必须检查实际调用顺序；本阶段不宣称新增 llvm-mc
  汇编支持（目标尚无 AsmParser），不拿该入口替代 llc 验收。
- 新增 `MCS251ELFObjectWriter.cpp`（MCELFObjectTargetWriter）：machine=0x9999、
  Is64Bit=false、HasRelocationAddend=true、支持明确 fixup→reloc 映射。
  新 ELF streamer 基于 MCELFStreamer，设置 e_flags/ABI note，不吞掉任意 raw text。
- AsmPrinter 在 ELF 对象分支不调用 ASxxxx `.module/.optsdcc/.area` 的 emitRawText；
  真正的 section、symbol、REG_BANK_0 预留和 XINIT 通过 MC API 发射。
  不以“声明支持 raw text 并全部丢弃”隐藏 ELF 错误；inline asm 仍按当前支持域诊断。
- 共享 MCCodeEmitter 保持机器指令字节不变；现有五个目标地址 fixup 直接映射。
  J11/J16 首先在 E3 YAML relocation 测试/CRT fixture 覆盖，不为它们改写现有 llc
  的 ECALL/EJMP 策略。未解 PC8 必须正确携带下一字节偏置，不能重复减1。
- **本 fork 的 MCAssembler 不替 backend 调 recordRelocation**。ELF 未解 fixup
  必须先调用 ELF writer 的 recordRelocation，再用返回的 FixedValue（RELA 通常0）
  写字段；不能沿用 REL 分支“先写 area addend，后记 relocation”的顺序，否则有
  双计加数/残留字段错误。用明确格式分支隔离，REL 分支保持原顺序及字节。
- 增补 ELF.h、ELFRelocs/MCS251.def、Object 的 machine→Triple/格式名/reloc 名称、
  ObjectYAML 的 machine/flags/reloc 映射、llvm-readobj 的显示；否则可写但不可观测
  的“未知机器对象”不算 E2 完成。禁用/拒绝 v1 未支持的 CREL、addrsig 等元数据。

不顺带开放目前拒绝的全局类型、显式 IR section、弱符号或对齐要求。若功能支持域
改变会使 .rel 与 ELF 编译器行为不同，另立后续任务，不纳入本战役偷偷扩展。

## 9. lld 实现、输出与 CRT 闭环（E3）

### 9.1 源码组织

根目录新增 `C:/Prj/LLVM/MCS251/lld/MCS251/`，建议分为 Driver/Options.td、
InputFiles、Symbols、Layout、Relocations、Writer、CMakeLists。
复用 LLVM Object 的 ELF32BE 读取及边界检查、OptTable、MemoryBuffer、Error/Expected、
lld Common 的诊断/生命周期/重入机制。每次链接一个 context，无进程残留符号表。

公共接线涵盖 `lld/Common/DriverDispatcher.cpp`、`lld/include/lld/Common/Driver.h`、
`lld/tools/lld/lld.cpp`、工具 CMake/symlink、顶层 CMake、AsLibAll 驱动及链接依赖。
新增 flavor 枚举放在末尾，不重编号已有项；已有 GNU/COFF/MachO/Wasm 默认不变。
不让 `ld.lld` 自动因机器编号转入未声明的新命令语法。

处理顺序：参数→严格 ELF/ABI 输入校验→强符号/引用记录→区域分类和放置→
合成边界/栈符号→未定义检查→重定位→XINIT校验→稀疏镜像碰撞检查→HEX/map。
有错误不生成新固件；输出先写临时文件，成功后原子替换，保留已有好文件。

构建全走 WSL。在 E3 开始时保存 CMakeCache 后，沿用原参数配置
`-DLLVM_ENABLE_PROJECTS=lld -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=MCS251`，
在 `/home/liu/build-mcs251` 构建 llc、lld、llvm-readobj、yaml2obj、FileCheck。
共享构建树配置变更前向 PM 报节点，不在 E1 执行。

### 9.2 为什么建议直接 HEX

标准 ELF 作为**对象输入**已有工具收益；立即再实现 ET_EXEC/program headers/
LMA 与 Harvard loader policy，对本阶段无必要。更重要的是普通单一 PROGBITS
VECS 会把旧链空洞烧成零；即使正确拆 fragments，llvm-objcopy 的 IHEX 分行长度、
entry record 和 section 边界策略也未必与旧链文本一致。

【2026-09-06 修正案（用户裁定）】本节"直接从稀疏 byte map 发 HEX"的发射器要求
废止；E3 输出 ET_EXEC ELF 可执行文件。要求改为：

- 程序头/段结构镜像稀疏占用：每个连续占用区间一个可装载段；VECS 等空洞不被任何
  段覆盖（不得烧零）；NOBITS（OSEG/DATA 运行区）不产生装载内容。
- Intel HEX 由链中 `llvm-objcopy -O ihex <elf> <hex>` 产生；HEX 文本排版
  （分行长度、type04 位置等）委托 objcopy，不要求与旧链文本一致。
- 验收锚点：objcopy 后的稀疏地址→字节域与预期完全一致（洞≠00/FF）；QEMU 完整
  串口逐行对照。栈门禁/ROM 门禁/容量检查仍在链接器内、在输出前完成。
- 不声称 llvm-objcopy 已验证可用——首条链跑通时按上锚点实测验收。
- e_entry（PM 裁定 2026-09-06）：取 HOME 区域起始数值（--area-start=HOME 或链接
  默认；无 HOME 区域则为 0）。本目标架构入口=复位向量=HOME；纯数值、零符号依赖；
  objcopy/QEMU 路径不消费，纯自描述元信息。

### 9.3 不可遗漏的 CRT/辅助对象桥接

当前 CRT 仍由自研 ASxxxx 汇编源通过 sdas 产 .rel；仅让 llc 产 ELF 后，
**不能把 .rel CRT 塞给 lld**，也不能拿旧链最终 HEX 当第二链的启动输入。

E3 在 `C:/Prj/LLVM/MCS251/validation/mcs251-elf/runtime/` 新增独立的、可复现的
CRT ELF fixture 源（LLVM YAML，yaml2obj 生成），以及必要的辅助对象定义。
使用经过审计的机器指令字节和**未绑定地址的 RELA**，正常保留 BOOT 中 main/
stack/XINIT 引用；VECS 用第4.3节 PROGBITS/NOBITS fragments。
原始 CRT 不修改，板相关常量只在 validation 构建层注入。

fixture 的每个 opcode 区间、符号偏移、relocation 必须与冻结的自研 CRT 源/对象
逐项校验并留指纹；自动转换旧格式最多用于验证和生成一次性参考，**不属于
正式 ELF 链接路径或其运行期依赖**。不得导入旧 Python 链接器来解析/重定位新链。
如需修改 CRT 指令来修 bug，两链字节对等和运行时基线先报 PM，不暗中改新 fixture。

E3 完成条件包括能从这些未链接 ELF 输入独立生成 self-start 固件；不能只用
“无 CRT 的单函数 ELF 成功链接”宣告本阶段完成。

## 10. 阶段测试与汇报门槛

### E1：设计审批

提交本规范、源码/工具指纹、现有 lit 与栈测试基线。明确报告待批准方案和任何
未解决差异；**PM 批准后才进入 E2**。不把设计中的命令当已完成结果。

### E2：对象输出

新增 lit 位于 `C:/Prj/LLVM/MCS251/llvm/test/CodeGen/MCS251/`，同 IR 双 RUN：

- 默认/显式 rel 文本逐字节不变，asm 不变，既有54测试全过。
- ELF header、ABI note、e_flags、RELA 的名称/offset/symbol/addend 可读。
- 16/24位、lo/mid/hi、本地与跨模块符号、正/负 addend、near截断和24位进位。
- initialized/BSS/aggregate globals，XINIT zero record、DSEG多个slice、REG_BANK_0。
- 单模块/跨模块 OSEG max overlay的输入形态；非叶 DSEG 和仅取函数地址不产生伪引用。
- 未支持 fixup、bad option、elf+asm、符号差和未支持元数据的明确错误。

**首个 ELF 对象可由 readobj 完整解读时立即报 PM**；全部对象测试完成再报 E2。

### E3：链接与语义

新增 `C:/Prj/LLVM/MCS251/lld/test/MCS251/`，用 yaml2obj 构造独立输入，
不依赖 llc 成功掩盖 linker 解析 bug；连接 lld lit/test-deps 和 AsLibAll 重入测试。

必测集合：

1. 双文件定义/引用、局部重名、undefined/duplicate、wrong machine/endian/class/note。
2. 九种 reloc 操作，尤其 J16 下一指令跨64K、J11 下一指令跨2K、PC8 -128/127
   成功及两侧溢出、字段越界/重叠、坏 r_sym/r_link/r_info、截断 ELF。
3. DSEG 顺序first-fit、银行和位区洞、OSEG同模块/跨模块同址与max size、非叶不覆盖。
4. 第6节的栈默认/自定义上界/刚好1024/少于1024、绝对预留高水位、无引用不启用、
   用户覆盖拒绝、XDATA/CODE不算入H。
5. XINIT 6字节zero、不同DSEG slice、big-endian多字节、坏size、截断、目的跨slice。
6. VECS 64字节跨度但32字节装载、HEX洞、00/FF segment/type04、32字节分行、
   64K边界、checksum、重复CODE占用拒绝、不产生失败固件。
7. 独立 CRT ELF→self-start HEX、map，与冻结 .rel CRT 的未绑定结构及最终字节对照。

**首次 lld 链接成功立即报 PM**；CRT/布局/门禁与负例全部到位才报 E3 完成。

### E4：对等矩阵

不修改 `C:/Prj/LLVM/MCS251/validation/mcs251-demo-modern`。在 PM/Shizuku 确认的
输入指纹上复制只读快照到 `/home/liu/mcs251-elf-alice/E4`，在那里构建、运行原有
host check和QEMU流程；所有生成物/串口文件均在沙盒。冻结相同 IR，再分别llc输出
rel/ELF，不让两条链各跑一次前端产生潜在不同IR。

| 组 | 编译/链接对象检查 | HEX对等 | 运行时 |
| --- | --- | --- | --- |
| 14特性 demo-modern | 相同IR/模块顺序/CRT/板参数 | 稀疏地址域和所有字节；规范化文件cmp | host期望、两链QEMU串口完全一致 |
| 01-selftest，8项 | 相同QEMU profile IR | 同上 | 两链 SELFTEST-PASS，检查完整串口而非只grep末行 |
| globals代表集 | init/zero/aggregate、多DSEG、lo/mid/hi | 同上 | 初始化和可变存储结果一致 |
| 调用与参数槽代表集 | O0/O2、OSEG/非叶DSEG、跨模块 | 同上 | 现有可执行调用探针一致 |
| 控制转移/地址边界 | 全reloc类型、已解分支/正负加数 | 小型固定gold+旧链参考 | 有可执行harness者运行；纯重定位边界不伪称QEMU覆盖 |
| 错误输入 | LLVM/lld独立lit负例 | 不产新HEX | 不运行非法固件 |
| 全部既有54 lit | 生产默认回归 | 有链接harness者另列，不能把54都称完整固件 | 无harness不虚报运行覆盖 |

QEMU 使用 `/home/liu/mcs251-clang/bin-frozen/6b9edfd0/qemu-system-mcs251`，
机器名/板参数仅出现在沙盒运行命令及 validation 构建层。每次 `-serial file:<path>`，
设置有界退出/超时、保存返回码和完整日志；不得仅依赖控制台偶然显示 PASS。

比较分两层：

1. 严格解析HEX校验和、记录类型、重复地址，比较完整地址集合及其每个字节；
   缺失地址不等于00或FF。这是固件语义的第一裁判。
2. 使用本规范的HEX序列化与旧 Python输出直接cmp；文本分行不一致也记录，
   默认目标为文件字节完全一致，而不是只宣称“功能一样”。

任何差异按 region→input slice→symbol/relocation→单字节定位，保存最小复现；
不能为了让cmp通过修改oracle、填洞或挪用旧链已链接输出。所有差异（包括认为是
旧链缺陷者）逐项报 PM 裁定，未裁定项不记PASS。

**首个双链HEX字节对照结果立即报 PM**，之后逐矩阵批次汇报，不攒最终报告。
最终提交输入/工具SHA、命令、退出码、section/symbol/reloc/map、原始HEX、串口文件和
完整矩阵；不 git commit，由 PM 验收后入库。

## 11. 证据位置与红线

- 源码规范：`C:/Prj/LLVM/MCS251/validation/mcs251-elf/SPEC.md`。
- 沙盒：`/home/liu/mcs251-elf-alice/E1`、`/home/liu/mcs251-elf-alice/E2`、
  `/home/liu/mcs251-elf-alice/E3`、`/home/liu/mcs251-elf-alice/E4`。
- E1 实际输入清单：`/home/liu/mcs251-elf-alice/E1/baseline.json`，
  源码副本在其 `baseline` 子目录；测试输出单独为 `.log`。
- 后续机器可读对拍产物使用 JSON/log；阶段结论直接消息报 PM，不另造分析报告文件。
- 不写入其他任务工作目录，不提交/重置他人的变更；不引入器件型号字符串到 llvm。
  已有旧 ABI 字符串属于现有生产行为，不在本战役“顺手清理”。
- E2 若共享后端修改导致任一默认 .rel 字节、asm或诊断差异，先修复或报批，
  不用“ELF更现代”作为破坏生产链的理由。
