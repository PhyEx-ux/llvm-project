# mcs251-uartdemo-33m — IRC 33.1776MHz UART1 串口 Demo（STC32G12K128）

MCS251/STC32 LLVM 后端验证固件：IRC=33.1776MHz 下 UART1 115200-8N1 循环心跳输出。
构建链与编码模式均按 proven 真机 PASS 配方（g12-hexdemo / g12-xfrrw / g12-divrt-i2c）。

## 产物

| 文件 | 说明 |
|---|---|
| `uartdemo-33m.hex` | 烧录文件（Intel HEX，STC-ISP 直接烧） |
| `uartdemo-33m.map` | 链接 map（HOME 0xFF0000 / VECS 0xFF0003 / BOOT 0xFF0100 / CSEG 0xFF0200 / XINIT 0xFF8000；含 `FUNC` 行） |
| `src/uartdemo.c` | 固件源码 |
| `build.sh` | 一键构建（链接传显式 ROM 窗口门禁，构建后自动跑编码模式硬门禁） |
| `check-encoding.py` | HEX 完整性 + ROM 窗口 + 251 源模式指令边界解码 + STC32G T2 布局断言（硬门禁，`--self-test` 含 8 项负例 T1–T8 + 长度正例/对账组 L1–L7） |
| `build/qemu-run.log` | QEMU 顺跑串口输出留证 |

## 烧录（三要点，缺一出乱码或无输出）

1. **STC-ISP 芯片选 STC32G12K128**，烧 `uartdemo-33m.hex`。
   固件运行时不改写 IRC 频率寄存器——IRC 频率完全由烧录时 STC-ISP 写入。
2. **STC-ISP 里 IRC 频率必须选 33.1776MHz**（"IRC频率"下拉框里有精确档位）。
   选成默认 24MHz 会把波特率带偏 38%（24M 重载 0xFFCC vs 33.1776M 重载 0xFFB8），
   串口只见乱码。这是本固件与 24M demo 的唯一差别。
3. **串口助手 115200 / 8N1 / 无流控**，接 USB 转串口的 RXD→P3.1(TxD)、TXD→P3.0(RxD)。
   烧完冷启动即循环输出，无需掐时间窗口。

## 预期串口输出（循环重复，节奏约每秒数行）

```
MCS251-UARTDEMO-33M-START
IRC=33177600 UART1=115200-8N1 T2RELOAD=0xFFB8 AUXR=0x15
HEARTBEAT 0000
HEARTBEAT 0001
HEARTBEAT 0002
...（0000–9999 循环）
```

数字递增经过 4 位十进制转换（u32 div/mod libcall → 链接 divulong/modulong 运行时），
因此本固件同时覆盖：UART 初始化、TI 轮询发送、只读数据段、32 位除法/取模运行时。

## UART 参数（源码级核对，src/uartdemo.c + stc32g-v1.h）

- IRC = 33,177,600 Hz（STC-ISP 烧录时选定）
- UART1 模式 1（8N1），REN=1：`SCON = 0x50`
- Timer2 1T 波特率发生器：重载 = 65536 − 33177600/4/115200 = 65536 − 72 = **0xFFB8**
  （编译期 `#error` 断言强制；T2H=0xFF、T2L=0xB8）
- **STC32G T2 寄存器地址：T2L@0xD7、T2H@0xD6**（非 8052 的 0xCC/0xCD）
- `AUXR`（0x8E）置位 = T2R(bit4) | T2x12(bit2) | S1BRT(bit0) = **0x15**，
  序列先清 T2R/T2CT 再最后启动，绝不置 bit3
- `P_SW1 = 0x00`：UART1 在 P3.0/P3.1
- 与真机 PASS 参照 `validation/mcs251-demo-modern/src/uart.h` 的 init 序列逐句一致

## 构建方法

```sh
./build.sh        # 构建并在末尾自动跑 check-encoding.py 硬门禁
```

链（proven 全链，与真机 PASS 配方一致，无 sdas251；两处 build 层增量见下）：

```
clang-24 --target=mcs251-unknown-none -Xclang -mcs251-memory-contract=1,1,32,8,1
  → llc -mcs251-object-format=elf
  → mcs251-lld --flash-base=0xff0000 --flash-size=0x10000 --keep-symbols
               （--area-start=HOME/VECS/BOOT/CSEG/XINIT）
  → llvm-objcopy -O ihex → uartdemo-33m.hex
```

与 proven 脚本 g12-xfrrw-build.sh 的两处差异（都在链接步，均不改动固件字节）：

1. **显式 ROM 窗口门禁** `--flash-base=0xff0000 --flash-size=0x10000`。
   mcs251-lld 的门禁是 **opt-in**：不传这两个参数时 `checkFlashGate()`
   直接短路，链接器做**任何**越界检查（`lld/MCS251/Driver.cpp` 只在两个参数
   同时给出时才置 `Core.FlashGate`）。proven 脚本原本没传，"ROM 门禁"声明
   是空的；本脚本补上。窗口值取自 ISR 链接器测试的既有调用
   （`validation/mcs251-isr` 的 `compile-matrix.py` 与
   `proposals/ISR-TASK-BREAKDOWN.md`：`--flash-base=0xff0000
   --flash-size=0x10000`），覆盖 0xFF0000..0xFFFFFF，包含全部 area 起点。
   已验证：把 XINIT 挪到 0xFE8000 或把窗口缩到 0x300，lld 报错且不产出
   任何文件；不传门禁参数时同样的越界布局会被静默接受。
2. **`--keep-symbols`**：让 map 携带 `FUNC addr +len name` 行，
   `check-encoding.py` 据此得到每个函数的精确指令边界（见下节）。

运行时依赖：`/home/liu/c23demo-work/rt/mcs251rt_{divulong,modulong}.o`（proven 除法/模
运行时，真机 PASS g12-divrt 同款）与 `validation/mcs251-elf/runtime/crt-selfstart.yaml`
（yaml2obj 生成的自启动 crt：BOOT 置 WTST=0、spx=#0x10F、ecall 全局初始化、ecall _main）。

## 编码模式硬门禁（hwframe 事故防线，构建自动执行）

`check-encoding.py`（v3）在构建末尾自动执行，按指令边界做主检查：

**1. Intel HEX 完整性（v2 的 reader 不校验，已修复）**：逐条记录校验
count 字段与实际字节数、checksum（累加和低 8 位为 0）、记录类型
（仅 00/01/04/05）、EOF 记录存在且其后无记录、无重复写地址；坏记录报错
退出（exit 2）。

**2. ROM 窗口复核**：镜像全部字节与 CODE 类 area 起点都在
`[0xFF0000, 0x1000000)` 内——从产物侧复核链接器门禁（同 lld 一样，
DSEG/ISEG/OSEG/REG_BANK 等 RAM 区域豁免）。

**3. 指令边界线性解码（主检查）**：BOOT 整区 + map 每个 `FUNC` 符号
（`_main`/`__divulong`/`__modulong`，`--keep-symbols` 给出精确边界）
按指令边界逐条解码，每条指令都必须是合法 251 源模式编码且恰好覆盖到
区域末尾。BOOT 序列字节锚点同时断言：`75 E9 00`（mov WTST,#0）+
`7E F8 01 0F`（mov spx,#0x010F）+ `9A FF 01 14`（ecall 全局初始化）+
`9A FF 02 00`（ecall _main@0xFF0200 ∈ CSEG）。长度表来源：LLVM 后端
`MCS251MCCodeEmitter.cpp` 的封闭编码集（逐字节对过 sdas251 gold）+
8051 经典指令长度表 + 两条 sdas251 实测的通用形（`0B/1B` 字寄
inc/dec 2 字节、`AD` 字乘 2 字节）；**表外任何字节一律 FAIL**
（fail-closed）。经典长度表已逐项与公开 8051 指令 map 对账全等
（255 个操作码无一缺漏、无一长度错，该全等由自检 L7 组对机器断言，
不再只是人工声明）：`0x76..0x7F` 全部是 2 字节
MOV immediate（`76/77` = mov @ri,#imm、`78..7F` = mov rn,#imm；早期
版本曾把 `78..7F` 误覆盖回 1 字节，导致转义 `A5 7E 12` 被解成
2 字节——已修）；补齐 sdas251 实测的转义经典缺项 `D6/D7`（xchd）、
`E6/E7`（mov a,@ri）、`F6/F7`（mov @ri,a）、`B2`（cpl bit），以及
`0B/1B` 的 mode-8 字移形式（`mov wr4,@wr2` = `0B 18 20`，3 字节）。
第三轮 review（Alice）又抓出 7 个裸经典缺项 `B3/B4/B5/E4/E5/F4/F5`
——缺项时裸 `cjne a,#0x12,rel` 这类合法源模式指令会误报
"undecodable bare opcode"；现按 sdas251 现场汇编实测补齐（source
mode 下全部裸编码、不属 A5 转义、不与 native 竞争）：`cpl c` =
`B3`（1 字节）、`cjne a,#0x12` = `B4 12 09`（3 字节）、
`cjne a,0x30` = `B5 30 06`（3 字节）、`clr a` = `E4`（1 字节）、
`mov a,0x30` = `E5 30`（2 字节）、`cpl a` = `F4`（1 字节）、
`mov 0x30,a` = `F5 30`（2 字节），均与公开 8051 表一致（无分歧）。
长度自检含正例组：全部期望值先用 sdas251 现场汇编读回后固化；
L7 组把 CLASSIC_LEN 与公开 8051 长度 map 逐项对账（255 个操作码，
A5 除外），表再缺项/错长直接 FAIL。
本轮又以 sdas251 复核了 151 条代表性指令形：除 C 位操作被 sdas251
源模式汇编成 native `A9 <经典操作码> <direct>` 形（属 LLVM emitter
封闭集之外，门禁保持 fail-closed，与经典表无关）外，其余 139 条与
模型逐一相符、与公开表零分歧。
本次实测：BOOT 41 条 + `_main` 457 条 + `__divulong`
514 条 + `__modulong` 470 条 = **1482 条指令、3448 代码字节 100% 解码**。

**4. 字符串池校验**：字符串字面量落在 CSEG 内 `_main` 之后（本固件
86 字节，`_main` 结束到 divulong 之间）。FUNC 未覆盖的 .text 间隙按纯
数据校验：只允许可打印 ASCII / NUL / CR / LF / TAB。

**5. 反向断言（辅助证据，保留 v2）**：

- binary 模式转义签名 `A5 7E`/`A5 9A`/`A5 8A`/`A5 AA`/`A5 78`/`A5 7A`
  **只在已解码指令的 opcode/prefix 字节上判定**（直接用解码已产出的
  `A5+op` 直方图）：`A5 7E`（转义的 `mov r6,#imm`）可解码、且 emitting
  它表示 binary 模式生产者回归，但这**不能靠原始字节扫描判**——立即数
  里出现 `A5 7E`（如合法原生 4 字节指令 `7E 44 A5 7E` = mov
  wr4,#0x7EA5）不构成签名命中，不得 FAIL。原始字节级 `A5 xx` 频率仅作
  `[info]` 证据输出，永不触发失败。T7 自检把 `_main` 内 SCON 写窗口的
  首条 3 字节指令（`7E 00 50`）原位换成等长 `A5 7E 50`，解码保持精确
  对齐，验证该规则确实抓得住"指令起始处"的真签名。
- 写 0xCC/0xCD（8052 T2 布局）必须为 0；SFR 写窗口逐字节命中：
  `SCON(0x98)=0x50`、`T2L(0xD7)=0xB8`、`T2H(0xD6)=0xFF`、
  `AUXR(0x8E) |= 01/04/10`
- 字节频率证据（**纯信息输出**，走 `[info]` 证据行、不经过 `check()`
  失败路径，永不触发 FAIL；与"仅作参考"的声明一致）：代码区裸 `7E`
  147 次、裸 8051 `74/75/F5` 17 字节（0.49%）
- CSEG 3423 字节 < XINIT 0xFF8000

**自检（负例 + 长度正例 + 对账）**：`python3 check-encoding.py
--self-test uartdemo-33m.hex uartdemo-33m.map` 对固件副本注入 8 类破坏
——坏 checksum、count 字段不符、缺 EOF、垃圾行、未知记录类型、函数区
植入非法操作码 `0xB7`、指令边界处植入 `A5 7E` 签名、字符串池植入不可
打印字节——逐一断言必须被抓到（T1–T8）。另有**长度正例组 L1–L7**：
`instr_len()` 对 sdas251 现场汇编读回后固化的参考字节逐条断言——
含 `0x76..0x7F` 全部转义 MOV immediate（`A5 7E 12` = 3 字节）、其余
A5 转义（`A5 D6` xchd、`A5 B8 10 FC` cjne 等）、裸经典、原生 251 形
（含立即数内含 `A5 7E` 的 `7E 14 A5 7E` = 4 字节，必须解码合法），
以及必须抛 DecodeError 的边界负例（裸 `76`、`A5 75`、裸 `7B`、
`A5 B4`——B4 低半字节 <6 只许裸编码、永不被转义——等）。
L6 组为第三轮补齐的 7 个裸经典缺项 `B3/B4/B5/E4/E5/F4/F5` 的 sdas251
实测正例 + 整段汇编流的端到端解码；L7 组把 CLASSIC_LEN 与公开 8051
长度 map（255 个操作码）逐项对账全等。负例全抓 + 正例/对账全对才
PASS。

**关于 A5 前缀（v3，由解码器精确计数）**：251 源模式对低半字节 ≥6 的
经典指令（`mov a,rn` = E8..EF、`mov rn,a` = F8..FF 族等）加 A5 前缀
（sdas251 `mcs251mch.c needs_prefix` / 后端 `MCS251MCCodeEmitter::
putOpcode` 同规则）。本固件 CSEG 共 **500 条 A5 前缀指令**，全部位于
合法经典族：E8-EF 253 条 + F8-FF 247 条（运行时 491 条：div 243 +
mod 248；应用代码 `_main` 9 条）。**"CSEG 无任何 A5 字节"不是源模式的
判据**；判据是"按指令边界全解码为合法源模式 + 转义族合法 +
已解码指令级 binary 签名为 0（原始字节里的 A5 仅作信息输出）"。

## QEMU 验证

```
qemu-system-mcs251 -M stc32g144k246 -bios uartdemo-33m.hex \
  -accel tcg -display none -monitor none -serial stdio
```

顺跑通过（2026-09-09，留证 `build/qemu-run.log`）：起始 banner + HEARTBEAT 持续
递增，十进制转换经 div/mod 运行时（含 A5 前缀指令）解码执行正常。真机参数
（IRC 33.1776MHz）以 STC-ISP 烧录设置为准，QEMU 不模拟 IRC 频率档。
