/*
 * modern-form.c - X4 e2e, modern-qualifier shapes (__xdata/__code, no
 * dialect flag).  Same coverage as keil-form.c with the C-compliant TR18037
 * spellings: initializer payload record, clear-only record, CODE ROM table,
 * cross-TU extern.
 */
#include "initbytes.h"

extern unsigned char __xdata ModExt[16];

unsigned char __xdata mbuf[32] = XDATA_E2E_MOD32_INIT;
unsigned char __xdata mzbuf[8];

char __code MODTAB[18] = {0xC9, 0xFE, 0x4C, 0x6F, 0x77, 0x2D, 0x66, 0x72,
                          0x6F, 0x6D, 0x2D, 0x49, 0x52, 0x2D, 0x74, 0x6F,
                          0x2D, 0x52};

unsigned modern_sum(unsigned i)
{
    return (unsigned)mbuf[i & 0x1F] + (unsigned)mzbuf[i & 0x07] +
           (unsigned)ModExt[i & 0x0F];
}

void modern_write(unsigned i, unsigned char v)
{
    mbuf[i & 0x1F] = v;
    mzbuf[i & 0x07] = (unsigned char)(v + 1);
}

char modern_code_read(unsigned i)
{
    return MODTAB[i & 17];
}
