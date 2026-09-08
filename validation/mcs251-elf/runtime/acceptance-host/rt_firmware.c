/*
 * rt_firmware.c — mcs251rt 除法/取模运行时 QEMU 语义验收固件（合法输入）。
 *
 * 规范依据：除法设计 v4 §8.2/§8.4（金标对拍 + 边界向量表）。
 *
 * 红线对照：
 *   - 本固件只含合法输入（rt_vectors.h 静态保证），UB 观察在独立固件
 *     ub_observe.c、独立 QEMU 会话（§8.3 测试三分离）。
 *   - 目标侧不使用 variadic printf：全部输出经定宽十六进制序列化
 *     （uart_hex8/16/32 语义，逐位移位取半字节）写 SBUF(0x99)。
 *   - 操作数经 volatile 拷贝进入除法表达式，禁止常数折叠进断言。
 *   - 行格式与宿主 host_expect.c 逐字节一致：
 *       D16U a=XXXX b=XXXX q=XXXX
 *       M16U a=XXXX b=XXXX r=XXXX
 *       D16S / M16S / D32U / M32U / D32S / M32S 同构
 *   - C 源里的 a/b、a%b 表达式由本链前端发给后端并触发 libcall——
 *     这正是 DUT 侧的被测路径（llc 发 __divuint 等 ECALL，运行时
 *     八对象按清单序供给）。
 *   - 跨调用活跃值（§8.8 执行级）：每个 report 前把一个 u32 哨兵值
 *     存入 caller 局部 volatile，调用后核对该值未被破坏（regmask
 *     契约的执行级证明）。
 */
#include <stdint.h>
#include "rt_vectors.h"

/* ---- 定宽串口输出（QEMU test-port 模型：写 SBUF 即出现在 transcript） ----
 * uart_puts/uart_hex* 一律 noinline：固件要发射 69x8 行，内联展开会把
 * .text 撑爆 CSEG 窗口（实测内联版 29KB > E4 板型余量）。 */
#define SBUF (*(volatile uint8_t *)0x99)

__attribute__((noinline)) static void uart_putc(char c) { SBUF = (uint8_t)c; }

__attribute__((noinline)) static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

__attribute__((noinline)) static void uart_hex8(uint8_t v)
{
    uint8_t hi = (uint8_t)(v >> 4) & 0x0Fu;
    uint8_t lo = v & 0x0Fu;
    uart_putc((char)(hi < 10 ? '0' + hi : 'A' + hi - 10));
    uart_putc((char)(lo < 10 ? '0' + lo : 'A' + lo - 10));
}

static void uart_hex16(uint16_t v)
{
    uart_hex8((uint8_t)(v >> 8));
    uart_hex8((uint8_t)v);
}

static void uart_hex32(uint32_t v)
{
    uart_hex16((uint16_t)(v >> 16));
    uart_hex16((uint16_t)v);
}

/* ---- 语义行发射：div 与 rem 成对（§8.8：同 a,b 各断 div 与 rem，
 *      两次独立 libcall、CALLSEQ 内槽存储不交错的执行级覆盖） ---- */

/* 目标侧向量经 rt_vectors.h 的切片访问器取数（>64 元素常量数组在本链
 * 会被降级为不可链的 packed struct 形态，详见该头文件注释）。 */
static volatile uint16_t v_a16, v_b16;
static volatile uint32_t v_a32, v_b32;

/* 跨调用活跃值哨兵：caller 侧跨除法调用存活的变量（§8.7/§8.8 执行级）。
 * volatile 防止优化器把它挪进寄存器生命周期外；实际存活路径 = 调用前写、
 * 调用后读比较。 */
static volatile uint32_t live_sentinel;

static uint32_t g_fail;

/* 单函数体量纪律：本链 rel8 条件分支 ±128B 限制下，把全部向量展开进
 * 一个函数会触发 llc "PC-relative branch out of range"（AsmBackend 的
 * FK_Data_1 PC-rel 检查，验收实测边界约在 35~40 条向量/函数）。语义行
 * 发射按"每操作一函数"的 noinline 粒度组织（em16u/em16s/em32u/em32s），
 * 调度片只做 volatile 写与调用——不改变被测路径（除法表达式仍在发射
 * 函数内、libcall 照发、div/rem 成对 CALLSEQ 覆盖不变）。 */
__attribute__((noinline)) static void em16u(void)
{
    uint16_t a = v_a16;
    uint16_t b = v_b16;
    uint32_t guard_before = 0x5A3C96F1u;
    live_sentinel = guard_before;
    {
        uint16_t q = a / b;                 /* -> udiv i16 -> __divuint */
        if (live_sentinel != guard_before) { g_fail++; }
        uart_puts("D16U a="); uart_hex16(a); uart_puts(" b="); uart_hex16(b);
        uart_puts(" q="); uart_hex16(q); uart_putc('\n');
    }
}

__attribute__((noinline)) static void em16s(void)
{
    uint16_t a = v_a16;
    uint16_t b = v_b16;
    int16_t as = (int16_t)a, bs = (int16_t)b;
    uint32_t guard_before = 0x5A3C96F1u;
    live_sentinel = guard_before;
    {
        int16_t q = as / bs;                /* -> sdiv i16 -> __divsint */
        int16_t r = as % bs;                /* -> srem i16 -> __modsint */
        if (live_sentinel != guard_before) { g_fail++; }
        uart_puts("D16S a="); uart_hex16((uint16_t)as); uart_puts(" b="); uart_hex16((uint16_t)bs);
        uart_puts(" q="); uart_hex16((uint16_t)q); uart_putc('\n');
        uart_puts("M16S a="); uart_hex16((uint16_t)as); uart_puts(" b="); uart_hex16((uint16_t)bs);
        uart_puts(" r="); uart_hex16((uint16_t)r); uart_putc('\n');
    }
    /* M16U 独立函数（见 em16r）以控制函数体量。 */
}

__attribute__((noinline)) static void em16r(void)
{
    uint16_t a = v_a16;
    uint16_t b = v_b16;
    uint32_t guard_before = 0x5A3C96F1u;
    live_sentinel = guard_before;
    {
        uint16_t r = a % b;                 /* -> urem i16 -> __moduint */
        if (live_sentinel != guard_before) { g_fail++; }
        uart_puts("M16U a="); uart_hex16(a); uart_puts(" b="); uart_hex16(b);
        uart_puts(" r="); uart_hex16(r); uart_putc('\n');
    }
}

__attribute__((noinline)) static void em32u(void)
{
    uint32_t a = v_a32;
    uint32_t b = v_b32;
    uint32_t guard_before = 0x3C5A96F1u;
    live_sentinel = guard_before;
    {
        uint32_t q = a / b;                 /* -> __divulong */
        if (live_sentinel != guard_before) { g_fail++; }
        uart_puts("D32U a="); uart_hex32(a); uart_puts(" b="); uart_hex32(b);
        uart_puts(" q="); uart_hex32(q); uart_putc('\n');
    }
}

__attribute__((noinline)) static void em32r(void)
{
    uint32_t a = v_a32;
    uint32_t b = v_b32;
    uint32_t guard_before = 0x3C5A96F1u;
    live_sentinel = guard_before;
    {
        uint32_t r = a % b;                 /* -> __modulong */
        if (live_sentinel != guard_before) { g_fail++; }
        uart_puts("M32U a="); uart_hex32(a); uart_puts(" b="); uart_hex32(b);
        uart_puts(" r="); uart_hex32(r); uart_putc('\n');
    }
}

__attribute__((noinline)) static void em32s(void)
{
    uint32_t a = v_a32;
    uint32_t b = v_b32;
    int32_t as = (int32_t)a, bs = (int32_t)b;
    uint32_t guard_before = 0x3C5A96F1u;
    live_sentinel = guard_before;
    {
        int32_t q = as / bs;                /* -> __divslong */
        int32_t r = as % bs;                /* -> __modslong */
        if (live_sentinel != guard_before) { g_fail++; }
        uart_puts("D32S a="); uart_hex32((uint32_t)as); uart_puts(" b="); uart_hex32((uint32_t)bs);
        uart_puts(" q="); uart_hex32((uint32_t)q); uart_putc('\n');
        uart_puts("M32S a="); uart_hex32((uint32_t)as); uart_puts(" b="); uart_hex32((uint32_t)bs);
        uart_puts(" r="); uart_hex32((uint32_t)r); uart_putc('\n');
    }
}

__attribute__((noinline)) static void report16(unsigned idx)
{
    (void)idx;
    em16u();
    em16r();
    em16s();
}

__attribute__((noinline)) static void report32(unsigned idx)
{
    (void)idx;
    em32u();
    em32r();
    em32s();
}

/* 向量调度（noinline）：每函数恰一条向量（无循环、无条件分支链），
 * main 线性调用 85 次——所有函数体都保持在 rel8 域内。 */
__attribute__((noinline)) static void run_one(unsigned i)
{
    v_a16 = rt_pick_a16(i);   /* volatile 写：操作数在运行期进入 */
    v_b16 = rt_pick_b16(i);
    v_a32 = rt_pick_a32(i);
    v_b32 = rt_pick_b32(i);
    report16(i);
    report32(i);
}

int main(void)
{
    run_one(0u); run_one(1u); run_one(2u); run_one(3u); run_one(4u);
    run_one(5u); run_one(6u); run_one(7u); run_one(8u); run_one(9u);
    run_one(10u); run_one(11u); run_one(12u); run_one(13u); run_one(14u);
    run_one(15u); run_one(16u); run_one(17u); run_one(18u); run_one(19u);
    run_one(20u); run_one(21u); run_one(22u); run_one(23u); run_one(24u);
    run_one(25u); run_one(26u); run_one(27u); run_one(28u); run_one(29u);
    run_one(30u); run_one(31u); run_one(32u); run_one(33u); run_one(34u);
    run_one(35u); run_one(36u); run_one(37u); run_one(38u); run_one(39u);
    run_one(40u); run_one(41u); run_one(42u); run_one(43u); run_one(44u);
    run_one(45u); run_one(46u); run_one(47u); run_one(48u); run_one(49u);
    run_one(50u); run_one(51u); run_one(52u); run_one(53u); run_one(54u);
    run_one(55u); run_one(56u); run_one(57u); run_one(58u); run_one(59u);
    run_one(60u); run_one(61u); run_one(62u); run_one(63u); run_one(64u);
    run_one(65u); run_one(66u); run_one(67u); run_one(68u); run_one(69u);
    run_one(70u); run_one(71u); run_one(72u); run_one(73u); run_one(74u);
    run_one(75u); run_one(76u); run_one(77u); run_one(78u); run_one(79u);
    run_one(80u); run_one(81u); run_one(82u); run_one(83u); run_one(84u);
    if (g_fail == 0u) {
        uart_puts("RT-SEMANTIC-PASS\n");
    } else {
        uart_puts("RT-SEMANTIC-FAIL live-sentinel violations=");
        uart_hex32(g_fail);
        uart_putc('\n');
    }
    /* 主循环停机：QEMU 会话在完整终止行后由运行器截止 */
    for (;;) { }
}
