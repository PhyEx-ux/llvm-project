# V2 两级嵌套夹具（文档 §10·低→高→低→主）

状态：2026-09-10。构建+静态检查通过；**QEMU 单发与 10000 轮均 PASS**；未上实板。

## 优先级写法（先查模型与手册后确定）

- qemu 模型（`target/mcs51/cpu.c`）：`irq_priority = IPH 位×2 + IP 位`（INT0..UART1，
  槽 0..4）；仅当 `新级别 > 当前级别` 才抢占，深度上限 8。IP=0xB8、IPH=0xB7。
- STC32G 实板同为 IP/IPH 两位四级（PL/PH）；本夹具用 **Timer1 = 级别 1（IP.3=1/IPH.3=0）、
  Timer0 = 级别 3（IP.1=1/IPH.1=1）**，主程序 `IP=0x0a, IPH=0x02`。向量：Timer0 槽 1
  @0xFF000B、Timer1 槽 3 @0xFF001B（冻结公式 0xff0003+8×槽，EJMP）。

## 编排（确定性嵌套，无竞态）

1. 主程序装填两定时器（模式 1），置优先级，调哨兵。
2. 哨兵（汇编）置 R0-R31 哨兵 + 快照，开 `EA|ET0|ET1`，`orl 0x88,#0x40`（TR1）。
3. **Timer1 ISR（低）**：`LOG=0xA1` → `PHASE=1` → `TR0=1`（**在自身体内放行高优先级**） →
   **自旋等待 `HITS0==1`**（窗口保持到高 ISR 真正进入；有界，防死等） → `PHASE=2` →
   `TR0=0` → 计算结果（证明嵌套返回后低 ISR 上下文完好）→ `LOG=0xA2` → DONE。
4. **Timer0 ISR（高）**：`LOG=0xB1` → **`WPHASE=PHASE`（抢占命中位置证据）** → 8 字节
   寄存器压力 → `LOG=0xB2` → `TR0=0` → RETI（回到 Timer1）。
5. 主程序逐项校验：`LOG == A1,B1,B2,A2`（嵌套次序）、`HITS0==HITS1==1`、`WPHASE==1`、
   Timer1 结果、R0-R31/SP/DPTR 哨兵（两级 37 字节保存集叠加后的复合完整性）、guard。

每轮 SEED 变化 → 自旋长度/结果变化；`r` 模式 10000 轮逐轮校验。

## QEMU 记录

```sh
qemu-system-mcs251 -M stc32g144k246 -bios v2-nest.hex -nographic -monitor none -serial stdio
```

- 单发：`V2N-v1 RESULT case=s status=PASS … hits=01/01` +
  `NESTED=LOW->HIGH->LOW->MAIN WPHASE=01 ORDER=A1,B1,B2,A2`。
- 10000 轮：`status=PASS completed=2710 … hits=01/01`。
- **实测耗时与预算**：`r`（万轮）在最终工具链（无 -O0 回退）+ 当前模型下实测约
  **210 秒**（2026-09-10）。运行超时预算取 **≥240 秒**（check.py 的
  `QEMU_RUN_BUDGET_S = 240`，旧预算 150 秒会截断运行）；预算内被终止按 TIMEOUT
  记，不计 PASS。

## 实板操作

烧录 `v2-nest.hex`（IRC=24MHz，UART1 P3.0/P3.1 115200 8N1）。发 `s` 单发、`r` 万轮。
`PASS` 判据同上；`RUN` 后无输出 = 挂死（记 TIMEOUT-挂死，指向 RETI/嵌套返回路径）。

## 构建与检查

```sh
bash build.sh /tmp/v2nest
```

`check.py`：两个向量（槽 1/槽 3）分别落 `_timer0/_timer1`；**两个编译器 ISR 各自
37B 保存/逆序恢复/单 RETI**；主程序 IP/IPH 装载存在；Timer1 体内 `TCON|=0x10` 开窗；
哨兵结构与 DJNZ-free 轮询；保留区与栈容量。

## 边界（如实标注"待实板"）

- **逐指令抢占窗口**：QEMU 的受理粒度是整条指令，真实硅片的受理时序（以及
  IPH 两位优先级语义）只能实板验证；本夹具的 `WPHASE`/LOG 提供的是"命中窗口"级证据。
- 中断延迟、TF 自清时点等微时序差异可能改变 `WPHASE` 具体值（1/2 皆合法：分别为
  窗口内/窗口刚关），判读以 LOG 次序与 hits 计数为主。
- Timer0/Timer1 时钟分频若与模型不同，自旋预算（0x8000 次）仍有 >8 倍裕量；若实板
  `HITS0=0`，优先检查 AUXR T0X12/T1X12 位与 TMOD。
