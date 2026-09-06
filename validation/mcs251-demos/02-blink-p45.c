/*
 * ============================================================================
 *  02-blink-p45.c — P4.5 纯 LED 默认模式的真机 blink demo
 * ============================================================================
 *
 *  设计目标：本项目工具链在真机 STC32G12K128 上的**最小可信闭环**——
 *  不依赖 XINIT 全局初值（天然免疫 STCISP 的 EEPROM 分区搬移）、
 *  不依赖串口（LED 肉眼即可判定），只验证：
 *    复位取指 -> WTST/栈初始化(crt) -> C 代码 -> GPIO 模式配置 -> 翻转 -> 软件延时
 *
 *  默认零串口依赖，只翻转LED并执行volatile计数延时，避免TI等待挡住GPIO。
 *  仅显式 -DBLINK_ENABLE_UART=1 才发送tick（此模式仍可能因TI卡住而停止后续翻转）。
 *  真机周期未校准：BLINK_PERIOD_MS=10000是循环预算参数，不再宣称精确10秒。
 *  volatile计数保证-O2不删除空循环；编译器生成的访存开销使旧6T估算失效。
 *  QEMU观察串口需显式开UART且不定义STC32_REAL_HW；纯LED模式本就没有serial。
 *
 *  硬件依据（STC32G 官方头文件 / 手册）：
 *    P4   = 0xC0   P4 数据口
 *    P4M1 = 0xB3   P4 模式寄存器 1
 *    P4M0 = 0xB4   P4 模式寄存器 0
 *    P4.0~P4.7 上电默认高阻 —— 必须先配 P4M1=0/P4M0=0（准双向）才能驱动 LED
 *    延时预算沿用 MAIN_Fosc/6000 次，但volatile访存开销不同，必须实机校准
 *
 *  构建命令（WSL 内，从仓库根；真机版加 -DSTC32_REAL_HW）：
 *    /home/liu/build-clang/bin/clang --target=mcs251-unknown-none \
 *        -std=c11 -O2 -Wall -Wextra -DSTC32_REAL_HW \
 *        validation/mcs251-demos/02-blink-p45.c -S -emit-llvm -o /home/liu/d02/blink.ll
 *    /home/liu/build-mcs251/bin/llc -mtriple=mcs251-unknown-none \
 *        -filetype=obj /home/liu/d02/blink.ll -o /home/liu/d02/blink.rel
 *    /home/liu/build-sdcc/bin/sdas251 -plosgffw -o /home/liu/d02/crt.rel \
 *        validation/mcs251-firmware/crt-selfstart.asm
 *    （.lk 模板见本目录 build-realhw.py 的链接段；XINIT 为空表，基址随意）
 *
 *  烧录：STCISP 任意的 EEPROM 分区设置均可（本固件无 FE 段数据依赖），
 *  主频选 24MHz。
 * ============================================================================
 */

#include <stdint.h>
#ifdef HOST_BUILD
#include <stdio.h>
#endif

/* ---------- 延时预算参数：历史名称保留，不代表精确毫秒；可 -D 缩短 ---------- */
#ifndef BLINK_PERIOD_MS
#define BLINK_PERIOD_MS 10000u
#endif

#ifndef BLINK_ENABLE_UART
#define BLINK_ENABLE_UART 0
#endif

#define MAIN_Fosc 24000000UL   /* 主频：与烧录时 ISP 选择的 HIRC 一致 */

/* ---------- GPIO / 串口寄存器（官方头文件地址） ---------- */
#define P4     (*(volatile uint8_t *)0xC0)   /* P4 数据口 */
#define P4M1   (*(volatile uint8_t *)0xB3)   /* P4 模式 1 */
#define P4M0   (*(volatile uint8_t *)0xB4)   /* P4 模式 0 */
#define LED_BIT 0x20u                         /* P4.5 */

/* ---------- 显式启用时才编译串口路径 ---------- */
#if BLINK_ENABLE_UART
#ifdef HOST_BUILD
static void uart_init(void) { }
static void putstr_(const char *s) { fputs(s, stdout); }
static void puthex_(uint32_t v)
{
    for (int i = 28; i >= 0; i -= 4) {
        uint32_t d = (v >> (unsigned)i) & 0xFu;
        putchar((int)(d < 10 ? '0' + d : 'A' + d - 10));
    }
}
#elif defined(STC32_REAL_HW)
/* 真机串口：UART1 @ P3.0/P3.1，24MHz 下 115200 8N1（Timer2 1T 重载 0xFFCC） */
#define SCON (*(volatile uint8_t *)0x98)
#define SBUF (*(volatile uint8_t *)0x99)
#define T2L  (*(volatile uint8_t *)0xD7)
#define T2H  (*(volatile uint8_t *)0xD6)
#define AUXR (*(volatile uint8_t *)0x8E)
#define P_SW1 (*(volatile uint8_t *)0xA2)
#define P3M1 (*(volatile uint8_t *)0xB1)
#define P3M0 (*(volatile uint8_t *)0xB2)
#define TI_GET()  ((SCON >> 1) & 1u)
#define TI_SET(v) do { SCON = (uint8_t)((SCON & ~0x02u) | ((v) ? 0x02u : 0u)); } while (0)
static void uart_init(void)
{
    P_SW1 = 0x00u;           /* V1真机已验证配置；不猜测复位值。 */
    P3M1 &= (uint8_t)~0x03u;
    P3M0 &= (uint8_t)~0x03u;
    AUXR &= (uint8_t)~0x10u; /* 停止T2后装入重载，防止继承运行状态。 */
    SCON = 0x50u;             /* 模式 1，REN=1 */
    T2L = 0xCCu; T2H = 0xFFu; /* 24MHz@115200 重载 */
    AUXR &= (uint8_t)~0x08u;  /* T2_C/T=0：定时器模式 */
    AUXR |= 0x05u;            /* S1BRT=1 选 T2、T2x12=1 1T */
    AUXR |= 0x10u;            /* T2R=1 启动（最后置位） */
}
static void putc_(char c)
{
    SBUF = (uint8_t)c;        /* 写后轮询：停止位开始发送时硬件置 TI */
    while (!TI_GET()) { }
    TI_SET(0);
}
static void putstr_(const char *s) { while (*s) putc_(*s++); }
static void puthex_(uint32_t v)
{
    const char *d = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4)
        putc_(d[(v >> (unsigned)i) & 0xFu]);
}
#else
/* QEMU test-port：直写 SBUF 即输出，无需初始化 */
static void uart_init(void) { }
#define SBUF (*(volatile uint8_t *)0x99)
static void putc_(char c) { SBUF = (uint8_t)c; }
static void putstr_(const char *s) { while (*s) putc_(*s++); }
static void puthex_(uint32_t v)
{
    const char *d = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4)
        putc_(d[(v >> (unsigned)i) & 0xFu]);
}
#endif
#endif /* BLINK_ENABLE_UART */

/* ---------- volatile软件延时：保留实际执行，不保证精确毫秒 ---------- */
static void delay_ms(uint16_t ms)
{
    if (!ms) return;
    volatile uint16_t remaining = ms;
    do {
        volatile uint16_t i = (uint16_t)(MAIN_Fosc / 6000UL);
        while (--i) { }
    } while (--remaining);
}

int main(void)
{
#if BLINK_ENABLE_UART
    uint32_t tick = 0;        /* 局部计数：无全局初值 -> XINIT 空表 */
#endif

    P4M1 = 0x00u;             /* P4 全口准双向（上电高阻，必须先配） */
    P4M0 = 0x00u;
    P4 &= (uint8_t)~LED_BIT;  /* 初始熄灭（假设 LED 低电平点亮/高电平熄灭之一） */

#if BLINK_ENABLE_UART
    uart_init();
#endif

    for (;;) {
        P4 ^= LED_BIT;        /* 翻转在任何可选串口等待之前。 */
#if BLINK_ENABLE_UART
        putstr_("tick=");
        puthex_(tick);
        putstr_("\n");
        tick++;
#endif
        for (uint32_t k = 0; k < BLINK_PERIOD_MS / 10u; k++)
            delay_ms(10u);    /* 仅循环预算；真实周期待板上校准。 */
    }
}
