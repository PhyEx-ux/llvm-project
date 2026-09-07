/*===-- mcs251rt_moduint.c ------------------------------------------------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/*
 * MCS251 运行时：16 位无符号取模。链接符号 __moduint；叶函数（零外部
 * 调用——与除法助手互不调用，div 与 rem 同现为两次独立 libcall，
 * §10-Q6）；第二参数槽 __moduint_PARM_2 = 2B（大端）。
 *
 * 许可：Apache-2.0 WITH LLVM-exception（随本 LLVM fork 分发）。
 *
 * 独立实现声明：依据公开算法描述（恢复余数的移位-比较除法）独立写出，
 * 未逐行参照 SDCC 源码或其编译产物，未参照 compiler-rt、libgcc、newlib
 * 等既有实现文本（除法设计 v4 §10-Q1；算法出处见目录 README.md）。
 *
 * 结果恢复转换依赖（除法设计 v4 §6.1 冻结清单第 5 项）：本文件为无符号
 * 取模，结果恒在无符号域，无"无符号到有符号"恢复转换点；该实现定义依赖
 * 只在四个有符号包装层（mcs251rt_divsint.c、mcs251rt_divslong.c、
 * mcs251rt_modsint.c、mcs251rt_modslong.c）的恢复点生效并逐点注释在位。
 *
 * 红线对照（除法设计 v4 §6.2）：不含除法/取模运算符（因而不是四种除取余
 * IR 指令的来源）；无 64 位类型与 64 位中间量；无浮点；无内联汇编；
 * 无 pragma；无静态或全局可变状态。除数为零与 (INT_MIN, -1) 是 IR 层的
 * UB，本实现不为其添加特判、陷阱或返回值契约（§8.3、§10-Q2），
 * 算法对其自然运行。
 */

#include "mcs251rt_div.h"

/*
 * 恢复余数取模（移位-比较），16 轮，每轮至多移 1 位，循环定数必然终止。
 * 与 mcs251rt_divuint.c 同一循环结构，只保留部分余数、不累计商。
 *
 * 循环不变量（公开教科书对恢复余数除法的标准论证）：第 i 轮（i 自 0 起）
 * 入口处，真实部分余数 r 等于已消费的 x 高 i 位前缀减去已累计商与 y 的
 * 乘积，故 0 <= r <= 2^i - 1。i <= 15 时 r <= 0x7FFF，"r 左移一位并入 x
 * 的最高位"不会超出 16 位——因此 16 位余数寄存器全域够用。
 * 对合法除数（y 非零）结束时 0 <= r < y，r 即余数。
 *
 * 移位纪律（§6.2-3）：uint16_t 左操作数进入移位表达式前已被整数提升为
 * 32 位 int，必须先显式转换为 unsigned 再移位；每步算术结果显式截回
 * 定宽类型——到无符号类型的截断取模由 C 标准保证，处处有定义。
 */
uint16_t _moduint(uint16_t x, uint16_t y)
{
    uint16_t r;
    uint16_t t;
    uint32_t i;

    r = (uint16_t)0u;
    for (i = (uint32_t)0u; i < (uint32_t)16u; ++i) {
        /* (r, x) 位流整体左移一位：x 的最高位并入 r 的最低位。 */
        r = (uint16_t)(((unsigned)r << 1) | ((unsigned)x >> 15));
        x = (uint16_t)((unsigned)x << 1);
        /* 试减：t 为模 2^16 差值；无符号比较 (r < y) 即借位标志。 */
        t = (uint16_t)((unsigned)r - (unsigned)y);
        if (r >= y) {
            r = t;
        }
    }
    return r;
}
