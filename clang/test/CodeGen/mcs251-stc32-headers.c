// Verify built-in STC32 / MCS-251 resource headers work out of the box with
// zero -I flags across C11, C23, Keil dialect, +int16, C++17, STC32G12K128,
// and STC32G144K246, and lower to the expected MCS-251 hardware instructions.
//
// RUN: %clang --target=mcs251-unknown-none -std=c11 -Wall -Wextra -Werror -O2 -c %s -o %t.g12.o
// RUN: llvm-objdump -d %t.g12.o | FileCheck %s --check-prefix=ASM
//
// RUN: %clang --target=mcs251-unknown-none -std=c11 -DSTC32G144K246 -DSTC32_ENABLE_KEIL_TYPES -Wall -Wextra -Werror -O2 -c %s -o %t.g144.o
// RUN: %clang --target=mcs251-unknown-none -std=c11 -fmcs251-keil -DSTC32_ENABLE_KEIL_TYPES -Wall -Wextra -Werror -O2 -c %s -o %t.keil.o
// RUN: %clang --target=mcs251-unknown-none -std=c23 -Wall -Wextra -Werror -O2 -c %s -o %t.c23.o
// RUN: %clang --target=mcs251-unknown-none -x c++ -std=c++17 -Wall -Wextra -Werror -O2 -c %s -o %t.cxx.o
// RUN: %clang --target=mcs251-unknown-none -Xclang -target-feature -Xclang +int16 -std=c11 -Wall -Wextra -Werror -O2 -c %s -o %t.i16.o
//
// Verify end-to-end link (with linker-synthesized __mcs251_isr_priority_init
// and G11 fixed placement on STC32G12K128).
// RUN: %clang --target=mcs251-unknown-none -std=c11 -O2 -DSTC32_LINK_SMOKE %s \
// RUN:   -Wl,--area-start=CSEG=0xff0700 -Wl,--area-start=XSEG=0x010000 \
// RUN:   -Wl,--area-start=XINIT=0xff8000 -Wl,--area-start=XDATA_INIT=0xff8800 \
// RUN:   -Wl,--flash-base=0xfe0000 -Wl,--flash-size=0x20000 \
// RUN:   -Wl,--keep-symbols -o %t.fw.elf
// RUN: llvm-readelf -h -s %t.fw.elf | FileCheck %s --check-prefix=ELF

#include <stc32.h>
#include <stc32_gpio.h>

#define TEST_LED STC32_P45
#ifdef __cplusplus
#define TEST_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define TEST_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif
TEST_STATIC_ASSERT(STC32_PIN_PORT(TEST_LED) == 4, "port must be 4");
TEST_STATIC_ASSERT(STC32_PIN_NUM(TEST_LED) == 5, "bit must be 5");
TEST_STATIC_ASSERT(STC32_PIN_MASK(TEST_LED) == 0x20U, "mask must be 0x20");
TEST_STATIC_ASSERT(STC32_PORT_SFR_ADDR(4) == 0xC0U, "P4 base must be 0xC0");
TEST_STATIC_ASSERT(STC32_PIN_BITADDR(TEST_LED) == 0xC5U, "P4.5 bitaddr 0xC5");
TEST_STATIC_ASSERT(STC32_PIN_MASK(STC32_P30) == STC32_PIN_MASK_0, "P3.0 mask");
TEST_STATIC_ASSERT(STC32_PIN_MASK(STC32_P77) == STC32_PIN_MASK_7, "P7.7 mask");
TEST_STATIC_ASSERT(STC32_PIN_BITADDR(STC32_P77) == 0xFFU, "P7.7 bitaddr 0xFF");

STC32_CODE const unsigned char kBanner[4] = { 'S', 'T', 'C', '!' };
STC32_XDATA volatile unsigned int STC32_PLACE_AT(0x010100) STC32_NOINIT gFixedBuf;

// ASM-LABEL: <_sfr_and_xfr_io>:
// Direct AS6 SFR store to SBUF (0x99) and P0 (0x80)
// ASM-DAG: mov 0x99, r{{[0-9]+}}
// ASM-DAG: mov 0x80, r{{[0-9]+}}
unsigned char sfr_and_xfr_io(unsigned char v) {
  SBUF = v;
  P0 = v;
  EAXSFR();
  I2CCFG = 0x5Au;
  unsigned char r = I2CCFG;
#if defined(STC32G144K246)
  P_SW4 |= P_SW4_QSPI_S0;
  r ^= CPU_CRR_CFG;
#endif
  EAXRAM();
  return (unsigned char)(r + kBanner[0]);
}

#define TEST_UART_PORT 3
#ifndef __cplusplus
// ASM-LABEL: <_bit_ops>:
// ASM-DAG: setb 0xb2
// ASM-DAG: cpl 0xb2
// ASM-DAG: clr 0x99
// ASM-DAG: setb 0xff
// ASM-DAG: cpl 0xc5
unsigned char bit_ops(void) {
  P32 = 1;
  P32 ^= 1;
  TI = 0;
  P77 = 1;
  STC32_GPIO_SET_PORT_MODE(TEST_UART_PORT,
                           STC32_PIN_MASK(STC32_P30) | STC32_PIN_MASK(STC32_P31),
                           STC32_GPIO_MODE_QUASI);
  STC32_GPIO_SET_MODE(TEST_LED, STC32_GPIO_MODE_PP);
  STC32_GPIO_TOGGLE(TEST_LED);
  STC32_GPIO_PORT_SET(TEST_UART_PORT, STC32_PIN_MASK(STC32_P31));
  STC32_GPIO_PORT_CLR(TEST_UART_PORT, STC32_PIN_MASK(STC32_P30));
  EAXSFR();
  STC32_GPIO_PULLUP_EN(TEST_LED);
  STC32_GPIO_SCHMITT_EN(TEST_LED);
  STC32_GPIO_FAST_SLEW(TEST_LED);
  STC32_GPIO_HIGH_DRIVE(TEST_LED);
  STC32_GPIO_DIGITAL_IN_EN(TEST_LED);
  EAXRAM();
  return (unsigned char)(STC32_GPIO_READ(TEST_LED) +
                         STC32_GPIO_PORT_READ(TEST_UART_PORT,
                                              STC32_PIN_MASK(STC32_P30)));
}
#else
unsigned char cxx_gpio_ops(void) {
  STC32_GPIO_SET_PORT_MODE(TEST_UART_PORT,
                           STC32_PIN_MASK(STC32_P30) | STC32_PIN_MASK(STC32_P31),
                           STC32_GPIO_MODE_QUASI);
  STC32_GPIO_SET_MODE(TEST_LED, STC32_GPIO_MODE_PP);
  STC32_GPIO_SET(TEST_LED);
  STC32_GPIO_CLR(TEST_LED);
  STC32_GPIO_TOGGLE(TEST_LED);
  STC32_GPIO_WRITE(TEST_LED, 1);
  STC32_GPIO_PORT_TOGGLE(TEST_UART_PORT, STC32_PIN_MASK(STC32_P31));
  EAXSFR();
  STC32_GPIO_PULLUP_DIS(TEST_LED);
  STC32_GPIO_SCHMITT_DIS(TEST_LED);
  STC32_GPIO_NORM_SLEW(TEST_LED);
  STC32_GPIO_NORM_DRIVE(TEST_LED);
  STC32_GPIO_DIGITAL_IN_DIS(TEST_LED);
  EAXRAM();
  return STC32_GPIO_READ(TEST_LED);
}
#endif

// ASM-LABEL: <_critical_and_nop>:
// ASM-DAG: jbc 0xaf
// ASM-DAG: nop
// ASM-DAG: setb 0xaf
unsigned int critical_and_nop(void) {
  unsigned char saved = stc32_ea_save_disable();
  _nop_();
  NOP4();
  gFixedBuf = gFixedBuf + 1u;
  unsigned int val = gFixedBuf;
  stc32_ea_restore(saved);
  return val;
}

float tfpu_smoke(float x) {
  return stc32_tfpu_add(stc32_tfpu_sin(x), 1.0f);
}

#if defined(STC32_ENABLE_KEIL_TYPES)
u16 keil_types_smoke(u8 a, u16 b) {
  return (u16)(a + b);
}
#endif

#if !defined(STC32_LINK_SMOKE)
void STC32_ISR_PRIO(TMR0_VECTOR, 2) tmr0_isr(void) {
  P4 ^= 0x01u;
}
#if defined(STC32G144K246)
void STC32_ISR(DMA_PWMER_VECTOR) dma_pwmer_isr(void) {
  P4 ^= 0x02u;
}
#endif
#endif

// ELF: Type: EXEC
// ELF: Flags: 0x102
// ELF-DAG: FUNC GLOBAL DEFAULT {{[0-9]+}} _main
// ELF-DAG: FUNC GLOBAL DEFAULT {{[0-9]+}} ___mcs251_isr_priority_init
int main(void) {
  stc32_isr_priority_init();
  unsigned char c = sfr_and_xfr_io(0x41u);
#ifndef __cplusplus
  bit_ops();
#endif
  unsigned int n = critical_and_nop();
  (void)tfpu_smoke(0.5f);
  return (int)(c + (unsigned char)n);
}
