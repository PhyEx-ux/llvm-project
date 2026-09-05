# rtc-step

- 来源：`14-IO行列扫描键盘数码管显示键值和调整时间/C语言/main.c:91-103,143-160`
- 原函数签名：`void RTC(void)`
- 选案：时分秒进位状态机；独立于 RTC 外设寄存器，适合作为主循环算法回归。
- 改写：显式 `u8/u16/u32` typedef；原全局 `hour/minute/second` 收拢为 `rtc_state *` 单参数；`msecond` 字段保留为主循环状态但不改变 `RTC` 进位语义；去 SFR 和中断属性。
- 主循环体提取：只取主循环调用的 `RTC()` 计算体，不取定时器 ISR、显示和键盘输入。
- 可观察输出：`rtc_hour/minute/second()` 返回更新后的时分秒。
- 编译自检：GCC `-c -Wall -Wextra -std=c89` 通过（无 warning）；SDCC `-mmcs251 --model-small -c` 通过。SDCC driver 输出两条环境 warning：`__has_builtin` 与 `__STDC_HOSTED__` redefined；无 kernel error/warning。
