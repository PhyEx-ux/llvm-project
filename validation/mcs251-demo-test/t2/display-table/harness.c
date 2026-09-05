typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;

extern u8 t2_display_table(void);
extern void t2_display_emit(void);
void harness_check_u8(u8 expected, u8 got);

#define MCS251_CHECKPOINTS() do { \
    UART_PUTC('d'); \
    t2_display_emit(); \
    UART_PUTC('c'); \
    harness_check_u8(0x05, t2_display_table()); \
} while (0)
#include "../../../mcs251-firmware/harness-template.c"
