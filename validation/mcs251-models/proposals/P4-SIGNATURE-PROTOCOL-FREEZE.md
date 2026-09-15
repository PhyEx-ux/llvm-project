# P-4 签名记录线格式冻结提案（按 P07 流程冻结后实施）

状态：**已冻结**（r3；Alice 三轮复审——一轮 9 项 CHANGES、二轮 6 项精修、
三轮 APPROVED，2026-09-14）。PM 裁定 #3 录于下节。writer/reader 实施批次按本
文件执行。

## 实施记录（2026-09-14，冻结后同批落地）

线格式与拒绝规则按上文冻结内容实现，writer/reader 同批交付：

- **共享编解码**：新增 `llvm/include/llvm/BinaryFormat/MCS251Signatures.h` 与
  `llvm/lib/BinaryFormat/MCS251Signatures.cpp`（严格 decoder、自检 encoder、
  `compareRecords` 冻结比较语义、bitmap/role 辅助、`!mcs251.signatures`
  元数据线格式常量）。
- **writer**：`clang` 在 `Targets/MCS251.cpp::emitTargetMetadata` 从
  CGFunctionInfo/FunctionDecl 产生 `!mcs251.signatures`（源类型 `bit` 判定，
  不得从 i8 反猜）；`llc` 在 `MCS251AsmPrinter` 读取该元数据、校验完整覆盖、
  对后端生成的外部 libcall 按 `MCS251HelperABI.h` 登记 ABI 自动补记录（未登记
  ABI 硬错），并经 `renderRegisteredIdentity(..., Signatures)` 发射 Tag 28。
- **reader（lld 三级时机）**：`MCS251Attributes::decode` 内严格解码 + call_abi
  与对象身份核对；`validateFileSignatures` 在 `resolveSymbols` 后做对象内符号
  关联（bit0 必须为本对象定义、函数入口按 STT_FUNC 或"链接级被代码重定位引用"
  的 STT_NOTYPE 识别；bit1 允许无符号条目，有则为外部未定义）；`validateSignatureSet`
  做跨对象逐字段核对与逐对象缺条目检查，均在布局前完成。Tag 28 不加入
  `V2ComparedTags`。
- **夹具影响面**：`llvm/test/CodeGen/MCS251` 全部 v2 ELF 输出的 .ll/.mir 补
  `!mcs251.signatures`；`lld/test/MCS251/v2-object-identity.test` 与
  `crt-v2.test` 的全部 carrier 补 Tag 28（含畸形单故障基线，原故障不变）；
  `crt-selfstart-v2.yaml`/`crt-irq-v2.yaml` 携带真实函数记录；
  `check-crt-v2.py` 改为按 Tag 28 内部格式独立验证各 CRT 的签名集与变长载体。
- **验证**：`BinaryFormatTests` MCS251 套件 53/53；`CodeGen/MCS251` lit 149/149；
  `v2-object-identity.test` 140 条 RUN、`crt-v2.test` 33 条 RUN 全通过。

## PM 裁定 #3（2026-09-14，用户四项选择）

| # | 裁定点 | 裁定 |
|---|---|---|
| 1 | 载体 | `.mcs251.attributes` 扩展新 Tag（不新建节类型，不占 reloc/能力/e_flags） |
| 2 | 覆盖范围 | 全部 TU 强制发射签名记录 |
| 3 | 缺记录策略 | 一律硬错（范围内函数缺签名记录即拒绝） |
| 4 | 冲突检出时机 | writer 与 reader 同批交付，lld 硬错同名冲突 |

## 范围与兼容性收窄（裁定 #3 的直接推论，随裁定生效登记）

- 签名强制域为 **v2 身份对象**：签名 Tag 活在 `.mcs251.attributes` 载体内，仅
  v2 对象携带；v1 对象（note 身份）无载体、不参与核对、照常链接，且**不获得
  P-4 签名验证保证**（规范 :363 的兼容语义按此收窄登记）。
- **P-4 生效前产生的 v2 对象（无本 Tag）将被 reader 拒绝，必须以新工具重建**；
  受影响夹具见"夹具影响面"。v1 资产一字节不改。
- v1/v2 混链已被 W4 拒绝（LinkerCore.cpp:1632-1650），无跨代核对歧义。

## 外层线格式（TLV 登记）

**Tag 28 `MCS251_TAG_FUNCTION_SIGNATURES`**：File scope=1，ValueType=VT_BYTES
(0x04)，Critical=1，TypeFlags=0x84；每个 v2 对象**恰一条**，加入 RequiredTags。
- 值可含内嵌 NUL（Writer::addBytes 支持），writer 调用必须显式 Critical=true
  （MCS251AttributesWriter.h:62-63 默认 false）。
- 最小编码器扩展：登记表 Tag 28/名称/末号（27→28）、RequiredTags 追加、BYTES
  schema 分支（现有缺失检查与未知 Critical 拒绝见 MCS251Attributes.cpp:592-602,
  694-703；仅改末号会落入"未实现 schema"拒绝分支）。
- **旧 reader 遇 Critical Tag 28 必须明确拒绝**；既有未知节/能力/reloc 拒绝
  纪律不变。
- Tag 28 **不加入** lld 的全对象 U32 相等比较表（V2ComparedTags），不比较整块
  签名数组，也不要求 placement 相同；签名一致性由专用跨对象核对执行。

## 内部值格式（FREEZE）

字节序：**仅本 Tag 内部的多字节字段固定小端（LE）**；属性信封与既有 U32 记录
仍为大端（BE）。对齐 1。

```
Value ::= 头 记录* blob
头    ::= version:u8(=1) flags:u8(保留=0) count:u16le
记录  ::= name_off:u32le role:u8 param_count:u8 bitmap:ceil(n/8)B
          ret:u8 call_abi_major:u8 call_abi_minor:u8     -- n=param_count
blob  ::= 全部记录之后；count 个以 NUL 结尾的名字串顺序串接
```

- 记录长度为**隐式**：9 + ceil(param_count/8)，由 param_count 完全决定；不设
  显式长度字段（避免长度/内容不一致的自洽性攻击面）。
- bitmap 位序：`bitmap[i/8]` 的 `bit(i%8)` 对应源参数位置 i（0 起）；超出
  param_count 的尾位必须 0。
- name_off 语义：blob 内**字符串首字节**偏移；指向串中非首字节、空名、blob
  尾部多余字节、同名（串内容相等）多条记录，一律拒绝。
- 空集合：完整 Value = `01 00 00 00`（version=1, flags=0, count=0，blob 为空），
  **不得以零长度 Value 替代**。
- role 位（其余保留 0）；**合法组合约束（decoder 验证）：`role&3` 只能为 1
  （仅定义）或 2（仅声明/引用），0 与 3 均拒绝**：
  - bit0 has-definition：本 TU 有该函数定义；
  - bit1 declared-not-defined：本 TU 仅声明/引用（含未用声明、被优化删除的
    引用——这类记录允许在符号表中无对应条目；**若存在符号条目，必须为外部
    未定义函数**，不得借 bit1 记录绕过定义关联检查）；
  - bit2 no-prototype：K&R 无原型（bit 参数已被 Sema N14 拒绝）。本条原冻结
    文本（"bit2=1 时 param_count=0、bitmap 为空、bit3=0，均由 decoder 验证；
    比较语义：两侧 bit2 不同即冲突"）**已被下方"零参数协议修订
    （2026-09-15）"节替代**，被替代的是三项规则：① "bit2=1 ⟹ param_count=0"
    （废除：bit2=1 允许携带真实 param_count>0）；② "bitmap 为空"（改述：bitmap
    按 param_count 照常编码，位必须全 0）；③ "两侧 bit2 不同即冲突"（收窄：
    双方 param_count 均为 0 时豁免）。未被替代、继续有效的部分：bit3=0 由
    decoder 验证；两侧均为 1 只比较 (ret, call_abi)；两侧均为 0 比较
    (param_count, bitmap, ret, call_abi, bit3)。跨对象同名记录集合按修订节的
    **全对比较**核对（兼容关系不传递）。
  - bit3 variadic：变参（bit 参数已被 Sema N13 拒绝）；**param_count 仅计固定
    形参，不含省略号**；两侧 bit3 一致方可比。
- ret ∈ {0,1}；call_abi_major/minor 必须与本对象身份的 CallABI 代一致。
- 记录粒度：本 TU 源码**声明或定义的全部外部链接函数**（普通函数同等记录，
  即"已知 byte"）；local/internal 符号不记录（已决，不再 OPEN）。

## 签名的 IR 层保留（跨 opt/llc 不丢源类型）

- clang 在 module 挂 named metadata `!mcs251.signatures`：每外部函数一条
  （ELF 名、role、param_count、bitmap、ret、原型性），由 CodeGen 从
  CGFunctionInfo/FunctionDecl（未丢源类型）产生；**不得从 i8 签名反猜**。
  clang 在发射前校验**全部**源外部声明/定义均已登记（空 metadata 不替代完整
  覆盖）；llc 对最终 module 中仍存在的范围内函数逐项核对，缺项或畸形 metadata
  在对象发射前硬错。
- 后端生成的外部 libcall（如现有除法 lowering 生成的外部 helper）按**已登记
  helper ABI** 自动补签名记录；未登记 ABI 的外部 libcall 硬错。除此之外仅
  local/internal helper 排除。
- llc 发射 v2 ELF 时读取该 metadata 转 Tag 28；无 metadata 的 module 产出 v2
  对象即缺 Tag → 硬错（手写 IR 作者须显式提供 metadata，这是裁定 #3 的真实
  代价）。编译器生成内部 helper 不在外部链接域，不记录。
- 名字**逐字节等于最终 ELF 符号名**：复用 LLVM Mangler/getSymbol 产生（含目标
  名字前缀——当前 `m:s` 会给普通 C 名加 `_`；**不得使用裸源名或二次加前缀**），
  并遵守 asm-label 的 `\01` 免修饰规则，与 lld 所见一致。

## 拒绝规则（fail-closed 全套，reader 侧）

对象级（严格 decoder，validateV2Identity 内）：version != 1；头/role 保留位非
0；count 与实际记录数不符；截断；重复 Tag；TypeFlags 非 0x84；name_off 越界/
非串首/空名/串内容重复；bitmap 越界位非 0；ret∉{0,1}；call_abi 与对象身份不符；
v2 对象缺失本 Tag。

对象内符号关联（loadFile 完成符号表加载后执行）：bit0 记录的函数名必须存在
于本对象符号表且为定义（st_shndx ≠ UND；函数入口按 STT_FUNC **或** STT_NOTYPE
识别，但 NOTYPE 判定须**区分函数与数据**——CRT 的 `_main` 引用与函数入口是
NOTYPE，栈/区间边界等 NOTYPE 数据符号不得当函数；判定边界：名字在签名域内
且被函数重定位引用）；bit1 记录允许无符号表条目，有则必须为外部未定义。

跨对象（validateIdentitySet 通过后、resolveSymbols 附近、布局前）：同名多条
记录按上文 bit2/bit3 比较语义执行，任一分歧硬错；缺条目检查**逐对象**进行——
每个对象核对其外部函数定义及可识别的外部函数声明/引用均在自身 Tag 28 内有条
目（**其他对象的同名记录不得补足本对象缺项**）；count=0 仅在该对象上述域为空
时合法。

## writer/reader 分工（同批）

- writer：clang 收集 + `!mcs251.signatures`；llc metadata→Tag 28（经
  MCS251AttributesWriter；AsmPrinter.cpp:1592-1620 组装/decode 与
  MCELFStreamer.cpp:626-642 原样发射路径不变，仅新增记录种类）。
- reader：lld 三级时机如上；诊断文案冻结后列入 N 系。
- 测试：真实两 TU golden（正/参数位冲突/返回冲突/ABI 代冲突/K&R vs 原型/
  双无原型/变参）；变异矩阵（version、保留位、count、bitmap、name_off、空名、
  串重复、截断、缺 Tag、count=0 逃逸、**role 非法组合（0 与 3）**、
  **仅引用对象缺条目**、**名字前缀错误（裸源名/二次前缀）与 asm-label**、
  **NOTYPE 数据符号误判排除**、NOTYPE 函数入口正例）；v1 对象照常链；opt 全
  管线（clang|opt|llc）签名存续；手写 IR 无 metadata 硬错。

## 零参数协议修订（2026-09-15，复审后修订，替代上文 bit2 冻结条款中的三项规则）

上文"内部值格式"bit2 条款中被本节替代的是**三项**规则（方案 B 首版把 writer 的
ParamCount 在无原型时强制写 0，置位条件 `NoProto && ParamCount != 0` 因此恒假
——bit2 门整体失效：K&R `int f(x) int x;` 被写为 prototyped 零参，与另一 TU 的
`int f(void)` 链接成功；复审后修订）：

1. "bit2=1 ⟹ param_count=0"——废除（bit2=1 允许携带真实 param_count>0）；
2. "bitmap 为空"——改述（bitmap 按 param_count 照常编码，位必须全 0，见下）；
3. "两侧 bit2 不同即冲突"——收窄（双方 param_count 均为 0 时豁免，否则冲突）。

修订后：

- **writer（clang）**：`ParamCount = FD->getNumParams()` 恒取精确计数（K&R 定义的
  形参表即源真值）；bit2 恒按 `NoProto` 置位，不再做参数个数分支。
- **wire**：bit2=1 的记录**允许携带 param_count>0**；仍要求 bitmap 全 0（K&R 形参
  不得为 `__bit`，Sema N14）与 bit3=0。decoder（BinaryFormat）与 llc 的
  `!mcs251.signatures` 校验同步放宽/补检。
- **reader 比较语义（修订，非原冻结语义）**：兼容关系按**记录对**判定，且**不
  传递**——零参豁免使 A（`int f();`）与 B（K&R 带参定义）、A 与 C
  （`int f(void);`）分别兼容，而 B-C 仍冲突；A 的在场不豁免 B-C。因此同名记录
  的跨对象核对必须**全对比较**（lld `validateSignatureSet` 按符号名分组后对组内
  全部记录两两 compareRecords，不得以"首条记录"为比较基准），任何一对冲突即
  拒绝，与命令行对象顺序无关。逐对判定：bit2 差异且双方 param_count=0 → 豁免
  prototype-ness 冲突，仍比较 ret、call_abi、bit3（demo 76 的 `void main()` 定义
  × CRT `void main(void)` 声明照常链接）；bit2 差异且任一方带参 → 冲突；
  双 bit2=1 → 只比 (ret, call_abi)。
- **诊断文案统一**：decoder（BinaryFormat）与 llc AsmPrinter 的 bit2 违规文案
  逐字一致："a no-prototype record cannot also be variadic" 与
  "a no-prototype record cannot set a __bit bit-ness for source parameter N
  (K&R parameters cannot be __bit)"。
- 回归：`validation/mcs251-elf/signature-zeroparam-e2e.sh`（六链路判定 +
  三对象六排列全拒 + 两组全对兼容对照 + 零参记录计数变异负例 + 13 条完整节点
  IR 钉死）；lld/test/MCS251/signature-protocol.test 同名三记录六排列全拒 +
  两组全对兼容对照同步。

## 夹具影响面（Alice 复核清单，实施批次执行）

- lld/test/MCS251/v2-object-identity.test：36 个带载体分段中 16 个必须补齐
  Tag 28 才维持原验收（xs、sm、tiny、large、unknown-optional、optmix、
  abi-options-nonzero、isrv2、oseg-a/b、resv21/22/23-opt、caplo、caphi、
  abiminor）；其余为单故障变异基线，同步补齐但**不得修掉原故障**。
- lld/test/MCS251/crt-v2.test:99,135 两个 v2 分段补齐。
- validation/mcs251-elf/runtime/crt-selfstart-v2.yaml / crt-irq-v2.yaml：必须
  携带**真实函数条目**（不得统一 count=0）；gen-crt-v2.sh 验收同步；
  check-crt-v2.py:112-163,597-601,656-696 必改（现写死 172B 载体、21 个
  RequiredTags、拒绝 BYTES 类型），改为独立验证各 CRT 签名与新长度，保留
  启动字节/重定位不变断言。
- 合计 lld 内 39 个显式 v2 分段、38 个带载体 + runtime 2 个 yaml ≈ 40 个载体
  payload 候选；input-reader/isr-vectors 的无载体 e_flags 派生负测不补。
- llvm 侧 v2 ELF 输出的 .ll 测试补 `!mcs251.signatures`（elf-v2-identity.ll
  等，实施时盘点）。v1 资产不改。
