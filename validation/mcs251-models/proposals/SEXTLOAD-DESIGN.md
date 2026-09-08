# MCS251 符号扩展加载（SEXTLOAD）设计稿

**版本**：v2（2026-09-07，按 Alice 裁定修订）
**作者**：Mai（编译器工程师）
**状态**：设计提案，未实施；所有未实测指令语义一律标注实证状态，未实证前不得写实现
**源码基线**：当前工作树（`llvm/lib/Target/MCS251/`，未取 git 快照；引用行号以本稿成稿时文件为准）
**适用范围**：MCS251 后端 SelectionDAG lowering（SEXTLOAD / SIGN_EXTEND / SIGN_EXTEND_INREG），不触及对象格式、ABI、链接器
**方法论红线**：语义先 QEMU 实证再信（与 BRCC8S、SRA32ri 的开发口径一致）；本稿只含探针设计，不含任何执行结果

## 修订摘要（v2，按 Alice REQUEST-CHANGES 裁定逐条落实）

1. **算术与证明修正**（§2.2）：边界表笔误 `0x107F` 更正为 `0x1007F`；删除 v1 错误的"模 256 同余提升为模 65536 等式"论证，替换为对**全部输入**成立的统一代数证明（源宽 m、目标宽 N 的一般形）。
2. **三宽度完整序列**（§2.2）：i8→i32 加法输入 lane 明确为 `{00,00,00,x⊕80}`、偏置为完整 `0xFFFFFF80`；i16→i32 lane `{00,00,hi⊕80,lo}`、偏置 `0xFFFF8000`，⊕0x8000 作用于大端 Bytes[0]（sub_hi8，低地址字节）。按"REG_SEQUENCE 不发射指令也不凭空造零"重算发射指令数；删除"4/5 条""省 70–75%""全维度占优"等未测量承诺；保留"避免新增 CFG"结构优势；最终大小/spill 成本统一标注**待候选代码生成后测量**。I3 草案明确三个宽度各用哪条序列，i8→i32 直接 32 位组装、不先造无用 i16 符号扩展。
3. **统一原语证据表**（§2.5，三档：历史已证／证据待定位／本轮待测）：`add wr,#0xff80` 定为本轮待测、必须先过 P-A；`mov wr,#imm16` 因 td:300-302 是 ALU 证据而非 MOV 证据改为"待定位或补测"；删除"两字节 ADD8ri 拼偏置"回退（进位未解决）；MOV16ri 不可用时回退改为"已证 byte MOV 组装常数 + ADD16rr（仍需组合探针）"；"imm 全链失败"不再无条件回退到仍依赖 MOVDRri/MOVHDRi 的 (d)；注明 ADD16ri 虽已在动态栈/帧地址路径使用但"源码已用"≠偏置回绕运行证据；偏置加法禁止附加 nuw。
4. **P-A 拆三步**（§5.2）：P-A0 harness 正控制 → P-A1 单原语 → P-A2 完整组合；打印污染（emit_text 的 R12 与 DR12/WR12 别名）列为硬约束；放行规则改为**按选型分别放行**，废除"四形全 PASS"总闸。
5. **新增 I4：SIGN_EXTEND_INREG**（§4.1，阻塞级遗漏）：显式建模 i8/i16 内层宽度（Custom 后提取有效 lane 复用 I3 核心），单列回归；i1/其他宽度/向量声明不在本轮支持内。
6. **lit 必测升级**（§5.1）：O0、store 往返（trunc(sext) 可能被优化消掉，宽值须有独立可观察用途，另测宽 store 后 reload）、真实 spill（可分配 DR 仅 DR0/DR4/DR12）、多用途源值（两地址 COPY）、CSE 警告（SelectionDAG.cpp:12700-12719）全部升为必测；规划四个 lit 文件名。
7. **七问裁定结论**（§7 逐条"已裁定"注记）；**阶段归属与依赖图**（§8）：独立 SEXTLOAD 摘雷小战役挂接 F.2，P-A 不依赖 lld；P-B 修正参数拼写 `-mcs251-object-format=elf` 并补 `-filetype=obj`。

---

## 0. 问题陈述

日常 C 代码中 `int8_t` / `signed char` 读入 `int` 的整数提升（本 fork 默认 `int=32`）在 `-O1`
以上会被 DAG combiner 合并为 `SEXTLOAD`，随后命中：

- `llvm/lib/Target/MCS251/MCS251ISelLowering.cpp:1329-1331`
  （`LowerLoad` 内）：

```cpp
if (LD->getExtensionType() == ISD::SEXTLOAD)
  report_fatal_error("MCS251: sign-extending loads are not supported (no "
                     "8-to-16 bit sign extension yet)");
```

这是当前最大的日常编译硬雷：任何有符号小整数参与的 C 表达式都可能中止整个编译。
`short` → `int`（i16→i32 SEXTLOAD）同样命中该拒绝。

现有唯一测试是负例 `llvm/test/CodeGen/MCS251/loadstore-error-sextload.ll`
（`not --crash llc ... | FileCheck`），其注释自述原因："widening a byte into a word
is only implemented for zero/any extension ... a sign extension would need a
shift or branch sequence that this phase does not have."

### 0.1 证据基线（已核实；E14–E17 为 v2 新增/精化）

| # | 事实 | 出处 |
|---|---|---|
| E1 | EXTLOAD/ZEXTLOAD/SEXTLOAD 的 i8→i16、i8→i32、i16→i32 全部注册 Custom | `MCS251ISelLowering.cpp:90-95` |
| E2 | SEXTLOAD 一律 `report_fatal_error` | 同上 `:1329-1331` |
| E3 | LowerLoad 主干 = parseAddress → 逐字节 `buildByteLoad`（大端序）→ `makeWord`/`makeDR`（REG_SEQUENCE lane 组装）；`ValVT != MemVT` 时**一律发 `ISD::ZERO_EXTEND`**（`:1355-1356`）。EXTLOAD（anyext）被精化为零是合法的；ZEXTLOAD 正确；SEXTLOAD 到不了这里 | `:1333-1357` |
| E4 | SIGN_EXTEND i16/i32 是 Custom 且 **signed 路径已存在**：`Sign = setcc(Src <s 0)`、`Hi = select(Sign, 0xffff, 0)`，i8→i16 用 `makeWord(trunc(Hi), Src)`，i16→i32 用 `makeDR(Hi, Src)` | `:104`、`:107`、`LowerExtend :1437-1461` |
| E5 | 但该 signed 路径**没有任何 lit 正例锁定**：全测试目录含 "sext" 的文件仅 `loadstore-error-sextload.ll` 一条（grep 证实）。即：`-O0` 下未被合并的显式 `sext` 走的是一条无证据基线的重路径 | grep over `llvm/test/CodeGen/MCS251/` |
| E6 | 偏置二进制（offset-binary）原语族已被 QEMU 实证并在 BRCC8S 插入器使用：`mov r,#imm` / `mov r,r` / `xrl r,#imm`（即 `x ⊕ 0x80`）`/ cmp wr,wr`，加 `{0, lo}` 的 REG_SEQUENCE 组装 | `MCS251ISelLowering.cpp:811-865`（BRCC8S），`:615-620`（SELECT_CC 的 Flip） |
| E7 | ZEXT8 伪指令（custom inserter：`mov hi,#0` + REG_SEQUENCE）是无符号扩展的既有形状 | `:942-959`；td `MCS251InstrInfo.td:283-284` |
| E8 | 8/16 位 ALU 全集：add/sub/anl/orl/xrl 的 rr 与 ri 形式；**rr 形式与 anl/orl/xrl wr,#imm16 为 QEMU 运行实证，其余 imm 形式（含 add wr,#imm16）仅 sdas251 汇编器验证**；全部 `Defs = [PSW]`，虚拟 PSW 保护 glue 对不被拆 | td `:300-311`、`:313-392` |
| E9 | native 1-bit 移位 `sll/srl/sra`（r 与 wr 形式，无 dword 形式）；累加器族 `mov a,rX` / `rlc a` / `rrc a` / `clr c` / `mov rX,a` 已实证 | td `:418-441`；cpp `:725-806` |
| E10 | **td 中没有 ADDC/SUBB 的任何建模**（grep 无命中）；8051 系手册虽有这些编码，本后端从未定义、从未实证 | grep over `MCS251InstrInfo.td` |
| E11 | MOV32ri 常数经 custom inserter 展开为 `mov dr,#imm16`（清高字）+ `movh dr,#imm16`（写高字），顺序不可换 | cpp `:705-724` |
| E12 | -O0 FastRA 存在 lane coalescing 坑：从同一 DR 常量拆两个 WR lane 会被错误合并，常数必须用独立 vreg 材料化 | cpp `LowerLogical32 :1494-1499`、`splitI32ToBytes :481-486` |
| E13 | QEMU 探针既有口径：`Inputs/phase11-qemu.py`（sdas251/sdld 链、`-M stc32g144k246`、`-serial stdio`、串口断言 PASS/FAIL）；生产链 = clang → llc（`-mcs251-object-format=elf`）→ lld → HEX → QEMU；timeout 124 本身不是证据 | `phase11-qemu.py`；`validation/mcs251-models/DESIGN.md` 0.1.11、0.4 |
| E14 | **SIGN_EXTEND_INREG 漏洞（阻塞级）**：DAGCombiner 把 `sext(trunc x)` 合成为 `SIGN_EXTEND_INREG`（`:16089-16097`）；标量默认 Legal（TargetLoweringBase `:765-772`）；LegalizeDAG **按内层类型 InnerVT** 查 action（`:1056-1059`）。MCS251 当前无对应 action/分支/规则 → `i32 → trunc i8 → sext i32` 不保证路由进 I3 | `llvm/lib/CodeGen/SelectionDAG/DAGCombiner.cpp:16089-16097`、`llvm/lib/CodeGen/SelectionDAG/TargetLoweringBase.cpp:765-772`、`llvm/lib/CodeGen/SelectionDAG/LegalizeDAG.cpp:1056-1059` |
| E15 | 可分配 DR 仅 DR0/DR4/DR12（本稿复核：`GPR32 = (add DR0, DR4, DR12)`）；i32 spill 是完整 DR 伪指令、到 PEI 才拆 | `MCS251RegisterInfo.td:206-211` |
| E16 | SelectionDAG 会 CSE 相同 MachineNode：重复调用 `buildMOV8ri(0)` **不保证得到独立 vreg** | `SelectionDAG.cpp:12700-12719` |
| E17 | td:300-302 的实证注释覆盖的是 **ALU** imm 形式（anl/orl/xrl wr,#imm16 QEMU 已证、其余 imm 仅汇编器验证），**不含 MOV16ri 的证据**（对 E8 的精化；本稿复核注释原文） | `MCS251InstrInfo.td:298-302` |

## 1. 语义与触发路径

```
C: int v = sc[i];                 // signed char
IR: %v = load i8, ptr %p
     %e = sext i8 %v to i32
DAG(-O1+): i32 = sextload i8 %p     → LowerLoad → E2 的 fatal error
DAG(-O0 / 有中间使用): load + SIGN_EXTEND 分立
     → SIGN_EXTEND Custom → LowerExtend signed 分支（E4，重路径，E5 无基线）

第三条路径（v2 新增识别，阻塞级，E14）：
IR: %t = trunc i32 %w to i8 ; %e = sext i8 %t to i32   （或 i16 内层）
DAG: sext(trunc x) --DAGCombiner:16089-16097--> SIGN_EXTEND_INREG
     → 标量默认 Legal（TargetLoweringBase:765-772）；LegalizeDAG:1056-1059
       按内层类型 InnerVT 查 action；MCS251 无任何注册
     → 该形状不保证进 I3，机器序列不受本设计约束 → 必须新增 I4
```

三类目标宽度：i8→i16（`short s = sc[i];` 或 `(short)sc[i]`）、i8→i32（默认 int 提升）、
i16→i32（`int v = s;`）。三者共用同一套原语即可覆盖（§2.2 各给完整序列）。

**字节语义**（大端 lane 布局，寄存器内 sub_hi8/sub_lo8 与内存大端序一致，E3）：
32 位值的大端字节序记 Bytes[0..3]（Bytes[0] = 最高字节 = 低地址）；16 位值的
Bytes[0] = sub_hi8（低地址字节）、Bytes[1] = sub_lo8。
sext8→16 结果 = `{0x00|0xFF, x}`；sext8→32 / sext16→32 高位字节全复制符号位。
正确性判据：**代数证明覆盖全部输入**（§2.2）；`0x00/0x7F/0x80/0xFF`
（16 位版 `0x0000/0x7FFF/0x8000/0xFFFF`）仅作 sanity 边界样本。

## 2. 候选方案

### 2.1 案 0：拆分 SEXTLOAD，复用既有 SIGN_EXTEND（零新原语）

**思路**：SEXTLOAD 不是原子需求。把 `LowerLoad` 的拒绝删掉，`:1355-1356` 按
`getExtensionType()` 分发 `SIGN_EXTEND`/`ZERO_EXTEND`，让 sign 部分流进既有
`LowerExtend` signed 分支（setcc + select，E4）。

**字节级序列**（i8→i16，展开后实际机器码，按 E4 + SETCC/SELECT 的 Custom/Expand 链推演）：

```
; Sign = (x <s 0)：signed i8 比较走 offset-binary 加宽（E6 先例）
mov  rT0, #0x00          ; hi lane for flip(LHS)
mov  rT1, rX             ; 复制 x
xrl  rT1, #0x80          ; flip
... {rT0,rT1} regseq ...  ; (ZEXT8 形状，另含 mov rT2,#0)
mov  rT3, #0x00 / mov rT4, #0x00 + xrl #0x80  ; flip(常数 0) 同样展开
cmp  wrA, wrB
jCCinv skip / ejmp T / ejmp F ...              ; SELECT8 棱形（3 MBB）
; Hi = select(Sign, 0xffff, 0)：SELECT Expand → SELECT_CC → SELECT16 棱形
cmp  rSign, #0x00
jCCinv skip / ejmp T / ejmp F ...              ; 又 3 MBB + PHI
```

**代价**（推演估计，未测量）：约 15–17 条发射指令、6 个新 MBB、2 个 PHI；
跨块 live range 增加 FastRA spill 面。
**正确性**：纯组合逻辑（setcc/select），四边界语义平凡成立；但 E5——该路径无基线。
**集成点**：仅 `LowerLoad:1329-1331`（删拒绝）与 `:1355-1356`（分发），约 4 行。
**评价**：最小改动、最快摘雷；但把最常见表达式（int 提升）留在 CFG 膨胀的代码形状上，
与本后端"无条件长分支全展开"的架构相互放大。定位为紧急止血选项 / 案 A 的对照实现，
不作为生产行（老路径处置已裁定②，见 §7）。

### 2.2 案 A（推荐）：偏置二进制还原——`xrl #0x80` + 偏置加法

**统一恒等式**（BRCC8S 已实证前半段原语的逆运算；m = 源宽度，N = 目标宽度，N > m）：

```
sext_{m→N}(x) = zext_{m→N}(x ⊕ 2^(m-1)) + (2^N − 2^(m-1))   (mod 2^N)

偏置常数 = 2^N − 2^(m-1)：
  i8→i16 : 0xFF80
  i8→i32 : 0xFFFFFF80     （完整 32 位偏置）
  i16→i32 : 0xFFFF8000    （完整 32 位偏置）
```

**统一证明（覆盖全部输入，v2 替换 v1 的错误论证）**。设 `x ∈ [0, 2^m)`，
`s = ⌊x / 2^(m-1)⌋ ∈ {0,1}` 为符号位：

1. 整数等式 `x ⊕ 2^(m-1) = x + 2^(m-1) − s·2^m`：s=0 时顶位为 0，翻位即 +2^(m-1)；
   s=1 时顶位为 1，翻位即 −2^(m-1) = +2^(m-1) − 2^m。
2. 该结果 ∈ [0, 2^m)，zext 不改数值：
   `zext_N(x ⊕ 2^(m-1)) = x + 2^(m-1) − s·2^m`（整数等式，非仅同余）。
3. 加偏置：`(x + 2^(m-1) − s·2^m) + (2^N − 2^(m-1)) = x − s·2^m + 2^N
   ≡ x − s·2^m (mod 2^N)`。
4. N 位补码纹样：s=0 时 `sext_N(x) = x`；s=1 时 `= x + 2^N − 2^m ≡ x − 2^m (mod 2^N)`；
   合并即 `sext_N(x) ≡ x − s·2^m (mod 2^N)`。

∴ 恒等式对**全部** `x ∈ [0, 2^m)` 成立，非仅四样本。

**nuw 禁令**：第 3 步在 s=0（正数输入）时和恰好超出 2^N、必须按**无符号回绕**落回。
实现中无论 DAG MachineNode 还是任何未来 SDNode 化重写，**不得给偏置加法附加 nuw**
（nuw 在回绕时是毒行为，而正数输入恰需回绕）。

**v1 论证错误记录（在案）**：v1 曾写 `x ⊕ 0x80 ≡ x + 0x80 (mod 256)` 再据以推出
mod 2^16 的结论——模 256 同余不能提升为模 65536 等式（s=1 时
`zext(x⊕0x80) = x − 0x80` 而非 `x + 0x80`）。该论证作废；结论由上述统一证明重新支撑。

**sanity 边界表**（i8→i16；仅为抽检，不构成证明）：

| x | x⊕0x80 | zext | +0xFF80 (mod 2^16) | 期望 sext(x) |
|---|---|---|---|---|
| 0x00 | 0x80 | 0x0080 | **0x0000**（0x10000 截断） | 0x0000 ✓ |
| 0x7F | 0xFF | 0x00FF | **0x007F**（0x1007F 截断） | 0x007F ✓ |
| 0x80 | 0x00 | 0x0000 | **0xFF80** | 0xFF80 ✓ |
| 0xFF | 0x7F | 0x007F | **0xFFFF** | 0xFFFF ✓ |

i16→i32 抽检：0x0000→0x0000、0x7FFF→0x7FFF（0xFFFF+0xFFFF8000=0x1'00007FFF 截断）、
0x8000→0xFFFF8000、0xFFFF→0xFFFFFFFF。i8→i32 抽检同 §5.2 P-A2 期望表。

**lane 组装纪律**：REG_SEQUENCE（`makeWord`/`makeDR`）只做 lane 组装——**不发射指令，
也不凭空产生 0**：每个为零的目标 lane 都需要一条独立 `mov r,#0`（MOV8ri）材料化
（E7 ZEXT8 同款）；EXTRACT_SUBREG（sub_hi8/sub_lo8）为 lane 读取，本身不发射，
但两地址约束或多用途源可能引入 COPY（§5.1 压力必测点）。

**字节级完整序列**（v2 重写；每行标注 §2.5 证据档位）：

(a) i8→i16 **主形**——3 条发射：

```
; 入口：rX = 已加载字节（LowerLoad buildByteLoad 产物，i8 GPR8 vreg）
mov  rT, #0x00           ; MOV8ri   Bytes[0]=00                  [历史已证]
xrl  rX, #0x80           ; XOR8ri   Bytes[1]=x⊕0x80              [历史已证]
add  wrD, #0xff80        ; ADD16ri  wrD=makeWord(rT,rX')+0xFF80  [本轮待测，P-A1]
; makeWord(rT, rX') 为 REG_SEQUENCE：不发射、零 lane 已由上面 MOV8ri 材料化
```

(b) i8→i16 **回退形一**——4 条发射（ADD16ri 不可用时）：

```
mov  rT, #0x00           ; MOV8ri                                    [历史已证]
xrl  rX, #0x80           ; XOR8ri                                    [历史已证]
mov  wrK, #0xff80        ; MOV16ri  独立 vreg（E12）                  [证据待定位]
add  wrD, wrK            ; ADD16rr                                   [历史已证]
```

(b') i8→i16 **回退形二**——5 条发射（MOV16ri 亦不可用时；全已证原语的组合）：

```
mov  rT, #0x00           ; MOV8ri                                    [历史已证]
xrl  rX, #0x80           ; XOR8ri                                    [历史已证]
mov  rK0, #0xff          ; MOV8ri   偏置 Bytes[0]（大端）              [历史已证]
mov  rK1, #0x80          ; MOV8ri   偏置 Bytes[1]                      [历史已证]
add  wrD, wrK            ; ADD16rr  wrK=makeWord(rK0,rK1)（REG_SEQUENCE 不发射） [历史已证]
```

(c) i8→i32 **主形**——7 条发射（全 byte 零 lane 形）：

```
; 加法输入 lane（大端 Bytes[0..3]）= {00, 00, 00, x⊕80}；偏置 = 0xFFFFFF80（完整）
mov  rB0, #0x00          ; MOV8ri   Bytes[0]=00                       [历史已证]
mov  rB1, #0x00          ; MOV8ri   Bytes[1]=00                       [历史已证]
mov  rB2, #0x00          ; MOV8ri   Bytes[2]=00                       [历史已证]
xrl  rX, #0x80           ; XOR8ri   Bytes[3]=x⊕0x80                   [历史已证]
mov  drJ, #0xff80        ; MOVDRri  偏置低字（同时清高字，E11）        [证据待定位]
movh drJ, #0xffff        ; MOVHDRi  偏置高字（顺序不可换，E11）        [证据待定位]
add  drD, drJ            ; ADD32rr  drD = Z32 + 0xFFFFFF80            [历史已证]
; Z32 = makeDR(makeWord(rB0,rB1), makeWord(rB2,rX'))，四个 lane 全部材料化，REG_SEQUENCE 不发射
```

(c') i8→i32 **变体**——6 条发射：Bytes[0:1] 的零字改用一条 `mov wrK,#0x0000`
（MOV16ri，证据待定位；**通过 P-A1 后方可采用**）。

(d) i16→i32 **主形**——6 条发射：

```
; 入口：wrS = 16 位源值（大端 lane：sub_hi8 = 源 Bytes[0] = 低地址字节，sub_lo8 = 源 Bytes[1]）
; ⊕0x8000 的 0x80 落在常数高字节，即作用于源的大端 Bytes[0]（sub_hi8，低地址字节）
; ——与 (a)/(c) 的符号位字节（最高有效字节）同位，这是三宽度共用一套原语的关键。
mov  rB0, #0x00          ; MOV8ri   目标 Bytes[0]=00                   [历史已证]
mov  rB1, #0x00          ; MOV8ri   目标 Bytes[1]=00                   [历史已证]
xrl  rHi8', #0x80        ; XOR8ri   目标 Bytes[2] = wrS.sub_hi8 副本 ⊕0x80 [历史已证]
mov  drJ, #0x8000        ; MOVDRri  偏置低字（清高字）                  [证据待定位]
movh drJ, #0xffff        ; MOVHDRi  偏置高字                            [证据待定位]
add  drD, drJ            ; ADD32rr  偏置 = 0xFFFF8000（完整）           [历史已证]
; Z32 = makeDR(makeWord(rB0,rB1), makeWord(wrS.sub_hi8 ⊕0x80, wrS.sub_lo8))
; sub_hi8/sub_lo8 为 EXTRACT_SUBREG lane 读取，不发射；多用途源/两地址约束可能引入 COPY
```

(d') i16→i32 **变体**——5 条发射：目标 Bytes[0:1] 零字改用一条 `mov wrK,#0x0000`
（同 (c') 门控）。

**发射指令计数汇总**：i8→i16：3（主）/4（b）/5（b'）；i8→i32：7（c）/6（c'）；
i16→i32：6（d）/5（d'）。计数口径 = 上列序列逐条计，**未含**调度换序、两地址 COPY、
PEI spill 展开。

**代价声明（v2 收紧）**：最终代码量、代码字节、spill、调度成本**待候选代码生成后
测量**，本稿不作任何未测量的数量对比承诺。已证的结构优势：全部形为**直线代码——
0 新 MBB、0 PHI、无跨块 live range**（对比案 0 的双棱形 CFG），不与本后端
"无条件长分支全展开"特性相互放大。额外 vreg 面：2–4 个 GPR8 零/偏置 lane +
1 个 GPR16/GPR32 临时。

**对 LowerLoad 集成点**：`:1329-1331` 拒绝删除的理由——SextLoad 的语义在
本方案下由"字节 load（既有）+ LowerExtend（重写为恒等式序列）"合成，LowerLoad
只需把扩展类型如实转发（`:1355-1356` 分发 `SIGN_EXTEND`），无需在 LowerLoad
内联任何字节技巧，保持其"纯内存访问分类 + 字节组装"的单一职责。
**spill/寄存器压力**：直线序列内临时立即消亡，无跨块区间；-O0 下唯一坑是 E12
（常数/同类拆 lane 必须独立 vreg 材料化——实现约束，见 §4 代码草案注释；E15 的
可分配 DR 上限 DR0/DR4/DR12 使真实压力场景必须专门测试，§5.1）。
PSW 写入面：序列写标志但无 glue 消费者，虚拟 PSW（E8）保证不拆散 cmp+jcc 对。

### 2.3 案 B：移位链——`(zext << 8) >>a 8`（零新原语的穷举基线）

```
; i8→i16：zext 后 SLL16 ×8 再 SRA16 ×8
mov rT, #0x00
; wrD = {rT, rX}
sll  wrD        ; ×8（每个 MachineNode 一条，LowerShift 既有展开形状，E9）
...
sra  wrD        ; ×8
```

17 条、0 新 MBB。i16→i32 需 16+16=32 条且 dword 无 native 移位，SRA32 链
（`mov a/rrc a` ×4 字节 ×16 位，cpp `:725-806`）约 80+ 条——**该宽度不可行**。
全部原语已实证（E9）是其唯一优点。价值：作为案 A 探针失败时的语义对照实现
（预期输出可与案 A 交叉验证），以及 -Odebug 目标的后备。不推荐作为主路径。

### 2.4 案 C：累加器 CY 链——`rlc a` 搬符号位 + `subb a,acc` 广播

经典 8051 技巧：

```
mov  a, rX        ; MOV8a    已实证（E9）
rlc  a            ; RLCA     CY = x.7（入口 CY 只进 A.0，可弃）
subb a, acc       ; SUBB A,E0h(direct)  A = 0 − CY = 0x00/0xFF   ← 未建模！
mov  rHi, a       ; MOV8ra   已实证
```

4 条得符号字节广播，再 REG_SEQUENCE。**硬伤**：(1) td 无 SUBB 定义（E10），
需新增指令 + 编码 QEMU 实证，实证负担最重；(2) 独占 A 累加器并写 PSW，
与 MUL/SELECT 等固定寄存器序列的插入约束叠加；(3) 相对案 A(a) 收益存疑且引入
新语义面。**已裁定④（§7）**：ADDC/SUBB 建模单独立项、与本战役解耦，本战役任何
回退不得借道偷渡未建模指令——案 C 本轮不做，仅在 ADDC/SUBB 立项后重估。

### 2.5 统一原语证据表（v2 新增，三档分类）

选型门槛：任何原语进入**生产选型**前，要么属"历史已证"，要么已通过 P-A 对应层级
（单原语 P-A1 + 所在组合 P-A2）。档位三档：历史已证／证据待定位／本轮待测。

| 原语（td 名 / asm 形） | 档位 | 依据 / 缺口 |
|---|---|---|
| MOV8ri `mov r,#imm` | 历史已证 | E6/E7（BRCC8S、ZEXT8 路径 QEMU 运行实证） |
| MOV8rr `mov r,r` | 历史已证 | E6 |
| XOR8ri `xrl r,#0x80` | 历史已证 | E6（BRCC8S 偏置翻转，QEMU 运行实证） |
| ADD16rr `add wr,wr` | 历史已证 | E8（QEMU 运行实证） |
| ADD32rr `add dr,dr` | 历史已证 | E8（QEMU 运行实证） |
| **ADD16ri `add wr,#0xff80`** | **本轮待测** | E8：imm 形式仅 sdas251 汇编器验证。**注**：ADD16ri 已在动态栈/帧地址路径使用（cpp 动态 alloca/帧锚链），但"源码已用"不替代**偏置回绕**（正数输入需无符号回绕 + 立即数 0xff80 的大端编码）的运行证据；必须先过 P-A1/P-A2 才能进生产选型 |
| **MOV16ri `mov wr,#imm16`** | **证据待定位** | td:300-302 的实证记录是 **ALU** 证据（anl/orl/xrl wr,#imm16），**不是 MOV 的证据**（E17）；需定位历史运行证据，定位不到则本轮 P-A1 补测 |
| **MOVDRri `mov dr,#imm16`**（写低字 + 清高字） | **证据待定位** | E11：lit 有形状锁定，运行语义实证待定位；P-A1 含裁定⑦的中间态断言（清高字的是它、不是 movh） |
| **MOVHDRi `movh dr,#imm16`**（写高字） | **证据待定位** | 同上；顺序敏感（先 mov 后 movh，E11），P-A1 断言序锁定 |
| REG_SEQUENCE / EXTRACT_SUBREG（sub_hi8/sub_lo8） | 非发射指令 | lane 组装/读取，不发射；两地址约束/多用途源可能引入 COPY（§5.1 必测） |

（v1 的"两字节 ADD8ri 拼偏置"回退**已删除**：8 位加法进位语义未解决，不得作为回退路径。）

## 3. 对比总表

| 维度 | 案 0（setcc+select） | 案 A（偏置加法）★推荐 | 案 B（移位链） | 案 C（CY 链） |
|---|---|---|---|---|
| CFG 形状 | 6 新 MBB、2 PHI（推演） | **0 新 MBB、0 PHI**（全部形直线） | 0 / 0 | 0 / 0 |
| 直线发射指令（设计计数†） | ~15–17（推演） | i8→16：3／回退 4–5；i8→32：7（变体 6）；i16→32：6（变体 5） | i8→16：17；i16→32：~80+（不可行） | i8→16：4 + 1 定义 |
| 新增 td 指令 | 0 | 0 | 0 | 1（SUBB8，E10 未建模） |
| 待测/待定位原语（§2.5） | 0 个新原语（但整路径无运行基线，E5） | ADD16ri（本轮待测）；MOV16ri、MOVDRri/MOVHDRi（待定位） | 0 | SUBB 编码+语义 |
| 标志/调度面 | 大（2 棱形跨块） | 小（PSW 写、无 glue 冲突；虚拟 PSW 保护，E8） | 中（17 次写） | 大（独占 A） |
| 代码量 / spill / 调度成本 | **待候选代码生成后测量** | **待候选代码生成后测量** | 同左 | 同左（另加 A 占用约束） |

† 计数口径见 §2.2（序列逐条计，未含调度换序、两地址 COPY、PEI spill）；案 0 为按
Custom/Expand 链的推演估计。**本表不构成任何未测量的优劣结论**；案 A 的既证优势是
结构性的：不新增 CFG、直线代码、无跨块 live range。

## 4. 推荐案与精确集成点

**推荐：案 A 主路径**；案 0 仅作对照实现/紧急止血，不进生产。**已裁定②**：老
setcc+select signed 路径**直接替换**，只动 `LowerExtend` 的 signed 分支；不保留老路径的
理由 = E5（无基线可回归）+ 案 A 的结构优势（避免新增 CFG）；验收口径 = LLVM sext
语义 + 宿主期望表 + 新 lit 正例（§5）。

| # | 位置 | 改动 |
|---|---|---|
| I1 | `MCS251ISelLowering.cpp:1329-1331`（`LowerLoad`） | 删除 SEXTLOAD 的 `report_fatal_error` |
| I2 | `MCS251ISelLowering.cpp:1355-1356`（`LowerLoad` 尾部） | `Res = DAG.getNode(ISD::ZERO_EXTEND, ...)` 改为按 `LD->getExtensionType()` 分发：SEXTLOAD→`ISD::SIGN_EXTEND`，其余（ZEXTLOAD/NON_EXTLOAD/**EXTLOAD 维持 ZERO_EXTEND，已裁定③**，合法精化且是现状，不扩大改动面） |
| I3 | `MCS251ISelLowering.cpp:1442-1455`（`LowerExtend` signed 分支） | 整体替换为 §2.2 恒等式序列。发出的 `ISD::SIGN_EXTEND`（I2 产物）会再次进入 legalizer 路由回 `LowerExtend`，故 SEXTLOAD 与显式 sext 一个实现点全覆盖。**三宽度各用：i8→i16 = (a)（回退 (b)/(b')）；i8→i32 = (c)（直接 32 位组装，不先造无用 i16 符号扩展）；i16→i32 = (d)** |
| I4 | `MCS251ISelLowering.cpp`（setOperationAction 注册区 + LowerOperation 分派） | **新增 SIGN_EXTEND_INREG 显式建模**，见 §4.1 |

I3 代码草案（示意，非最终实现；v2 重写）：

```cpp
if (Signed) {
  // 统一恒等式（§2.2，对全部输入成立；边界表仅 sanity）：
  //   zext_N(x ⊕ 2^(m-1)) + (2^N − 2^(m-1)) == sext_N(x)   (mod 2^N)
  // 偏置：i8->i16 0xFF80；i8->i32 0xFFFFFF80；i16->i32 0xFFFF8000。
  //
  // 实现纪律（写入代码注释，评审核对）：
  //  * 偏置加法禁止 nuw：正数输入恰需无符号回绕（§2.2 证明第 3 步）。
  //  * 零/常数 lane 一律独立 vreg 材料化（E12）；但 SelectionDAG.cpp:12700-12719
  //    会 CSE 相同 MachineNode——重复调用 buildMOV8ri(0) 不保证得到独立 vreg（E16），
  //    需要独立化时必须显式破坏 CSE（不同 chain/构建入口），并靠 sext-pressure.ll 锁定。
  //  * REG_SEQUENCE/makeWord/makeDR 只组装 lane：不发射指令、不凭空产生 0，
  //    每个零 lane 一条 MOV8ri（MOV16ri(0) 一条顶两个字节，gated on §2.5 证据）。
  auto FlipMSB8 = [&](SDValue Byte) {              // i8 -> i8，符号位翻转
    return SDValue(DAG.getMachineNode(MCS251::XOR8ri, DL, MVT::i8,
        {Byte, DAG.getTargetConstant(0x80, DL, MVT::i8)}), 0);
  };

  if (SrcVT == MVT::i8) {
    SDValue Flipped = FlipMSB8(Src);
    if (Op.getValueType() == MVT::i16) {           // 序列 (a)：3 条
      SDValue Z16 = makeWord(buildMOV8ri(0, DL, DAG), Flipped, DL, DAG);
      return SDValue(DAG.getMachineNode(MCS251::ADD16ri, DL, MVT::i16,
          {Z16, DAG.getTargetConstant(0xff80, DL, MVT::i16)}), 0);
      // ADD16ri 未过 P-A 前不得进生产；回退形 (b)/(b') 见 §2.2，形选择由 P-A 裁定。
    }
    // 序列 (c)：i8->i32 直接 32 位 lane 组装 {00,00,00,x⊕80} + 完整偏置 0xFFFFFF80。
    // 不先做 i8->i16 再二次扩展（不造无用的 i16 符号扩展中间值）。
    SDValue Z32 = makeDR(
        makeWord(buildMOV8ri(0, DL, DAG), buildMOV8ri(0, DL, DAG), DL, DAG),
        makeWord(buildMOV8ri(0, DL, DAG), Flipped, DL, DAG), DL, DAG);
    SDValue Bias = buildMOV32ri(0xffffff80u, DL, DAG);   // E11 展开：mov dr,#0xff80 + movh dr,#0xffff
    return SDValue(DAG.getMachineNode(MCS251::ADD32rr, DL, MVT::i32,
                                      {Z32, Bias}), 0);
  }
  // 序列 (d)：i16->i32。⊕0x8000 落在源的大端 Bytes[0]（sub_hi8，低地址字节）。
  // hi/lo 提取对齐 LowerStore:1419-1424 先例；多用途源时 XOR 必须作用于副本（COPY），
  // 不得破坏源值（sext-pressure.ll 必测）。
  SDValue Hi = /* EXTRACT_SUBREG(Src, sub_hi8)（示意） */;
  SDValue Lo = /* EXTRACT_SUBREG(Src, sub_lo8)（示意） */;
  SDValue Z32 = makeDR(
      makeWord(buildMOV8ri(0, DL, DAG), buildMOV8ri(0, DL, DAG), DL, DAG),
      makeWord(FlipMSB8(Hi), Lo, DL, DAG), DL, DAG);
  SDValue Bias = buildMOV32ri(0xffff8000u, DL, DAG);     // mov dr,#0x8000 + movh dr,#0xffff
  return SDValue(DAG.getMachineNode(MCS251::ADD32rr, DL, MVT::i32,
                                    {Z32, Bias}), 0);
}
```

（`buildMOV32ri` 为示意名 = 发射 MOV32ri 机器节点经 E11 custom inserter 展开，
或直接发射 MOVDRri/MOVHDRi 序列，实施时定；`ADD16ri` vs `(b)/(b')`、`MOV16ri(0)`
变体 (c')/(d') 的取舍由 §5.2 P-A 结果按选型分别裁定。）

**不动清单**：td 零改动（案 A 不需要新指令/伪指令/custom inserter，全部用 DAG 层
MachineNode 组合，与 BRCC8S 的 BuildFlipped、LowerShift 的 unroll 同形；I4 的 Custom
action 注册也在 cpp 构造函数完成）；`setLoadExtAction`（:90-95）维持 Custom 不变。

### 4.1 I4：SIGN_EXTEND_INREG（v2 新增，阻塞级遗漏）

**问题**（E14）：`DAGCombiner.cpp:16089-16097` 把 `sext(trunc x)` 合成为
`SIGN_EXTEND_INREG`；`TargetLoweringBase.cpp:765-772` 标量默认 **Legal**；
`LegalizeDAG.cpp:1056-1059` 按**内层类型 InnerVT** 查 action。MCS251 当前没有
SIGN_EXTEND_INREG 的任何 action/分支/规则 → `i32 → trunc i8 → sext i32`（宽寄存器
取低字节再符号提升）**不保证进 I3**：Legal 默认下节点被当合法直接留下，机器序列
不受本设计约束，形成第二个语义黑洞。

**设计**：

- `setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i8, Custom)` 与
  `setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i16, Custom)`（action 按 InnerVT
  索引，E14）；其余 InnerVT 不注册。
- LowerOperation 新增分支：从 operand 1（ValueTypeSDNode）读 InnerVT；按 InnerVT
  提取源值**有效 lane**（i8 → 低字节 = 大端 Bytes[3]；i16 → 低字 = Bytes[2:3]），
  然后**复用 I3 的 (InnerVT → DstVT) 分发核心**（同一套恒等式序列），不另写第二套语义。
- **单列回归**：`sext.ll` 内 `trunc+sext` 回形各一函数（i32→i8→i32、i32→i8→i16、
  i32→i16→i32）。
- **范围声明**：i1 内层、i8/i16 以外的内层宽度、向量 SIGN_EXTEND_INREG **显式不在
  本轮支持声明内**——遇到时维持现状（默认 Legal 既有行为）并记录，后续单独立项。

## 5. 验证计划

### 5.1 lit 正例（v2：四个文件，组织可调、覆盖不可省；全部必测项不得降级为可选）

| 文件 | 覆盖（全部必测） |
|---|---|
| `llvm/test/CodeGen/MCS251/sextload.ll` | ① 三宽度 SEXTLOAD（i8→i16 / i8→i32 / i16→i32），RUN 与 load.ll 一致（`llc -mtriple=mcs251 -verify-machineinstrs < %s \| FileCheck %s`），CHECK 锁字节 load + `xrl r,#0x80` + 选定偏置加法形（CHECK 行序以实际发射序固化；XOR 与 `mov rT,#0` 无数据依赖可换序）。② **-O0 必测**：显式 sext 走 I3（SIGN_EXTEND Custom）的 FastRA lane 放置（对照 `frame-o0.ll` 先例，防 E12/E16 类回归）。③ **交错比较**：sext 结果直接进有符号比较，锁 PSW 写入不拆散 cmp+jcc（虚拟 PSW 回归哨兵）；注意 `if ((int)c < -100)` 可能被 combiner **缩成 i8 比较**（−100 在 i8 域内）——必须核对生成序列确认扩展与 cmp/jcc 依赖真实保留，被缩域则改写用例（域外比较值或另加宽值可观察用途），不得让哨兵空转。实现合入时删除负例 `loadstore-error-sextload.ll` |
| `llvm/test/CodeGen/MCS251/sext.ll` | ① 参数直传纯寄存器版三宽度（`sext i8 %a to i16/i32`、`sext i16 %a to i32`），锁 I3 重写后 LowerExtend 形状，填补 E5 基线空洞。② **I4 单列回归**：`trunc+sext` 回形（§4.1）锁 SIGN_EXTEND_INREG 显式建模 |
| `llvm/test/CodeGen/MCS251/sext-roundtrip.ll` | **已裁定⑥必测**：① 截断 store 往返 i16→i8 / i32→i8 / i32→i16（`sc[j] = (signed char)...` 形）。警告：trunc(sext) 可能被优化**消掉**——宽值必须有**独立可观察用途**（进算术/比较/返回值），不得只写窄 store。② **宽 store 后 reload**：32 位 sext 结果整体存内存再读回比较，锁全宽度纹样 |
| `llvm/test/CodeGen/MCS251/sext-pressure.ll` | ① **真实 spill 必测**：足够多 live 值跨调用/跨块，迫使 sext 结果 spill；E15：可分配 DR 仅 DR0/DR4/DR12（RegisterInfo.td:206-211），i32 spill 是完整 DR 伪指令、到 PEI 才拆。② **多用途源值必测**：源字节/字同时被 sext 与其他消费者使用（两地址约束可能要求 COPY），锁源值不被 XOR 破坏。③ **CSE 哨兵**：E16——SelectionDAG.cpp:12700-12719 会 CSE 相同 MachineNode，重复 `buildMOV8ri(0)` 不保证独立 vreg；-O0 变体锁定 lane 放置 |

### 5.2 QEMU 探针设计（只设计，不执行；v2 拆为 P-A0/P-A1/P-A2 三步 + P-B）

**层级结构**：正控制 → 单原语 → 完整组合，任何一层 FAIL 不得进入下一层（DESIGN.md
0.4 口径：FAIL 保留记录，不得以源码推断覆盖实测）。P-A 全程走 sdas251/sdld 链 +
QEMU，**不依赖 lld**（§8）。

**打印不污染纪律（硬约束，P-A0 建立并全程生效）**：现有 `emit_text()` 用 R12 作
打印暂存（`phase11-qemu.py:57-59`，`mov r12,#.. ; mov 0x99,r12`），会覆盖 DR12/WR12
的组成部分。二选一，写入每个探针 asm 头部注释：① 被测值所在寄存器与 R12 **完全不
别名**（探针寄存器分配避开 R12 及包含它的 WR12/DR12）；② 打印前**无破坏快照**（被测
字节经 `mov r,r` 复制到不别名寄存器/direct RAM 再打印）。

**P-A0：harness 正控制（不含任何待测原语）**

- 只用历史已证原语（`mov r,#imm` / `mov r,r` / halt 等）。
- 内容：已知常数 → 无破坏快照 → 串口回传 → 宿主比对；PASS/FAIL 哨兵尾行；
  timeout 124 不作判定依据。
- 目的：把"串口链路 + 快照纪律 + 判定脚本"自身的正确性从待测结论中剥离；同时验证
  所选的打印不污染方案（上节 ①或②）。

**P-A1：单原语**

- 输入准备只用已证 byte MOV（`mov r,#imm` / `mov r,r`）。
- 检查项：目标**每个字节**逐一断言 + **非目标寄存器哨兵**（执行前写哨兵值，执行后
  断言不变，捕获隐性破坏）。
- 对象（对照 §2.5 档位）：
  - `add wr,#0xff80`（ADD16ri，**本轮待测**，E8 关键缺口；虽已在动态栈/帧地址路径
    使用，"源码已用"不替代偏置回绕运行证据）；
  - `mov wr,#imm16`（MOV16ri，**证据待定位**——td:300-302 是 ALU 证据不是 MOV
    证据；定位不到历史记录则本轮补测）；
  - `mov dr,#imm16`（MOVDRri）+ `movh dr,#imm16`（MOVHDRi）：含**裁定⑦中间态断言**——
    先 byte 写**非零高字哨兵** → MOV 后断言**高字为零、低字正确**（清高字的是
    `mov dr,#imm16`，不是 `movh`）→ MOVH 后断言**高字正确、低字不变**（顺序敏感性）；
  - 已证原语复验一轮（防回归）：`xrl r,#0x80`、`mov r,#imm`、`mov r,r`、
    `add wr,wr`、`add dr,dr`。
- 判定：逐原语独立 PASS/FAIL；FAIL 保留记录，对应组合不得进 P-A2 选型。

**P-A2：完整组合**

- 输入集：三宽度边界 i8 `{0x00, 0x7F, 0x80, 0xFF}`、i16 `{0x0000, 0x7FFF, 0x8000,
  0xFFFF}`（可加中间值样本）；逐输入执行完整序列，结果 WR/DR 按字节拆开回传，
  宿主侧与期望表比对：
  - i8→16 期望：`0000 007F FF80 FFFF`
  - i8→32 期望：`00000000 0000007F FFFFFF80 FFFFFFFF`
  - i16→32 期望：`00000000 00007FFF FFFF8000 FFFFFFFF`
- 序列集：主形 (a)/(c)/(d) + **选定回退形**（(b) 或 (b')，按 P-A1 结果选定；回退形
  组合本身也要测——"全已证原语的组合"不豁免组合探针）。
- **放行规则（已裁定：按选型分别放行）**：每个形独立判定；主形 FAIL 不阻塞回退形，
  "主形 FAIL + 回退形独立 PASS"可裁定切换选型。**不是** v1 的"四形全 PASS"总闸。
- 逐条回填 §2.5 证据档位；结果 VERDICTS 式落盘 `validation/mcs251-models/probes/`，
  注明平台为 QEMU（不可记真机，DESIGN.md 0.4；真机补测另列验收矩阵单独记账，裁定⑤，
  保留 RSTCFG 禁触等安全规则）。探针资产与结论冻结后，方可在
  `MCS251InstrInfo.td:300-302` 的实证注释中追加 imm 形式的 QEMU 记录（同步更新 E8 口径）。

**P-B：端到端探针（生产链，v2 修正参数拼写）**

clang → llc **`-mcs251-object-format=elf` `-filetype=obj`** → lld → HEX → QEMU
`-M stc32g144k246 -serial stdio`（v1 的 `-mcs251-object-form=elf` 为笔误，正确拼写
经 `MCS251MCTargetDesc.cpp:34` 核实；对象生成显式 `-filetype=obj`）：

```c
/* 期望表在宿主侧；设备侧只回传观测值与逐项 PASS/FAIL */
static const signed char sc[] = {0, 1, 127, -128, -127, -2, -1};
static const short        ss[] = {0, 1, 32767, -32768, -32767, -2, -1};
/* 覆盖：i8→32（int v = sc[i];）、i8→16（short t = sc[i]; 比较 t）、
   i16→32（int w = ss[i];）、有符号比较交错（if (v < -100)，注意 §5.1 缩域警告）。
   若全局 const 放置踩到当前数据区限制，fallback：拆两个编译单元，
   表指针经参数传入，被测函数加 noinline。 */
```

- **前置（已裁定，§8）**：P-B 及生产切换必须等**已验证的 ELF 执行链**（复用
  DESIGN.md 的 E3 纯 ELF 基础设施）；**lld 编译通过 ≠ 满足**。
- 判定：逐项断言 + 完整串口 + 哨兵；结果按 VERDICTS 式落盘于
  `validation/mcs251-models/probes/`，注明平台为 QEMU（不可记真机）。

## 6. 风险与回退（v2 重写，含已裁定口径）

| 风险 | 缓解 |
|---|---|
| `add wr,#0xff80`（ADD16ri）实证失败（P-A1） | 切换回退形 (b)：MOV16ri+ADD16rr；MOV16ri 亦不可用则 (b')：**已证 byte MOV 组装常数**（`mov 0xff`/`mov 0x80` + REG_SEQUENCE）+ ADD16rr——**仍需 P-A2 组合探针**（"已证原语的组合"不豁免）。按选型分别放行（§5.2 P-A2）。v1 的"两字节 ADD8ri 拼偏置"回退**已删除**：进位语义未解决 |
| imm 形式全链失败（ADD16ri + MOV16ri 均失败） | (c)/(d) 仍依赖 MOVDRri/MOVHDRi，**不得无条件回退到它们**；若 MOVDRri/MOVHDRi 一并失败：i8→i16 退 (b')（全已证原语组合，仍过 P-A2）；i8→i32/i16→i32 无全已证直线形（案 B 移位链 i16→i32 不可行）→ **升级 Alice 裁定**，不得静默引入未证原语 |
| `sext(trunc)` 漏网（SIGN_EXTEND_INREG 未建模） | I4 显式 Custom（InnerVT i8/i16）+ `sext.ll` 单列回归（E14） |
| CHECK 行序不稳定 | lit 以实际发射序固化；调度噪声大时改为单指令存在性断言 + QEMU 端到端兜底语义 |
| -O0 FastRA lane 坑（E12）+ CSE 合并（E16） | 实现约束：需独立 vreg 的常数**不得**靠重复 `buildMOV8ri(0)`（MachineNode CSE 会合并同形节点）；`sext-pressure.ll` / `-O0` 变体哨兵 |
| 两地址约束破坏多用途源值 | XOR 作用于副本/lane 提取值，不写回源；`sext-pressure.ll` 多用途用例必测 |
| 删除老 signed 路径引入隐性回归 | 已裁定②：直接替换；E5 证实无基线依赖；验收 = LLVM sext 语义 + 宿主期望 + 新正例（`sext.ll`） |
| ADDC/SUBB 偷渡 | 已裁定④：单独立项与本战役解耦；本战役所有形零新增 td 指令，任何回退不得引入未建模指令 |

## 7. v1 开放问题 → 已裁定结论（Alice，2026-09-07）

1. **实证排期 → 已裁定①**：先 P-A、再实现、再 P-B；**P-A 立即可进独立探针窗口，
   不等 lld**（§8 依赖图：P-A 走 sdas251/sdld 链，与 lld 无关）。
2. **老路径处置 → 已裁定②**：老 setcc+select signed 路径**直接替换**；验收口径 =
   LLVM sext 语义 + 宿主期望 + 新 lit 正例（不以老形状为基准）；只替换 `LowerExtend`
   的 signed 分支。
3. **EXTLOAD 精化口径 → 已裁定③**：维持 `ZERO_EXTEND`（现状合法精化），不改
   ANY_EXTEND。
4. **ADDC/SUBB 建模立项 → 已裁定④**：单独立项、与本战役解耦；本战役任何回退不得
   借道偷渡未建模指令（案 C 本轮不做）。
5. **探针平台 → 已裁定⑤**：QEMU 通过不可记真机；**真机补测列入验收矩阵单独记账**
   （不混入 QEMU 判定）；保留 RSTCFG 禁触等安全规则。
6. **sext 与截断 store 往返 → 已裁定⑥（必测）**：`sext-roundtrip.ll` 必须包含
   i16→i8 / i32→i8 / i32→i16 截断 store 往返 + 宽 store 后 reload。
7. **`movh dr,#imm` 顺序敏感性 → 已裁定⑦（必须有中间态断言）**：写入 P-A1。清高字的
   是 `mov dr,#imm16`（MOVDRri）**不是 `movh`**；断言序 = 先 byte 写非零高字哨兵 →
   MOV 后断言高字为零、低字正确 → MOVH 后断言高字正确、低字不变。

## 8. 阶段归属与依赖图（v2 新增，已裁定）

**战役定位**：独立 SEXTLOAD 摘雷小战役，挂接 DESIGN.md 的 F.2。

| 阶段 | 内容 | 入口条件 | 角色分工 |
|---|---|---|---|
| 阶段 1：P-A 探针纪律 | §5.2 P-A0/A1/A2 | 无——**不依赖 lld**（sdas251/sdld 链 + QEMU），立即可排独立探针窗口 | Mai 出规格（本稿 §5.2 即规格）、Sakuna 主测量、Shizuka 构建校验、PM 排窗口、Alice 审核 |
| 实现子阶段（本战役） | I1–I4 lowering + §5.1 四个 lit 文件 | P-A 结论冻结（VERDICTS 落盘 `probes/`） | Mai 实现，Alice 审核合入 |
| 生产执行闭环：P-B | §5.2 P-B（生产链端到端） | **已验证的 ELF 执行链**（复用 DESIGN.md 的 E3 纯 ELF 基础设施；**lld 编译通过 ≠ 满足**） | 同探针纪律分工 |

```
[P-A0 harness 正控制] → [P-A1 单原语] → [P-A2 完整组合]     ← 不依赖 lld
                              │ 结论冻结（validation/mcs251-models/probes/ 落盘）
                              ▼
      [实现子阶段：I1–I4 lowering + sextload/sext/sext-roundtrip/sext-pressure]
                              ▼
[P-B 生产执行闭环]  ← 前置：已验证的 ELF 执行链（DESIGN.md E3 纯 ELF 基础设施）
```

真机补测（裁定⑤）：列入验收矩阵单独记账，不并入 QEMU 判定；保留 RSTCFG 禁触等
安全规则。
