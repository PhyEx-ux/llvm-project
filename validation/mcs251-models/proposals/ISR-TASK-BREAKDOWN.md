# 中断战役任务书

**作者:Alice。状态:设计冻结文本,尚未实施、尚未运行本文新增测试。**
**落盘:PM 于 2026-09-09 依 Alice 交付文本原样写入此路径。**

接口冻结区、卡清单、依赖关系及构建安排已通过协调者消息报送 PM。

---

# 0. 执行者通用纪律

本节编号为 **D0**。每张任务卡均强制适用,派单时必须连同本节及接口冻结区一起发送,不能只发卡标题。

1. **PM 只派通用 subagent**,不使用预设工程师角色。
2. 执行者不需要项目历史;本文是本役唯一实施任务书。历史设计有冲突时采用:
   - 用户最新 B1/B2 裁定;
   - `MINIMAL-ISR-SLICE.md`;
   - 本任务书冻结的线格式与接口。
3. **所有代码必须由 Alice review**。执行者自测通过不代表可以进入下一验收阶段。
4. Git 只在宿主 Git Bash 执行。严禁 `wsl.exe` 内执行 Git。
5. 文件直接编辑宿主路径 `C:\Prj\LLVM\MCS251\...`。
6. 构建和 QEMU 使用:
   ```bash
   wsl.exe -d Debian -e bash -c '...'
   ```
7. **构建前先 `pgrep -a ninja`,取得 PM 的构建树锁后才运行 `ninja -j8`。**(并行度 8 为用户 2026-09-09 批准,覆盖本文最初的 -j4。)
8. 不允许自行修改 CMake 配置、提高并行度、终止其他人的构建。
9. 不允许 `git commit`、push、reset、clean、stash 或整文件恢复。
10. 工作区原有未跟踪文件一律不碰,不纳入整理、格式化、测试输入或删除范围。
11. PM 为本卡批准的新文件与原有未跟踪文件是不同集合;新文件若已经存在且不是本卡创建,立即停下核对归属。
12. **遇到任何困难、规格不清、接口不符或测试不过,立即停止当前修改,报 PM 转 Alice;不许猜测设计意图、不许降低断言、不许偷偷扩围。**
13. 事件驱动汇报:
    - 完成立即报 PM;
    - 受阻立即报 PM;
    - 发现共享文件变化立即报 PM;
    - 不等待固定时间批量汇报。
14. 完成汇报必须包含:
    - 修改的绝对路径;
    - 实际构建命令、测试命令、退出码;
    - 完整失败输出或通过数量;
    - 未执行项;
    - 是否涉及共享文件交接;
    - 请求 Alice review。
15. 运行测试不得使用 `icount`。超时不是 PASS。模型不支持的能力明确记"模型不支持",不得改期望值制造 PASS。

## 0.1 范围与禁止扩围

本役交付:

- 槽 **0–51**;
- GNU 属性 `__attribute__((interrupt(N)))`;
- Keil 后缀 `void isr() interrupt N`;
- 专属 CC、IR 保活、对象身份与注册;
- 每层 37B 整数保存及独立 RETI;
- lld 唯一向量表;
- 新 IRQ CRT,BOOT=`0xFF0210`;
- 默认 IRQ fail-stop;
- C→ELF、模型保存窗口、分板真机验收。

明确不做:

- 槽 52–64 扩展;
- priority 属性或优先级管理;
- A 线、B 线、调用闭包安全检查;
- 静态参数槽的传递禁令;
- 全程序 SPX 安全检查;
- 栈上界、抢占图、嵌套容量认证;
- IP/IPH/IP2/IP3 初始化;
- 自动开放源或 EA;
- 自动清外设标志;
- 自动插 NOP(4);
- 自动保存外设、DMA、USB、FPU 状态;
- ISR 与浮点运行库交互;
- ELF ABI v2 或 `.mcs251.attributes`;
- 通用 AsmParser、archive 懒提取、GC、ICF、LTO;
- 普通函数 CC、静态参数 ABI 或普通 ERET 的重设计。

`double=32` 已在当前目标代码中实现,不能再写"尚未落实"。但 ISR 浮点交互仍不在本役。

---

# A. 接口冻结区

**本区是所有并行任务的共同契约。执行者不得改名字、编号、字节布局、槽分类或语义。修改只能由 PM 转 Alice 裁定。**

## A1. 已核对的现状

| 位置 | 当前事实 |
|---|---|
| `C:\Prj\LLVM\MCS251\clang\lib\Sema\SemaDeclAttr.cpp:6692–6702` | 已有 mcs251 MI0 硬错误 `err_mcs251_interrupt_not_implemented` |
| `C:\Prj\LLVM\MCS251\llvm\include\llvm\IR\CallingConv.h:298` | 最后具名编号 `CHERIoT_LibraryCall=127`,128 未登记 |
| `C:\Prj\LLVM\MCS251\llvm\lib\Target\MCS251\MCS251CallingConv.td:31–41` | 仅 `RetCC_MCS251` 和 `CC_MCS251` 两个参数/返回分配规则 |
| `C:\Prj\LLVM\MCS251\llvm\lib\Target\MCS251\MCS251ISelLowering.cpp:2262` | `LowerFormalArguments` 只接受 C/Fast |
| 同文件 `:2633–2683` | `LowerReturn` 最后生成 `MCS251ISD::ERET` |
| `C:\Prj\LLVM\MCS251\llvm\lib\Target\MCS251\MCS251FrameLowering.cpp:117` | 尾声断言只接受 ERET |
| `C:\Prj\LLVM\MCS251\lld\MCS251\LinkerCore.cpp:1250–1256` | `resolveSymbols→layout→errorUndefined→applyRelocations` |
| 同文件 `:423–424` | 当前拒绝 relocation 指向非 ALLOC 节,需要精确增加 ISR 例外 |
| `C:\Prj\LLVM\MCS251\validation\mcs251-elf\runtime\crt-selfstart.yaml` | 旧 ELF CRT 使用八槽 VECS,BOOT 由参数放在 FF0100 |
| `C:\Prj\LLVM\MCS251\validation\mcs251-demo-test\t4-probes\ISR-STUB-VERDICT.md` | 手工 11B 保存桩、ECALL 普通 LLVM 体、RETI 的受限先例 |

不能把 `MCS251CallingConv.td` 当全局 CC 编号表。不能把 t4 的 11B 桩当本役保存实现。

## A2. CC 与 IR

### A2.1 编号

冻结:

```cpp
llvm::CallingConv::MCS251_INTR = 128
```

LLVM 文本:

```llvm
mcs251_intrcc
```

Clang 新枚举:

```cpp
clang::CC_MCS251_INTR
```

**Clang 枚举值不设为 128。** 当前 `FunctionType::ExtInfo` 的 CC 掩码为 `0x3F`;Clang 使用自身枚举,`CGCall.cpp` 显式映射为 LLVM 128。

### A2.2 函数表示

兼容布局的完整最小例子:

```llvm
target triple = "mcs251-unknown-none"

@llvm.used = appending global [1 x ptr] [ptr @timer0], section "llvm.metadata"

define mcs251_intrcc void @timer0() #0 {
entry:
  ret void
}

attributes #0 = { noinline "mcs251-isr-vector"="1" }
```

冻结规则:

1. CC 与 `"mcs251-isr-vector"` **必须成对出现**。
2. 属性值是规范十进制:
   - `"0"` 合法;
   - `"1"` 合法;
   - `"01"`、`"+1"`、`"0x1"`、空串、负数均非法。
3. 类型必须为非变参 `void()`。
4. 定义具有 `noinline`。
5. 定义必须进入 `llvm.used`,不是仅 `llvm.compiler.used`。
6. 声明具有 CC 与槽属性,但不生成对象注册记录,也不要求保活根。
7. 一个定义一个槽;同模块不能重复注册。
8. 允许内部链接 ISR,但必须强制生成定义并保活。
9. 不允许 weak、linkonce、COMDAT、alias、ifunc ISR。
10. 普通 helper 保持其普通 CC。
11. 禁止任何普通 call/invoke/callbr 进入 ISR:
    - call 自身写 CC128 也拒绝;
    - 普通 CC call 指向 ISR 也拒绝。
12. 禁止 ISR 作为普通指针值逃逸,包括 constant expression、全局初始化、cast、ptrtoint。
13. 只有结构正确的 `llvm.used` 保活项可以引用 ISR;允许其为地址空间适配所必需的标准 cast,但不得接受任意其他引用链。
14. 不新增独立槽号 named metadata;槽号只来自上述函数属性。
15. 文本→bitcode→文本保留 CC、槽号、保活根和函数类型。输入 `cc 128` 时输出规范文本名 `mcs251_intrcc`。

对于程序地址空间 AS4,保活项保留函数真实地址空间;若 `llvm.used` 容器需要转换,只采用 LLVM 标准 used 构造方式。对象 v1 兼容门禁只能豁免已验证的此类保活项,不能扩大为"一切函数指针都可放行"。

### A2.3 前端支持形态

GNU 主入口:

```c
void timer0(void) __attribute__((interrupt(1)));
```

Keil 别名:

```c
void timer0() interrupt 1;
```

Keil 开关冻结为:

```text
-fmcs251-keil
```

- Driver 与 cc1 均接受;
- 仅 mcs251 C 方言有效;
- 非目标使用硬错误;
- 未开开关时不把普通标识符 `interrupt` 全局抢成关键字;
- 开启后由真实 token 和声明解析器处理;
- 数字、展开后数字宏、括号化 ICE 可用;
- 非括号形式只消费一个数值 token;
- 复杂表达式必须加括号;
- 后缀 `void f()` 仅对该 ISR 规范化为零参原型;
- 普通 C `void f()` 规则不变;
- `using`、`__using` 不删除、不宏消除,报错。

首次声明必须建立 ISR 身份。后续声明可省略属性并继承身份;显式重复属性必须同槽。普通首次声明之后补 ISR 属性,报错。

## A3. 对象协议

### A3.1 不变部分

保持:

```text
ELFCLASS32
ELFDATA2MSB
ET_REL
EM_MCS251 = 0x9999
e_flags = 0x00000001
```

保持原 `.note.mcs251.abi`:

- 大小 52B;
- namesz=7;
- descsz=32;
- type=1;
- owner=`MCS251\0`,补齐为 8B;
- descriptor 为八个大端 u32:
  ```text
  1, 1, 0, 2, 0x0000f3ff, 7, 0, 0
  ```

不修改:

`C:\Prj\LLVM\MCS251\lld\test\MCS251\input-reader.test`

其中 v2 对象拒绝测试继续有效。

### A3.2 新元数据节

冻结:

```text
section name: .mcs251.isr
type:         SHT_PROGBITS
flags:        0
alignment:    4
entry size:   0
```

每输入对象最多一个该节。节内为连续 24B 记录,无节级头、无尾部填充。非空且总大小必须为 24 的整数倍。

所有多字节整数均为 **big-endian**。

| 偏移 | 大小 | 字段 | 规则 |
|---:|---:|---|---|
| 0 | 2 | protocol_version | 首版为 1 |
| 2 | 2 | record_size | 必须为 24 |
| 4 | 1 | record_kind | 见下表 |
| 5 | 1 | entry_kind | 见下表 |
| 6 | 1 | hardware_profile | 0 或 1 |
| 7 | 1 | save_profile | 0 或 1 |
| 8 | 2 | vector_slot | 注册号,非槽记录为 FFFF |
| 10 | 2 | required_caps | 必须为 0001 |
| 12 | 4 | symbol_reference | 四字节零;通过专用 RELA 关联 |
| 16 | 4 | asset_profile | 编译 ISR 为 0;冻结 CRT 为 1 |
| 20 | 4 | reserved | 必须为零 |

协议字段放在记录开头。未来版本不得借当前 reserved 字段偷偷改变含义;旧 reader 对未知版本、未知能力位硬拒绝。

### A3.3 记录种类

| record_kind | 名称 | entry_kind | hardware | save | slot | asset |
|---:|---|---:|---:|---:|---|---:|
| 1 | ISR_ENTRY | 1:IRQ_RETI | 1:IRQ4 | 1:INT37 | 合法槽 | 0 |
| 2 | ISR_REGISTER | 1:IRQ_RETI | 1 | 1 | 同上 | 0 |
| 3 | IRQ_DEFAULT | 2:IRQ_STOP | 1 | 0 | FFFF | 1 |
| 4 | IRQ_RESET | 3:RESET | 0 | 0 | FFFF | 1 |

- 每个编译器 ISR 定义发射恰好一个 ENTRY 和一个 REGISTER。
- 两记录必须引用同一个精确函数符号、同一个槽。
- ENTRY 不能没有 REGISTER;REGISTER 不能没有 ENTRY。
- IRQ_DEFAULT 不能被用户 REGISTER 引用。
- IRQ_RESET 引用新 CRT 的 HOME reset 符号。
- DEFAULT 与 RESET 必须来自同一输入 CRT 对象且各一个。
- 普通对象不要求任何 ISR 元数据;不以缺少元数据拒绝普通 helper 或库。

### A3.4 专用符号关联

新增:

```text
R_MCS251_ISR_REF = 9
```

此编号在当前 relocation 表 0–8 后追加。

冻结语义:

- **零写入宽度**;
- 不计算函数地址;
- 不把地址写入四字节槽;
- 不进入普通 `applyRelocations`;
- 只表达记录关联的精确符号。

RELA 节:

```text
.rela.mcs251.isr
type = SHT_RELA
sh_link = 本对象 .symtab
sh_info = 本对象 .mcs251.isr
sh_entsize = 12
```

每条记录:

- 恰一个 relocation;
- `r_offset = record_offset + 12`;
- type=9;
- addend=0;
- 对应四字节仍必须为零。

符号限制:

- 必须为具名 `STT_FUNC`;
- 必须直接关联函数符号,不能折叠为 `STT_SECTION + addend`;
- 不能 `SHN_ABS`;
- 不能指向函数内部偏移;
- identity 记录必须引用本对象内定义;
- REGISTER 必须与同对象 ENTRY 精确配对;
- 目标必须位于可执行 PROGBITS 节,函数范围不得越界;
- weak/COMDAT/alias 不在首版支持集合。

`needsRelocateWithSymbol` 对 type9 返回 true。

**type9 出现在其他节一律拒绝;其他 relocation 出现在 `.mcs251.isr` 一律拒绝。**

示例:槽 1 ENTRY 的 24B 内容:

```text
0001 0018 01 01 01 01 0001 0001 00000000 00000000 00000000
```

相应 REGISTER 仅把 offset4 改为 `02`。

### A3.5 AsmPrinter 发射路径

在 `runOnMachineFunction`:

```cpp
SetupMachineFunction(MF);
emitParameterSlots(MF);
emitFunctionBody();
emitISRRecords(MF); // 新增;只对ISR定义、只在ELF对象输出
```

`emitISRRecords`:

1. 再验证函数 CC、槽、正常返回机器指令。
2. 保存当前 section。
3. 切换 `.mcs251.isr`。
4. 用 `emitIntValue` 发固定字段。
5. offset12 发四字节零。
6. 用 `emitRelocDirective` 发 type9 对真实 `getSymbol(&F)` 的关联。
7. 发后续字段。
8. 发 ENTRY、REGISTER 两条。
9. 恢复 section。

本树有 `emitRelocDirective` 接口,但 MCS251 `getFixupKind` 尚未支持该名称;T06 必须接通。

REL 对象 ISR 硬拒绝。汇编文本允许作为检查产物,但加入不可被历史 ASxxxx 当作生产资产接受的标记:

```text
.mcs251_isr_nonobject
```

不能输出具有普通 ABI 签名、却没有 ISR 注册协议的"可生产汇编"。

### A3.6 lld 校验顺序

```text
loadFile
  → validate ordinary ELF v1 identity
  → parse exact ISR metadata and RELA structure
resolveSymbols
  → validateISRIdentitiesAndRegistrations
  → synthesizeIRQVectors
layout
  → validateIRQReservedRangesAndCRT
  → checkFlashGate
errorUndefined
applyRelocations
  → validate final IRQ targets/assets
validateXInit
buildMap
```

`--print-input` 执行输入结构校验,不伪装成完整链接验收。

触发 IRQ 模式:

- 任一输入含 `.mcs251.isr` 即触发;
- 无新命令行 profile 字符串;
- 不按板名决定语义。

当前无 GC,保留所有代码即可。GC、ICF、LTO、archive 不新增支持;现有未知选项/输入拒绝不得被删除。

## A4. 槽拓扑完整数组

证据:

- `C:\Prj\LLVM\stc-int-img\p-0674.png`,PDF674/印刷634;
- `C:\Prj\LLVM\stc-int-img\p-0675.png`,PDF675/印刷635;
- `C:\Prj\LLVM\stc-int-img\p-0676.png`,PDF676/印刷636;
- `C:\Prj\LLVM\stc-int-img\pk-0147.png`,PDF147/印刷107。

上述四张图版已亲读。与情报稿一致。

以下数组冻结在:

`C:\Prj\LLVM\MCS251\llvm\include\llvm\BinaryFormat\MCS251ISR.h`

```cpp
enum class ISRSlotKind : uint8_t {
  Legal = 0,
  Reserved = 1,
  System = 2
};

struct ISRSlotDesc {
  ISRSlotKind Kind;
  uint16_t PDFPage;
};

static constexpr ISRSlotDesc ISRSlots[52] = {
    {ISRSlotKind::Legal,    674}, //  0 INT0
    {ISRSlotKind::Legal,    674}, //  1 Timer0
    {ISRSlotKind::Legal,    674}, //  2 INT1
    {ISRSlotKind::Legal,    674}, //  3 Timer1
    {ISRSlotKind::Legal,    674}, //  4 UART1
    {ISRSlotKind::Legal,    674}, //  5 ADC
    {ISRSlotKind::Legal,    674}, //  6 LVD
    {ISRSlotKind::Reserved, 674}, //  7 gap between 6 and 8
    {ISRSlotKind::Legal,    674}, //  8 UART2
    {ISRSlotKind::Legal,    674}, //  9 SPI
    {ISRSlotKind::Legal,    674}, // 10 INT2
    {ISRSlotKind::Legal,    674}, // 11 INT3
    {ISRSlotKind::Legal,    674}, // 12 Timer2
    {ISRSlotKind::Reserved, 147}, // 13 reserved transfer slot; also absent on 674
    {ISRSlotKind::System,   147}, // 14 system internal
    {ISRSlotKind::System,   147}, // 15 system internal
    {ISRSlotKind::Legal,    674}, // 16 INT4
    {ISRSlotKind::Legal,    674}, // 17 UART3
    {ISRSlotKind::Legal,    674}, // 18 UART4
    {ISRSlotKind::Legal,    674}, // 19 Timer3
    {ISRSlotKind::Legal,    674}, // 20 Timer4
    {ISRSlotKind::Legal,    674}, // 21 CMP
    {ISRSlotKind::Reserved, 674}, // 22 gap between 21 and 24
    {ISRSlotKind::Reserved, 674}, // 23 gap between 21 and 24
    {ISRSlotKind::Legal,    674}, // 24 I2C
    {ISRSlotKind::Legal,    674}, // 25 USB
    {ISRSlotKind::Legal,    674}, // 26 PWMA
    {ISRSlotKind::Legal,    674}, // 27 PWMB
    {ISRSlotKind::Legal,    675}, // 28 CANBUS
    {ISRSlotKind::Legal,    675}, // 29 CAN2BUS
    {ISRSlotKind::Legal,    675}, // 30 LINBUS
    {ISRSlotKind::Reserved, 675}, // 31 gap between 30 and 36
    {ISRSlotKind::Reserved, 675}, // 32 gap between 30 and 36
    {ISRSlotKind::Reserved, 675}, // 33 gap between 30 and 36
    {ISRSlotKind::Reserved, 675}, // 34 gap between 30 and 36
    {ISRSlotKind::Reserved, 675}, // 35 gap between 30 and 36
    {ISRSlotKind::Legal,    675}, // 36 RTC
    {ISRSlotKind::Legal,    676}, // 37 P0
    {ISRSlotKind::Legal,    676}, // 38 P1
    {ISRSlotKind::Legal,    676}, // 39 P2
    {ISRSlotKind::Legal,    676}, // 40 P3
    {ISRSlotKind::Legal,    676}, // 41 P4
    {ISRSlotKind::Legal,    676}, // 42 P5
    {ISRSlotKind::Legal,    676}, // 43 P6
    {ISRSlotKind::Legal,    676}, // 44 P7
    {ISRSlotKind::Reserved, 676}, // 45 gap between 44 and 47
    {ISRSlotKind::Reserved, 676}, // 46 gap between 44 and 47
    {ISRSlotKind::Legal,    676}, // 47 DMA_M2M
    {ISRSlotKind::Legal,    676}, // 48 DMA_ADC
    {ISRSlotKind::Legal,    676}, // 49 DMA_SPI
    {ISRSlotKind::Legal,    676}, // 50 DMA_UART1_TX
    {ISRSlotKind::Legal,    676}  // 51 DMA_UART1_RX
};

static_assert(sizeof(ISRSlots) / sizeof(ISRSlots[0]) == 52);
```

统计必须为:

```text
Legal=39
Reserved=11
System=2
```

13 不是通用分发入口;不能把旧 `interrupt 13` 自动映射到任意真实源。

板级源是否实际可用,与号码是否合法分开。12K128 端口边沿警告不扩展成 INT0/INT1 禁用。

## A5. 向量与默认入口

公式:

```cpp
constexpr uint32_t ISRVectorBase = 0xFF0003;
constexpr uint32_t ISRVectorStride = 8;
constexpr uint32_t ISRVectorCount = 52;
constexpr uint32_t ISRVectorEnd = 0xFF01A3;
```

- 完整范围 `[FF0003,FF01A3)` 为向量保留区。
- 每合法槽:
  - 前四字节 `8A aa bb cc`,EJMP;
  - 后四字节为无载荷占位,不写 NOP、不放函数。
- 已注册合法槽→对应 ISR;
- 未注册合法槽→独立默认入口;
- Reserved/System 整个 8B 无载荷,不生成 EJMP;
- 地址不能压缩;
- 无载荷仍参加重叠和 ROM 范围检查;
- 不允许利用 HEX 空洞把 BOOT 或普通代码塞进去。

默认入口冻结机器字节:

```text
C2 AF 80 FE
```

含义:

```asm
clr EA
halt:
sjmp halt
```

要求:

- `STT_FUNC`,size=4;
- 非 ALLOC metadata 中有 kind3/asset1;
- 函数范围内无 relocation;
- lld 校验精确机器码;
- 不调用 C;
- 不增加软件栈;
- 不返回;
- 不打印;
- 不插 NOP(4)。

不凭函数名或 `noreturn` 获得身份。对象协议不是对恶意伪造目标文件的密码学认证,但不能提供"改个名字就通过"的正常输入旁路。

## A6. 保存与返回

软件固定顺序:

```text
PSW
DR0
DR4
DR8
DR12
DR16
DR20
DR24
DR28
DR56/DPX
```

软件保存量:

```text
1 + 9×4 = 37B
```

恢复严格逆序,最后:

```text
pop PSW
RETI
```

硬件 profile1:

```text
4B;入栈顺序 PSW1、PC[23:16]、PC[7:0]、PC[15:8]
```

证据级别:当前 QEMU 源码事实及待 qualification 验证;**不得标为手册已验证的所有 251 通用帧**。

固定帧关系:

```text
受理前 SPX = S
硬件后       S+4
保存后       S+41
局部帧后     S+41+F
撤销局部帧   S+41
恢复后       S+4
RETI后       S
```

37B 不计入普通 `MachineFrameInfo::StackSize`,避免重复分配。局部对象沿现有向上栈 `ObjectOffset - StackSize` 访问;软件保存区位于其下方。

禁止:

- DR60/SPX push/pop;
- 保存前建立 DR16 锚;
- 普通 callee-save 机制再次保存同一 37B;
- 按需省略寄存器;
- shrink-wrapping;
- 尾跳 helper;
- 自动关/开 EA;
- 保存共享 bit 后回滚用户对 bit 的修改。

本役 ISR 本体只实现固定帧。动态 alloca/stackrestore 等本体未实现帧形态硬错误,这是后端能力拒绝,不是恢复 A/B 传递安全检查。普通 helper 的既有支持不因 ISR 调用而额外收紧。

---

# B. 构建与资源协议

## B1. s1 已核配置

`/home/liu/build-mcs251-s1/CMakeCache.txt`:

```text
CMAKE_BUILD_TYPE=Release
CMAKE_C_COMPILER=/usr/bin/cc
CMAKE_CXX_COMPILER=/usr/bin/c++
LLVM_ENABLE_ASSERTIONS=OFF
LLVM_ENABLE_RTTI=OFF
LLVM_ENABLE_PROJECTS=clang
LLVM_TARGETS_TO_BUILD=
LLVM_EXPERIMENTAL_TARGETS_TO_BUILD=MCS251
LLVM_INCLUDE_TESTS=ON
LLVM_BUILD_TOOLS=ON
LLVM_ENABLE_LLD=OFF
LLVM_ENABLE_RUNTIMES=
```

源码:

```text
/mnt/c/Prj/LLVM/MCS251/llvm
```

**s1 没有集成 lld 项目。**

## B2. T00:PM 构建资源准备

这是管理前置,不派模型修改产品代码。

准备:

| 树 | 用途 |
|---|---|
| `/home/liu/build-mcs251-s1` | T01;最终集成 T09 |
| `/home/liu/build-mcs251-isr-fe` | T02/T03 |
| `/home/liu/build-mcs251-isr-be` | T04/T05/T06 |
| `/home/liu/build-mcs251-isr-link` | lld 配套 LLVM 核心工具与库 |
| `/home/liu/build-mcs251-isr-lld` | standalone lld |
| `/home/liu/mcs251-isr-qualification` | T10 输出,不是源码目录 |

新核心树必须复制 s1 的**完整有效 CMake 配置**,而不是只抄上面摘要。PM 从 cache 导出非 INTERNAL 配置项,排除原构建目录派生路径,重新配置;对规范化 cache 做比较。

禁止直接复制 `CMakeCache.txt` 后运行 Ninja。

standalone lld:

```bash
wsl.exe -d Debian -e bash -c '
set -eu
pgrep -a ninja || true
cmake -G Ninja \
  -S /mnt/c/Prj/LLVM/MCS251/lld \
  -B /home/liu/build-mcs251-isr-lld \
  -DLLVM_DIR=/home/liu/build-mcs251-isr-link/lib/cmake/llvm \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_ASSERTIONS=OFF \
  -DLLVM_ENABLE_RTTI=OFF \
  -DLLVM_INCLUDE_TESTS=ON
'
```

若 standalone 配置报告缺 `llvm_gtest`,停报 PM;不能自行关测试。PM 负责准备匹配 LLVM 构建导出的测试依赖。

## B3. 公共命令

每卡将 `TREE` 换为其指定绝对路径:

```bash
wsl.exe -d Debian -e bash -c '
set -eu
pgrep -a ninja || true
ninja -C TREE -j8 clang llc opt llvm-as llvm-dis llvm-readobj \
  llvm-objcopy yaml2obj FileCheck not count split-file
'
```

> **T08 实测补充(2026-09-09)**:standalone lld 链接前,isr-link 需预建 `llvm-config LLVMDTLTO LLVMLibDriver LLVMWindowsDriver LLVMWindowsManifest LLVMMCS251CodeGen LLVMMCS251Desc LLVMMCS251Info`(lit 基建另要求 `count`)。后续卡首次建树直接带上,避免窗口内撞缺目标。

LLVM lit:

```bash
wsl.exe -d Debian -e bash -c '
set -eu
TREE/bin/llvm-lit -sv \
  TREE/test/CodeGen/MCS251 \
  TREE/test/Assembler/mcs251-isr-cc.ll
'
```

Clang lit 用构建目录:

```text
TREE/tools/clang/test/Sema/...
TREE/tools/clang/test/CodeGen/...
TREE/tools/clang/test/Parser/...
```

lld:

```bash
wsl.exe -d Debian -e bash -c '
set -eu
pgrep -a ninja || true
ninja -C /home/liu/build-mcs251-isr-lld -j8 lld
/home/liu/build-mcs251-isr-link/bin/llvm-lit -sv \
  /home/liu/build-mcs251-isr-lld/test/MCS251
'
```

## B4. 并行源码屏障

独立 build 目录**不等于**独立源码快照。

本役共享一份源码时:

1. 并行编辑仅允许文件集不相交;
2. PM 宣布"本轮编辑结束";
3. 全员停止编辑;
4. 在各自 build 树并行构建;
5. 全部构建结束后 PM 才允许下一轮编辑。

不能一边有人改公共头,一边另一个树构建旧依赖集合。

---

# C. 任务卡

## T01 — MI1:全局 CC、IR 结构契约、共享协议头

**工作量:L。构建树:s1。纪律:D0。**

### 前置

- T00 完成;
- A 区冻结;
- 不依赖前端或 RETI 已实现。

### 文件独占权

唯一属主 T01:

- `C:\Prj\LLVM\MCS251\llvm\include\llvm\IR\CallingConv.h`
- `C:\Prj\LLVM\MCS251\llvm\include\llvm\AsmParser\LLToken.h`
- `C:\Prj\LLVM\MCS251\llvm\lib\AsmParser\LLLexer.cpp`
- `C:\Prj\LLVM\MCS251\llvm\lib\AsmParser\LLParser.cpp`
- `C:\Prj\LLVM\MCS251\llvm\lib\IR\AsmWriter.cpp`
- `C:\Prj\LLVM\MCS251\llvm\lib\IR\Verifier.cpp`
- 新建 `C:\Prj\LLVM\MCS251\llvm\include\llvm\BinaryFormat\MCS251ISR.h`
- `C:\Prj\LLVM\MCS251\llvm\include\llvm\BinaryFormat\ELFRelocs\MCS251.def`
- 新建 `C:\Prj\LLVM\MCS251\llvm\test\Assembler\mcs251-isr-cc.ll`
- 新建 `C:\Prj\LLVM\MCS251\llvm\test\Verifier\mcs251-isr-invalid.ll`

其他任务只读这些文件。

### 实施步骤

1. 在全局表追加 128;不改已有编号。
2. 增加文本 token、lexer keyword、parser case、AsmWriter case。
3. 不改 bitcode schema;现有 Function/CallBase CC 通道承载 128。
4. 在共享头放 A3/A4 常量与槽数组;提供:
   ```cpp
   inline constexpr bool isLegalISRSlot(unsigned N);
   ```
5. 追加 relocation 编号9。
6. 在 Verifier 增加仅针对 CC128/槽属性的结构验证:
   - 类型、规范槽字符串、配对;
   - 合法链接类型;
   - 同模块重复;
   - 定义 noinline/used;
   - 不允许普通调用和普通值用途。
7. 用迭代 visited 集遍历用途,避免 constant graph 重复访问。
8. 允许合法 `llvm.used` 根,不要求对象专用 metadata 出现在 IR。
9. 普通函数无新限制。

### 完整测试一

`C:\Prj\LLVM\MCS251\llvm\test\Assembler\mcs251-isr-cc.ll`

```llvm
; RUN: llvm-as %s -o %t.bc
; RUN: llvm-dis %t.bc -o %t.ll
; RUN: FileCheck %s < %t.ll
; RUN: llvm-as %t.ll -o %t.2.bc
; RUN: llvm-dis %t.2.bc -o - | FileCheck %s
; RUN: opt -passes='globaldce,verify' -S %t.bc -o - | FileCheck %s

target triple = "mcs251-unknown-none"

@llvm.used = appending global [1 x ptr] [ptr @irq], section "llvm.metadata"

define internal cc 128 void @irq() #0 {
  ret void
}

define void @ordinary() {
  ret void
}

attributes #0 = { noinline "mcs251-isr-vector"="51" }

; CHECK: @llvm.used = appending global [1 x ptr] [ptr @irq]
; CHECK: define internal mcs251_intrcc void @irq()
; CHECK: define void @ordinary()
; CHECK: attributes #0 = { noinline "mcs251-isr-vector"="51" }
```

### 完整测试二

`C:\Prj\LLVM\MCS251\llvm\test\Verifier\mcs251-isr-invalid.ll`

```llvm
; RUN: split-file %s %t
; RUN: not llvm-as %t/missing.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=MISSING
; RUN: not llvm-as %t/reserved.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=SLOT
; RUN: not llvm-as %t/call.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=CALL
; RUN: not llvm-as %t/address.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=USE

; MISSING: MCS251 ISR: calling convention and vector attribute must appear together
; SLOT: MCS251 ISR: vector is not a legal slot in profile 0-51
; CALL: MCS251 ISR: interrupt entry may not be called
; USE: MCS251 ISR: interrupt entry has a non-registration use

;--- missing.ll
declare cc 128 void @irq()

;--- reserved.ll
declare cc 128 void @irq() "mcs251-isr-vector"="7"

;--- call.ll
declare cc 128 void @irq() "mcs251-isr-vector"="1"
define void @caller() {
  call cc 128 void @irq()
  ret void
}

;--- address.ll
@escaped = global ptr @irq
declare cc 128 void @irq() "mcs251-isr-vector"="1"
```

### 自验与完成定义

- B3 s1 构建;
- 上述两个文件 lit 通过;
- 用 `rg` 确认128只新增一次全局登记;
- 旧 calling-convention 测试不回归;
- Alice review确认用途例外没有放宽到任意全局指针。

### 不许做

不许新增 v2 note;不许给普通函数加 noinline;不许把 invalid IR 自动修正为普通 CC;不许改优化器通用策略来让一个测试通过。

---

## T02 — MI2:GNU 属性、AST CC、重声明、使用限制、IR 保活

**工作量:L。构建树:isr-fe。纪律:D0。**

### 前置

- T01 review通过;
- T03 只能在本卡公共 Clang 文件交接后写入;
- 可与 T04/T07/T08 并行编辑。

### 文件独占权

- `C:\Prj\LLVM\MCS251\clang\include\clang\Basic\Attr.td`
- `C:\Prj\LLVM\MCS251\clang\include\clang\Basic\AttrDocs.td`
- `C:\Prj\LLVM\MCS251\clang\include\clang\Basic\Specifiers.h`
- `C:\Prj\LLVM\MCS251\clang\include\clang\Basic\DiagnosticSemaKinds.td`
- `C:\Prj\LLVM\MCS251\clang\lib\AST\Type.cpp`
- `C:\Prj\LLVM\MCS251\clang\lib\Basic\Targets\MCS251.h`
- `C:\Prj\LLVM\MCS251\clang\lib\Sema\SemaDeclAttr.cpp`
- `C:\Prj\LLVM\MCS251\clang\lib\Sema\SemaDecl.cpp`
- `C:\Prj\LLVM\MCS251\clang\lib\Sema\SemaExpr.cpp`
- `C:\Prj\LLVM\MCS251\clang\lib\Sema\SemaType.cpp`
- `C:\Prj\LLVM\MCS251\clang\lib\CodeGen\CGCall.cpp`
- `C:\Prj\LLVM\MCS251\clang\lib\CodeGen\CodeGenModule.cpp`
- 新建 `C:\Prj\LLVM\MCS251\clang\test\Sema\mcs251-isr.c`
- 新建 `C:\Prj\LLVM\MCS251\clang\test\CodeGen\mcs251-isr.c`

如编译报其他 CC exhaustive switch,立即报 PM 扩充归属,不自行修改其他人的文件。

### 实施步骤

1. `Attr.td` 增 `MCS251Interrupt`:
   - `TargetMCS251`;
   - GNU interrupt spelling;
   - `ParseKind="Interrupt"`;
   - 一个 unsigned 参数;
   - 函数 subject;
   - 属性合并不采用"后声明覆盖前声明"。
2. 在 `SemaDeclAttr.cpp` 加 `handleMCS251InterruptAttr`。
3. 对 ICE 使用完整 `APSInt`:
   - 先检查负数;
   - 再检查范围;
   - 再安全转换;
   - 不先 `getLimitedValue()` 或截断到8位。
4. 检查 `void(void)`、零参、非变参。
5. GNU 旧式无原型声明不自动获得 Keil 特例。
6. 设置函数类型 ExtInfo 的 `CC_MCS251_INTR`,保留原返回类型、地址空间与其他 ExtInfo。
7. 第一次普通声明后补 ISR 报错;省略属性的重声明继承。
8. 同 TU 查重只针对定义,不把两个不同 ISR 声明先误报成两个注册。
9. 拒绝 naked/always_inline/weak/alias/ifunc/COMDAT 及显式不兼容 CC。
10. `BuildDeclRefExpr`/调用检查处拒绝普通源代码对 ISR 的使用;不影响纯声明、属性处理、CodeGen 内部保活。
11. `CGCall.cpp` 映射 AST CC→LLVM128。
12. `CodeGenModule.cpp` 对声明和定义加槽属性;对定义加 noinline、used。
13. 内部未引用定义也必须进入 deferred emission/used 路径,不能等机器码阶段补根。
14. 保留 MI0 诊断定义。未支持的拼装形态仍硬拒绝,不能重新落 ARM。
15. 前端全功能公开验收必须等待 T05/T06;本卡完成不代表生产对象已可用。

### 完整 Sema 测试

```c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fsyntax-only -verify %s

#define VEC (24)
void good(void) __attribute__((interrupt(VEC)));
void good(void) {}

void neg(void) __attribute__((interrupt(-1))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-51}}
void gap(void) __attribute__((interrupt(7))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-51}}
void transfer(void) __attribute__((interrupt(13))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-51}}
void system14(void) __attribute__((interrupt(14))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-51}}
void system15(void) __attribute__((interrupt(15))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-51}}
void high(void) __attribute__((interrupt(52))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-51}}
void wide(void) __attribute__((interrupt(0x100000001ULL))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-51}}

int runtime_slot;
void nonice(void) __attribute__((interrupt(runtime_slot))); // expected-error {{integer constant expression}}
void zero(void) __attribute__((interrupt)); // expected-error {{takes one argument}}
void two(void) __attribute__((interrupt(1, 2))); // expected-error {{takes one argument}}

int badret(void) __attribute__((interrupt(1))); // expected-error {{MCS251 interrupt function must have type void(void)}}
void arg(int x) __attribute__((interrupt(2))); // expected-error {{MCS251 interrupt function must have type void(void)}}
void variadic(int x, ...) __attribute__((interrupt(3))); // expected-error {{MCS251 interrupt function must have type void(void)}}

void late(void);
void late(void) __attribute__((interrupt(4))); // expected-error {{MCS251 interrupt identity must be established on the first declaration}}

void mismatch(void) __attribute__((interrupt(5)));
void mismatch(void) __attribute__((interrupt(6))); // expected-error {{conflicting MCS251 interrupt vector}}

void duplicate_a(void) __attribute__((interrupt(8)));
void duplicate_a(void) {}
void duplicate_b(void) __attribute__((interrupt(8)));
void duplicate_b(void) {} // expected-error {{duplicate MCS251 interrupt vector}}

void naked_isr(void) __attribute__((interrupt(9), naked)); // expected-error {{incompatible with MCS251 interrupt}}
void inline_isr(void) __attribute__((interrupt(10), always_inline)); // expected-error {{incompatible with MCS251 interrupt}}

void use(void) {
  good(); // expected-error {{MCS251 interrupt entry cannot be used as an ordinary function}}
  void (*p)(void) = good; // expected-error {{MCS251 interrupt entry cannot be used as an ordinary function}}
  (void)&good; // expected-error {{MCS251 interrupt entry cannot be used as an ordinary function}}
  (void)(unsigned long)&good; // expected-error {{MCS251 interrupt entry cannot be used as an ordinary function}}
}
```

这里冻结的是诊断主体。若 TableGen 的通用参数数量诊断文案不同,执行者报 PM,由 Alice 校准测试,不得删除数量测试。

### 完整 CodeGen 测试

```c
// RUN: %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,1,32,8,1 -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s

extern void helper(void);
static void irq(void) __attribute__((interrupt(1)));
static void irq(void) { helper(); }
void ordinary(void) { helper(); }

// CHECK: @llvm.used = appending global
// CHECK-SAME: @irq
// CHECK-DAG: define internal mcs251_intrcc void @irq()
// CHECK-DAG: define{{.*}} void @ordinary()
// CHECK: "mcs251-isr-vector"="1"
// CHECK-NOT: "interrupt"=
```

### 自验与完成定义

- B3 指定树构建;
- 两测试通过;
- AST dump 显示 `MCS251InterruptAttr`,不显示 `ARMInterruptAttr`;
- PCH/AST 往返仍保留属性与函数 CC;
- O0/Os 都保留内部 ISR;
- 普通 helper CC 不变;
- Alice review后移交 T03。

### 不许做

不许把 GNU 属性实现成纯字符串标记;不许只设 `noinline` 不保活;不许仅禁止 `&isr` 却放行函数衰变;不许检查 helper 重入安全或禁止跨 TU helper。

---

## T03 — MI2:Keil token 与函数声明解析

**工作量:M。构建树:isr-fe,与T02串行占树。纪律:D0。**

### 前置

- T02接口与诊断可用;
- 可以提前阅读、准备测试,不得抢写T02文件。

### 文件独占权

- `C:\Prj\LLVM\MCS251\clang\include\clang\Basic\LangOptions.def`
- `C:\Prj\LLVM\MCS251\clang\include\clang\Basic\TokenKinds.def`
- `C:\Prj\LLVM\MCS251\clang\include\clang\Options\Options.td`
- `C:\Prj\LLVM\MCS251\clang\lib\Basic\IdentifierTable.cpp`
- `C:\Prj\LLVM\MCS251\clang\include\clang\Parse\Parser.h`
- `C:\Prj\LLVM\MCS251\clang\lib\Parse\ParseDecl.cpp`
- `C:\Prj\LLVM\MCS251\clang\lib\Frontend\CompilerInvocation.cpp`
- `C:\Prj\LLVM\MCS251\clang\lib\Driver\ToolChains\Clang.cpp`
- 新建 `C:\Prj\LLVM\MCS251\clang\test\Parser\mcs251-keil-interrupt.c`
- 新建 `C:\Prj\LLVM\MCS251\clang\test\CodeGen\mcs251-keil-interrupt.c`

T02 的 Sema/Attr 文件只读;解析结果归约到已有 MCS251Interrupt 属性。

### 实施步骤

1. 新 LangOpt `MCS251Keil`,默认关闭。
2. Options.td 加 Driver/cc1 开关和 LangOpt marshalling。
3. token 名冻结为 `kw___mcs251_interrupt`,实际受控拼写 `interrupt`。
4. `IdentifierTable::AddKeywords` 在开关打开时登记该拼写,不改变其他目标。
5. 在 `ParseDirectDeclarator` 完成函数后缀的位置消费 token。
6. 检查 Declarator 最外层确为函数,不允许变量或函数指针变量后缀。
7. 数字形式消费一个 token;`(` 形式用现有常量表达式解析并要求闭括号。
8. 构造同 ParseKind 的 ParsedAttr,不做源码字符串重写。
9. 仅对后缀 ISR 的空参列表设置零参原型。
10. `using`、`__using` 留给明确错误路径,不能跳过。
11. 错误恢复不能吞掉下一函数定义。

### 完整 Parser 测试

```c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fsyntax-only -verify %s

#define SLOT 36
void a() interrupt 0 {}
void b() interrupt SLOT {}
void c() interrupt (48 + 1) {}
void ordinary();

int value interrupt 1; // expected-error {{interrupt suffix requires a function declarator}}
void bad() interrupt 7 {} // expected-error {{MCS251 interrupt vector must be a legal slot in 0-51}}
void oldarg(x) interrupt 1 int x; {} // expected-error {{MCS251 interrupt function must have type void(void)}}
void using_bad() interrupt 2 using 1 {} // expected-error {{using is not supported for MCS251 interrupt functions}}
void dunder_using_bad() interrupt 3 __using(1) {} // expected-error {{using is not supported for MCS251 interrupt functions}}
```

### 完整 CodeGen 测试

```c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -mcs251-memory-contract=1,1,32,8,1 -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s

#define SLOT 51
void keil() interrupt SLOT {}
void gnu(void) __attribute__((interrupt(50)));
void gnu(void) {}
void plain() {}

// CHECK-DAG: define{{.*}} mcs251_intrcc void @keil()
// CHECK-DAG: define{{.*}} mcs251_intrcc void @gnu()
// CHECK-DAG: define{{.*}} void @plain()
// CHECK-DAG: "mcs251-isr-vector"="51"
// CHECK-DAG: "mcs251-isr-vector"="50"
```

### 自验与完成定义

- B3 构建和两个测试;
- Driver 的 `-### -fmcs251-keil` 可看到传入 cc1;
- 关闭开关时 `int interrupt;` 仍可作为普通标识符;
- 开关不改变普通函数原型;
- 两拼写 AST属性和IR语义一致。

### 不许做

不许宏兼容;不许预处理扫描源码;不许支持 SDCC `__interrupt`;不许顺带做官方头、SFR全集、`using` bank切换。

---

## T04 — MI3:RETI 与固定保存原生指令

**工作量:M。构建树:isr-be。纪律:D0。**

### 前置

T01通过。可与T02/T07/T08并行编辑。

### 文件独占权

- `C:\Prj\LLVM\MCS251\llvm\lib\Target\MCS251\MCS251ISelLowering.h`
- `C:\Prj\LLVM\MCS251\llvm\lib\Target\MCS251\MCS251InstrInfo.td`
- `C:\Prj\LLVM\MCS251\llvm\lib\Target\MCS251\MCS251RegisterInfo.td`
- `C:\Prj\LLVM\MCS251\llvm\lib\Target\MCS251\MCTargetDesc\MCS251MCCodeEmitter.cpp`
- 新建 `C:\Prj\LLVM\MCS251\llvm\test\CodeGen\MCS251\isr-instructions.mir`

T05只读本卡的 `.td` 和 lowering头。

### 实施步骤

1. 新增独立 `MCS251ISD::RETI`、SDNode、机器 RETI。
2. RETI:
   - Size=1;
   - isReturn/isTerminator/isBarrier;
   - hasSideEffects;
   - mayLoad;
   - Uses/Defs表达SPX;
   - Defs表达恢复后的状态寄存器;
   - 注释明确PSW1和in-service副作用。
3. 新建无临时寄存器的固定操作码:
   ```text
   ISR_PUSH_PSW / ISR_POP_PSW
   ISR_PUSH_DR0 / ISR_POP_DR0
   ISR_PUSH_DR4 / ISR_POP_DR4
   ISR_PUSH_DR8 / ISR_POP_DR8
   ISR_PUSH_DR12 / ISR_POP_DR12
   ISR_PUSH_DR16 / ISR_POP_DR16
   ISR_PUSH_DR24 / ISR_POP_DR24
   ISR_PUSH_DR28 / ISR_POP_DR28
   ISR_PUSH_DPX / ISR_POP_DPX
   ```
4. 每条push读SPX、写SPX、写栈;pop读栈、读写SPX、定义恢复寄存器。
5. DR8对应A/B隐式Uses/Defs;DPX对应DPL/DPH/DPTR隐式Uses/Defs。
6. 不重构全局寄存器分配集合;可以采用准确的指令隐式效果作为本役等价别名建模。
7. 机器码:
   - RETI=`32`;
   - push PSW=`C0 D0`;
   - pop PSW=`D0 D0`;
   - push DR=`CA ((code<<4)|0B)`;
   - pop DR=`DA ((code<<4)|0B)`;
   - DR0..DR28 code=0..7;
   - DR56 code=14,使用现有 `regCode` 规则;
   - 不产生 A5 错误前缀。
8. 使用现有 `putOpcode` native标记规则,不直接把所有opcode值照抄为普通Binary opcode。
9. 不新增合并保存pass。

### 完整 MIR 测试

```yaml
# RUN: llc -mtriple=mcs251 -start-after=prolog-epilog -filetype=obj -mcs251-object-format=elf %s -o %t.o
# RUN: llvm-objcopy --dump-section=.text=%t.bin %t.o
# RUN: %python -c "import pathlib,sys; b=pathlib.Path(sys.argv[1]).read_bytes(); assert b.hex() == 'c0d0ca0bca1bca2bca3bca4bca5bca6bca7bcaebdaebda7bda6bda5bda4bda3bda2bda1bda0bd0d032', b.hex()" %t.bin

--- |
  target triple = "mcs251-unknown-none"
  @llvm.used = appending global [1 x ptr] [ptr @irq], section "llvm.metadata"
  define mcs251_intrcc void @irq() #0 { ret void }
  attributes #0 = { noinline "mcs251-isr-vector"="1" }
...
---
name: irq
tracksRegLiveness: false
body: |
  bb.0:
    ISR_PUSH_PSW
    ISR_PUSH_DR0
    ISR_PUSH_DR4
    ISR_PUSH_DR8
    ISR_PUSH_DR12
    ISR_PUSH_DR16
    ISR_PUSH_DR20
    ISR_PUSH_DR24
    ISR_PUSH_DR28
    ISR_PUSH_DPX
    ISR_POP_DPX
    ISR_POP_DR28
    ISR_POP_DR24
    ISR_POP_DR20
    ISR_POP_DR16
    ISR_POP_DR12
    ISR_POP_DR8
    ISR_POP_DR4
    ISR_POP_DR0
    ISR_POP_PSW
    RETI
...
```

### 自验与完成定义

- 精确41B机器码通过;
- TableGen效果与Size正确;
- 加 `-verify-machineinstrs` 的真实T05管线随后通过;
- Alice审查push未先破坏待保存值。

### 不许做

不许把ERET打印成RETI;不许复制M68k保存集;不许新造DR32–DR52;不许push SPX;不许以寄存器"保留不可分配"为理由忽略别名副作用。

---

## T05 — MI3:LowerReturn、37B帧、正常出口

**工作量:L。构建树:isr-be,T04后串行占树。纪律:D0。**

### 文件独占权

- `C:\Prj\LLVM\MCS251\llvm\lib\Target\MCS251\MCS251ISelLowering.cpp`
- `C:\Prj\LLVM\MCS251\llvm\lib\Target\MCS251\MCS251FrameLowering.cpp`
- `C:\Prj\LLVM\MCS251\llvm\lib\Target\MCS251\MCS251FrameLowering.h`
- 新建 `C:\Prj\LLVM\MCS251\llvm\test\CodeGen\MCS251\isr-frame.ll`

### 实施步骤

1. `getTargetNodeName`增加RETI。
2. `LowerFormalArguments`接受CC128,但仅零参,不进入普通参数分配。
3. `CanLowerReturn`对CC128要求void且无输出值。
4. `LowerReturn`入口分流:
   ```cpp
   if (CallConv == CallingConv::MCS251_INTR) {
     // Validate void/non-vararg/no output operands.
     return DAG.getNode(MCS251ISD::RETI, DL, MVT::Other, Chain);
   }
   // Existing ordinary-return path unchanged.
   ```
5. `LowerCall`拒绝任何CC128调用和已知ISR被调目标。
6. 从ISR调用普通helper仍走原C/Fast普通路径;显式禁止尾调用。
7. `emitPrologue`在任何SPAdjust/FP动作前发A6保存序列。
8. `emitEpilogue`:
   - ISR要求RETI;
   - 普通要求ERET;
   - ISR先撤销局部帧再逆序恢复;
   - popPSW紧邻RETI。
9. ISR固定帧不调用PUSHFP/POPFP,不把37重复加进StackSize。
10. 所有返回块都处理;不只最后一个块。
11. 本体动态帧硬错误,普通函数动态帧现状不改。
12. FrameSetup/FrameDestroy和栈MMO正确;异步现场的push输入不能误标undef。

### 完整测试

```llvm
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs %s -o - | FileCheck %s

target triple = "mcs251-unknown-none"

@llvm.used = appending global [1 x ptr] [ptr @irq], section "llvm.metadata"
declare void @helper(ptr)

define mcs251_intrcc void @irq() #0 {
entry:
  %a = alloca [12 x i8], align 1
  call void @helper(ptr %a)
  ret void
}

define void @ordinary() {
  ret void
}

attributes #0 = { noinline "mcs251-isr-vector"="1" }

; CHECK-LABEL: _irq:
; CHECK: push psw
; CHECK-NEXT: push dr0
; CHECK-NEXT: push dr4
; CHECK-NEXT: push dr8
; CHECK-NEXT: push dr12
; CHECK-NEXT: push dr16
; CHECK-NEXT: push dr20
; CHECK-NEXT: push dr24
; CHECK-NEXT: push dr28
; CHECK-NEXT: push dpx
; CHECK: inc spx
; CHECK: ecall _helper
; CHECK: dec spx
; CHECK: pop dpx
; CHECK-NEXT: pop dr28
; CHECK-NEXT: pop dr24
; CHECK-NEXT: pop dr20
; CHECK-NEXT: pop dr16
; CHECK-NEXT: pop dr12
; CHECK-NEXT: pop dr8
; CHECK-NEXT: pop dr4
; CHECK-NEXT: pop dr0
; CHECK-NEXT: pop psw
; CHECK-NEXT: reti
; CHECK-LABEL: _ordinary:
; CHECK: eret
```

T09补充多出口、spill和优化矩阵;本卡不可用单一空ISR结束验收。

### 自验与完成定义

- B3构建;
- T04、T05测试通过;
- 普通MCS251 CodeGen目录不回归;
- 逐条标出SPX增减,37B单独核账;
- Alice review所有返回路径。

### 不许做

不许尾跳;不许仅改AsmPrinter;不许全程序动态栈检查;不许禁止跨TU helper或静态参数槽helper。

---

## T06 — MI1/MI3:对象记录发射与最终机器边界校验

**工作量:M。构建树:isr-be。纪律:D0。**

### 前置

- T01可开始编码;
- 正式自验依赖T04/T05;
- 与T07按A3并行。

### 文件独占权

- `C:\Prj\LLVM\MCS251\llvm\lib\Target\MCS251\MCS251AsmPrinter.cpp`
- `C:\Prj\LLVM\MCS251\llvm\lib\Target\MCS251\MCTargetDesc\MCS251AsmBackend.cpp`
- `C:\Prj\LLVM\MCS251\llvm\lib\Target\MCS251\MCTargetDesc\MCS251ELFObjectWriter.cpp`
- `C:\Prj\LLVM\MCS251\llvm\lib\CodeGen\MCS251ContractVerifier.cpp`
- 新建 `C:\Prj\LLVM\MCS251\llvm\test\CodeGen\MCS251\isr-object.ll`

### 实施步骤

1. 目标contract verifier即使关闭通用verify仍执行A2检查;复用结构逻辑,不增加安全闭包。
2. AsmPrinter实现A3.5。
3. `.mcs251.isr`对齐4,字段大端。
4. `getFixupKind("R_MCS251_ISR_REF")`支持literal relocation9。
5. literal relocation的info宽度0;不得访问24B记录外字节。
6. ELF writer对type9强制保留符号。
7. REL writer路径不新增type9支持,而是在ISR对象输出前硬拒绝。
8. 检查源码布局v2产生的合法`llvm.used`根,精确豁免;普通AS4指针payload仍按原门禁拒绝。
9. 最终机器函数:
   - ISR返回opcode只能RETI;
   - 普通返回不得RETI;
   - 未展开的本目标ISR伪指令硬错误。
10. 不照搬无条件`MI->isPseudo()`断言去炸掉允许的LLVM通用伪指令;只校验发射阶段不该残留的目标伪指令。

### 完整测试

```llvm
; RUN: llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %s -o %t.o
; RUN: llvm-readobj --file-headers --sections --symbols --relocations %t.o | FileCheck %s
; RUN: llvm-objcopy --dump-section=.mcs251.isr=%t.meta %t.o
; RUN: %python -c "import pathlib,struct,sys; b=pathlib.Path(sys.argv[1]).read_bytes(); assert len(b)==48; a=[struct.unpack('>HHBBBBHHIII',b[i:i+24]) for i in (0,24)]; assert a==[(1,24,1,1,1,1,1,1,0,0,0),(1,24,2,1,1,1,1,1,0,0,0)],a" %t.meta
; RUN: not --crash llc -mtriple=mcs251 -filetype=obj %s -o %t.rel 2>&1 | FileCheck %s --check-prefix=REL

target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr @irq], section "llvm.metadata"
define internal mcs251_intrcc void @irq() #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="1" }

; CHECK: EF_MCS251_ABI_V1
; CHECK: Name: .mcs251.isr
; CHECK: Type: SHT_PROGBITS
; CHECK: AddressAlignment: 4
; CHECK: R_MCS251_ISR_REF _irq 0x0
; CHECK: R_MCS251_ISR_REF _irq 0x0
; REL: MCS251 ISR requires ELF object output
```

### 自验与完成定义

- 本地/internal ISR仍是type9引用真实函数符号,不是section;
- metadata不ALLOC、无地址payload;
- 原ABI note逐字节不变;
- 两次编译得到相同记录;
- 普通ELF/REL回归通过;
- Alice审查literal relocation记录而不写入的路径。

### 不许做

不许借`R_MCS251_24`假装身份;不许在`.mcs251.attributes`放relocation;不许REL静默丢记录;不许给任意noreturn函数发DEFAULT。

---

## T07 — MI4:lld非ALLOC解析、唯一向量、冲突与身份

**工作量:L。构建树:isr-link+isr-lld。纪律:D0。**

### 前置

- T01冻结头可用;
- T06可并行开发;
- CRT正例验收依赖T08。
- **LinkerCore.cpp不拆给两个并行执行者。**

### 文件独占权

- `C:\Prj\LLVM\MCS251\lld\MCS251\LinkerCore.cpp`
- `C:\Prj\LLVM\MCS251\lld\MCS251\LinkerCore.h`
- 新建 `C:\Prj\LLVM\MCS251\lld\test\MCS251\isr-vectors.test`
- 新建 `C:\Prj\LLVM\MCS251\lld\test\MCS251\Inputs\isr-fixture.py`
- 新建 `C:\Prj\LLVM\MCS251\lld\test\MCS251\Inputs\isr-check-image.py`

`Driver.cpp`只读,现有参数足够。

### 实施步骤

1. `validateMetaSection`增加精确白名单,不使用`.mcs251.*`通配放行。
2. `loadFile`非ALLOC relocation特例仅针对A3。
3. type9结构校验和普通relocation范围校验分开。
4. 记录解析用减法式边界检查,避免offset+size溢出。
5. 在`resolveSymbols`之后构建精确`InputSymbol*`身份表,执行A3配对和查重。
6. 合成输入节对象:
   - 合法槽4B PROGBITS加4B NOBITS;
   - 保留/系统8B NOBITS;
   - 不把type9复制到合成机器代码;
   - 合成EJMP使用原R_MCS251_24,目标为已核入口。
7. 新表是唯一VECS;IRQ模式拒绝所有输入旧VECS,即使大小零也不把旧资产当配套CRT。
8. 固定向量区域不能被`--area-start=VECS`移动;传入非FF0003值报错。
9. 检查整个向量保留范围与所有CODE范围交叉,不能只检查`Image`已有字节。
10. HOME只能3B reset;验证J16跳到配套BOOT,不能4B EJMP。
11. BOOT必须不小于FF0210;T08资产默认固定在FF0210。
12. DEFAULT、RESET、机器码、asset与所属文件一致。
13. 原undefined/ROM/重定位检查照常运行。
14. `applyRelocations`跳过已消费的metadata relocation,不向Image插入metadata。
15. 若普通ALLOC relocation直接指向已知ISR,拒绝其作为普通地址/调用用途;合成向量是内部例外。section+addend也需与已知精确ISR入口地址交叉核对。
16. 不反汇编全映像建立调用安全图。
17. map固定新增52行:
   ```text
   IRQ 00 0xff0003 ISR _irq0
   IRQ 01 0xff000b DEFAULT __mcs251_isr_unhandled
   IRQ 07 0xff003b RESERVED
   IRQ 14 0xff0073 SYSTEM
   ```
18. 合法槽末尾按两位十进制打印;地址使用6位十六进制。
19. map不写ISR-safe、最大栈、priority。
20. 如果新CRT的bit初值通过既有XINIT引用BSEG_BYTES字节,仅增加这种已分配字节范围的XINIT目标合法性;不新增bit分配器或v2能力。

### 测试夹具生成器接口

`isr-fixture.py`由本卡实现,接口冻结:

```text
isr-fixture.py --out FILE --slots 0,6,8,24,36,49,50,51
isr-fixture.py --out FILE --slots 7 --allow-invalid-slot
isr-fixture.py --out FILE --slots 1 --mutation NAME
```

输出完整YAML ELF32/MSB/v1:

- 每函数使用T04冻结的41B空ISR机器码;
- `STT_FUNC`、正确size;
- ENTRY/REGISTER成对;
- type9引用函数;
- 原ABI note;
- 不内含CRT。

mutation集合固定:

```text
version
record-size
caps
reserved-field
missing-ref
duplicate-ref
ref-addend
ref-section-symbol
ref-in-alloc
wrong-entry-kind
wrong-save-profile
registration-without-entry
entry-without-registration
default-as-user-isr
undefined-entry
```

每个mutation只改一个因素,其余保持有效。生成器不得导入产品reader或从产品数组反推预期槽表。

### 完整lit文件

```text
# RUN: %python %S/Inputs/isr-fixture.py --out %t.good.yaml --slots 0,6,8,24,36,49,50,51
# RUN: yaml2obj %t.good.yaml -o %t.good.o
# RUN: yaml2obj %S/../../../validation/mcs251-elf/runtime/crt-irq.yaml -o %t.crt.o
# RUN: mcs251-lld %t.crt.o %t.good.o --area-start=HOME=0xff0000 --area-start=BOOT=0xff0210 --area-start=CSEG=0xff0400 --area-start=XINIT=0xff8000 --flash-base=0xff0000 --flash-size=0x10000 --map=%t.map -o %t.elf
# RUN: FileCheck %s --check-prefix=MAP < %t.map
# RUN: %python %S/Inputs/isr-check-image.py %t.elf %t.map --registered 0,6,8,24,36,49,50,51
# RUN: %python %S/Inputs/isr-fixture.py --out %t.bad.yaml --slots 7 --allow-invalid-slot
# RUN: yaml2obj %t.bad.yaml -o %t.bad.o
# RUN: not mcs251-lld %t.crt.o %t.bad.o -o %t.bad.elf 2>&1 | FileCheck %s --check-prefix=SLOT
# RUN: %python %S/Inputs/isr-fixture.py --out %t.v2.yaml --slots 1 --mutation version
# RUN: yaml2obj %t.v2.yaml -o %t.v2.o
# RUN: not mcs251-lld --print-input %t.v2.o 2>&1 | FileCheck %s --check-prefix=VERSION

# MAP: IRQ 00 0xff0003 ISR
# MAP: IRQ 01 0xff000b DEFAULT
# MAP: IRQ 06 0xff0033 ISR
# MAP: IRQ 07 0xff003b RESERVED
# MAP: IRQ 13 0xff006b RESERVED
# MAP: IRQ 14 0xff0073 SYSTEM
# MAP: IRQ 15 0xff007b SYSTEM
# MAP: IRQ 51 0xff019b ISR
# SLOT: MCS251 ISR: vector is not a legal slot in profile 0-51
# VERSION: MCS251 ISR: unsupported metadata version
```

夹具还需提供普通`_main`定义,否则正确CRT的未定义检查会失败;定义放普通CSEG,不能标ISR。该普通main可用`80 FE`终止字节。

`isr-check-image.py`独立解析ELF PT_LOAD:

- 建立地址→字节映射;
- 独立硬编码13个非合法槽;
- 检查39个EJMP;
- 检查各地址公式;
- 检查默认目标一致;
- 检查13整槽没有PT_LOAD载荷;
- 检查合法槽尾4B无载荷;
- 检查reset3B、BOOT范围;
- 不把map存在当机器字节正确。

### 必须额外执行的矩阵

生成器与同一lit框架逐项运行:

- 39个合法槽分别正例;
- 13个非法槽分别负例;
- 52、64、65535负例;
- mutation全集;
- 两对象重复槽;
- 同函数两个REGISTER;
- 普通函数伪造默认名;
- 默认机器码改成ERET;
- 同时输入旧CRT;
- BOOT=FF0100;
- 普通CSEG放入槽31无载荷区;
- ROM恰好边界与少1B;
- GC/LTO选项继续拒绝;
- 原v2对象拒绝回归。

### 完成定义与禁止项

上述矩阵全部记录实际结果,原lld目录回归通过,Alice审核metadata不进入ROM及保留洞参与重叠。

不许实现v2;不许取消旧输入结构检查;不许按符号名认证默认;不许扫描普通库寻找安全摘要。

---

## T08 — MI5:独立IRQ CRT与BOOT迁移

**工作量:M。构建树:isr-link工具;不占lld产品文件。纪律:D0。**

### 前置

A3/A5冻结即可开发。静态链接依赖T07。

### 文件独占权

- 新建 `C:\Prj\LLVM\MCS251\validation\mcs251-elf\runtime\crt-irq.yaml`
- 新建 `C:\Prj\LLVM\MCS251\validation\mcs251-elf\runtime\gen-crt-irq.sh`
- 新建 `C:\Prj\LLVM\MCS251\validation\mcs251-elf\runtime\check-crt-irq.py`
- 新建 `C:\Prj\LLVM\MCS251\lld\test\MCS251\isr-crt.test`

旧`crt-selfstart.asm/yaml`、旧生成脚本一律只读。

### 实施步骤

1. 新建YAML,不在构建时删除旧CRT的VECS。
2. HOME精确`020000`,J16 relocation到BOOT。
3. BOOT始于:
   ```asm
   clr EA
   mov PSW,#0
   mov DPS,#0
   ```
   地址:
   - PSW=D0;
   - DPS=E3。
4. 保持普通中断关闭、NMI未启用的复位前提。不得用伪RETI清in-service状态。
5. 使用原v1栈基址符号`__mcs251_stack_base`。
6. 初始化bit字节区:
   - 采用新CRT拥有的16B BSEG_BYTES保留,地址20–2F;
   - 先清零;
   - 若既有XINIT提供该字节区显式初值,随后覆盖;
   - 不新增源语言bit allocator。
7. 使用原XINIT格式初始化全局;原walker可逐字节复用,但所有新位置和relocation必须重新计算。
8. 调`_main`使用ECALL;main返回后无栈死循环。
9. DEFAULT单独函数,内容`C2AF80FE`。
10. RESET和DEFAULT记录来自同一对象,asset1。
11. 不提供旧VECS。
12. `gen-crt-irq.sh`只接收明确的yaml2obj/readobj路径和输出路径,不默认写仓库内`crt.o`。
13. `check-crt-irq.py`独立检查:
    - 原v1身份;
    - 新metadata;
    - HOME/BOOT/default内容;
    - relocation偏移;
    - 不存在VECS;
    - 不存在IP地址初始化;
    - 无SETB EA;
    - 默认无call/push/return。
14. checker不能用"整个文件搜索字节B8"判断是否写IP;应按冻结启动模板和指令边界检查。

### 原walker可复用字节

从旧BOOT offset14到6A前,长度86B:

```text
7E0800007A0C00007E240000BE24000068430B0A400B0C0B0C7DA40B0A600B0C0B0C0B0A800B0C0B0C9E2400067EE000BE64000068097A49E00B441B6480F1BE84000068C77E0BE00B0C7AA9E00BA41B841B2480EAAA
```

walker内部相对分支在整体搬移后不变。相对walker起点的符号字段:

```text
+2 MID8(s_XINIT)
+3 LO8(s_XINIT)
+7 HI8(s_XINIT)
+10 R_MCS251_16(l_XINIT)
```

不得把旧BOOT全块复制后保留旧默认打印和旧符号偏移。

### 完整lit测试

```text
# RUN: yaml2obj %S/../../../validation/mcs251-elf/runtime/crt-irq.yaml -o %t.o
# RUN: %python %S/../../../validation/mcs251-elf/runtime/check-crt-irq.py %t.o
# RUN: llvm-readobj --file-headers --sections --symbols --relocations %t.o | FileCheck %s

# CHECK: EF_MCS251_ABI_V1
# CHECK: Name: .mcs251.HOME
# CHECK: Name: .mcs251.BOOT
# CHECK: Name: .mcs251.isr
# CHECK: R_MCS251_ISR_REF
# CHECK: R_MCS251_ISR_REF
```

### 完成定义

- checker和lit通过;
- 原CRT文件无diff;
- 新默认4B,reset3B;
- FF0210链接后BOOT/CSEG不重叠;
- globals和bit初值运行验证交T09/T10。

### 不许做

不许IP初始化;不许开放EA/源;不许统一清pending;不许修改历史CRT;不许把默认改成C函数;不许增加WTST/板时钟策略的新推断。

---

## T09 — MI6:C→IR→ELF、优化、集成回归门禁

**工作量:M。构建树:s1最终集成+冻结lld。纪律:D0。**

### 前置

T01–T08全部Alice review通过。

### 文件独占权

- 新建 `C:\Prj\LLVM\MCS251\validation\mcs251-isr\compile-matrix.py`
- 新建 `C:\Prj\LLVM\MCS251\validation\mcs251-isr\firmware.c`
- 新建 `C:\Prj\LLVM\MCS251\validation\mcs251-isr\helper.c`
- 新建 `C:\Prj\LLVM\MCS251\clang\test\CodeGen\mcs251-isr-opt.c`
- 新建 `C:\Prj\LLVM\MCS251\llvm\test\CodeGen\MCS251\isr-exits.ll`

产品文件只读;发现错码退回对应属主,不能自己跨卡修补。

### 实施步骤

1. s1重新构建全部产品,不使用旧树混合工具。
2. `compile-matrix.py`接受:
   ```text
   --clang ABS --llc ABS --opt ABS --lld ABS
   --yaml2obj ABS --readobj ABS --out-dir ABS
   ```
3. C优化档O0/O1/O2/O3/Os分别执行:
   - GNU属性;
   - Keil后缀;
   - 内部未引用ISR;
   - 多个早返回;
   - 固定局部数组;
   - 高寄存器压力spill;
   - 跨TU普通helper;
   - 多参数普通helper的既有静态槽ABI正例。
4. 检查每个对象恰有配对记录。
5. 对IR进行GlobalDCE/IPO后仍保留定义、CC、slot、root。
6. 检查helper不带CC128、不带ISR槽属性。
7. 各C对象与新CRT链接,运行T07独立镜像检查器。
8. globals初值和bit字节初值由运行镜像验证,不只查ELF。
9. 负例通过已冻结诊断拒绝;不能生成普通ERET对象。
10. 编译矩阵输出JSON,记录命令、版本、sha256、结果,不输出虚假栈安全字段。

### 完整优化测试

```c
// RUN: %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,1,32,8,1 -O0 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,1,32,8,1 -O1 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,1,32,8,1 -O2 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,1,32,8,1 -O3 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -mcs251-memory-contract=1,1,32,8,1 -Os -emit-llvm -o - %s | FileCheck %s

extern void helper(unsigned char, unsigned char);
volatile unsigned char value;

static void irq(void) __attribute__((interrupt(1)));
static void irq(void) {
  if (value == 0)
    return;
  helper(value, 3);
  if (value == 2)
    return;
  value = 4;
}

void ordinary(void) { helper(1, 2); }

// CHECK: @llvm.used = appending global
// CHECK-SAME: @irq
// CHECK: define internal mcs251_intrcc void @irq()
// CHECK: call void @helper
// CHECK: define{{.*}} void @ordinary()
// CHECK: "mcs251-isr-vector"="1"
```

### 完整多出口测试

```llvm
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %s -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs %s -o %t.o2.s
; RUN: FileCheck %s < %t.o2.s

target triple = "mcs251-unknown-none"
@flag = global i8 0
@llvm.used = appending global [1 x ptr] [ptr @irq], section "llvm.metadata"

define mcs251_intrcc void @irq() #0 {
entry:
  %v = load volatile i8, ptr @flag
  %z = icmp eq i8 %v, 0
  br i1 %z, label %a, label %b
a:
  store volatile i8 1, ptr @flag
  ret void
b:
  store volatile i8 2, ptr @flag
  ret void
}

attributes #0 = { noinline "mcs251-isr-vector"="1" }

; CHECK-LABEL: _irq:
; CHECK: push psw
; CHECK-NOT: eret
; CHECK: pop psw
; CHECK-NEXT: reti
; CHECK-NOT: eret
```

### 完成定义

- 全优化矩阵通过;
- T01–T08测试全绿;
- 普通函数CC/ERET/ELF/REL回归通过;
- s1与配套lld工具身份冻结;
- Alice确认可进入模型qualification。

### 不许做

不许降低优化档;不许把静态槽helper负例化;不许以手写IR替代C链;不许把测试成功解释成ISR-safe。

---

## T10 — MI6:模型保存窗口、长期运行、分板真机

**工作量:L。构建树:使用T09冻结产品,不修改QEMU。纪律:D0。**

### 前置

- 测试基础设施可提前开发;
- 生产镜像验证必须等T09;
- 真机需PM明确板、端口、烧录资产及操作授权。

### 文件独占权

- 新建 `C:\Prj\LLVM\MCS251\validation\mcs251-isr\qualify.py`
- 新建 `C:\Prj\LLVM\MCS251\validation\mcs251-isr\rsp.py`
- 新建 `C:\Prj\LLVM\MCS251\validation\mcs251-isr\qtest.py`
- 新建 `C:\Prj\LLVM\MCS251\validation\mcs251-isr\qualification.test`
- 新建 `C:\Prj\LLVM\MCS251\validation\mcs251-isr\board-results.json`
- 新建 `C:\Prj\LLVM\MCS251\validation\mcs251-isr\lit.cfg.py`

QEMU源码及历史t4资产只读。

### 已核模型接口

- QOM CPU路径由machine的`soc`子对象和`cpu`子对象构成:
  ```text
  /machine/soc/cpu
  ```
- qtest提供:
  ```text
  set_irq_in QOM-PATH NAME NUM LEVEL
  ```
- 先查询对象确认路径存在;不允许失败后任意猜路径。
- GDB核心寄存器:
  - 0–31:R0–R31;
  - 32–39:R56–R63;
  - 40:PSW;
  - 41:PSW1;
  - 42:32位大端PC。
- SFR物理窗口:
  ```text
  0x01000000 + (SFR地址 - 0x80)
  ```
- 当前默认接通IRQ0–4;高槽只做静态布局,不以64输入容量推断外设支持。

### 实施步骤

1. 复制T09工具和所用QEMU到qualification输出目录,记录sha256、版本、源码身份。
2. QEMU启动使用:
   ```text
   -M stc32g144k246
   -S
   -gdb tcp:127.0.0.1:独占端口
   -qtest unix:独占socket,server=on,wait=off
   -display none
   -serial file:独占日志
   -monitor none
   -kernel 已核HEX路径
   ```
3. 不使用qtest accelerator代替TCG执行;qtest仅作为控制接口。
4. 通过RSP单步停在真实指令边界;不能把PC直接改到ISR来冒充硬件入场。
5. 在低层实际到达选定边界后,才拉高高层IRQ。
6. 立即记录硬件帧与PC;确认进入高ISR后撤销注入请求。
7. 在高层RETI之后立即比较被打断边界快照:
   - R0–R31;
   - DPX可见实际值;
   - PSW、PSW1;
   - SPX;
   - 返回PC。
8. 不给硬件未实现的DPX保留位构造"必须可存"的假哨兵;先读实际可见值,再比较恢复。
9. 窗口必须覆盖:
   - 向量EJMP前/后;
   - 首条push前;
   - PSW保存后;
   - 每个DR保存后;
   - 每个固定帧SPAdjust后;
   - helper ECALL前/后、ERET前/后;
   - 每个固定帧撤销后;
   - 每个DR恢复后;
   - PSW恢复后、RETI前。
10. 高层自己使用与低层不同的寄存器值,不能空ISR仅push/pop却从未破坏现场。
11. 主→低→高→低→主分别核对;不只比较最终主程序。
12. 单层与两层至少各10000次,无SPX漂移、PC跑飞。
13. 共享bit字节由ISR修改,返回后修改必须保留,证明没有错误回滚共享内存。
14. 默认源:
   - 确认只有硬件4B入场;
   - 软件无额外增栈;
   - EA清零;
   - PC稳定在终止循环;
   - 不RETI、不UART打印。
15. 测试代码可以配置IP/IPH制造受控两层;这是夹具配置,不是工具链自动priority功能。
16. one-instruction deferral和硬件精确时序记模型不支持。
17. 真实外设触发与宿主IRQ注入分开输出,不混作同类证据。

### 测试接口冻结

```text
qualify.py --self-test
qualify.py --manifest ABS_JSON --case hardware-frame
qualify.py --manifest ABS_JSON --case single
qualify.py --manifest ABS_JSON --case nested-windows
qualify.py --manifest ABS_JSON --case long-run --iterations 10000
qualify.py --manifest ABS_JSON --case default
```

manifest必须包含:

```json
{
  "schema": 1,
  "qemu": {"path": "/absolute/path", "sha256": "64 hex"},
  "tools": {},
  "machine": "stc32g144k246",
  "image": {"path": "/absolute/path", "sha256": "64 hex"},
  "map": "/absolute/path",
  "output_dir": "/absolute/path",
  "gdb_port": 0,
  "qtest_socket": "/absolute/path",
  "timeout_seconds": 30
}
```

`gdb_port=0`只在生成manifest时表示请求分配;运行前必须替换为PM分配的非零独占端口。

self-test使用假的RSP/qtest transport验证:

- checksum;
- packet拆包;
- timeout;
- 大端寄存器解码;
- 状态比较;
- 帧账目;
- 不同结果分类。

不能在没有QEMU时让`--case nested-windows`退化为self-test。

### 完整lit文件

```text
# RUN: %python %S/qualify.py --self-test | FileCheck %s
# CHECK: ISR qualification self-test PASS
```

运行qualification的命令不放进普通LLVM默认lit,避免每次编译都自动启动QEMU或接触串口:

```bash
wsl.exe -d Debian -e bash -c '
set -eu
python3 /mnt/c/Prj/LLVM/MCS251/validation/mcs251-isr/qualify.py \
  --manifest /home/liu/mcs251-isr-qualification/manifest.json \
  --case nested-windows
'
```

### 真机子集

G12、G144分别记账:

- 单发定时器;
- 重复ISR;
- 完整整数现场与SPX;
- 普通helper ECALL/ERET;
- 受控两层;
- 默认入口;
- 具体芯片硬件帧profile;
- 能命中的保存/恢复窗口;
- RETI/IE/IP写后的硬件时序作为独立项。

状态固定为:

```text
PASS
FAIL
NOT_RUN
MODEL_UNSUPPORTED
BASELINE_MISMATCH
BLOCKED_NO_BOARD
```

真机未提供时填`BLOCKED_NO_BOARD`,不能把QEMU运行某板固件写成该板真机PASS。

### 完成定义

- self-test通过;
- 所有模型支持的规定断言实际执行;
- 保存窗口逐点有快照与返回比较;
- 10000次无漂移;
- 模型能力缺项单列;
- 真机实际结果按板登记;
- Alice review完整日志和断言,不只读PASS总结。

### 不许做

不许改QEMU让探针通过;不许模拟半条指令中断;不许CPU PC直接跳ISR代替受理;不许timeout记PASS;不许NMI/T0 mode3、危险Flash配置或未授权烧录。

---

# D. 文件共享与并行度

## D1. 核心文件唯一属主

| 共享风险文件 | 唯一属主 |
|---|---|
| 全局CallingConv、LLVM文本语法、Verifier、协议头 | T01 |
| Clang Attr/Sema/CGCall/CodeGenModule | T02 |
| Keil LangOpts/token/parser/driver开关 | T03 |
| MCS251InstrInfo.td、RegisterInfo.td、Lowering.h、MCCodeEmitter | T04 |
| ISelLowering.cpp、FrameLowering | T05 |
| AsmPrinter、AsmBackend、ELFObjectWriter、目标contract verifier | T06 |
| lld LinkerCore.cpp/.h | T07 |
| 新CRT及其checker | T08 |
| 集成矩阵和C测试固件 | T09 |
| RSP/qtest/运行qualification | T10 |

任何卡需要改表外文件,先报PM申请唯一属主;不能以"只改两行"为理由跨卡编辑。

## D2. 可并行集合

T01完成后:

```text
前端轨    T02 → T03
后端轨    T04 → T05 → T06
链接轨    T07
启动轨    T08
验证准备  T10 self-test/RSP/qtest基础设施
```

其中:

- T07 reader不依赖T06实际对象,可用独立YAML夹具;
- T07最终正例需要T08;
- T08不用产品源码,可与前后端并行;
- T10运行验收必须等T09;
- T09必须等全部生产实现review通过。

不得为了并行把T07的同一个LinkerCore.cpp拆给两个模型。

## D3. 工作量

| 卡 | MI | 估计 |
|---|---|---|
| T00 | 管理前置 | S |
| T01 | MI1 | L |
| T02 | MI2 | L |
| T03 | MI2 | M |
| T04 | MI3 | M |
| T05 | MI3 | L |
| T06 | MI1/MI3 | M |
| T07 | MI4 | L |
| T08 | MI5 | M |
| T09 | MI6 | M |
| T10 | MI6 | L |

本表是相对工作量,不冒称工期承诺。T01较原切片估计增加了明确IR用途验证和共享协议登记;MI4不含不存在的v2底座建设。

---

# E. 建议派单顺序

## 第一批

1. PM执行T00。
2. 派一个通用subagent做T01。
3. 可另派通用subagent提前做T08新CRT及T10 transport self-test;这两者只消费冻结区,不修改T01文件。
4. T01完成立即Alice review。

## 第二批

T01通过后,同时派:

- T02;
- T04;
- T07。

T08继续。

先开并行编辑窗口,再开统一源码冻结后的并行构建窗口。

## 第三批

- T02通过→T03;
- T04通过→T05;
- T05通过→T06;
- T07 reader完成后与T08做第一次纯YAML链接;
- T06完成后做真实llc对象与lld协议联调。

若ENTRY/REGISTER解析出现差异,退回Alice,不能由两执行者私下重新议定字段。

## 第四批

T01–T08全部review通过:

- T09最终s1集成;
- O0–Os完整矩阵;
- 普通代码回归;
- 冻结工具与镜像身份。

## 第五批

T09通过:

- T10硬件帧;
- 单层;
- 两层窗口;
- 长期;
- 默认;
- 最后分板真机。

硬件帧profile或保存编码失败时,阻塞运行验收;**不能退回11B桩或"先只支持空ISR"。**

---

# F. Alice review清单

每卡review至少逐项回答:

1. 普通函数ABI是否保持原样?
2. 是否恢复了已删除的安全检查或priority范围?
3. CC、slot、保活、对象身份是否完整连接?
4. 任一失败是否会静默变普通函数?
5. 符号关联是否精确、是否意外section-fold?
6. metadata是否被放进ROM?
7. 保存前是否破坏任何未保存现场?
8. PSW、PSW1、A/B、DPX、SPX是否正确建模?
9. 每个正常出口是否完整恢复并RETI?
10. 保留洞是否参与布局冲突?
11. 默认身份是否独立、是否验证真实4B终止码?
12. 旧VECS和FF0100冲突是否明确拒绝?
13. C两种语法是否都走同一AST/IR?
14. O0–Os是否不丢内部根?
15. 是否允许既有ABI支持的跨TU普通helper?
16. 测试是否真正执行,是否存在空CHECK、弱断言或仅看符号名?
17. 模型、源码事实、真机是否分开记账?
18. 是否触碰原有未跟踪文件或未授权文件?

---

# G. 用户契约,必须随交付保留

工具链只承诺:

- 属性不被忽略;
- 支持profile下每层整数保存恢复正确;
- 普通返回与RETI不混用;
- 向量、合法注册、重复、默认、布局正确;
- 编译器自身固定帧与保存序列正确。

用户负责:

- 配置优先级、使能、运行时变化;
- 提供足够且始终有效的物理栈;
- 审计普通函数、运行库、静态槽、overlay异步重入;
- 避免不安全的动态或手工SPX更新;
- 保持bank0、DPS0、匹配硬件帧、排除NMI/T0 mode3;
- 声明共享可见性并设计同步;
- 正确确认外设及共享资源;
- 不使用longjmp、手写RETI或伪造返回帧逃离ISR。

`volatile`不提供RMW原子性。37B不覆盖其他bank、其他DPTR、外设事务或加速器状态。

每层`4+37=41B`只是成本下界,不是工程最大栈证明。

默认入口零软件增栈不等于零硬件栈,也不等于原子停机。

NOP(4)不是每次写寄存器后的自动动作,不是SPX两写原子证明,本役不自动插入。

---

## 最终自检结论

- B1已解除:52项槽数组有明确页码,39/11/2分类完整。
- B2已落实:原ELF v1身份不变,不建设v2对象底座。
- 原MI0位置、普通CC、ERET机制、lld接入点、CRT冲突均按当前代码核对。
- Keil后缀列为本役交付,不再写"可选以后做"。
- 原CRT与原v2拒绝测试保持不动。
- 文件属主与构建树冲突已单独列出。
- 生产实现、静态测试、QEMU验证、真机验证没有混为一个完成状态。
- M68k只作为返回分流与晚期边界检查的参考;本役保留独立RETI DAG lowering,不加入无必要的统一RET伪展开或保存合并pass。
- **本文测试为待实施验收规格,未实际执行;目标任务书文件未落盘。**
