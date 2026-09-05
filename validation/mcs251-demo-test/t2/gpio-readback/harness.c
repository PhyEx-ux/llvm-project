typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;

extern u8 t2_gpio_input(void);
extern u8 t2_gpio_p0_pushpull(void);
extern u8 t2_gpio_p1_pushpull(void);
void harness_check_u8(u8 expected, u8 got);

#define MCS251_CHECKPOINTS() do { \
    UART_PUTC('g'); \
    harness_check_u8(0xff, t2_gpio_input()); \
    UART_PUTC('p'); \
    harness_check_u8(0x5a, t2_gpio_p0_pushpull()); \
    UART_PUTC('1'); \
    harness_check_u8(0xc3, t2_gpio_p1_pushpull()); \
} while (0)
#include "../../../mcs251-firmware/harness-template.c"
