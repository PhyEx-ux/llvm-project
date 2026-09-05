# keyboard-scan-step

- 来源：`14-IO行列扫描键盘数码管显示键值和调整时间/C语言/main.c:220-261`
- 原函数签名：`void IO_KeyScan(void)`
- 选案：行列键盘的连续采样、去抖、长按重复状态机。
- 改写：显式 `u8/u16/u32` typedef；`bit F0` 的临时标志改为 `u8 first_or_repeat`；GPIO/P3 行列驱动与 `IO_KeyDelay()` 移除，改由单参数 `raw_rows` 注入已经采样的 8 位矩阵状态；原全局键状态保留为内核状态。
- ISR/外设边界：该函数由主循环每 50 ms 调用，不带中断属性；只提取算法状态机，不猜 GPIO 电平行为。
- 可观察输出：`keyboard_scan_code()` 返回键码，`keyboard_scan_event()` 返回新事件标志。
- 编译自检：GCC `-c -Wall -Wextra -std=c89` 通过（无 warning）；SDCC `-mmcs251 --model-small -c` 通过。SDCC driver 输出两条环境 warning：`__has_builtin` 与 `__STDC_HOSTED__` redefined；无 kernel error/warning。
