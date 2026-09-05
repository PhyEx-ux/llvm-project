# task-dispatch

- 来源：`32-通过定时器周期性调度任务综合例程，简单实用的任务调度系统，推荐/Sources/src/Task.c:50-68`
- 原函数签名：`void Task_Pro_Handler_Callback(void)`。
- 选案：任务 run 标志消费和函数指针回调，覆盖函数指针间接调用。
- 主循环/调度体提取：去掉应用任务表和定时器上下文，只保留任务处理回调的循环体；没有中断属性。
- 可观察输出：返回实际调用的 hook 数量，并消费 run 标志。

## Phase 11 指针对象尺寸问题与当前改写

Phase 11 的原始紧凑形态把 `run` 和 `task_hook` 放在同一个 struct 中。三端对含函数指针 struct 的实测 stride 不一致：host 为 16 字节、SDCC 为 5 字节、LLVM MCS251 侧为 4 字节。也就是说，同一个 `tasks[i]` 在三条链上会落到不同地址；这是“指针对象尺寸问题”在 struct 中的直接体现，不能把某一端创建的函数指针对象交给另一端按自己的布局读取。

当前改写把两类状态分离：

- `dispatch_task_storage[40]` 保存目标侧 8 条、每条 5 字节的任务记录槽；run 字节显式位于 `i * 5`，以 `(i << 2) + i` 计算，不依赖任何编译器的 struct stride。
- `dispatch_task_hook_ids[8]` 只跨边界保存 1 字节 hook ID。函数地址由 `task_dispatch_resolve()` 在各编译器自己的 kernel 对象内解析为局部 `task_hook`，随后仍通过 `hook()` 执行真实函数指针间接调用。
- `dispatch_task_count` 仍由驱动定义，kernel 只声明 extern 并按升序遍历。

这样既保留原算法的 run 消费、空 hook 跳过和间接回调语义，又不再共享表示宽度不一致的函数指针对象。此前把 run storage 与函数指针数组拆开仍不充分：SDCC wrapper 的函数指针数组元素是 3 字节，而 DUT 从 host-target IR 形成的 pointer GEP/load 按 4 字节对象处理，首项便会读成错误跳转地址；改为 u8 ID 后该 ABI 混用被消除。

## u32 三端展开与宏机制

本案算法本身只使用 u8 和函数指针，但为遵守共享 kernel 的统一类型规则仍声明 u32：

| 链路 | `SDCC_FW` | `u32` 实际展开 | 宽度 | 本案公开函数签名/IR |
| --- | --- | --- | --- | --- |
| Oracle-A host（GCC；host Clang 同型） | 未定义 | `unsigned int` | 32 bit | `unsigned char task_dispatch_step(void)` |
| Oracle-B（SDCC mcs251） | 由 runner 的 cpp 命令定义 | `unsigned long` | 32 bit | `unsigned char task_dispatch_step(void)`；本编译器内部 `task_hook` 被解析并间接调用 |
| DUT Clang→LLVM IR→llc | 未定义 | `unsigned int` | 32 bit | `define zeroext i8 @task_dispatch_step()`；hook ID 是 `[8 x i8]`，resolver 返回 `ptr` |

runner 只对 SDCC 预处理路径定义 `SDCC_FW`，host 与 DUT 不定义。因此即使本案将来使用 u32，也不会让 SDCC 的 16 位 int 或 LP64 host 的 64 位 long 进入共享 ABI。

## 期望与验证记录

- 日期：2026-09-05。
- Oracle-A 真值 serial：`Ba02b01c02d00e00f00g01h04PASS\n`。
- serial md5：`55df3a031c95e0dc83c7708243f7e652`。
- 指定 llc（md5 `f6c2f4034f9e32ee61ecfb690b238c93`）、`--no-ir-shims`：Oracle-A、Oracle-B、DUT 三方 serial 逐字一致并 PASS。
