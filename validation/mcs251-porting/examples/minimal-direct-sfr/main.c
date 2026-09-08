/*
 * Smallest currently portable official-style program:
 * direct SFR macros from the converted header plus Keil _nop_() compatibility.
 * QEMU's MCS251 test-port publishes each SBUF write on the serial transcript.
 */
#include "../../generated/stc32g-v1.h"
#include "../../include/intrins.h"

static void uart_puts(const char *text)
{
    while (*text) {
        SBUF = (unsigned char)*text++;
    }
}

int main(void)
{
    /* Three supported 0x80..0xff direct SFR lvalues; no bit/xdata/interrupt. */
    P0 = 0x00u;
    P1 = 0x00u;
    P4 = 0x00u;
    _nop_();
    uart_puts("PORTING-MINIMAL-PASS\n");

    for (;;) {
    }
}
