/*===---- board.h - STC32G12K128 board profile & shared telemetry ----------===*
 * STC32G12K128 板级配置与共享遥测数据结构
 *===-----------------------------------------------------------------------===*/
#ifndef STC32G12K128_DEMO_BOARD_H
#define STC32G12K128_DEMO_BOARD_H

#include <stc32.h>
#include <stc32_gpio.h>

/* Clock & serial parameters (verified on STC32G12K128 @ 24 MHz HIRC):
 * 时钟与串口参数：
 *   - AiCube-ISP: User HIRC = 24.000 MHz, EEPROM <= 1 KiB (0 recommended).
 *     AiCube-ISP 设置：用户 IRC = 24.000 MHz，EEPROM <= 1 KiB（推荐设为 0）。
 *   - UART1: P3.0 (RxD) / P3.1 (TxD), 115200 8N1 via Timer2 1T.
 *     UART1：P3.0 (RxD) / P3.1 (TxD)，Timer2 1T 产生 115200 8N1 波特率。
 *   - Timer0: 16-bit auto-reload 1T -> 1.000 ms tick (65536 - 24000 = 0xA240).
 *     Timer0：16 位自动重载 1T 模式，精确 1.000 ms 节拍（65536 - 24000 = 0xA240）。
 */
#define BOARD_FOSC_HZ        24000000UL
#define BOARD_UART_BAUD      115200UL
#define BOARD_UART_T2_RELOAD 0xFFCCU /* 65536 - round(24e6 / (4 * 115200)) */
#define BOARD_TMR0_RELOAD    0xA240U /* 65536 - (24e6 / 1000) */
#define BOARD_HEARTBEAT_MS   500U    /* Heartbeat LED period (ms) / 心跳灯周期（毫秒） */

/* Board pin map via <stc32_gpio.h>: a single pin macro simultaneously derives
 * Port SFR (P4/P4M1/P4M0), byte mask (STC32_PIN_MASK -> 1U << 5 = 0x20), and
 * hardware bit lvalue (STC32_PIN_BIT -> P45, single cpl/setb/clr instruction).
 * 板级引脚表（基于 <stc32_gpio.h>）：单个引脚宏同时推导端口 SFR（P4/P4M1/P4M0）、
 * 字节掩码（STC32_PIN_MASK -> 1U << 5 = 0x20）与硬件位左值（STC32_PIN_BIT -> P45）。 */
#define BOARD_LED            STC32_P45
#define BOARD_UART1_RXD      STC32_P30
#define BOARD_UART1_TXD      STC32_P31

/* Linker-script symbols defined in stc32g12k128.ld.
 * 在 stc32g12k128.ld 中定义的链接脚本符号。 */
extern const unsigned char fw_flash_origin;
extern const unsigned char fw_version;

/* G11 fixed-address telemetry block in XDATA (0x010100, noinit, retained
 * across linker GC and independently verified by mcs251-placement-verify).
 * 位于 XDATA 0x010100 的 G11 固定地址遥测块（不初始化、GC 强制保活，
 * 链接期由独立进程 mcs251-placement-verify 校验）。 */
typedef struct {
  uint32_t magic;        /* Initialization sentinel / 初始化魔数 */
  uint32_t boot_count;   /* Soft-reset boot counter / 复位启动计数 */
  uint32_t last_tick_ms; /* Latest 1 ms tick timestamp / 最近毫秒节拍 */
  uint16_t rx_bytes;     /* UART1 RX byte counter / 串口接收字节计数 */
  uint16_t tx_bytes;     /* UART1 TX byte counter / 串口发送字节计数 */
} BoardTelemetry;

#if !defined(BOARD_DEFINE_TELEMETRY)
extern STC32_XDATA volatile BoardTelemetry g_telemetry;
#endif

/* Small aggregate returned & passed by value (exercises Route B struct ABI).
 * 按值返回与按值传递的小结构体（用于验证 Route B 聚合 ABI）。 */
typedef struct {
  uint16_t checksum; /* Aggregated check value / 综合校验值 */
  uint16_t passed;   /* Passed check count / 通过项数 */
} SelfCheckResult;

/* Reentrant multi-parameter helper shared by main() and Timer0 ISR.
 * 由 main() 与 Timer0 中断服务函数并发共享的可重入多参数辅助函数。 */
uint16_t board_mix_crc16(uint16_t acc, uint8_t byte_val, uint8_t salt);

#endif /* STC32G12K128_DEMO_BOARD_H */
