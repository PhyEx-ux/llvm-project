# reverse32

- 来源：`74-MSC(Mass Storage Class)协议范例/src/util.c:8-18`
- 原函数签名：`DWORD reverse4(DWORD d)`
- 选案：纯 u32 掩码、移位和四字节端序转换；不依赖 USB 外设。
- 改写：`DWORD` 归一为显式 `u32`，补齐 `u8/u16` typedef；保留四个字节的掩码/移位顺序；无 SFR、`bit`、`code` 或中断属性。
- 可观察输出：返回交换后的 `u32`。
- 编译自检：GCC `-c -Wall -Wextra -std=c89` 通过（无 warning）；SDCC `-mmcs251 --model-small -c` 通过。SDCC driver 输出两条环境 warning：`__has_builtin` 与 `__STDC_HOSTED__` redefined；无 kernel error/warning。
