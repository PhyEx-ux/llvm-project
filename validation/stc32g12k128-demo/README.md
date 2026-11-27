# STC32G12K128 现代 LLVM/Clang MCS-251 固件 Demo

本示例是一个面向 **STC32G12K128**（24 MHz HIRC）的完整可烧录裸机固件工程，展示当前 MCS-251 工具链的全部现代特性，且**零外部运行时库依赖**、**零手动 `-I` 头路径**：

- **内置 `<stc32.h>` 与 `<stc32_gpio.h>` 设备头**：AS6 直寻址 SFR（`mov 0xNN, rM`）、24 位 XFR（`EAXSFR()`/`EAXRAM()`）、以单引脚宏统一推导端口 SFR / 字节掩码 / 硬件位左值（`BOARD_LED = STC32_P45` → `STC32_GPIO_SET_MODE` + `STC32_GPIO_TOGGLE` 降为单条 `cpl 0xc5`）、以及具名控制位掩码（`AUXR_T0x12`、`AUXR_S1BRT` 等）；
- **GNU 链接脚本（`-T stc32g12k128.ld`）**：定义 `FLASH`/`IRAM`/`EDATA`/`XDATA` 内存窗口、`PROVIDE(_fw_version)` 符号、`KEEP(*(.text._fw_info_keep))` 与 `--gc-sections` 死代码裁剪；
- **硬件中断优先级自动配置 + WP2 抢占图自动推导**：
  - `timer0_isr`（`TMR0_VECTOR=1`，**优先级 2**，1 ms 硬件节拍）；
  - `uart1_isr`（`UART1_VECTOR=4`，**优先级 1**，RX 环形缓冲）；
  - `mcs251-lld` 自动推导 `UART1 -> TMR0` 嵌套抢占关系、运行 WP2 重入审计，并自动合成 `___mcs251_isr_priority_init` 配置 `IP`/`IPH` SFR；
- **Route B 可重入栈 ABI**：`board_mix_crc16()` 为 3 参数函数，被 `main()` 与 `timer0_isr()` 并发共享零冲突；包含结构体按值传递/返回（`SelfCheckResult`）与标准 `<stdarg.h>` 精简 `uart1_printf`；
- **C11 `<stdatomic.h>`**：`_Atomic uint32_t g_ms_ticks`、`atomic_fetch_add` / `atomic_compare_exchange_strong` 通过原子 `EA` 临界区（`jbc 0xaf` / `setb 0xaf`）展开；
- **G11 固定地址放置 + 零信任校验**：`g_telemetry` 固定置于 XDATA `0x010100`（`STC32_PLACE_AT(0x010100) STC32_NOINIT STC32_RETAIN`），链接期自动由独立进程 `mcs251-placement-verify` 完成 V1–V18 校验；
- **完整调试与工程产物**：`-g` DWARF（通过 `llvm-dwarfdump --verify`）、文本 `--map`、结构化 `--map-json`、`--link-facts`、反汇编清单 `.lst` 与可直接烧录的 Intel HEX（`.hex`）。

---

## 目录结构

| 文件 | 说明 |
|---|---|
| `stc32g12k128.ld` | STC32G12K128 GNU 风格链接脚本（128K Flash / 128B IRAM / 4K EDATA / 8K XDATA，layout-v2 全 ROM 落在 `0xFF0000..0xFFFFFF`） |
| `src/board.h` | 板级时钟/串口/引脚参数与共享数据结构声明 |
| `src/uart.h` / `src/uart.c` | UART1 驱动（P3.0/P3.1，Timer2 1T 115200 8N1，优先级 1 RX 中断 + XDATA 环形缓冲 + 独立 `<stdarg.h>` `uart1_printf`） |
| `src/timer.h` / `src/timer.c` | Timer0 1 ms 节拍驱动（1T 自动重载，优先级 2 中断 + C11 `_Atomic` 毫秒计数 + 500 ms 心跳） |
| `src/main.c` | 主程序：硬件初始化、开机自检（ABI/原子/固定放置/链接脚本符号）、500 ms LED 闪烁与串口交互命令 |
| `build.ps1` / `Makefile` | Windows PowerShell 与 Linux/WSL 一键构建与自动校验脚本 |

---

## 快速构建

### Windows（PowerShell）

```powershell
cd c:\Prj\XL\llvm-project\validation\stc32g12k128-demo
.\build.ps1            # 默认真机固件（含 --gc-sections、-g、--verify-placement）
.\build.ps1 -Lto       # 可选：启用 Full LTO (-flto) 构建
.\build.ps1 -QemuMode  # 可选：QEMU test-port 模式（不轮询 TI）
```

### Linux / WSL

```bash
cd validation/stc32g12k128-demo
make TOOLCHAIN_BIN=/path/to/build-mcs251/bin
```

构建产物位于 `build/`：
- `build/stc32g12k128-demo.hex`：**烧录用 Intel HEX**
- `build/stc32g12k128-demo.elf`：含符号与 DWARF 调试信息的 `ET_EXEC`（`e_flags=0x102`）
- `build/stc32g12k128-demo.map` / `.map.json` / `.placement` / `.facts` / `.lst`

---

## STC32G12K128 真机接线与 AiCube-ISP 烧录

1. **接线**（USB-TTL 电平与板卡一致，共地，切勿将 P3.0/P3.1/P3.2 同时拉低）：
   - `USB-TTL TXD` → 芯片 `P3.0 (RxD)`
   - `USB-TTL RXD` → 芯片 `P3.1 (TxD)`
   - `LED` → 芯片 `P4.5`（默认推挽输出，500 ms 翻转）
2. **AiCube-ISP 设置**：
   - 芯片型号：`STC32G12K128`
   - 打开文件：`build/stc32g12k128-demo.hex`（Intel HEX）
   - 用户 IRC 频率：**`24.000 MHz`**
   - EEPROM 大小：**`<= 1 KB`**（推荐 `0`，本固件全部 ROM 位于 `0xFF0000..0xFFFFFF`，不会与 `0xFE0000` 处 EEPROM 冲突）
3. **串口终端（115200 8N1）**：
   上电后串口打印开机自检 `[CHECK] passed=4/4` 与每 500 ms 心跳 `[HB] tick=...ms`，支持输入单字符命令：
   - `h` / `?`：查看命令帮助
   - `t`：打印当前毫秒计数、LED 状态、固定 XDATA 遥测（`boot_count`/`rx`/`tx`）与 ISR CRC
   - `l`：立即翻转 `P4.5` LED
   - `s`：重跑片上自检
   - `r`：开启 `EAXSFR()` 读取并打印时钟 XFR（`CLKSEL`/`CLKDIV`/`HIRCCR`）
