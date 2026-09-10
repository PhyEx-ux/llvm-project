# asm2elf — sdas251 .rel → MCS251 ELF 标准衔接路径（E4）

回应 `validation/mcs251-isr/realhw-demo/COMPILER-ASSESSMENT-20260910.md` §7（E4：
手写汇编与 ELF 链接路径缺少顺畅衔接）。

现状问题：sdas251 输出 SDCC `.rel` 格式，C 侧输出 ELF，两者无法一起喂给
mcs251-lld。T10 demo 的 `gen-sentinel.py` 只能先把哨兵汇编经 sdld 链成纯机器码
再 YAML 包装成 ELF——刻意要求内部无外部符号、无绝对跳转，不是通用方案。

本目录提供明确受支持的汇编 → ELF 标准路径：**sdrel2elf.py** 把 sdas251 的
`.rel` 目标文件直接转换成 mcs251-lld 可链接的 ELF32 MSB ET_REL 对象，保留
符号、重定位与 ABI 信息。

## 组成

| 文件 | 说明 |
|---|---|
| `sdrel2elf.py` | `.rel` → ELF 转换器（`sdrel2elf.py <in.rel> -o <out.o> --source-mode source`） |
| `demo/` | 验收 demo：2 个汇编对象 + 2 个 C 对象，跨对象链接 + 静态断言 |
| `demo/run.sh` | 一键端到端：sdas251 → sdrel2elf → clang/llc → mcs251-lld → check.py |
| `demo/check.py` | 对最终镜像的静态断言（调用点、返回帧配对、参数准备、跳转、数据落点、符号与 size）+ 独立 ABI 对照 |
| `demo/cabi.c` | 独立 ABI 对照物：编译器生成的 C caller（`c_add(0x44332211, 0x33445566)`），check.py 据此核对手写汇编的 `_c_add_PARM_2` 槽字节序，防止实现与 oracle 同错 |
| `negative/run_negatives.sh` | 11 个负例（ABI 缺失/损坏、不支持形态、坏 .rel、缺 source-mode 声明）+ 行号/诊断长度断言 |
| `addend/run_addend_checks.sh` | 阻塞 1 / 二至五轮 review 验收：每个用例同时用 sdld 与 转换+lld 链接，逐字节对照（3 个有意分歧用例断言 lld 拒绝） |

## 使用

```sh
sdas251 -los out/liba.rel demo/liba.asm                # 1. 汇编（source 编码模式）
python3 sdrel2elf.py out/liba.rel -o out/liba.o \
    --source-mode source                               # 2. 转 ELF（显式声明编码模式）
lld -flavor mcs251 out/liba.o out/libb.o cunit.o \
    --area-start=CSEG=0xff0000 ... -o out/demo.elf     # 3. 与 C 对象一起链接
```

## .rel 格式权威来源

`.rel`（ASxxxx version 3 / sdld）的语法与重定位语义全部以仓库内 sdcc 源码为
准实现，未做任何猜测：

- `sdcc-upstream/sdas/linksrc/lkrel.c` — 文件识别（`[XDQ][HL][234]` 头）
- `sdcc-upstream/sdas/linksrc/lkmain.c` — 行指令 X/D/Q、H、M、A、S、T、R、P
- `sdcc-upstream/sdas/linksrc/lkarea.c` — `A <name> size <n> flags <n> addr <n>`
  （skip() 消费关键字、eval() 取值）
- `sdcc-upstream/sdas/linksrc/lksym.c` — `S <name> (Def|Ref)<value>`，值为
  最近一个 A 行 area 内偏移（area 之前定义的为绝对值，如内建 SFR 符号）
- `sdcc-upstream/sdas/linksrc/lkrloc3.c` — `T`/`R` 行重定位处理
  （`relr3()`：模式位解码、PCR/页校验、J11 操作码合并、字节选择）
- `sdcc-upstream/sdas/linksrc/aslink.h` — R3_* 模式位与 0xF0 转义前缀
- `sdcc-upstream/sdas/asxxsrc/asout.c` — 汇编器侧发射
  （`write_rmode` 转义、`outrwm` J11 三字节布局、`outr3b` R_C24）
- `sdcc-upstream/sdas/as251/mcs251mch.c` — MCS251 指令编码
  （`out_control16` → R_J16、`out_control11` → R_J11|R_MCS251_CONTROL 等）

实现要点（易错处，均经实机 sdas251 输出验证）：

- 头行接受 `XH3`/`DH3`/`QH3`（sdas 的 `-d`/`-q` listing 选项会改变 .rel 数值
  进制），数值按头声明的进制解析；2/4 字节地址头直接拒绝。
- `R` 行的 `rtp` 索引的是**含 3 字节地址前缀**的 `rtval[]`（`relt3()` 把地址
  存在 `rtval[0..2]`），数据索引 = `rtp - 3`。
- 模式 >0xFF 时按 `write_rmode` 转义：`0xF0|(mode>>8), mode&0xFF`。
- 字节选择类重定位（R_BYT3/R3_BYTX）在 T 行中占 2/3 字节、镜像中只占 1 字节，
  输出偏移按“保留字节的顺序”计算；全字段重定位（16/24 位、J16）保留全部
  T 字节。J11 的第三字节（原始操作码）被隐藏，操作码按
  `((addr_hi & 7) << 5) | opcode` 预合并写入节内容——lld 的
  `applyRelocations()` 从该内容字节取操作码、只重写地址位。
- ELF 侧约束与 `lld/MCS251/LinkerCore.cpp` 一一对应：SHT_RELA（非 SHT_REL）、
  每对象恰好一个 `.note.mcs251.abi`（52 字节，描述字与
  `MCS251ELFStreamer.cpp initSections()` 相同）、唯一 SHT_SYMTAB、
  ALLOC 节对齐 1、节名白名单。

## area → 节映射

| sdas area | ELF 节 | lld 区域 | 符号类型 |
|---|---|---|---|
| `CSEG`（或任意带 `(CODE)` 的 area `<NAME>`） | `.text` / `.text.<NAME>` | CSEG（PROGBITS ALLOC\|EXECINSTR） | STT_FUNC |
| `DSEG` | `.mcs251.dseg` | DSEG（NOBITS ALLOC\|WRITE） | STT_OBJECT |
| `XSEG`（或带 `(XDATA)` 的 area） | `.mcs251.XSEG[.<NAME>]` | XSEG | STT_OBJECT |
| `ISEG` | `.mcs251.ISEG` | ISEG | STT_OBJECT |
| `BSEG` | `.mcs251.BSEG_BYTES` | BSEG_BYTES | STT_OBJECT |

注意 `.mcs251.CSEG` **不是**合法节名（评估文档 §7 记录的正是这个拒绝）；
代码必须映射进 `.text` 系。节名白名单见 `LinkerCore.cpp classifySection()`。
`_CODE` 缺省 area 为空时跳过，非空则报错并提示改用 `.area CSEG (CODE)`。

## 重定位映射表

| .rel 模式（lkrloc3.c） | 典型来源（as251） | ELF 类型 | 语义 |
|---|---|---|---|
| `R3_WORD`（mode 0x00/0x02） | `mov dptr,#sym`（`outrw R_NORM`） | `R_MCS251_16` | 16 位大端绝对，S+A |
| `R_C24`（0x80\|SYM，转义发射） | `ecall/ejmp #sym`（`outr3b`） | `R_MCS251_24` | 24 位大端绝对 |
| `R3_WORD\|R3_BYTX(\|R_MCS251_CONTROL)`（0x08/0x808/0x80A） | `ajmp/acall`（`outrwm`） | `R_MCS251_J11` | 11 位页内跳转，操作码合并，2K 页校验由 lld 执行 |
| `R_J16`（0x800，转义发射） | `ljmp/lcall #sym`（`out_control16`） | `R_MCS251_J16` | 16 位绝对（写低 16 位），64K 区域校验由 lld 执行 |
| `R3_PCR\|R3_BYTE`（可叠加 R_BYT3） | `sjmp/条件分支` 跨 area（`outrb R_PCR`） | `R_MCS251_PC8` | 8 位相对：S+A-(P+1)，±127 由 lld 校验 |
| `R3_BYTE\|R_BYT3`（无选择位） | `mov a,#sym`（`outrb`） | `R_MCS251_LO8` | 取 24 位值 bit[7:0] |
| `R3_BYTE\|R_BYT3\|R3_MSB` | `mov a,#sym>>8` | `R_MCS251_MID8` | bit[15:8] |
| `R3_BYTE\|R_BYT3\|R_HIB` | `mov a,#sym>>16` | `R_MCS251_HI8` | bit[23:16] |
| `R3_BYTE\|R3_BYTX`（16 位选择） | 非 mcs251 路径，兼容保留 | LO8/MID8 | 同上 |
| `R3_PAG0` 叠加位 | `mov dir,...` 直接寻址（`out_direct`） | （叠加在 LO8 上） | setdp 基址恒为 0，语义不变 |

addend 语义（sdld `adb_*` → lld `applyRelocations()` 逐类型推导）：

sdld 的 T 行字段存放的是**表达式的部分值**（asout.c 直接写 `esp->e_addr`
按字段宽截断，未定义符号按 0 计），链接时 `relr3()` 把解析出的基值加进字段
并**按域宽截断**写回（`adb_1b`/`adb_2b`/`adb_3b`，lkrloc.c；
`adb_24_lo/mid/hi`、`adb_lo/hi` 先按全字段加再选字节，lkrloc3.c）。ELF RELA
的"写 S+A"与之等价需要**两层性质，分开陈述**（三轮 review 裁定）：

- **字节同余**：只要 lld 接受该链接，写出字节与 sdld 逐字节一致当且仅当
  A ≡ T 字段（mod 2^W），W 为该类型的域宽——全字段类即 `adb_*` 的加法
  域宽（= lld 的写字宽）；字节选择类即选中字节所依赖的同余模宽
  （LO8/MID8/HI8 分别为 2^8/2^16/2^24，进位只向上传播）。任何同余代表
  都满足这一层。
- **检查语义**：选**哪个**代表决定 lld 的范围检查（`applyRelocations()`：
  `_24` 要求 S+A ∈ [0,2^24)，`_16`/`LO8`/`MID8`/`HI8` 为同一"24 位切片"族、
  要求 S+A ∈ [-0x800000, 0xFFFFFF]，`J16`/`J11` 要求 [0,2^24) 且另有
  bank/2K 页一致性，`PC8` 为 ±127）能否与 sdld 的判定一致（sdld 的
  bank/页/字节 PCR 检查作用于**未截断**中间和；`_24`/`_16` 与字节选择
  **无任何检查**，纯模加）。这一层按类型 × 引用形态细分——五轮 review
  后：数据类中 `_24`/`HI8` 取 24 位二补数，纯切片类（`_16`/`MID8`/
  `LO8`）取**非正域宽代表**（np，两种引用形态一律）；控制/相对类
  （J16/J11/PC8）保留形态区分（见矩阵）。

R 行的 `R3_SYM` 位**只区分基值来源**（置位：`reli = symval()`——外部/全局
符号，asexpr.c 只对未在本模块定义的符号置位，T 字段是可借位的部分表达式；
清零：`reli = a[rindex]->a_addr`——被引用 area 的基址，对象内标签由
asout.c 把 area 内偏移折进 T 字段），它**不蕴含"area 字段均为非负偏移"**：
同 area 标签可参与负偏移表达式（`ecall #(_entry-1)` 产生裸 area 引用、
T=FF FF FF，三轮 review 原例）。可恢复的结构是**地址空间**：MCS251 代码
area 处于 64K CSEG 区域，合法非负 area 偏移 T < 0x10000 < 2^23，故全
24 位域中 bit 23 置位的字段只能来自负表达式按 2^24 回绕（或本就出界的
  常数，sdld 只会静默截断）。最终矩阵（"不变"指维持符号引用的二补数
  解释；**np = 非正域宽代表**：u = T mod 2^(8d)，u=0 时 A=0，否则
  A = u−2^(8d)——同余类在区间 [−(2^(8d)−1), 0] 内的唯一代表，五轮引入）：

| ELF 类型 | adb 域（依据） | area 引用（R3_SYM=0） | 符号引用（R3_SYM=1） |
|---|---|---|---|
| `R_MCS251_24` | `adb_3b`：24 位全字段（lkrloc.c:233、lkrloc3.c:633）；lld 写 24 位、查 [0,2^24) | **sext24** | sext24（不变） |
| `R_MCS251_HI8` | `adb_24_hi` 内部即 `adb_3b`：24 位全字段；`>>16` 由 R_HIB 模式位承载、**不折进 T**（实测 `mov a,#((_entry-1)>>16)` 发射 T=FF FF FF），借位形态可达 | **sext24** | sext24（不变） |
| `R_MCS251_MID8` | `adb_24_mid`（lkrloc3.c:533/1379）：先按 `adb_3b` 全字段加、再清 MSB/LSB 的 rtflg；选中字节只取决于和 mod 2^16 | **np16**（五轮修正） | np16（五轮统一） |
| `R_MCS251_LO8` | `adb_24_lo`（lkrloc3.c:538/1422）：同上加后清高两字节；选中字节只取决于和 mod 2^8 | **np8**（五轮修正） | np8（同） |
| `R_MCS251_16` | `adb_2b`：16 位（lkrloc.c:191、lkrloc3.c:652）；写出字节只取决于 mod 2^16 | **np16**（五轮修正） | np16（同） |
| `R_MCS251_J16` | `adb_2b`（lkrloc3.c:642）；lld 另查 bank 一致 | u16（汇编期已排除负目标） | sext16（见分歧 3） |
| `R_MCS251_J11` | `adb_2b`（lkrloc3.c:569），第三字节是操作码、不参与加法；lld 另查 2K 页 | u16（同上） | sext16（见分歧 3） |
| `R_MCS251_PC8` | `adb_1b`/`adb_24_lo`（=字段宽，lkrloc3.c:553）；lld 查 ±127 | 字段宽无符号 | 字段宽 sext（不变） |

各格依据（字节同余 × 检查语义两层）：

- **`_24`/`HI8` 一律 sext24**：sext24 与 T 字段 mod 2^24 同余——字节同余层
  无条件成立；合法非负偏移 < 2^23 取值不变，负表达式正确恢复（三轮 review
  原例 `ecall #(_entry-1)`：sdld 与 转换+lld 双侧 exit 0、落点 0xFEFFFF
  逐字节一致，用例 `ecall24_area_borrow`）。检查语义层：无符号解释把
  bit23=1 的字段读成 ≥0x800000 的"正偏移"，Value 恒溢出 [0,2^24) 而拒绝
  sdld exit 0 的借位链接；sext24 的落点恰为 sdld 截断写回的同余值。
- **`_16`/`MID8`/`LO8` 非正域宽代表 np（五轮 review 修正，两种引用形态一律）**：
  字节同余层：写出字节只取决于 (S+T) mod M（M = 2^(8d)：`_16`/`MID8`
  为 2^16、`LO8` 为 2^8，进位只向上传播），故 A ≡ T (mod M) 的任何
  代表字节一致，np 满足同余。检查语义层：lld 把这三类型与 `HI8` 归为
  "24 位切片"族，要求 S+A ∈ [-0x800000, 0xFFFFFF]（LinkerCore.cpp
  `applyRelocations()`），sdld 这些路径无任何检查。np 的区间推导
  （对**任意**字段、**任意**节基址 S ∈ [0, 0xFFFFFF] 成立）：
  - 上界：A ≤ 0 且 S ≤ 0xFFFFFF ⇒ S+A ≤ 0xFFFFFF，恒成立；
  - 下界：A ≥ -(M-1) = -65535 且 S ≥ 0 ⇒ S+A ≥ -65535 > -0x800000，
    恒成立。
  故该窗口检查对这三类型**永不触发**：所有 sdld 接受（或静默截断）的
  链接均被接受、字节一致——"所有空间内链接双侧接受"至此才真正成立。
  历史修正：二轮的域宽无符号代表在高基址下把 -1 借位推出窗外（四轮
  复现 A/B）；四轮改用的域宽二补数（sext）被五轮推翻——sext 仅当
  u ≥ M/2 才为负，**深于半域的借位**（-b 回绕为 u = M-b < M/2）被读成
  正偏移，高基址下 S+u 仍溢出（五轮复现，三例真实目标均在空间内、
  sdld 全 exit 0、sext 版 lld 全拒：LO8 `(_entry-129)`@0xFFFFF0 →
  0xFFFF6F；`_16`/`MID8` `(_entry-0x8001)`@0xFF9000 → 0xFF0FFF）。
  np **无需区分正大偏移与负借位**——这正是它优于 sext 之处。该选择
  不可推广到 J16/J11/PC8（bank/2K 页/±127 窗口比较的是**完整目标**，
  残差代表会错）与 `_24`/`HI8`（全字段写出与落点语义，维持三轮
  sext24）。正常/零偏移不回退：`lo8_area_plus`(+0x12)、
  `mid8_area_plus`(+0x1234)、`mov16_area_high`(+0x9ABC)、
  `mid8_area_high`(+0xABCD)、`slice_area_zero`（u=0 → A=0）。
- **`J16`/`J11` area 引用 u16**：mcs251mch.c `out_control16`/
  `out_control11` 在**汇编期**就拒绝同 area 负目标（实测 exit 2），字段
  只能是真实偏移（< 2^16，含 +0x8000 边界 `ljmp16_area_high`）；此时
  sdld 未截断和的 bank/页检查与 lld 的检查逐值相等，双侧 exit 0 且
  逐字节一致。
- **`J16`/`J11` 符号引用 sext16**：恢复跨对象借位（`ljmp16_borrow`：
  sdld 因未截断和进位而告警、字节仍一致）；代价见分歧 3。
- **`PC8`**：同 area 字节分支汇编期已解决（mcs251mch.c `out_relative`）；
  跨 area 的 area 引用 T = 目标 area 内非负偏移，不存在借位形态，无符号
  与二补数同值。

area 引用一律转换为 STT_SECTION 符号 + addend（节符号地址即被引用 area
的基址，与 `relr3()` 的 `reli = a[rindex]->a_addr` 对应）。

保证与分歧（`addend/run_addend_checks.sh`：36 个用例 = 33 个 sdld 与
转换+lld **产物逐字节一致**（其中 `ljmp16_borrow`/`sjmp_pcr` 允许并
验证了 sdld 对未截断中间和的既有告警，其余双侧 exit 0）+ 3 个**有意
分歧**（断言 lld 以明确诊断拒绝））：

- **逐字节一致**：借位/进位/边界/各类型，符号引用与 area 引用两类、
  16 位域与 24 位域各覆盖。含三轮 review 的 C24 area 借位
  （`ecall24_area_borrow`）与正常大偏移（`ecall24_area_plus`，+0x234）、
  二轮的段内 `ljmp #_target` +0x8000 边界（`ljmp16_area_high`）、
  HI8/LO8/MID8 的 area 选择域（含 HI8 借位 `hi8_area_borrow`：sdld 写
  FE，lld 同）、四轮的高基址切片族借位（复现 A
  `mov16_mid8_area_borrow_bankstart`：CSEG=0xFF0400 的 `_16`+MID8；
  复现 B `mov16_area_borrow_prepended`：前置 2 字节对象使第二对象节
  基址 0xFF0002；LO8 顶空 `lo8_area_borrow_topspace`：CSEG=0xFFFF80）
  与真实大偏移不回退（`mov16_area_high` +0x9ABC、`mid8_area_high`
  +0xABCD）、五轮的深于半域借位（`lo8_area_borrow_halfdomain`：
  -129@0xFFFFF0；`mov16_area_borrow_halfdomain`/
  `mid8_area_borrow_halfdomain`：-0x8001@0xFF9000——**普通逐字节一致
  用例，不属于任何歧义/分歧**）与零偏移（`slice_area_zero`：u=0 →
  A=0）。
- **两类不同的"溢出/歧义"问题，明确区分**（四轮裁定、五轮修正表述）：
  1. **section 借位溢出（曾是实现 bug，五轮修复，不是分歧）**：lld 的
     24 位切片窗口 [-0x800000, 0xFFFFFF] 与 sdld 的无检查模加之间的
     差异，曾被代表选择人为放大——二至三轮的无符号代表把高基址下的
     -1 借位推出窗外（四轮复现 A/B）；四轮的域宽二补数代表仍无法
     恢复**深于半域**的借位（五轮复现：-129@0xFFFFF0、-0x8001@
     0xFF9000，真实目标均在空间内）。非正域宽代表（np）修复后，该
     窗口检查对 `_16`/`MID8`/`LO8` **永不触发**，所有链接双侧接受且
     字节一致——上界 A ≤ 0 ∧ S ≤ 0xFFFFFF、下界 A ≥ -65535 ∧ S ≥ 0
     对任意字段/基址成立，此论断现在有完整推导支撑（见矩阵依据）。
  2. **J16 symbol 大常数歧义（格式固有限制，永久声明的分歧 3）**：
     跨对象 J16/J11 字段 T ≥ 0x8000 时，-N 借位与 +0x8000 级常数产生
     **同一字段**，.rel 中信息论上不可恢复；任何代表选择都只能覆盖
     一类。这与借位溢出无关，也不会被任何代表修复。
- **有意分歧（用例固化，如实声明；不声称不存在歧义）**：
  1. **真值超出 24 位域**：如 `ecall #(_target+0x400000)`、area 引用
     `ecall #(_entry+0x123456)`、`mov a,#((_entry+0x120000)>>16)`——
     sdld 静默截断到错误落点，lld 以 `relocation overflow` 拒绝
     （`div_ecall_wide`、`ecall24_area_wide`）。该类对两种引用形态
     同权适用，且**仅存于 `_24`/`HI8`**（全字段写出错误落点、sext24
     落点语义）；纯切片三类型（`_16`/`MID8`/`LO8`）在非正代表下
     **不参与该分歧**——窗口检查永不触发，出界目标按 sdld 的模语义
     接受并写出相同字节。
  2. **C24/HI8 域的 ≥0x800000 常数与负借位不可区分**：实测 sdas **允许**
     `ecall #(_entry+0x800000)`（T=80 00 00，exit 0），它与 `-0x800000`
     的借位产生同一字段，.rel 中无法恢复意图。转换器取 sext24（即 sdld
     的模语义），落点与 sdld 逐字节一致、双侧 exit 0
     （`ecall24_area_hugeconst`：双方都写 7F 00 00）。此形态意图本身
     不可恢复，使用方不应依赖 ≥0x800000 的常数偏移。
  3. **跨对象 J16/J11 字段 T ≥ 0x8000 时意图不可恢复**：同一 T 字段
     既可能是 -N 借位、也可能是 +0x8000 级常数（字节相同）。转换器取
     sext16（恢复借位类，`ljmp16_borrow` 字节一致），因此 lld 的 bank
     检查会拒绝 sdld **静默接受**（无进位、落点正确）的
     `ljmp #(_sym+0x8000)` 常数类（`ljmp16_sym_const_high`，
     "J16 bank overflow"）；改取无符号代表则反转、牺牲借位类——两者
     不可兼得，如实声明为分歧。J11 同理（该类在 sdld 侧本身会触发
     2K 页告警）。这类常数请改写：equ/标号或绝对 `ljmp #常数`。
- **sdld 更严的一侧**（不影响本路径字节输出，仅作对照说明）：sdld 的
  J16/2K 页/字节 PCR 检查作用于未截断的中间和，因此跨对象字节 PCR、
  借位形 J16/J11 常数即使最终落点正确也会被 sdld 告警（对照用例
  `ljmp16_borrow`/`sjmp_pcr` 记录了该告警且字节仍一致）。

`.__.ABS.`/index 0xFFFF 的绝对控制转移被拒绝（见下）。

## 符号与函数大小

- `.rel` 只携带全局符号（`S` 行）：Def → 本对象定义（按所在 area 给
  section+value；代码区 STT_FUNC，数据区 STT_OBJECT；area 之前出现的 Def
  为绝对符号，如内建 SFR 表）；Ref → UND 全局符号，由 mcs251-lld 做常规
  未定义符号解析。
- `.rel` 格式不含大小信息，也**不含局部标号**。代码符号的 st_size 按“到同节
  下一个定义全局符号的距离（最后一个到节尾）”给出——与链接器自身诊断使用的
  有界间隔约定一致。它是**近似值**：只有当布局显式以全局符号为边界（下一个
  `.globl` 恰在函数结束、间隙内无 padding/常量池/内部标签数据，如 demo 的
  `_asm_entry`）时才等于真实函数大小；一般情形间隙中还可能包含非全局代码与
  数据，值偏大。需要精确函数大小时应保证全局边界布局，或在 C 侧由编译器
  `.size` 提供（如 demo 的 `_c_add`）。
- 代码区符号统一标 STT_FUNC 是 `.rel` 的一个限制：S 行不携带逐符号
  类型/大小信息，本转换器把 CODE area 内的定义全局一律声明为函数——
  CSEG 内的**数据**符号（demo 的 `_asm_table` 常量表）因此也带 STT_FUNC
  （check.py 显式断言了这一点），消费方不应把该类型当作可执行性证明。
- 函数大小经 `mcs251-lld --keep-symbols` 传入最终 ELF 符号表与 map 的
  `FUNC <addr> +<size> <name>` 行（demo/check.py 断言两者一致）。

## 输入契约：source 编码模式（必填声明）

sdas251 支持 `.source`/`.binary` 两个伪指令切换**操作码映射**（同一函数在
两种模式下汇编出不同机器码，例如 `mov r4,#0x55; eret` 分别为
`7E 40 55 AA` 与 `7C 55 A5 AA`）。`.rel` 的 XH3 头只记录进制/端序/地址
宽度，**不记录编码模式**；`.rel` 的重定位与符号记录也不携带任何编码身份，
因此转换器**无法自行检测**输入是哪种模式——这不是转换器可以验证的事实，
而是必须由调用方声明受控输入契约：

- `--source-mode source` 为**必填**参数（唯一合法值），缺省或给其他值
  直接拒绝（negative N11）。含义：调用方保证 `.rel` 来自默认 source
  编码模式（或源码中有显式 `.source`），`.binary` 模式模块不得进入本
  路径、不得链入 source 模式 MCS251 镜像。
- 注入的 `.note.mcs251.abi` 是**该声明之下的身份标记**，不是转换器对
  调用约定的验证。若把 `.binary` 对象误传进来，转换照常成功、note 照常
  注入——误用不可检测，只能靠流程约束（本仓库所有脚本都显式传
  `--source-mode source`）。
- N1/N2 证明的是 **lld 会拒绝缺失/损坏的目标 ELF note**；这与“转换器
  发现源 `.rel` 的 ABI/编码不匹配”是两回事——后者 `.rel` 根本不携带
  该信息，本转换器不做此声称。

## ABI 信息

在 `--source-mode source` 声明之下（见上节），每个转换出的对象注入与 llc
完全一致的 `.note.mcs251.abi`
（namesz=7 `"MCS251\0"`、descsz=32、type=1、8 个描述字，共 52 字节；布局见
`MCS251ELFStreamer.cpp initSections()`，校验见 `LinkerCore.cpp
validateNote()`）。链接时每个输入对象都必须携带且匹配——缺失或描述字不符
都会被 mcs251-lld 以明确诊断拒绝（negative N1/N2）。

`--no-abi-note` / `--corrupt-abi-note` 是仅用于负例验证的转换器开关，正常
流程不要使用。

## 明确不支持（如实声明）

- **可重定位数据初始化**：DSEG/XSEG/ISEG/BSEG area 内出现 `T` 数据（如
  `.db`）即拒绝——lld 将这些区域映射为 NOBITS 保留，无 ROM 字节；常量表放
  CSEG（`movc` 读取，demo 的 `_asm_table` 即此形态）。带初值的全局变量
  初始化属 C 侧 XINIT/CRT 职责。
- **位寻址重定位（R_BIT）**：lld 无位空间重定位；bit 区域（`.area BIT`）
  与 bit 引用直接拒绝。
- **绝对（常数）ajmp/acall 目标**：以 `.__.ABS.`/0xFFFF 引用出现；lld 要求
  11/16 位控制转移目标是 CODE 符号。`ljmp/lcall #常数` 生成最终字节，不受限。
- **setdp（P 行）/非零直接页**：mcs251-lld 无 setdp 概念；仅支持直接页 0。
- **R3_J19（DS80C390 19 位跳转）**、16 位 PC 相对（非 MCS251 形态）、以及
  含未知模式位的记录：拒绝。
- **绝对 area（ABS）、overlay area（OVR）、带非零基址的 area**：ET_REL 无
  定位语义；用 mcs251-lld 的 `--area-start` 放置。
- **局部标号**：.rel 不携带局部符号，无法恢复（demo 的 `_asm_back` 即示例，
  静态断言改为验证数值落点）。需要符号可见性时使用 `.globl`。
- **库格式**（`;!FILE`/sdcclib 包装）、多模块单文件（多个 `H`）、2/4 字节
  地址头（XH2/XH4…）：拒绝。
- **调试信息**：.rel 不含源码位置/DWARF，本路径不生成调试节（评估文档要求
  中的“调试位置”一项不在本实现范围，与 E5 的既有结论一致）。
- **ISR 注册**：`.mcs251.isr` 元数据与 type9 关联在 .rel 中无表示；R3 规则
  要求 ISR 身份与元数据同对象，因此手写汇编对象不能作为注册 ISR 入口，但
  可以被 C/CRT 侧注册的 ISR 正常调用。demo 因此是非 IRQ 链接，“符号与向量
  一致”一项不适用（无合成向量表），由调用点/符号一致性断言覆盖。

## 与 gen-sentinel.py 的关系

`realhw-demo/gen-sentinel.py`（sdld 预链接 + 机器码 YAML 包装）保留用于其
原始场景：**无符号、无重定位**的哨兵汇编，其输出字节受 release manifest
SHA256 冻结，不宜改动。新代码应使用本路径（sdrel2elf.py）；两条路径生成的
ABI note 完全一致，可在同一次链接中混合（demo 即为 sdrel2elf 对象与
yaml/llc 对象混合链接的实例——demo 中 C 对象由 llc 产生，与 sentinel YAML
对象同级可互换）。

## 验收对照（评估文档 §7）

| 要求 | 覆盖 |
|---|---|
| 跨对象调用 | demo：`lcall #_asm_helper_b`（asm→asm J16，near 帧）、`ecall #_c_add`（asm→C，24 位，extended 帧）、`ecall #_asm_helper_x`（24 位）；check.py 验证调用点字段 == 最终符号地址 |
| 调用/返回帧配对 | check.py 断言：每个 ecall 目标体以 ERET（0xAA）结束、每个 lcall/acall 目标体以 RET（0x22）结束，且无任何函数被两种帧混调；asm→C 调用按 `int c_add(int,int)` ABI 准备参数（arg0 → 寄存器通道 DPL:DPH:B:A **低位在前**；arg1 → 静态槽 `_c_add_PARM_2` **内存大端**：0x33445566 按地址升序为 33 44 55 66，两回事，见 `liba.asm` 注释） |
| 独立 ABI 对照 | `demo/cabi.c`（编译器生成的 C caller，做与 liba.asm 相同的 `c_add(0x44332211, 0x33445566)` 调用）：check.py 从 cabi.o 自身的重定位解码其槽写入，断言等于文档化的大端值、且与 liba.asm 手写存储逐字节相同——手写实现与静态期望不可能一起悄悄写错字节序 |
| 绝对跳转 | `ljmp #_asm_back`（J16 绝对、区域校验）；`ecall` 24 位绝对 |
| 相对跳转 | `sjmp _asm_resume` 跨对象 PC8（位移 == 目标-(P+1)）；`ajmp` 跨对象 J11 含 2K 页校验 |
| 数据引用 | CSEG 常量表（16 位 + movc）、C 全局（DSEG，16 位与直接 8 位两条路径）、XSEG 保留（16 位） |
| 函数大小 | `.rel` 间隙大小 → 输入 symtab → `--keep-symbols` 最终 ELF 与 map FUNC 行，check.py 三方一致 |
| addend 算术 | `addend/run_addend_checks.sh`：36 个用例（借位/进位/边界/各类型；符号引用与 area 引用、16 位域与 24 位域各覆盖）——33 个 sdld 与 转换+lld 产物逐字节一致（其中 `ljmp16_borrow`/`sjmp_pcr` 允许并验证了 sdld 告警，其余双侧 exit 0），含三轮 review 的 C24 area 借位（`ecall24_area_borrow`，Alice 原例 `ecall #(_entry-1)`）、二轮的段内 `ljmp #_target` +0x8000 边界（`ljmp16_area_high`）、HI8/LO8/MID8 area 选择域、四轮的高基址切片族借位（复现 A `mov16_mid8_area_borrow_bankstart`、复现 B `mov16_area_borrow_prepended`、LO8 顶空 `lo8_area_borrow_topspace`、真实大偏移 `mov16_area_high`/`mid8_area_high`）与五轮的深于半域借位（`lo8_area_borrow_halfdomain`、`mov16_area_borrow_halfdomain`、`mid8_area_borrow_halfdomain`）及零偏移（`slice_area_zero`）；3 个有意分歧断言 lld 拒绝（真值超 24 位域 ×2、J16 常数歧义 ×1，见"addend 语义"） |
| ABI 不匹配诊断 | N1（缺 note）/N2（坏描述字）→ mcs251-lld 明确报错 |
| 负例清晰报错 | N3–N11（不支持形态、坏 .rel、缺 source-mode 声明）：转换器诊断全部带 `文件:行号` 前缀且可行动（run_negatives.sh 断言行号与诊断长度）；N1/N2/N11 为链接器/用法错误，格式由各自工具决定 |

运行：`demo/run.sh <out-dir>`、`negative/run_negatives.sh <out-dir>` 与
`addend/run_addend_checks.sh <out-dir>`。
