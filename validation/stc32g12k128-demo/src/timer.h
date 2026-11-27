/*===---- timer.h - STC32G12K128 Timer0 1 ms tick & atomic counter ---------===*
 * STC32G12K128 Timer0 1 毫秒系统节拍与原子计数器接口
 *===-----------------------------------------------------------------------===*/
#ifndef STC32G12K128_DEMO_TIMER_H
#define STC32G12K128_DEMO_TIMER_H

#include <stdint.h>

/* Configure Timer0 in 16-bit auto-reload 1T mode for 1 ms interrupts.
 * 配置 Timer0 为 16 位自动重载 1T 模式并开启 1 ms 定时中断。 */
void timer0_init_1ms(void);

/* Atomically read the 32-bit millisecond uptime counter.
 * 原子读取 32 位毫秒运行时间计数器。 */
uint32_t timer0_millis(void);

/* Atomically read the rolling CRC updated inside Timer0 ISR.
 * 原子读取在 Timer0 中断中持续更新的滚动校验值。 */
uint16_t timer0_isr_crc(void);

/* Atomically consume and clear the 500 ms heartbeat flag (returns 1 if due).
 * 原子读取并清零 500 ms 心跳标志（到达周期时返回 1，否则返回 0）。 */
uint8_t timer0_consume_heartbeat(void);

#endif /* STC32G12K128_DEMO_TIMER_H */
