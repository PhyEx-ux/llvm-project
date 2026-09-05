# demo×QEMU 测试体系设计（DESIGN）

状态：定稿 v1（2026-09-05，PM）。输入：Momo 语料扫描（/home/liu/mcs251-demo-scan/，
275 文件/2751 函数/88 demo）、Moka QEMU 外设实测（/home/liu/mcs251-qemu-periph/
REPORT.md，10 probe 全 PASS）、现有固件资产（validation/mcs251-firmware/）。

## 1. 目标与定位

1. **真实代码形状的回归保护**：用官方 demo 的真实算法/外设交互形态回归后端，
   替代纯手写 .ll 的形状盲区。
2. **能力缺口的持续度量**：频率数据 × 后端能力 → 优先级；每次能力落地后矩阵
   更新、测试扩容（缺口 → 正向测试）。
3. **延续铁律**：一切判定以 QEMU 串口 transcript 为唯一 oracle；任何新硬件
   行为猜想先 QEMU 实测再进测试。

被测生产链（零 SDCC 工具）：`llc -mtriple=mcs251-unknown-none -filetype=obj →
python3 validation/mcs251-ld/mcs251_ld.py --mcs251-abi -f <.lk> → .ihx→.hex →
qemu-system-mcs251 -M stc32g144k246 -bios <.hex> -serial stdio`。

SDCC 的角色（oracle 专用，符合 de-SDCC 战略）：harness C 的编译器（Step 4 前）
+ 目标行为参考实现。

## 2. 分层

### T1 算法内核回归（L0，最高价值）

- 选案：Momo `l0-kernels.tsv` 67 个候选先入 12–20 个。**选案标准：可观察
  输出**（返回值或输出缓冲），覆盖状态机/查表/移位解码/数值计算/指针遍历/
  函数指针/递归；delay 类只做"能编译+能终止"冒烟，不做行为 oracle。
- 提取改写规则（只改类型不改语义）：u8/u16/u32 显式宽度 typedef；`bit`→u8；
  `code`→const；去 `__sfr`/中断。多参函数保留原签名记录在 T3，首批不进 T1
  （多参是缺口本身，OSEG 落地后升级为正向测试）。
- **双 oracle 三角**：
  - Oracle-A 逻辑真值：同一 kernel.c 在 host 原生编译执行，打印与固件相同
    格式的 checkpoint 行，逐字符 diff。
  - Oracle-B 目标行为真值：同一 kernel.c 交 SDCC 编译→sdld→QEMU，串口 diff。
  - 判定：被测串口 == A == B（差异必须逐条归因记录）。
- 形态：每用例一个目录 `t1/<name>/`：kernel.c（+kernel.expect 或生成脚本）、
  wrapper（MCS251_CHECKPOINTS 宏，复用 harness-template.c 模式）。
- host 工具链（clang/gcc）可用性由实施者先盘点并记录；不可用时的降级路线
  （SDCC-only oracle / 手写 .ll）在实施记录中说明。

### T2 SFR/GPIO/UART1 低层交互（L1+L2 可测子集）

- 形态：LLVM IR 级固定地址 volatile 指针（如 `store volatile i8 %c,
  ptr inttoptr(i32 0x99 to ptr)` = SBUF；0x80=P0、0x90=P1、0x98=SCON）。
  常量地址 ≤0xff 已走 direct 寻址可达 SFR。
- 场景来源：demo 01 跑马灯（P0 模式输出+回读）、demo 03 数码管（字库查表+
  位扫描）、demo 10 串口收发（TI/RI 轮询 echo）。
- 缺席外设哨兵：对 QEMU 静默 0 区（UART2 S2CON 等）写特征值再读回，断言
  读回为 0——防止未来 QEMU 行为漂移无声改变测试前提。
- 判定：串口 transcript（`B...PASS` 模式，失败输出 `FAIL expected=… got=…`）。

### T3 能力缺口矩阵（文档型，非执行）

频率数据（Momo per-feature-counts.tsv，2751 函数）× 后端能力 → 优先级：

| 特性 | 函数占比 | 文件占比 | 后端现状 | 优先级结论 |
| --- | --- | --- | --- | --- |
| 多参数调用（≥2 实参） | 40.6%（1117 函数/4400 调用点） | 57.8% | OSEG 未实现，响亮报错 | **最高**：demo 过半文件被此挡住 |
| bit 类型 | 11.2% | 34.2% | 无 i1/bit lowering | 第二（配合 SFR 改写） |
| __interrupt | 6.9% | 36.4% | 无；可用 asm 向量 stub + 普通 LLVM 函数绕过（待 QEMU 实测） | 第三（T4 先行验证绕过形态） |
| SFR 引用 | 26.7% | 65.8% | 固定地址 volatile 指针可达（T2 验证） | 改写层解决，非后端缺口 |
| 浮点 | 3.5% | 9.8% | 无 | 低（TFPU QEMU 行为未实测，不预设） |
| 结构体按值 / varargs 定义 | 0% / 0.1% | — | 无 | 忽略 |

维护规则：每项能力落地后更新本表，并把对应 demo 函数从"依赖缺口"移入
T1/T2 扩容清单（Momo 数据可按特性反查）。

### T4 定时器/中断/外设探测回归（QEMU 前提保护）

- 基线：Moka 10 个裸 asm probe 原样入库（transcript 已留档），保护
  "测试体系对 QEMU 行为的假设"不被 QEMU 升级无声破坏。
- 升级：LLVM 侧 ISR 形态假设——`vector_stub: lcall _isr_body; reti`（ERET
  弹 lcall 帧、reti 弹硬件帧）——**必须先 QEMU 实测**再固化为测试。
- 已知约束（来自实测）：HOME 复位 stub ≤3 字节（INT0 向量 0xFF0003）；
  ES 使能期间打印需先关 ES（ISR 自清 TI）；无 icount，定时器断言用轮询+
  超时或同指令流比值。

## 3. 目录与 runner

```
validation/mcs251-demo-test/
  DESIGN.md            本文档
  run-tests.py         统一 runner（WSL python3；--filter t1|t2|t4|all）
  t1/<case>/           kernel.c + wrapper + 判定材料
  t2/<case>/           .ll 或改写 .c + wrapper
  t4-probes/           10 个 asm probe + 构建脚本 + 期望 transcript
  RESULTS.md           最近一次全量结果与已知差异清单
```

- runner 职责：构建（llc/sdas251/sdcc）→ 链接（mcs251_ld.py 生产链；sdld
  仅 oracle 链）→ QEMU（timeout 判 124 预期）→ 串口解析 → JSON 结果 + rc。
- 工作目录纪律：可再生产物放 /home/liu/mcs251-demo-test/（持久），
  /tmp 只放符号链接。所有 agent 开工时把当前 llc/QEMU 二进制**冻结**到
  自己工作目录，避免他人重建 llc 造成测试基准漂移。
- 判定：串口含完整 `PASS` 且无 `FAIL`（T1 另加双 oracle diff=0）。

## 4. QEMU 不可测清单（直接引用外设图，不重复测试）

低功耗停振（PCON.IDL/PD 不生效，demo 07/08）、UART2/3/4、ADC、WDT、T2/T3/T4、
PWM、SPI、I2C、CAN、RTC、DMA、USB、比较器、INT2/3/4。依赖这些的 demo 只做
L0 提取，不做外设行为测试。

## 5. 实施分工

| 谁 | 什么 | 交付 |
| --- | --- | --- |
| Shizuku | T1 全链 runner + 首批 12–20 内核端到端 | run-tests.py + t1/ 用例 + RESULTS.md |
| Sakuna | T2 SFR/GPIO/UART 用例 + 缺席哨兵 | t2/ 用例并入 runner |
| Moka | T4 probe 入库 + ISR stub 形态 QEMU 实测 | t4-probes/ + 实测报告 |
| Momo | 语料收尾 + T1 内核提取改写（按选案标准） | t1/ kernel.c 来源清单 |
| Alice | （并行线）端序统一专项——Phase 11 最高优先遗留 | 独立报告，另行验收 |

## 6. 与 Step 4/5 的关系

T1 内核与判定逻辑在 Step 4（自建 crt0/HOME/_main）后原样存活——只换 harness
生产方式，不换 oracle 与用例；本测试体系因此同时是 de-SDCC 的验收基准。
