typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;

extern u8 t2_sfr_uart_tx(void);
extern u8 t2_sfr_uart_scon(void);
void harness_check_u8(u8 expected, u8 got);
#define UART_PUTC(ch) do { SBUF = (u8)(ch); } while (0)
__sfr __at (0x99) SBUF;

#define MCS251_CHECKPOINTS() do { \
    UART_PUTC('u'); \
    harness_check_u8(0x50, t2_sfr_uart_tx()); \
    UART_PUTC('s'); \
    harness_check_u8(0x55, t2_sfr_uart_scon()); \
} while (0)
#include "../../../mcs251-firmware/harness-template.c"
