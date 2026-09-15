# G5 AS3 形参改写语料快照（demo 82 / 44）

**目的**：改写包（`/home/liu/LLVM_STC32/mcs251-demos-rewritten/`）不受 Git 管理，
G5 切片的 AS3 精确形参改写此前只存在于仓库外工作区。本目录把 demo 82 与 44 的
改写后源码固化为受控资产，使改动可审计、可复现，而不是仅凭主树的设计与 lit
测试就宣称语料已入库（Alice 2026-09-15 复审要求）。

**来源**：`mcs251-demos-rewritten/src/82-CANFD使用DMA收发测试/`（本目录 `src/`）
与 `.../44-CANFD1-CANFD2同时使用收发测试/`（本目录 `src/44/`），逐字节复制。

**改写范围**（G5-ADDRSPACE-DESIGN-draft.md §3.2 的闭包，Alice 复算确认）：

| demo | 函数数 | AS 编辑处数 | 文件分布 |
|---|---|---|---|
| 82 | 15 | 21 | canfd.c 15、canfd.h 1、canfd_dma.c 1、main.c 4 |
| 44 | 17 | 20 | canfd.c 17、canfd.h 3 |

- 最小闭包之外的 5（82）/3（44）个函数**保持 AS0 不变**（逐名核对见设计 §3.2）。
- 82 的 `pCanRx` 读**与写**路径均已 AS3 化（`pCanRx->u32ID = reverse4(...)` 的
  store 必须落 AS3；早期副本只改读路径，Alice 复审证伪了当时的"零漂移"结论）。
- 改写性质：把被 AS3 实参流入的形参声明为 `__xdata`（AS3），使
  `ptr addrspace(3)` 端到端保留，而不是在调用点做 AS 转换（后者会把访存通道
  从 `movx @dptr` 24 位降为 DR 通道——设计 §4(a) 已否决）。

**验收证据**（详见 `/home/liu/LLVM_STC32/IMPL-G5-PROGRESS.md`）：

- 五 TU clang+llc v2 契约（1,2,32,8,1）全 0 错、零 `changes address space` 诊断残留。
- IR 保留 `ptr addrspace(3)`（`@CANFD_Set_DMA_Buff(ptr, ptr addrspace(3))`、
  `DmaRxBuffer_Decode(..., ptr addrspace(3))`）；`main.ll` 函数体内
  `addrspacecast` 计数为 0。
- 访存 `mov 0x84`(DPXL bank 字节) + `movx @dptr,a` 全 24 位通道：
  `Set_DMA_Buff` 6/6、`DmaRxBuffer_Decode` 15/15（含 4 条 RMW 写回）；
  44 侧 `CAN_SendData` 15/15、`CAN_ReceiveData` 19/19，`CANFD_Init` 本体无访存
  （经调用下传，Alice 复测修正）。
- 静态槽 `_PARM_2` = 4B OBJECT；`Dma*Buffer` 落 XSEG。
- 跨 TU QEMU 链（callee 用 demo-82 精确形参）byte-exact PASS。

**本快照不改变残余记账**：两 demo 的 T1 仍被与 G5 无关的容量缺口挡住——
82 剩 B1(DSEG 196B vs 80B 空闲窗)+B2(CSEG text 38340B > 30976B)、
44 剩 B1'(DSEG 1228B)+B2(text 33780B)，归 G8/CSEG 切片。

**注意事项**：v2 契约下 SFR 直访需 AS6（`__attribute__((address_space(6)))`）
形态；AS0 常量访问 SFR 会走 DR 间接窗口而摸不到（实测 AS0 0x99 走 DR、AS6 走
direct）。demo 语料中的 SFR 访问已按此改写。
