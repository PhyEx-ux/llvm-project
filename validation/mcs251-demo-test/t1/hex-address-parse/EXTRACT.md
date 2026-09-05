# hex-address-parse

- 来源：`18-通过USB-CDC发送命令读写EEPROM测试程序/EEPROM.c:255-283`
- 原函数签名：`u32 GetAddress(void)`；被调函数 `u8 CheckData(u8 dat)` 位于同文件 `:255`。
- 选案：移位解码、十六进制字符分类和 24 位地址结果。
- 改写：显式 `u8/u16/u32` typedef；去掉 USB/SFR 依赖；将原全局 `UsbOutBuffer` 改为 `const u8 *input` 单参数；保留 `0X` 前缀、6 个十六进制字符和错误哨兵语义。原函数的 `CheckData` 作为 `hex_nibble` 内联为私有 helper。
- 参数打包：原函数本身无参数；全局输入显式收敛为单指针参数，不改变算法。
- 可观察输出：返回解析的 `u32` 地址或 `0xffffffff`。
- 编译自检：GCC `-c -Wall -Wextra -std=c89` 通过（无 warning）；SDCC `-mmcs251 --model-small -c` 通过。SDCC driver 输出两条环境 warning：`__has_builtin` 与 `__STDC_HOSTED__` redefined；无 kernel error/warning。
