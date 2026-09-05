# adc-key-step

- 来源：`15-ADC键盘扫描数码管显示键值和调整时间/C语言/main.c:236-278`
- 原函数签名：`void CalculateAdcKey(u16 adc)`
- 选案：ADC 键盘的阈值查找、三次一致确认和长按重复状态机；ADC 外设本身不进入 T1 内核。
- 改写：显式 `u8/u16/u32` typedef；原函数已是单参数；移除 ADC SFR 采样入口，保留 `adc` 作为已采样 12 位值；所有原状态全局改为内核静态状态；没有 `bit`、`code` 或中断属性。
- ISR 状态机去中断提取：本函数在 10 ms 调度路径运行，保留三次一致、首次按下、100 tick 重复和 90 tick 回装语义。
- 可观察输出：`adc_key_code_result()` 和 `adc_key_event_result()` 返回键码/事件状态。
- 编译自检：GCC `-c -Wall -Wextra -std=c89` 通过（无 warning）；SDCC `-mmcs251 --model-small -c` 通过。SDCC driver 输出两条环境 warning：`__has_builtin` 与 `__STDC_HOSTED__` redefined；无 kernel error/warning。

## 期望重导记录

- 日期：2026-09-05
- Oracle-A 真值 serial：`Ba00b00c00d00e00f00g02h01i02j01k02l01m03n01PASS\n`
- 期望比对：各 checkpoint 均与宿主真值一致；替换值：无。
