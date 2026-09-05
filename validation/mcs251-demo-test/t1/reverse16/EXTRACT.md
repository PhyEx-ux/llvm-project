# reverse16

- 来源：`44-CANFD1-CANFD2同时使用收发测试/canfd.c:49-57`
- 原函数签名：`uint16_t reverse2(uint16_t w)`
- 选案：纯数值/移位与端序转换；不依赖 CAN 外设。
- 改写：`uint16_t` 归一为显式 `u16`，补充 `u8/u32` typedef 以统一 T1 类型契约；保留 16 位高低字节交换算法；无 SFR、`bit`、`code` 或中断属性。
- 可观察输出：返回交换后的 `u16`。
- 编译自检：GCC `-c -Wall -Wextra -std=c89` 通过（无 warning）；SDCC `-mmcs251 --model-small -c` 通过。SDCC driver 输出两条环境 warning：`__has_builtin` 与 `__STDC_HOSTED__` redefined；无 kernel error/warning。
