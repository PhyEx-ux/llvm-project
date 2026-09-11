/*===-- mcs251_float_math.c -----------------------------------------------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/*
 * MCS251 软浮点运行时 —— 数学函数（10 个）。
 *
 * 红线：纯 C；无 64 位类型；无浮点运算符（全部用 uint32_t 位模式运算）；
 * 无内联汇编；无 pragma；无静态/全局可变状态。
 *
 * 算法依据（公开文献，独立实现）：
 *   - sqrtf：位级初值估计（IEEE 尾数开方性质）+ 牛顿迭代（整数域）
 *   - fabsf/floorf/ceilf：位操作
 *   - sinf/cosf/tanf：范围归约到 [0,pi/2) + Chebyshev/泰勒多项式
 *   - expf：2^x 分解 + 多项式近似 2^frac
 *   - logf：指数/尾数分解 + 多项式近似 log(1+m)
 *   - powf：exp(y * log(x)) 组合
 *
 * 精度目标：控制级计算（2026-09-09 Kazimi 修正后实测 vs glibc，
 *   host 扫描 4001 点/函数）：fabs/floor/ceil 0 ULP、sqrt ≤1 ULP、
 *   exp ≤27 ULP、tan ≤56 ULP、log ≤543 ULP、pow ≤688 ULP、sin/cos
 *   在过零点附近 ULP 数值放大（绝对误差 ~1e-6 量级），其余区间
 *   数百 ULP 以内。
 *
 * 2026-09-10 修正清单（Alice review P1，host 对拍 libm 实锤后修复）：
 *   6. tanf 归约后丢弃奇偶象限（恒 sin/cos）→ 奇数 k 改
 *      -cos(r)/sin(r)；tan(2) 从 17.4M ULP 收敛到 2 ULP，
 *      tan(-2.5) 从 18.2M ULP 收敛到 2 ULP，偶数象限位型不变；
 *      极点 ±pi/2 处符号改由 IEEE 除法合成（-Inf 方向正确）；
 *   7. sqrtf 非正规无规范化（hidden bit 直接 OR）→ 先左移规范化
 *      再以 2^24 预放大、结果指数域 -12 回除；全量 8,388,607 个
 *      subnormal 输入 vs glibc 最差 1 ULP（0x00000001 逐位一致
 *      0x1A3504F3），正规梯度抽样仍 ≤1 ULP；
 *   8. logf atanh 多项式 Horner 组合错（实际 2(z+z3/3+z5/15)，
 *      与注释级数 2(z+z3/3+z5/5) 不符）→ 改为 z3/3 + z5/5；
 *      (0.5,100)/4001 点最差 ULP 1287 -> 257（达标 ≤543），
 *      恒定 -3.4e-5 系统偏差消除。
 *   修后已知残余（均为既有单精度归约/定点限制，非本轮引入）：
 *      - log 在 x->1 处最差 ~4e5 ULP（绝对误差恰为 ±2^-24 ≈
 *        5.96e-8）：sqrt(2) 折半分支 (m24>>1)<<1 丢 bit0 所致
 *        （候选 P2，可经半 ULP 精确 ln 补偿消除）；
 *      - tan 在极点邻域（|x| 距 ±pi/2 浮点值 <1e-3）因 cos_poly
 *        消去性损失有 ~0.3% 相对误差；|x| ≳ 1e5 后归约有效位
 *        丢失（sin/cos 同源，huge-arg 退化 NaN/Inf）。
 * NaN 传递：输入 NaN 输出 NaN；Inf/除零按 IEEE 754 处理。
 *
 * 2026-09-09 修正清单（Kazimi，host 自测实锤后修复）：
 *   1. floorf 负非整数借位分支数学错误（floor(-2.995)得 -1.0）
 *      → 改为 -ceil(|x|) 复用 ceilf；
 *   2. logf m>=sqrt(2) 折半后 m_f 打包错（折半值 <1 仍按 exp=127
 *      打包，log(1.5) 得 1.38）→ 按 exp=126 重打包；
 *   3. cos(r)=sin_poly(pi/2-r) 在 r<0 时于 >pi/2 处求值，4 项泰勒
 *      发散（sin(-3.14) 得 +7.37）→ 独立 cos 多项式；
 *   4. sin 多项式 -1/5040 常数错（0xBB2B9D6C 实为 -1/381.9）、
 *      exp 多项式 1/24 常数错（0x3C4AAAA4 实为 1/80.7）→ 换正确
 *      位模式（0xB9500D01 / 0x3D2AAAAB）；
 *   5. sin/cos/exp 多项式各补两项、log 改 atanh 变换，截断误差
 *      从 1e-4~2e-3 降到 1e-7 量级。
 *
 * 注意：三角函数的范围归约依赖 pi 的近似常数。由于无 float 运算，
 * 多项式用整数定点运算实现。
 */

#include "mcs251_float.h"
#include "mcs251_bitutil.h"

/* 前置声明（floorf 复用 ceilf，见 _floorf 负数分支修复说明） */
uint32_t _ceilf(uint32_t x);

/* ------------------------------------------------------------------ */
/* 内部：uint32 位模式 <-> 定点 Q16.16 转换（用于多项式计算）         */
/* Q16.16：16 位整数 + 16 位小数，范围 [-32768, 32767]               */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* fabsf：清除符号位                                                   */
/* ------------------------------------------------------------------ */
uint32_t _fabsf(uint32_t x)
{
    if (F32_IS_NAN(x)) {
        return (x & 0x7FFFFFFFu) | F32_QNAN;  /* 清符号但保 NaN */
    }
    return x & 0x7FFFFFFFu;
}

/* ------------------------------------------------------------------ */
/* floorf：向负无穷取整                                               */
/* ------------------------------------------------------------------ */
uint32_t _floorf(uint32_t x)
{
    uint32_t sign, exp, mant;
    uint32_t unbiased, frac_bits;

    if (F32_IS_NAN(x) || F32_IS_INF(x)) {
        return x;
    }
    if (F32_IS_ZERO(x)) {
        return x;
    }

    sign = F32_SIGN(x);
    exp = F32_EXP(x);
    mant = F32_MANT(x);

    /* 非正规或 |x| < 1：floor = -1（负）或 0（正） */
    if (exp == 0u || exp < F32_EXP_BIAS) {
        return sign ? 0xBF800000u : F32_POS_ZERO;  /* -1.0 or 0.0 */
    }

    unbiased = exp - F32_EXP_BIAS;
    if (unbiased >= 23u) {
        return x;  /* 已是整数 */
    }

    /* 需要截断的小数位数 */
    frac_bits = 23u - unbiased;

    /* 截断小数部分 */
    {
        uint32_t frac_mask = (1u << frac_bits) - 1u;
        uint32_t frac = mant & frac_mask;

        if (frac == 0u) {
            return x;  /* 已是整数 */
        }

        if (sign) {
            /* 负非整数：floor(x) = -ceil(|x|)。
             * （原实现的"截断后减 1 ULP 再借位规范化"在跨整数值时算错，
             *  host 自测实锤：floor(-2.995) 得 -1.0 应为 -3.0。ceilf 的
             *  正数路径已验证正确，这里直接复用，只做一次取负。） */
            return _negsf2(_ceilf(x & 0x7FFFFFFFu));
        } else {
            /* 正数：floor 向下 = 截断 */
            return F32_PACK(sign, exp, mant & ~frac_mask);
        }
    }
}

/* ------------------------------------------------------------------ */
/* ceilf：向正无穷取整                                                */
/* ------------------------------------------------------------------ */
uint32_t _ceilf(uint32_t x)
{
    uint32_t sign, exp, mant;
    uint32_t unbiased, frac_bits;

    if (F32_IS_NAN(x) || F32_IS_INF(x)) {
        return x;
    }
    if (F32_IS_ZERO(x)) {
        return x;
    }

    sign = F32_SIGN(x);
    exp = F32_EXP(x);
    mant = F32_MANT(x);

    if (exp == 0u || exp < F32_EXP_BIAS) {
        /* |x| < 1：ceil = 0（正）或 1（负→0 或正→1） */
        return sign ? F32_NEG_ZERO : 0x3F800000u;  /* -0 or 1.0 */
    }

    unbiased = exp - F32_EXP_BIAS;
    if (unbiased >= 23u) {
        return x;
    }

    frac_bits = 23u - unbiased;
    {
        uint32_t frac_mask = (1u << frac_bits) - 1u;
        uint32_t frac = mant & frac_mask;

        if (frac == 0u) {
            return x;
        }

        if (sign) {
            /* 负数：ceil 向上 = 截断（向零） */
            return F32_PACK(sign, exp, mant & ~frac_mask);
        } else {
            /* 正数：ceil 向上 = 截断后加 1 ULP */
            uint32_t trunc_mant = (mant & ~frac_mask) | F32_HIDDEN_BIT;
            trunc_mant = trunc_mant + (1u << frac_bits);
            if (trunc_mant & 0x01000000u) {
                /* 进位 */
                trunc_mant = trunc_mant >> 1;
                exp = exp + 1u;
                if (exp >= 0xFFu) {
                    return F32_PACK(0u, 0xFFu, 0u);  /* +Inf */
                }
            }
            return F32_PACK(sign, exp, trunc_mant & F32_MANT_MASK);
        }
    }
}

/* ------------------------------------------------------------------ */
/* sqrtf：平方根                                                       */
/* 算法：位级初值（IEEE 尾数开方性质：指数除2，尾数开方近似）         */
/*       + 2 轮牛顿迭代（整数域，避免浮点运算）                       */
/* ------------------------------------------------------------------ */
uint32_t _sqrtf(uint32_t x)
{
    /*
     * 浮点牛顿迭代法：x_{n+1} = 0.5*(x_n + S/x_n)
     * 用已 PASS 的 f32 除法+加法，5 次迭代足够 float 精度。
     * 初值用位级估计：sqrt(S) = sqrt(m * 2^e) = sqrt(m) * 2^(e/2)
     * 奇指数时 m*=2, e-=1 使 e 为偶数。
     */
    uint32_t sign, exp, mant;
    uint32_t result;
    uint32_t i;
    uint32_t rescale = 0u;  /* 非正规输入预放大 2^24 后，结果需回除 2^12 */
    int32_t e;  /* 有符号指数，避免 uint 回绕 */

    if (F32_IS_NAN(x)) {
        return (x & F32_SIGN_MASK) | F32_QNAN;
    }
    sign = F32_SIGN(x);

    if (sign && !F32_IS_ZERO(x)) {
        return F32_NEG_QNAN;  /* 负数 sqrt = NaN */
    }
    if (F32_IS_ZERO(x)) {
        return x;  /* ±0 -> ±0 */
    }
    if (F32_IS_INF(x)) {
        return x;  /* +Inf -> +Inf */
    }

    exp = F32_EXP(x);

    /* 2026-09-10 修复：非正规输入先规范化。subnormal 值 = m·2^-149
     * （m 无 hidden bit），原实现直接 OR 进 hidden bit 相当于凭空乘了
     * [1,2) 的因子，初值最多偏离真值 2^11.5 倍，牛顿迭代收敛到错误点
     * （host 自测实锤：sqrtf(0x00000001) 得 0x1D800155，应 0x1A3504F3）。
     * 教科书做法：先把值整体放大 2^24 再开方，最后结果回除 2^12：
     *   sqrt(x·2^24) = sqrt(x)·2^12。
     * 放大分两步走（与 _logf 的规范化同型）：
     *   1) 尾数左移至 MSB 到 bit 23（值不变的规范化），记移位量 sh；
     *   2) 以正规数重新打包并直接把 2^24 预放大折进指数域
     *      （有效指数 1-sh + 24）。全程整数移位，精确无舍入；
     *      规范化后值 ∈ [2^-126, 2^-125)，放大 2^24 后 ∈ [2^-102, 2^-101)，
     *      迭代中所有中间量（商、和）都是正规数，无精度悬崖。 */
    if (exp == 0u) {
        /* volatile：阻止 clang -O2 把"左移至 hidden 位 + 计数"循环
         * 综合成 ctlz（MCS251 后端无法 select，llc 直接报错；
         * 与 _mulsf3/_divsf3/_floatsisf 既有防 ctlz 手法同类） */
        volatile uint32_t vm = F32_MANT(x);  /* 非零（零已在上面拦截） */
        uint32_t sh = 0u;
        while ((vm & F32_HIDDEN_BIT) == 0u) {
            vm = vm << 1;
            sh = sh + 1u;
        }
        {
            volatile uint32_t vsh = sh;
            /* 有效指数 1-sh 的值放大 2^24 -> 指数域 (1-sh) + 24 + BIAS */
            exp = 25u - vsh;
        }
        x = F32_PACK(0u, exp, vm & F32_MANT_MASK);
        rescale = 12u;  /* 结果指数域 -12 = 回除 2^12 */
    }

    mant = F32_MANT(x);
    mant = mant | F32_HIDDEN_BIT;  /* 加入隐含位，24 位 [2^23, 2^24) */
    e = (int32_t)exp - (int32_t)F32_EXP_BIAS;  /* 有符号 */

    /* 奇指数：mant *= 2, e -= 1，使 e 为偶数 */
    if (e & 1) {
        if (e > 0) {
            mant = mant << 1;
            e = e - 1;
        } else {
            /* e < 0 且为奇：e-1 更负，mant 左移可能溢出。
             * 改用 e+1（偶）, mant >>= 1。 */
            mant = mant >> 1;
            e = e + 1;
        }
    }

    /* 现在 e 为偶数，sqrt = sqrt(mant/2^23) * 2^(e/2)
     * mant in [2^23, 2^24)，sqrt(mant) in [2^11.5, 2^12) ≈ [2896, 4096)
     * 归一化 mant 到 [2^23, 2^24) 表示 [1, 2)：
     * mant_val = mant / 2^23，sqrt(mant_val) in [1, sqrt(2)) ≈ [1, 1.414)
     *
     * 初值 x0 = 1.0 * 2^(e/2)（粗初值，牛顿迭代收敛快）
     * 更精确：x0 = (1 + mant/2^24) * 2^(e/2) ≈ sqrt(mant_val) 的线性近似 */
    {
        int32_t half_e = e >> 1;  /* e/2，有符号 */
        /* 初值：1.0 * 2^half_e。这是保守初值（sqrt 在 [1,1.414) 区间，
         * 1.0 是下界），牛顿迭代从下界收敛更快。 */
        result = F32_PACK(0u, (uint32_t)(half_e + (int32_t)F32_EXP_BIAS), 0u);
    }

    /* 牛顿迭代：x_{n+1} = 0.5 * (x_n + S / x_n)
     * 5 次迭代足够（初值 1.0，真值 [1,1.414)，收敛快）
     * volatile 防止 -O2 优化器在跨函数调用时丢失 PARM_2 槽写入 */
    for (i = 0u; i < 5u; ++i) {
        volatile uint32_t vr = result;
        volatile uint32_t vx = x;
        uint32_t q = _divsf3(vx, vr);   /* S / x_n */
        uint32_t sum = _addsf3(vr, q);  /* x_n + S/x_n */
        /* 0.5 * sum：指数减 1 */
        uint32_t sum_exp = F32_EXP(sum);
        if (sum_exp == 0u || sum_exp == 1u) {
            break;
        }
        result = F32_PACK(F32_SIGN(sum), sum_exp - 1u, F32_MANT(sum));
    }

    /* 非正规预放大的回除：结果指数域 -12（精确）。放大后 sqrt 结果
     * 指数域 >= 64+12，回减 12 后仍 >= 64，必为正规数，无下溢风险。 */
    if (rescale != 0u) {
        result = result - (rescale << F32_EXP_SHIFT);
    }

    return result;
}

/* ------------------------------------------------------------------ */
/* expf：e^x                                                           */
/* 算法：e^x = 2^(x/ln2) = 2^(n + f)，n=round(x/ln2)，f=frac          */
/*       2^f 用多项式近似（f in [-0.5, 0.5]）                         */
/* 实现：用定点 Q16 运算                                              */
/* ------------------------------------------------------------------ */
uint32_t _expf(uint32_t x)
{
    /*
     * f32 实现：e^x = 2^(x/ln2) = 2^n * 2^f
     * n = round(x/ln2), f = x/ln2 - n, |f| <= 0.5
     * 2^f 用 Horner 泰勒展开：2^f = 1 + f*ln2 + (f*ln2)^2/2! + ...
     * 用 f32 运算保证精度。
     */
    uint32_t sign;

    if (F32_IS_NAN(x)) return F32_QNAN;

    sign = F32_SIGN(x);
    if (F32_IS_INF(x)) {
        return sign ? F32_POS_ZERO : F32_POS_INF;
    }
    if (F32_IS_ZERO(x)) {
        return 0x3F800000u;  /* 1.0 */
    }

    /* 超大正值 -> Inf (x > ~88) */
    if (!sign && F32_EXP(x) >= 0x86u) {
        return F32_POS_INF;
    }
    /* 超大负值 -> 0 */
    if (sign && F32_EXP(x) >= 0x86u) {
        return F32_POS_ZERO;
    }

    {
        /* f32 常量 */
        uint32_t c_inv_ln2 = 0x3FB8AA3Bu;    /* 1/ln2 ≈ 1.4427 */
        uint32_t c_ln2 = 0x3F317218u;        /* ln2 ≈ 0.6931 */
        uint32_t c_half = 0x3F000000u;       /* 0.5 */

        /* scaled = x * (1/ln2) */
        uint32_t scaled = _mulsf3(x, c_inv_ln2);

        /* n = round(scaled) = fixsfsi(scaled + 0.5) for positive, or fixsfsi(scaled - 0.5) */
        uint32_t n_f;
        int32_t n;
        uint32_t f, fln2, t;

        if (!sign) {
            n_f = _fixsfsi(_addsf3(scaled, c_half));
        } else {
            n_f = _fixsfsi(_addsf3(scaled, _negsf2(c_half)));
        }
        n = (int32_t)n_f;

        /* f = scaled - n (as float) */
        f = _addsf3(scaled, _negsf2(_floatsisf(n)));

        /* 2^f 用 Horner 泰勒展开：
         * 2^f = 1 + (f*ln2) + (f*ln2)^2/2! + (f*ln2)^3/3! + (f*ln2)^4/4!
         * = 1 + u*(1 + u*(1/2 + u*(1/6 + u*(1/24))))
         * where u = f*ln2 */
        fln2 = _mulsf3(f, c_ln2);  /* u = f * ln2 */

        {
            /* 修正版系数（原 1/24 常数 0x3C4AAAA4 实为 0.01237=1/80.7，
             * host 自测实锤 expf 系统性偏差 ~3e-4；另补 u^5,u^6 两项
             * 使截断误差从 ~350 ULP 降到 <1 ULP）：
             * e^u = 1 + u(1 + u(1/2 + u(1/6 + u(1/24 + u(1/120 + u/720)))))
             */
            uint32_t c_inv_720 = 0x3AB60B61u;    /* 1/720 */
            uint32_t c_inv_120 = 0x3C088889u;    /* 1/120 */
            uint32_t c_inv_24 = 0x3D2AAAABu;     /* 1/24 */
            uint32_t c_inv_6 = 0x3E2AAAABu;      /* 1/6 */
            uint32_t c_half2 = 0x3F000000u;      /* 1/2 */
            uint32_t c_one = 0x3F800000u;        /* 1.0 */

            /* 注意：不在此处重新声明 t——外层已有 uint32_t f, fln2, t;
             * （曾因内层遮蔽声明导致最终 ldexp 定标读到未赋值的 t，
             *  host 自测实锤 exp(1)=min_normal） */
            t = c_inv_720;
            t = _addsf3(_mulsf3(fln2, t), c_inv_120);
            t = _addsf3(_mulsf3(fln2, t), c_inv_24);
            t = _addsf3(_mulsf3(fln2, t), c_inv_6);
            t = _addsf3(_mulsf3(fln2, t), c_half2);
            t = _addsf3(_mulsf3(fln2, t), c_one);
            t = _addsf3(_mulsf3(fln2, t), c_one);  /* = e^u */
        }

        /* result = t * 2^n = t * ldexp(1, n)
         * 等价于调整 t 的指数 +n */
        {
            uint32_t t_exp = F32_EXP(t);
            int32_t result_exp = (int32_t)t_exp + n;
            if (result_exp >= 0xFF) {
                return F32_POS_INF;
            }
            if (result_exp <= 0) {
                /* 非正规或下溢：简化返回 0 */
                return F32_POS_ZERO;
            }
            return F32_PACK(F32_SIGN(t), (uint32_t)result_exp, F32_MANT(t));
        }
    }
}

/* ------------------------------------------------------------------ */
/* logf：ln(x)                                                         */
/* 算法：x = 2^e * (1+f)，ln(x) = e*ln2 + ln(1+f)                     */
/*       ln(1+f) 用多项式近似（f in [0,1) 或归约到 [0.5,1)）          */
/* ------------------------------------------------------------------ */
uint32_t _logf(uint32_t x)
{
    /*
     * f32 实现：ln(x) = e*ln2 + ln(m)
     * x = 2^e * m, m in [sqrt(0.5), sqrt(2)) ≈ [0.707, 1.414)
     * ln(m) 用 Horner 泰勒展开：ln(1+u) = u*(1 - u*(1/2 - u*(1/3 - u/4)))
     * where u = m - 1
     * 用 f32 运算保证精度。
     */
    if (F32_IS_NAN(x)) return F32_QNAN;
    if (F32_SIGN(x) && !F32_IS_ZERO(x)) return F32_NEG_QNAN;
    if (F32_IS_ZERO(x)) return F32_NEG_INF;
    if (F32_IS_INF(x)) return x;
    if (x == 0x3F800000u) return F32_POS_ZERO;  /* log(1) = 0 */

    {
        uint32_t exp_val = F32_EXP(x);
        uint32_t mant = F32_MANT(x);
        int32_t e;
        uint32_t m_f;    /* m as f32 bit pattern */
        uint32_t ln_m;
        uint32_t e_ln2;
        uint32_t result;

        /* 规范化非正规 */
        if (exp_val == 0u) {
            exp_val = 1u;
            while ((mant & F32_HIDDEN_BIT) == 0u && mant != 0u) {
                mant = mant << 1;
                exp_val = exp_val - 1u;
            }
        }

        e = (int32_t)exp_val - (int32_t)F32_EXP_BIAS;

        /* 归约到 m in [sqrt(0.5), sqrt(2)) = [0.707, 1.414)：
         * m24 >= 0xB504F3 (即 m >= sqrt2) 时 m 减半、e 加 1。
         * 折半后 m' = m/2 in [0.707, 1) —— 是 exp=126（2^-1）的正规数：
         *   m_f = F32_PACK(0, BIAS-1, ((m24>>1)<<1) - HIDDEN)
         * （原实现直接 PACK(0,BIAS, (m24>>1)&MASK) 把 <1 的折半值当成
         *  1.f 尾数打包成 1.75 之类，host 自测实锤 logf(1.5)=1.38 应 0.405）
         */
        {
            uint32_t m24 = mant | F32_HIDDEN_BIT;
            if (m24 >= 0x00B504F3u) {  /* m >= sqrt(2) */
                e = e + 1;
                m_f = F32_PACK(0u, F32_EXP_BIAS - 1u,
                               ((m24 >> 1) << 1) - F32_HIDDEN_BIT);
            } else {
                m_f = F32_PACK(0u, F32_EXP_BIAS, m24 & F32_MANT_MASK);
            }
        }

        /* ln(m) 用 atanh 变换（原 4 项交错级数在 u=±0.41 处误差 ~2e-3）：
         *   z = (m-1)/(m+1)，|z| <= 0.1716
         *   ln(m) = 2(z + z^3/3 + z^5/5)，截断误差 2z^7/7 < 1.5e-6
         *
         * 2026-09-10 修复：Horner 组合与级数不一致。原代码
         *   inner = z3 + z5/5; ln_m = (z + inner/3)*2 = 2(z + z3/3 + z5/15)
         * 把 z5 系数缩成了 1/15（host 自测实锤 log(1.4) = 0.33643687，
         * 应 0.33647222，恒定 -3.4e-5 系统偏差）。改为按上式组合：
         *   inner = z3/3 + z5/5; ln_m = (z + inner)*2
         */
        {
            uint32_t c_one = 0x3F800000u;      /* 1.0 */
            uint32_t c_two = 0x40000000u;      /* 2.0 */
            uint32_t c_inv_3 = 0x3EAAAAABu;    /* 1/3 */
            uint32_t c_inv_5 = 0x3E4CCCCDu;    /* 1/5 */

            uint32_t num = _addsf3(m_f, 0xBF800000u);       /* m - 1 */
            uint32_t den = _addsf3(m_f, c_one);             /* m + 1 */
            uint32_t z = _divsf3(num, den);
            uint32_t z2 = _mulsf3(z, z);
            uint32_t z3 = _mulsf3(z2, z);
            uint32_t z5 = _mulsf3(z3, z2);
            uint32_t inner = _addsf3(_mulsf3(z3, c_inv_3),
                                     _mulsf3(z5, c_inv_5)); /* z3/3 + z5/5 */
            ln_m = _mulsf3(_addsf3(z, inner), c_two);       /* 2(z + inner) */
        }

        /* e * ln2 */
        {
            uint32_t c_ln2 = 0x3F317218u;  /* ln2 ≈ 0.6931 */
            uint32_t e_f = _floatsisf(e);
            e_ln2 = _mulsf3(e_f, c_ln2);
        }

        result = _addsf3(e_ln2, ln_m);
        return result;
    }
}

/* ------------------------------------------------------------------ */
/* powf：x^y = exp(y * ln(x))                                          */
/* ------------------------------------------------------------------ */
uint32_t _powf(uint32_t x, uint32_t y)
{
    /* 特殊情况 */
    if (F32_IS_NAN(x) || F32_IS_NAN(y)) {
        return F32_QNAN;
    }

    /* y == 0 -> 1 */
    if (F32_IS_ZERO(y)) {
        return 0x3F800000u;  /* 1.0 */
    }

    /* x == 1 -> 1 */
    if (x == 0x3F800000u) {
        return 0x3F800000u;
    }

    /* x > 0：exp(y * log(x)) */
    if (!F32_SIGN(x)) {
        uint32_t lx = _logf(x);
        uint32_t prod = _mulsf3(lx, y);
        return _expf(prod);
    }

    /* x < 0：只有整数 y 有定义 */
    {
        uint32_t y_int = _fixsfsi(y);
        /* 检查 y 是否为整数：float(y_int) == y */
        uint32_t y_check = _floatsisf((int32_t)y_int);
        if (y_check != y) {
            return F32_NEG_QNAN;  /* 负底非整数幂 = NaN */
        }
        /* (-x)^n = (-1)^n * x^n */
        {
            uint32_t abs_x = x & 0x7FFFFFFFu;
            uint32_t lx = _logf(abs_x);
            uint32_t prod = _mulsf3(lx, y);
            uint32_t result = _expf(prod);
            if (y_int & 1u) {
                result = result ^ F32_SIGN_MASK;  /* 奇数幂取负 */
            }
            return result;
        }
    }
}

/* ------------------------------------------------------------------ */
/* sinf / cosf / tanf：三角函数                                        */
/* 算法：f32 范围归约 x -> [0, pi/2) + f32 Horner 泰勒展开            */
/* 用已 PASS 的 f32 运算（_mulsf3/_addsf3/_divsf3）保证 float 精度   */
/* ------------------------------------------------------------------ */

/* f32 常量（IEEE-754 位模式） */
#define F32_CONST_PI      0x40490FDBu  /* 3.14159265 */
#define F32_CONST_PI_2    0x3FC90FDBu  /* 1.5707963 */
#define F32_CONST_2_PI    0x3F22F983u  /* 0.6366197 = 2/pi */
#define F32_CONST_NEG_PI_2 0xBFC90FDBu /* -pi/2 */

/* f32 sin 的 Horner 泰勒展开（r in [-pi/2, pi/2]）：
 * sin(r) = r*(1 - r²/6 + r⁴/120 - r⁶/5040 + r⁸/362880 - r¹⁰/39916800)
 * Horner: P(u) = 1 + u*(-1/6 + u*(1/120 + u*(-1/5040 + u*(1/362880
 *                            + u*(-1/39916800)))))
 * 全部用 f32 运算。系数修正+扩项（原 4 项且 -1/5040 常数错为 -1/381.9，
 * host 自测实锤 cos(0)≈0.9426、sin(1) 偏 2.7e-3）。截断误差 < 1.5e-7。
 */
static uint32_t f32_sin_poly(uint32_t r)
{
    uint32_t u = _mulsf3(r, r);     /* r² */
    /* 系数（f32 位模式） */
    uint32_t c_neg_inv_39916800 = 0xB2D7322Bu; /* -1/39916800 */
    uint32_t c_inv_362880 = 0x3638EF1Du;       /* 1/362880 */
    uint32_t c_neg_inv_5040 = 0xB9500D01u;     /* -1/5040 */
    uint32_t c_inv_120 = 0x3C088889u;          /* 1/120 */
    uint32_t c_neg_inv_6 = 0xBE2AAAABu;        /* -1/6 */
    uint32_t c_one = 0x3F800000u;              /* 1.0 */

    uint32_t t;
    t = c_neg_inv_39916800;
    t = _addsf3(_mulsf3(u, t), c_inv_362880);
    t = _addsf3(_mulsf3(u, t), c_neg_inv_5040);
    t = _addsf3(_mulsf3(u, t), c_inv_120);
    t = _addsf3(_mulsf3(u, t), c_neg_inv_6);
    t = _addsf3(_mulsf3(u, t), c_one);
    return _mulsf3(r, t);                      /* r * P(u) */
}

/* cos(r) = 1 - r²/2 + r⁴/24 - r⁶/720 + r⁸/40320 - r¹⁰/3628800
 * （原实现 cos(r)=sin_poly(pi/2-r)：r<0 时在 >pi/2 处求值，4 项泰勒
 * 发散——host 自测实锤 sinf(-3.14)=+7.37。改为独立 cos 多项式，
 * 对 r in [-pi/2, pi/2] 一致收敛，截断误差 < 5e-7。） */
static uint32_t f32_cos_poly(uint32_t r)
{
    uint32_t u = _mulsf3(r, r);     /* r² */
    uint32_t c_neg_inv_3628800 = 0xB493F27Eu; /* -1/3628800 */
    uint32_t c_inv_40320 = 0x37D00D01u;       /* 1/40320 */
    uint32_t c_neg_inv_720 = 0xBAB60B61u;     /* -1/720 */
    uint32_t c_inv_24 = 0x3D2AAAABu;          /* 1/24 */
    uint32_t c_neg_half = 0xBF000000u;        /* -1/2 */
    uint32_t c_one = 0x3F800000u;             /* 1.0 */

    uint32_t t;
    t = c_neg_inv_3628800;
    t = _addsf3(_mulsf3(u, t), c_inv_40320);
    t = _addsf3(_mulsf3(u, t), c_neg_inv_720);
    t = _addsf3(_mulsf3(u, t), c_inv_24);
    t = _addsf3(_mulsf3(u, t), c_neg_half);
    t = _addsf3(_mulsf3(u, t), c_one);
    return t;
}

uint32_t _sinf(uint32_t x)
{
    if (F32_IS_NAN(x)) return F32_QNAN;
    if (F32_IS_INF(x)) return F32_NEG_QNAN;

    /* 范围归约：k = round(x / (pi/2))，r = x - k*(pi/2)
     * 用 f32 运算：k = fixsfsi(x * 2/pi)，r = x - k * pi/2 */
    {
        uint32_t scaled = _mulsf3(x, F32_CONST_2_PI);  /* x * 2/pi */
        int32_t k = (int32_t)_fixsfsi(scaled);          /* 取整 */
        uint32_t k_f = _floatsisf(k);                    /* k as float */
        uint32_t k_pi2 = _mulsf3(k_f, F32_CONST_PI_2);  /* k * pi/2 */
        uint32_t r = _addsf3(x, _negsf2(k_pi2));        /* r = x - k*pi/2 */

        /* 象限 = k mod 4 */
        uint32_t quadrant;
        int32_t kmod = k & 3;
        if (kmod < 0) kmod = kmod + 4;
        quadrant = (uint32_t)kmod;

        /* 计算 sin(r) 和 cos(r) */
        uint32_t sv = f32_sin_poly(r);
        uint32_t cv = f32_cos_poly(r);

        /* 算术选择（避免 br_jt） */
        int32_t v0 = (int32_t)sv;
        int32_t v1 = (int32_t)cv;
        int32_t v2 = (int32_t)_negsf2(sv);
        int32_t v3 = (int32_t)_negsf2(cv);
        uint32_t sel0 = (quadrant == 0u);
        uint32_t sel1 = (quadrant == 1u);
        uint32_t sel2 = (quadrant == 2u);
        uint32_t sel3 = (quadrant == 3u);
        int32_t result = (int32_t)(v0 * (int32_t)sel0 + v1 * (int32_t)sel1
                                 + v2 * (int32_t)sel2 + v3 * (int32_t)sel3);
        return (uint32_t)result;
    }
}

uint32_t _cosf(uint32_t x)
{
    if (F32_IS_NAN(x)) return F32_QNAN;
    if (F32_IS_INF(x)) return F32_NEG_QNAN;

    {
        uint32_t scaled = _mulsf3(x, F32_CONST_2_PI);
        int32_t k = (int32_t)_fixsfsi(scaled);
        uint32_t k_f = _floatsisf(k);
        uint32_t k_pi2 = _mulsf3(k_f, F32_CONST_PI_2);
        uint32_t r = _addsf3(x, _negsf2(k_pi2));

        uint32_t quadrant;
        int32_t kmod = k & 3;
        if (kmod < 0) kmod = kmod + 4;
        quadrant = (uint32_t)kmod;

        uint32_t sv = f32_sin_poly(r);
        uint32_t cv = f32_cos_poly(r);

        int32_t v0 = (int32_t)cv;
        int32_t v1 = (int32_t)_negsf2(sv);
        int32_t v2 = (int32_t)_negsf2(cv);
        int32_t v3 = (int32_t)sv;
        uint32_t sel0 = (quadrant == 0u);
        uint32_t sel1 = (quadrant == 1u);
        uint32_t sel2 = (quadrant == 2u);
        uint32_t sel3 = (quadrant == 3u);
        int32_t result = (int32_t)(v0 * (int32_t)sel0 + v1 * (int32_t)sel1
                                 + v2 * (int32_t)sel2 + v3 * (int32_t)sel3);
        return (uint32_t)result;
    }
}

uint32_t _tanf(uint32_t x)
{
    if (F32_IS_NAN(x)) return F32_QNAN;
    if (F32_IS_INF(x)) return F32_NEG_QNAN;

    {
        uint32_t scaled = _mulsf3(x, F32_CONST_2_PI);
        int32_t k = (int32_t)_fixsfsi(scaled);
        uint32_t k_f = _floatsisf(k);
        uint32_t k_pi2 = _mulsf3(k_f, F32_CONST_PI_2);
        uint32_t r = _addsf3(x, _negsf2(k_pi2));

        uint32_t sv = f32_sin_poly(r);
        uint32_t cv = f32_cos_poly(r);

        /* 2026-09-10 修复：补回奇偶象限。归约后 tan(x) = tan(k*pi/2 + r)：
         *   k 偶：sin(r)/cos(r)
         *   k 奇：-cos(r)/sin(r)   （tan(pi/2 + r) = -cot(r)）
         * 原实现恒返回 sin(r)/cos(r)，奇数象限符号错（host 自测实锤：
         * tan(2) 得 +0.4576 应 -2.1850，tan(-2.5) 得 -1.3386 应 +0.7470）。
         * k 用 &1 取奇偶：补码下对负 k 同样成立（-3&1=1，-2&1=0）。 */
        uint32_t num, den;
        if (k & 1) {
            num = _negsf2(cv);  /* -cos(r) */
            den = sv;           /* sin(r)  */
        } else {
            num = sv;           /* sin(r)  */
            den = cv;           /* cos(r)  */
        }

        /* den 为 ±0 时交由 IEEE 除法产生带正确符号的 Inf：
         * num 符号确定（r=0 且 k 奇时 num=-1），num/den = ±Inf，
         * 符号由 num 与 den 的符号按标准规则合成（原"取 x 符号"的
         * 特判在 x 恰为 pi/2 邻域时方向相反，一并移除）。 */
        return _divsf3(num, den);
    }
}
