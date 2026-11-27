/*===---- main.c - STC32G12K128 Modern LLVM/Clang MCS-251 Firmware Demo ----===*
 * STC32G12K128 现代 LLVM/Clang MCS-251 固件示例主程序
 *
 * Showcases the full modern MCS-251 toolchain on STC32G12K128:
 * 全面演示当前 MCS-251 工具链在 STC32G12K128 上的现代工程能力：
 *   - Built-in <stc32.h> (AS6 direct SFRs, XFR EAXSFR/EAXRAM, hardware sbits)
 *     内置 <stc32.h>（AS6 直寻址 SFR、XFR 切换宏、硬件位左值）
 *   - GNU linker script (-T stc32g12k128.ld) + --gc-sections + KEEP(...)
 *     GNU 风格链接脚本 + 节级死代码裁剪 + KEEP 强制保活
 *   - Route B reentrant stack ABI (shared multi-param ISR helper, struct-by-
 *     value, standalone <stdarg.h> uart1_printf)
 *     Route B 可重入栈 ABI（ISR 共享多参函数、结构体按值传参/返回、标准变参）
 *   - C11 <stdatomic.h> (EA critical-section atomic 1/2/4-byte ops)
 *     C11 原子操作（基于 EA 临界区的 1/2/4 字节原子展开）
 *   - Hardware ISR priority auto-config (TMR0 prio 2, UART1 prio 1 ->
 *     linker-synthesized __mcs251_isr_priority_init + WP2 preemption audit)
 *     硬件中断优先级自动配置（链接器自动合成 IP/IPH 初始化 + WP2 抢占重入审计）
 *   - G11 fixed placement at XDATA 0x010100 with mcs251-placement-verify
 *     G11 固定地址放置（XDATA 0x010100）与独立进程零信任校验
 *===-----------------------------------------------------------------------===*/

#define BOARD_DEFINE_TELEMETRY
#include "board.h"
#include "timer.h"
#include "uart.h"
#include <stdatomic.h>

/* G11 fixed-address telemetry block at XDATA 0x010100 (verified by
 * mcs251-placement-verify under --gc-sections).
 * G11 固定放置于 XDATA 0x010100 的遥测块（--gc-sections 下由
 * mcs251-placement-verify 独立校验）。 */
STC32_XDATA volatile BoardTelemetry STC32_PLACE_AT(0x010100) STC32_NOINIT
    STC32_RETAIN g_telemetry;

/* Read-only strings placed in Flash (__code / AS4).
 * 存放于 Flash（__code / AS4）的只读字符串常量。 */
static STC32_CODE const char kBanner[] =
    "\n========================================\n"
    " STC32G12K128 LLVM/Clang MCS-251 Demo\n"
    " 24MHz HIRC | UART1 115200 8N1 | P4.5 LED\n"
    "========================================\n";

static STC32_CODE const char kHelp[] =
    "Commands:\n"
    "  h/? - show this help\n"
    "  t   - show 1ms tick, LED & telemetry\n"
    "  l   - toggle P4.5 LED immediately\n"
    "  s   - run on-chip ABI/atomic/XFR self-check\n"
    "  r   - read clock XFRs (CLKSEL/CLKDIV)\n";

/* Kept explicitly by KEEP(*(.text._fw_info_keep)) in stc32g12k128.ld even
 * under --gc-sections and -flto, proving linker-script KEEP works end-to-end.
 * 即使开启 --gc-sections 或 -flto，也由 stc32g12k128.ld 中的
 * KEEP(*(.text._fw_info_keep)) 强制保留，用于验证链接脚本 KEEP 机制。 */
uint16_t fw_info_keep(void) {
  return (uint16_t)(uintptr_t)&fw_version;
}

/* Unused helper that SHOULD be garbage-collected by --gc-sections.
 * 未被引用的死函数，应在 --gc-sections 链接阶段被自动裁剪。 */
uint16_t dead_unused_helper(uint16_t x) {
  return (uint16_t)(x * 3U + 7U);
}

/* Reentrant multi-parameter helper called from BOTH main() and Timer0 ISR.
 * 同时被主循环 main() 与 Timer0 中断调用的可重入多参数函数。 */
uint16_t board_mix_crc16(uint16_t acc, uint8_t byte_val, uint8_t salt) {
  uint16_t v = (uint16_t)(acc ^ ((uint16_t)(byte_val ^ salt) << 8));
  for (uint8_t i = 0U; i < 4U; ++i) {
    if (v & 0x8000U)
      v = (uint16_t)((v << 1) ^ 0x1021U);
    else
      v = (uint16_t)(v << 1);
  }
  return v;
}

static SelfCheckResult run_self_check(uint16_t seed) {
  SelfCheckResult res = {0U, 0U};

  /* 1. Multi-parameter reentrant helper + inline _nop_()
   * 1. 多参数可重入函数与内联汇编 _nop_() 自检 */
  _nop_();
  NOP4();
  uint16_t c1 = board_mix_crc16(seed, 0xA5U, 0x3CU);
  uint16_t c2 = board_mix_crc16(seed, 0xA5U, 0x3CU);
  if (c1 == c2 && c1 != 0U)
    res.passed++;
  res.checksum ^= c1;

  /* 2. C11 _Atomic fetch_add / compare_exchange (EA critical section)
   * 2. C11 _Atomic 原子加与比较交换自检（EA 临界区展开） */
  _Atomic uint32_t atom = 100UL;
  uint32_t old = atomic_fetch_add(&atom, 23UL);
  uint32_t expected = 123UL;
  if (old == 100UL && atomic_compare_exchange_strong(&atom, &expected, 200UL) &&
      atomic_load(&atom) == 200UL)
    res.passed++;
  res.checksum ^= (uint16_t)atomic_load(&atom);

  /* 3. G11 fixed-address XDATA readback
   * 3. G11 固定地址 XDATA 读写回读自检 */
  if (g_telemetry.magic == 0x53544332UL)
    res.passed++;
  res.checksum ^= (uint16_t)g_telemetry.boot_count;

  /* 4. Linker script PROVIDE symbol + __code Flash table read
   * 4. 链接脚本 PROVIDE 符号与 __code Flash 常量读取自检 */
  uint32_t ver = (uint32_t)(uintptr_t)&fw_version;
  uint32_t fl_org = (uint32_t)(uintptr_t)&fw_flash_origin;
  if (ver == 0x0100UL && fl_org == STC32_FLASH_BASE && kBanner[1] == '=')
    res.passed++;
  res.checksum ^= (uint16_t)ver;

  return res;
}

static void print_self_check(SelfCheckResult r) {
  uart1_printf("[CHECK] passed=%u/4 checksum=0x%X\n", (unsigned int)r.passed,
               (unsigned int)r.checksum);
}

static void print_clock_xfr(void) {
  /* Enable EAXFR inside an EA critical section to read clock XFRs safely.
   * 在 EA 临界区内临时开启 EAXFR 安全读取时钟扩展寄存器（XFR）。 */
  uint8_t ea = stc32_ea_save_disable();
  EAXSFR();
  uint8_t clksel = CLKSEL;
  uint8_t clkdiv = CLKDIV;
  uint8_t hirccr = HIRCCR;
  EAXRAM();
  stc32_ea_restore(ea);

  uart1_puts("[XFR] CLKSEL=0x");
  uart1_put_hex8(clksel);
  uart1_puts(" CLKDIV=0x");
  uart1_put_hex8(clkdiv);
  uart1_puts(" HIRCCR=0x");
  uart1_put_hex8(hirccr);
  uart1_puts("\n");
}

static void handle_command(uint8_t ch) {
  switch (ch) {
  case 'h':
  case '?':
    uart1_puts(kHelp);
    break;
  case 't':
    uart1_printf("[STAT] tick=%ums led=%u boot=%u rx=%u tx=%u isr_crc=0x%X\n",
                 (unsigned int)timer0_millis(),
                 (unsigned int)STC32_GPIO_READ(BOARD_LED),
                 (unsigned int)g_telemetry.boot_count,
                 (unsigned int)g_telemetry.rx_bytes,
                 (unsigned int)g_telemetry.tx_bytes,
                 (unsigned int)timer0_isr_crc());
    break;
  case 'l':
    STC32_GPIO_TOGGLE(BOARD_LED); /* Hardware `cpl 0xc5` / 硬件单条位翻转指令 */
    uart1_printf("[LED] P4.5=%u\n", (unsigned int)STC32_GPIO_READ(BOARD_LED));
    break;
  case 's':
    print_self_check(run_self_check(0x1234U));
    break;
  case 'r':
    print_clock_xfr();
    break;
  case '\r':
  case '\n':
    break;
  default:
    uart1_printf("[ECHO] '%c' (0x", (int)ch);
    uart1_put_hex8(ch);
    uart1_puts(")\n");
    break;
  }
}

int main(void) {
  /* Full-speed STC32G12K128 @ 24 MHz HIRC: 0 Flash/XDATA wait states.
   * 24 MHz 全速运行：将 Flash 与外部总线等待周期设为 0。 */
  WTST = 0x00U;
  CKCON = 0x00U;

  /* Initialize G11 fixed XDATA telemetry block (persists across soft resets).
   * 初始化固定地址 XDATA 遥测块（软复位后保留 boot_count 计数）。 */
  if (g_telemetry.magic != 0x53544332UL) {
    g_telemetry.magic = 0x53544332UL;
    g_telemetry.boot_count = 0UL;
  }
  g_telemetry.boot_count = g_telemetry.boot_count + 1UL;
  g_telemetry.last_tick_ms = 0UL;
  g_telemetry.rx_bytes = 0U;
  g_telemetry.tx_bytes = 0U;

  /* Configure P4.5 LED as push-pull output (P4M1.5=0, P4M0.5=1 via pin mask).
   * 通过引脚描述符配置 P4.5 LED 为推挽输出（自动操作 P4M1/P4M0 与掩码 0x20）。 */
  STC32_GPIO_SET_MODE(BOARD_LED, STC32_GPIO_MODE_PP);
  STC32_GPIO_SET(BOARD_LED);

  /* Apply linker-synthesized STC32G hardware ISR priorities (TMR0=2, UART1=1).
   * 调用链接器自动合成的 STC32G 硬件中断优先级初始化函数（TMR0=2, UART1=1）。 */
  stc32_isr_priority_init();

  uart1_init();
  timer0_init_1ms();

  /* Enable global interrupts (hardware `setb 0xaf`).
   * 开启全局总中断（硬件 `setb 0xaf` 指令）。 */
  stc32_ea_enable();

  uart1_puts(kBanner);
  uart1_printf("[BOOT] fw=0x%X flash=0x%X boot_count=%u\n",
               (unsigned int)(uintptr_t)&fw_version,
               (unsigned int)(uintptr_t)&fw_flash_origin,
               (unsigned int)g_telemetry.boot_count);
  print_self_check(run_self_check(0x1234U));
  uart1_puts(kHelp);

#if defined(QEMU_MODE)
  /* In QEMU test-port mode, exercise command handlers once and halt cleanly.
   * QEMU 测试端口模式：依次触发命令处理函数后停机等待。 */
  handle_command('t');
  handle_command('l');
  handle_command('r');
  uart1_puts("STC32G12K128-DEMO-READY\n");
  for (;;) {
    _nop_();
  }
#else
  /* Main loop: 500 ms LED heartbeat + interactive UART1 command shell.
   * 主循环：每 500 ms 翻转 LED 并输出心跳，同时轮询处理 UART1 交互命令。 */
  for (;;) {
    if (timer0_consume_heartbeat()) {
      STC32_GPIO_TOGGLE(BOARD_LED); /* Hardware `cpl 0xc5` / 硬件单条位翻转指令 */
      uart1_printf("[HB] tick=%ums led=%u\n", (unsigned int)timer0_millis(),
                   (unsigned int)STC32_GPIO_READ(BOARD_LED));
    }
    uint8_t ch = 0U;
    if (uart1_getc_nonblock(&ch))
      handle_command(ch);
  }
#endif
}
