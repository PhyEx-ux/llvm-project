/*===---- uart.h - STC32G12K128 UART1 driver (Timer2 1T, RX IRQ ring) ------===*
 * STC32G12K128 UART1 串口驱动接口（Timer2 1T 波特率 + 接收中断环形缓冲）
 *===-----------------------------------------------------------------------===*/
#ifndef STC32G12K128_DEMO_UART_H
#define STC32G12K128_DEMO_UART_H

#include <stdarg.h>
#include <stdint.h>

/* Initialize UART1 (P3.0/P3.1, 115200 8N1) and enable RX interrupt.
 * 初始化 UART1（P3.0/P3.1，115200 8N1）并开启接收中断。 */
void uart1_init(void);

/* Blocking single-byte / string / formatted numeric transmit helpers.
 * 阻塞式单字节、字符串及十六进制/十进制数字发送函数。 */
void uart1_putc(char c);
void uart1_puts(const char *s);
void uart1_put_hex8(uint8_t v);
void uart1_put_hex16(uint16_t v);
void uart1_put_hex32(uint32_t v);
void uart1_put_u32(uint32_t v);

/* Standalone minimal printf supporting %s, %c, %u, %d, %x, %X, %%.
 * 独立精简版 printf（支持 %s、%c、%u、%d、%x、%X、%%，无外部 libc 依赖）。 */
void uart1_printf(const char *fmt, ...);

/* Non-blocking RX read from the IRQ-driven XDATA ring buffer.
 * Returns 1 and writes *out if a byte was available, otherwise 0.
 * 从中断驱动的 XDATA 环形缓冲区非阻塞读取 1 字节；有数据时写入 *out 并返回 1，否则返回 0。 */
uint8_t uart1_getc_nonblock(uint8_t *out);

#endif /* STC32G12K128_DEMO_UART_H */
