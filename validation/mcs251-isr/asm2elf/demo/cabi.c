/* cabi.c - independent ABI oracle for the asm -> C call in liba.asm.
 *
 * Compiled by clang/llc (not by hand) and linked next to the hand-written
 * objects.  This caller performs exactly the call liba.asm performs:
 *
 *     c_add(0x44332211, 0x33445566)
 *
 * so the compiler itself decides how arg1 reaches the static parameter
 * slot _c_add_PARM_2.  check.py decodes the slot stores this object
 * carries (anchored on its R_MCS251_LO8 relocations against
 * _c_add_PARM_2 plus llc's store form) and asserts they are identical to
 * liba.asm's hand-written stores and equal to the documented big-endian
 * layout of 0x33445566 (33 44 55 66 at ascending addresses,
 * MCS251ISelLowering.cpp:2529-2537).  That keeps the hand-written object
 * and the check.py expectation from ever agreeing on a wrong byte order
 * together: any such drift breaks against the compiler output first.
 *
 * cabi_call is never called; it exists so the ABI setup bytes survive
 * into the linked image for inspection.
 */
extern int c_add(int a, int b);

int cabi_call(void) { return c_add(0x44332211, 0x33445566); }
