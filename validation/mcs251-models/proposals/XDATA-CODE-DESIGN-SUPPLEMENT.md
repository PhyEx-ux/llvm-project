# XDATA-CODE 设计补充稿：DPXL 管理协议与 MOVX 全 24 位序列（X2-1/X2-2）

**日期**：2026-09-12。**状态**：随 X2 修复实现；正文为冻结补充，改动需新裁定。X2-4 修订（2026-09-12，Alice 复审定稿）改写 §3 的中断保持义务，其余正文不动。
**上位文档**：DESIGN.md B.2（AS3=`__xdata` 32/8，"保持完整 24 位有效地址"）、XDATA-CODE-SLICE-TASK.md §3 X3 需求补充⑥。

## 1. 裁定背景

X2 首版采用"phase-1 16 位窗口"实现：MOVX @DPTR 只覆盖 DPXL 复位值 01h 指向的单个 64K 区域，常量地址越窗即拒绝（rc=-6）、运行期指针只取低 16 位。Alice 复审给出反例（`p+65536` 生成 `add wr0,#0x0000` 后 MOVX，bank 静默回绕；常量 0x11234/0x21234 被拒），与冻结契约 B.2 冲突。协调员裁定方向 (a)：**完整 24 位语义**——每条 AS3 MOVX 访问序列显式管理 DPXL。

## 2. 生成的序列（MCS251ISelLowering.cpp，`buildXDATAAddress`/`splitXDATAAddress`/`buildMOVXByteLoad`/`buildMOVXByteStore`）

每个访问字节：

```
mov  rN, #bank      ; 常量 bank：编译期折叠（MOV8ri）
mov  0x84, rN       ; MOV8dpxl：region 写（sdas251 gold 7A 21 84）
mov  dpl/dph, ...   ; 窗口偏移 = 位 [15:0]（MOV8dpl/MOV8dph）
movx a,@dptr / movx @dptr,a
```

- 常量地址：bank 与窗口双双折叠；**bank==01h 也照样发射**（裁定：首期不省略；省略需全函数分析 pass，留待后续）。
- 运行期指针：任何常量偏移以**完整 32 位加法**折入指针（绝不走 i16 加——进位属于 bank 字节）；bank = 结果位 [23:16]（DR 高半字低字节），窗口 = 位 [15:0]。位 [31:24] 污染不进入 DPXL。
- 全局/外部符号：走既有 byte-of-24 重定位通道（MOVADDR32，与 AS9 far 全局同形），bank 字节来自链接器解析的 24 位地址（`(_sym) >> 16` 重定位）——落实 X3 裁定①"不得直接把 XSEG 地址截成低 16 位"。
- i16/i32 对象按字节展开，**每字节独立**计算 bank+窗口（对象跨 bank 边界时逐字节换 bank，不再有跨界拒绝）。

## 3. DPXL 保持协议（X3 需求⑥成文；X2-4 修订中断保持义务）

**核心性质（不变）：后端生成的代码对 DPXL 无保持依赖（自愈式）。** 每条 AS3 访问在 movx 前一步重设 DPXL。

**异步抢占：ISR 及嵌套 ISR 必须保存恢复 DPXL（X2-4 修订，Alice 定稿）。** 首版"ISR 无需保存 DPXL"为错误结论。反例（interrupt-trace.txt）：主程序 MOVX 序列中点（region 已写 01h）被抢占 → ISR 内 AS3 访问重设 DPXL=02h 且不恢复 → 返回后主程序同一序列的 MOVX 命中 02:1234 而非 01:1234（错 bank）。"返回后下一条访问自愈"只覆盖序列边界，覆盖不了序列中点的窗口。因此：

- 编译器生成的 ISR（含嵌套 ISR 的每一层帧）**必须保存并恢复 DPXL**。
- 现有 A6 固定帧已物理覆盖：`push dpx`（FrameSetup 末位）/`pop dpx`（FrameDestroy 首位）存取 24 位 DPX 含 region 字节（证据：irq.s:19-28）。**该保证不得删除。**
- 建模同步（X2-4）：ISR_PUSH_DPX `Uses` 补 DPXL、ISR_POP_DPX `Defs` 补 DPXL（MCS251InstrInfo.td），ISR 入口块 async live-ins 补 DPXL（MCS251FrameLowering.cpp）——调度/优化由此知道 ISR 帧触及 DPXL，帧操作不得被优化删除；回归测试 `xdata-isr-window.ll` 钉住。

三类场景分层陈述（义务不同，不得混谈）：

| 场景 | DPXL 义务 | 依据 |
|---|---|---|
| CRT 启动 | 无义务：生成代码不依赖复位值 01h，无置初值动作 | 自愈式 |
| 普通调用边界（未来 call 战役） | 被调方可任意 clobber，**不纳入 callee-saved**：调用点后第一条 AS3 访问自己重设 region | 自愈式 |
| 异步抢占（ISR/嵌套 ISR） | **必须保存恢复**：编译器生成帧 push/pop dpx 覆盖 DPXL | 上文反例 |

用户 AS6 写 SFR 0x84 与生成序列的交错仍为文档化未定义交互（§5），但该未定义性**不豁免编译器的 ISR 保存恢复义务**。

序内正确性由指令描述符钉住：MOV8dpxl `Defs=[DPXL], hasSideEffects=1`；MOVXALD/MOVXAST `Uses=[DPL,DPH,DPXL]`。效果：(1) 调度器把 region 写当作 movx 的依赖，两条独立 AS3 链的 bank 写不可能跨越对方 movx；(2) hasSideEffects 禁止 MachineCSE 合并相邻同 bank 两次 region 写——合并会把一次 region 写摊到两条 movx 上，序列中点窗口内任何未恢复的 DPXL 改写（手写 ISR 体、用户 SFR 写；编译器生成 ISR 帧已恢复）都会使第二条 movx 错 bank，测试 `xdata-dead-value.ll::twice` 钉住。

## 4. AS9 口径更正（声称更正项）

修复方报告曾把 "AS9 load/store rc=0" 列入**保持拒绝**清单，属口径错误。按冻结 far 设计（DESIGN.md B.2：AS9=`__far` generic，32/8 canonical 24 位数据地址容器；§403：AS0 far/AS9 走 canonical DR 24 位有效地址访问），AS9 load/store 经通用 DR 通道放行且 rc=0 是**正确行为**。更正后清单：

- **far 设计放行**（generic DR 通道，`mov r,@dr`）：AS0(4B)/AS4 load/AS9。
- **保持拒绝**：AS4 store（CODE 只读）、AS5（bit 空间，仅受控位左值机制）、AS7（保留）、其余未编号 AS。
- 仓库内无测试把 AS9 记作拒绝（`checkDataAddressSpace` 实现集 `{0,1,2,3,4,6,8,9}` 与 B.2 一致）；本节为口径的唯一更正记录。

## 5. 遗留边界

- 用户内联/手写对 0x84 的写与后端 AS3 访问的交错为未定义交互（§3）；后端从不在自身序列中间插入他方代码。
- XDATA 访问的**动态可达性**（指针可能指向未分配单元）由后端语义负责（按字节寻址如实生成），放置/重叠检查仍属 X3 链接器契约。
- 尺寸代价（实测，sdas251 汇编字节计）：常量 bank 访问每字节 +2 指令 +6 字节（`mov r,#bank`+`mov 0x84,r`）；运行期无偏移访问每字节 +1 指令 +3 字节（bank 即指针 lane）；运行期带常量偏移另加 DR 常量装载 + `add dr,dr`（+4~7 指令）。Alice 反例 `_runtime`（volatile i8 @p+65536）整函数 9→14 指令、22→37 字节（含旧版被截断而省去的 2 条 ABI lane 装载）。
- 新增指令：MOV8dpxl（编码锚 `xdata-code-bytes.mir::dpxl.mir`）；新保留寄存器 DPXL（HWEncoding 占位 60，bits<6> 上限内，不入任何分配类）。
