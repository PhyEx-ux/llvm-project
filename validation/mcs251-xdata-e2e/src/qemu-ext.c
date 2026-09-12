/*
 * qemu-ext.c - X4 e2e QEMU firmware cross-TU definition.  Its xdata_init
 * record travels in this object; the link merges it with qemu-fw.c's and the
 * CRT walker applies both (record order is link order, destinations are
 * disjoint - the lld gate guarantees both).
 */
#include "mcs251_type_compat.h"
#include "initbytes.h"

BYTE xdata ExtXbuf[16] = XDATA_E2E_FWEXT16_INIT;
