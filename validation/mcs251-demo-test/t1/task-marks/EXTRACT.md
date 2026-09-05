# task-marks

- 来源：`32-通过定时器周期性调度任务综合例程，简单实用的任务调度系统，推荐/Sources/src/Task.c:12-48`
- 原函数签名：`void Task_Marks_Handler_Callback(void)`
- 选案：周期任务标记核心；覆盖数组遍历、计数递减、周期重装和 run 标记。
- 改写：显式 `u8/u16/u32` typedef；将原 `TASK_COMPONENTS` 中本算法需要的字段压缩为 `task_mark`；原静态任务表和 `Tasks_Max` 移出，改为单指针加数量参数，以便注入最小观察输入；没有 SFR/bit/中断属性。
- 主循环/调度体提取：只提取标记回调的算法体，不提取定时器 ISR、任务初始化或应用任务。
- 可观察输出：返回本次被置 `run=1` 的任务数；输入数组的 `time_count/run` 也会被原地更新。
- 编译自检：GCC `-c -Wall -Wextra -std=c89` 通过（无 warning）；SDCC `-mmcs251 --model-small -c` 通过。SDCC driver 输出两条环境 warning：`__has_builtin` 与 `__STDC_HOSTED__` redefined；无 kernel error/warning。
