typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;

extern u8 t2_absent_s2con(void);
extern u8 t2_absent_s2buf(void);
extern u8 t2_absent_adc_contr(void);
extern u8 t2_absent_adc_result(void);
extern u8 t2_absent_adc_resl(void);
extern u8 t2_absent_wdt(void);
void harness_check_u8(u8 expected, u8 got);

#define MCS251_CHECKPOINTS() do { \
    UART_PUTC('a'); \
    harness_check_u8(0x00, t2_absent_s2con()); \
    UART_PUTC('1'); \
    harness_check_u8(0x00, t2_absent_s2buf()); \
    UART_PUTC('2'); \
    harness_check_u8(0x00, t2_absent_adc_contr()); \
    UART_PUTC('3'); \
    harness_check_u8(0x00, t2_absent_adc_result()); \
    UART_PUTC('4'); \
    harness_check_u8(0x00, t2_absent_adc_resl()); \
    UART_PUTC('5'); \
    harness_check_u8(0x00, t2_absent_wdt()); \
} while (0)
#include "../../../mcs251-firmware/harness-template.c"
