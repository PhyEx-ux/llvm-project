/*===---- uart.c - STC32G12K128 UART1 driver (115200 8N1 + RX ISR) ---------===*
 * STC32G12K128 UART1 串口驱动实现（115200 8N1 + 优先级 1 接收中断）
 *===-----------------------------------------------------------------------===*/
#include "uart.h"
#include "board.h"

#define UART_RX_BUF_SIZE 64U
#define UART_RX_BUF_MASK (UART_RX_BUF_SIZE - 1U)

/* 64-byte RX ring buffer in on-chip XDATA / 位于片上 XDATA 的 64 字节接收环形缓冲区 */
static STC32_XDATA volatile uint8_t g_rx_buf[UART_RX_BUF_SIZE];
static volatile uint8_t g_rx_head = 0;
static volatile uint8_t g_rx_tail = 0;

/* Powers-of-10 table in __code Flash (avoids 32-bit runtime division).
 * 存放于 __code Flash 中的 10 的幂次表（避免 32 位运行期除法依赖）。 */
static STC32_CODE const uint32_t kPow10[10] = {
    1000000000UL, 100000000UL, 10000000UL, 1000000UL, 100000UL,
    10000UL,      1000UL,      100UL,      10UL,      1UL,
};

static STC32_CODE const char kHexUpper[] = "0123456789ABCDEF";
static STC32_CODE const char kHexLower[] = "0123456789abcdef";

/* UART1 ISR at hardware priority 1 (lower than Timer0 priority 2).
 * mcs251-lld automatically derives preemption pair (UART1 -> TMR0), verifies
 * WP2 reentrancy safety, and synthesizes IP/IPH register initialization.
 *
 * 硬件优先级 1 的 UART1 接收中断（低于优先级 2 的 Timer0 中断）。
 * mcs251-lld 会自动推导 (UART1 -> TMR0) 抢占关系、执行 WP2 重入安全检查，
 * 并自动合成 IP/IPH 优先级寄存器初始化函数。 */
void STC32_ISR_PRIO(UART1_VECTOR, 1) uart1_isr(void) {
  if (RI) {
    uint8_t ch = SBUF;
    RI = 0;
    uint8_t next = (uint8_t)((g_rx_head + 1U) & UART_RX_BUF_MASK);
    if (next != g_rx_tail) {
      g_rx_buf[g_rx_head] = ch;
      g_rx_head = next;
    }
    g_telemetry.rx_bytes = (uint16_t)(g_telemetry.rx_bytes + 1U);
  }
}

void uart1_init(void) {
#if !defined(QEMU_MODE)
  /* Real hardware: route UART1 to P3.0 (RxD) / P3.1 (TxD), quasi-bidirectional.
   * Never modify P3.2 (USB ISP boot detect pin).
   * 真机配置：将 UART1 映射至 P3.0 (RxD) / P3.1 (TxD) 并设为准双向口；
   * 绝不触碰 P3.2（USB 下载检测引脚）。 */
  P_SW1 = 0x00U;
  STC32_GPIO_SET_PORT_MODE(
      STC32_PIN_PORT(BOARD_UART1_RXD),
      STC32_PIN_MASK(BOARD_UART1_RXD) | STC32_PIN_MASK(BOARD_UART1_TXD),
      STC32_GPIO_MODE_QUASI);

  /* Stop Timer2 and clear C/T bit (internal clock) before loading reload.
   * 写入重载值前先停止 Timer2 并清零 C/T 位（选择内部时钟，避免误入外部计数）。 */
  AUXR &= (uint8_t)~(AUXR_T2R | AUXR_T2CT);
  SCON = 0x50U; /* Mode 1 (8-bit variable baud), REN=1, TI=0, RI=0 / 模式 1，允许接收 */
  T2L = (uint8_t)(BOARD_UART_T2_RELOAD & 0xFFU);
  T2H = (uint8_t)((BOARD_UART_T2_RELOAD >> 8) & 0xFFU);

  /* S1BRT=1 (UART1 uses Timer2), T2x12=1 (1T), T2R=1 (start Timer2).
   * 配置 UART1 使用 Timer2（1T 模式）作为波特率发生器并启动 Timer2。 */
  AUXR |= (uint8_t)(AUXR_S1BRT | AUXR_T2x12 | AUXR_T2R);
  ES = 1; /* Enable UART1 interrupt / 使能 UART1 中断 */
#endif
}

void uart1_putc(char c) {
#if defined(QEMU_MODE)
  /* QEMU test-port mode writes directly to SBUF without TI polling.
   * QEMU 测试端口模式直接写 SBUF，不轮询 TI 标志。 */
  SBUF = (uint8_t)c;
#else
  /* Real hardware: write SBUF, poll hardware TI flag, then clear TI.
   * 真机发送：写 SBUF -> 轮询硬件 TI 置位 -> 清零 TI。 */
  SBUF = (uint8_t)c;
  while (!TI) {
  }
  TI = 0;
#endif
  g_telemetry.tx_bytes = (uint16_t)(g_telemetry.tx_bytes + 1U);
}

void uart1_puts(const char *s) {
  while (*s) {
    if (*s == '\n')
      uart1_putc('\r');
    uart1_putc(*s++);
  }
}

uint8_t uart1_getc_nonblock(uint8_t *out) {
  /* Enter atomic EA critical section to read/advance ring tail safely.
   * 进入 EA 原子临界区，安全读取并推进环形缓冲区尾指针。 */
  uint8_t ea = stc32_ea_save_disable();
  if (g_rx_tail == g_rx_head) {
    stc32_ea_restore(ea);
    return 0;
  }
  *out = g_rx_buf[g_rx_tail];
  g_rx_tail = (uint8_t)((g_rx_tail + 1U) & UART_RX_BUF_MASK);
  stc32_ea_restore(ea);
  return 1;
}

static void uart1_put_hex_n(uint32_t v, uint8_t digits, uint8_t upper) {
  STC32_CODE const char *tab = upper ? kHexUpper : kHexLower;
  while (digits > 0U) {
    --digits;
    uint8_t nib = (uint8_t)((v >> (digits * 4U)) & 0x0FU);
    uart1_putc(tab[nib]);
  }
}

void uart1_put_hex8(uint8_t v) { uart1_put_hex_n((uint32_t)v, 2U, 1U); }
void uart1_put_hex16(uint16_t v) { uart1_put_hex_n((uint32_t)v, 4U, 1U); }
void uart1_put_hex32(uint32_t v) { uart1_put_hex_n(v, 8U, 1U); }

void uart1_put_u32(uint32_t v) {
  /* Convert uint32_t to decimal via repeated subtraction using kPow10.
   * 使用 kPow10 表通过减法将 32 位无符号整数转为十进制字符串。 */
  uint8_t started = 0U;
  for (uint8_t i = 0U; i < 10U; ++i) {
    uint32_t p = kPow10[i];
    uint8_t d = 0U;
    while (v >= p) {
      v -= p;
      ++d;
    }
    if (d != 0U || started || i == 9U) {
      started = 1U;
      uart1_putc((char)('0' + d));
    }
  }
}

void uart1_printf(const char *fmt, ...) {
  /* Uses Route B stack-cursor <stdarg.h> ABI (unbounded variadic arguments).
   * 使用 Route B 栈游标 <stdarg.h> 变参 ABI（无参数数量硬上限）。 */
  va_list ap;
  va_start(ap, fmt);
  while (*fmt) {
    if (*fmt != '%') {
      if (*fmt == '\n')
        uart1_putc('\r');
      uart1_putc(*fmt++);
      continue;
    }
    ++fmt;
    char spec = *fmt ? *fmt++ : '\0';
    switch (spec) {
    case 's': {
      const char *s = va_arg(ap, const char *);
      uart1_puts(s ? s : "(null)");
      break;
    }
    case 'c': {
      int ch = va_arg(ap, int);
      uart1_putc((char)ch);
      break;
    }
    case 'u': {
      uint32_t v = va_arg(ap, unsigned int);
      uart1_put_u32(v);
      break;
    }
    case 'd': {
      int32_t v = va_arg(ap, int);
      if (v < 0) {
        uart1_putc('-');
        uart1_put_u32((uint32_t)(-(v + 1)) + 1UL);
      } else {
        uart1_put_u32((uint32_t)v);
      }
      break;
    }
    case 'x': {
      uint32_t v = va_arg(ap, unsigned int);
      uart1_put_hex_n(v, 8U, 0U);
      break;
    }
    case 'X': {
      uint32_t v = va_arg(ap, unsigned int);
      uart1_put_hex_n(v, 8U, 1U);
      break;
    }
    case '%':
      uart1_putc('%');
      break;
    default:
      uart1_putc('%');
      if (spec)
        uart1_putc(spec);
      break;
    }
  }
  va_end(ap);
}
