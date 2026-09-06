/*
 * uart.h — 串口输出（目标机路径）与宿主机输出（对拍路径）的统一接口。
 *
 * 目标机（STC32/QEMU）：QEMU 的 UART 模型下，直接向 SBUF(0x99) 写入一个字节
 *   即刻出现在串口 transcript 上，无需波特率初始化、无需 TI 轮询。
 *   SFR 访问使用"固定地址 volatile 指针"——这正是后端 direct 0x80-0xff
 *   寻址通路（SFR 的唯一通路）所覆盖的形态。
 *
 * 宿主机（make check 的期望生成侧）：同名的 puts/hex 函数走 printf，
 *   使目标机与宿主机输出格式完全一致，可逐行 diff。
 */
#ifndef MCS251_DEMO_UART_H
#define MCS251_DEMO_UART_H

#include <stdint.h>
#include "board_config.h"

#if defined(HOST_BUILD) && defined(STC32_REAL_HW)
#error "HOST_BUILD 与 STC32_REAL_HW 必须二选一"
#endif

#ifdef STC32_REAL_HW
/* 防止烧录后再打开终端错过一次性输出。仅延迟重发状态，不重跑自检。
 * volatile避免O2删除循环；周期只是低速预算，未经真机精确校准。 */
static void uart_status_pause(void)
{
    volatile uint16_t outer = 200;
    do {
        volatile uint16_t inner = 4000;
        while (--inner) { }
    } while (--outer);
}
/* ---------- 板级 UART1（参数来自生成的 board_config.h） ----------
 * 当前配置使用 P3.0/P3.1、SCON 模式1和 Timer2 1T；HRIC/BAUD/重载值由
 * boards 目录下的板级片段提供。仅访问标准 SFR，无需打开扩展 SFR。P_SW1全写0也复位
 * 其它复用路由，适用于此独占演示；集成其它外设需重新审计。
 */
#define EXPECTED_UART_RELOAD \
    (0x10000u - ((HRIC + (2u * BAUD)) / (4u * BAUD)))
_Static_assert(UART_RELOAD == EXPECTED_UART_RELOAD,
               "UART_RELOAD 与 HRIC/BAUD 不一致");
#define AUXR (*(volatile uint8_t *)0x8E)
#define SCON (*(volatile uint8_t *)0x98)
#define SBUF (*(volatile uint8_t *)0x99)
#define P_SW1 (*(volatile uint8_t *)0xA2)
#define P3M1 (*(volatile uint8_t *)0xB1)
#define P3M0 (*(volatile uint8_t *)0xB2)
#define T2H (*(volatile uint8_t *)0xD6)
#define T2L (*(volatile uint8_t *)0xD7)
static void uart_init(void)
{
    P_SW1 = 0x00u; /* V1真机已验证配置：RxD=P3.0、TxD=P3.1；不假定复位读值。 */
    P3M1 &= (uint8_t)~0x03u;
    P3M0 &= (uint8_t)~0x03u;  /* P3.0/1准双向弱上拉；不改P3.2。 */
    AUXR &= (uint8_t)~0x10u; /* T2R=0：停止后写计数/重载寄存器。 */
    AUXR &= (uint8_t)~0x08u; /* T2_C/T=0：内部时钟定时器，非外部计数器。 */
    SCON = 0x50u;           /* 模式1，REN=1，TI/RI清零。 */
    T2L = (uint8_t)UART_RELOAD;
    T2H = (uint8_t)(UART_RELOAD >> 8);
    AUXR |= 0x01u;          /* S1BRT=1：UART1使用Timer2。 */
    AUXR |= 0x04u;          /* T2x12=1：1T。 */
    AUXR |= 0x10u;          /* T2R=1：最后启动；合计置位0x15，不置bit3。 */
}
static void uart_putc(char c)
{
    SBUF = (uint8_t)c;
    while (!(SCON & 0x02u)) { } /* 硬件在停止位开始时置TI；QEMU不模拟。 */
    SCON &= (uint8_t)~0x02u;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

#elif defined(HOST_BUILD)
/* ---------- 宿主机路径：stdio，用于生成期望输出 ---------- */
#include <stdio.h>
static void uart_init(void) { }
static void uart_putc(char c) { putchar(c); }
static void uart_puts(const char *s) { fputs(s, stdout); }

#else
/* ---------- QEMU 默认路径：test-port，不模拟波特率且不会置 TI ---------- */
static void uart_init(void) { }
#define SBUF (*(volatile uint8_t *)0x99)
static void uart_putc(char c) { SBUF = (uint8_t)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
#endif

/* 以十六进制打印 8/16/32 位值（大写、固定位宽）——逐位移位取半字节。 */
static void uart_hex8(uint8_t v)
{
    uint8_t hi = (uint8_t)(v >> 4) & 0x0Fu;   /* 常量移位：后端展开路径 */
    uint8_t lo = v & 0x0Fu;
    uart_putc((char)(hi < 10 ? '0' + hi : 'A' + hi - 10));
    uart_putc((char)(lo < 10 ? '0' + lo : 'A' + lo - 10));
}

static void uart_hex16(uint16_t v)
{
    uart_hex8((uint8_t)(v >> 8));             /* 高字节在低槽：大端打印序 */
    uart_hex8((uint8_t)v);
}

static void uart_hex32(uint32_t v)
{
    uart_hex16((uint16_t)(v >> 16));
    uart_hex16((uint16_t)v);
}

#endif /* MCS251_DEMO_UART_H */
