# temperature-lookup

- 来源：`16-NTC测温度数码管显示/C语言/main.c:45-50,328-370`
- 原函数签名：`u16 get_temperature(u16 adc)`
- 选案：数值查表、二分缩小区间和线性插值。
- 改写：`u8/u16/u32` 为显式宽度 typedef；`u16 code *p` 改为 `static const u16 temp_table[]`，即 `code`→`const`；完整保留原 161 项表；去掉 SFR/中断属性。原算法的 5 次区间缩小、边界哨兵 `0xfffe/0xffff` 和插值公式保留。
- 可观察输出：`temperature_lookup()` 返回放大 10 倍的温度或边界哨兵。
- 主循环/外设：只提取计算函数；ADC 采样入口不进入内核。
- 编译自检：GCC `-c -Wall -Wextra -std=c89` 通过（无 warning）；SDCC `-mmcs251 --model-small -c` 通过。SDCC driver 输出两条环境 warning：`__has_builtin` 与 `__STDC_HOSTED__` redefined；无 kernel error/warning。
- 批量合规补记：删除对 SDCC 宏无差别的冗余条件编译，保留唯一显式宽度 u32 typedef，语义不变。

## 期望重导记录

- 日期：2026-09-05
- Oracle-A 真值 serial：`BaFFFEbFFFFc0000d028Ae0640f0001PASS\n`
- 期望比对：原 d/f 期望值非 Oracle-A 来源，已按宿主真值替换（0x0320→0x028A, 0x000A→0x0001）；其余 checkpoint 与宿主真值一致。

## DUT 状态：SKIP-known-limitation（ro-align 阻塞，mul/div 已解决；2026-09-06）

- kernel 原样保留，不使用 IR shims。`post-ec171ddee+muldiv`（llc MD5 `ff1d986ce8be131cbf961cea6f965fab`）已完成 `i32 mul` 与 `udiv` 选指，原乘除法阻塞已解决。
- 官方 runner `--case temperature-lookup` 暴露后续独立限制：host clang 为只读 `temp_table`（`[161 x i16]`）生成 `align 16`，AsmPrinter 当前只接受 `align 1`，在全局发射阶段明确拒绝；未生成可执行 DUT，不能记三方转正。
- Oracle-A、Oracle-B 本次真实 serial 均为 `BaFFFEbFFFFc0000d028Ae0640f0001PASS\n`；DUT 无 serial。
- PM 裁定：ro-align 独立后续处理，不抹除 alignment，不伪造转正。`.bndry 16` 单独只能对齐模块内偏移；跨模块 CSEG slice 的基址也必须由链接器保证（独立 QEMU 探针测得表最终地址 `0xFC2815`，低四位为 5）。
- 证据：`/home/liu/mcs251-muldiv-alice/acceptance/temperature-lookup/`；对齐调查与布局量化见同沙盒 `alignment-probe.*`、`alignment-layout.json`。
