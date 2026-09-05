# decimal-length-parse

- 来源：`18-通过USB-CDC发送命令读写EEPROM测试程序/EEPROM.c:289-302`
- 原函数签名：`u8 GetDataLength(void)`；被调函数 `u8 CheckData(u8 dat)` 位于同文件 `:255`。
- 选案：指针遍历、字符转数字和十进制累积。
- 改写：显式 `u8/u16/u32` typedef；去掉全局 `UsbOutBuffer`，把 `const u8 *input` 与 `input_count` 打包为一个 `decimal_length_args *`；`CheckData` 内联为无 SFR 的 `hex_nibble`；保留起始下标 11、遇到非十进制字符停止和 8 位长度累积语义。
- 参数打包：原函数是无参但隐式读取全局输入；显式输入指针/长度是测试 harness 注入，不是原生多参 ABI。
- 可观察输出：返回 `u8` 长度。
- 编译自检：GCC `-c -Wall -Wextra -std=c89` 通过（无 warning）；SDCC `-mmcs251 --model-small -c` 通过。SDCC driver 输出两条环境 warning：`__has_builtin` 与 `__STDC_HOSTED__` redefined；无 kernel error/warning。
