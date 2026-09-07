# mcs251rt —— MCS251 除法/取模运行时（源码目录说明）

- **作者**：Sakuna（编译器运行时工程师）
- **日期**：2026-09-07
- **许可**：Apache-2.0 WITH LLVM-exception（随本 LLVM fork 分发；每个源文件头均载完整声明）
- **规范依据**：除法设计 v4（`validation/mcs251-models/proposals/SIGNED-DIV-REMAINDER-DESIGN.md`，
  §5.4/§6.1/§6.2/§6.4/§7.6/§10/§11.2）；SPEC 2026-09-07 修正案
  （`validation/mcs251-elf/SPEC.md`，第 2 条、第 3 条，已获 PM 审批）
- **状态**：源码已落盘；2026-09-07 按 Alice 复审（REQUEST-CHANGES）完成 R1–R3
  构建草案修正与本目录注释/README 小修，并按窄复审完成三处构建草案修正
  （-D 参数整参数引用、形态 B 工具文件级 DEPENDS、审计标记落盘结果检查），
  八个实现的算法与红线**未改动**。
  仍未构建、未产 IR、未审计产物、未实测语义（构建时隙归 PM；后端接线
  `RuntimeLibcalls.td` / ISelLowering 另行串行，不在本目录任务内）

## 1. 来源声明

### 1.1 独立实现声明

本目录八个实现（`mcs251rt_*.c`）与头文件由 Sakuna **依据公开算法描述独立写出**。
写作过程**未打开、未逐行参照** SDCC 的 `device/lib/_divuint.c` 等源文本、其
`build-smoke` 编译产物（`.asm`/`.rel`），也未参照 compiler-rt、libgcc、newlib 等
既有实现文本。取自 SDCC 侧的只有**除法设计 v4 已批准文本转述的行为契约**：符号命名、
第二参数静态槽 `__<fn>_PARM_2`、余数符号跟随被除数、UB 输入无契约。按 §10-Q1，
SDCC 材料在本工程中的定位仅为行为参考与语义 oracle（Oracle-B），禁止也未曾逐行改写
伪装独立实现。

### 1.2 公开算法出处

核心算法为公共领域的**恢复余数除法**（restoring division，移位-减法/移位-比较），
出处均为公开可复核文献（复审修正 2026-09-07：给出版次与具体章节、固定版本链接）：

- Patterson & Hennessy, *Computer Organization and Design*, 5th ed.,
  Morgan Kaufmann, 2013 —— Chapter 3 "Arithmetic for Computers", §3.4
  "Division"（restoring division 的硬件结构与逐步数值示例）；
- Henry S. Warren, Jr., *Hacker's Delight*, 2nd ed., Addison-Wesley, 2012
  —— Chapter 9 "Integer Division"（多字长除法的软件实现视角与商/余
  不变量的处理）；
- Wikipedia "Division algorithm"，"Slow division methods — Restoring
  division" 小节，引用固定版本
  <https://en.wikipedia.org/w/index.php?title=Division_algorithm&oldid=1371057773>
  （2026-08-24 版；固定链接与"Restoring division"小节标题已于 2026-09-07
  实访问核对）。

**关键独立推导（写入各源文件注释）**：教科书形态使用 2N 位部分余数工作寄存器，
而本库红线禁用 64 位中间量、i32 除法只能用 32 位余数寄存器。本实现依据不变量
"第 i 轮入口处真实部分余数 = 已消费前缀 − 已累计商×除数 ≤ 2^i − 1"（i ≤ N−1）
证明 N 位余数寄存器全域够用（N÷N 除法左移永不溢出），从而在不加宽寄存器、不加
进位特判的前提下保持全域正确。该不变量论证属教科书标准论证的直接应用。

### 1.3 正确性验证（宿主对拍，非交付门）

落盘后以 Python 3.13 逐字镜像八函数 C 语义（32 位中间域 + 定宽截断 + 二补码恢复）
与 Python 大整数真值（无符号商/余、C99 截断除法）对拍，固定种子 20260907：

| 覆盖面 | 检查数 | 结果 |
| --- | --- | --- |
| 16 位无符号：17 个边界除数 × 全量 65536 被除数（含 y=0 跳过断言） | 2097152 | 0 失败 |
| 16 位无符号：17 个边界被除数 × 全量 65536 除数 | 2228224* | 0 失败 |
| 16 位无符号：随机 12 万对 | 240000 | 0 失败 |
| 16 位有符号：边界组合 + [−512,512]×[−17,17] 全扫 + 随机 12 万对 | 480000+ | 0 失败 |
| 32 位无符号：边界矩阵 + 随机 15 万对 | 348000+ | 0 失败 |
| 32 位有符号：边界组合 + 随机 8 万对（首次运行含测试数据自身越界 bug，修正后全过） | 162108 | 0 失败 |
| 设计 §6.1-5/§8.4 代表值：−7÷2=−3、−7 余 2=−1、INT16_MIN÷1=−32768、7 余 −2=+1 等 | 8 | 0 失败 |
| 合计 | ≈546 万 | **0 失败** |

UB 对（除数 0、(INT_MIN, −1)）按 §8.3/§10-Q2 **不设断言**，仅确认算法自然运行、
无任何特判路径。注意：以上是宿主机源级对拍，**不构成**除法设计 §8.2/§8.7 要求的
QEMU 执行级证据。

## 2. 文件清单与冻结签名对照（§6.1 终表）

| 文件 | 符号（目标前缀后） | IR 签名 | PARM_2 槽 | 调用层次/落区（§6.4） |
| --- | --- | --- | --- | --- |
| mcs251rt_div.h | —— 八签名唯一定义点 + 4 条 `_Static_assert`（随每 TU 编译生效） | —— | —— | —— |
| mcs251rt_divuint.c | `__divuint` | `define i16 @_divuint(i16, i16)` | 2B | 叶 / OSEG+OVERLAY |
| mcs251rt_divulong.c | `__divulong` | `define i32 @_divulong(i32, i32)` | 4B | 叶 / OSEG+OVERLAY |
| mcs251rt_divsint.c | `__divsint` | `define i16 @_divsint(i16, i16)` | 2B | 1 层调 divuint / DSEG |
| mcs251rt_divslong.c | `__divslong` | `define i32 @_divslong(i32, i32)` | 4B | 1 层调 divulong / DSEG |
| mcs251rt_moduint.c | `__moduint` | `define i16 @_moduint(i16, i16)` | 2B | 叶 / OSEG+OVERLAY |
| mcs251rt_modulong.c | `__modulong` | `define i32 @_modulong(i32, i32)` | 4B | 叶 / OSEG+OVERLAY |
| mcs251rt_modsint.c | `__modsint` | `define i16 @_modsint(i16, i16)` | 2B | 1 层调 moduint / DSEG |
| mcs251rt_modslong.c | `__modslong` | `define i32 @_modslong(i32, i32)` | 4B | 1 层调 modulong / DSEG |
| CMakeLists.txt | `mcs251rt` target 定义草案（未并入 LLVM 构建图） | —— | —— | —— |

行数（wc -l）：divuint 68 / divulong 68 / moduint 64 / modulong 64 / divsint 76 /
divslong 76 / modsint 82 / modslong 78 / div.h 63 / CMakeLists 334 / README 188。
恢复转换点注释行号：divsint.c:75、divslong.c:71、modsint.c:77、modslong.c:73
（各文件末尾 return 在 79/75/81/77）。

文件名前缀 `mcs251rt_` 为本任务指定（设计 §7.6 曾写 `_divuint.c` 形态，语义等价）；
**函数名与链接符号不受影响**，仍为 §6.1 终表逐字。

## 3. 冻结依赖注释清单对照（§6.1 冻结清单第 5 项）

结果恢复依赖：本链冻结 Clang 对 N=16/32 超范围**无符号→有符号**转换"保留低 N 位、
按二补码解释"（C11 6.3.1.3p3 实现定义）。范围区分：转换到**无符号**类型的取模是
C 标准保证（归一化步不属本项）；仅超范围转换到**有符号**类型是本项记录的实现定义
依赖。

| 检查项 | 要求出处 | 落实位置与状态 |
| --- | --- | --- |
| 冻结 Clang 身份归档（版本+构建 triple/哈希） | §6.1-5、§11.2-6 | 构建期职责（CMake 草案注释已标注；PM 执行） |
| 整数模型 short=16、int=long=32 | §6.1-1、§6.2-1 | `mcs251rt_div.h` 4 条 `_Static_assert` **只锁宽度**（LP64 宿主上 long=64 断言照样通过，不能证明模型身份，复审修正 2026-09-07）；模型身份由 §6.1 冻结清单与构建记录归档保证（与第 6 节风险项一致） |
| 源码注释要求：每处恢复转换点带规定注释 | §6.1-5 | 4 处，逐点在位：divsint.c:75、divslong.c:71、modsint.c:77、modslong.c:73（函数末尾 return 前的注释块；行号为 2026-09-07 复审小修后行号） |
| 恢复转换点数量 | §6.2-4 | 有符号包装层每文件恰 1 处（最终 return）；4 个无符号助手 0 处 |
| 转换检查方式（IR 恢复点形状核对） | §6.1-5、§11.2-6 | 构建审计职责，作用于实际送 llc 的 .ll 原件 |
| 合法负结果回归（−7÷2→−3、−7 余 2→−1、INT16_MIN÷1→−32768 等） | §6.1-5、§8.4 | 验收职责（QEMU 对拍）；宿主级已在 §1.3 预验通过 |
| 重验触发（工具链身份或 §6.1-1..4 编译条件变化） | §6.1-5 | 流程职责，README 与源文件头均已声明依赖该重验链 |

## 4. 红线与禁用构造自查记录（2026-09-07 落盘即查）

- **① 运算符审计**（perl 剥除块注释后扫描全部 9 个 C 文件）：
  残余 `/` = 0、残余 `%` = 0 —— 代码无除法/取模运算符；文件内全部 `/` 均位于
  注释定界符、注释文本或 URL。`grep` 全文：`%`、`int64`、`long long`、`double`、
  `float`、`i64`、四种除取余 IR 操作名（udiv/sdiv/urem/srem 字样）**全部零命中**
  （四种操作名只出现在 CMakeLists.txt 审计模式与本 README 文档中，不在编译源内）。
- **② 调用图**：4 个无符号助手（divuint/divulong/moduint/modulong）函数体**零
  外部调用**（叶）；4 个有符号包装层各**恰一次**调用对应 unsigned helper：
  `divsint→divuint`（divsint.c:59）、`divslong→divulong`（divslong.c:59）、
  `modsint→moduint`（modsint.c:59）、`modslong→modulong`（modslong.c:59）。
  无自环、无互递归、无清单外依赖。（机器级复核仍在 §11.2-4 构建审计。）
- **③ 签名对照**：头文件 8 条声明与 §6.1 终表逐字一致（返回/参数类型、顺序、
  名称）；八个定义与头文件声明一致。
- **禁用构造**：static/全局可变状态 0（八函数全部自动变量，重入限制仅来自 ABI
  静态参数槽）；浮点 0；64 位类型与中间量 0；内联汇编 0；`#pragma` 0。
- **移位纪律**（§6.2-3）：所有移位的左操作数均写显式 `(unsigned)` 转换后再移位
  （uint16_t 提升警示；i32 侧防御整型模型变化）；移位计数仅 1/15/31，均 < 32；
  每轮至多移 1 位；每步算术结果显式截回定宽类型。
- **UB 口径**（§8.3/§10-Q2）：除数零与 (INT_MIN, −1) 无特判、无陷阱、无返回值
  契约；除零时算法自然跑完全部轮次返回确定垃圾值，该行为不是承诺。

## 5. 构建两阶段说明（§11.2；SPEC 修正案 §2.4）

1. **第一阶段**：常规 LLVM 构建产出 `clang` 与 `llc`（含 MCS251 目标；需启用
   clang 项目）。运行时不参与第一阶段。
2. **第二阶段**：独立显式 target `mcs251rt` 调用**同一构建树**刚产出的工具链，
   三步管道（CMakeLists.txt 已实现该骨架）：
   ① 产 IR：`clang --target=mcs251-unknown-none -O2 -S -emit-llvm` 八源 → 八份
   `.ll`（driver 默认 xsmall 契约 `1,2,32,8,1`，§6.1 冻结清单）；
   ② 审计：作用于该批 `.ll` **原件**——四种除取余 IR 操作的子串前哨检查
   （R2 修正：三态语义——grep 命中 0 = 拒绝、零命中 1 = 通过、执行/读取错误
   ≥2 = 拒绝；只有成功检查且零命中才写审计标记，旧草案把"无命中"与"执行
   失败"并入同一分支会误发标记。该检查宁可误报不可漏报，不称"IR 指令计数"，
   权威判定仍是 §6.2-5/§11.2-4 实体口径审计）、i64 实体口径、IR 签名结构与
   ABI 属性核对（后两者为构建审计脚本职责）；
   ③ 交 llc：同一批 `.ll` **原样** `llc -mtriple=mcs251 -O2 -mcs251-object-format=elf
   -filetype=obj` → 八个 `.o`；随后机器级调用图核对作用于 `.o`。
3. **清单**（SPEC §3；R3 修正）：UTF-8 无 BOM、LF、末行有换行、恰好八行、每行
   一个规范化绝对路径；行序固定 `__divuint __divulong __divsint __divslong
   __moduint __modulong __modsint __modslong`。清单是 `mcs251rt` 的**声明产物、
   在构建依赖链内**：作为 add_custom_command 的 OUTPUT（DEPENDS 全部八对象，
   对象又依赖各自审计标记），生成即校验（恰八行、与配置期期望表逐行逐字一致、
   对象存在性），校验失败不写文件——删除清单后重建 target 会恢复清单并重新
   校验；配置期另将源列表与清单顺序逐位锁死（长度恰 8、无重复、逐位一致），
   两份列表不可能静默分叉。正式供给 = 清单（校验通过）+ 全部八对象 + 审计
   成功；消费方按清单行序把八对象作为独立路径参数传给链接器（修正案 §3.4），
   不泛称"target 构建成功"为供给。
4. **隔离**：`mcs251rt` 不进任何 LLVM 库/工具的链接图（无 `target_link_libraries`
   到 LLVM 侧；只用构建依赖表达两阶段顺序，防 `llc → Runtime → llc` 环）；不注册
   ALL，第一阶段常规构建不触碰它。
5. **本期接线位置**（PM 执行；R1 修正——`if(TARGET ...)` 只检查本次配置已声明
   的 target，本树先 lib 后 tools，在后端目录处 TARGET clang/llc 必不可见，
   故分两形态）：
   形态 A（首选）：`llvm/CMakeLists.txt` 末尾（tools 子目录处理之后）加
   `option(LLVM_MCS251_ENABLE_RUNTIME ... OFF)` 守卫的
   `add_subdirectory(lib/Target/MCS251/Runtime)`——此时 in-tree clang/llc
   target 已声明，自动走 `$<TARGET_FILE:...>` + DEPENDS 路径，构建顺序由
   依赖图表达；
   形态 B：仍从 `llvm/lib/Target/MCS251/CMakeLists.txt` 发起
   `add_subdirectory(Runtime)`，但必须配置期传入缓存变量 `MCS251RT_CLANG` /
   `MCS251RT_LLC`（第一阶段二进制绝对路径，配置期校验存在性），两阶段顺序
   由构建者保证（先完成第一阶段构建再构建 mcs251rt）。
   本期未接入；交付口径 = 八个独立 ELF32BE ET_REL `.o` + 清单，**不产 `.a`**
   （lld MCS251 只收单对象，SPEC §3.5 排除 archive）。

## 6. 遗留风险与未竟事项

- 源级正确性已宿主对拍（§1.3），但**全部 QEMU 执行级验收（§8.2/8.4/8.7）、
  Oracle-B 三方对拍（§8.5）、ELF 对象级验收（§8.6）未做**——对象生成 ≠ 链接
  成功 ≠ 串口 PASS ≠ 运行时完成（§11.1）。
- `-O2` 优化器行为未实测：理论上从本循环形状综合出除取余 IR 的可能性极低
  （无常数除数、动态除数），§11.2-2 的 IR 审计门是硬兜底。
- 若整数模型或工具链身份变化（§6.1 第 5 项重验触发），恢复转换依赖须全量重验；
  `_Static_assert` 只锁宽度，不锁模型身份。
- `CMakeLists.txt` 审计/清单步骤的脚本由 CMake 配置期生成、经 `cmake -P` +
  `execute_process` 执行（不经 shell 引号层），属草案实现（构建树在 WSL）；
  接线时可按 PM 的构建审计脚本统一重构；i64 实体口径与签名结构核对尚未
  脚本化。CMake 草案未经 cmake 实跑验证（构建时隙归 PM），接线时以实际
  配置为准。
