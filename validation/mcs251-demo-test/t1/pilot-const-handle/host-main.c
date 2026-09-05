/*
 * Oracle-A host driver for pilot-const-handle (DESIGN.md section 2-T1).
 * Prints the same checkpoint stream as the firmware: 'B' + <tag><HEX8>*
 * + "PASS\n".  Expected values are read off this run and frozen into
 * wrapper.c's harness_check calls, then re-verified by the triangle diff.
 */
#include <stdio.h>
#include "kernel.c"

static void ck(char tag, u8 v)
{
    putchar(tag);
    printf("%02X", v);
}

int main(void)
{
    putchar('B');
    ck('a', alg_const_handle('e'));
    ck('b', alg_const_handle('p'));
    ck('c', alg_const_handle('x'));
    ck('d', alg_const_handle('y'));
    ck('E', alg_const_handle('z'));
    ck('f', alg_const_handle('A'));
    ck('g', alg_const_handle('q'));   /* plain letter: NO_CONST */
    ck('h', alg_const_handle('+'));   /* operator char: NO_CONST */
    ck('i', alg_const_handle('0'));   /* digit:         NO_CONST */
    ck('j', alg_const_handle('P'));   /* case-sensitive: NO_CONST */
    puts("PASS");
    return 0;
}
