/*===-- mcs251rt_divsint.c ------------------------------------------------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/*
 * MCS251 运行时：16 位有符号除法。链接符号 __divsint；非叶（恰有一次
 * 对 __divuint 的显式调用）；第二参数槽 __divsint_PARM_2 = 2B（大端）。
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
 * N=16 超范围无符号到有符号转换"保留低 16 位、按二补码解释"的行为
 * （C11 6.3.1.3p3 实现定义），非可移植性质。代表位型：商 -3 为 0xFFFD、
 * 商 -1 为 0xFFFF、商 -32768 为 0x8000。工具链身份或 §6.1 第 1-4 项任一
 * 编译条件变化即触发该依赖的全量重验。
 *
 * 红线对照（除法设计 v4 §6.2）：绝对值在无符号域构造（§6.2-1，含
 * INT16_MIN 全域有定义）；显式调用对应 unsigned helper（§6.2-2）；
 * 不含除法/取模运算符（因而不是四种除取余 IR 指令的来源）；无 64 位
 * 类型与中间量；无浮点；无内联汇编；无 pragma；无静态或全局可变状态。
 * 除数为零与 (INT_MIN, -1) 是 IR 层的 UB，本实现不为其添加特判、陷阱或
 * 返回值契约（§8.3、§10-Q2），算法对其自然运行。
 */

#include "mcs251rt_div.h"

int16_t _divsint(int16_t x, int16_t y)
{
    uint16_t ux;
    uint16_t uy;
    uint16_t uq;
    uint16_t rq;

    /*
     * 归一化（§6.2-1）：先做 int16_t 到 uint16_t 的转换——到无符号类型
     * 的转换取模，C 标准保证，处处有定义；再在无符号域取负：
     * (uint16_t)(0u - ux) 按提升规则在 32 位无符号域计算后截回 16 位
     * （模 2^16），含 x == INT16_MIN 全域有定义。
     * 次序说明（复审修正 2026-09-07，红线 §6.2-1 不变）：在本链整数模型
     * （int=32）下 int16_t 运算先提升为 32 位 int，对 INT16_MIN 在有符号
     * 域取负其实可表示、并非 UB——但该定义性依赖提升域宽度（int 为 16 位
     * 的整数模型下它立即是 UB）。无符号域归一化对任何整数模型全域有定义、
     * 不依赖提升宽度，故按红线固定采用，不采用有符号取负捷径。
     */
    ux = (uint16_t)x;
    uy = (uint16_t)y;
    if (x < 0) {
        ux = (uint16_t)(0u - ux);
    }
    if (y < 0) {
        uy = (uint16_t)(0u - uy);
    }

    /* 显式调用对应 unsigned helper（§6.2-2；精确宽度原型经头文件固定）。 */
    uq = _divuint(ux, uy);

    /*
     * 商符号律（C99 与 LLVM 一致的截断除法）：两操作数符号相异则商为负。
     * 取负在无符号域完成后才做恢复转换。
     */
    rq = uq;
    if ((x < 0) != (y < 0)) {
        rq = (uint16_t)(0u - uq);
    }

    /*
     * 结果恢复转换点（本文件唯一一处）：依赖冻结工具链的模 2^N 转换行为
     * （C11 6.3.1.3p3 实现定义），非可移植性质（除法设计 v4 §6.1 冻结
     * 清单第 5 项、§6.2-4）。
     */
    return (int16_t)rq;
}
