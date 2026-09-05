# stack-pop-packed

- 来源：`37-科学计算器使用CDC虚拟液晶屏显示/Resource/alg_linearlist.c:96-112`
- 原函数签名：`LArray_BOOL stack_pop(LArray *pArray, LArrayElem *pElem)`
- 选案：栈空判断、栈顶读取、输出指针写回和长度递减。
- 改写：显式 `u8/u16/u32` typedef；将原生二参数对象指针/输出指针打包为 `stack_pop_args *` 单参数；去除结构体元素的浮点/联合体外壳，只保留此回归所需 u8 元素；无 SFR/中断属性。
- 参数打包降维：这是原生多参函数的 T1 降维形态，保留空栈分支、可空输出指针、读取后递减的算法语义。
- 可观察输出：返回 pop 成功标志，输出缓冲和长度会被修改。
- 编译自检：GCC `-c -Wall -Wextra -std=c89` 通过（无 warning）；SDCC `-mmcs251 --model-small -c` 通过。SDCC driver 输出两条环境 warning：`__has_builtin` 与 `__STDC_HOSTED__` redefined；无 kernel error/warning。
