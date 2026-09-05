# task-dispatch

- 来源：`32-通过定时器周期性调度任务综合例程，简单实用的任务调度系统，推荐/Sources/src/Task.c:50-68`
- 原函数签名：`void Task_Pro_Handler_Callback(void)`
- 选案：任务 run 标志消费和函数指针回调，覆盖函数指针间接调用。
- 改写：显式 `u8/u16/u32` typedef；保留 `void (*TaskHook)(void)` 的函数指针形状；原静态任务数组改为单参数 `task_item *` 加数量，字段只保留 `Run/TaskHook`；回调调用次数作为返回值。
- 主循环/调度体提取：去掉应用任务表和定时器上下文，只保留任务处理回调的循环体；没有中断属性。
- 可观察输出：返回实际调用的 hook 数量，且消费 `run` 标志。
- 编译自检：GCC `-c -Wall -Wextra -std=c89` 通过（无 warning）；SDCC `-mmcs251 --model-small -c` 通过。SDCC driver 输出两条环境 warning：`__has_builtin` 与 `__STDC_HOSTED__` redefined；无 kernel error/warning。
