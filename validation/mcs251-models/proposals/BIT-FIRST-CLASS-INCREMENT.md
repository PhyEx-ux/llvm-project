# `bit` 一等类型设计增量稿

**作者**：Alice（设计增量交付）。**日期**：2026-09-07。**状态**：设计建议，PM 落盘；不代表已实现或验证。
**证据口径**：[U]=用户已裁定；[C]=语料实测；[S]=本轮源码亲核；[SD]=SDCC 文档/源码先例；[M]=手册证据（页码）；[D]=设计契约/推断；[P]=待实现/探针/真机验收。规范性规则统一属 [D]。

## 1. 结论及与 L1 的衔接

**推荐"一种逻辑值类型、三种存储形态、一套位操作语义"。** 不把 `bit` 宏替换成 `_Bool`，不另造与 L1 无关的位访问后端。

| 形态 | 源语言身份 | 实际承载 |
|---|---|---|
| 全局／静态 `bit` 对象 | 独立目标布尔标量类型，可读写、不可取址 | lld 分配的 RAM 位槽 |
| 自动局部变量／形参的函数内副本 | 同一 `bit` 类型、每次调用独立生命周期 | SSA／寄存器；spill 用规范化 1B 栈槽 |
| L1 固定位引用（含未来 `sbit` 包装） | 同一 0/1 值域，另带固定位置和受控左值身份 | 不分配对象；使用既有固定 RAM／SFR 位 |

规则要点：
- 新增目标 `bit` 类型；保留 `__bit` 核心拼写，裸 `bit` 在可选 Keil 方言模式识别；允许 `typedef bit BOOL`。
- L1 固定位引用保持 volatile、不可取址；普通 `bit` 对象 volatile 与否由声明决定。
- 持久位对象的读写、SETB/CLR/CPL 复用 L1 intrinsic／pseudo／机器效果规则；差别仅是位置来自**固定常量**还是**待链接符号句柄**。
- 自动局部 `bit` 是调用私有的逻辑值，不承诺物理位打包（刻意区别于 SDCC 局部位分配）。
- 普通 AS5 指针、GEP、load/store 继续禁止；内部符号句柄只允许注册与位 intrinsic 消费。
- L1 §7.6"地址必须为常量"扩展为"固定常量或受控位对象符号"；不开放动态位地址。
- `sbit KEY1 = P3 ^ 2` 的旧式声明解析属头文件/声明兼容工作；本稿只冻结其语义接入点。

**显式修订既有设计**：方言设计 §7.7／O09 的"自动 packing、跨 TU allocator、bit relocation 后置"由本增量开启；§13 R4 及 DESIGN D.2/D.5 的"bit 参数／返回不开放"由本文第 4 节替换。普通 AS5 禁令不变。

## 2. 需求证据及统计校正

631 次/211 文件与 90 次/22 文件已按原脚本复现；但 631 的正则混入部分返回类型、漏部分带限定声明；90 只匹配**具名 bit 形参**。[C][S]

### 2.1 原始 90 次完整分类（逐类匹配无遗漏）

| 形态 | 次数 | 代表签名 |
|---|---:|---|
| 单 bit 参数、无其他参数、非 bit 返回 | 72 | `void Change_Val(bit i)` |
| 单 bit 参数＋两个非 bit 参数 | 2 | `void simple_num(bit flag, double (*a)[5], double (*b)[5])` |
| 单 bit 参数＋三个非 bit 参数 | 4 | `void OLED_Draw_Byte(uint8*, uint8, uint8, bit)` |
| bit 返回＋单 bit 参数＋一个非 bit 参数 | 4 | `bit getWaveLength(uint16, bit)` |
| bit 返回＋单 bit 参数＋三个非 bit 参数 | 8 | `bit GetTriggerPos(uint16×3, bit)` |
| 多个 bit 参数 | **0** | 需合成测试补充 |
| 合计 | 90 | — |

补充扫描：原正则漏 **24 处仅返回类型含 bit** 与 **4 处无名 bit 形参**；裸 bit 函数声明/定义 **118 处/38 文件**（82 定义+36 声明，非 118 个独立函数）；**29 处 `typedef bit BOOL`**；存在自动局部和 `static bit`。未发现 `bit *`、`bit a[]`（含 BOOL 派生拼写）——源文本负证据，不宣称宏展开后全程序证明。**2 处 `bit xdata f_z`（科学计算器）**：明确拒绝冲突存储声明，由移植改为普通自动 bit 或 `_Bool xdata`，不静默忽略 xdata。[C]

## 3. 位槽分配、别名与初始化

### 3.1 地址单位校正

**8051 先例是 128 个位地址 `0x00–0x7F`，对应内部 RAM 字节 `0x20–0x2F`（16 字节），不是 0x00-0x2F 共 48 位。** SDCC 手册明写 128 bits/16 bytes，与 L1 映射相符。[SD][D] `backing_byte = 0x20 + (bit_address >> 3)`，`bit_index = bit_address & 7`。

### 3.2 分配责任：编译器提出需求，lld 统一分配

1. 每个静态存储期位对象输出版本化记录（符号身份、定义/引用、0/1 初值、限定信息）；不伪造普通 1B DSEG 对象。
2. lld 跨 TU 解析 extern 与定义；重复强定义/未定义引用按正常符号规则。
3. 分配顺序：显式预留 → 固定位引用 backing 约束 → 自动位对象 → 普通 RAM 类别。
4. 自动对象可跨 TU 共用同一 backing byte 的不同位；不作函数间生命周期 overlay、不设全局 bit 传参银行。
5. backing byte 被自动位对象占用即整字节从普通 RAM allocator 排除；未用位仍可供其他自动位对象。
6. 固定位引用按 L1 保守预留 backing；首期不让自动对象挤入用户控制的固定别名字节。
7. **位槽耗尽必须链接失败**，不退化为 byte RAM。自动局部 spill 是另一种承载，不占 128 位。

当前 E3 工作树已有 BSEG_BYTES（0x20-0x2F 预留）与 BIT_BANK overlay 路径，**无跨 TU 单位槽分配器**；新 allocator 可复用内部 RAM 占用账本，不得把独立位对象误并成 overlay 银行。[S]

### 3.3 relocation 与初始化

- 新增专用"位对象符号→位指令地址字段"relocation；验证目标为位对象、范围合法、首期拒绝任意 addend；正式编号 PM 登记。
- 位地址 0 必须合法（不被 null 规则拒绝）。
- lld 汇总每个自有 backing byte 的初始化 mask/value；CRT 在开放中断前完成清零/置初值；不把 NOBITS 当上电已清零、不覆盖用户预留。
- 普通 RAM 访问可与 backing byte 别名；不因 AS5 或位身份产生错误 NoAlias；volatile 位操作保留 L1 次数与排序约束。
- 有意 byte/bit alias 以同一 backing 所有者表达；两个分配记录不得争用物理 RAM。

### 3.4 是否加入 37B ISR 保存：**不加入**

全局/静态 bit 是程序状态，不是 CPU 上下文；ISR 置标志后被尾声恢复会抹掉本次通信。自动 bit 活在寄存器中由每层寄存器保存保护；已 spill 的属该次调用栈帧。共享标志需正确的 volatile 声明；volatile 不提供多操作事务原子性。DR20 等寄存器编号不是 RAM 0x20，不得误算。[D]

## 4. ABI 裁定——规范化字节值 ABI（推荐冻结）

### 4.1 SDCC 先例实况 [SD]

非重入函数 bit 参数直接分配在位内存；重入前 8 个 bit 参数走位区虚拟寄存器 `bits`（b0-b7）；bit 返回经 **carry**；`bits` 是位寻址 RAM 虚拟寄存器，不是物理 B 寄存器八位。"全塞 DPL/DPH/B/A 某个位"不是 SDCC 现成 ABI。

### 4.2 本项目规则：bit 按规范化 i8 传递，唯一有效值 0/1

普通函数 CC 不因含 bit 而改。[D]

| ABI 位置 | 规则 |
|---|---|
| 第一个源参数为 bit | 占完整 **DPL**；载荷 bit0，bit7:1 必须为零 |
| 第一个参数不是 bit | 沿用原类型 DPL/DPH/B/A 序列；不把后续 bit 塞"空闲位" |
| 第二个及以后参数为 bit | 各占原参数序号的 `_PARM_n` **1B 静态槽**，内容 0/1 |
| 多个 bit 参数 | 按普通位置逐个传递；不打包、无 8 参数限制 |
| bit 返回值 | 完整 DPL=0 或 1；不以 carry/B/A 返回 |
| 函数内部形参副本 | 尽早读取 ABI 位置形成调用私有逻辑值；跨调用活跃按普通规则 spill |

补充：源层 `int→bit` 先"是否非零"再 0/1，不靠 trunc 得奇偶语义；Clang 表达式内可用 i1，**ABI 边界显式物化 i8**；`_PARM_n` 序号按原始源参数位置；跨 TU 同版 bit ABI；与 SDCC/Keil 无裸混链承诺；单 bit 参数普通函数指针调用沿现有单参间接调用路线，多参仍受静态槽限制。**重入代价明示**：后续 bit 槽与普通静态槽同样不自动重入；最小 ISR 不检查闭包安全（用户责任）。[D][U]

## 5. 语义边界（首期）

| 项目 | 规则 |
|---|---|
| 值域/赋值 | 仅 0/1；整数零→0 非零→1；bit→int 零扩展 |
| 整数提升 | 提升为当前 C `int`（int 位宽不变） |
| 与 0/1 比较、条件、`!` | 支持；`~b` 是整数提升后按位取反，不是逻辑翻转 |
| true/false | 服从 C 标准/stdbool.h |
| typedef、cv、extern、static | 支持；位对象身份不因 typedef 丢失 |
| 取址、bit 指针、bit 数组、聚合字段 | **首期拒绝**（含 typedef/typeof 间接构造） |
| sizeof(bit)/布局查询 | 首期诊断 |
| `_Atomic bit`、memcpy 位对象 | 拒绝 |
| 浮点↔bit | 随浮点设计另案 |
| `bit xdata` 等冲突放置 | 拒绝并给诊断 |

持久位左值遵守 L1 小保证集（常量赋值 SETB/CLR；动态值一次采样一次写；丢弃结果的 `^=1`/`b=!b` 才 CPL；CPL 结果使用等仍拒绝）。自动调用私有值的运算不受"必须物理 CPL"约束。

## 6. 测试矩阵与实施批次

| 层 | 必测 |
|---|---|
| Sema/AST | 裸词/`__bit`/typedef/cv/extern/static/无名形参；数组/指针/取址/布局/冲突限定负例 |
| CodeGen | 0,1,2,-1 转换；bit→int；`~` 不误作 CPL；i1 表达式与 i8 ABI 分离 |
| ABI | 首参/返回/后续槽/多 bit 合成/bit+i16/i32/指针混合/跨 TU/O0-Os/寄存器压力 |
| L1 | 固定地址与符号对象同语义；volatile 次数；CPL 负例；普通 AS5 IR 拒绝 |
| MC/lld | 位 0、7/8 边界、127；满 128 位及第 129 位失败；预留冲突/跨 TU packing/extern 合并/错 relocation 目标 |
| CRT/别名 | 零与非零初值、mask 保留邻位、普通 RAM 不占同字节 |
| ISR 集成 | ISR 置共享 bit 主程序可见；不回滚位 RAM；自动 bit 寄存器/spill 保存；竞争正负对照 |

| 批次 | 内容 | 体量 |
|---|---|---|
| BT0 | 冻结类型、字节值 ABI、句柄/relocation 协议；保留未实现负测 | M |
| BT1 | 类型、转换、自动局部值、参数/返回；复用普通 ABI | L |
| BT2 | 符号位 intrinsic、MC relocation；依赖 L1 位指令验收 | L |
| BT3 | E3 跨 TU allocator、backing 账本、CRT 初始化 | L |
| BT4 | 跨 TU/优化/ISR 集成及语料回归 | M |

## 7. 开放问题

| 决策点 | 推荐 |
|---|---|
| 局部变量是否也物理打包 | **否**；静态打包、自动走寄存器/栈。SDCC 式局部位银行另立非重入 ABI 才做 |
| DPL+1B 槽 vs carry/bits 银行 | **采纳前者**；不承诺 Keil/SDCC 二进制兼容 |
| 裸 `bit` 启用方式 | 与 Keil ISR 别名共用可选方言模式；旗标与协议编号 PM 登记 |

---

# 附：证据索引

E1 DIALECT-FRONTEND-DESIGN.md §7/§9/§13；E2 INTERRUPT-DESIGN.md v2；E3 INTERRUPT-SCHEDULING-RECOMMENDATION.md 含修订 v2；E4 STC32G-中断-手册情报；E5 /tmp/stc-examples/INVENTORY.md+统计脚本+语料 ~/stcex/src/；E6 sdcc-upstream/doc/sdccman.lyx:23441-23497,28121-28123,47358-47392；E7 sdcc-upstream/src/mcs51/main.c:90-139, gen.c:5302-5308, SDCCsymt.c:2396-2417；E8 MCS251ISelLowering.cpp:1771-2037；E9 lld/MCS251/LinkerCore.cpp:590-682,935-954；E10 SemaDeclAttr.cpp:6666-6695, CallingConv.h:298-301；E11 DESIGN.md D.2/D.5, crt-selfstart.yaml。
