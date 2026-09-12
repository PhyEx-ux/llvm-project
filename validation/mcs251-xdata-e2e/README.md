# validation/mcs251-xdata-e2e — X4 集成 e2e 切片（官方源形态字节链 + QEMU）

XDATA-CODE-SLICE-TASK.md X4 行的验收包：把 X1（限定符）/X2（MOVX 与 DR 装载）/
X3（放置链接）与 X4 新增的 CRT xdata_init 遍历器（`validation/mcs251-elf/runtime/`，
本切片同步扩展）闭合到"官方源形态 → 字节断言 → 运行时"的完整链。

## 链路与工具

```
clang --target=mcs251-unknown-none -std=c11 [-fmcs251-keil] -O0
      -Xclang -mcs251-memory-contract=1,1,32,8,1 -S -emit-llvm
llc  -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1
      -mcs251-object-format=elf -filetype=obj          # v2 契约模块必须走 ELF 对象路径
lld  -flavor mcs251 crt.o <objs> --area-start=... --map --keep-symbols
llvm-objcopy -O ihex
python3 check-bytes.py build/                          # 146 项字节断言（X2 修复后口径）
qemu-system-mcs251 -M stc32g144k246 -bios fw.hex ...   # 串口哨兵收尾
```

`1,1,32,8,1` 是 uartdemo-33m 验证过的**兼容布局数值契约**（TransportVersion 1 /
ASLayoutVersion 1 / AS0 32 位 / InternalExtended / ExecutionContract 1）。在该契约
下 AS3/AS4 访问函数、逐对象 XSEG 放置与 xdata_init 记录全部经 v1 ELF 对象身份发
射；无契约默认（v2 形布局）下 AS3/AS4 指针操作数会在 ELF 对象输出处被 X3-R1 门禁
拒绝（见"边界"）。

链接窗口（rt-acceptance 板参数 + X4 两个新区）：

| 区 | 起点 | 依据 |
|---|---|---|
| HOME/VECS/BOOT | 0xff0000/0xff0003/0xff0100 | rt-acceptance 先例 |
| CSEG | 0xff0200 | 同上 |
| XINIT / XDATA_INIT | 0xff8000 / 0xff9000 | XINIT 先例 / lld xdata-init.test |
| XSEG | 0x010000 | QEMU stc32g `STC32G_XDATA_BASE`（stc32g.h） |

`--edata-end 0x3fff`（144K 板 16K EDATA）。

## 测试矩阵（src/）

| 用例 | 源形态 | 覆盖 | 断言 |
|---|---|---|---|
| keil-form.c + keil-extern.c | `BYTE xdata buf[256]=…`（payload 记录）、`BYTE xdata zbuf[64]`（仅清零记录）、`char code DEVICEDESC[18]`（CODE ROM 表）、`extern BYTE xdata ExtXbuf[16]`（跨 TU） | Keil 方言官方形态 | 对象级：`.mcs251.XSEG.<sym>` NOBITS + 记录 HI8/MID8/LO8 三元组；链接级：XSEG 地址/单 64K 窗、记录 bank=01/window/尺寸/载荷逐字节、CODE 图像逐字节、DPXL 先于每条 MOVX |
| modern-form.c + modern-extern.c | `unsigned char __xdata`/`char __code` 同覆盖 | 现代限定符形态 | 同上 |
| qemu-fw.c + qemu-ext.c | 官方形态固件（走 QEMU） | 运行时 | OK1 遍历器应用 payload 记录；OK2 仅清零记录；OK3 MOVX 写路径往返；OK4 CODE 读（和 0x0257/异或 0x57）；OK5 跨 TU 记录；哨兵 `XDATA-E2E-PASS` |
| stub-main.c | 字节链镜像的链接锚（CRT 恒 ecall _main） | — | — |
| defect-repro.c（-O2） | `store3` 三连存 | **X2 缺陷修复钉子**（见下节） | check-bytes 断言 -O2 绿色形态（每条 MOVX 紧随各自的 DPXL/DPL/DPH 车道写） |

构建产物在 `build/`（`.gitignore` 规则 `validation/**/build/` 覆盖，不进 git）。

## 运行

```sh
bash build.sh            # 全链 + 146 项字节断言 + QEMU（退出码即裁决）
bash build.sh --no-qemu  # 只跑字节链
# 单独复跑断言：
python3 check-bytes.py build/
```

实测（2026-09-12）：

- check-bytes：**146/146 PASS**（对象级 6 模块放置 + 链接级 3 镜像 + X2 修复后的 -O2 绿色钉子）。
- QEMU：串口 `OK1 OK2 OK3 OK4 OK5 XDATA-E2E-PASS`（写/读/清零/CODE/跨 TU 全过）。

## check-bytes.py 的独立性与解码器

- 纯 stdlib（check-crt-irq.py 同一纪律，不导入 LLVM/lld 产品代码）；自带 ELF32BE
  解析器与 **MCS-251 source 模式指令长度解码器**（从 QEMU `target/mcs51/decode.c`
  的表移植：经典长度表 + source 模式 specifier 长度 + 0xA5 逃逸的逐指令模式翻转）。
  DPXL 断言只在指令边界上裁决，绝不做裸字节扫描。操作数不含 A5 前缀及 opcode，
  只认 byte-direct 模式，避免把 `A5 7A 84`（经典 `mov R2,#84`）误认成 DPXL 写；
  8 项解码自测覆盖单/双前缀、寻址模式与截断输入。
- QEMU 运行前按 map 定位 `fwzero`，用 loader 将全部 32 字节预填为 `0xA7`，
  避免默认零 RAM 让跳过清零的错误遍历器假绿；不改固件/CRT 字节。审查负例将
  walker+0x3D 的 `68 15` 改成 `68 CD`（跳过清零），预填后报 32 个 F2 并 FAIL。
- DPXL 不变量（X2 冻结契约，DESIGN-SUPPLEMENT §2/§3）：每条 MOVX（E0/F0）与上一条
  MOVX 之间必有更近的 `mov 0x84,rN`（7A … 84）区域写。-O0 基线上 3 镜像全过。

## X2 缺陷（X4 发现）——已修复（Glue 焊接，-O2 绿色）

**现象（修复前）**：`-O2` 下同一基本块内多个 XDATA 访问（尤其常量地址、循环全展开），发射
顺序变成按寄存器分流的流——DPXL×N、DPL×N、DPH×N、A×N、然后 **裸 `F0 … F0` 连发**。
DPL/DPH/DPXL 车道写与各自 MOVX 消费者之间跨链错位，全部 MOVX 命中最后写入的指针。

**证据链（修复前）**（src/defect-repro.c，`store3` 三连存，-O2）：

1. 链接后 `_store3` 尾部字节：`… 7A3182 7A7182 7AF182 7A2183 7A6183 7AE183
   7CB8 7CB9 7EFB00 A5E8 F0 F0 F0 1BFC AA` —— 三条 `mov dpl`、三条 `mov dph`
   之后三连裸 `movx @dptr,a`。
2. `-stop-after=finalize-isel` 的 MIR 已是该顺序，且车道写带 `implicit-def dead`
   标记（`dead` 与后续 MOVXAST 的 `implicit $dpl/$dph/$dpxl` 使用矛盾，物理寄存器
   依赖未被调度尊重；MCS251InstrInfo.td 注释宣称的 pin 没有兑现成发射顺序）。
3. QEMU 运行时（-O2 固件）：写读回检查除 i=0 外全败——全部写入落在最后写入指针
   的精确签名。Intel MCS251 手册（intel-um.txt）与 STC32G 手册均无 DPTR 写队列/
   延迟提交语义，QEMU 模型（`target/mcs51/cpu.c` 直接 `dptr[selected]` 写入）即
   权威语义。
4. 缺陷与内存契约无关（默认 v2 布局的 asm 输出同形）。

**根因**：两层叠加。

1. `-O1+` 时 DAG combiner 对不相交的非 volatile 访问做链并行化（DAGCombiner.cpp
   `parallelizeChainedStores`——同基址不相交区间，即 `a3[0..2]` 形态；以及
   `FindBetterChain`——visitSTORE/visitLOAD 的通用改链），三条 store 的线性链变成
   TokenFactor 并行——这在通用 SDAG 层面完全合法（volatile 在 `isSimple()` 检查处
   被排除，所以 volatile 序列从不被并行化、也从不复现错序）。
2. 我们的 LowerStore/LowerLoad 把每条访问展开成 DPL/DPH/DPXL/A **物理寄存器**钉住
   的 5/6 节点机器节点序列，而这些 pin 只存在于 MCInstrDesc 的 implicit 操作数里：
   SelectionDAG 调度器（`ScheduleDAGSDNodes::AddSchedEdges`）只沿 SDValue 操作数建
   依赖边，看不见 implicit 物理寄存器（`CheckForPhysRegDependency` 只认
   CopyToReg/CopyFromReg）。于是 BURR 列调度器在并行链间自由按寄存器分流发射。
   `dead` 标记是果不是因：`InstrEmitter::EmitMachineNode` 的
   `setPhysRegsDeadExcept` 前扫描当前节点之后所有 glued users 的 implicit uses，
   并非只检查最终尾节点——无 glue 时
   车道写的 implicit def 一律标 dead。

**修复**：`buildMOVXByteLoad`/`buildMOVXByteStore`
（llvm/lib/Target/MCS251/MCS251ISelLowering.cpp）把 Glue 边从 MOVXALD→MOV8ra 尾巴
扩展到整个字节序列（dpxl→dpl→dph→(mov a)→MOVX）。Glue 把一次访问合并成单个
调度单元（同一先例：位读序列 MOVCBIT→MOVAI→RLCA→MOV8ra），独立访问仍是独立单元、
可整序重排——没有把整个基本块焊死成全局串行。Glue 链同时让 InstrEmitter 把
MOVX 的 implicit uses 计入 UsedRegs，车道写的 implicit def 发射为**活**定义
（不再是 dead），pre/post-RA 机器调度器与 RA 活跃度都能看见真实的物理寄存器依赖。

**修复验证**：

- lit：llvm/test/CodeGen/MCS251/xdata-o2-order.ll（store/store/load 三函数，
  -O2 asm 断言每条 MOVX 紧随自己的 0x84/dpl/dph/(mov a) 车道写，MIR 断言无
  `implicit-def dead $dpl/$dph/$dpxl/$a`）。
- e2e：check-bytes.py 的 defect-o2 钉子翻转为绿色断言（每条 MOVX 紧随各自的
  DPXL/DPL/DPH 车道写、无裸 MOVX 连发）；-O0 三镜像 + fw.elf/fw.hex 字节级不变
  （md5 逐一比对）；QEMU 全过（OK1–OK5 哨兵）。
- 带外 QEMU 实证：同形态 -O2 探针固件（store3(0x5A,0xC3,0x81) + MOVX 读回）
  串口输出 `R=5AC381 O2-STORE3-PASS`——修复前只有 i=0 正确，修复后三个下标全对。

**处置更新**：缺陷已修复，`defect-o2` 镜像改为 -O2 绿色钉子；三镜像保持 -O0
字节稳定基线（是否整体切 -O2 是后续决策，不在本修复范围）。

## 边界

- **AS3/AS4 指针值能力**（签名/局部指针变量/指针算术）在 ELF 对象路径上被 X3-R1
  v1 身份门禁拒绝（"v2 object output is not implemented"）——官方形态的
  `BYTE xdata *pdat` 运行期操作不在本切片链内；对象放置 + 直接下标 + 存储指针叶子
  （X3 ptrleaf 形态）已覆盖。v2 对象身份获批前这是硬边界。
- **中断/USB**：明确不做（任务书裁定）。
- **QEMU 外设依赖**：无——本固件只用 UART1（SBUF 直写，QEMU test-port 模型）与
  XRAM（stc32g 模型 0x010000 起）。CODE 常量表读走 DR 统一读，无外设依赖。
- BT06 类型兼容层（BYTE/WORD 家族）见
  `validation/mcs251-dialect/include/mcs251_type_compat.h`（台账
  `type-compat.json`，75 名全记账）；SFR 名沿用 sfr-convert 生成头
  （`validation/mcs251-porting/generated/stc32g144k246-*.h`）；sbit 名沿用
  `mcs251_bit_compat.h`。本包固件用 `mcs251_type_compat.h` + `0x99` 直写。
