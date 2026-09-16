/*===-- mcs251_printf.c ---------------------------------------------------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/*
 * MCS251 libc 子集 —— printf/sprintf 变参实现（G2 B1，B-S4 迁移，
 * G2-VARIADIC-DESIGN-draft.md §4.7）。
 *
 * 变参定义（frozen va_list ABI，cap 6）：
 *   void  printf(const char *fmt, ...);
 *   void  sprintf(char *buf, const char *fmt, ...);
 * 槽位（对象级，B-S2 延续槽补发）：
 *   printf  槽 = _printf_PARM_2..7   （fmt 首参走 DPL 寄存器通道，6×4B）
 *   sprintf 槽 = _sprintf_PARM_2..8  （buf 首参走寄存器，_PARM_2 = fmt
 *             指针槽，_PARM_3..8 = 6 个变参延续槽，7×4B = 28B）
 * buf 语义：sprintf 输出写入调用方提供的缓冲（g_out_buf），不再用
 * 全局缓冲区；内部 24B 截断 + 强制 NUL 上限维持（非标准行为，如实
 * 记录：调用方缓冲小于 24B 时仍按 24B 预算截断）。
 * P-4 Tag 28 记录变化：_printf bit3 0→1、param_count 7→1；
 * _sprintf bit3 0→1、param_count 7→2。
 * 契约：v2（1,2,32,8,1）——compat(1,1) 身份拒绝静态槽指针参数
 * （sprintf 的 fmt 槽所迫）。
 *
 * 格式符按语料频率实现：d/i u x X s c f g %%
 * 支持基本宽度（%5d）和精度（%.2f）。
 *
 * 设计：printf 和 sprintf 共享同一格式化引擎，区别只在输出目标
 * （printf 调 putchar，sprintf 写调用方缓冲）。
 *
 * G13a S3（G13A-CODE-DESIGN-draft.md rev-2 §3-c）：out_float 完整实现
 * **原样**迁出至独立 TU mcs251_printf_float.c（对象符号 _out_float），
 * 本文件经 mcs251_printf_internal.h 声明引用；out_char/out_str 由
 * static 改外部链接（供浮点 TU 复用，签名不变、noinline 保持）。
 * 裁剪形态 -DMCS251_PRINTF_NO_FLOAT：格式引擎无浮点分支（%f/%F、
 * %g/%G、%e/%E 落入未知格式符分支，原样输出 '%' + 格式符），对象无
 * UND _out_float；仅当可证明程序无浮点格式时使用（判定规则冻结于
 * mcs251_printf_internal.h：覆盖全部浮点转换符 + 任意 flags/宽度/
 * 精度/长度组合 + 非字面量格式串一律保留完整实现）。
 *
 * 红线：纯 C；无 64 位；无浮点运算符；无内联汇编；无 pragma。
 */

#include "mcs251_libc.h"
#include "mcs251_printf_internal.h"

/* putchar 由使用方（固件）提供：本 libc 子集经用户范围裁剪后不再
 * 自带 putchar（mcs251_libc.h 无此原语）。私有 ABI：接受单个 char，
 * 不返回值（与 stdio 的 int putchar(int) 不同，见文件头 ABI 约束）。
 * 2026-09-09 Kazimi 修复：原草稿直接调用未声明的 putchar（C11 隐式
 * 声明在 -Werror 下拒绝）。 */
extern void putchar(char c);

/* ---- sprintf 输出缓冲（B-S4：buf 由调用方提供）----
 * 2026-09-09 历史：全局 g_sprintf_buf 曾 256→64→24 收紧（sdld 经典
 * DATA 页 / DSEG 0x80 硬上限预算）。
 * 2026-09-15 B-S4 迁移（G2-VARIADIC-DESIGN §4.7）：定义改
 * sprintf(buf, fmt, ...)，buf 是首源参数（DPL 寄存器通道）；全局
 * g_sprintf_buf[24] 退役（-24B），新增 g_out_buf 指针（+4B），
 * out_char mode-1 臂改写 g_out_buf[g_sprintf_pos++]。
 * 保留内部 SPRINTF_BUFSZ=24 截断 + 强制 NUL 上限：单次调用最多
 * 写 24B（非标准行为，见文件头），g_sprintf_pos 每次调用入口清零，
 * 截断 + 末尾强制 NUL 语义与迁移前一致。sprintf_reset/len/getc/str
 * 随全局缓冲退役（repo 内无外部使用者，全树 sweep 实证）。 */
#define SPRINTF_BUFSZ 24
static char *g_out_buf;          /* 调用方提供的输出缓冲（v2 允许全局指针） */
static uint32_t g_sprintf_pos;

/* ---- 输出目标抽象 ----
 * 用函数指针模式不现实（后端限制），改用全局模式标志。
 * 2026-09-09 Kazimi：g_output_mode 声明为 volatile——否则 `== 0u`
 * 比较会被 clang instcombine 窄化成 i1 类型 load（i32 全局），MCS251
 * 后端无 i1 访存选择模式（llc 崩溃 Cannot select）。与本文件既有的
 * volatile 防 switch 综合手法同类。 */
static volatile uint32_t g_output_mode;  /* 0=printf(putchar), 1=sprintf(buf) */

/* 内部输出一个字符。noinline：防止全局变量读取被内联到调用方后
 * 产生后端无法 select 的 load-from-global DAG 节点。
 * G13a-S3：static → 外部链接（mcs251_printf_float.c 复用）；声明与
 * 打包规则注释见 mcs251_printf_internal.h。 */
__attribute__((noinline))
void out_char(char c)
{
    if (g_output_mode == 0u) {
        putchar(c);
    } else {
        if (g_sprintf_pos < SPRINTF_BUFSZ) {
            g_out_buf[g_sprintf_pos] = c;
            g_sprintf_pos++;
        }
    }
}

__attribute__((noinline))
void out_str(const char* s)
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
    /* G13a-S3：标量条件的三元改写为 if/else（语义零变化）。原式
     * `uppercase ? A : B` 在 clang -O0 下 IRGen 会留一条**无使用者**
     * 的 `zext i1 to i64`（条件提升的死代码），触发本运行时 IR 审计
     * 的 64 位类型红线；if/else 形态不产生该死值。-O2 产物不变
     * （实测 .text 逐字节同尺寸，见 GAP-G13A-PROBES/s3）。 */
    const char* digits;
    if (uppercase != 0u) {
        digits = "0123456789ABCDEF";
    } else {
        digits = "0123456789abcdef";
    }

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

/* out_float（f32 位模式格式化，%f/%F/%g/%G/%e/%E 的引擎）
 * G13a-S3 迁出至独立 TU mcs251_printf_float.c（函数体逐字保持，
 * static → 外部链接）；本 TU 经 mcs251_printf_internal.h 声明调用。
 * 判定规则（何时可用无浮点引擎）冻结于该头文件。 */


/* ---- 格式化引擎 ----
 * 解析 fmt，按序从 PARM 静态槽取实参输出。
 * output_mode 已由调用方设置。
 *
 * 2026-09-09 Kazimi：去 g_args[6]/g_nargs（DSEG 预算，见文件头）。
 * 实参直接读 printf/sprintf 的静态参数槽——引擎本就运行在参数槽已
 * 写好之后，语义等价（本就非重入）。
 * 2026-09-15 B-S4 迁移（G2-VARIADIC-DESIGN §4.7）：printf/sprintf 定义
 * 改变参，具名 a0..a5 与镜像回存退役。目标侧槽由调用方后端在调用前
 * 写入（LowerCall 既有机制）+ 本对象 B1 延续槽补发（_printf_PARM_2..7 /
 * _sprintf_PARM_3..8）提供；宿主 oracle 侧由 printf/sprintf 体内的
 * va_start/va_arg shim 把变参物化进同名全局（两端同源的新机制）。
 * 取参用 if 链 + volatile 序号（k），防止 clang 把多值比较综合成
 * switch IR（switch -> br_jt 后端不支持，同文件既有手法）。 */
/* C 标识符不带前导下划线：目标 '_' 前缀后恰为后端参数槽符号
 * _printf_PARM_2（多一层下划线会变成另一个符号 __printf_PARM_2，
 * 链接期未定义）。printf 槽 = _PARM_2..7（6 变参延续槽）；
 * sprintf 槽 = _PARM_3..8（_PARM_2 是 fmt 指针槽、由被调方
 * LowerFormalArguments 既有路径读，引擎不经手）。
 * 槽位宽度：目标 32 位（指针与提升标量同 4B，get_arg 原 u32 读法
 * 逐字节不变）；宿主 64 位（uintptr_t——%s 的指针实参必须完整过槽，
 * 截成低 32 位会在 out_str 解引用时崩溃，2026-09-15 B-S4 实测）。
 * 目标侧 typedef 恒 uint32_t，对象级布局零变化。 */
#ifdef MCS251_RT_TARGET
typedef uint32_t printf_arg_t;
#else
typedef uintptr_t printf_arg_t;
#endif
extern volatile printf_arg_t printf_PARM_2, printf_PARM_3, printf_PARM_4,
    printf_PARM_5, printf_PARM_6, printf_PARM_7;
extern volatile printf_arg_t sprintf_PARM_3, sprintf_PARM_4, sprintf_PARM_5,
    sprintf_PARM_6, sprintf_PARM_7, sprintf_PARM_8;
#ifdef MCS251_RT_TARGET
/* 目标侧：槽对象由本 TU 的 B-S2 延续槽补发定义（emitParameterSlots），
 * 上面 extern 即绑定到它们。 */
#else
/* 宿主侧：无后端槽补发，这里定义同名全局供 oracle 编译本文件使用；
 * printf/sprintf 体内的 va_start/va_arg shim 在调用引擎前写入。 */
volatile printf_arg_t printf_PARM_2, printf_PARM_3, printf_PARM_4,
    printf_PARM_5, printf_PARM_6, printf_PARM_7;
volatile printf_arg_t sprintf_PARM_3, sprintf_PARM_4, sprintf_PARM_5,
    sprintf_PARM_6, sprintf_PARM_7, sprintf_PARM_8;
#include <stdarg.h>   /* 宿主桥 shim 专用（目标侧不用 va_*） */
#endif

__attribute__((noinline))
static printf_arg_t get_arg(uint32_t idx)
{
    volatile uint32_t k = idx;
    uint32_t m = g_output_mode;

    if (k == 0u) return (m == 0u) ? printf_PARM_2 : sprintf_PARM_3;
    if (k == 1u) return (m == 0u) ? printf_PARM_3 : sprintf_PARM_4;
    if (k == 2u) return (m == 0u) ? printf_PARM_4 : sprintf_PARM_5;
    if (k == 3u) return (m == 0u) ? printf_PARM_5 : sprintf_PARM_6;
    if (k == 4u) return (m == 0u) ? printf_PARM_6 : sprintf_PARM_7;
    return (m == 0u) ? printf_PARM_7 : sprintf_PARM_8;
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
            }
#ifndef MCS251_PRINTF_NO_FLOAT
            /* 完整形态的浮点分支（默认）。裁剪形态（无浮点引擎，
             * -DMCS251_PRINTF_NO_FLOAT）：三分支整体缺席，f/F/g/G/e/E
             * 落入下方未知格式符分支（原样输出 '%' + 格式符；该分
             * 支不消费实参、ai 不前进）。违反"可证明程序无浮点格
             * 式"前提时不提供降级安全保证：后续转换会错读浮点实
             * 参槽乃至非法访存（宿主探针 %f %s 实测 %s 解引用首槽
             * 浮点位模式即 SIGSEGV）；仅当按 mcs251_printf_internal.h
             * 冻结规则可证明程序无浮点格式时才允许该形态。 */
            else if (fc == (uint32_t)'f' || fc == (uint32_t)'F') {
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
            }
#endif /* !MCS251_PRINTF_NO_FLOAT */
            else {
                out_char('%');
                out_char((char)fc);
                p++;
            }
        }
    }
}

/* ---- printf ----
 * B-S4 迁移（G2-VARIADIC-DESIGN §4.7）：固定 7 参 → 变参
 * （固定形参 7→1，P-4 记录 bit3 0→1、param_count 7→1）。
 * fmt 走 DPL 首参通道；6 个变参延续槽 _printf_PARM_2..7 由调用方
 * 后端在调用前写入 + 本对象 B1 延续槽补发（对象级布局与迁移前
 * 逐字节相同，探针 R：6×4B 连续 @0x0..0x14）。目标侧无物化代码；
 * 宿主（Oracle-A）侧由 va_start/va_arg shim 物化槽值。 */
void printf(const char* fmt, ...)
{
#ifndef MCS251_RT_TARGET
    /* 宿主桥 shim：宿主没有后端槽写机制，把变参物化进同名全局。
     * 契约：宿主 oracle 调用方必须恰好传满 6 个 printf_arg_t
     * (uintptr_t) 实参——少于 6 个或类型不匹配时 va_arg 是未定义
     * 行为（会读入栈参数区，不是无害的寄存器保存区读）。oracle
     * battery（/tmp/bs4-host/battery.c 先例）以补齐六参的方式
     * 调用；本 shim 不对欠参调用提供任何保证。 */
    va_list ap;
    va_start(ap, fmt);
    printf_PARM_2 = va_arg(ap, printf_arg_t);
    printf_PARM_3 = va_arg(ap, printf_arg_t);
    printf_PARM_4 = va_arg(ap, printf_arg_t);
    printf_PARM_5 = va_arg(ap, printf_arg_t);
    printf_PARM_6 = va_arg(ap, printf_arg_t);
    printf_PARM_7 = va_arg(ap, printf_arg_t);
    va_end(ap);
#endif
    g_output_mode = 0u;
    format_engine(fmt);
}

/* ---- sprintf ----
 * B-S4 迁移：固定 7 参（fmt+6，无 buf）→ 变参
 * sprintf(buf, fmt, ...)（固定形参 7→2，P-4 记录 bit3 0→1、
 * param_count 7→2）。buf = 首源参数（DPL 寄存器通道、无槽），
 * fmt = 第二源参数 → _sprintf_PARM_2（4B 指针槽），变参延续槽
 * _sprintf_PARM_3..8（对象 7 槽 28B，较迁移前 +4B）。
 * buf 语义：输出写调用方缓冲（g_out_buf），g_sprintf_pos 每次调用
 * 清零，24B 截断 + 强制 NUL 维持。 */
void sprintf(char* buf, const char* fmt, ...)
{
#ifndef MCS251_RT_TARGET
    /* 宿主桥 shim：变参从 fmt 之后起取（buf 是固定首参）。 */
    va_list ap;
    va_start(ap, fmt);
    sprintf_PARM_3 = va_arg(ap, printf_arg_t);
    sprintf_PARM_4 = va_arg(ap, printf_arg_t);
    sprintf_PARM_5 = va_arg(ap, printf_arg_t);
    sprintf_PARM_6 = va_arg(ap, printf_arg_t);
    sprintf_PARM_7 = va_arg(ap, printf_arg_t);
    sprintf_PARM_8 = va_arg(ap, printf_arg_t);
    va_end(ap);
#endif
    g_out_buf = buf;
    g_sprintf_pos = 0u;
    g_output_mode = 1u;
    format_engine(fmt);
    /* 确保 NUL 结尾（超长截断：末字节强制 NUL，与迁移前语义一致） */
    if (g_sprintf_pos < SPRINTF_BUFSZ) {
        g_out_buf[g_sprintf_pos] = 0;
    } else {
        g_out_buf[SPRINTF_BUFSZ - 1u] = 0;
    }
}
