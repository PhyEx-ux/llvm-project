/*
 * keil-form.c - X4 e2e, official Keil-dialect source shapes (non-USB demo
 * class: flash / UART-DMA buffer usage).  Compiled with -fmcs251-keil so the
 * bare `xdata`/`code` storage keywords carry the X1 semantics; the BYTE
 * typedef comes from the BT06 X4 type compat layer
 * (validation/mcs251-dialect/include/mcs251_type_compat.h).
 *
 * Shapes covered (XDATA-CODE-SLICE-TASK.md X4 row):
 *   BYTE xdata buf[256] with an initializer -> .mcs251.XSEG.buf +
 *     .mcs251.xdata_init payload record (consumed by the X4 CRT walker);
 *   BYTE xdata zbuf[64] without an initializer -> clear-only record;
 *   char code DEVICEDESC[18] -> CODE-space read-only image (ROM table);
 *   extern BYTE xdata ExtXbuf[16] -> cross-TU placement: this TU emits no
 *     storage for it, keil-extern.c defines it (with its own record).
 */
#include "mcs251_type_compat.h"
#include "initbytes.h"

extern BYTE xdata ExtXbuf[16];

BYTE xdata buf[256] = XDATA_E2E_BUF256_INIT;
BYTE xdata zbuf[64];

char code DEVICEDESC[18] = {0x12, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40,
                            0x34, 0x12, 0xEF, 0xCD, 0x01, 0x01, 0x00, 0x00,
                            0x00, 0x00};

/* Runtime accessors: each lowers to the X2 full-24-bit MOVX/DR sequences
 * (DPXL reload before every movx; never a bare 16-bit access). */
unsigned xdata_sum(unsigned i)
{
    return (unsigned)buf[i & 0xFF] + (unsigned)zbuf[i & 0x3F] +
           (unsigned)ExtXbuf[i & 0x0F];
}

void xdata_write(unsigned i, BYTE v)
{
    buf[i & 0xFF] = v;
    zbuf[i & 0x3F] = (BYTE)(v ^ 0xFF);
}

char code_read(unsigned i)
{
    return DEVICEDESC[i & 17];
}
