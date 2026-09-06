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

#if defined(HOST_BUILD) && defined(STC32_REAL_HW)
#error "HOST_BUILD 与 STC32_REAL_HW 必须二选一"
#endif

#ifdef STC32_REAL_HW
/* ---------- 真机模板：未经真机实测，烧录前必须完成下方 TODO ----------
 * 与单文件 01-selftest.c 使用相同 SCON/TI 约定。此模板尚未提供完整的
 * 板级初始化，不能把编译/链接通过当作波特率或引脚已验证。
 */
#define SCON (*(volatile uint8_t *)0x98)
#define SBUF (*(volatile uint8_t *)0x99)
#define TI_GET() ((SCON >> 1) & 1u)
#define TI_SET(v) do { SCON = (uint8_t)((SCON & ~0x02u) | ((v) ? 0x02u : 0u)); } while (0)
static void uart_init(void)
{
    /* TODO(真机): 配置 TX GPIO 模式与实际映射引脚（如 P3.1）。 */
    SCON = 0x40u;  /* 模式 1：8 位可变波特率，REN=0。 */
    /* TODO(真机): 核对主频，设置 AUXR/BRT 或 T1 波特率发生器；
     * 访问扩展 SFR 前按 STC32G 手册设置 P_SW2。不可省略后直接烧录。 */
    TI_SET(1);
}
static void uart_putc(char c)
{
    while (!TI_GET()) { }
    TI_SET(0);
    SBUF = (uint8_t)c;
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
