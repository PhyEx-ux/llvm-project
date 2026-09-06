# mcs251-demo-modern — 现代 C 特性演示工程

本工程演示 **Clang + LLVM 直接把现代 C 编译成 STC32（MCS-251）固件** 的完整链路，
并逐项展示后端已支持的 C99/C11 语言特性。全部注释为中文，构建由 Makefile 管理。

## 工具链（生产链，零 SDCC 生成代码）

```
fork-clang (--target=mcs251-unknown-none, -std=c11)
    │  生成目标 IR（.ll）
    ▼
llc (MCS251 后端, -filetype=obj)
    │  生成 ASxxxx .rel 对象
    ▼
mcs251_ld.py (自研链接器, 严格 ABI 模式)
    │  布局 CSEG/DSEG/XINIT/VECS/HOME, 产出 Intel-HEX
    ▼
qemu-system-mcs251（机器名来自 boards/*.mk）
       串口 transcript 即验收输出
```

唯一借用的 SDCC 工具是 `sdas251`，仅用于汇编**手写**启动模块 `crt-selfstart.asm`
（复位向量 + 向量表 + 稀疏 XINIT 全局初始化），不参与任何 C 代码编译。

## 展示的特性清单

| 特性 | 所属函数 | 依赖的后端能力 |
|---|---|---|
| C99 块内/for 内声明、`//` 注释 | `feat_c99_decl` | 基础 DAG |
| stdint 精确宽度整数 (u8/u16/u32) | `feat_stdint_math` | int=32 数据模型 |
| u32 乘法 / u16 无符号除法 | `feat_stdint_math` | 原生 MUL + udiv libcall |
| 变量移位 + 循环移位 (rotate) | `feat_shift_var` | 单位移位循环 lowering |
| `bool` 与短路求值 | `feat_bool_logic` | i1/i8 逻辑 |
| C99 指定初始化器（结构体/数组） | `feat_designated_init` | rodata CSEG 常量 |
| const 查表 + 字符串字面量 | `feat_const_table` | 只读 CSEG 数据发射 |
| 可变全局（非零初值 + BSS 清零） | `feat_mutable_globals` | DSEG + 稀疏 XINIT 协议 |
| 多参数函数（4 参混合宽度，含嵌套调用） | `feat_multiparam` | OSEG/DSEG 静态参数槽 |
| 递归（斐波那契/阶乘） | `feat_recursion` | 栈帧 + CALLSEQ |
| 函数指针表 + 间接调用 | `feat_funcptr` | ecall @dr 间接调用 |
| 指针算术遍历 | `feat_pointer_walk` | i32 规范指针寻址 |
| 嵌套结构体全局（带初值） | `feat_struct_nest` | XINIT 聚合初始化 |
| `inline` 函数（C99） | `feat_inline` | internal 化 |
| `_Static_assert`（C11） | `feat_static_assert` | 编译期断言 |
| volatile SFR 直写（串口输出） | `uart_*` | SFR direct 0x80-0xff 通路 |

**证据强度与优化边界**：本工程验收配置是前端 `-O2`、无 LTO。
上表是 C 源码特性清单，不等于每项都执行对应机器指令。固定输入允许合法常量折叠：
`stdint_math`、`multiparam`、指定初始化器、CRC 查表、指针遍历及 inline 混合
在当前优化 IR 中可折叠；不能据这些 `OK` 宣称运行时 libcall/静态槽/查表均被执行。
实际保留的路径包括变量移位/rotate、`fastcc` 递归、函数指针间接调用、
变量乘法、`llvm.bswap.i16`、可变全局/XINIT 与 SFR 输出。
独立后端 lit 和 QEMU 差分负责逐项指令与 ABI 验证。整个 demo 不承诺前端 `-O0`
可用：该配置可能保留尚未支持的 const 聚合和初始化器重定位。

**未承诺的特性**：`switch` 跳转表、变量有符号除法/取余、`float/double`、
64 位整数、可变参数 `...`、第 2+ 参数为指针的多参数 ABI。源码中的 `% 3u`
是有界常数取模，当前优化后变为乘法/移位等，不代表通用 `urem` 已受支持。

## 使用方法（全部在 WSL 内执行）

```bash
cd /mnt/c/Prj/LLVM/MCS251/validation/mcs251-demo-modern

make            # 构建固件 demo.hex
make run        # QEMU 运行，串口输出到 build/demo.serial
make check      # 黄金验收：host gcc 编同一份源码生成期望输出，与 QEMU 串口逐行比对
make test-runner # 无需目标工具链，测试超时/旧输出/同错伪通过等负例
make clean      # 清理构建产物

# 选择板型；缺省 BOARD=stc32g12k128
make BOARD=stc32g12k128 check
make BOARD=stc32g144k246 check

# 保留独立证据目录；工具路径与构建目录均可覆盖
make BOARD=stc32g144k246 BUILD=/home/liu/mcs251-demo-g144 check
# 链接器变量为 MCS251_LD，不使用 GNU make 内置的 LD=ld
```

## 板级配置架构

型号差异只存在于构建层：`boards/<BOARD>.mk` 提供 Flash/EDATA、链接布局、
UART/LED 与 QEMU 参数；Makefile 生成 `$(BUILD)/board_config.h` 给 C 源消费，并把
`EDATA_END` 作为纯数值 `--edata-end` 参数传给链接器。Clang/LLVM/MC 与链接器
内部都不含型号表或型号自动探测；编译器只认识 MCS-251 架构。

| BOARD | 数据依据 | EDATA 门禁 | QEMU 说明 |
|---|---|---:|---|
| `stc32g12k128`（默认） | 现有真机实证配置 | `0x0FFF` | 当前以 G144 QEMU 机器代跑；镜像只用两型号 Flash 交集区 |
| `stc32g144k246` | 手册内存数据 + 对应 QEMU 机器 | `0x3FFF` | 原生使用 `stc32g144k246` 机器；不等于 G144 真机 UART/LED 已验证 |

两个片段当前都把 CSEG/XINIT 放在 `FF:0200`/`FF:8000`，因此默认板生成的 HEX
与多板改造前逐字节一致。若新增板型，必须补齐片段中的全部变量；未知或缺变量的
BOARD 会在 Makefile 解析阶段失败。同一个 BUILD 目录切换 BOARD 时，型号戳会
强制刷新生成头和链接命令文件；正式验收仍建议每板使用独立 BUILD 目录。

`make check` 是本工程的验收口径：同一份 C 源在宿主机（gcc）与目标机（QEMU 里的
STC32）各自运行，逐字比对输出——语义一致才算通过，必须完整输出 14 项 `:OK`
并以 `DEMO-PASS` 结束。两侧同样失败、截断、超时、旧串口文件不能判通过。
串口保存在 `build/demo.serial`，QEMU stderr 单独保存在 `build/demo.stderr`；
默认超时 30 秒，可用 `QEMU_TIMEOUT` 覆盖。构建保留 `.ll` 供检查优化后真实路径。

## QEMU 与真机串口不是同一条验收链

`uart.h` 提供三个互斥路径：`HOST_BUILD` 走宿主 stdio；默认路径走板片段所选
QEMU 的 UART test-port（写 SBUF 即输出，不模拟波特率、不置 TI）；
`STC32_REAL_HW` 走 SCON 模式1、Timer2波特率和写SBUF后TI轮询。
**真机轮询路径不能拿到当前 QEMU 中运行**。

`HRIC/BAUD/UART_RELOAD` 来自生成的板级头，并有编译期一致性断言。当前两个
片段均为24MHz、115200/8N1、Timer2 1T重载0xFFCC；其中 G12 配置已走过连续
UART真机探针，G144参数只作公共保守配置，尚无 G144 真机闭环。UART入口显式
`P_SW1=0`；该整字节写也复位其它复用路由，本demo不使用那些外设。AUXR逐位
保留其它配置，清T2_C/T，最后启动T2R。共享crt从链接器符号设置动态向上栈，
链接器按所选板的 `EDATA_END` 保证静态布局后至少剩余1KB。

两个板片段均把 XINIT 放在FF:8000程序段，不再与FE:0000起的EEPROM窗口冲突。
当前 `EEPROM_MAX=0x700` 是本演示包兼容独立MOVC探针的保守上限，推荐0；两份
自检demo自身全部ROM内容位于FF段。G12单文件selftest layout-v2已有真机闭环，
modern工程仍待复验；G144真机下载、UART与LED也须按手册和具体开发板单独验收。
完整接线/下载步骤和QEMU差异矩阵见
`/mnt/c/Prj/LLVM/MCS251/validation/mcs251-demos/REALHW-GUIDE.md`。

仅验证真机分支编译/链接时，使用独立构建目录（不执行 `make check`）：

```bash
make BOARD=stc32g12k128 \
  BUILD=/home/liu/mcs251-realhw-alice/layout-v2/modern-real-hw \
  CFLAGS='--target=mcs251-unknown-none -std=c11 -O2 -Wall -Wextra -DSTC32_REAL_HW'
```

## 输出协议

每个特性一行 `<特性名>:OK` 或 `<特性名>:FAIL got=xxxxxxxx expect=xxxxxxxx`，
全部通过时最后一行 `DEMO-PASS`，否则 `DEMO-FAIL(<失败数>)`。
HOST/QEMU协议保持一次性输出；真机完成自检后用volatile软件延时低速重发
最终状态行，避免烧录后打开串口窗口太晚而看到空白。不重跑会修改全局的自检，
周期未精确校准。仅收到重发PASS可说明最终状态；完整验收仍应终端就绪后复位
并捕获全部14项。
