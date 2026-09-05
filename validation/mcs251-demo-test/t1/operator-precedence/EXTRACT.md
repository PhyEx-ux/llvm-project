# operator-precedence

- 来源：`37-科学计算器使用CDC虚拟液晶屏显示/Resource/alg_caculate.c:34-83`
- 原函数签名：`LEVEL_TYPE alg_compare_level(char operator1, char operator2)`
- 选案：运算符查表、比较分支；覆盖原生多参数定义。
- 改写：显式 `u8/u16/u32` typedef；将两个字符参数打包为 `precedence_args *` 单参数；`LEVEL_TYPE` 和运算级别归一为 u8 常量；保留原运算符级别和 invalid/smaller/same/bigger 返回语义；去 SFR/中断属性。
- 参数打包降维：原生二参数函数保留原签名记录，但执行内核使用一个上下文指针，便于 T1 后端单参 ABI 回归；不是宣称 OSEG 已支持。
- 可观察输出：返回比较结果。
- 编译自检：GCC `-c -Wall -Wextra -std=c89` 通过（无 warning）；SDCC `-mmcs251 --model-small -c` 通过。SDCC driver 输出两条环境 warning：`__has_builtin` 与 `__STDC_HOSTED__` redefined；无 kernel error/warning。
- 批量合规补记：删除对 SDCC 宏无差别的冗余条件编译，保留唯一显式宽度 u32 typedef，语义不变。

## 期望重导记录

- 日期：2026-09-05
- Oracle-A 真值 serial：`Ba01b00c02d02e03f02PASS\n`
- 期望比对：各 checkpoint 均与宿主真值一致；替换值：无。
