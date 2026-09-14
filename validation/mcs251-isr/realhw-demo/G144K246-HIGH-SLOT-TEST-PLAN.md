# G144K246 高槽（52..126）真机实测计划

状态：**仅计划，未执行**。本文档由 G1-5 切片编写（2026-09-14），未烧录任何板卡、未运行任何
模拟器。规范依据：`validation/mcs251-models/proposals/G1-ISR-PROFILE-EXTENSION-DESIGN.md`
§1.1（Legal ≠ 板上有外设；G144K246 证据表不声明旧 STC32G 板有新增外设）、§5.6（高槽运行用例
要有板卡实际路由证明，不能只改 `interrupt(N)` 就声称测试了相应外设）、§6.1 三层分账。

## 0. 目的与边界

- 目的：证明 G1 127 槽 profile 中高槽（52..126）的**向量入口在真芯片上被真实外设事件命中**，
  且命中后 ISR 协议（37B 保存/逆序恢复/RETI，ProtocolVersion=2）行为正确。
- 边界一（三层分账）：本计划证明「控制器把请求路由到向量」+「CPU 入口执行」两层；「外设真实
  产生事件」一层由外设输出现象/状态寄存器/仪器佐证（各向量注明）。CPU 能跳高向量地址 ≠
  控制器可注入 ≠ 真外设产生该源。
- 边界二（QEMU，见 §6）：G1-0 首查未取得 stc32g144k246 机器向量模型源码，QEMU 一律记 skip，
  不得以模型结果替代真机。
- 边界三（适用范围）：实测 PASS 只对受测板（建议 STC32G144K246 实验箱 V1.4，SCH/PCB 随官方
  demo 目录提供）负责；不外推到其他 STC32G 板。
- 本计划不改变任何既有 NOT_RUN 结论；`board-results.json` 在实测完成并经 owner review 前不动。

## 1. 固件形态（复用 realhw-demo 机制）

- 构建入口：派生自 `build.sh`（crt-irq.yaml v2、BOOT=0xFF0500/CSEG=0xFF0700、reset=LJMP 0xFF0500、
  VECS [0xFF0003,0xFF03FB)），工具链为 G1 重建登记（`./REBUILD-20260914.md`）所用三构建目录。
- 每相位一个固件（避免一板全开外设的时钟/引脚冲突），新增 `isr-high-<phase>.c`：
  - 每个采样槽一个 `void vec<N>(void) __attribute__((interrupt(N)))` 最小处理函数：清该外设中断
    标志（清除序列按手册/官方 demo，不自行发明）→ `hit[N]++`（volatile，独立 RAM 字节，位于
    显式保留区，仿 `.mcs251.DATA.fixture` 机制）→ RETI。不改其他寄存器。
  - 复用 `main.c` 的 UART1 命令循环骨架（READY/RUN/RESULT 报告、`s`=注入一次、`r`=循环注入）。
  - 仿 `sentinel()` 在前台布设 R0..R31/DPL/DPH/DPXL 哨兵与上下 guard 区（0x580/0x880 口径）。
- 静态检查扩展（check.py 派生）：对每个注册槽 N 断言 `blob(0xFF0003+8N,4) == 0x8a + addr24(vec<N>)`
  （地址取自 MAP）；default 入口 `__mcs251_isr_unhandled` 仍在 BOOT+0x102；Reserved 槽向量区无字节。
- 报告字段沿用 T10 十六进制口径，新增 `slot=`、`hits=`（该槽计数）、`crosstalk=0`、`default_hit=0`。

## 2. 判定标准（逐向量，全部满足才记该槽 PASS）

1. **向量入口被命中**：一次注入恰 `hit[N]+=1`（共享向量 96..99 需分别证明两个源各自命中同一
   入口，且读标志可区分是哪个源）。
2. **协议正确**：返回后哨兵（R0..R31、DPL/DPH/DPXL、SP）与 guard 区逐字节不变；前台心跳连续。
3. **无串扰**：注入槽 N 时其他槽 hit 不变。default 入口（冻结资产 `C2 AF 80 FE`：清 EA
   后原地不返回、不写任何计数）的未进入判据为**前台心跳与 UART 应答持续**——注意该判据
   只证明"未停在 default"，不能区分 default 与其他停跳原因；一旦心跳停摆，须结合 UART 静默
   与寄存器现场另行定位，不得直接记为 default 命中。
4. **可重复**：每槽 ≥3 次注入，结果一致；`r` 循环 ≥1000 次不漂移。
5. **第 1 层佐证**：外设侧证据（波形/回环数据/屏幕刷新/状态寄存器读数，见向量表"佐证"列）。
6. 超时（done 不为 1）记 TIMEOUT，不是 PASS；不得降低断言。

## 3. 测试向量表（67 个 Legal 高槽全覆盖，按器材分四相）

向量地址 = 0xFF0003+8N。触发配置一律**引用**官方 demo（`/home/liu/LLVM_STC32/STC32G144K246-DEMO-CODE/`）
与手册（`/home/liu/LLVM_STC32/manuals-md/G144K246/`）的现成序列，本文只给槽-源-器材对应。
Reserved 槽（81、92..95、100/101、113）无源，不设向量（HEX 中该区无字节即正确静态形态）。

### 相 A：板内源，无外接线（首跑最小集从这里抽）

| 槽 | 源 | 触发方法（引用） | 佐证 |
|---|---|---|---|
| 90/91 | PAINT/PBINT | P 口任意 IO 可中断：配 PAn/PBn 输入+边沿，用相邻输出脚给边沿（demo 68；手册 16） | 逻辑分析仪或引脚电平 |
| 96 | TMR5/TMR6 共享 | 24 位定时器溢出（demo 02；手册 17） | 计数器读数 |
| 97 | TMR7/TMR8 共享 | 同上 | 同上 |
| 98 | TMR9/TMR10 共享 | 同上 | 同上 |
| 99 | TMR17/TMR18 共享 | 同上 | 同上 |
| 67 | TMR11 | 定时器 11 溢出（demo 02；手册 17） | 计数器读数 |
| 106 | ADC2 | ADC2 转换完成（手册 §22.2；触发序列移植自 demo 56 的 ADC1 流程——该 demo 本身用槽 48/DMA_ADC_VECTOR，不是本槽；ADC2 寄存器/使能/启动/清标志步骤按手册 §22.2 改写） | 结果寄存器 |
| 107/108 | DAC/DAC2 | DAC 中断（手册 23） | 输出电压 |
| 114..116 | CMP2..4 | 比较器+内部固定比较电压（手册 20；demo 20/21） | 比较结果位 |
| 121..124 | PWMC..F | 高级 PWM 更新/周期中断（手册 28；demo 24/79/80） | PWM 波形 |
| 72..75 | DMA_PWMA/PWMC T/R | DMA 通道绑 PWM 事件上载（demo 24；手册 33） | DMA 计数/波形 |
| 125/126 | DMA_PWME T/R | 同上（PWME） | 同上 |
| 76 | DMA_ADC2 | DMA-ADC 自动存储（手册 §33；由 demo 56 的 DMA-ADC1 序列移植，DMA 通道与 ADC2 寄存器按手册 §22.2/§33 改写，demo 56 原槽为 48） | RAM 目标区数据 |
| 77/78 | DMA_DAC/DAC2 | DMA-DAC（手册 33） | 波形 |

### 相 B：跳线回环（USB-TTL 之外仅杜邦线）

| 槽 | 源 | 触发方法（引用） | 佐证 |
|---|---|---|---|
| 102..105 | UART5..8 | TXn→RXn 回环跳线，收发中断（demo 13；手册 19） | 回环数据一致 |
| 52..57 | DMA_UR2..4 T/R | UART2..4 回环 + DMA 收发（demo 59；手册 33） | DMA 长度/数据 |
| 82..89 | DMA_UR5..8 T/R | UART5..8 回环 + DMA（demo 13/59） | 同上 |
| 111/112 | SPI2/SPI3 | MOSI→MISO 回环（手册 25；demo 42 参考） | 回环数据 |
| 79/80 | DMA_SPI2/3 | SPI2/3 回环 + DMA（手册 33） | 同上 |
| 109 | I2C2 | 同板 I2C2 从机自环（demo 26 方法移植到 I2C2；手册 27） | 回环数据 |
| 60/61 | DMA_I2C T/R | I2C DMA（demo 60；手册 33） | 同上 |
| 68/69 | DMA_I2C2 T/R | I2C2 DMA（手册 33） | 同上 |

### 相 C：外设硬件（按板卡资源就位情况排期）

| 槽 | 源 | 触发方法（引用） | 器材 |
|---|---|---|---|
| 59 | LCM（TFT 彩屏） | ILI9341 I8080 接口刷新中断（demo 41；手册 32） | ILI9341 3.2 寸 TFT |
| 58 | DMA_LCM | LCM+DMA（demo 61） | 同上 |
| 66 | QSPI | QSPI Flash 读写（demo 63/64；手册 26） | QSPI Flash（W25Q） |
| 65 | DMA_QSPI | QSPI+DMA（demo 63/64） | 同上 |
| 62 | I2S | I2S 音频（demo 84；手册 34） | I2S codec/MP3 模块 |
| 63/64 | DMA_I2S T/R | I2S+DMA（demo 84） | 同上 |
| 110 | I2S2 | I2S2（手册 34） | 同上 |
| 70/71 | DMA_I2S2 T/R | I2S2+DMA（手册 34） | 同上 |
| 117/118 | DMA_CANFD1 T/R | CANFD1+DMA（demo 44/82；手册 38） | CAN 收发器+对端节点 |
| 119/120 | DMA_CANFD2 T/R | CANFD2+DMA（demo 44/82） | 同上 |

### 首跑最小集（12 槽，跨全部家族，一天内可完成）

`{90, 91, 96, 99, 106, 121, 59, 52, 102, 114, 117, 66}`
覆盖：P 口中断、共享向量定时器、ADC2、PWM、TFT/LCM、DMA-UART、高号串口、比较器、CAN-DMA、QSPI。

## 4. 器材与接线总清单

1. STC32G144K246 实验箱 V1.4（或最小系统板；SCH_实验箱STC32G144K246-V1.4 随 demo 目录）。
2. 烧录：STC-ISP（IRC 24MHz 与 T10 口径一致），USB-TTL 接 P3.0/P3.1，115200/8N1，共地。
3. 杜邦跳线：UART5..8 / UART2..4 TX→RX 回环；SPI2/3 MOSI→MISO 回环；P 口触发脚互连。
4. 外设模块（仅相 C）：ILI9341 TFT、QSPI Flash、I2S codec、CAN 收发器+对端节点（或第二块板）。
5. 可选：逻辑分析仪/示波器（第 1 层佐证用，非判定必需；判 定 以 hit/哨兵/回环数据为准）。

## 5. 执行顺序与产出

1. 每相：构建 → check-crt-irq 20 项 + 扩展 check.py 全过 → 烧录 → 按 §2 判定 → 记录。
2. 记录模板（每槽一行 JSON，收进新的 board-results 文件，不改既有 board-results.json）：
   `{slot, vector_addr, source, trigger_ref, injections, hits, sentinel, crosstalk, default_hit, layer1_evidence, verdict, date, firmware_sha256}`。
3. 未完成相/槽显式列 `PENDING:equipment` 或 `PENDING:board`；禁止留空或记 PASS。
4. 全部完成后 owner review（设计 §4 G1-5 完成门槛），用户接受后方可更新发布结论。

## 6. 与 QEMU skip 的边界

- G1-0 首查未决：未取得 QEMU stc32g144k246 向量模型/中断控制器源码；**本计划全部动态项在
  QEMU 侧记 skip/NOT_RUN：模型能力未证**（G1-0 首查未决，即未运行——不得写成 T2-silent；
  T2-silent 保留给"已运行满窗口但无 UART 输出"的已运行语义），即使后续模型能注入 IRQ 线也只覆盖 CPU 入口层，不覆盖控制器
  路由与外设产生层，不能替代本计划。
- 静态三层（槽合法性、字节链、镜像）已由 lld 测试、compile-matrix、本目录 check.py 覆盖；
  真机四层分账中"模型执行"栏对高槽保持"未实现/不适用"，不因静态通过改记。
