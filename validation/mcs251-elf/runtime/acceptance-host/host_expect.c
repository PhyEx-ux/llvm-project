/*
 * host_expect.c — 宿主 Oracle-A 期望生成器（除法设计 v4 §8.2）。
 *
 * 红线对照：
 *   - 期望一律用 int64_t/uint64_t 计算（宽类型），宿主程序自身无 UB：
 *     x86 宿主 INT_MIN/-1 是 SIGFPE——宿主在算之前显式跳过 UB 输入对；
 *     本消费的向量表（rt_vectors.h）已静态保证不含 UB 输入，这里仍
 *     保留运行时防御性跳过（不输出该行）。
 *   - 每语义行同时断言恒等式 a == q*b + r、|r| < |b|、余号律、
 *     结果范围；不满足则宿主自身报错退出（期望文件必须可信）。
 *   - 输出格式与目标固件逐字节一致：每行 "D a=HEX b=HEX q=HEX r=HEX"，
 *     十六进制大写定宽（§8.2 定宽序列化、不用 printf 变参目标侧）。
 *     宿主侧可以用 printf（它不进目标镜像）。
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include "rt_vectors.h"

static int g_host_fail = 0;

/* 对一条 16 位向量：四操作（udiv16/sdiv16/urem16/srem16）各产一行期望。 */
static void emit16(uint16_t a_u, uint16_t b_u)
{
    int16_t a_s = (int16_t)a_u;
    int16_t b_s = (int16_t)b_u;

    /* udiv/urem：除数非零则全域定义 */
    printf("D16U a=%04X b=%04X q=%04X\n", a_u, b_u, (uint16_t)(a_u / b_u));
    printf("M16U a=%04X b=%04X r=%04X\n", a_u, b_u, (uint16_t)(a_u % b_u));

    /* sdiv/srem：跳过 UB 对（(INT16_MIN, -1)）；表内已无，防御性保留 */
    if (!(a_s == INT16_MIN && b_s == -1)) {
        int64_t q64 = (int64_t)a_s / (int64_t)b_s;
        int64_t r64 = (int64_t)a_s % (int64_t)b_s;
        int64_t abs_b = b_s < 0 ? -(int64_t)b_s : (int64_t)b_s;
        /* 宿主宽类型断言（§8.2）：恒等式 + |r| < |b| + 余号律 + 范围 */
        if ((int64_t)a_s != q64 * (int64_t)b_s + r64) { g_host_fail++; }
        if (r64 >= abs_b || r64 <= -abs_b) { g_host_fail++; }
        if (r64 != 0 && ((r64 < 0) != (a_s < 0))) { g_host_fail++; }
        if (q64 < -32768 || q64 > 32767) { g_host_fail++; }
        printf("D16S a=%04X b=%04X q=%04X\n",
               (uint16_t)a_s, (uint16_t)b_s, (uint16_t)(int16_t)q64);
        printf("M16S a=%04X b=%04X r=%04X\n",
               (uint16_t)a_s, (uint16_t)b_s, (uint16_t)(int16_t)r64);
    }
}

static void emit32(uint32_t a_u, uint32_t b_u)
{
    int32_t a_s = (int32_t)a_u;
    int32_t b_s = (int32_t)b_u;

    printf("D32U a=%08X b=%08X q=%08X\n", a_u, b_u, (uint32_t)(a_u / b_u));
    printf("M32U a=%08X b=%08X r=%08X\n", a_u, b_u, (uint32_t)(a_u % b_u));

    if (!(a_s == INT32_MIN && b_s == -1)) {
        int64_t q64 = (int64_t)a_s / (int64_t)b_s;
        int64_t r64 = (int64_t)a_s % (int64_t)b_s;
        int64_t abs_b = b_s < 0 ? -(int64_t)b_s : (int64_t)b_s;
        if ((int64_t)a_s != q64 * (int64_t)b_s + r64) { g_host_fail++; }
        if (r64 >= abs_b || r64 <= -abs_b) { g_host_fail++; }
        if (r64 != 0 && ((r64 < 0) != (a_s < 0))) { g_host_fail++; }
        if (q64 < INT32_MIN || q64 > INT32_MAX) { g_host_fail++; }
        printf("D32S a=%08X b=%08X q=%08X\n",
               (uint32_t)a_s, (uint32_t)b_s, (uint32_t)(int32_t)q64);
        printf("M32S a=%08X b=%08X r=%08X\n",
               (uint32_t)a_s, (uint32_t)b_s, (uint32_t)(int32_t)r64);
    }
}

int main(void)
{
    for (unsigned i = 0; i < RT_VEC_COUNT; ++i) {
        emit16(k_vectors[i].a16, k_vectors[i].b16);
        emit32(k_vectors[i].a32, k_vectors[i].b32);
    }
    printf("HOST-EXPECT-DONE\n");
    if (g_host_fail) {
        fprintf(stderr, "host identity check failed: %d\n", g_host_fail);
        return 1;
    }
    return 0;
}
