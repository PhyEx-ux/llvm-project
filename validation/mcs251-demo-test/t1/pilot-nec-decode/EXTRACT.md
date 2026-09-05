# EXTRACT — t1/pilot-nec-decode

## 出处
- demo 27-红外遥控接收程序(NEC码)-数码管显示用户地址和键值/C语言/main.c:165
  `void IR_RX_NEC(void)`（NEC 红外解码状态机：同步头捕获 + 位移位解码 +
  数据/反码校验 + 字节轮转）
- **授权路线 1（零参 ISR 状态机内核）**：原调用方为
  `void timer0(void) interrupt 1 { IR_RX_NEC(); ... }`，去中断属性与 ISR
  壳，本体即零参纯计算函数，状态全在全局——提取零改形

## 改写清单（详见 kernel.c 头注释）
1. bit→u8（P_IR_RX_temp/B_IR_Sync/B_IR_Press）；PSW F0 暂存→局部 u8
2. 输入 `#define P_IR_RX (PBIN & 0x40)`（SFR 读）→全局 g_ir_level，
   kernel 内自行归一 0/1
3. 定时阈值按 SysTick=10000/48MHz 折算字面量（demo 宏整数除法同源）：
   SYNC_MAX=150/SYNC_MIN=97/SYNC_DIVIDE=123/DATA_MAX=30/DATA_MIN=6/
   DATA_DIVIDE=16/位数 32
4. `~IR_DataShift == IR_data`（正反码校验）改写 `(u8)(~IR_DataShift)
   == IR_data`：原式在 C 整数提升下 int 负值与 unsigned char 比较恒假
   （host 编译器语义），Keil C251 实际按 8 位求值；显式截断钉住 8 位
   语义，解码逻辑未动
5. 11 个状态全局 extern 化（defined global 缺口）
6. 向量：gen-vectors.py 生成两帧（0x12/0x34/0xB5 + 0xFF/0x00/0x5A，
   帧间 160 tick 高电平 idle——无 idle 时帧 2 SYNC 判定沿被帧 1 尾段
   污染（SampleTime=11 进不了 SYNC 窗），数据错位导致校验失败，分析
   记录于 gen-vectors.py 注释）
7. 驱动按 demo 应用层契约在打印后清 B_IR_Press

## 判定（**当前状态：PASS**，基线重置 2026-09-05，宽度修复后）
- 历史：帧 2 曾 UserCode 写 0、IR_code store 丢失，移交静默错码专项。
  双独立取证线（Alice+Sakuna）定性为 **T1 提取层宽度缺陷**——`typedef
  unsigned int u16` 使 host-clang(x86) 把共享对象 extern 成 i32、
  store i32 写 4 字节，而 SDCC 侧 `unsigned int`=16 位对象只有 2 字节；
  i32 store 在 0x000B 写 [00,00,34,12]，SDCC 读 [00,00]=0x0000 并越界踩
  邻接（IR_code）。属测试生成链问题，非后端 bug。
- 修复：kernel.c:32 `typedef unsigned short u16`（SDCC 与 host 的 short
  均为 16 位），其余不变。新基线 llc(67a17057, post-端序修复)下 DUT
  两帧全对或与 oracle 一致：
  `Bp01u3412kB5H34L12dB5p01u00FFk5AH00LFFd5APASS`
- 已排除（probe）：store i16 方向正确（0x00FF 精确）、u8 双向含 0xFF
  正确、wrapper→kernel load u8 0xFF 正确。完整证据：
  /home/liu/mcs251-investigate/repro/ 与 /home/liu/mcs251-demo-test/probes/nec-frame2/

