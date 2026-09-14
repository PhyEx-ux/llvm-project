# T10 G12 实机 Demo v1（单层）

状态：2026-09-10，开发验证固件。编译、链接及静态检查通过；遵照用户要求**未运行模拟器**，未运行实板或动态故障注入。不是T09冻结资产资格报告。`release/t10-g12.hex` 是硬件版，真实TI轮询。

## G1 重建登记（2026-09-14）

`release/` 现为 G1 工具链（ISR 记录 ProtocolVersion=2、127 槽 profile、IRQ 配方 BOOT=0xFF0500/CSEG=0xFF0700、reset=LJMP 0xFF0500）按 `build.sh` 重建的资产；check-crt-irq 20/20、check.py 全过。G1 前旧资产（52 槽、BOOT=0xFF0210/CSEG=0xFF0400）原样封存于 `release-pre-g1-20260910/`，旧→新 hash 对照与差异逐条归因见 `REBUILD-20260914.md`。旧 manifest 曾由一份未提交的 main.c（E1 前）构建，现盘 main.c 自 E1 提交后未再变化；工具链对旧 IR 复算结果逐字节一致（证明后端确定、差异不在 llc 层），结合源 diff 将 main 产物差异归因于该源码编辑（前端层逐字节等价未单独证明）。动态项（s/r 实测、故障注入）仍**待真机**，本登记不改变任何 NOT_RUN 结论。

## 操作

STC32G12K128，STC-ISP IRC=24MHz，UART1 P3.0/RX、P3.1/TX，115200/8N1、无流控。串口TTL TX接P3.0，RX接P3.1，共地。镜像在FF程序Flash，无XINIT负载。

1. 烧录 `release/t10-g12.hex`，打开串口，等待循环READY。
2. 先发送ASCII `s`（HEX 73），执行一次真实Timer0中断。
3. 报告会持续重发。发送 `q`（HEX 71）回到READY。
4. 单次成功后发送 `r`（HEX 72），逐轮执行10000次，每100次输出进度。
5. 长测期间不接收取消命令。死在ISR或PC跑飞时可能只有最后一条RUN/进度，不保证打印FAIL；需手动复位。

输出示例仅说明格式，**不是已运行结果**：

```text
T10-G12-v1 READY 24MHz 115200; s=single r=10000
T10-G12-v1 RUN case=s
T10-G12-v1 RESULT case=s status=PASS completed=0001 attempted=0001 code=00 addr=0000 expected=00 actual=00 hits=01 SP=0600/0600 PSW_RAW=00/00 PSW1_RAW=00/00
FLAGS=NOT_TESTED NESTED=NOT_TESTED WINDOWS=NOT_TESTED
```

所有数值字段都是**十六进制**。10000次完成显示 `completed=2710 attempted=2710`。`hits` 为最后一轮中断次数，每轮必须01，不是总次数。长测逐轮更换SEED、检查结果；寄存器哨兵在各轮固定为按寄存器编号区分的模式，不是每轮更换模式。

`status=TIMEOUT` 表示done不为1，等待预算约419万次直接RAM倒计数，非精确毫秒值。实际中断完成后任何断言不一致显示FAIL，保存首个错误，不再继续测试。重复报告是同一最终结果，不是后台继续测试。

| code | 检查失败项 |
|---|---|
| 01 | done标志（报告为TIMEOUT；不是精确区分未触发与标志损坏） |
| 02 | 单轮ISR次数不等于1 |
| 03 | R0..R31，地址0040..005F对应编号0..31 |
| 04 | 返回SP与基线不符，0060低字节、0061高字节 |
| 05 | DPL/DPH/DPXL恢复不符，0062..0064 |
| 06 | 共享byte应由12变为52 |
| 07 | 跨文件helper结果错误 |
| 08 | 栈保护区损坏 |

## 被测路径

`isr.c` 的 `__attribute__((interrupt(1)))` 由Clang/LLVM生成，链接器注册Timer0向量。ISR实际软件序言是PSW加DR0..DR28与DPX，共37字节；局部volatile数组占8字节；普通跨文件helper以ECALL调用，helper局部占4字节，ERET返回，ISR逆序恢复并RETI。

普通汇编函数 `sentinel` 保存C调用环境后换到测试栈，设置32个字节寄存器哨兵和可见DPX值。R16..R31用DR16..DR28访问，因为汇编器不支持R16等独立字节名称。等待只使用JB和直接RAM DJNZ，不借用哨兵寄存器。中断返回后先记录PSW原始值，再直接保存R0..R15，随后用已保存的DR0搬运DR16..DR28，最后记录SP/DPX，关闭中断并恢复C栈。

Timer0每轮停表、清标志、装载F000后重启，ISR停表；UART占用Timer2，两者独立。ISR不打印，前台不在可被中断阶段调用helper，避免静态参数槽重入。当前ISR与helper实际主要使用低寄存器，故高寄存器的“返回后值正确”不等于已证明其面对主动破坏后的恢复能力；本版不是全寄存器破坏覆盖测试。

PSW/PSW1只记录原始值，不参与PASS。DPX只比较DPL/DPH/DPXL，不给保留字节构造要求。只检查共享byte保留，不宣称共享bit原子操作资格。没有单独读取返回PC；成功回到检查/报告路径是控制流证据，不是精确PC快照。

## 内存与预算

| 区间 | 用途 |
|---|---|
| 0020..002F | CRT保留bit字节；20=done，21..23=等待计数器 |
| 0030..0064 | 显式链接保留，种子/结果/哨兵快照/C栈保存 |
| 0065..006F | 本次链接分配的C参数槽，map为准 |
| 0100..010F | 显式保留首次失败信息 |
| 0580..05FF | 下guard，填A5 |
| 0601..087F | 中断测试栈，初始SPX=0600 |
| 0880..08FF | 上guard，填5A |
| 0910..0FFF | 普通C栈，CRT初始SPX=090F，容量1776字节 |

当前被测中断路径栈预算：硬件4+软件37+ISR局部8+ECALL3+helper局部4=56字节。此预算是静态推算，不是实测高水位。下guard检查下溢，上guard检查越过测试栈容量的写入；不能保证识别所有非法跳跃写入。整个保留区通过ELF DATA段参加链接分配，不与普通参数槽隐式重叠。

## 构建和静态检查

```sh
bash build.sh /tmp/t10-g12-new
```

输出目录必须为空，不会递归删除已有内容。默认工具路径可用CLANG/LLC/LLD/YAML2OBJ/OBJCOPY/SDAS/SDLD覆盖。无LTO。报告辅助函数的noinline规避已随E1（提交088d95bd7）移除，当前源码不再使用。

`gen-sentinel.py` 汇编内部仅相对跳转的独立模块，提取连续机器码包装为ELF .text；另附ABI note及固定DATA保留。它没有ISR声明，不能替代被测ISR。`check.py` 检查HEX校验和与地址、向量目标、ISR序言/尾声结构、局部栈分配、真实helper调用及ERET/RETI、内存保留和栈容量；CRT另经既有20项字节检查。

release保留HEX、ELF、map、IR、编译器汇编、sentinel列表和生成源码、manifest及STATIC-CHECKS。manifest记录实际使用工具和输入/产物SHA256；不伪称工具已匹配T09冻结身份。动态正常/负向验证、故障检测有效性均待实板，本次没有运行QEMU。

## T10覆盖限制

本版只实现设计的s/r，未实现n两级嵌套。即使实板显示PASS，也只代表本版断言覆盖的单层测试通过，不能自动放行PSW1语义、全部寄存器主动破坏覆盖、完整逐指令保存/恢复窗口、默认fail-stop或RETI/IE/IP时序。正式 `board-results.json` 未改为PASS。
