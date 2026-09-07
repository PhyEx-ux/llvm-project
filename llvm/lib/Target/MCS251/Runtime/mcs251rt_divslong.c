/*===-- mcs251rt_divslong.c -----------------------------------------------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/*
 * MCS251 运行时：32 位有符号除法。链接符号 __divslong；非叶（恰有一次
 * 对 __divulong 的显式调用）；第二参数槽 __divslong_PARM_2 = 4B（大端）。
 *
 * 许可：Apache-2.0 WITH LLVM-exception（随本 LLVM fork 分发）。
 *
 * 独立实现声明：依据公开算法描述（恢复余数的移位-减法除法 + 标准符号
 * 恢复律）独立写出，未逐行参照 SDCC 源码或其编译产物，未参照
 * compiler-rt、libgcc、newlib 等既有实现文本（除法设计 v4 §10-Q1；
 * 算法出处见目录 README.md）。
 *
 * 结果恢复转换依赖（除法设计 v4 §6.1 冻结清单第 5 项）：本文件恰有一处
 * 无符号到有符号的恢复转换点（函数末尾 return），依赖本链冻结 Clang 对
 * N=32 超范围无符号到有符号转换"保留低 32 位、按二补码解释"的行为
 * （C11 6.3.1.3p3 实现定义），非可移植性质。代表位型：商 -3 为
 * 0xFFFFFFFD、商 -1 为 0xFFFFFFFF、商 -2147483648 为 0x80000000。
 * 工具链身份或 §6.1 第 1-4 项任一编译条件变化即触发该依赖的全量重验。
 *
 * 红线对照（除法设计 v4 §6.2）：绝对值在无符号域构造（§6.2-1，含
 * INT32_MIN 全域有定义）；显式调用对应 unsigned helper（§6.2-2）；
 * 不含除法/取模运算符（因而不是四种除取余 IR 指令的来源）；无 64 位
 * 类型与中间量；无浮点；无内联汇编；无 pragma；无静态或全局可变状态。
 * 除数为零与 (INT_MIN, -1) 是 IR 层的 UB，本实现不为其添加特判、陷阱或
 * 返回值契约（§8.3、§10-Q2），算法对其自然运行。
 */

#include "mcs251rt_div.h"

int32_t _divslong(int32_t x, int32_t y)
{
    uint32_t ux;
    uint32_t uy;
    uint32_t uq;
    uint32_t rq;

    /*
     * 归一化（§6.2-1）：先做 int32_t 到 uint32_t 的转换——到无符号类型
     * 的转换取模，C 标准保证，处处有定义；再在无符号域取负：
     * (uint32_t)(0u - ux) 在 32 位无符号域计算（模 2^32），含
     * x == INT32_MIN 全域有定义。不允许先在有符号域取负——对
     * INT32_MIN 那是 C 未定义行为。
     */
    ux = (uint32_t)x;
    uy = (uint32_t)y;
    if (x < 0) {
        ux = (uint32_t)(0u - ux);
    }
    if (y < 0) {
        uy = (uint32_t)(0u - uy);
    }

    /* 显式调用对应 unsigned helper（§6.2-2；精确宽度原型经头文件固定）。 */
    uq = _divulong(ux, uy);

    /*
     * 商符号律（C99 与 LLVM 一致的截断除法）：两操作数符号相异则商为负。
     * 取负在无符号域完成后才做恢复转换。
     */
    rq = uq;
    if ((x < 0) != (y < 0)) {
        rq = (uint32_t)(0u - uq);
    }

    /*
     * 结果恢复转换点（本文件唯一一处）：依赖冻结工具链的模 2^N 转换行为
     * （C11 6.3.1.3p3 实现定义），非可移植性质（除法设计 v4 §6.1 冻结
     * 清单第 5 项、§6.2-4）。
     */
    return (int32_t)rq;
}
