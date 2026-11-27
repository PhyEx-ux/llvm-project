/*===---- timer.c - STC32G12K128 Timer0 1 ms tick (Priority 2 ISR) ---------===*
 * STC32G12K128 Timer0 1 毫秒节拍实现（优先级 2 硬件中断 + C11 原子操作）
 *===-----------------------------------------------------------------------===*/
#include "timer.h"
#include "board.h"
#include <stdatomic.h>

/* C11 _Atomic state lowered via EA critical sections (jbc/setb 0xAF).
 * C11 _Atomic 共享状态，后端自动经由 EA 临界区（jbc/setb 0xAF）展开。 */
static _Atomic uint32_t g_ms_ticks = 0;
static _Atomic uint16_t g_isr_crc = 0x1D0FU;
static volatile uint16_t g_hb_div = 0;
static _Atomic uint8_t g_hb_flag = 0;

/* Timer0 ISR at hardware priority 2 (can preempt Priority-1 UART1 ISR).
 * Calls shared multi-parameter `board_mix_crc16()` to prove Route B stack ABI
 * reentrancy across ISR & foreground contexts.
 *
 * 硬件优先级 2 的 Timer0 中断（可抢占优先级 1 的 UART1 中断）。
 * 内部调用与主循环共享的多参数函数 `board_mix_crc16()`，演示 Route B 栈 ABI
 * 在中断与前台并发调用下的天然可重入性。 */
void STC32_ISR_PRIO(TMR0_VECTOR, 2) timer0_isr(void) {
  uint32_t now =
      atomic_fetch_add_explicit(&g_ms_ticks, 1UL, memory_order_relaxed) + 1UL;
  g_telemetry.last_tick_ms = now;

  uint16_t prev = atomic_load_explicit(&g_isr_crc, memory_order_relaxed);
  uint16_t next = board_mix_crc16(prev, (uint8_t)now, 0x5AU);
  atomic_store_explicit(&g_isr_crc, next, memory_order_relaxed);

  uint16_t div = (uint16_t)(g_hb_div + 1U);
  if (div >= BOARD_HEARTBEAT_MS) {
    g_hb_div = 0U;
    atomic_store_explicit(&g_hb_flag, 1U, memory_order_release);
  } else {
    g_hb_div = div;
  }
}

void timer0_init_1ms(void) {
  TR0 = 0;
  TF0 = 0;
  /* Mode 0: 16-bit auto-reload, internal clock, 1T (24 MHz / 24000 = 1 kHz).
   * 模式 0：16 位自动重载、内部时钟、1T 分频（24 MHz / 24000 = 1 kHz）。 */
  TMOD &= (uint8_t)~0x0FU;
  AUXR |= AUXR_T0x12;
  TL0 = (uint8_t)(BOARD_TMR0_RELOAD & 0xFFU);
  TH0 = (uint8_t)((BOARD_TMR0_RELOAD >> 8) & 0xFFU);
  ET0 = 1; /* Enable Timer0 interrupt / 使能 Timer0 中断 */
  TR0 = 1; /* Start Timer0 / 启动 Timer0 */
}

uint32_t timer0_millis(void) {
  return atomic_load_explicit(&g_ms_ticks, memory_order_acquire);
}

uint16_t timer0_isr_crc(void) {
  return atomic_load_explicit(&g_isr_crc, memory_order_acquire);
}

uint8_t timer0_consume_heartbeat(void) {
  return atomic_exchange_explicit(&g_hb_flag, 0U, memory_order_acq_rel);
}
