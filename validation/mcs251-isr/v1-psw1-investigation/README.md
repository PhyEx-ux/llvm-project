# V1 PSW1 差异调查包（文档 §9）

状态：2026-09-10（第二轮修订）。构建、静态检查、**三张镜像 QEMU 全量复跑（g 矩阵 +
r 直方图）全部通过**，实测与 [PROTOCOL.md](PROTOCOL.md) 逐 combo 预测逐位一致；
**未上实板**——实板判读按 PROTOCOL.md 回传。机器码逐指令审计见
[PSW1-AUDIT.md](PSW1-AUDIT.md)。

第二轮修订（Alice review FAIL 项的修复）：

1. 判读表按**两视图共享物理标志语义**重写（后写 PSW1 合法覆盖先写 PSW 的
   C/AC/OV/RS；DJNZ 只写 N/Z），不再把 combo4/7 回读 00、combo3/6 的 82/06 之类的
   正常共享视图行为误判为 H-D；H-A~H-D 收窄为各自单一证据，其余偏离一律专项排查。
2. I 阶段非零 N/Z 建立点移到开窗 `orl TCON,#0x10` **之后**（该指令按模型把 N/Z 清 0，
   旧固件 combo1/2 的 entry PSW1 实测 00，I 阶段无法证明非零 N/Z 被帧恢复）；
   复跑后 combo1/2 的 `en=01/02、01/20` 非零且 `af==en==w`，限制已消除（PROTOCOL §3）。
3. 构建以**最终工具链**（含 E1 分支松弛修复的 clang/llc）O2 复跑，**无 -O0 回退**
   （三个 `*-O2-failure.log` 全空）。

第三轮修订（Alice 第二轮 review 判 FAIL 项的收尾修复；实现侧无返工）：

1. **H-A 负向对照改为完整 before/after 保持判据**（PROTOCOL §0/§2.2）：经典语义
   （DJNZ 不动标志）下写入值应**原样保持**而非归零——判据改为 af==bf 逐 combo
   完整保持（combo0→00、combo1→02、combo2→20），§0 写明模型语义与经典语义
   双分支基准，两支判读各自成立。
2. **"帧恢复 PSW1 全部位"收窄为实测覆盖范围**（PROTOCOL §2.3/§2.4）：只声明实际
   建立并验证的 N、Z、C、OV 及其组合；AC/RS 明确标注本轮仅零值覆盖（combo4/7，
   且共享语义本就覆盖为零），非零行为待后续 combo。
3. **开窗时序固化为构造保证**：`_v1_ix` 每轮开窗前显式重置定时器（TCON=0 停表
   并清残留 TF0/挂起请求、TMOD=0x01 十六位、TH0/TL0=00 重装），首次溢出距 TR0
   置位恒为 65536 个定时器时钟，不再依赖上一阶段遗留状态；`check.py` 加静态断言
   （重装/清 pending 序列存在且先于开窗 `orl`）。PROTOCOL §2.3 写明 en 判定前提，
   `en != w` 一律先按"受理过早/开窗状态不符"处理，专项排查优先于任何标志归因。
4. **三臂 QEMU 复跑**（/tmp/v1p-r3）：结果与第二轮记录及修正后判读表逐位一致
   （时序固化仅新增 4 条 mov/12 字节，见 PROTOCOL §3 第三轮记录）。

## 目标

解释单发与万轮实板均出现的 `PSW_RAW=00/00 PSW1_RAW=00/20`。不掩盖、不屏蔽 0x20：
0x20 在模型位序下是 PSW1 的 N 位，来源待实板对照实验定案。

## 三臂对照（其余条件一致）

| 镜像 | Timer0 ISR | 阶段 |
|---|---|---|
| `v1-baseline.hex` | 无（向量指向默认 fail-stop） | W + L |
| `v1-minisr.hex` | 手写最小 ISR（汇编，补记入口 PSW/PSW1 → 故意 `0x7f+0x01` 置 N/Z/AC/OV → DONE → RETI） | W + L + I |
| `v1-compiler.hex` | 原 T10 编译器 ISR + helper（逐字节同管线复刻） | W + L + I |

阶段（全部在手写汇编模块内，采样点指令级可控）：

- **W**：写 PSW/PSW1 组合 → 立即回读（写-读一致性）。
- **L**：写组合 → before → **T10 同形 `jb`+3×`djnz` 预算等待（IE=0）** → after。
  对照"等待循环自身对 PSW1 的污染"。
- **I**：写组合 → before → 开 EA/ET0+TR0 → **开窗后重新建立 combo**（保证非零 N/Z
  存活到受理时刻）→ **纯 `jb`/`sjmp` 轮询（无 DJNZ，标志中性）** → after；
  minisr 臂额外记录受理瞬间 `en`。窗口内标志只被被测 ISR 改动。

8 个 combo 覆盖 Z/N/C/AC/F0/OV/RS1 各物理标志（RS 因共享覆盖实际不可置 1，
这正是被测语义的一部分，见 PROTOCOL §2.1）。

## 判读表要点（详见 PROTOCOL.md §2）

- 预期值全部按共享语义逐 combo 推导：`rb-PSW1 == w-PSW1` 恒成立；`rb-PSW` 的
  C/AC/RS/OV 取 w-PSW1（后写覆盖）、F0 取 w-PSW、P=ACC 奇偶；L 阶段 af 按**模型
  语义分支**推导（终拍 DJNZ 置 Z=1/N=0：af-PSW1 仅 combo3=82、combo6=06，其余
  =02），**经典语义分支（H-A）的判据是 af==bf 完整保持**（combo0→00、combo1→02、
  combo2→20，见 PROTOCOL §0 双分支基准）；I 阶段主键
  `af-PSW1 == en-PSW1 == w-PSW1`（combo1/2 现有 02/20 非零证据）。
- **只有偏离对应分支预测的行才算异常**，且先进专项排查；H-A（DJNZ 经典语义，
  判据=af==bf 完整保持）、H-B（RETI 不恢复 PSW1）、H-D（PSW1 写路径）分别只由
  L、I、W 单一证据引用。
- I 阶段"帧恢复成立"的范围限定：实测覆盖位 N/Z/C/OV；AC/RS 仅零值覆盖，非零
  行为待后续 combo。en 判定前提：`_v1_ix` 已每轮显式重置定时器，en==w 为构造
  保证；`en != w` 先按"受理过早/开窗状态不符"排查，不直接归因标志指令。
- combo3/4/7 的 W 回读若呈 01/40/08，指向硅片两视图物理独立（模型-硅片差异证据），
  按 PROTOCOL §2.1 处理，不是缺陷标签。

## QEMU 记录（2026-09-10 第二轮，`/home/liu/build-qemu/qemu-system-mcs251 -M stc32g144k246 -bios <hex> -serial stdio`，最终工具链构建 /tmp/v1p-fix；**第三轮复跑 /tmp/v1p-r3 见文末**）

- W：8/8 与 PROTOCOL §2.1 预测一致（00/00、01/02、01/20、81/80、00/00、20/00、
  05/04、00/00）。
- L：8/8 `bf == rb`、`af` 与 §2.2 一致（combo3=81/82、combo6=05/06，其余 `*/02`）；
  `r` 模式 128/128 → `value=02 count=80`。
- I（minisr）：8/8 与 §2.3 一致；**combo1 `en=01/02 af=01/02`、combo2
  `en=01/20 af=01/20`**（非零 N/Z 进帧并恢复）；combo3 81/80、combo6 05/04；
  combo5 `af=21/00`（F0+P 泄漏，PSW 专属位不经帧，预期）。
- I（compiler）：8/8 `af-PSW1 == w-PSW1`（含 combo1→02、combo2→20）；combo5
  `af-PSW=20`（F0 经 `pop psw` 恢复）；`en` 恒 00/00。
- 三臂 W/L 段逐字节一致（同模块）；无任何偏离修正后判读表的行。
- 冻结 t10-g12.hex 同机对照（第一轮记录）：`PSW_RAW=00/00 PSW1_RAW=00/00`（对比实板
  00/20 → 差异为等待循环受理相位的 N 位，见审计 §5）。

**第三轮复跑（2026-09-10，`_v1_ix` 显式定时器初始化 + 判读表收尾修订后，/tmp/v1p-r3）**：
三臂 `g` 矩阵 + minisr `r` 直方图与上列第二轮记录**逐位一致**（W 8/8、L 8/8、
I minisr 8/8 含 combo1 `en=01/02 af=01/02`、combo2 `en=01/20 af=01/20`，I compiler
8/8，`r` 128/128 → `value=02 count=80`）；时序固化仅新增 4 条 mov（12 字节），
`check.py` 全部断言通过。本轮构建说明：`/home/liu/build-mcs251-s1` 的 llc 于当日
07:06 重建后在 main.c 上触发 MOV8a 指令尺寸断言崩溃（-O2 与 -O0 均崩，属后端
回归、与本包改动无关）；本轮以同代 llc（`LLC=/home/liu/build-mcs251/bin/llc`，
含 E1 修复的 09-09 21:18 构建）复跑，三个 `*-O2-failure.log` 仍全空、**无 -O0
回退**。

## 实板操作

1. 烧录对应 hex（STC-ISP，IRC=24MHz），串口 115200/8N1，P3.0/P3.1。
2. 先发 `g` 跑矩阵，再发 `r` 跑 tally；按 [PROTOCOL.md](PROTOCOL.md) 判读并回传全量输出。
3. I 阶段轮询无超时：若 RUN 后无行输出，说明 ISR 未受理或未返回——这本身即判读表的一行
   （记 TIMEOUT-挂死）。

## 构建与静态检查

```sh
bash build.sh /tmp/v1p        # 产出 v1-baseline.hex / v1-minisr.hex / v1-compiler.hex
```

构建工具默认取 `/home/liu/build-mcs251-s1/bin/{clang,llc,yaml2obj,llvm-objcopy}`、
`/home/liu/build-mcs251-lld/bin/{mcs251-lld,lld}`、`/home/liu/build-sdcc/bin/{sdas251,sdld}`。
2026-09-10 起后端已含 E1 分支松弛修复，三臂全部 `-O2` 一次通过（`check.py` 的
manifest 记录 O2）；build.sh 保留 -O0 重试分支仅作后端回归时的安全网，正常构建
不触发——**交付判读以无回退构建为准**（回退会改变 codegen，须在 manifest 中注明）。

`check.py` 校验：三臂链接产物、复位/Timer0 向量落点（baseline 落默认项，另两臂分别落
模块内/编译器 `_timer0`）、各阶段 11 压/逆序 11 弹 + ERET、L 阶段字节形与 T10 等待一致、
I 阶段 DJNZ-free 且 **combo 重建点位于开窗 `orl` 之后**、**开窗前存在显式定时器重置
序列（TCON=0 清残留 TF0、TMOD=0x01、TH0/TL0 重装）且先于开窗 `orl`**、minisr 的入口
快照 + ADD 扰动 + RETI、编译器臂 37B 保存/逆序恢复/单 RETI/ecall helper。

## 边界

- 本包不改变 T10 冻结产物与验收状态；不宣称硅片 PSW1 位序（这正是被测项）。
- QEMU 结果是模型语义证据；0x20 的最终归因以实板 I 阶段两臂对照为准。
