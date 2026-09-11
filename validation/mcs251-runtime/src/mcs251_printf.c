/*===-- mcs251_printf.c ---------------------------------------------------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/*
 * MCS251 libc 子集 —— printf/sprintf 定参实现。
 *
 * ABI 约束（见 mcs251_libc.h）：固定 6 参槽，无 variadic。
 * 格式符按语料频率实现：d/i u x X s c f g %%
 * 支持基本宽度（%5d）和精度（%.2f）。
 *
 * 设计：printf 和 sprintf 共享同一格式化引擎，区别只在输出目标
 * （printf 调 putchar，sprintf 写全局缓冲区）。
 *
 * 红线：纯 C；无 64 位；无浮点运算符；无内联汇编；无 pragma。
 * 全局缓冲区用 uint8_t 数组（后端允许全局整数数组）。
 */

#include "mcs251_libc.h"

/* putchar 由使用方（固件）提供：本 libc 子集经用户范围裁剪后不再
 * 自带 putchar（mcs251_libc.h 无此原语）。私有 ABI：接受单个 char，
 * 不返回值（与 stdio 的 int putchar(int) 不同，见文件头 ABI 约束）。
 * 2026-09-09 Kazimi 修复：原草稿直接调用未声明的 putchar（C11 隐式
 * 声明在 -Werror 下拒绝）。 */
extern void putchar(char c);

/* ---- sprintf 全局缓冲区 ----
 * 2026-09-09 Kazimi：256 -> 64。sdld 经典 DATA 页共 256B（与 REG_BANK/
 * BIT_BANK/OSEG/DSEG 共享），256B 缓冲区必然放不下（链接直接失败）；
 * 64B 足够验收用例的行长（最长样例 < 40B）。溢出防护逻辑不变：
 * 超长截断 + 强制 NUL。
 * 2026-09-09 Kazimi 二次收紧：64 -> 24。sdld 源码（lkarea.c lnksect2）
 * 对 DSEG 硬上限 0x80=128B（经典 8051 直接寻址页，无旗标可放宽）；
 * 扣除 REG_BANK_0(8) 与除法运行时 OSEG(8) 后预算 112B。原布局
 * 175B 超限。本文件整体去 g_args/g_nargs（-28）+ out_* 参数打包
 * （-11）+ 缓冲区 64->24（-40）后 DSEG=96B。验收语料最长 sprintf
 * 行 17 字符 + NUL = 18B，24B 仍留 6B 余量。 */
#define SPRINTF_BUFSZ 24
static uint8_t g_sprintf_buf[SPRINTF_BUFSZ];
static uint32_t g_sprintf_pos;

void sprintf_reset(void)
{
    g_sprintf_pos = 0u;
}

uint32_t sprintf_len(void)
{
    return g_sprintf_pos;
}

char sprintf_getc(uint32_t idx)
{
    if (idx >= SPRINTF_BUFSZ) return 0;
    return (char)g_sprintf_buf[idx];
}

const char* sprintf_str(void)
{
    /* 确保以 NUL 结尾 */
    if (g_sprintf_pos < SPRINTF_BUFSZ) {
        g_sprintf_buf[g_sprintf_pos] = 0;
    } else {
        g_sprintf_buf[SPRINTF_BUFSZ - 1u] = 0;
    }
    return (const char*)(uintptr_t)g_sprintf_buf;
}

/* ---- 输出目标抽象 ----
 * 用函数指针模式不现实（后端限制），改用全局模式标志。
 * 2026-09-09 Kazimi：g_output_mode 声明为 volatile——否则 `== 0u`
 * 比较会被 clang instcombine 窄化成 i1 类型 load（i32 全局），MCS251
 * 后端无 i1 访存选择模式（llc 崩溃 Cannot select）。与本文件既有的
 * volatile 防 switch 综合手法同类。 */
static volatile uint32_t g_output_mode;  /* 0=printf(putchar), 1=sprintf(buf) */

/* 内部输出一个字符。noinline：防止全局变量读取被内联到调用方后
 * 产生后端无法 select 的 load-from-global DAG 节点。 */
__attribute__((noinline))
static void out_char(char c)
{
    if (g_output_mode == 0u) {
        putchar(c);
    } else {
        if (g_sprintf_pos < SPRINTF_BUFSZ) {
            g_sprintf_buf[g_sprintf_pos] = (uint8_t)c;
            g_sprintf_pos++;
        }
    }
}

__attribute__((noinline))
static void out_str(const char* s)
{
    while (*s != 0) {
        out_char(*s);
        s++;
    }
}

/* ---- 数字格式化 ----
 * 2026-09-09 Kazimi：out_* 系列第二参数打包为单 word（DSEG 预算，
 * 见文件头）：每个函数只留一个 _PARM_2 槽（4B）。
 *   wp  = (pad<<16)|width        （out_uint / out_int / 小写 out_hex）
 *   wpu = 0x80000000|(pad<<16)|width （大写 out_hex）
 *   fs  = 0x80000000|frac_digits （%g strip；%f 不带位） */

/* 输出无符号十进制 */
static void out_uint(uint32_t v, uint32_t wp)
{
    char buf[11];
    uint32_t pos = 0u;
    uint32_t i;
    uint32_t min_width = wp & 0xFFFFu;
    char pad = (char)(wp >> 16);

    if (v == 0u) {
        buf[pos] = '0';
        pos++;
    } else {
        while (v != 0u) {
            buf[pos] = (char)('0' + (v % 10u));
            pos++;
            v = v / 10u;
        }
    }
    /* buf 是逆序，输出时反转 + 补 padding */
    while (pos < min_width) {
        out_char(pad);
        min_width--;
    }
    for (i = pos; i > 0u; i--) {
        out_char(buf[i - 1u]);
    }
}

/* 输出有符号十进制 */
static void out_int(int32_t v, uint32_t wp)
{
    uint32_t uv;
    uint32_t sign_width = 0u;
    uint32_t min_width = wp & 0xFFFFu;
    char pad = (char)(wp >> 16);

    if (v < 0) {
        /* 负数：先输出符号（或占位），再输出绝对值 */
        if (pad == '0') {
            out_char('-');
            sign_width = 1u;
        } else {
            /* 空格 padding：符号占一位宽度 */
            sign_width = 1u;
            if (min_width > 0u) min_width--;
            /* 先补 padding 再输出符号 */
            uv = (uint32_t)(0u - (uint32_t)v);
            /* 计算 digits 长度来决定 padding */
            {
                char tmpbuf[11];
                uint32_t tpos = 0u;
                if (uv == 0u) { tmpbuf[tpos]='0'; tpos++; }
                else { while(uv!=0u){tmpbuf[tpos]=(char)('0'+(uv%10u));tpos++;uv=uv/10u;} }
                while (tpos < min_width) { out_char(' '); min_width--; }
                out_char('-');
                {
                    uint32_t ti;
                    for (ti=tpos; ti>0u; ti--) out_char(tmpbuf[ti-1u]);
                }
                return;
            }
        }
        uv = (uint32_t)(0u - (uint32_t)v);
    } else {
        uv = (uint32_t)v;
    }
    /* 2026-09-10 修复：宽度扣符号位用饱和减法。原 (min_width -
     * sign_width) 在 width==0（%0d 的 '0' 被当 flags、宽度域为 0）
     * 或 width<1 时无符号下溢，&0xFFFF 后成 65535 列零填充
     * （host 自测实锤：%0d 打 -1 输出 '-' + 65535 个 '0'）。
     * 同类宽度算术排查：out_uint/out_hex 的 `while (pos < min_width)
     * min_width--` 递减的是局部副本且有 pos<min_width 前置条件，
     * 不会下溢；out_int 空格路径的 min_width-- 带 >0 保护，同样安全。 */
    {
        uint32_t body_width = (min_width > sign_width)
                                ? (min_width - sign_width) : 0u;
        out_uint(uv, ((uint32_t)(uint8_t)pad << 16) | (body_width & 0xFFFFu));
    }
}

/* 输出十六进制 */
static void out_hex(uint32_t v, uint32_t wpu)
{
    char buf[9];
    uint32_t pos = 0u;
    uint32_t i;
    uint32_t uppercase = wpu >> 31;
    uint32_t min_width = wpu & 0xFFFFu;
    char pad = (char)(wpu >> 16);
    const char* digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

    if (v == 0u) {
        buf[pos] = '0';
        pos++;
    } else {
        while (v != 0u) {
            buf[pos] = digits[v & 0xFu];
            pos++;
            v = v >> 4;
        }
    }
    while (pos < min_width) {
        out_char(pad);
        min_width--;
    }
    for (i = pos; i > 0u; i--) {
        out_char(buf[i - 1u]);
    }
}

/* 输出 f32 浮点（bit pattern 传入） */
static void out_float(uint32_t bits, uint32_t fs)
{
    uint32_t sign, exp, mant;
    uint32_t int_part;
    char ibuf[11];
    uint32_t ipos = 0u;
    /* 2026-09-09 Kazimi 绕道（后端 bug 取证 parm_probe2/fD）：
     * `fs & 0xFFu` 对直接参数在 -O0 下被错译为固定直接寻址载荷
     * （mov r2,0x81 / mov r6,0x85，无重定位不对应任何符号）并破坏
     * 邻近寄存器分配。`x & 0xFF` 改写为移位+减法等价式：
     * fs - ((fs>>8)<<8)。&0xF / &0xFFFF / >>16 / >>31 实测无此问题。 */
    uint32_t frac_digits = fs - ((fs >> 8) << 8);
    uint32_t strip_trailing = fs >> 31;

    /* 2026-09-09 Kazimi 修复：sign 提取提前——原 Inf 分支在 sign 赋值
     * 前引用它（读未初始化变量，-Inf 可能丢负号或行为未定义）。 */
    sign = bits >> 31;

    /* NaN / Inf 处理 */
    if ((bits & 0x7FFFFFFFu) == 0x7F800000u) {
        if (sign) out_char('-');
        out_str("inf");
        return;
    }
    if (((bits >> 23) & 0xFFu) == 0xFFu && (bits & 0x7FFFFFu) != 0u) {
        out_str("nan");
        return;
    }

    exp = (bits >> 23) & 0xFFu;
    mant = bits & 0x7FFFFFu;

    if (sign) out_char('-');

    /* 零 */
    if ((bits & 0x7FFFFFFFu) == 0u) {
        uint32_t zi;
        out_char('0');
        if (frac_digits > 0u) {
            out_char('.');
            for (zi = 0u; zi < frac_digits; zi++) out_char('0');
        }
        return;
    }

    /* 加入隐含位 */
    if (exp != 0u) mant = mant | 0x00800000u;
    else exp = 1u;

    /* 实际值 = mant * 2^(exp-150)（150 = 23+127）
     * 2026-09-09 Kazimi 重写小数生成：原实现用 32x32->64 位长乘法算
     * frac_bits*10^frac_digits/2^shift，但丢失 mid<<16 的进位（"2.5"
     * 输出成 "2.491808"）。
     * 2026-09-10 二次重写（修复 Alice review P1）：上一版对
     * shift >= 32 直接把小数位全置零（%.6f 打 0.001f 得 "0.000000"，
     * 应 "0.001000"——0.001f 的 shift=33），且 shift 29..31 时 fb*10
     * 会溢出 32 位累加器。改为分段提取 + guard 位舍入（舍入规则
     * round-to-nearest-even，与 glibc 对齐；上一版纯截断，如
     * 123.4499969... 打成 123.449996，glibc 为 123.449997）：
     *   shift <= 28：逐位 ×10 提取（fb < 2^shift <= 2^28，×10 < 2^32
     *                不溢出），全程整数精确；多取一位 guard 位 + 精确
     *                余量，恰半局可精确判定；
     *   shift >  28：值 < 2^-4，小数先折成 F28 定点（frac28 =
     *                mant >> (shift-28)，初始截断误差 < 2^-28 ≈
     *                3.7e-9），再逐位 ×10 >> 28 提取。
     * 已知限界：shift > 28 时因 F28 初始截断，真值距舍入边界
     * < 2^-28 的近平局末位可能与正确舍入差 1（约 0.4% 概率；
     * "截断伪平局"已按上法纠正，此处仅剩 guard 落在 4/5 边界
     * 之下的不可判定窗口）；shift <= 28 路径完全精确。进位链
     * 999..9 -> 整数部分 +1。 */
    {
        int32_t unbiased = (int32_t)exp - 150;
        char fbuf2[8];
        uint32_t fpos2 = 0u;
        uint32_t fi2;
        uint32_t guard = 0u;   /* 第 frac_digits+1 位（舍入判据） */
        uint32_t rem = 0u;     /* guard 之后的余量（0 = 恰半局） */
        uint32_t tie_clipped = 0u;  /* F28 路径截去位非零（见舍入注释） */

        if (frac_digits > 6u) frac_digits = 6u;  /* fbuf2 容量保护 */

        if (unbiased >= 0) {
            /* 整数部分：mant << unbiased。值恰为整数，小数恒为 0。 */
            if (unbiased <= 8) {
                int_part = mant << unbiased;
            } else {
                int_part = 0xFFFFFFFFu;  /* 溢出，显示大数 */
            }
            for (fi2 = 0u; fi2 < frac_digits; fi2++) fbuf2[fi2] = '0';
            fpos2 = frac_digits;
        } else {
            /* unbiased < 0：值 = num0 / 2^shift（shift = -unbiased,
             * 1..149）。整数部分 = mant >> shift（shift >= 32 恒为 0，
             * 移位计数避开 >=32 的 UB）；小数分子 num0：
             * shift <= 24 取 mant 低 shift 位，shift > 24 时 mant
             * 整体都在小数点下（mant < 2^24）。 */
            uint32_t shift = (uint32_t)(-unbiased);
            uint32_t num0;
            uint32_t i;

            int_part = (shift >= 32u) ? 0u : (mant >> shift);

            if (shift <= 24u) {
                num0 = mant & ((1u << shift) - 1u);
            } else {
                num0 = mant;
            }

            if (shift <= 28u) {
                /* 精确路径：fb*10 < 2^28*10 < 2^32，永不溢出。
                 * 多提取一位 guard（fbuf2[8] 容量 >= 6+1）。 */
                uint32_t fb = num0;
                uint32_t mask = (1u << shift) - 1u;
                for (i = 0u; i <= frac_digits; i++) {
                    fb = fb * 10u;
                    if (i < frac_digits) {
                        fbuf2[i] = (char)('0' + (fb >> shift));
                    } else {
                        guard = fb >> shift;
                    }
                    fb = fb & mask;
                }
                rem = fb;  /* 精确余量 */
            } else {
                /* F28 半精确路径：frac28 = value * 2^28 截断。
                 * t = frac28*10 < 2^28*10 < 2^32，安全。 */
                uint32_t frac28;
                uint32_t sh28 = shift - 28u;
                frac28 = (sh28 >= 32u) ? 0u : (num0 >> sh28);
                /* 记录是否截掉了非零位：截断把高于半局的真值削到
                 * "恰半"时，rem==0 不再代表真恰半（见舍入注释） */
                if (sh28 < 32u && (num0 & ((1u << sh28) - 1u)) != 0u) {
                    tie_clipped = 1u;
                }
                for (i = 0u; i <= frac_digits; i++) {
                    uint32_t t = frac28 * 10u;
                    if (i < frac_digits) {
                        fbuf2[i] = (char)('0' + (t >> 28));
                    } else {
                        guard = t >> 28;
                    }
                    frac28 = t & 0x0FFFFFFFu;
                }
                rem = frac28;
            }
            fpos2 = frac_digits;
        }

        /* 舍入：guard > 5 进位；guard < 5 舍去；guard == 5 时余量
         * 非零进位；恰半保留偶数位（round-to-nearest-even，与
         * glibc printf 对精确十进制平局的行为一致，实测 0.25→"0.2"、
         * 0.5→"0"、2.5→"2"、0.0078125→"0.007812"）。
         * 例外：F28 路径截去位非零（tie_clipped）时，rem==0 是截断
         * 伪影——真值 = 截断值 + δ ∈ (半局, 半局+2^-28]，必须进位
         * （如 0.0156250009 → "0.01563"）。真恰半要求展开在
         * guard 位终结，即 shift <= 31 且截去位全零，此时
         * tie_clipped==0，仍走精确半偶分支。 */
        {
            uint32_t round_up;
            if (guard > 5u) {
                round_up = 1u;
            } else if (guard < 5u) {
                round_up = 0u;
            } else if (rem != 0u) {
                round_up = 1u;
            } else if (tie_clipped != 0u) {
                round_up = 1u;  /* 截断伪平局：真值必在半局之上 */
            } else {
                /* 恰半：看保留部分的末位奇偶（0 舍 1 进） */
                uint32_t last = (frac_digits > 0u)
                    ? (uint32_t)(fbuf2[frac_digits - 1u] - '0')
                    : (int_part % 10u);
                round_up = last & 1u;
            }
            if (round_up != 0u) {
                int32_t j = (int32_t)frac_digits - 1;
                for (;;) {
                    if (j < 0) {
                        /* 999..9 全进位：小数清零、整数部分 +1 */
                        int_part = int_part + 1u;
                        for (fi2 = 0u; fi2 < frac_digits; fi2++) {
                            fbuf2[fi2] = '0';
                        }
                        break;
                    }
                    if (fbuf2[j] == '9') {
                        fbuf2[j] = '0';
                        j--;
                    } else {
                        fbuf2[j] = (char)(fbuf2[j] + 1);
                        break;
                    }
                }
            }
        }

        /* 输出整数部分 */
        if (int_part == 0u) {
            ibuf[ipos] = '0';
            ipos++;
        } else {
            while (int_part != 0u) {
                ibuf[ipos] = (char)('0' + (int_part % 10u));
                ipos++;
                int_part = int_part / 10u;
            }
        }
        {
            uint32_t ti;
            for (ti = ipos; ti > 0u; ti--) out_char(ibuf[ti - 1u]);
        }

        /* 输出小数部分 */
        if (frac_digits > 0u) {
            out_char('.');
            /* strip_trailing（%g）：去掉末尾全部为零的位 */
            uint32_t keep = fpos2;
            if (strip_trailing) {
                while (keep > 0u && fbuf2[keep - 1u] == '0') keep--;
                if (keep == 0u) keep = 1u;  /* 至少留一位（"x.0"） */
            }
            for (fi2 = 0u; fi2 < keep; fi2++) out_char(fbuf2[fi2]);
        }
    }
}



/* ---- 格式化引擎 ----
 * 解析 fmt，按序从 PARM 静态槽取实参输出。
 * output_mode 已由调用方设置。
 *
 * 2026-09-09 Kazimi：去 g_args[6]/g_nargs（DSEG 预算，见文件头）。
 * 实参直接读 printf/sprintf 的 _PARM_2..7 静态槽——引擎本就运行在
 * 参数槽已写好之后，语义等价（本就非重入）。printf/sprintf 体内保留
 * 镜像回存：目标侧后端本就把参数放同一批槽（回存幂等冗余），宿主
 * oracle 侧则靠回存把实参物化进槽，两端同源。
 * 取参用 if 链 + volatile 序号（k），防止 clang 把多值比较综合成
 * switch IR（switch -> br_jt 后端不支持，同文件既有手法）。 */
/* C 标识符不带前导下划线：目标 '_' 前缀后恰为后端参数槽符号
 * _printf_PARM_2（多一层下划线会变成另一个符号 __printf_PARM_2，
 * 链接期未定义）。 */
extern volatile uint32_t printf_PARM_2, printf_PARM_3, printf_PARM_4,
    printf_PARM_5, printf_PARM_6, printf_PARM_7;
extern volatile uint32_t sprintf_PARM_2, sprintf_PARM_3, sprintf_PARM_4,
    sprintf_PARM_5, sprintf_PARM_6, sprintf_PARM_7;

__attribute__((noinline))
static uint32_t get_arg(uint32_t idx)
{
    volatile uint32_t k = idx;
    uint32_t m = g_output_mode;

    if (k == 0u) return (m == 0u) ? printf_PARM_2 : sprintf_PARM_2;
    if (k == 1u) return (m == 0u) ? printf_PARM_3 : sprintf_PARM_3;
    if (k == 2u) return (m == 0u) ? printf_PARM_4 : sprintf_PARM_4;
    if (k == 3u) return (m == 0u) ? printf_PARM_5 : sprintf_PARM_5;
    if (k == 4u) return (m == 0u) ? printf_PARM_6 : sprintf_PARM_6;
    return (m == 0u) ? printf_PARM_7 : sprintf_PARM_7;
}

__attribute__((noinline))
static void format_engine(const char* fmt)
{
    uint32_t ai = 0u;
    const char* p = fmt;

    for (;;) {
        /* 用 volatile 读取当前字符，阻止 clang 把 *p==0 / *p=='%' 等多值
         * 比较综合成 switch IR（switch -> br_jt 后端不支持）。 */
        volatile uint32_t cur = (uint32_t)*p;
        if (cur == 0u) break;
        if (cur != (uint32_t)'%') {
            out_char((char)cur);
            p++;
            continue;
        }
        p++;  /* 跳过 % */

        /* 解析 flags（每个比较用 volatile 避免链式 switch） */
        volatile uint32_t ch0 = (uint32_t)*p;
        char pad = ' ';
        if (ch0 == (uint32_t)'0') { pad = '0'; p++; ch0 = (uint32_t)*p; }
        if (ch0 == (uint32_t)'-') { p++; ch0 = (uint32_t)*p; }
        if (ch0 == (uint32_t)'+') { p++; ch0 = (uint32_t)*p; }
        if (ch0 == (uint32_t)' ') { p++; ch0 = (uint32_t)*p; }
        if (ch0 == (uint32_t)'#') { p++; ch0 = (uint32_t)*p; }

        /* 解析宽度 */
        uint32_t width = 0u;
        {
            volatile uint32_t wd = (uint32_t)*p;
            while (wd >= (uint32_t)'0' && wd <= (uint32_t)'9') {
                width = width * 10u + (wd - (uint32_t)'0');
                p++;
                wd = (uint32_t)*p;
            }
        }

        /* 解析精度 */
        uint32_t precision = 6u;
        uint32_t has_precision = 0u;
        {
            volatile uint32_t pd = (uint32_t)*p;
            if (pd == (uint32_t)'.') {
                p++;
                precision = 0u;
                has_precision = 1u;
                volatile uint32_t pp = (uint32_t)*p;
                while (pp >= (uint32_t)'0' && pp <= (uint32_t)'9') {
                    precision = precision * 10u + (pp - (uint32_t)'0');
                    p++;
                    pp = (uint32_t)*p;
                }
            }
        }

        /* 解析长度修饰符（用 volatile 避免链式 switch） */
        {
            volatile uint32_t lm = (uint32_t)*p;
            if (lm == (uint32_t)'l') {
                p++;
                volatile uint32_t lm2 = (uint32_t)*p;
                if (lm2 == (uint32_t)'l') p++;
            } else if (lm == (uint32_t)'h') {
                p++;
                volatile uint32_t lm2 = (uint32_t)*p;
                if (lm2 == (uint32_t)'h') p++;
            }
        }

        /* 解析格式符 */
        {
            volatile uint32_t fc = (uint32_t)*p;
            if (fc == 0u) break;
            (void)has_precision;

            if (fc == (uint32_t)'%') {
                out_char('%');
                p++;
            } else if (fc == (uint32_t)'d' || fc == (uint32_t)'i') {
                if (ai < 6u) {
                    out_int((int32_t)get_arg(ai), ((uint32_t)(uint8_t)pad << 16) | width);
                    ai++;
                }
                p++;
            } else if (fc == (uint32_t)'u') {
                if (ai < 6u) {
                    out_uint(get_arg(ai), ((uint32_t)(uint8_t)pad << 16) | width);
                    ai++;
                }
                p++;
            } else if (fc == (uint32_t)'x') {
                if (ai < 6u) {
                    out_hex(get_arg(ai), ((uint32_t)(uint8_t)pad << 16) | width);
                    ai++;
                }
                p++;
            } else if (fc == (uint32_t)'X') {
                if (ai < 6u) {
                    out_hex(get_arg(ai), 0x80000000u | ((uint32_t)(uint8_t)pad << 16) | width);
                    ai++;
                }
                p++;
            } else if (fc == (uint32_t)'s') {
                if (ai < 6u) {
                    out_str((const char*)(uintptr_t)get_arg(ai));
                    ai++;
                }
                p++;
            } else if (fc == (uint32_t)'c') {
                if (ai < 6u) {
                    out_char((char)get_arg(ai));
                    ai++;
                }
                p++;
            } else if (fc == (uint32_t)'f' || fc == (uint32_t)'F') {
                if (ai < 6u) {
                    out_float(get_arg(ai), precision);
                    ai++;
                }
                p++;
            } else if (fc == (uint32_t)'g' || fc == (uint32_t)'G') {
                if (ai < 6u) {
                    out_float(get_arg(ai), 0x80000000u | precision);
                    ai++;
                }
                p++;
            } else if (fc == (uint32_t)'e' || fc == (uint32_t)'E') {
                if (ai < 6u) {
                    out_float(get_arg(ai), precision);
                    ai++;
                }
                p++;
            } else {
                out_char('%');
                out_char((char)fc);
                p++;
            }
        }
    }
}

/* ---- printf ---- */
void printf(const char* fmt, uint32_t a0, uint32_t a1, uint32_t a2,
            uint32_t a3, uint32_t a4, uint32_t a5)
{
    /* 镜像回存：目标侧后端已把 a0..a5 放进 _printf_PARM_2..7（回存
     * 幂等冗余）；宿主 oracle 侧靠这组回存物化槽值，两端同源。 */
    printf_PARM_2 = a0; printf_PARM_3 = a1; printf_PARM_4 = a2;
    printf_PARM_5 = a3; printf_PARM_6 = a4; printf_PARM_7 = a5;
    g_output_mode = 0u;
    format_engine(fmt);
}

/* ---- sprintf ---- */
void sprintf(const char* fmt, uint32_t a0, uint32_t a1, uint32_t a2,
             uint32_t a3, uint32_t a4, uint32_t a5)
{
    sprintf_PARM_2 = a0; sprintf_PARM_3 = a1; sprintf_PARM_4 = a2;
    sprintf_PARM_5 = a3; sprintf_PARM_6 = a4; sprintf_PARM_7 = a5;
    g_output_mode = 1u;
    /* g_sprintf_pos 由调用方通过 sprintf_reset() 设置 */
    format_engine(fmt);
    /* 确保 NUL 结尾 */
    if (g_sprintf_pos < SPRINTF_BUFSZ) {
        g_sprintf_buf[g_sprintf_pos] = 0;
    } else {
        g_sprintf_buf[SPRINTF_BUFSZ - 1u] = 0;
    }
}
