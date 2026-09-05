typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;

extern u8 t2_uart_echo_poll(void);
void harness_check_u8(u8 expected, u8 got);

#define MCS251_CHECKPOINTS() do { \
    UART_PUTC('e'); \
    harness_check_u8(0x5a, t2_uart_echo_poll()); \
} while (0)
#include "../../../mcs251-firmware/harness-template.c"
