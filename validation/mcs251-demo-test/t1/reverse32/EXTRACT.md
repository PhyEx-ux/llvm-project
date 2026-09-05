# reverse32

- 来源：`74-MSC(Mass Storage Class)协议范例/src/util.c:8-18`
- 原函数签名：`DWORD reverse4(DWORD d)`。
- 选案：纯 u32 四字节端序转换；不依赖 USB 外设。
- 改写：`DWORD` 归一为条件编译的 `u32`，补齐 `u8/u16` typedef；原 32 位掩码/移位链改为通过 `u8 *` 读取四个字节并原位交换。两种写法都把输入的第 0/1/2/3 字节映射到第 3/2/1/0 字节，数值语义相同；新形态不需要 IR shift shim。
- 可观察输出：返回交换后的 `u32`。
- 无 SFR、`bit`、`code` 或中断属性。

## u32 三端展开与宏机制

| 链路 | `SDCC_FW` | `u32` 实际展开 | 宽度 | `reverse32` 签名/IR |
| --- | --- | --- | --- | --- |
| Oracle-A host（GCC；host Clang 同型） | 未定义 | `unsigned int` | 32 bit | `unsigned int reverse32(unsigned int)` |
| Oracle-B（SDCC mcs251） | 由 runner 的 cpp 命令定义 | `unsigned long` | 32 bit | `unsigned long reverse32(unsigned long)`；汇编入口 `_reverse32`，参数/返回均占 4 字节 ABI 值 |
| DUT Clang→LLVM IR→llc | 未定义 | `unsigned int` | 32 bit | raw IR 为 `define i32 @reverse32(i32)`；MCS251 llc 输出 `_reverse32` |

runner 只在 `build_sdcc_rel()` 的预处理命令中传入 `-DSDCC_FW`；Oracle-A 与 DUT 的 host-target Clang 均不定义它。因此 SDCC 选择 `long=32`，host/Clang 选择 `int=32`，同时避免 LP64 host 的裸 `unsigned long` 生成 i64 函数签名。

## 从 32 位移位链改为 u8 字节交换的动机

最初在 LP64 host 分支把 `u32` 写成裸 `unsigned long` 时，Clang 生成 i64 参数/返回，llc 原始报错为：

```text
LLVM ERROR: minimal MCS251 backend only supports zero or one i8/i16/i32 return value; multi-value returns are not supported
```

把 u32 收窄到真实 i32 后，旧 frozen llc（md5 `67a170573cba625fe24441d0b6825d10`）对原 i32 移位链的原始报错为：

```text
LLVM ERROR: Cannot select: 0x62d4014560d0: i32 = srl 0x62d40145a760, Constant:i32<8>
```

当前指定 llc（md5 `f6c2f4034f9e32ee61ecfb690b238c93`）已能选择精确 i32 shift probe；仍保留 u8 字节交换写法，是为了同时消除 `UL` 常量在 LP64 下把表达式提升为 i64的问题，并让本案无需常量移位 shim 即可覆盖同一端序转换语义。

## 期望与验证记录

- 日期：2026-09-05。
- Oracle-A 真值 serial：`Ba00000000b78563412cDDCCBBAAdFF000000PASS\n`。
- serial md5：`465429d6d933b8f83f390285013ad99d`。
- 指定 llc、`--no-ir-shims`：Oracle-A、Oracle-B、DUT 三方 serial 逐字一致并 PASS。
