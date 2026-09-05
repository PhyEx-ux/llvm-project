# stack-pop-packed

- 来源：`37-科学计算器使用CDC虚拟液晶屏显示/Resource/alg_linearlist.c:96-112`
- 原函数签名：`LArray_BOOL stack_pop(LArray *pArray, LArrayElem *pElem)`
- 选案：栈空判断、栈顶读取、输出指针写回和长度递减。
- 改写：显式 `u8/u16/u32` typedef；使用 `stack_pop_args *` 单指针上下文；数据、输出缓冲及空指针判定改为驱动定义全局并用索引访问；以 `u8` 字节数组+`element_size` 保留整个元素的输出拷贝语义，避免引入浮点/联合体；无 SFR/中断属性。
- 参数打包降维：这是原生多参函数的 T1 降维形态，保留空栈分支、可空输出指针、读取后递减的算法语义。
- 可观察输出：返回 pop 成功标志，输出缓冲和长度会被修改。
- 编译自检：GCC `-c -Wall -Wextra -std=c89` 通过（无 warning）；SDCC `-mmcs251 --model-small -c` 通过。SDCC driver 输出两条环境 warning：`__has_builtin` 与 `__STDC_HOSTED__` redefined；无 kernel error/warning。
- 批量合规补记：删除对 SDCC 宏无差别的冗余条件编译，保留唯一显式宽度 u32 typedef，语义不变。

## 期望重导记录

- 日期：2026-09-05
- Oracle-A 真值 serial：`Ba01b01cA3dA5e01f00g00h00PASS\n`
- 期望比对：各 checkpoint 均与宿主真值一致；替换值：无。

## DUT 状态：三方转正（2026-09-06）

- kernel 原样保留；`i32 mul` 已由原生 16×16 部分积 lowering 支持，不使用 IR shims，不改写测试算法。
- 官方 runner `--case stack-pop-packed`：Oracle-A、Oracle-B、DUT serial 逐字一致，均为 `Ba01b01cA3dA5e01f00g00h00PASS\n`。
- llc 冻结：`post-ec171ddee+muldiv`，MD5 `ff1d986ce8be131cbf961cea6f965fab`；QEMU MD5 `6b9edfd0be5618a466c846df0f488faa`。严格链接的 ABI 签名由 `.lk` 内 `-A` 行驱动。
- 证据：`/home/liu/mcs251-muldiv-alice/acceptance/stack-pop-packed/`（三份 `.serial`、`results.json`、原始 IR/REL/HEX 与链接日志）。
