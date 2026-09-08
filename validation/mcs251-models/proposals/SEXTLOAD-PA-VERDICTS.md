# SEXTLOAD P-A 原语探针 VERDICTS 汇总

**战役**：SEXTLOAD 摘雷小战役 · 阶段 1 P-A 探针纪律（挂接 DESIGN.md F.2）
**执行**：Sakuna（编译器测试/测量工程师，GLM5.3Flash）· 2026-09-07
**设计依据**：`SEXTLOAD-DESIGN.md` v2 §2.5 / §5.2 P-A0/A1/A2 / §6 按选型分别放行
**平台**：QEMU stc32g144k246，sdas251 → mcs251_ld.py → HEX → `-serial stdio`，**无 icount**，timeout 10（qemu 退出码 124=halt 死循环预期，仅记录不作判定）
**证据资产**：`/home/liu/mcs251-models-probe/sextload-pa/{pa0,pa1,pa2}/`（探针源 `gen_all.py`、probe.asm/.lst/.hex、期望卡 expectations.{json,txt}、串口原文 qemu.out、compare-*.txt、verdicts-*.txt、VERDICTS.txt、编码 encoding-bytes.txt、命令与布局 commands-and-layout.txt、工具指纹 tool-fingerprints.txt、manifest.sha256）
**真机声明（裁定⑤）**：本结案只覆盖 QEMU 侧；真机补测列入验收矩阵单独记账；RSTCFG（0xFF direct）禁触等安全规则全程保持，探针未触碰任何配置类 SFR。

---

## 总判定

| 层级 | 判定 | 说明 |
|---|---|---|
| P-A0 harness 正控制 | **PASS**（QEMU） | 串口链路/快照纪律/观察 hop/判定脚本与待测结论解耦；打印不污染双方案实证（P1 活体字段） |
| P-A1 单原语 | **PASS**（QEMU） | 九项原语全部 PASS，零哨兵破坏，无 INCONCLUSIVE |
| P-A2 完整组合 | **PASS**（QEMU） | 七形态 × 4 边界输入 = 28 组全 PASS；正数进位路径真观测 |

---

## P-A1 逐原语判定表

| 原语 | §2.5 原档位 | 判定 | 证据要点 | 编码 |
|---|---|---|---|---|
| `add wr12,#0xff80`（ADD16ri） | 本轮待测 | **PASS** | 4 样本含无符号回绕：0080+FF80→**0000**、00FF+FF80→007F、007F+FF80→FFFF（无回绕边界）、0040+FF80→FFC0；偏置回绕运行证据成立 | `2E 64 FF 80`（imm 大端） |
| `mov wr,#imm16`（MOV16ri） | 证据待定位 | **PASS** | 历史证据**已定位**：p7/r1 W1 字段（`mov wr12,#0x1234`→大端 12,34，P7R1 PASS）；本轮 wr14 三样本（FF80/1234/0000）独立复测 | `7E 74 FF 80` |
| `mov dr,#imm16`（MOVDRri） | 证据待定位 | **PASS** | 裁定⑦中间态：lane 写非零高字哨兵（C3,D5 实测落位）→ mov 后**高字=00,00（清高字者是它）**、低字正确 | `7E 68 imm16` |
| `movh dr,#imm16`（MOVHDRi） | 证据待定位 | **PASS** | 承上态 movh 后高字正确、**低字原样保活**（8000→56,78,80,00；AACD→BB,BB,AA,CD）；先 mov 后 movh 顺序实测成立 | `7A 6C imm16` |
| `xrl r,#0x80` / `mov r,#imm` / `mov r,r` | 历史已证 | PASS（复验零回归） | 0F→8F；66 链 | — |
| `add wr,wr` / `add dr,dr` | 历史已证 | PASS（复验零回归） | 0x1111+0x2222；0x22222222+0x11111111 | `2D 67` / `2F 64` |

## P-A2 逐形态判定表（结果=设计 §5.2 期望表逐字节）

| 形态 | 组合 | 结果（按输入序） | 判定 |
|---|---|---|---|
| A = (a) i8→16 主形 | 零 lane + XOR8ri + ADD16ri | 0000 007F FF80 FFFF | **PASS** |
| B = (b) 回退一 | + MOV16ri 常数 + ADD16rr | 0000 007F FF80 FFFF | PASS |
| C = (b') 回退二 | + byte MOV 常数 + ADD16rr | 0000 007F FF80 FFFF | PASS |
| D = (c) i8→32 主形 | 3×零 lane + XOR + MOVDRri/MOVHDRi 偏置 + ADD32rr | 00000000 0000007F FFFFFF80 FFFFFFFF | **PASS** |
| E = (c') 变体 | 零字改 MOV16ri（门控通过） | 同 D | PASS |
| F = (d) i16→32 主形 | lane 副本 XOR（源保活实测）+ 偏置 + ADD32rr | 00000000 00007FFF FFFF8000 FFFFFFFF | **PASS** |
| G = (d') 变体 | 零字改 MOV16ri（门控通过） | 同 F | PASS |

**正数进位路径真观测**（§2.2 禁 nuw 的硬件侧支撑）：F1 源 0000→0x1_0000_0000 全宽回绕得 00000000（低字向高字进位）；F2 源 7FFF→00007FFF（高字进位）；A1 0080+FF80→0x1_0000 回绕得 0000。

---

## 按选型放行建议（§6，按选型分别放行）

1. **i8→i16：放行主形 (a)**（ADD16ri，P-A1+P-A2 双层过）。回退 (b)/(b') 独立 PASS 已记账，无需切换。
2. **i8→i32：放行主形 (c)**（MOVDRri+MOVHDRi 偏置构造已证）。变体 (c') 门控通过，实现可按代码量自选，不构成放行阻塞。
3. **i16→i32：放行主形 (d)**。变体 (d') 门控通过，同上。
4. 实现纪律再确认：偏置加法**禁止附加 nuw**（回绕为正数输入的必需行为，已真观测）；零/常数 lane 独立 vreg（E12/E16）；XOR 只作用于副本（F/G 已证源保活路径）。
5. §2.5 证据档位回填：ADD16ri → QEMU 运行已证；MOV16ri/MOVDRri/MOVHDRi → 证据定位/本轮补测完成，均升为"QEMU 运行已证"。探针资产与结论冻结后，方可在 `MCS251InstrInfo.td:300-302` 实证注释追加 imm 形式 QEMU 记录（同步更新 E8 口径）——留给实现子阶段执行。

## 过程偏差与限界（在案）

1. **P-A1 首轮 FAIL 归因**：WR14 同时充当读回 hop 目标与哨兵，hop 数据覆盖 r14/r15（实测值与 hop 内容逐一吻合，与被测指令无关）；修正为哨兵在 hop 前断言后重跑全绿。
2. **裁定⑦哨兵写法偏差**：r16+ 无 byte 寄存器名（sdas251 拒绝 `mov r24,#imm`），P-A0 D1 证 direct 0x18-0x1B 与 DR24 lane 不别名；非零高字哨兵改经 MOV16ri lane 写，落位经 T3p 字段实测——裁定⑦中间态语义完整满足。
3. **MOVD 观察 hop 依赖**：DR 字节读回经 MOV16rr/MOV32rr hop（生产 copyPhysReg 同款指令），hop 于 P-A0 M1/M2/M3 在相同寄存器号上先行验证。
4. QEMU 单进程串口运行，未触碰并行 ninja 构建；未运行 lit/ninja/git；llvm/ 源码零改动。

## 工具冻结

QEMU 11.1.0（sha256 0907e2e4…373a）、sdas251 V05.50.4+NoICE+SDCCmods-WIP-R14（878887af…1d32）、mcs251_ld.py（45eac91f…4ef0）、板 profile stc32g144k246（EDATA_END=0x3FFF）。完整指纹见各层 tool-fingerprints.txt。
