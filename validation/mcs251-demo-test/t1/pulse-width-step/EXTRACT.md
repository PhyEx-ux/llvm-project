# pulse-width-step

- 来源：`05-利用定时器1测量INT1引脚低电平脉冲宽度/C语言/main.c:206-220`
- 原函数签名：`void timer1_int (void) interrupt 3`
- 选案：定时器 1 ISR 中的脉宽测量状态机。
- 改写：显式保留 `u8/u16/u32` typedef；去掉 `interrupt 3`；将原来的 `P33` 输入改为单参数 `pin_high`；`Temp_cnt`/`Test_cnt`/完成标志收拢为文件内状态。每次 `pulse_width_step` 代表一次 ISR tick。
- ISR 状态机去中断提取：保留低电平累计、上升沿锁存、`>10` 有效门限和计数清零语义；中断入口/显示调用不进入内核。
- 可观察输出：`pulse_width_result()` 返回锁存脉宽，`pulse_width_ready()` 返回完成标志。
- 编译自检：GCC `-c -Wall -Wextra -std=c89` 通过（无 warning）；SDCC `-mmcs251 --model-small -c` 通过。SDCC driver 输出两条环境 warning：`__has_builtin` 与 `__STDC_HOSTED__` redefined；无 kernel error/warning。
- 批量合规补记：删除对 SDCC 宏无差别的冗余条件编译，保留唯一显式宽度 u32 typedef，语义不变。

## 期望重导记录

- 日期：2026-09-05
- Oracle-A 真值 serial：`Ba0000b00c0000d00e000Bf01g000BPASS\n`
- 期望比对：各 checkpoint 均与宿主真值一致；替换值：无。
