# stack-packed

- 来源：`37-科学计算器使用CDC虚拟液晶屏显示/Resource/alg_linearlist.c:74-86`
- 原函数签名：`LArray_BOOL stack_push(LArray *pArray, const LArrayElem elem)`
- 选案：栈边界判断、指针写入和长度更新。
- 改写：显式 `u8/u16/u32` typedef；为避免结构体按值和多参数 ABI，使用 `stack_args *` 单指针上下文，将原 `pArray` 与 `elem` 打包为一个上下文指针；元素以 `const u8 *`+`element_size` 作为不透明字节对象，保留原整个 `LArrayElem` 赋值的拷贝语义；无 SFR/中断；`code` 无原函数依赖。
- 参数打包降维：明确记录原生二参数（对象指针 + 值元素）被压成一个上下文指针，算法分支与更新顺序保持。
- 可观察输出：返回 push 成功标志并更新输出数组/长度。
- 编译自检：GCC `-c -Wall -Wextra -std=c89` 通过（无 warning）；SDCC `-mmcs251 --model-small -c` 通过。SDCC driver 输出两条环境 warning：`__has_builtin` 与 `__STDC_HOSTED__` redefined；无 kernel error/warning。
