# G12K128 硬件中断帧探针 v2

目标：独立测量 Timer0 中断受理时的硬件压栈字节数与原始字节，不使用 LLVM 的 ISR 序言推断硬件行为。汇编完成测量，Clang/LLVM 编译的 C 完成 UART 初始化、交互和循环报告。

## 烧录与操作

烧录本目录 `hwframe-g12.hex`，STC-ISP IRC 设置 **24 MHz**。UART1 使用 **P3.0/RXD、P3.1/TXD，115200 / 8N1，无流控**。USB-TTL TX 接 P3.0，RX 接 P3.1，共地。镜像全部位于 FF 程序区，无 XINIT，不涉及旧版本说明中的 FE EEPROM/XINIT 冲突。

1. 下载完成后打开串口助手。程序持续发送 `HWF2 READY 24MHz 115200; send g`，无需抢上电时机。
2. 以 ASCII 发送小写 `g`（十六进制发送则为 `67`），触发一次测量。
3. 正常情况下先看到 `HWF2 RUN`，随后 `I RETURN` 和重复的测量报告。测量本身只做一次，后续输出读取同一份快照。复位可重新测量。

输出示例（**QEMU 模型结果，不是真机期望值或硬件资格结论**）：

```text
HWF2 STATUS=CAPTURED B=01FE I=0202 R=01FE N=0004 PSW1=02/00/00 BE_RAW=00/00/00
W@01F0=A5 A5 A5 A5 A5 A5 A5 A5 A5 A5 A5 A5 A5 A5 A5 00 FF 78 02 A5 A5 A5 A5 A5 A5 A5 A5 A5 A5 A5 A5 A5
```

- `B/I/R`：中断前、ISR 入口、返回后的 `SPH(85):SP(81)` 原始读数。
- `N`：完整 16 位 `I-B`，包含跨页进位。初始 SP=01FE，4 字节硬件帧会使 SP=0202，2 字节会使 SP=0200。不能只减 SP 低字节。
- `PSW1`：三个采样点的读数。B 是启动定时器前的基线；等待循环会改变标志，B 不等于中断受理瞬间的 PSW1。R 在等待循环检查完成后读取，也不能直接用 B/R 比较证明 PSW1 恢复正确。
- `BE_RAW`：SFR BE 的三个原始读数。项目 STC32G 头文件将 BE 定义为 ADC_RESL；**不把它当成第二个 SPH，也不据此访问 RAM**。
- `W@01F0`：按地址升序排列的 01F0..020F 原始快照，共 32 字节。测量前填充 A5，初始栈顶 01FE，因此硬件压栈从窗口偏移 15（01FF）开始。A5 可能也是有效帧字节，不能只靠哨兵变化数推断帧长。
- `CAPTURED` 仅表示 ISR 已采样且返回 SP 等于基线，不预设 N=4、不自动认定 INTR 位或字节顺序。
- `TIMEOUT` 表示等待预算内没有完成 ISR，N 显示 `----`，窗口显示 `UNAVAILABLE`；未采样字段清零。超时按指令循环计数，不保证精确毫秒值。
- `SP_MISMATCH` 表示执行到返回侧，但 SP 未恢复。汇编仍使用保存的 C 栈地址恢复报告环境。

定位无输出：

| 最后可见内容 | 含义 |
|---|---|
| 没有 READY | 尚不能归因于中断；检查烧录、24MHz、接线、串口参数和 C 启动/输出路径 |
| READY 持续，发送 g 无 RUN | 检查 RX 接线和发送格式；g 必须是字节 67 |
| RUN 后没有 I 或 RETURN | 测量或中断入口路径异常；无 IRQ 的普通情况会返回 TIMEOUT |
| I 后没有 RETURN | ISR 已完成快照并写出标记，问题集中在 RETI/返回衔接；不能只凭标记断定具体原因 |
| RETURN 后报告重复 | C 调用环境已恢复，快照不会被后续打印覆盖 |

## 测量边界

`hwframe.asm` 的 `_hwf_measure` 是普通 ECALL/ERET 汇编函数，保存其会修改的寄存器和原 C 栈指针后，切换到测量栈。整个测量期间没有 C 执行。Timer0 模式 1，IE 只开放 EA/ET0；ISR 是独立 EJMP 入口，不经过编译器 ISR 序言。

ISR 首先记录 PSW1、SP/SPH、BE，再停止 Timer0，使用寄存器间接寻址复制固定窗口。入口至 RETI **无 PUSH、CALL、ECALL**。复制完才写 SBUF 标记 I；C 在输出 RETURN 前等待该字节发送完成。中断会修改等待循环的 A/标志，但循环每次重新读取完成标记，且使用的 R3/R4 不被 ISR 修改。

返回侧立即保存读数，关闭 Timer0/中断，恢复原 C 栈和寄存器，再 ERET。此专用程序独占 Timer0 和 IE，不是可直接集成到任意应用的保留外设状态库。没有对 RETI 跑飞进行硬件看门狗恢复；若 PC 根本未返回，保留 I 标记用于定位。

内存布局：

| 地址 | 用途 |
|---|---|
| 0040..004F | 采样值、完成标记，测量前清零 |
| 0050..0051 | 保存的 C 栈指针 |
| 01F0..020F | 填充 A5 的测量栈窗口，初始 SPX=01FE |
| 0400..041F | ISR 复制的不可变窗口 |
| 0801..0FFF | C 栈可用范围，启动 SPX=0800 |
| FF0000 / FF000B | 复位 / Timer0 向量 |
| FF0200 起 | 汇编、C 代码与常量 |

C 没有可变全局对象，不需要 CRT 数据初始化。固定地址访问采用项目兼容内存契约 `1,1,32,8,1`。通过 map 查看 `_hwf_wait` 与 `_hwf_wait_end`，硬件返回 PC 通常应落在这段等待代码内；字节顺序必须以真机快照和芯片依据解释，不能使用 QEMU 的帧顺序做循环论证。

## 构建与验证

```sh
bash build.sh /tmp/hwf-hardware hardware
bash build.sh /tmp/hwf-simulation qemu
python3 check.py /tmp/hwf-hardware /tmp/hwf-simulation
```

输出目录必须为空，脚本不再递归删除用户指定目录。不传目录则自动建立唯一临时目录。可通过 CLANG、LLC、SDAS251、SDLD 覆盖工具路径。构建保留 LLVM IR、C 汇编输出、汇编列表、对象、map 和 HEX。

`hardware` 使用真实 TI 轮询；`qemu` 仅为 UART test-port 跳过 TI 等待和报告延迟，**不能烧到实板**。正确模拟器加载参数为 `-bios`：

```sh
/home/liu/build-qemu/qemu-system-mcs251 -M stc32g144k246 \
  -bios /tmp/hwf-simulation/hwframe.hex -nographic -monitor none -serial stdio
```

`check.py` 校验两个版本的 HEX 校验和、地址和向量，并对模拟器版本实际经串口发送 g，验证正常、禁用中断超时、跳过 RETI 的 SP_MISMATCH 三种路径；每种检查至少三份重复报告与窗口一致。故障注入只修改临时镜像。QEMU 的四字节帧是模型假设，不构成真机证明。

发布产物的 SHA256 见 `SHA256SUMS`。

## 2026-09-10 真机结果

用户已反馈 G12K128 实板测量成功：`B=01FE I=0202 R=01FE N=0004`，原始帧为 `00 FF 74 02`，返回 PC 解读为 `FF0274`，与 `_hwf_wait` 地址吻合。当前配置下 Timer0 非嵌套中断的 **4 字节硬件帧、返回 SP 恢复以及 SFR 85 的 SPH 跨页读数**已有实板依据。重复输出是一次采样的重发，不是多次独立测量。

完整用户采样、镜像 SHA256、帧地址解释和结论边界见 [真机记录](REALHW-20260910.md)。本次未直接读取 INTR 位，也未完成 PSW1 非零标志恢复或编译器完整 ISR 保存恢复资格验证。
