# STC32G12K128 首次真机验收指南

适用对象：STC32G12K128（手册核对版本为 STC32G12K128-24A，2026-08-19）。
layout-v2 已从布局上规避 EEPROM 窗口冲突。旧版串口空白不等于首字符前死机：
后续M1.5/M1.6/M1.7真机实验已验证asm子集、ECALL/ERET和被测编译器函数路径；
UART V0观测TI到来，V1连续整行在115200正确输出。早先“全编码错误”“TI永不
置位”“P_SW1复位值非零”均不能作为已证根因。一次性输出在打开串口前结束是
高置信可观察性假说；当前真机版改为低速重发状态，仍须重烧捕获完整自检结果。
本指南区分“手册依据”“QEMU 实测”和“待真机裁定”，不把仿真通过当作实机通过。

## 1. 固定验收条件

- 官方下载工具：Windows **AiCube-ISP**，选择实际芯片型号及正确 COM 口。
- 在 ISP 中明确将**用户 HIRC 设为 24MHz**。上电先运行 ISP 的 24MHz HIRC，
  不代表用户程序必然继承 24MHz；用户程序使用 ISP 保存的时钟配置。
- **本包统一支持 EEPROM 分区 ≤0x700 字节（1792字节，含现场1K），推荐0**。
  layout-v2 两份demo的 XINIT 已从 `FE:0000` 移至 `FF:8000` 程序区，全部ROM
  内容均在FF段，因此不再依赖EEPROM设0。MOVC探针必须在非FF段执行，现移至
  `FE:0800`，所以它仍要求上述分区上限；不支持将更大分区随意套用到整个包。
  更改分区后完整断电再上电。旧版FE:0000 XINIT及FE:0200探针不能继续烧录。
- UART1：115200、8 数据位、无校验、1 停止位（8N1），无流控。
- UART1 默认路由：RxD=P3.0、TxD=P3.1；P3.0/P3.1 配为准双向弱上拉，保留
  其它 GPIO 配置。代码不操作 P3.2。
- 使用与开发板供电/IO 电平匹配的 **USB-TTL** 串口适配器；禁止将 RS-232
  正负电压接口直接接入芯片。供电电压与封装引脚号以你的板图/芯片手册为准。

UART 参数依据手册页 441、675、774、777：SCON=0x50（模式1、REN=1），
Timer2 停止后装入 T2L=0xCC、T2H=0xFF；清 AUXR.B3（T2_C/T=0），分别置
B0（S1BRT）、B2（T2x12=1T）、最后置 B4（T2R）。这些置位合计为 0x15，
**0x15 不含 B3**，不得误设为外部计数模式。其它 AUXR 位用读改写保留。

重载计算：`65536-round(24000000/(4*115200)) = 0xFFCC`；实际波特率约
115384.6，误差 +0.16%。发送使用“写 SBUF → 等 TI → 清 TI”，不依赖预置 TI。
本包占用 Timer2 作为 UART1 波特率发生器，不能同时将 Timer2 分配给别的用途。

## 2. 接线与上电

按信号名找板上排针，不按本文猜测封装管脚编号：

```text
PC USB-TTL TXD  ---->  板上 P3.0 / RxD
PC USB-TTL RXD  <----  板上 P3.1 / TxD
PC USB-TTL GND  ------ 板上 GND
板上 VCC       ------ 合适的独立电源或板载供电（避免双电源互灌）
```

确认 TX/RX 交叉、共地，且适配器不会经 TXD/VCC 在“断电”时反向给板供电。
不要同时把 P3.0/P3.1/P3.2 拉低：该组合参与 USB 下载判断，可能使芯片不进入
预期用户程序。G12 的 GPIO 不要按其它型号臆设 10K 下拉；本包不启用额外拉电阻。

## 3. 构建要烧录的 HEX

所有命令在 WSL 内执行。使用本包配套的 `crt-selfstart.asm` 和
`mcs251_ld.py`，不要混用旧 crt（旧值 SPX=0x2fff 在真机上越界）。

### 首选：8 项单文件自检

```bash
python3 /mnt/c/Prj/LLVM/MCS251/validation/mcs251-demos/build-realhw.py \
  --out /home/liu/mcs251-realhw-alice/layout-v2/selftest-real-hw
```

默认启用 `STC32_REAL_HW`，生成：

- `/home/liu/mcs251-realhw-alice/layout-v2/selftest-real-hw/selftest.hex`：**要烧录的文件**。
- 同目录 `layout.json`：区域实际地址与初始 SPX。
- `.ll`、`.rel`、`.lk`：复验中间产物；它们不是烧录文件。

layout-v2单文件构建脚本检查非空 CODE slice 全在 `FF:0000–FF:FFFF`，内部数据在栈起点之下，
XDATA（如存在）在 `01:0000–01:1FFF`。标准 Intel HEX 含扩展线性地址记录，
在 AiCube-ISP 中使用 HEX-80/Intel HEX，不转成丢失高地址的裸 binary。

Windows 可通过 `\\wsl.localhost\Debian\home\liu\mcs251-realhw-alice\layout-v2\selftest-real-hw\selftest.hex`
访问该文件，或复制到本地磁盘。WSL 发行版名不是 Debian 时相应替换。

### 14 项工程版

```bash
make -C /mnt/c/Prj/LLVM/MCS251/validation/mcs251-demo-modern \
  BUILD=/home/liu/mcs251-realhw-alice/layout-v2/modern-real-hw \
  CFLAGS='--target=mcs251-unknown-none -std=c11 -O2 -Wall -Wextra -DSTC32_REAL_HW'
```

烧录 `/home/liu/mcs251-realhw-alice/layout-v2/modern-real-hw/demo.hex`。
**不要烧默认 `make check` 生成的 QEMU test-port 固件**；不要用 QEMU 跑真机
TI 轮询分支。编译参数改变时用不同 BUILD 目录，避免复用旧产物。

### 本次 layout-v2 发布位

重构建并审计后发布到 Windows 侧，烧录前核对本次提供的 SHA256：

- `/mnt/c/Prj/LLVM/realhw-hex/selftest.hex`（Windows：`C:\Prj\LLVM\realhw-hex\selftest.hex`）
- `/mnt/c/Prj/LLVM/realhw-hex/modern-demo.hex`
- `/mnt/c/Prj/LLVM/realhw-hex/movc-arbitration.hex`

旧发布件保留在 `/home/liu/mcs251-realhw-alice/layout-v2/previous-published/`。
`selftest-eeprom1k.hex`（FE:0400）是现场临时诊断版，不是这里的FF:8000根治版。

## 4. AiCube-ISP 操作顺序

1. 关闭占用 COM 口的串口终端；选择芯片型号、COM 口，核对供电和接线。
2. 打开上节指定的 **layout-v2真机版 `.hex`**；设置用户HIRC=24MHz、
   EEPROM≤0x700字节（推荐0，现场1K即0x400也可）。不要误选旧HEX或临时FE:0400版。
3. 芯片断电；确认没有 USB-TTL 反向供电。
4. PC 点击“下载/编程”，进入等待芯片状态。
5. 再给芯片上电，等待芯片识别、擦写、校验全部成功；保存下载日志和设置截图。
6. 下载程序释放 COM 口后，打开串口终端设115200/8N1。新真机版会低速重发
   最终状态（MOVC则重发仲裁行），因此晚打开也能观察；不过完整检查项可能已
   输出过，正式验收仍须终端就绪后再次复位/断电上电并从第一行捕获。
7. 同时保存芯片型号/批次、板子版本、HIRC/EEPROM设置、HEX SHA256 与串口日志。

`stcgal` 的型号表含 G12K128 的 MCS251 标识，可作为**非官方、尚未实机验证**的
尝试路径；首次正式验收优先 AiCube-ISP，不把型号识别等同于下载流程已验证。

## 5. 预期串口与判定

单文件版完整输出：

```text
widths:OK
arith:OK
shift:OK
multiparam:OK
globals:OK
const_table:OK
recursion:OK
c99c11:OK
SELFTEST-PASS
```

工程版应完整输出14项 `:OK`，最后为 `DEMO-PASS`；具体行序与工程版
`host.expected` 相同。串口终端显示时可能补 CR，原始日志比较可只规范化 CRLF，
不得删掉错误行或忽略丢失的检查项。真机一次自检结束后重复最终PASS/FAIL行，
不会重新执行有副作用的特性函数；QEMU/HOST仍保持原有单次精确协议。

验收通过必须同时满足：下载校验成功、完整行序正确、无 `FAIL`、无启动返回
标记 `S`、无意外中断标记 `!`、最终终止行出现且无反复复位。单独出现几个 OK
或最后一行不能替代完整记录。`-O2` 可能折叠常量特性，demo 的通过不是所有
机器指令的穷举真机验证。

## 6. 内存与启动契约

手册页556/559/561、§10.3.1/§11.3.5：

| 用途 | G12K128 真实范围 / 本包布局 |
|---|---|
| EDATA、向上生长栈 | `00:0000–00:0FFF`，只有4KB |
| XDATA | `01:0000–01:1FFF`，8KB |
| Flash | `FE:0000–FF:FFFF`，128KB |
| HOME / 向量 | `FF:0000` / `FF:0003 + n*8` |
| BOOT / CSEG | `FF:0100` / `FF:0200` |
| XINIT（layout-v2） | `FF:8000`程序区，不再占用FE EEPROM窗口 |

当前CSEG只有约8–10KB，低于FF:8000，XINIT与它不重叠。HEX为稀疏记录，不因
中间空隙填满Flash；后续程序增长必须重新审计各slice，不能假定CSEG永远不会
接近XINIT。FF段总容量为64KB，不能据128KB芯片容量忽略这一布局限制。

此前 QEMU 专用栈 `SPX=0x2fff` 已被本包自启动 crt 修复。新 crt 引用
`__mcs251_stack_base`：链接器按所有内部字节数据 slice 的真实最大 end 计算，
至少避开低0x100，再做16字节对齐并加16字节间隔；SPX指向首个栈字节之前。
数据空洞、多个 DSEG slice 和 OSEG overlay 都计入，不使用并非高水位的
`s_DSEG+l_DSEG`。**链接器保证剩余至少1KB，空间不足则拒绝链接，无需手工设栈底。**

这只保证静态布局有空间，**不等于运行时无限递归或任意VLA不会栈溢出**。
本包当前输入（含fib16）用冻结QEMU `one-insn-per-tb=on` 与 `cpu,nochain`
逐指令跟踪（无固件插桩）实测如下；这些是默认QEMU分支的峰值，不冒充真机时序测量：

| 固件 | 内部静态slice最大end（不含末端） | 初始SPX | 最大SPX | 峰值栈用量 |
|---|---:|---:|---:|---:|
| 14项工程版 | 0x006B | 0x010F | 0x01C5 | 182字节 |
| 8项单文件版 | 0x003E | 0x010F | 0x01CA | 187字节 |

两者分配栈容量均3824字节。证据位于 `/home/liu/mcs251-realhw-alice/modern-qemu/demo.stack.json`
与 `/home/liu/mcs251-realhw-alice/selftest-qemu/selftest.stack.json`，对应 `.cpu.log.gz`
保留每条指令之前的寄存器快照。扩大数据、递归深度或加入中断嵌套后必须重新量测。QEMU专用 SDCC `crt0.asm`
未改变，仍不属于本包可烧录启动资产。

BOOT 先写 WTST(0xE9)=0：手册p553规定 G12上电值为7，工作频率低于35MHz
强烈建议改0。本包24MHz满足条件；改主频/芯片后不能盲目沿用这个值。

## 7. “QEMU过、真机可能失败”的差异矩阵

| 项目 | 冻结 QEMU 与 G12真机差异 | 本包措施 / 扩展时注意 |
|---|---|---|
| `00:1000–00:3FFF` | QEMU G144模型有16KB EDATA，G12没有此物理RAM保证 | 自启动栈已改4KB容量门禁；不得烧QEMU专用crt0固件 |
| XDATA超`01:1FFF` | QEMU可接受更大范围，G12仅8KB | 当前demo不用越界XDATA；新增对象需审核布局 |
| `FC:2800–FD:FFFF` | 旧验证链在低Flash运行，G12无该Flash | 两demo只用FE/FF；MOVC探针执行FE，不用FC |
| RAM执行别名 | 模型提供的别名不证明G12有相同硬件映射 | 本包没有RAM执行；禁止据仿真结果扩大映射 |
| SBUF/TI | QEMU即时输出且不置TI，不模拟发送时序 | 真机单独初始化并写后查TI；两种固件不要烧错 |
| P_SW1 / Timer2波特率 | 模型不能验证真实路由、主频、分频 | ISP24MHz、P_SW1显式0、FFCC重载的V1连续文本已真机通过；复位值未测 |
| WTST | QEMU未建模取指等待 | BOOT按24MHz条件设0；不能据模型推算真实耗时 |
| ISP / P3.2下载判断 | QEMU不走硬件ISP启动流程 | 按断电→点击下载→上电操作，避免P3.0/1/2全低 |
| MOVC bank | 模型用当前PC bank，手册例程支持固定FF bank | 用下一节独立双哨兵微固件仲裁，不预先宣布真机结果 |

## 8. MOVC 仲裁（独立烧录，覆盖自检固件）

构建并复跑冻结 QEMU：

```bash
python3 /mnt/c/Prj/LLVM/MCS251/validation/mcs251-demos/movc-arbitration/build.py \
  --out /home/liu/mcs251-realhw-alice/layout-v2/movc --run-qemu
```

真机烧录 `/home/liu/mcs251-realhw-alice/layout-v2/movc/movc-real-hw.hex`；不要烧
`movc-qemu.hex`。同样ISP24MHz、EEPROM≤0x700字节（推荐0）、115200/8N1。

layout-v2探针在 `FE:0800`附近执行 MOVC（实际指令地址见布局文件），避开
FE:0000起的常规1K窗口；DPTR=0x8000、A=0，两个哨兵仍分别为
`FE:8000=0x3C`、`FF:8000=0xA7`。同时使用完整24位DR寻址读取两处作为控制。
布局文件 `.layout.json` 可核对 MOVC 指令地址，固件不使用MOVC读取提示字符串。
真机版低速循环重发仲裁行，QEMU版只输出一次以保持回归协议。

冻结 QEMU 当前真实输出：

```text
MOVC=3C FE=3C FF=A7
```

真机结果解释：

- `MOVC=A7 FE=3C FF=A7`：支持固定FF bank，证明当前QEMU模型与G12不符。
- `MOVC=3C FE=3C FF=A7`：支持当前执行bank，固定FF的假设不适用于本次G12。
- 控制值不是 `FE=3C FF=A7`、MOVC为其它值、无输出：**不能裁定MOVC**，先排查
  Flash/EEPROM、烧录地址、串口和启动配置。

置信度：官方例程p1589把DPTR0=#1000H注释为读FF1000H，是强支持固定FF的证据；
p1655规范仅写EA=(A)+(DPTR)，没有明确bank文字。必须保留实际真机输出才能定案。

## 9. 常见故障排查

- 下载找不到芯片：核对交叉TX/RX、共地、电平、COM占用、真正断电和上电时序。
- 下载成功但无输出：确认烧的是 `STC32_REAL_HW` 版；终端已打开后再复位；
  检查HIRC=24MHz、P3.0/1路由、Timer2是否被其它代码占用。
- 乱码：优先检查ISP用户时钟、终端115200/8N1、电平和接线，不先改真值。
- 反复重启或递归项之前停止：检查电源、旧crt栈值、EEPROM分区和栈峰值。
- 首字符前卡死或出现 `globals:FAIL`：确认烧的是layout-v2（XINIT=FF:8000），
  而不是FE:0000旧版；核对下载校验和HEX SHA256。MOVC还需检查EEPROM上限。
  EEPROM=0是独立诊断手段，但新版两demo不应依赖该设置才能通过。
- 出现 `S` / `!`：分别表示主函数返回 / 无处理器中断，不属于正常通过输出。
- 动态栈链接门禁失败：减少内部静态数据或调整有依据的存储方案；不要删除门禁
  或把EDATA上限改成QEMU的16KB来让真机包“编过”。
