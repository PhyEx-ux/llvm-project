/*
 * mcs251-as4-cross-tu-callee.c - definition TU for the A2c cross-TU test.
 *
 * Defines the CODE-resident table and the callee entry points declared in
 * mcs251-as4-cross-tu.h.  Compiled separately from the caller (see the RUN
 * lines in mcs251-as4-cross-tu.c); the two objects are compared field by
 * field (address space, const, signature) by
 * Inputs/check-as4-cross-tu-signatures.py.
 *
 * No __code write appears here: the object is read-only by construction.
 */
#include "mcs251-as4-cross-tu.h"

const uint8 __code shared_rom[8] = {10, 20, 30, 40, 50, 60, 70, 80};

const uint8 __code *code_lookup(unsigned idx)
{
    return &shared_rom[idx & 7u];
}

uint8 code_read(const uint8 __code *p, unsigned i)
{
    return p[i & 7u];
}

const uint8 __code *code_base(void)
{
    return shared_rom;
}

/* The generic entry point reads through an ordinary AS0 pointer: a converted
 * CODE source arrives here as the same value (the representation contract is
 * pinned by code-addrspacecast-datalayout.ll and the alias behaviour by
 * code-addrspacecast-alias.ll). */
uint8 generic_read(const uint8 *p, unsigned i)
{
    return p[i & 7u];
}
