# hex-address-parse

- 来源：`18-通过USB-CDC发送命令读写EEPROM测试程序/EEPROM.c:255-283`
- 原函数签名：`u32 GetAddress(void)`；被调函数 `u8 CheckData(u8 dat)` 位于同文件 `:255`。
- 选案：移位解码、十六进制字符分类和 24 位地址结果。
- 改写：显式 `u8/u16/u32` typedef；去掉 USB/SFR 依赖；将原全局 `UsbOutBuffer` 改为驱动定义的 `hex_input[16]` 全局，内核用数组下标直接访问；保留 `0X` 前缀、6 个十六进制字符和错误哨兵语义。原函数的 `CheckData` 作为私有 helper `hex_nibble` 保留。
- 参数形态：原函数和提取函数均无参数；输入仍来自驱动拥有的全局缓冲，不引入参数打包。
- 可观察输出：返回解析的 `u32` 地址或 `0xffffffff`。
- wrapper 的结果临时值也使用同一条件 32 位类型；不能在 SDCC 路径使用 16 位 `unsigned int`，否则 `0x001234AB` 会在检查前被截断为 `0x000034AB`。

## u32 三端展开与宏机制

| 链路 | `SDCC_FW` | `u32` 实际展开 | 宽度 | `parse_hex_address` 签名/IR |
| --- | --- | --- | --- | --- |
| Oracle-A host（GCC；host Clang 同型） | 未定义 | `unsigned int` | 32 bit | `unsigned int parse_hex_address(void)` |
| Oracle-B（SDCC mcs251） | 由 runner 的 cpp 命令定义 | `unsigned long` | 32 bit | `unsigned long parse_hex_address(void)`；返回占 4 字节 ABI 值 |
| DUT Clang→LLVM IR→llc | 未定义 | `unsigned int` | 32 bit | raw IR 为 `define i32 @parse_hex_address()`；MCS251 llc 输出 `_parse_hex_address` |

runner 仅在 SDCC 预处理路径传入 `-DSDCC_FW`。这使 SDCC 选用 32 位 long，而 Oracle-A 和 host-target Clang 选用 32 位 int；host 可达路径没有裸 `unsigned long`，所以不会生成不受后端支持的 i64 返回。

## 期望与验证记录

- 日期：2026-09-05。
- Oracle-A 真值 serial：`Ba001234ABb00000000c00FFFFFFdFFFFFFFFeFFFFFFFFPASS\n`。
- serial md5：`fbeaf61842759eb6e9306c9a97517e78`。
- 指定 llc（md5 `f6c2f4034f9e32ee61ecfb690b238c93`）、`--no-ir-shims`：Oracle-A、Oracle-B、DUT 三方 serial 逐字一致并 PASS。
