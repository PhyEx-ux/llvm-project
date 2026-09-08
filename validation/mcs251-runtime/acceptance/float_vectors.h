/*===-- float_vectors.h ---------------------------------------------------===*/
/*
 * MCS251 软浮点运行时验收 —— 边界向量表。
 *
 * 每个向量为 (a, b) 对的 uint32_t 位模式，覆盖 IEEE-754 关键边界：
 *   0, 1, -1, 最小正规, 最大正规, +Inf, -Inf, qNaN, 除零, 非正规
 *
 * 逐函数选择向量子集：
 *   add/sub : 8 对
 *   mul     : 8 对
 *   div     : 20 对（含边界舍入路径）
 *   compare : 6 对
 *   neg     : 6 个单值（vec_neg_data）
 *   conv    : int<->float 6 个值
 */

#ifndef FLOAT_VECTORS_H
#define FLOAT_VECTORS_H

#include <stdint.h>

/* IEEE-754 binary32 常量 */
#define F32_POS_ZERO  0x00000000u
#define F32_NEG_ZERO  0x80000000u
#define F32_POS_ONE   0x3F800000u
#define F32_NEG_ONE   0xBF800000u
#define F32_POS_TWO   0x40000000u
#define F32_NEG_TWO   0xC0000000u
#define F32_POS_HALF  0x3F000000u
#define F32_POS_INF   0x7F800000u
#define F32_NEG_INF   0xFF800000u
#define F32_QNAN      0x7FC00000u
#define F32_MIN_NORM  0x00800000u  /* 最小正规数 ~1.18e-38 */
#define F32_MAX_NORM  0x7F7FFFFFu  /* 最大正规数 ~3.40e+38 */
#define F32_PI        0x40490FDBu  /* 3.14159... */
#define F32_PI_4      0x3F490FDBu  /* pi/4 */
#define F32_E         0x402DF854u  /* e = 2.71828... */
#define F32_SQRT2     0x3FB504F3u  /* sqrt(2) */

/* add/sub 向量：8 对 */
#define NVEC_ADDSUB 8
static const uint32_t vec_add_a[NVEC_ADDSUB] = {
    F32_POS_ONE, F32_POS_ONE, F32_POS_INF, F32_QNAN,
    F32_POS_ZERO, F32_NEG_ZERO, F32_MIN_NORM, F32_MAX_NORM
};
static const uint32_t vec_add_b[NVEC_ADDSUB] = {
    F32_POS_ONE, F32_NEG_ONE, F32_POS_INF, F32_POS_ONE,
    F32_NEG_ZERO, F32_POS_ZERO, F32_POS_ZERO, F32_POS_ONE
};

/* mul 向量：8 对 */
#define NVEC_MUL 8
static const uint32_t vec_mul_a[NVEC_MUL] = {
    F32_POS_TWO, F32_POS_INF, F32_POS_ZERO, F32_NEG_ONE,
    F32_POS_HALF, F32_MAX_NORM, F32_MIN_NORM, F32_QNAN
};
static const uint32_t vec_mul_b[NVEC_MUL] = {
    F32_POS_HALF, F32_POS_ZERO, F32_POS_INF, F32_NEG_ONE,
    F32_POS_TWO, F32_MAX_NORM, F32_MIN_NORM, F32_POS_ONE
};

/* div 向量：20 对（覆盖特殊值 + 需要舍入的非精确商 + 边界舍入路径） */
#define NVEC_DIV 20
static const uint32_t vec_div_a[NVEC_DIV] = {
    F32_POS_ONE, F32_POS_ONE, F32_POS_INF, F32_POS_ZERO,
    F32_POS_TWO, F32_NEG_ONE, F32_MAX_NORM, F32_QNAN,
    /* 非精确商：1/3, 1/7, 1/10, 2/3, 7/3, 10/3, 1/127, 1/129 */
    0x3F800000u, 0x3F800000u, 0x3F800000u, 0x40000000u,
    0x40E00000u, 0x41200000u, 0x3F800000u, 0x3F800000u,
    /* 边界舍入（Alice 补测目标码路径，2026-09-08）：
     * 最小非正规/2（halfway 偶舍入 0）、非正规 halfway 偶舍、
     * 舍入升最小正规（(2-2^-23)/2^127 恰为最大非正规与最小正规
     * 中点，偶舍升位）、最大正规/0.5 溢出到 Inf */
    0x00000001u, 0x00000003u, 0x3FFFFFFFu, 0x7F7FFFFFu
};
static const uint32_t vec_div_b[NVEC_DIV] = {
    F32_POS_TWO, F32_POS_ZERO, F32_POS_INF, F32_POS_ONE,
    F32_POS_HALF, F32_NEG_ONE, F32_MAX_NORM, F32_POS_ONE,
    0x40400000u, 0x40E00000u, 0x41200000u, 0x40400000u,
    0x40400000u, 0x40400000u, 0x42FE0000u, 0x43010000u,
    0x40000000u, 0x40000000u, 0x7F000000u, 0x3F000000u
};

/* 比较向量：6 对 */
#define NVEC_CMP 6
static const uint32_t vec_cmp_a[NVEC_CMP] = {
    F32_POS_ONE, F32_POS_ONE, F32_NEG_ONE, F32_POS_INF,
    F32_QNAN, F32_NEG_ZERO
};
static const uint32_t vec_cmp_b[NVEC_CMP] = {
    F32_POS_ONE, F32_POS_TWO, F32_POS_ONE, F32_POS_INF,
    F32_POS_ONE, F32_POS_ZERO
};

/* 取负向量（原 vec_math，改名避免误解为 math API 测试）：6 个单值 */
#define NVEC_NEG 6
static const uint32_t vec_neg_data[NVEC_NEG] = {
    F32_POS_ZERO, F32_POS_ONE, F32_NEG_ONE, F32_PI_4,
    F32_POS_TWO, F32_POS_HALF
};

/* int->float 转换向量 */
#define NVEC_I2F 6
static const int32_t vec_i2f[NVEC_I2F] = {
    0, 1, -1, 100, -1000, 0x7FFFFFFF
};

/* float->int 转换向量 */
#define NVEC_F2I 6
static const uint32_t vec_f2i[NVEC_F2I] = {
    F32_POS_ZERO, F32_POS_ONE, F32_NEG_ONE, F32_POS_TWO,
    0x41200000u,  /* 10.0 */
    0xC1A00000u   /* -20.0 */
};

#endif /* FLOAT_VECTORS_H */
