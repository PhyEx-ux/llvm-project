# decimal-length-parse

- 来源：`18-通过USB-CDC发送命令读写EEPROM测试程序/EEPROM.c:289-302`
- 原函数签名：`u8 GetDataLength(void)`；被调函数 `u8 CheckData(u8 dat)` 位于同文件 `:255`。
- 选案：指针遍历、字符转数字和十进制累积。
- 改写：显式 `u8/u16/u32` typedef；去掉全局 `UsbOutBuffer`，改为驱动定义的 `decimal_input[64]`/`decimal_input_count` 全局，内核用数组下标直接遍历；`CheckData` 内联为无 SFR 的 `hex_nibble`；保留起始下标 11、遇到非十进制字符停止和 8 位长度累积语义。
- 参数打包：原函数是无参但隐式读取全局输入；显式输入指针/长度是测试 harness 注入，不是原生多参 ABI。
- 可观察输出：返回 `u8` 长度。
- 编译自检：GCC `-c -Wall -Wextra -std=c89` 通过（无 warning）；SDCC `-mmcs251 --model-small -c` 通过。SDCC driver 输出两条环境 warning：`__has_builtin` 与 `__STDC_HOSTED__` redefined；无 kernel error/warning。
- 批量合规补记：删除对 SDCC 宏无差别的冗余条件编译，保留唯一显式宽度 u32 typedef，语义不变。
- 后端等价改写：将 `length * 10u` 改写为 `(length << 3) + (length << 1)`，即 `8x + 2x = 10x`；外层 `u8` 转换仍保留每次累积后的 8 位截断，因此 C90 语义不变，同时避开 MCS-251 后端未实现的 i32 `mul` 选择。host gcc 与 SDCC/DUT 三方输出均确认该改写保持 checkpoint 语义。
- Oracle-A 期望修正：host gcc 真值为 `Ba00b39c0Cd00eFFPASS`；因此 `b=0x39`（原 Oracle-B 的 `0x7B` 错期望）、`c=0x0C`、`e=0xFF`，其余为 `0x00`。
