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

## DUT 状态：SKIP-known-limitation

- PM 批准口径：kernel 原样保留为 `i32 mul` 后端验收样本，不为当前最小后端改写乘法，也不由 runner 的 shift shim 隐式降级。
- 指定 llc（md5 `f6c2f4034f9e32ee61ecfb690b238c93`）在 `%24 = mul nsw i32 %19, %23` 的 SelectionDAG 处终止，首行诊断为 `LLVM ERROR: Cannot select: ... i32 = mul ...`；这是已知 MCS251 后端能力限制，故 DUT 记 `SKIP-known-limitation`，不是算法回归失败。
- 2026-09-05 host 复跑通过，serial 仍为 `Ba01b01cA3dA5e01f00g00h00PASS\n`，未发生变化。
