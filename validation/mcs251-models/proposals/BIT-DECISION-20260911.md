# bit/sbit 战役 P01–P10 拍板记录

**日期**：2026-09-11。**裁定人**：用户（会话原话"同意推荐"，对 BIT-TASK-BREAKDOWN.md §8 十项推荐整体批准，无修改）。**登记**：PM 协调员。

以下每项均为 Alice 推荐原文要点，经用户批准后即为战役有效裁定：

- **P01 bit 值 ABI**：DPL 完整 0/1；第二个及以后的 bit 参数按源参数原序号占 `_PARM_n` 1B 静态槽；bit 返回 DPL；无 carry/bits 银行、无与 SDCC/Keil 裸混链。重入代价已知悉：后续 bit 静态槽与普通静态槽同样不自动重入，最小 ISR 不检查闭包安全（用户责任）。
- **P02 volatile 语义**：L1/sbit 隐含 volatile；普通 bit 按声明；首版保守 effects 可不消除非 volatile 持久访问；ISR 共享由用户显式 volatile/同步。
- **P03 sbit 透明范围**：支持本役两种旧式声明及正常读写/条件/认可 toggle；允许薄兼容头替换；不承诺未经改动的整个 Keil 官方头。
- **P04 管理寄存器/EA**：普通接口首期拒绝；EA 未来专用 save/restore，其他按精确寄存器效果另案；EA 原样 sbit 必须独立扩卡，不得默认放行。
- **P05 位槽耗尽与局部 packing**：128 位扣除预留后耗尽链接硬错；自动局部只 SSA/byte spill，不 fallback/overlay。
- **P06 CRT 池所有权**：新 bit-aware CRT/profile；lld 单次池预留+子分配+mask/value 初始化；旧 CRT 与新受控位协议拒绝混用；S1 固定 owned 全清零子集可先验。
- **P07 协议/ABI 标识发布**：保持现有 ELF 身份与 ISR 编号；新增版本化精确能力记录；旧 reader 拒绝未知节/reloc；必须升 ABI 版本时转 PM 另案。
- **P08 支持的 C 形态**：`__bit` 为核心，裸 `bit`/`sbit` 沿用 `-fmcs251-keil`；首期 C，排除 C++/varargs/无原型 bit 调用/COMMON/weak；函数指针含单 bit 值签名允许。**同时登记 Option B 范围修订**：OMP/ACC 构造边界整体拒绝 bit 能力（构造内任何携带 bit 能力的类型/表达式即拒绝，22 项 Sema 入口清单闭合，首期不承诺 OMP+bit 组合；仅当项目决策变更才重开；C++ 启用前须重审类型重建与成员实例化入口）。
- **P09 位符号句柄表示**：无 AS5 的受验证 AS0 身份占位（结构属性 `mcs251-bit-object` 的 i8 GlobalVariable）+ 专用 consumer/记录 + llvm.used 保活；正式内建名/编号/记录字节以后端已冻结实现为准（llvm/BinaryFormat/MCS251Bit.h、MCS251BitObject.h、lld/MCS251/BIT-OBJECT-CONTRACT.md v1），M2 前端沿用，不再另造表示。
- **P10 验收板型与放行门槛**：首批板和安全 SFR 清单由 PM 后续明确（QEMU stc32g144k246 模型层先行）；模型与逐板分别 PASS；无板只记"静态/模型完成"，不宣称硬件原子保证。

**生效影响**：M2（Clang CodeGen 降级）按 P01/P02/P09 解锁；BT06（兼容头）按 P03/P04 范围执行；BT14（CRT profile）按 P06；发布验收按 P10。

**关联证据**：M1 已于 2026-09-11 提交（c032488a6，Alice 四轮终审+F10 亲修 APPROVED，停止规则触发）；后端 S0/S1/BT12 见 0aa879d47 / c3ce58c13 / eba50ce45。
