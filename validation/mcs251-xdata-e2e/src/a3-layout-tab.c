/*
 * a3-layout-tab.c - the CODE-resident table for the R4/R9 layout acceptance.
 *
 * This object lives in its OWN translation unit (Alice review R9-2): if the
 * table and the comparison live in one TU, -O2 resolves the address and folds
 * the whole check into a relocation-constant expression, so the QEMU run no
 * longer measures a run-time conversion.  With the table here and only the
 * opaque accessor visible to a3-layout-fw.c, the converted address stays a
 * run-time value in the firmware TU.
 *
 * The accessor is deliberately non-inlinable across TUs (separate objects),
 * which is what keeps the address unknown at compile time.
 */
#include "mcs251_type_compat.h"

const BYTE code lay_tab[8] = {0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE};

const BYTE code *lay_base(void)
{
    return lay_tab;
}
