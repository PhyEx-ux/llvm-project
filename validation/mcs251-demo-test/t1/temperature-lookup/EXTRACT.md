# temperature-lookup

- 来源：`16-NTC测温度数码管显示/C语言/main.c:45-50,328-370`
- 原函数签名：`u16 get_temperature(u16 adc)`
- 选案：数值查表、二分缩小区间和线性插值。
- 改写：`u8/u16/u32` 为显式宽度 typedef；`u16 code *p` 改为 `static const u16 temp_table[]`，即 `code`→`const`；完整保留原 161 项表；去掉 SFR/中断属性。原算法的 5 次区间缩小、边界哨兵 `0xfffe/0xffff` 和插值公式保留。
- 可观察输出：`temperature_lookup()` 返回放大 10 倍的温度或边界哨兵。
- 主循环/外设：只提取计算函数；ADC 采样入口不进入内核。
- 编译自检：GCC `-c -Wall -Wextra -std=c89` 通过（无 warning）；SDCC `-mmcs251 --model-small -c` 通过。SDCC driver 输出两条环境 warning：`__has_builtin` 与 `__STDC_HOSTED__` redefined；无 kernel error/warning。
