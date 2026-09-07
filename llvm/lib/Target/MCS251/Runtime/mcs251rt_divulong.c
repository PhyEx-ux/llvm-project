/*===-- mcs251rt_divulong.c -----------------------------------------------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/*
 * MCS251 运行时：32 位无符号除法。链接符号 __divulong；叶函数（零外部
 * 调用）；第二参数槽 __divulong_PARM_2 = 4B（大端）。
 *
 * 许可：Apache-2.0 WITH LLVM-exception（随本 LLVM fork 分发）。
 *
 * 独立实现声明：依据公开算法描述（恢复余数的移位-减法除法）独立写出，
 * 未逐行参照 SDCC 源码或其编译产物，未参照 compiler-rt、libgcc、newlib
 * 等既有实现文本（除法设计 v4 §10-Q1；算法出处见目录 README.md）。
 *
 * 结果恢复转换依赖（除法设计 v4 §6.1 冻结清单第 5 项）：本文件为无符号
 * 除法，结果恒在无符号域，无"无符号到有符号"恢复转换点；该实现定义依赖
 * 只在四个有符号包装层（mcs251rt_divsint.c、mcs251rt_divslong.c、
 * mcs251rt_modsint.c、mcs251rt_modslong.c）的恢复点生效并逐点注释在位。
 *
 * 红线对照（除法设计 v4 §6.2）：不含除法/取模运算符（因而不是四种除取余
 * IR 指令的来源）；无 64 位类型与 64 位中间量——32 轮循环全部在 32 位
 * 无符号域完成；无浮点；无内联汇编；无 pragma；无静态或全局可变状态。
 * 除数为零与 (INT_MIN, -1) 是 IR 层的 UB，本实现不为其添加特判、陷阱或
 * 返回值契约（§8.3、§10-Q2），算法对其自然运行。
 */

#include "mcs251rt_div.h"

/*
 * 恢复余数除法（restoring division），32 轮，每轮至多移 1 位，循环定数
 * 必然终止。
 *
 * 循环不变量（公开教科书对恢复余数除法的标准论证）：第 i 轮（i 自 0 起）
 * 入口处，真实部分余数 r 等于已消费的 x 高 i 位前缀减去已累计商与 y 的
 * 乘积，故 0 <= r <= 2^i - 1。i <= 31 时 r 的最高位恒为 0，"r 左移一位
 * 并入 x 的最高位"不会超出 32 位——因此 32 位余数寄存器全域够用，
 * 不需要 64 位中间量。
 *
 * 移位纪律（§6.2-3）：在本链冻结整数模型下 uint32_t 即 unsigned int，
 * 提升后类型不变；仍按红线要求写显式 (unsigned) 转换，以防御整型模型
 * 未来变化。每步算术结果显式转换回定宽类型。
 */
uint32_t _divulong(uint32_t x, uint32_t y)
{
    uint32_t q;
    uint32_t r;
    uint32_t t;
    uint32_t i;

    q = (uint32_t)0u;
    r = (uint32_t)0u;
    for (i = (uint32_t)0u; i < (uint32_t)32u; ++i) {
        /* (r, x) 位流整体左移一位：x 的最高位并入 r 的最低位。 */
        r = (uint32_t)(((unsigned)r << 1) | ((unsigned)x >> 31));
        x = (uint32_t)((unsigned)x << 1);
        /* 试减：t 为模 2^32 差值；无符号比较 (r < y) 即借位标志。 */
        t = (uint32_t)((unsigned)r - (unsigned)y);
        if (r >= y) {
            r = t;
            q = (uint32_t)(((unsigned)q << 1) | 1u);
        } else {
            q = (uint32_t)((unsigned)q << 1);
        }
    }
    return q;
}
