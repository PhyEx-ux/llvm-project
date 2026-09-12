/*
 * keil-extern.c - X4 e2e cross-TU definition TU (Keil dialect).  Defines the
 * object the extern declaration in keil-form.c refers to; its initializer
 * produces an xdata_init record in THIS object, so the link merges records
 * from two objects and the walker applies both.
 */
#include "mcs251_type_compat.h"
#include "initbytes.h"

BYTE xdata ExtXbuf[16] = XDATA_E2E_EXT16_INIT;
