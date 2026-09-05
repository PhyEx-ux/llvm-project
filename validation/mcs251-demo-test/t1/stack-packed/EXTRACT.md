# stack-packed

- 来源：`37-科学计算器使用CDC虚拟液晶屏显示/Resource/alg_linearlist.c:74-86`
- 原函数签名：`LArray_BOOL stack_push(LArray *pArray, const LArrayElem elem)`
- 选案：栈边界判断、指针写入和长度更新。
- 改写：显式 `u8/u16/u32` typedef；为避免结构体按值和多参数 ABI，使用 `stack_args *` 单指针上下文；数据与元素改为驱动定义的 `stack_data[]`/`stack_element[]`，内核用数组下标直接拷贝；元素以 `const u8 *`+`element_size` 作为不透明字节对象，保留原整个 `LArrayElem` 赋值的拷贝语义；无 SFR/中断；`code` 无原函数依赖。
- 参数打包降维：明确记录原生二参数（对象指针 + 值元素）被压成一个上下文指针，算法分支与更新顺序保持。
- 可观察输出：返回 push 成功标志并更新输出数组/长度。
- 编译自检：GCC `-c -Wall -Wextra -std=c89` 通过（无 warning）；SDCC `-mmcs251 --model-small -c` 通过。SDCC driver 输出两条环境 warning：`__has_builtin` 与 `__STDC_HOSTED__` redefined；无 kernel error/warning。
- 批量合规补记：删除对 SDCC 宏无差别的冗余条件编译，保留唯一显式宽度 u32 typedef，语义不变。

## 期望重导记录

- 日期：2026-09-05
- Oracle-A 真值 serial：`Ba01b01c01d03e01f02g09h00i02j00k08PASS\n`
- 期望比对：各 checkpoint 均与宿主真值一致；替换值：无。

## DUT 状态：三方转正（2026-09-06）

- kernel 原样保留；`i32 mul` 已由原生 16×16 部分积 lowering 支持，不使用 IR shims，不改写测试算法。
- 官方 runner `--case stack-packed`：Oracle-A、Oracle-B、DUT serial 逐字一致，均为 `Ba01b01c01d03e01f02g09h00i02j00k08PASS\n`。
- llc 冻结：`post-ec171ddee+muldiv`，MD5 `ff1d986ce8be131cbf961cea6f965fab`；QEMU MD5 `6b9edfd0be5618a466c846df0f488faa`。严格链接的 ABI 签名由 `.lk` 内 `-A` 行驱动。
- 证据：`/home/liu/mcs251-muldiv-alice/acceptance/stack-packed/`（三份 `.serial`、`results.json`、原始 IR/REL/HEX 与链接日志）。
