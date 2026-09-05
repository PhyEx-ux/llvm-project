# task-marks

- 来源：`32-通过定时器周期性调度任务综合例程，简单实用的任务调度系统，推荐/Sources/src/Task.c:12-48`
- 原函数签名：`void Task_Marks_Handler_Callback(void)`
- 选案：周期任务标记核心；覆盖数组遍历、计数递减、周期重装和 run 标记。
- 改写：显式 `u8/u16/u32` typedef；将原 `TASK_COMPONENTS` 中本算法需要的字段压缩为 `task_mark`；改为驱动定义的 `mark_tasks[8]`/`mark_task_count` 全局，内核用数组下标直接遍历，以便保持单参数 ABI；没有 SFR/bit/中断属性。
- 主循环/调度体提取：只提取标记回调的算法体，不提取定时器 ISR、任务初始化或应用任务。
- 可观察输出：返回本次被置 `run=1` 的任务数；输入数组的 `time_count/run` 也会被原地更新。
- 编译自检：GCC `-c -Wall -Wextra -std=c89` 通过（无 warning）；SDCC `-mmcs251 --model-small -c` 通过。SDCC driver 输出两条环境 warning：`__has_builtin` 与 `__STDC_HOSTED__` redefined；无 kernel error/warning。
- 批量合规补记：删除对 SDCC 宏无差别的冗余条件编译，保留唯一显式宽度 u32 typedef，语义不变。
- 自比较改字面常量：wrapper 不再将 checkpoint 的 C8/C16 宏表达式同时作为 expected 和 got；期望改为 host gcc 逐 checkpoint 回填的字面常量 `01, 0005, 01, 00, 0001, 00, 01, 0002`，got 仍独立读取函数返回值或更新后的任务字段。
- 后端等价改写：任务表在 SDCC 侧显式位于 `__data`，内核以 `task_mark *` 顺序递增遍历，避免变量下标触发后端未实现的 i32 结构体地址乘法/通用指针 helper；遍历顺序、字段更新和计数语义与原数组循环等价，host/DUT 侧仍使用同一结构布局。
